export module games.generalszh.presentation.hud.resources.radar_events;
import std;

import engine.ecs.system.system;

// The radar's events for the one watching (the original's Radar::m_event, MAX_RADAR_EVENTS 64, a ring the oldest drop
// off of): what each is, where, its two colours, when it came, fades and goes; and the last one's place (not a
// beacon pulse's: getLastEventLoc). And what the radar needs to know about the game: which objects show on it (by
// definition: RadarPriority, else garrisonable or capturable ones), the damage types that never warn (PENALTY,
// HEALING), the under attack messages (RADAR:*) and sounds (MiscAudio RadarNotify*). Presentation state: not saved.
export namespace generalszh::presentation
{
enum class RadarEventType : std::uint8_t
{
	Invalid,
	Construction,
	Upgrade,
	UnderAttack,
	Information,
	BeaconPulse,
	Infiltration,
	BattlePlan,
	StealthDiscovered,
	StealthNeutralized,
	Fake,
};

struct RadarEvent
{
	RadarEventType type{RadarEventType::Invalid};
	bool active{false};
	std::uint64_t createFrame{0};
	std::uint64_t dieFrame{0};
	std::uint64_t fadeFrame{0};
	std::array<std::uint8_t, 4> color1{};
	std::array<std::uint8_t, 4> color2{};
	std::array<float, 3> world{};
	bool soundPlayed{false}; // its RadarEvent sound, played when it is first drawn
};

struct RadarEvents
{
	static constexpr std::size_t Capacity = 64;
	std::array<RadarEvent, Capacity> events{};
	std::size_t next{0};
	std::optional<std::size_t> last; // m_lastRadarEvent
};

struct RadarFeedback
{
	std::uint32_t penaltyDamage{0xFFFFFFFFu};
	std::uint32_t healingDamage{0xFFFFFFFFu};
	// RADAR:UnderAttack, UnitUnderAttack, HarvesterUnderAttack, StructureUnderAttack, Infiltration.
	std::u16string underAttack, unitUnderAttack, harvesterUnderAttack, structureUnderAttack, infiltration;
	// MiscAudio RadarNotifyHarvesterUnderAttackSound, RadarNotifyStructureUnderAttackSound, RadarNotifyInfiltrationSound.
	std::string harvesterSound, structureSound, infiltrationSound;
	// Each definition's BattlePlanUpdate messages (BombardmentMessageLabel, HoldTheLineMessageLabel,
	// SearchAndDestroyMessageLabel), by definition index (none: empty).
	std::map<std::string, std::array<std::u16string, 3>, std::less<>> battlePlanMessages;
	// StealthDetectorUpdate's feedback: MESSAGE:StealthDiscovered / StealthNeutralized, MiscAudio StealthDiscoveredSound /
	// StealthNeutralizedSound (each definition's EVA lines and kinds are its DefinitionLooks').
	std::u16string stealthDiscovered, stealthNeutralized;
	std::string stealthDiscoveredSound, stealthNeutralizedSound;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::RadarEvents>
{
	static constexpr std::string_view StableName = "generalszh.presentation.radar_events";
};
template<>
struct ResourceTraits<generalszh::presentation::RadarFeedback>
{
	static constexpr std::string_view StableName = "generalszh.presentation.radar_feedback";
};
}
