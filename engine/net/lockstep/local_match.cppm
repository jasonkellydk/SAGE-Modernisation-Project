export module engine.net.lockstep.local_match;
import std;

export import engine.net.lockstep.lockstep_server;
export import engine.net.lockstep.lockstep_peer;

// Single player as a match of one: the relay runs in-process and the player
// is its only peer, over loopback. The game takes exactly the multiplayer
// path (commands on the bus, bundles, hashes); with no network there is no
// input delay, so a command runs on the very next tick.
export namespace engine::net
{
class LocalMatch
{
public:
	// `startTick`: a game resumed from a checkpoint taken after that tick.
	explicit LocalMatch(std::uint64_t seed, std::uint64_t startTick = 0) : m_server(Options(seed, startTick))
	{
		auto [client, host] = MakeLoopback();
		m_server.Accept(std::move(host));
		m_client = std::make_unique<LockstepClient>(std::move(client));
		m_peer = std::make_unique<LockstepPeer>(*m_client);
		m_server.Update();
		m_client->Update();
	}

	void Submit(std::uint64_t type, std::vector<std::byte> payload) { m_client->Submit(type, std::move(payload)); }

	// Runs the simulation's next tick (up to `maxTicks`); returns the ticks run.
	std::size_t Advance(LockstepSimulation &simulation, std::size_t maxTicks = 1)
	{
		std::size_t ran = 0;
		while (ran < maxTicks)
		{
			m_client->Update();
			m_client->CloseInputFor(simulation.CurrentTick() + 1);
			m_server.Update();
			const std::size_t step = m_peer->Advance(simulation, 1);
			if (step == 0)
				break;
			ran += step;
		}
		return ran;
	}

	const LockstepServer &Relay() const noexcept { return m_server; }

private:
	static ServerOptions Options(std::uint64_t seed, std::uint64_t startTick)
	{
		ServerOptions options;
		options.players = 1;
		options.seed = seed;
		options.startTick = startTick;
		options.inputDelay = 0;
		options.adaptiveDelay = false;
		options.delay.minimum = 0;
		// Nobody to compare with; hashing would only cost time.
		options.hashInterval = 0;
		// Its only player is in this process: pausing (a debugger, loading) is not leaving.
		options.dropAfter = std::chrono::hours(24 * 365);
		return options;
	}

	LockstepServer m_server;
	std::unique_ptr<LockstepClient> m_client;
	std::unique_ptr<LockstepPeer> m_peer;
};
}
