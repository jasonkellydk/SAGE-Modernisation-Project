export module engine.net.lockstep.lockstep_server;
import std;

export import engine.net.lockstep.protocol;
export import engine.net.lockstep.command_log;
export import engine.net.lockstep.hash_vote;
export import engine.net.lockstep.input_delay;
export import engine.net.transport.connection;

// The relay every match runs through, single player included (as a hidden
// in-process relay). It never simulates, so it is cheap to host anywhere: in
// the host's process, on a LAN host, or on a central relay.
//
//   sequencing  collects every player's input frame per tick and releases
//               the tick's bundle (commands by seat, then submission order)
//               once all connected players' frames are in; logs it
//   liveliness  pings everyone; a player silent past `dropAfter` is dropped
//               (its input counts as empty) and may rejoin its seat
//   verifying   votes on reported state hashes; the minority diverged
//   repairing   asks a healthy player for a checkpoint and sends it, with the
//               ticks since, to whoever needs state: diverged players,
//               rejoining players and late observers
//   input delay adapts to the players' round trips
export namespace engine::net
{
struct ServerOptions
{
	std::uint32_t players{1};
	std::uint64_t seed{0};
	// Ticks between a command being issued and executed (network headroom).
	std::uint32_t inputDelay{2};
	// Peers report their state hash every this many ticks.
	std::uint32_t hashInterval{30};
	bool adaptiveDelay{true};
	InputDelaySettings delay{};
	std::chrono::microseconds pingInterval{std::chrono::milliseconds(500)};
	std::chrono::microseconds dropAfter{std::chrono::seconds(10)};
	// A checkpoint not delivered in this time is asked of another player.
	std::chrono::microseconds checkpointWait{std::chrono::seconds(5)};
	// A game resumed (a saved game loaded): the ticks up to this one are done, the first released is the next.
	std::uint64_t startTick{0};
};

struct RelayStats
{
	std::uint64_t desyncs{0};
	std::uint64_t repairs{0};
	std::uint64_t drops{0};
	std::uint64_t rejoins{0};
	std::uint64_t observersJoined{0};
};

class LockstepServer
{
public:
	explicit LockstepServer(ServerOptions options) :
		m_options(options), m_delay(options.delay, std::clamp(options.inputDelay, options.delay.minimum, options.delay.maximum)),
		m_released(options.startTick)
	{
	}

	// Takes a joining connection; it is seated once it says hello.
	void Accept(std::unique_ptr<Connection> connection) { m_pending.push_back(std::move(connection)); }

	std::size_t SeatedPlayers() const noexcept { return m_players.size(); }
	std::size_t ConnectedPlayers() const
	{
		return static_cast<std::size_t>(std::count_if(m_players.begin(), m_players.end(), [](const Seat &seat) { return seat.Connected(); }));
	}
	std::size_t Observers() const noexcept { return m_observers.size(); }
	std::uint64_t ReleasedTick() const noexcept { return m_released; }
	// Every seat taken: the game under way (ticks release from now on).
	bool EverySeatTaken() const noexcept { return Started(); }
	std::uint32_t InputDelay() const noexcept { return m_delay.Delay(); }
	const std::vector<std::string> &Desyncs() const noexcept { return m_desyncs; }
	const RelayStats &Stats() const noexcept { return m_stats; }
	const CommandLog &Log() const noexcept { return m_log; }

	// Handles everything that arrived, releases every tick now complete and
	// runs repairs and timeouts. `now` is any monotonic clock (tests pass their own).
	void Update(std::chrono::microseconds now)
	{
		m_now = now;
		SeatNewcomers();
		for (std::uint32_t seat = 0; seat < m_players.size(); ++seat)
			if (m_players[seat].Connected())
				while (auto message = m_players[seat].connection->Receive())
					HandlePlayer(seat, *message);
		for (std::uint32_t observer = 0; observer < m_observers.size(); ++observer)
			if (m_observers[observer].Connected())
				while (auto message = m_observers[observer].connection->Receive())
					HandleObserver(observer, *message);
		DropSilent();
		Release();
		Repair();
		Ping();
	}

	void Update() { Update(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())); }

private:
	struct Seat
	{
		std::unique_ptr<Connection> connection;
		std::chrono::microseconds lastHeard{0};
		std::chrono::microseconds lastPing{0};
		// Needs state from a checkpoint before it can take part (joined late or diverged).
		bool awaitingState{false};
		// Its hash reports up to here predate its last resync: ignored.
		std::uint64_t resyncFloor{0};
		// Its input through this tick is empty (while it was away or catching up).
		std::uint64_t emptyThrough{0};
		// Someone has sat in it (a seat claimed ahead of the game's start stays open until its player comes).
		bool taken{false};

