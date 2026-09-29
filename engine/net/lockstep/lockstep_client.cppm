export module engine.net.lockstep.lockstep_client;
import std;

export import engine.net.lockstep.protocol;
export import engine.net.transport.connection;

// A peer's side of the protocol. Commands submitted now go into the input
// frame of tick (next tick + input delay); tick N may run once N's bundle
// (every player's commands) has arrived. Answers pings, takes input delay
// changes, and holds the relay's checkpoint requests and resyncs for the
// peer driving the simulation.
export namespace engine::net
{
class LockstepClient
{
public:
	explicit LockstepClient(std::unique_ptr<Connection> connection, Hello hello = {}) : m_connection(std::move(connection))
	{
		m_connection->Send(Encode(hello));
	}

	// Handles what arrived.
	void Update()
	{
		while (auto message = m_connection->Receive())
		{
			switch (TypeOf(*message).value_or(MessageType::Hello))
			{
			case MessageType::Welcome:
				if (const auto welcome = DecodeWelcome(*message))
				{
					m_welcome = *welcome;
					m_delay = welcome->inputDelay;
					m_awaitingState = welcome->awaitResync;
					// The first ticks of our input have no earlier commands to carry: close them empty.
					m_lastClosed = welcome->firstInputTick - 1;
					if (IsPlayer())
						for (std::uint64_t tick = welcome->firstInputTick; tick < welcome->firstInputTick + m_delay; ++tick)
							SendFrame(tick);
				}
				break;
			case MessageType::TickBundle:
				if (auto bundle = DecodeFrame(*message))
					m_bundles.emplace(bundle->tick, std::move(*bundle));
				break;
			case MessageType::Desync:
				m_desynced = true;
				break;
			case MessageType::CheckpointRequest:
				m_checkpointWanted = true;
				break;
			case MessageType::Resync:
				if (auto checkpoint = DecodeCheckpoint(*message))
				{
					// What follows is the log after the checkpoint: earlier bundles are moot.
					m_bundles.clear();
					m_resync = std::move(*checkpoint);
				}
				break;
			case MessageType::Ping:
				if (const auto sent = DecodeValue(*message))
					m_connection->Send(Encode(MessageType::Pong, *sent));
				break;
			case MessageType::InputDelay:
				if (const auto delay = DecodeValue(*message))
					m_delay = static_cast<std::uint32_t>(*delay);
				break;
			default:
				break;
			}
		}
	}

	bool Joined() const noexcept { return m_welcome.has_value(); }
	bool Connected() const noexcept { return m_connection->Open(); }
	const std::optional<Welcome> &Seat() const noexcept { return m_welcome; }
	bool IsPlayer() const noexcept { return m_welcome && m_welcome->role == Role::Player; }
	std::uint32_t InputDelay() const noexcept { return m_delay; }
	bool Desynced() const noexcept { return m_desynced; }
	// Joined a match under way (or diverged): the state must come from a resync first.
	bool AwaitingState() const noexcept { return m_awaitingState; }

	void Submit(std::uint64_t type, std::vector<std::byte> payload) { m_pending.push_back({type, 0, std::move(payload)}); }

	// Before running `tick`: closes this player's input through tick + delay
	// (commands submitted since go into the first frame closed), so the
	// pipeline stays full, also across delay changes.
	void CloseInputFor(std::uint64_t tick)
	{
		if (!IsPlayer())
			return;
		for (std::uint64_t target = m_lastClosed + 1; target <= tick + m_delay; ++target)
			SendFrame(target);
	}

	// The bundle for `tick` once it has arrived (taken out).
	std::optional<TickBundle> Take(std::uint64_t tick)
	{
		const auto found = m_bundles.find(tick);
		if (found == m_bundles.end())
			return std::nullopt;
		TickBundle bundle = std::move(found->second);
		m_bundles.erase(found);
		return bundle;
	}

	std::size_t BundlesWaiting() const noexcept { return m_bundles.size(); }

	void ReportHash(std::uint64_t tick, std::uint64_t hash) { m_connection->Send(Encode(MessageType::StateHash, StateHashReport{tick, hash})); }

	// The relay wants this peer's state (for someone else's repair).
	bool CheckpointWanted() const noexcept { return m_checkpointWanted; }
	void ProvideCheckpoint(const CheckpointData &checkpoint)
	{
		m_connection->Send(Encode(MessageType::Checkpoint, checkpoint));
		m_checkpointWanted = false;
	}

	// State to restore before running on (after joining late or diverging).
	std::optional<CheckpointData> TakeResync()
	{
		std::optional<CheckpointData> resync = std::move(m_resync);
		m_resync.reset();
		return resync;
	}

	// The resync's state is in place: run on (a refused one leaves the peer waiting).
	void StateRestored() noexcept
	{
		m_awaitingState = false;
		m_desynced = false;
	}

private:
	void SendFrame(std::uint64_t tick)
	{
		m_connection->Send(Encode(MessageType::InputFrame, tick, m_pending));
		m_pending.clear();
		m_lastClosed = std::max(m_lastClosed, tick);
	}

	std::unique_ptr<Connection> m_connection;
	std::optional<Welcome> m_welcome;
	std::uint32_t m_delay{0};
	std::vector<CommandEnvelope> m_pending;
	std::map<std::uint64_t, TickBundle> m_bundles;
	std::optional<CheckpointData> m_resync;
	std::uint64_t m_lastClosed{0};
	bool m_desynced{false};
	bool m_awaitingState{false};
	bool m_checkpointWanted{false};
};
}
