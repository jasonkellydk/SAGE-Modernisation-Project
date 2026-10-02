export module engine.gameplay.rts.production.components.production_exit_gate;
import std;

export import engine.ecs.core.component_registry;

// A factory exit that lets its units out one at a time (the original's QueueProductionExitUpdate): after one goes out
// the next waits `delayTicks` (ExitDelay), except while its `burst` (InitialBurst) lasts, each exit using one up.
// Units done and not yet out wait in their production entry (ProductionUpdate keeps trying every frame).
export namespace engine::gameplay
{
struct ProductionExitGate
{
	std::uint64_t delayTicks{0};
	std::uint64_t readyTick{0}; // free from this tick (m_currentDelay counted down to 0)
	std::uint32_t burst{0};
	std::uint32_t reserved{0};

	// isFreeToExit.
	bool Free(std::uint64_t tick) const noexcept { return burst > 0 || tick >= readyTick; }

	// exitObjectViaDoor: m_currentDelay = ExitDelay, one fewer to burst.
	void Exited(std::uint64_t tick) noexcept
	{
		readyTick = tick + delayTicks;
		if (burst > 0)
			--burst;
	}
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::ProductionExitGate>
{
	static constexpr std::string_view StableName = "engine.gameplay.production_exit_gate";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