		bool Connected() const noexcept { return connection != nullptr && connection->Open(); }
	};

	// A repair target: a player seat or an observer.
	struct Target
	{
		Role role{Role::Player};
		std::uint32_t seat{0};
		auto operator<=>(const Target &) const = default;
	};

	// Every seat taken (a claimed seat counts once its player came).
	bool Started() const noexcept
	{
		return m_players.size() == m_options.players && std::all_of(m_players.begin(), m_players.end(), [](const Seat &seat) { return seat.taken; });
	}

	Welcome WelcomeFor(Role role, std::uint32_t seat, bool awaitState, std::uint64_t firstInput) const
	{
		return Welcome{role, seat, m_options.players, m_options.seed, m_delay.Delay(), m_options.hashInterval, firstInput, awaitState};
	}

	void SeatNewcomers()
	{
		for (auto it = m_pending.begin(); it != m_pending.end();)
		{
			auto message = (*it)->Receive();
			if (!message)
			{
				it = (*it)->Open() ? std::next(it) : m_pending.erase(it);
				continue;
			}
			const auto hello = TypeOf(*message) == MessageType::Hello ? DecodeHello(*message) : std::nullopt;
			if (hello)
				TakeSeat(std::move(*it), *hello);
			it = m_pending.erase(it);
		}
	}

	void TakeSeat(std::unique_ptr<Connection> connection, const Hello &hello)
	{
		// (A resumed game is under way once it has gone past the tick it resumed on.)
		const bool underway = m_released > m_options.startTick;
		if (hello.role == Role::Observer)
		{
			const auto observer = static_cast<std::uint32_t>(m_observers.size());
			connection->Send(Encode(WelcomeFor(Role::Observer, observer, underway, 1)));
			m_observers.push_back({std::move(connection), m_now});
			++m_stats.observersJoined;
			if (underway)
				NeedsState({Role::Observer, observer});
			return;
		}
		if (hello.seat == NoSeat)
		{
			if (Started() || underway)
			{
				connection->Close(); // no seat left
				return;
			}
			// The first seat no one has taken.
			std::uint32_t seat = 0;
			while (seat < m_players.size() && m_players[seat].taken)
				++seat;
			connection->Send(Encode(WelcomeFor(Role::Player, seat, false, m_options.startTick + 1)));
			if (seat == m_players.size())
				m_players.emplace_back();
			m_players[seat] = {std::move(connection), m_now};
			m_players[seat].taken = true;
			return;
		}
		// A seat claimed before the game starts (a LAN game's players take their slots' seats): if no one has it.
		if (!Started() && !underway && hello.seat < m_options.players && (hello.seat >= m_players.size() || !m_players[hello.seat].taken))
		{
			if (hello.seat >= m_players.size())
				m_players.resize(hello.seat + 1);
			connection->Send(Encode(WelcomeFor(Role::Player, hello.seat, false, m_options.startTick + 1)));
			m_players[hello.seat] = {std::move(connection), m_now};
			m_players[hello.seat].taken = true;
			return;
		}
		// Rejoining a seat left empty: its input stays empty until it has caught up.
		if (hello.seat >= m_players.size() || m_players[hello.seat].Connected())
		{
			connection->Close();
			return;
		}
		Seat &seat = m_players[hello.seat];
		const std::uint64_t firstInput = std::max(seat.emptyThrough, LatestInputTick()) + 1;
		seat = {std::move(connection), m_now};
		seat.taken = true;
		seat.emptyThrough = firstInput - 1;
		seat.connection->Send(Encode(WelcomeFor(Role::Player, hello.seat, underway, firstInput)));
		++m_stats.rejoins;
		if (underway)
			NeedsState({Role::Player, hello.seat});
	}

	// The last tick any player's input has been given for.
	std::uint64_t LatestInputTick() const
	{
		const std::uint64_t buffered = m_frames.empty() ? 0 : m_frames.rbegin()->first;
		return std::max({m_released + m_delay.Delay(), buffered});
	}

