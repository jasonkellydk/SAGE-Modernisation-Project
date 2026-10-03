export module engine.gameplay.common.areas.resources.trigger_volumes;
import std;
export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import Engine.Core.Math.FixedOrientedBox3;

export namespace engine::gameplay
{
struct TriggerVolume
{
	ecs::Entity subject;
	Engine::Math::FixedOrientedBox3 bounds;
	std::uint64_t accepted_categories{~std::uint64_t{}};
	bool enabled{true};
};
struct TriggerVolumes
{
	std::vector<TriggerVolume> volumes;
	// Removal is distinct from crossing the boundary. Games choose whether a
	// missing probe should emit an exit or just clear the stored membership.
	bool exit_on_removal{true};
	// Some authored volumes have opaque subjects; other compositions bind
	// them to living ECS owners. Opt in without changing existing callers.
	bool require_live_subject{false};
	bool retire_missing_probes{false};
};
struct VolumeTransition
{
	ecs::Entity subject, probe;
	std::uint32_t volume{};
	bool entered{};
	std::uint64_t previous_entry_tick{}, order{}, probe_order{};
};
using VolumeTransitions = ecs::ChunkOutputs<VolumeTransition>;
}
export namespace ecs
{
template<> struct ResourceTraits<engine::gameplay::TriggerVolumes>
{ static constexpr std::string_view StableName = "engine.gameplay.trigger_volumes"; };
template<> struct ResourceTraits<engine::gameplay::VolumeTransitions>
{ static constexpr std::string_view StableName = "engine.gameplay.volume_transitions"; };
}
