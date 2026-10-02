export module games.generalszh.gameplay.abilities.components.ability_laser;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// A special object streaming a laser (SpecialAbilityUpdate::initLaser on the special object's LaserUpdate: the Missile
// Defender's LaserBeam, the hackers' and the Black Lotus' BinaryDataStream): from `parent`, the unit whose ability made
// it (its SpecialObjectAttachToBone, found by its `power`), to `end`, the target's centre as it began (getCenterPosition;
// given that end, the laser never follows the target). It lasts as long as the special object. Simulation state:
// checkpointed.
export namespace generalszh::gameplay
{
struct AbilityLaser
{
	ecs::Entity parent;
	Engine::Math::FixedVector3 end;
	std::uint32_t power{0};
	std::uint32_t reserved{0}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::AbilityLaser>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.ability_laser";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
