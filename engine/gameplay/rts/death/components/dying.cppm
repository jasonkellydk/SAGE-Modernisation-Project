export module engine.gameplay.rts.death.components.dying;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// Mortality: which death an entity dies (an index into the DeathCatalog).
// Dying: it has died and is playing out a slow death. It is no longer part
// of the fighting (not targeted, not firing, not moving, not hurt); it sinks
// after sinkTick, plays its midpoint effects once, and is removed at
// destructionTick. A flung body (FlingForce) flies on its physics until it
// is down, its timers held back a tick for every tick in the air; it sinks
// held (its physics gone). Simulation state: hashed and checkpointed.
export namespace engine::gameplay
{
struct Mortality
{
	std::uint32_t death{0};
};

struct Dying
{
	static constexpr std::uint32_t Lingering = 0xFFFFFFFFu; // `slow` when no slow death applied
	static constexpr std::uint64_t Never = ~std::uint64_t{0};

	std::uint64_t sinkTick{0};
	std::uint64_t midpointTick{0};
	std::uint64_t destructionTick{0};
	std::uint64_t since{0}; // the tick it died
	Engine::Math::Fixed sinkRate;
	ecs::Entity killer;
	std::uint32_t death{0};
	std::uint32_t slow{0}; // which slow death of the definition
	std::uint32_t deathType{0};
	std::uint32_t midpointDone{0};
	std::uint8_t flung{0};  // thrown into the air (SlowDeathBehavior FLUNG_INTO_AIR)
	std::uint8_t landed{0}; // and down again (BOUNCED)
	std::uint8_t held{0};   // sinking: its physics let go
	std::uint8_t crushed{0}; // CrushDie: 1 its front, 2 its back (both: all of it)
	std::uint8_t snagged{0}; // flung into a tree and caught in it (SlowDeathBehavior: held, PARACHUTING)
	std::uint8_t reserved[3]{};
	// What a flung body last ran into (PhysicsBehavior::m_lastCollidee; see CrashCollisionSystem).
	ecs::Entity lastCollidee;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Mortality>
{
	static constexpr std::string_view StableName = "engine.gameplay.mortality";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ComponentTraits<engine::gameplay::Dying>
{
	static constexpr std::string_view StableName = "engine.gameplay.dying";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
