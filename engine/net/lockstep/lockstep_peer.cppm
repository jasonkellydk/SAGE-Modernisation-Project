export module engine.net.lockstep.lockstep_peer;
import std;

export import engine.net.lockstep.lockstep_client;

// Drives a deterministic simulation from the relay's bundles and keeps it
// honest: reports its state hash on the agreed ticks, serves checkpoints
// when the relay asks (for someone else's repair), and restores the state
// the relay sends (after joining late, rejoining, or diverging) before
// running on. The game supplies the simulation; this is the same for every
// game and for single player (through the in-process relay).
export namespace engine::net
{
class LockstepSimulation
{
public:
	virtual ~LockstepSimulation() = default;
	// Runs the next tick with its bundle's commands.
	virtual void Step(std::span<const CommandEnvelope> commands) = 0;
	// The last tick run (0 before the first).
	virtual std::uint64_t CurrentTick() const = 0;
	virtual std::uint64_t StateHash() const = 0;
	virtual std::vector<std::byte> SaveCheckpoint() const = 0;
	// Replaces the state with a checkpoint's; false (state unchanged) if it does not fit.
	virtual bool LoadCheckpoint(std::span<const std::byte> state) = 0;
};

struct PeerStats
{
	std::uint64_t restored{0};
	std::uint64_t refusedCheckpoints{0};
	std::uint64_t checkpointsServed{0};
};

class LockstepPeer
{
public:
	explicit LockstepPeer(LockstepClient &client) : m_client(client) {}

	// Handles the relay and runs up to `maxTicks` ticks whose bundles are in
	// (1 at the game's pace; more to catch up). Returns the ticks run.
	std::size_t Advance(LockstepSimulation &simulation, std::size_t maxTicks = 1)
	{
		m_client.Update();
		if (auto resync = m_client.TakeResync())
		{
			// Only a checkpoint that reproduces the relay's hash is taken.
			if (simulation.LoadCheckpoint(resync->state) && simulation.CurrentTick() == resync->tick && simulation.StateHash() == resync->hash)
			{
				m_client.StateRestored();
				++m_stats.restored;
			}
			else
				++m_stats.refusedCheckpoints;
		}
		if (!m_client.Joined() || m_client.AwaitingState())
			return 0;
		std::size_t ran = 0;
		while (ran < maxTicks)
		{
			const std::uint64_t next = simulation.CurrentTick() + 1;
			m_client.CloseInputFor(next);
			const auto bundle = m_client.Take(next);
			if (!bundle)
				break;
			simulation.Step(bundle->commands);
			++ran;
			const std::uint32_t interval = m_client.Seat()->hashInterval;
			if (interval != 0 && next % interval == 0)
				m_client.ReportHash(next, simulation.StateHash());
		}
		if (m_client.CheckpointWanted())
		{
			m_client.ProvideCheckpoint({simulation.CurrentTick(), simulation.StateHash(), simulation.SaveCheckpoint()});
			++m_stats.checkpointsServed;
		}
		return ran;
	}

	LockstepClient &Client() noexcept { return m_client; }
	const PeerStats &Stats() const noexcept { return m_stats; }

private:
	LockstepClient &m_client;
	PeerStats m_stats;
};
}
