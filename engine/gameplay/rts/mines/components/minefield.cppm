export module engine.gameplay.rts.mines.components.minefield;
import std;

export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;
export import engine.ecs.core.entity;
import engine.ecs.core.component_registry;
import engine.ecs.system.system;

// A mine (the original's MinefieldBehavior): `total` virtual mines in one object, `remaining` of them live, its health
// their share of its most. Whatever it may be set off by (`detonatedBy`: allies 1, enemies 2, neutral 4) that stands in
// it sets it off (onCollide), unless it is a worker (MineSafe) and workers do not (`workersDetonate`), it is still
// scooting out, or it has not moved `repeatThreshold` since it last set it off (the last few `detonators`): the
// detonation weapon goes off where it touched the mine, a virtual mine is spent and its health falls to match. Damage
// that takes its health below its live mines' share sets them off where it is (onDamage); healing back above brings
// them back. Spent, it looks rubble and cannot be attacked (MASKED); one that does not regenerate is then removed.
// One that regenerates (healing, kept at MIN_HEALTH 0.1 by its HealthFloor) stops when its `producer` dies (checked every
// `checkRate` ticks), and drains `drain` percent of its most a second from then (its drained mines spent quietly). It
// may scoot out from where it was made (`scootLeft` ticks at `scootVelocity`, `scootAcceleration`). Simulation state:
// checkpointed.
export namespace engine::gameplay
{
struct MineDetonator
{
	ecs::Entity who;
	Engine::Math::FixedVector3 where;
};

struct Minefield
{
	static constexpr std::uint32_t NoWeapon = 0xFFFFFFFFu;
	static constexpr std::size_t MaxDetonators = 8;

	std::uint32_t weapon{NoWeapon};
	std::uint32_t total{1};
	std::uint32_t remaining{1};
	std::uint8_t detonatedBy{2 | 4};
	std::uint8_t workersDetonate{0};
	std::uint8_t regenerates{0};
	std::uint8_t stopsRegen{1};
	std::uint8_t draining{0};
	std::uint8_t masked{0};
	std::uint8_t detonatorCount{0};
	std::uint8_t nextDetonator{0};
	std::uint32_t reserved{0};
	std::uint64_t checkRate{30};
	std::uint64_t nextCheck{0};
	std::uint64_t scootLeft{0};
	Engine::Math::Fixed drain;          // percent points of its most a second, once draining
	Engine::Math::Fixed repeatThreshold{Engine::Math::Fixed::One()};
	Engine::Math::Fixed lastHealth;     // its health as last seen (damage or healing since)
	Engine::Math::Fixed radius;         // its footprint (a cylinder's)
	ecs::Entity producer;
	Engine::Math::FixedVector3 scootVelocity;
	Engine::Math::FixedVector3 scootAcceleration;
	std::array<MineDetonator, MaxDetonators> detonators{};
};

// The damage a draining mine does itself (DAMAGE_UNRESISTABLE, DEATH_NORMAL) over the second's ticks.
struct MineSettings
{
	std::uint32_t unresistable{0}; // DAMAGE_UNRESISTABLE
	std::uint32_t normalDeath{0};  // DEATH_NORMAL
	std::uint32_t ticksPerSecond{30};
};

// What does not set mines off unless they say so (KINDOF_INFANTRY and KINDOF_DOZER: workers).
struct MineSafe
{
	std::uint8_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Minefield>
{
	static constexpr std::string_view StableName = "engine.gameplay.minefield";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<engine::gameplay::MineSettings>
{
	static constexpr std::string_view StableName = "engine.gameplay.mine_settings";
};

template<>
struct ComponentTraits<engine::gameplay::MineSafe>
{
	static constexpr std::string_view StableName = "engine.gameplay.mine_safe";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
