export module games.generalszh.presentation.interaction.resources.unit_voice_cues;
import std;

export import engine.ecs.core.entity;
import engine.ecs.system.system;

// What the player just told the selection (the original's pickAndPlayUnitVoiceResponse calls: the message it was given
// and its PickAndPlayInfo), for the unit voice system to answer once this frame: the interaction's own orders, the
// control bar's and the radar's (the host's). Presentation state: not saved.
export namespace generalszh::presentation
{
// GameMessage types pickAndPlayUnitVoiceResponse answers.
enum class VoiceOrder : std::uint8_t
{
	CreateGroup,       // MSG_CREATE_SELECTED_GROUP (and MSG_SELECT_TEAMn): only what the player controls answers
	Dock,              // MSG_DOCK
	Evacuate,          // MSG_EVACUATE
	Repair,            // MSG_DO_REPAIR
	CombatDrop,        // MSG_COMBATDROP_AT_LOCATION / _AT_OBJECT
	Enter,             // MSG_ENTER (hijack, car bomb, sabotage and enter alike)
	Move,              // MSG_DO_MOVETO
	AttackMove,        // MSG_DO_ATTACKMOVETO
	GetRepaired,       // MSG_GET_REPAIRED
	GetHealed,         // MSG_GET_HEALED
	Salvage,           // MSG_DO_SALVAGE
	Construct,         // MSG_DOZER_CONSTRUCT / _LINE, MSG_RESUME_CONSTRUCTION
	SwitchWeapons,     // MSG_SWITCH_WEAPONS
	ForceAttackGround, // MSG_DO_FORCE_ATTACK_GROUND
	ForceAttackObject, // MSG_DO_FORCE_ATTACK_OBJECT
	AttackObject,      // MSG_DO_ATTACK_OBJECT
	WeaponAtObject,    // MSG_DO_WEAPON_AT_OBJECT
	WeaponAtLocation,  // MSG_DO_WEAPON_AT_LOCATION
	Guard,             // MSG_DO_GUARD_POSITION / _OBJECT
	SpecialPower,      // MSG_DO_SPECIAL_POWER(_AT_LOCATION / _AT_OBJECT)
	InternetHack,      // MSG_INTERNET_HACK
};

// PickAndPlayInfo: the target (m_drawTarget), whether it flies (m_air), the weapon slot (m_weaponSlot), the special
// power (m_specialPowerType: its template's name); and the modes the original reads (InGameUI's waypoint and force move).
struct UnitVoiceCue
{
	static constexpr std::uint8_t NoSlot = 0xFF;
	VoiceOrder order{VoiceOrder::Move};
	ecs::Entity target;
	bool air{false};
	std::uint8_t weaponSlot{NoSlot};
	std::string specialPower; // none: empty
	bool waypointMode{false};
	bool forceMoveMode{false};
};

struct UnitVoiceCues
{
	std::vector<UnitVoiceCue> pending;
};

// MiscAudio's car bomb lines (TerroristInCarAttackVoice, TerroristInCarMoveVoice, TerroristInCarSelectVoice) and
// AllCheerSound. Presentation configuration.
struct UnitVoiceSounds
{
	std::string carBombAttack;
	std::string carBombMove;
	std::string carBombSelect;
	std::string allCheer;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::UnitVoiceCues>
{
	static constexpr std::string_view StableName = "generalszh.presentation.unit_voice_cues";
};
template<>
struct ResourceTraits<generalszh::presentation::UnitVoiceSounds>
{
	static constexpr std::string_view StableName = "generalszh.presentation.unit_voice_sounds";
};
}
