export module engine.gameplay.rts.economy.components.overcharge;
import std;

export import Engine.Core.Math.Fixed;
import engine.ecs.core.component_registry;

// A producer that can be overcharged (the original's OverchargeBehavior): while `active` its EnergyBonus counts (one of
// its EnergySource bonus sources) and it drains `drain` percent of its most health a second
// (HealthPercentToDrainPerSecond, in percent points: exact in fixed point), a second's ticks' share each tick, from the tick after it was switched on (`since`: setWakeFrame(UPDATE_SLEEP_NONE)); it switches
// itself off once its health is below `floor` of its most (NotAllowedWhenHealthBelowPercent). `retract`: switched off
// since the game last drew in its control rods (PowerPlantUpdate::extendRods(FALSE)). Simulation state: checkpointed.
export namespace engine::gameplay
{
struct Overcharge
{
	Engine::Math::Fixed drain;
	Engine::Math::Fixed floor;
	std::uint64_t since{0};
	std::uint8_t active{0};
	std::uint8_t retract{0};
	std::uint8_t reserved[6]{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Overcharge>
{
	static constexpr std::string_view StableName = "engine.gameplay.overcharge";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