	void HandlePlayer(std::uint32_t seat, const std::vector<std::byte> &message)
	{
		Seat &player = m_players[seat];
		player.lastHeard = m_now;
		switch (TypeOf(message).value_or(MessageType::Hello))
		{
		case MessageType::InputFrame:
			if (auto frame = DecodeFrame(message); frame && frame->tick > m_released && frame->tick > player.emptyThrough)
			{
				for (CommandEnvelope &command : frame->commands)
					command.player = seat; // the seat, never what the client claims
				m_frames[frame->tick].emplace(seat, std::move(frame->commands));
			}
			break;
		case MessageType::StateHash:
			if (const auto report = DecodeHash(message); report && !player.awaitingState && report->tick > player.resyncFloor)
				if (auto verdict = m_votes.Report(report->tick, seat, report->hash, Voters()))
					Judge(*verdict);
			break;
		case MessageType::Checkpoint:
			if (auto checkpoint = DecodeCheckpoint(message); checkpoint && m_donor == seat)
				Deliver(std::move(*checkpoint));
			break;
		case MessageType::Pong:
			if (const auto sent = DecodeValue(message); sent && m_options.adaptiveDelay && m_now.count() >= static_cast<std::int64_t>(*sent))
			{
				const std::uint32_t before = m_delay.Delay();
				const std::uint32_t after = m_delay.Sample(seat, m_now - std::chrono::microseconds(static_cast<std::int64_t>(*sent)));
				if (after != before)
					Broadcast(Encode(MessageType::InputDelay, after));
			}
			break;
		default:
			break;
		}
	}

	void HandleObserver(std::uint32_t observer, const std::vector<std::byte> &message)
	{
		Seat &seat = m_observers[observer];
		seat.lastHeard = m_now;
		if (TypeOf(message) != MessageType::StateHash || seat.awaitingState)
			return;
		// Observers do not vote; one that disagrees with the players is resynced.
		if (const auto report = DecodeHash(message); report && report->tick > seat.resyncFloor)
			if (const auto agreed = m_votes.Agreed(report->tick); agreed && *agreed != report->hash)
			{
				seat.connection->Send(Encode(MessageType::Desync, *report));
				NeedsState({Role::Observer, observer});
			}
	}

	std::set<std::uint32_t> Voters() const
	{
		std::set<std::uint32_t> voters;
		for (std::uint32_t seat = 0; seat < m_players.size(); ++seat)
			if (m_players[seat].Connected() && !m_players[seat].awaitingState)
				voters.insert(seat);
		return voters;
	}

	void Judge(const HashVerdict &verdict)
	{
		for (const std::uint32_t seat : verdict.diverged)
		{
			m_desyncs.push_back("tick " + std::to_string(verdict.tick) + ": player " + std::to_string(seat) + " diverged");
			++m_stats.desyncs;
			m_players[seat].connection->Send(Encode(MessageType::Desync, StateHashReport{verdict.tick, verdict.agreed}));
			NeedsState({Role::Player, seat});
		}
	}

	void NeedsState(Target target)
	{
		SeatOf(target).awaitingState = true;
		m_targets.insert(target);
	}

	Seat &SeatOf(const Target &target) { return target.role == Role::Player ? m_players[target.seat] : m_observers[target.seat]; }

	// Players silent too long are dropped: their input counts as empty from
	// then on (so the others play on) and their seat waits for a rejoin.
	void DropSilent()
	{
		bool dropped = false;
		for (std::uint32_t index = 0; index < m_players.size(); ++index)
		{
			Seat &seat = m_players[index];
			if (seat.connection == nullptr || (seat.Connected() && m_now - seat.lastHeard <= m_options.dropAfter))
				continue;
			seat.connection->Close();
			seat.connection.reset();
			seat.awaitingState = false;
			m_targets.erase({Role::Player, index});
			m_delay.Forget(index);
			if (m_donor == index)
				m_donor.reset();
			++m_stats.drops;
			dropped = true;
		}
		for (std::uint32_t index = 0; index < m_observers.size(); ++index)
		{
			Seat &seat = m_observers[index];
			if (seat.connection != nullptr && !seat.Connected())
			{
				seat.connection.reset();
				m_targets.erase({Role::Observer, index});
			}
		}
		// Votes waiting on a dropped player decide without it.
		if (dropped)
			for (const std::uint64_t tick : m_votes.PendingTicks())
				if (auto verdict = m_votes.Close(tick, Voters()))
					Judge(*verdict);
	}

