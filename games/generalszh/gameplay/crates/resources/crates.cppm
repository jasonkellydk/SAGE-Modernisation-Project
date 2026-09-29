export module games.generalszh.gameplay.crates.resources.crates;
import std;

export import engine.ecs.core.entity;
export import games.generalszh.gameplay.crates.components.crate;
export import engine.ecs.system.chunk_outputs;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// Crates in play: who
// touched one this tick (CrateTouches, per chunk, found by the crate touch
// system) and what picking crates up did this tick (CratePickups, for the
// presentation: the sound, the FX, the cash floating up).
export namespace generalszh::gameplay
{
struct CrateTouch
{
	ecs::Entity crate;
	ecs::Entity toucher;
};

struct CrateTouches : ecs::ChunkOutputs<CrateTouch>
{
};

struct CratePickup
{
	enum class Kind : std::uint8_t
	{
		Salvage, // armor or weapons from salvage (MiscAudio CrateSalvage)
		Money,   // cash (MiscAudio CrateMoney; floating text)
		Level,   // a veterancy level
		Unit,    // free units (MiscAudio CrateFreeUnit)
		CarBomb, // a vehicle made a car bomb (its FX only)
	};
	ecs::Entity picker;
	Kind kind{Kind::Money};
	std::int64_t amount{0};
	std::uint32_t player{0};
	Engine::Math::FixedVector3 position;
	std::string fx; // the crate's ExecuteFX
	bool floats{false}; // the money floats up as text (a salvage crate's)
	// Its world animation (ExecuteAnimation, for its time, rising and fading as it says).
	std::string animation;
	Engine::Math::Fixed animationSeconds;
	Engine::Math::Fixed animationRise;
	bool animationFades{true};
};

struct CratePickups
{
	std::vector<CratePickup> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::CrateTouches>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.crate_touches";
};
template<>
struct ResourceTraits<generalszh::gameplay::CratePickups>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.crate_pickups";
};
}
