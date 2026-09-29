export module engine.gameplay.common.spatial.components.body_extent;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// An object's geometry beyond its footprint (GeometryInfo): how high it reaches above its position
// (getMaxHeightAbovePosition: a sphere's radius, a box or cylinder's height), its bounding sphere's radius and how far
// above its position that sphere's centre is (getZDeltaToCenterPosition: none for a sphere, half the height else).
// Simulation data: checkpointed.
export namespace engine::gameplay
{
struct BodyExtent
{
	Engine::Math::Fixed maxHeight;
	Engine::Math::Fixed sphereRadius;
	Engine::Math::Fixed centerZ;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::BodyExtent>
{
	static constexpr std::string_view StableName = "engine.gameplay.body_extent";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
