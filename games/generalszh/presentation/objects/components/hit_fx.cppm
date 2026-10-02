export module games.generalszh.presentation.objects.components.hit_fx;
import std;

import engine.ecs.core.component_registry;

// When an object may next show a hit of the damage type it last showed
// (ActiveBody's m_lastDamageFXDone / m_nextDamageFXTime: DamageFX's
// ThrottleTime), in presentation ticks. A side table on the hit object.
export namespace generalszh::presentation
{
struct HitFxThrottle
{
	std::uint32_t lastType{0xFFFFFFFFu};
	std::uint64_t nextTick{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::presentation::HitFxThrottle>
{
	static constexpr std::string_view StableName = "generalszh.presentation.hit_fx_throttle";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
}
