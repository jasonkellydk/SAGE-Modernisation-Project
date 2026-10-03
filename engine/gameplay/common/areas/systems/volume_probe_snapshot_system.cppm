export module engine.gameplay.common.areas.systems.volume_probe_snapshot_system;
import std;
export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.common.areas.components.volume_probe;
export import engine.gameplay.common.spatial.components.transform;

export namespace engine::gameplay
{
struct VolumeProbePosition
{
	ecs::Entity entity;
	Engine::Math::FixedVector3 position;
	std::uint64_t categories{}, order{};
};
using VolumeProbePositionOutputs = ecs::ChunkOutputs<VolumeProbePosition>;
struct VolumeProbePositions
{
	std::vector<VolumeProbePosition> positions;
	const VolumeProbePosition *Find(ecs::Entity entity) const noexcept {
		const auto found = std::lower_bound(positions.begin(), positions.end(), entity.index,
			[](const auto &position, auto index) {return position.entity.index < index;});
		return found != positions.end() && found->entity == entity ? &*found : nullptr;
	}
};
}
export namespace ecs
{
template<> struct ResourceTraits<engine::gameplay::VolumeProbePositionOutputs>
{ static constexpr std::string_view StableName = "engine.gameplay.volume_probe_position_outputs"; };
template<> struct ResourceTraits<engine::gameplay::VolumeProbePositions>
{ static constexpr std::string_view StableName = "engine.gameplay.volume_probe_positions"; };
}
export namespace engine::gameplay
{
struct VolumeProbeSnapshotSystem
{
	using Query = ecs::Query<ecs::Read<Transform>, ecs::Read<VolumeProbe>>;
	using Resources = ecs::Resources<ecs::Write<VolumeProbePositionOutputs>, ecs::Write<VolumeProbePositions>>;
	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<VolumeProbePositionOutputs>().Reset(query.PreparedChunkCount()); }
	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const {
		const auto transforms = chunk.Get<Transform>(); const auto probes = chunk.Get<VolumeProbe>();
		const auto entities = chunk.Entities(); auto &out = context.Write<VolumeProbePositionOutputs>().Slot(context);
		for (std::size_t row = 0; row < transforms.size(); ++row) out.push_back({entities[row], transforms[row].position, probes[row].categories, probes[row].order});
	}
	void AfterChunks(Query &, ecs::SystemContext &context) const {
		auto &positions = context.Write<VolumeProbePositions>().positions; positions.clear();
		context.Write<VolumeProbePositionOutputs>().AppendTo(positions);
		std::ranges::sort(positions, {}, [](const auto &position) {return position.entity.index;});
	}
};
}
export namespace ecs
{
template<> struct SystemTraits<engine::gameplay::VolumeProbeSnapshotSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.volume_probe_snapshot";
	static constexpr std::size_t PieceRows = 32;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
