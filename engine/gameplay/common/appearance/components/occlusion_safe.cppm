export module engine.gameplay.common.appearance.components.occlusion_safe;
import std;

export import engine.ecs.core.component_registry;

// Object::m_safeOcclusionFrame: the tick from which the renderer may show it through the buildings in front of it
// (building occlusion), its OcclusionDelay after it was made (or walked out of a building: GarrisonContain /
// TunnelContain set it again). Simulation state the presentation reads.
export namespace engine::gameplay
{
struct OcclusionSafe
{
	std::uint64_t tick{0};
	std::uint64_t delay{0}; // its template's OcclusionDelay, in ticks
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::OcclusionSafe>
{
	static constexpr std::string_view StableName = "engine.gameplay.occlusion_safe";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
