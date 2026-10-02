export module engine.gameplay.rts.vision.components.partition_footprint;
import std;

export import engine.ecs.core.component_registry;
export import engine.gameplay.common.spatial.algorithms.footprint;

// The partition cells an object touches come from its geometry (the original's PartitionData::updateCellsTouched: a
// small one's few cells, a circle's or a box's fill), and what the shroud makes of it from them; an ALWAYS_VISIBLE one is
// clear to everyone. Simulation state: checkpointed.
export namespace engine::gameplay
{
struct PartitionFootprint
{
	Engine::Math::Fixed major; // a circle's radius, a box's half length
	Engine::Math::Fixed minor; // a box's half width
	std::uint8_t box{0};
	std::uint8_t smallGeometry{0}; // GeometryIsSmall
	std::uint8_t alwaysVisible{0};
	std::uint8_t reserved[5]{};

	Footprint Shape() const noexcept { return {box != 0 ? FootprintShape::Box : FootprintShape::Circle, major, minor}; }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::PartitionFootprint>
{
	static constexpr std::string_view StableName = "engine.gameplay.partition_footprint";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