	// Ticks release in order; a tick waits for every connected player's frame
	// (a seat away or catching up counts as empty).
	void Release()
	{
		// Nobody left to play: time stands still until someone rejoins.
		if (!Started() || ConnectedPlayers() == 0)
			return;
		for (;;)
		{
			const std::uint64_t tick = m_released + 1;
			auto &frames = m_frames[tick];
			for (std::uint32_t seat = 0; seat < m_players.size(); ++seat)
				if (!frames.contains(seat) && m_players[seat].Connected() && tick > m_players[seat].emptyThrough)
					return;
			std::vector<CommandEnvelope> commands;
			for (auto &[seat, frame] : frames) // map: ordered by seat
				for (CommandEnvelope &command : frame)
					commands.push_back(std::move(command));
			std::vector<std::byte> bundle = Encode(MessageType::TickBundle, tick, commands);
			for (Seat &seat : m_players)
				if (seat.Connected() && !seat.awaitingState)
					seat.connection->Send(bundle);
			for (Seat &seat : m_observers)
				if (seat.Connected() && !seat.awaitingState)
					seat.connection->Send(bundle);
			m_log.Append(tick, std::move(bundle));
			m_released = tick;
			m_frames.erase(tick);
		}
	}

	// One checkpoint at a time, from the lowest healthy player.
	void Repair()
	{
		if (m_targets.empty())
			return;
		if (m_donor && m_now - m_askedAt <= m_options.checkpointWait)
			return;
		if (m_donor)
			m_refused.insert(*m_donor); // too slow: ask someone else
		m_donor.reset();
		for (std::uint32_t seat = 0; seat < m_players.size(); ++seat)
			if (m_players[seat].Connected() && !m_players[seat].awaitingState && !m_refused.contains(seat))
			{
				m_donor = seat;
				m_askedAt = m_now;
				m_players[seat].connection->Send(Encode(MessageType::CheckpointRequest, m_released));
				return;
			}
		m_refused.clear(); // everyone tried: start over next time
	}

	void Deliver(CheckpointData checkpoint)
	{
		const std::uint32_t donor = *m_donor;
		m_donor.reset();
		// A checkpoint must match what the players agreed on for its tick.
		if (const auto agreed = m_votes.Agreed(checkpoint.tick); (agreed && *agreed != checkpoint.hash) || checkpoint.tick > m_released)
		{
			m_refused.insert(donor);
			return;
		}
		m_refused.clear();
		const std::vector<std::byte> resync = Encode(MessageType::Resync, checkpoint);
		for (const Target &target : m_targets)
		{
			Seat &seat = SeatOf(target);
			if (!seat.Connected())
				continue;
			seat.connection->Send(resync);
			m_log.ForEachAfter(checkpoint.tick, [&](std::uint64_t, const std::vector<std::byte> &bundle) { seat.connection->Send(bundle); });
			seat.awaitingState = false;
			seat.resyncFloor = m_released;
			++m_stats.repairs;
		}
		m_targets.clear();
	}

	void Ping()
	{
		const auto ping = [&](Seat &seat) {
			if (seat.Connected() && m_now - seat.lastPing >= m_options.pingInterval)
			{
				seat.connection->Send(Encode(MessageType::Ping, static_cast<std::uint64_t>(m_now.count())));
				seat.lastPing = m_now;
			}
		};
		for (Seat &seat : m_players)
			ping(seat);
		for (Seat &seat : m_observers)
			ping(seat);
	}

	void Broadcast(const std::vector<std::byte> &message)
	{
		for (Seat &seat : m_players)
			if (seat.Connected())
				seat.connection->Send(message);
		for (Seat &seat : m_observers)
			if (seat.Connected())
				seat.connection->Send(message);
	}

	ServerOptions m_options;
	InputDelayPolicy m_delay;
	std::chrono::microseconds m_now{0};
	std::vector<std::unique_ptr<Connection>> m_pending;
	std::vector<Seat> m_players;
	std::vector<Seat> m_observers;
	std::map<std::uint64_t, std::map<std::uint32_t, std::vector<CommandEnvelope>>> m_frames;
	CommandLog m_log;
	HashVote m_votes;
	std::set<Target> m_targets;
	std::optional<std::uint32_t> m_donor;
	std::set<std::uint32_t> m_refused;
	std::chrono::microseconds m_askedAt{0};
	std::vector<std::string> m_desyncs;
	RelayStats m_stats;
	std::uint64_t m_released{0};
};
}
