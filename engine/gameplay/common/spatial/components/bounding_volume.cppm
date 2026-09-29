export module engine.gameplay.common.spatial.components.bounding_volume;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// The space a body takes up in 3D (the original's GeometryInfo): its bounding sphere (radius, and how far above its
// position its centre is: half its height, a sphere's none), how far it reaches below and above its position, and what
// it is to what runs into it: immobile (it stands firm: a falling wreck strikes it) and a snag (a wreck spiralling down
// into it crashes there: a tree).
export namespace engine::gameplay
{
struct BoundingVolume
{
	Engine::Math::Fixed sphereRadius;
	Engine::Math::Fixed centerLift;
	Engine::Math::Fixed below;
	Engine::Math::Fixed above;
	Engine::Math::Fixed circleRadius; // its footprint's bounding circle (a box's corner)
	std::uint8_t immobile{0};
	std::uint8_t snag{0};
	std::uint8_t structure{0}; // KINDOF_STRUCTURE: a body falling on to it from high up is lost in it
	std::uint8_t reserved[5]{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::BoundingVolume>
{
	static constexpr std::string_view StableName = "engine.gameplay.bounding_volume";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
