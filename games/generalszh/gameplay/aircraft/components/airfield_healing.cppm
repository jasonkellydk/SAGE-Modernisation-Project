export module games.generalszh.gameplay.aircraft.components.airfield_healing;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// An airfield's repair of its parked jets (ParkingPlaceBehavior HealAmountPerSecond): each jet of its own that stands idle
// or reloading on the ground is a healee (JetAIUpdate's setHealee each update); a healee coming or going puts the next heal
// HEAL_RATE_FRAMES (6) off (resetWakeFrame; none left: never); each heal gives every healee 6 x HealAmountPerSecond / 30.
// `healing`: which of its spaces' jets were healees last tick. Simulation state: checkpointed.
// AirfieldHeals: the tick's heals, carried out after the step.
export namespace generalszh::gameplay
{
struct AirfieldHealing
{
	static constexpr std::uint64_t Forever = 0x3FFFFFFFFFFFFFFFull;
	Engine::Math::Fixed perSecond;
	std::uint64_t nextHeal{Forever};
	std::uint32_t healing{0};
	std::uint32_t reserved{0};
};

struct AirfieldHeal
{
	ecs::Entity jet;
	Engine::Math::Fixed amount;
};

struct AirfieldHeals
{
	std::vector<AirfieldHeal> list;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::AirfieldHealing>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.airfield_healing";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
template<>
struct ResourceTraits<generalszh::gameplay::AirfieldHeals>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.airfield_heals";
};
}
