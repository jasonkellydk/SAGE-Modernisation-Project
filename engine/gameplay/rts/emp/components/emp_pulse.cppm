export module engine.gameplay.rts.emp.components.emp_pulse;
import std;

export import Engine.Core.Math.Fixed;
export import engine.ecs.core.entity;
import engine.ecs.core.component_registry;

// An EMP pulse (the original's EMPUpdate): on `fadeTick` (StartFadeTime after it was made) it disables what it reaches
// within `radius` (bounding spheres, in 3D) for `duration` ticks (DISABLED_EMP), and it dies on `dieTick` (Lifetime).
// Reached are vehicles, faction structures (not its own player's when `sparesOwnBuildings`) and what fights with what it
// spawns; aircraft in the air are killed outright unless EMP hardened; allies are spared when `sparesAllies`. When its
// `producer` aims at something airborne, only airborne things are reached, and that victim (an aircraft, not hardened)
// is disabled even outside the radius when near enough. EmpTraits: what the pulse asks of its victims (the game's
// kinds). `targetScale`: the size it grows toward (TargetScaleMin..Max, rolled when made; its look). Simulation
// state: checkpointed.
export namespace engine::gameplay
{
struct EmpPulse
{
	std::uint64_t fadeTick{0};
	std::uint64_t dieTick{0};
	std::uint64_t duration{0};
	Engine::Math::Fixed radius{Engine::Math::Fixed::FromInt(200)};
	Engine::Math::Fixed targetScale{Engine::Math::Fixed::One()};
	ecs::Entity producer;
	std::uint8_t sparesAllies{0};
	std::uint8_t sparesOwnBuildings{0};
	std::uint8_t done{0};
	std::uint8_t reserved[5]{};
};

namespace emp_trait
{
inline constexpr std::uint32_t Hardened = 1u << 0;          // KINDOF_EMP_HARDENED
inline constexpr std::uint32_t SpawnsAreWeapons = 1u << 1;  // KINDOF_SPAWNS_ARE_THE_WEAPONS
inline constexpr std::uint32_t FactionStructure = 1u << 2;  // ThingTemplate::isFactionStructure
}

struct EmpTraits
{
	std::uint32_t flags{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::EmpPulse>
{
	static constexpr std::string_view StableName = "engine.gameplay.emp_pulse";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ComponentTraits<engine::gameplay::EmpTraits>
{
	static constexpr std::string_view StableName = "engine.gameplay.emp_traits";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
