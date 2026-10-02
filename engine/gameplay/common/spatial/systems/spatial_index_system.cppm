export module engine.gameplay.common.spatial.systems.spatial_index_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.components.object_shroud;
export import engine.gameplay.common.spatial.components.airborne_target;
export import engine.gameplay.common.spatial.components.body_extent;
export import engine.gameplay.common.spatial.components.bounding_volume;

// Before the simulation step: gathers every targetable entity (in
// parallel, per chunk), classifies it as airborne or on the ground, and
// rebuilds the spatial index the step's queries read. One with an
// AirborneTarget status is airborne by that status (its locomotor's
// AirborneTargetingHeight); one without, by its height over the surface. Nothing off the map is gathered, but a rider
// its container leaves in the world (OffMap Stationed: a fire base's). Each entry's bounding sphere goes beside it
// (its live BoundingVolume's, else its BodyExtent's; neither: its footprint's circle about its position).
export namespace engine::gameplay
{
struct SpatialGathered
{
	SpatialEntry entry;
	BoundingSphere sphere;
};
using SpatialGather = ecs::ChunkOutputs<SpatialGathered>;

// Above this height over the surface a target counts as airborne.
inline Engine::Math::Fixed AirborneHeight() noexcept { return Engine::Math::Fixed::FromInt(10); }

}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::SpatialGather>
{
	static constexpr std::string_view StableName = "engine.gameplay.spatial_gather";
};
}

export namespace engine::gameplay
{
struct SpatialIndexSystem
{
	using Query = ecs::Query<ecs::Read<Transform>, ecs::Read<Targetable>, ecs::Read<Owner>, ecs::Optional<ObjectShroud>, ecs::Optional<TeamMember>,
		ecs::Optional<AirborneTarget>, ecs::Optional<BodyExtent>, ecs::Optional<BoundingVolume>, ecs::Optional<OffMap>>;
	using Resources = ecs::Resources<ecs::Write<SpatialIndex>, ecs::Write<SpatialGather>, ecs::Read<GroundHeight>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<SpatialGather>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		SpatialGather &gather = context.Write<SpatialGather>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		const auto transforms = chunk.Get<Transform>();
		const auto targetables = chunk.Get<Targetable>();
		const auto owners = chunk.Get<Owner>();
		const auto entities = chunk.Entities();
		const auto shrouds = chunk.Get<ObjectShroud>();
		const auto teams = chunk.Get<TeamMember>();
		const auto statuses = chunk.Get<AirborneTarget>();
		const auto extents = chunk.Get<BodyExtent>();
		const auto volumes = chunk.Get<BoundingVolume>();
		const auto away = chunk.Get<OffMap>();
		auto &out = gather.Slot(context);
		for (std::size_t row = 0; row < transforms.size(); ++row)
		{
			// Off the map it is not found, but for a rider a container leaves in the world (Stationed).
			if (!away.empty() && away[row].reason != off_map_reason::Stationed)
				continue;
			const auto &position = transforms[row].position;
			std::uint32_t classes = targetables[row].classes;
			const bool airborne = statuses.empty() ? position.z - ground.Surface(position.XY()) > AirborneHeight() : statuses[row].airborne != 0;
			if (airborne)
				classes |= (classes & target_class::Infantry) != 0 ? target_class::AirborneInfantry : target_class::AirborneVehicle;
			else
				classes |= target_class::Ground;
			const BoundingSphere sphere = !volumes.empty() ? BoundingSphere{volumes[row].sphereRadius, volumes[row].centerLift}
				: !extents.empty() ? BoundingSphere{extents[row].sphereRadius, extents[row].centerZ} : BoundingSphere{targetables[row].radius, {}};
			out.push_back({{entities[row], position, targetables[row].radius, owners[row].player, classes, shrouds.empty() ? ~std::uint64_t{0} : shrouds[row].clear,
				teams.empty() ? 0xFFFFFFFFu : teams[row].team, targetables[row].disguiseTeam, targetables[row].disguisePlayer}, sphere});
		}
	}

	void AfterChunks(Query &, ecs::SystemContext &context)
	{
		SpatialIndex &index = context.Write<SpatialIndex>();
		SpatialGather &gather = context.Write<SpatialGather>();
		index.RebuildWithSpheres([&](std::vector<SpatialEntry> &entries, std::vector<BoundingSphere> &spheres) {
			gather.ForEach([&](const SpatialGathered &gathered) {
				entries.push_back(gathered.entry);
				spheres.push_back(gathered.sphere);
			});
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::SpatialIndexSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.spatial_index";
	// Its rows are independent: large chunks are shared out in pieces of 32 rows.
	static constexpr std::size_t PieceRows = 32;
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
