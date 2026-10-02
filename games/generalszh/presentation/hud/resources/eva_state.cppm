export module games.generalszh.presentation.hud.resources.eva_state;
import std;

export import engine.ecs.core.component_registry;
export import games.generalszh.content.eva.eva_content;
import engine.ecs.system.system;

// EVA, the announcer (the original's Eva: its check infos, its checks under way, the announcements asked for since
// its last update, whether it is on), for the one watching: their side picks the voice. Presentation state: never
// hashed or saved (the original's Eva has no save data). And on the simulation's objects, whether each of their
// superweapon countdowns has had its ready announcement (SuperweaponInfo::m_evaReadyPlayed), as a side table.
export namespace generalszh::presentation
{
struct EvaCheck
{
	std::uint32_t message{0};
	std::uint64_t triggeredOn{0};
	std::uint64_t nextCheck{0};
	bool played{false};
};

struct EvaState
{
	content::EvaCatalog catalog;
	bool enabled{true};                                          // setEvaEnabled (scripts: EVA_SET_ENABLED_DISABLED)
	std::array<bool, content::EvaMessageNames.size()> shouldPlay{}; // setShouldPlay since the last update
	std::vector<EvaCheck> checks;                                // m_checks, in the order they began
	std::string side;                                            // the watcher's side (getObservedOrLocalPlayer()->getSide())
	std::uint32_t requested{0};                                  // lines handed to the audio (its served count catches up)
};

// SuperweaponInfo::m_evaReadyPlayed, per power module slot.
struct SuperweaponEvaReady
{
	std::uint32_t played{0};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::EvaState>
{
	static constexpr std::string_view StableName = "generalszh.presentation.eva_state";
};

template<>
struct ComponentTraits<generalszh::presentation::SuperweaponEvaReady>
{
	static constexpr std::string_view StableName = "generalszh.presentation.superweapon_eva_ready";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
}
