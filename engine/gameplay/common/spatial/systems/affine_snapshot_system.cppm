export module engine.gameplay.common.spatial.systems.affine_snapshot_system;
import std;
export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.common.spatial.components.affine_pose;
export import engine.gameplay.common.appearance.components.model_override;

export namespace engine::gameplay
{
struct AffineVisibleObject
{
	ecs::Entity entity;
	std::uint32_t model{};
	Engine::Math::FixedAffineTransform3 transform;
};
using AffineVisibleObjects = ecs::ChunkOutputs<AffineVisibleObject>;
}
export namespace ecs
{
template<> struct ResourceTraits<engine::gameplay::AffineVisibleObjects>
{ static constexpr std::string_view StableName = "engine.gameplay.affine_visible_objects"; };
}
export namespace engine::gameplay
{
struct AffineSnapshotSystem
{
	using Query = ecs::Query<ecs::Read<AffinePose>, ecs::Read<ModelOverride>>;
	using Resources = ecs::Resources<ecs::Write<AffineVisibleObjects>>;
	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<AffineVisibleObjects>().Reset(query.PreparedChunkCount()); }
	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const {
		const auto poses = chunk.Get<AffinePose>(); const auto models = chunk.Get<ModelOverride>();
		const auto entities = chunk.Entities(); auto &out = context.Write<AffineVisibleObjects>().Slot(context);
		for (std::size_t row = 0; row < poses.size(); ++row)
			if (models[row].model) out.push_back({entities[row], models[row].model, poses[row].transform});
	}
};
}
export namespace ecs
{
template<> struct SystemTraits<engine::gameplay::AffineSnapshotSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.affine_snapshot";
	static constexpr std::size_t PieceRows = 32;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
