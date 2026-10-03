export module engine.gameplay.common.areas.systems.volume_presence_system;
import std;
export import engine.gameplay.common.areas.components.volume_contact;
export import engine.gameplay.common.areas.resources.trigger_volumes;
export import engine.gameplay.common.areas.systems.volume_probe_snapshot_system;

export namespace engine::gameplay
{
struct VolumePresenceSystem
{
	using Query = ecs::Query<ecs::Write<VolumeContact>>;
	using Lookup = ecs::Lookup<>;
	using Resources = ecs::Resources<ecs::Read<TriggerVolumes>, ecs::Read<VolumeProbePositions>, ecs::Write<VolumeTransitions>>;
	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<VolumeTransitions>().Reset(query.PreparedChunkCount()); }
	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const {
		const auto &volumes = context.Read<TriggerVolumes>(); const auto &positions = context.Read<VolumeProbePositions>();
		auto &out = context.Write<VolumeTransitions>().Slot(context);
		const auto contacts=chunk.Get<VolumeContact>();const auto alive=context.Lookup<Lookup>();
		for (std::size_t row=0;row<contacts.size();++row) {
			auto& contact=contacts[row];
			if (contact.volume >= volumes.volumes.size()) throw std::out_of_range("volume contact references missing level volume");
			const auto &volume = volumes.volumes[contact.volume]; const auto *probe = positions.Find(contact.probe);
			if(volumes.require_live_subject && !alive.IsAlive(volume.subject)) {
				// Destroying the volume is distinct from a boundary crossing.
				context.Commands().Destroy(chunk.Entities()[row]);continue;
			}
			const bool retire=volumes.retire_missing_probes && !alive.IsAlive(contact.probe);
			const bool inside = !retire && probe && volume.enabled && (probe->categories & volume.accepted_categories) && volume.bounds.Contains(probe->position);
			if(retire) context.Commands().Destroy(chunk.Entities()[row]);
			if (inside == bool(contact.inside)) continue;
			if ((probe && !retire) || volumes.exit_on_removal) out.push_back({volume.subject, contact.probe, contact.volume, inside, contact.entered_tick, contact.order, probe ? probe->order : 0});
			contact.inside = inside; if (inside) contact.entered_tick = context.Tick();
		}
	}
};
}
export namespace ecs
{
template<> struct SystemTraits<engine::gameplay::VolumePresenceSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.volume_presence";
	static constexpr std::size_t PieceRows = 32;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::VolumeProbeSnapshotSystem>;
};
}
