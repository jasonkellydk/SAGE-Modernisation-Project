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

// Before the simulation step: gathers every targetable entity (in
// parallel, per chunk), classifies it as airborne or on the ground, and
// rebuilds the spatial index the step's queries read.
export namespace engine::gameplay
{
using SpatialGather = ecs::ChunkOutputs<SpatialEntry>;

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
		ecs::Exclude<OffMap>>;
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
		auto &out = gather.Slot(context);
		for (std::size_t row = 0; row < transforms.size(); ++row)
		{
			const auto &position = transforms[row].position;
			std::uint32_t classes = targetables[row].classes;
			const bool airborne = position.z - ground.Surface(position.XY()) > AirborneHeight();
			if (airborne)
				classes |= (classes & target_class::Infantry) != 0 ? target_class::AirborneInfantry : target_class::AirborneVehicle;
			else
				classes |= target_class::Ground;
			out.push_back({entities[row], position, targetables[row].radius, owners[row].player, classes, shrouds.empty() ? ~std::uint64_t{0} : shrouds[row].clear,
				teams.empty() ? 0xFFFFFFFFu : teams[row].team, targetables[row].disguiseTeam, targetables[row].disguisePlayer});
		}
	}

	void AfterChunks(Query &, ecs::SystemContext &context)
	{
		SpatialIndex &index = context.Write<SpatialIndex>();
		SpatialGather &gather = context.Write<SpatialGather>();
		index.RebuildWith([&](std::vector<SpatialEntry> &entries) { gather.AppendTo(entries); });
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
