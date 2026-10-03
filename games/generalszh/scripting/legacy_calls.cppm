export module games.generalszh.scripting.legacy_calls;
import std;

export import engine.level.model.level;
export import engine.scripting.runtime.script_runtime;

// How Zero Hour reads the script calls a map stores (Scripts.cpp: ScriptAction::ParseAction and
// Condition::ParseConditionDataChunk), so the script runtime runs what the original ran.
export namespace generalszh::scripting
{
// A call template (ScriptEngine::init): its internal name and how many parameters it takes.
struct LegacyCallTemplate
{
	std::string_view name;
	std::uint32_t parameters;
};

// The codes calls are stored under (Scripts.h: ScriptAction::ScriptActionType and Condition::ConditionType, in order)
// with their templates (the enum's own name and no parameters where init sets no template). A chunk older than
// version 2 (actions) or 4 (conditions) keeps only the code and is read as that enum value.
inline constexpr std::array<LegacyCallTemplate, 344> LegacyActionTemplates{{
	{"DEBUG_MESSAGE_BOX", 1}, {"SET_FLAG", 2}, {"SET_COUNTER", 2}, {"VICTORY", 0}, {"DEFEAT", 0}, {"NO_OP", 0},
	{"SET_TIMER", 2}, {"PLAY_SOUND_EFFECT", 1}, {"ENABLE_SCRIPT", 1}, {"DISABLE_SCRIPT", 1}, {"CALL_SUBROUTINE", 1},
	{"PLAY_SOUND_EFFECT_AT", 2}, {"DAMAGE_MEMBERS_OF_TEAM", 2}, {"MOVE_TEAM_TO", 2}, {"MOVE_CAMERA_TO", 5},
	{"INCREMENT_COUNTER", 2}, {"DECREMENT_COUNTER", 2}, {"MOVE_CAMERA_ALONG_WAYPOINT_PATH", 5}, {"ROTATE_CAMERA", 4},
	{"RESET_CAMERA", 4}, {"SET_MILLISECOND_TIMER", 2}, {"CAMERA_MOD_FREEZE_TIME", 0},
	{"SET_VISUAL_SPEED_MULTIPLIER", 1}, {"CREATE_OBJECT", 4}, {"SUSPEND_BACKGROUND_SOUNDS", 0},
	{"RESUME_BACKGROUND_SOUNDS", 0}, {"CAMERA_MOD_SET_FINAL_ZOOM", 3}, {"CAMERA_MOD_SET_FINAL_PITCH", 3},
	{"CAMERA_MOD_FREEZE_ANGLE", 0}, {"CAMERA_MOD_SET_FINAL_SPEED_MULTIPLIER", 1}, {"CAMERA_MOD_SET_ROLLING_AVERAGE", 1},
	{"CAMERA_MOD_FINAL_LOOK_TOWARD", 1}, {"CAMERA_MOD_LOOK_TOWARD", 1}, {"TEAM_ATTACK_TEAM", 2},
	{"CREATE_REINFORCEMENT_TEAM", 2}, {"MOVE_CAMERA_TO_SELECTION", 0}, {"TEAM_FOLLOW_WAYPOINTS", 3},
	{"TEAM_SET_STATE", 2}, {"MOVE_NAMED_UNIT_TO", 2}, {"NAMED_ATTACK_NAMED", 2},
	{"CREATE_NAMED_ON_TEAM_AT_WAYPOINT", 4}, {"CREATE_UNNAMED_ON_TEAM_AT_WAYPOINT", 3},
	{"NAMED_APPLY_ATTACK_PRIORITY_SET", 2}, {"TEAM_APPLY_ATTACK_PRIORITY_SET", 2}, {"SET_BASE_CONSTRUCTION_SPEED", 2},
	{"NAMED_SET_ATTITUDE", 2}, {"TEAM_SET_ATTITUDE", 2}, {"NAMED_ATTACK_AREA", 2}, {"NAMED_ATTACK_TEAM", 2},
	{"TEAM_ATTACK_AREA", 2}, {"TEAM_ATTACK_NAMED", 2}, {"TEAM_LOAD_TRANSPORTS", 1}, {"NAMED_ENTER_NAMED", 2},
	{"TEAM_ENTER_NAMED", 2}, {"NAMED_EXIT_ALL", 1}, {"TEAM_EXIT_ALL", 1}, {"NAMED_FOLLOW_WAYPOINTS", 2},
	{"NAMED_GUARD", 1}, {"TEAM_GUARD", 1}, {"NAMED_HUNT", 1}, {"TEAM_HUNT", 1}, {"PLAYER_SELL_EVERYTHING", 1},
	{"PLAYER_DISABLE_BASE_CONSTRUCTION", 1}, {"PLAYER_DISABLE_FACTORIES", 2}, {"PLAYER_DISABLE_UNIT_CONSTRUCTION", 1},
	{"PLAYER_ENABLE_BASE_CONSTRUCTION", 1}, {"PLAYER_ENABLE_FACTORIES", 2}, {"PLAYER_ENABLE_UNIT_CONSTRUCTION", 1},
	{"CAMERA_MOVE_HOME", 0}, {"BUILD_TEAM", 1}, {"NAMED_DAMAGE", 2}, {"NAMED_DELETE", 1}, {"TEAM_DELETE", 1},
	{"NAMED_KILL", 1}, {"TEAM_KILL", 1}, {"PLAYER_KILL", 1}, {"DISPLAY_TEXT", 1}, {"CAMEO_FLASH", 2},
	{"NAMED_FLASH", 2}, {"TEAM_FLASH", 2}, {"MOVIE_PLAY_FULLSCREEN", 1}, {"MOVIE_PLAY_RADAR", 1},
	{"SOUND_PLAY_NAMED", 2}, {"SPEECH_PLAY", 2}, {"PLAYER_TRANSFER_OWNERSHIP_PLAYER", 2},
	{"NAMED_TRANSFER_OWNERSHIP_PLAYER", 2}, {"PLAYER_RELATES_PLAYER", 3}, {"RADAR_CREATE_EVENT", 2},
	{"RADAR_DISABLE", 0}, {"RADAR_ENABLE", 0}, {"MAP_REVEAL_AT_WAYPOINT", 3}, {"TEAM_AVAILABLE_FOR_RECRUITMENT", 2},
	{"TEAM_COLLECT_NEARBY_FOR_TEAM", 1}, {"TEAM_MERGE_INTO_TEAM", 2}, {"DISABLE_INPUT", 0}, {"ENABLE_INPUT", 0},
	{"PLAYER_HUNT", 1}, {"SOUND_AMBIENT_PAUSE", 0}, {"SOUND_AMBIENT_RESUME", 0}, {"MUSIC_SET_TRACK", 3},
	{"SET_TREE_SWAY", 5}, {"DEBUG_STRING", 1}, {"MAP_REVEAL_ALL", 1}, {"TEAM_GARRISON_SPECIFIC_BUILDING", 2},
	{"EXIT_SPECIFIC_BUILDING", 1}, {"TEAM_GARRISON_NEAREST_BUILDING", 1}, {"TEAM_EXIT_ALL_BUILDINGS", 1},
	{"NAMED_GARRISON_SPECIFIC_BUILDING", 2}, {"NAMED_GARRISON_NEAREST_BUILDING", 1}, {"NAMED_EXIT_BUILDING", 1},
	{"PLAYER_GARRISON_ALL_BUILDINGS", 1}, {"PLAYER_EXIT_ALL_BUILDINGS", 1}, {"TEAM_WANDER", 2}, {"TEAM_PANIC", 2},
	{"SETUP_CAMERA", 4}, {"CAMERA_LETTERBOX_BEGIN", 0}, {"CAMERA_LETTERBOX_END", 0}, {"ZOOM_CAMERA", 4},
	{"PITCH_CAMERA", 4}, {"CAMERA_FOLLOW_NAMED", 2}, {"OVERSIZE_TERRAIN", 1}, {"CAMERA_FADE_ADD", 5},
	{"CAMERA_FADE_SUBTRACT", 5}, {"CAMERA_FADE_SATURATE", 5}, {"CAMERA_FADE_MULTIPLY", 0}, {"CAMERA_BW_MODE_BEGIN", 1},
	{"CAMERA_BW_MODE_END", 1}, {"DRAW_SKYBOX_BEGIN", 0}, {"DRAW_SKYBOX_END", 0}, {"SET_ATTACK_PRIORITY_THING", 3},
	{"SET_ATTACK_PRIORITY_KIND_OF", 3}, {"SET_DEFAULT_ATTACK_PRIORITY", 2}, {"CAMERA_STOP_FOLLOW", 0},
	{"CAMERA_MOTION_BLUR", 2}, {"CAMERA_MOTION_BLUR_JUMP", 2}, {"CAMERA_MOTION_BLUR_FOLLOW", 1},
	{"CAMERA_MOTION_BLUR_END_FOLLOW", 0}, {"FREEZE_TIME", 0}, {"UNFREEZE_TIME", 0}, {"SHOW_MILITARY_CAPTION", 2},
	{"CAMERA_SET_AUDIBLE_DISTANCE", 1}, {"SET_STOPPING_DISTANCE", 2}, {"NAMED_SET_STOPPING_DISTANCE", 2},
	{"SET_FPS_LIMIT", 1}, {"MUSIC_SET_VOLUME", 1}, {"MAP_SHROUD_AT_WAYPOINT", 3}, {"MAP_SHROUD_ALL", 1},
	{"SET_RANDOM_TIMER", 3}, {"SET_RANDOM_MSEC_TIMER", 3}, {"STOP_TIMER", 1}, {"RESTART_TIMER", 1},
	{"ADD_TO_MSEC_TIMER", 2}, {"SUB_FROM_MSEC_TIMER", 2}, {"TEAM_TRANSFER_TO_PLAYER", 2}, {"PLAYER_SET_MONEY", 2},
	{"PLAYER_GIVE_MONEY", 2}, {"DISABLE_SPECIAL_POWER_DISPLAY", 0}, {"ENABLE_SPECIAL_POWER_DISPLAY", 0},
	{"NAMED_HIDE_SPECIAL_POWER_DISPLAY", 1}, {"NAMED_SHOW_SPECIAL_POWER_DISPLAY", 1}, {"DISPLAY_COUNTDOWN_TIMER", 2},
	{"HIDE_COUNTDOWN_TIMER", 1}, {"ENABLE_COUNTDOWN_TIMER_DISPLAY", 0}, {"DISABLE_COUNTDOWN_TIMER_DISPLAY", 0},
	{"NAMED_STOP_SPECIAL_POWER_COUNTDOWN", 2}, {"NAMED_START_SPECIAL_POWER_COUNTDOWN", 2},
	{"NAMED_SET_SPECIAL_POWER_COUNTDOWN", 3}, {"NAMED_ADD_SPECIAL_POWER_COUNTDOWN", 3},
	{"NAMED_FIRE_SPECIAL_POWER_AT_WAYPOINT", 3}, {"NAMED_FIRE_SPECIAL_POWER_AT_NAMED", 3}, {"REFRESH_RADAR", 0},
	{"CAMERA_TETHER_NAMED", 3}, {"CAMERA_STOP_TETHER_NAMED", 0}, {"CAMERA_SET_DEFAULT", 3}, {"NAMED_STOP", 1},
	{"TEAM_STOP", 1}, {"TEAM_STOP_AND_DISBAND", 1}, {"RECRUIT_TEAM", 2}, {"TEAM_SET_OVERRIDE_RELATION_TO_TEAM", 3},
	{"TEAM_REMOVE_OVERRIDE_RELATION_TO_TEAM", 2}, {"TEAM_REMOVE_ALL_OVERRIDE_RELATIONS", 1},
	{"CAMERA_LOOK_TOWARD_OBJECT", 5}, {"NAMED_FIRE_WEAPON_FOLLOWING_WAYPOINT_PATH", 2},
	{"TEAM_SET_OVERRIDE_RELATION_TO_PLAYER", 3}, {"TEAM_REMOVE_OVERRIDE_RELATION_TO_PLAYER", 2},
	{"PLAYER_SET_OVERRIDE_RELATION_TO_TEAM", 3}, {"PLAYER_REMOVE_OVERRIDE_RELATION_TO_TEAM", 2},
	{"UNIT_EXECUTE_SEQUENTIAL_SCRIPT", 2}, {"UNIT_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING", 3},
	{"UNIT_STOP_SEQUENTIAL_SCRIPT", 1}, {"TEAM_EXECUTE_SEQUENTIAL_SCRIPT", 2},
	{"TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING", 3}, {"TEAM_STOP_SEQUENTIAL_SCRIPT", 1}, {"UNIT_GUARD_FOR_FRAMECOUNT", 2},
	{"UNIT_IDLE_FOR_FRAMECOUNT", 2}, {"TEAM_GUARD_FOR_FRAMECOUNT", 2}, {"TEAM_IDLE_FOR_FRAMECOUNT", 2},
	{"WATER_CHANGE_HEIGHT", 2}, {"NAMED_USE_COMMANDBUTTON_ABILITY_ON_NAMED", 3},
	{"NAMED_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT", 3}, {"WATER_CHANGE_HEIGHT_OVER_TIME", 4}, {"MAP_SWITCH_BORDER", 1},
	{"TEAM_GUARD_POSITION", 2}, {"TEAM_GUARD_OBJECT", 2}, {"TEAM_GUARD_AREA", 2}, {"OBJECT_FORCE_SELECT", 4},
	{"CAMERA_LOOK_TOWARD_WAYPOINT", 5}, {"UNIT_DESTROY_ALL_CONTAINED", 1}, {"RADAR_FORCE_ENABLE", 0},
	{"RADAR_REVERT_TO_NORMAL", 0}, {"SCREEN_SHAKE", 1}, {"TECHTREE_MODIFY_BUILDABILITY_OBJECT", 2},
	{"WAREHOUSE_SET_VALUE", 2}, {"OBJECT_CREATE_RADAR_EVENT", 2}, {"TEAM_CREATE_RADAR_EVENT", 2},
	{"DISPLAY_CINEMATIC_TEXT", 3}, {"DEBUG_CRASH_BOX", 1}, {"SOUND_DISABLE_TYPE", 1}, {"SOUND_ENABLE_TYPE", 1},
	{"SOUND_ENABLE_ALL", 0}, {"AUDIO_OVERRIDE_VOLUME_TYPE", 2}, {"AUDIO_RESTORE_VOLUME_TYPE", 1},
	{"AUDIO_RESTORE_VOLUME_ALL_TYPE", 0}, {"INGAME_POPUP_MESSAGE", 5}, {"SET_CAVE_INDEX", 2}, {"NAMED_SET_HELD", 2},
	{"NAMED_SET_TOPPLE_DIRECTION", 2}, {"UNIT_MOVE_TOWARDS_NEAREST_OBJECT_TYPE", 3},
	{"TEAM_MOVE_TOWARDS_NEAREST_OBJECT_TYPE", 3}, {"MAP_REVEAL_ALL_PERM", 1}, {"MAP_REVEAL_ALL_UNDO_PERM", 1},
	{"NAMED_SET_REPULSOR", 2}, {"TEAM_SET_REPULSOR", 2}, {"TEAM_WANDER_IN_PLACE", 1}, {"TEAM_INCREASE_PRIORITY", 1},
	{"TEAM_DECREASE_PRIORITY", 1}, {"DISPLAY_COUNTER", 2}, {"HIDE_COUNTER", 1},
	{"TEAM_USE_COMMANDBUTTON_ABILITY_ON_NAMED", 3}, {"TEAM_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT", 3},
	{"NAMED_USE_COMMANDBUTTON_ABILITY", 2}, {"TEAM_USE_COMMANDBUTTON_ABILITY", 2}, {"NAMED_FLASH_WHITE", 2},
	{"TEAM_FLASH_WHITE", 2}, {"SKIRMISH_BUILD_BUILDING", 1}, {"SKIRMISH_FOLLOW_APPROACH_PATH", 3},
	{"IDLE_ALL_UNITS", 0}, {"RESUME_SUPPLY_TRUCKING", 0}, {"NAMED_CUSTOM_COLOR", 2},
	{"SKIRMISH_MOVE_TO_APPROACH_PATH", 2}, {"SKIRMISH_BUILD_BASE_DEFENSE_FRONT", 0},
	{"SKIRMISH_FIRE_SPECIAL_POWER_AT_MOST_COST", 2}, {"NAMED_RECEIVE_UPGRADE", 0}, {"PLAYER_REPAIR_NAMED_STRUCTURE", 2},
	{"SKIRMISH_BUILD_BASE_DEFENSE_FLANK", 0}, {"SKIRMISH_BUILD_STRUCTURE_FRONT", 1},
	{"SKIRMISH_BUILD_STRUCTURE_FLANK", 1}, {"SKIRMISH_ATTACK_NEAREST_GROUP_WITH_VALUE", 3},
	{"SKIRMISH_PERFORM_COMMANDBUTTON_ON_MOST_VALUABLE_OBJECT", 4}, {"SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_ALL", 3},
	{"SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_PARTIAL", 3}, {"TEAM_SPIN_FOR_FRAMECOUNT", 2},
	{"TEAM_ALL_USE_COMMANDBUTTON_ON_NAMED", 2}, {"TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_UNIT", 2},
	{"TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_GARRISONED_BUILDING", 2},
	{"TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_KINDOF", 3}, {"TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_BUILDING", 2},
	{"TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_BUILDING_CLASS", 3},
	{"TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_OBJECTTYPE", 3}, {"TEAM_PARTIAL_USE_COMMANDBUTTON", 3},
	{"TEAM_CAPTURE_NEAREST_UNOWNED_FACTION_UNIT", 1}, {"PLAYER_CREATE_TEAM_FROM_CAPTURED_UNITS", 2},
	{"PLAYER_ADD_SKILLPOINTS", 2}, {"PLAYER_ADD_RANKLEVEL", 2}, {"PLAYER_SET_RANKLEVEL", 2},
	{"PLAYER_SET_RANKLEVELLIMIT", 1}, {"PLAYER_GRANT_SCIENCE", 2}, {"PLAYER_PURCHASE_SCIENCE", 2},
	{"TEAM_HUNT_WITH_COMMAND_BUTTON", 2}, {"TEAM_WAIT_FOR_NOT_CONTAINED_ALL", 1},
	{"TEAM_WAIT_FOR_NOT_CONTAINED_PARTIAL", 1}, {"TEAM_FOLLOW_WAYPOINTS_EXACT", 3}, {"NAMED_FOLLOW_WAYPOINTS_EXACT", 2},
	{"TEAM_SET_EMOTICON", 3}, {"NAMED_SET_EMOTICON", 3}, {"AI_PLAYER_BUILD_SUPPLY_CENTER", 3},
	{"AI_PLAYER_BUILD_UPGRADE", 2}, {"OBJECTLIST_ADDOBJECTTYPE", 2}, {"OBJECTLIST_REMOVEOBJECTTYPE", 2},
	{"MAP_REVEAL_PERMANENTLY_AT_WAYPOINT", 4}, {"MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT", 1},
	{"NAMED_SET_STEALTH_ENABLED", 2}, {"TEAM_SET_STEALTH_ENABLED", 2}, {"EVA_SET_ENABLED_DISABLED", 1},
	{"OPTIONS_SET_OCCLUSION_MODE", 1}, {"LOCALDEFEAT", 0}, {"OPTIONS_SET_DRAWICON_UI_MODE", 1},
	{"OPTIONS_SET_PARTICLE_CAP_MODE", 1}, {"PLAYER_SCIENCE_AVAILABILITY", 3}, {"UNIT_AFFECT_OBJECT_PANEL_FLAGS", 3},
	{"TEAM_AFFECT_OBJECT_PANEL_FLAGS", 3}, {"PLAYER_SELECT_SKILLSET", 2}, {"SCRIPTING_OVERRIDE_HULK_LIFETIME", 1},
	{"NAMED_FACE_NAMED", 2}, {"NAMED_FACE_WAYPOINT", 2}, {"TEAM_FACE_NAMED", 2}, {"TEAM_FACE_WAYPOINT", 2},
	{"COMMANDBAR_REMOVE_BUTTON_OBJECTTYPE", 2}, {"COMMANDBAR_ADD_BUTTON_OBJECTTYPE_SLOT", 3},
	{"UNIT_SPAWN_NAMED_LOCATION_ORIENTATION", 5}, {"PLAYER_AFFECT_RECEIVING_EXPERIENCE", 2},
	{"PLAYER_EXCLUDE_FROM_SCORE_SCREEN", 1}, {"TEAM_GUARD_SUPPLY_CENTER", 2}, {"ENABLE_SCORING", 0},
	{"DISABLE_SCORING", 0}, {"SOUND_SET_VOLUME", 1}, {"SPEECH_SET_VOLUME", 1}, {"DISABLE_BORDER_SHROUD", 0},
	{"ENABLE_BORDER_SHROUD", 0}, {"OBJECT_ALLOW_BONUSES", 1}, {"SOUND_REMOVE_ALL_DISABLED", 0},
	{"SOUND_REMOVE_TYPE", 1}, {"TEAM_GUARD_IN_TUNNEL_NETWORK", 1}, {"QUICKVICTORY", 0},
	{"SET_INFANTRY_LIGHTING_OVERRIDE", 1}, {"RESET_INFANTRY_LIGHTING_OVERRIDE", 0}, {"TEAM_DELETE_LIVING", 1},
	{"RESIZE_VIEW_GUARDBAND", 2}, {"DELETE_ALL_UNMANNED", 0}, {"CHOOSE_VICTIM_ALWAYS_USES_NORMAL", 1},
	{"CAMERA_ENABLE_SLAVE_MODE", 2}, {"CAMERA_DISABLE_SLAVE_MODE", 0}, {"CAMERA_ADD_SHAKER_AT", 4},
	{"SET_TRAIN_HELD", 2}, {"NAMED_SET_EVAC_LEFT_OR_RIGHT", 2}, {"ENABLE_OBJECT_SOUND", 1}, {"DISABLE_OBJECT_SOUND", 1},
	{"NAMED_USE_COMMANDBUTTON_ABILITY_USING_WAYPOINT_PATH", 3}, {"NAMED_SET_UNMANNED_STATUS", 1},
	{"TEAM_SET_UNMANNED_STATUS", 1}, {"NAMED_SET_BOOBYTRAPPED", 2}, {"TEAM_SET_BOOBYTRAPPED", 2}, {"SHOW_WEATHER", 1},
	{"AI_PLAYER_BUILD_TYPE_NEAREST_TEAM", 3},
}};

inline constexpr std::array<LegacyCallTemplate, 109> LegacyConditionTemplates{{
	{"CONDITION_FALSE", 0}, {"COUNTER", 3}, {"FLAG", 2}, {"CONDITION_TRUE", 0}, {"TIMER_EXPIRED", 1},
	{"PLAYER_ALL_DESTROYED", 1}, {"PLAYER_ALL_BUILDFACILITIES_DESTROYED", 1}, {"TEAM_INSIDE_AREA_PARTIALLY", 3},
	{"TEAM_DESTROYED", 1}, {"CAMERA_MOVEMENT_FINISHED", 0}, {"TEAM_HAS_UNITS", 1}, {"TEAM_STATE_IS", 2},
	{"TEAM_STATE_IS_NOT", 2}, {"NAMED_INSIDE_AREA", 2}, {"NAMED_OUTSIDE_AREA", 2}, {"NAMED_DESTROYED", 1},
	{"NAMED_NOT_DESTROYED", 1}, {"TEAM_INSIDE_AREA_ENTIRELY", 3}, {"TEAM_OUTSIDE_AREA_ENTIRELY", 3},
	{"NAMED_ATTACKED_BY_OBJECTTYPE", 2}, {"TEAM_ATTACKED_BY_OBJECTTYPE", 2}, {"NAMED_ATTACKED_BY_PLAYER", 2},
	{"TEAM_ATTACKED_BY_PLAYER", 2}, {"BUILT_BY_PLAYER", 2}, {"NAMED_CREATED", 1}, {"TEAM_CREATED", 1},
	{"PLAYER_HAS_CREDITS", 3}, {"NAMED_DISCOVERED", 2}, {"TEAM_DISCOVERED", 2}, {"MISSION_ATTEMPTS", 3},
	{"NAMED_OWNED_BY_PLAYER", 2}, {"TEAM_OWNED_BY_PLAYER", 2}, {"PLAYER_HAS_N_OR_FEWER_BUILDINGS", 2},
	{"PLAYER_HAS_POWER", 1}, {"NAMED_REACHED_WAYPOINTS_END", 2}, {"TEAM_REACHED_WAYPOINTS_END", 2},
	{"NAMED_SELECTED", 1}, {"NAMED_ENTERED_AREA", 2}, {"NAMED_EXITED_AREA", 2}, {"TEAM_ENTERED_AREA_ENTIRELY", 3},
	{"TEAM_ENTERED_AREA_PARTIALLY", 3}, {"TEAM_EXITED_AREA_ENTIRELY", 3}, {"TEAM_EXITED_AREA_PARTIALLY", 3},
	{"MULTIPLAYER_ALLIED_VICTORY", 0}, {"MULTIPLAYER_ALLIED_DEFEAT", 0}, {"MULTIPLAYER_PLAYER_DEFEAT", 0},
	{"PLAYER_HAS_NO_POWER", 1}, {"HAS_FINISHED_VIDEO", 1}, {"HAS_FINISHED_SPEECH", 1}, {"HAS_FINISHED_AUDIO", 1},
	{"BUILDING_ENTERED_BY_PLAYER", 2}, {"ENEMY_SIGHTED", 3}, {"UNIT_HEALTH", 3}, {"BRIDGE_REPAIRED", 1},
	{"BRIDGE_BROKEN", 1}, {"NAMED_DYING", 1}, {"NAMED_TOTALLY_DEAD", 1}, {"PLAYER_HAS_OBJECT_COMPARISON", 4},
	{"OBSOLETE_SCRIPT_1", 0}, {"OBSOLETE_SCRIPT_2", 0}, {"PLAYER_TRIGGERED_SPECIAL_POWER", 2},
	{"PLAYER_COMPLETED_SPECIAL_POWER", 2}, {"PLAYER_MIDWAY_SPECIAL_POWER", 2},
	{"PLAYER_TRIGGERED_SPECIAL_POWER_FROM_NAMED", 3}, {"PLAYER_COMPLETED_SPECIAL_POWER_FROM_NAMED", 3},
	{"PLAYER_MIDWAY_SPECIAL_POWER_FROM_NAMED", 3}, {"DEFUNCT_PLAYER_SELECTED_GENERAL", 0},
	{"DEFUNCT_PLAYER_SELECTED_GENERAL_FROM_NAMED", 0}, {"PLAYER_BUILT_UPGRADE", 2},
	{"PLAYER_BUILT_UPGRADE_FROM_NAMED", 3}, {"PLAYER_DESTROYED_N_BUILDINGS_PLAYER", 3},
	{"UNIT_COMPLETED_SEQUENTIAL_EXECUTION", 0}, {"TEAM_COMPLETED_SEQUENTIAL_EXECUTION", 0},
	{"PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA", 5}, {"PLAYER_HAS_COMPARISON_UNIT_KIND_IN_TRIGGER_AREA", 5},
	{"UNIT_EMPTIED", 1}, {"TYPE_SIGHTED", 3}, {"NAMED_BUILDING_IS_EMPTY", 1},
	{"PLAYER_HAS_N_OR_FEWER_FACTION_BUILDINGS", 2}, {"UNIT_HAS_OBJECT_STATUS", 2}, {"TEAM_ALL_HAS_OBJECT_STATUS", 2},
	{"TEAM_SOME_HAVE_OBJECT_STATUS", 2}, {"PLAYER_POWER_COMPARE_PERCENT", 3}, {"PLAYER_EXCESS_POWER_COMPARE_VALUE", 3},
	{"SKIRMISH_SPECIAL_POWER_READY", 2}, {"SKIRMISH_VALUE_IN_AREA", 4}, {"SKIRMISH_PLAYER_FACTION", 2},
	{"SKIRMISH_SUPPLIES_VALUE_WITHIN_DISTANCE", 4}, {"SKIRMISH_TECH_BUILDING_WITHIN_DISTANCE", 3},
	{"SKIRMISH_COMMAND_BUTTON_READY_ALL", 3}, {"SKIRMISH_COMMAND_BUTTON_READY_PARTIAL", 3},
	{"SKIRMISH_UNOWNED_FACTION_UNIT_EXISTS", 3}, {"SKIRMISH_PLAYER_HAS_PREREQUISITE_TO_BUILD", 2},
	{"SKIRMISH_PLAYER_HAS_COMPARISON_GARRISONED", 3}, {"SKIRMISH_PLAYER_HAS_COMPARISON_CAPTURED_UNITS", 3},
	{"SKIRMISH_NAMED_AREA_EXIST", 2}, {"SKIRMISH_PLAYER_HAS_UNITS_IN_AREA", 2},
	{"SKIRMISH_PLAYER_HAS_BEEN_ATTACKED_BY_PLAYER", 2}, {"SKIRMISH_PLAYER_IS_OUTSIDE_AREA", 2},
	{"SKIRMISH_PLAYER_HAS_DISCOVERED_PLAYER", 2}, {"PLAYER_ACQUIRED_SCIENCE", 2},
	{"PLAYER_HAS_SCIENCEPURCHASEPOINTS", 2}, {"PLAYER_CAN_PURCHASE_SCIENCE", 2}, {"MUSIC_TRACK_HAS_COMPLETED", 2},
	{"PLAYER_LOST_OBJECT_TYPE", 2}, {"SUPPLY_SOURCE_SAFE", 2}, {"SUPPLY_SOURCE_ATTACKED", 1}, {"START_POSITION_IS", 2},
	{"NAMED_HAS_FREE_CONTAINER_SLOTS", 1},
}};

namespace legacy_parameter
{
// Parameter::ParameterType codes.
inline constexpr std::uint32_t Integer = 0, RealNumber = 1, Boolean = 8, Side = 11, ObjectType = 15, AiMood = 20, KindOf = 27, Upgrade = 32,
	SurfacesAllowed = 36, Percent = 51;
}

inline const LegacyCallTemplate *FindLegacyTemplate(std::span<const LegacyCallTemplate> templates, std::string_view name) noexcept
{
	for (const LegacyCallTemplate &entry : templates)
		if (entry.name == name)
			return &entry;
	return nullptr;
}

inline engine::level::ScriptParameter LegacyParameter(std::uint32_t kind, std::int64_t integer = 0)
{
	engine::level::ScriptParameter parameter;
	parameter.kind = kind;
	parameter.integer = integer;
	return parameter;
}

inline bool SameNoCase(std::string_view a, std::string_view b) noexcept
{
	return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) {
		return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
	});
}

// Parameter::ReadParameter's fix-ups, on every parameter of a call: an OBJECT_TYPE starting "Fundamentalist" is
// GLA's; the three obsolete capture-building UPGRADEs are Upgrade_InfantryCaptureBuilding; a KIND_OF_PARAM's name
// (any case) is its KindOf bit (`kindOfNames`: the KindOf table in order). Its quirks kept: CRUSHER, CRUSHABLE and
// OVERLAPPABLE are bit 0 (OBSTACLE: tested on the first entry), MISSILE becomes "SMALL_MISSILE" at bit 0 (its search
// compares the name with itself); a name nothing matches is left (the original fails the map's load).
inline void HealLegacyParameters(engine::level::ScriptCall &call, std::span<const std::string_view> kindOfNames)
{
	using namespace legacy_parameter;
	for (engine::level::ScriptParameter &parameter : call.parameters)
	{
		if (parameter.kind == ObjectType && parameter.text.starts_with("Fundamentalist"))
			parameter.text = "GLA" + parameter.text.substr(std::string_view{"Fundamentalist"}.size());
		else if (parameter.kind == Upgrade && (parameter.text == "Upgrade_AmericaRangerCaptureBuilding" ||
					 parameter.text == "Upgrade_ChinaRedguardCaptureBuilding" || parameter.text == "Upgrade_GLARebelCaptureBuilding"))
			parameter.text = "Upgrade_InfantryCaptureBuilding";
		else if (parameter.kind == KindOf && !parameter.text.empty())
			for (std::size_t index = 0; index < kindOfNames.size(); ++index)
			{
				if (SameNoCase(parameter.text, kindOfNames[index]))
				{
					parameter.integer = static_cast<std::int64_t>(index);
					break;
				}
				if (SameNoCase(parameter.text, "CRUSHER") || SameNoCase(parameter.text, "CRUSHABLE") || SameNoCase(parameter.text, "OVERLAPPABLE"))
				{
					parameter.integer = static_cast<std::int64_t>(index);
					break;
				}
				if (SameNoCase(parameter.text, "MISSILE"))
				{
					parameter.text = "SMALL_MISSILE";
					parameter.integer = 0;
					break;
				}
			}
	}
}

// ScriptAction::ParseAction after the name is matched: "heal old files" (parameters older maps lack, with their
// defaults), then an action whose parameter count is not its template's becomes NO_OP with none ("Invalid script
// action. Making it noop."); a name no template has is NO_OP too.
inline void HealLegacyAction(engine::level::ScriptCall &call)
{
	using namespace legacy_parameter;
	auto &p = call.parameters;
	const std::string_view name = call.name;
	if (name == "SKIRMISH_FIRE_SPECIAL_POWER_AT_MOST_COST")
	{
		if (p.size() == 1)
		{
			auto side = LegacyParameter(Side);
			side.text = "<This Player>"; // THIS_PLAYER
			p.insert(p.begin(), std::move(side));
		}
	}
	else if (name == "TEAM_FOLLOW_WAYPOINTS")
	{
		if (p.size() == 2)
			p.push_back(LegacyParameter(Boolean, 1));
	}
	else if (name == "SKIRMISH_BUILD_BASE_DEFENSE_FRONT")
	{
		if (p.size() == 1)
		{
			const bool flank = p[0].integer != 0;
			p.clear();
			if (flank)
				call.name = "SKIRMISH_BUILD_BASE_DEFENSE_FLANK";
		}
	}
	else if (name == "NAMED_SET_ATTITUDE" || name == "TEAM_SET_ATTITUDE")
	{
		if (p.size() >= 2 && p[1].kind == Integer)
			p[1] = LegacyParameter(AiMood, p[1].integer);
	}
	else if (name == "MAP_REVEAL_AT_WAYPOINT" || name == "MAP_SHROUD_AT_WAYPOINT")
	{
		if (p.size() == 2)
			p.push_back(LegacyParameter(Side));
	}
	else if (name == "MAP_REVEAL_ALL" || name == "MAP_REVEAL_ALL_PERM" || name == "MAP_REVEAL_ALL_UNDO_PERM" || name == "MAP_SHROUD_ALL")
	{
		if (p.empty())
			p.push_back(LegacyParameter(Side));
	}
	else if (name == "SPEECH_PLAY")
	{
		if (p.size() == 1)
			p.push_back(LegacyParameter(Boolean, 1)); // "Default it to TRUE"
	}
	else if (name == "CAMERA_MOD_SET_FINAL_ZOOM" || name == "CAMERA_MOD_SET_FINAL_PITCH")
	{
		if (p.size() == 1)
		{
			p.push_back(LegacyParameter(Percent));
			p.push_back(LegacyParameter(Percent));
		}
	}
	else if (name == "MOVE_CAMERA_TO" || name == "MOVE_CAMERA_ALONG_WAYPOINT_PATH" || name == "CAMERA_LOOK_TOWARD_OBJECT")
	{
		if (p.size() == 3)
		{
			p.push_back(LegacyParameter(RealNumber));
			p.push_back(LegacyParameter(RealNumber));
		}
	}
	else if (name == "RESET_CAMERA" || name == "ZOOM_CAMERA" || name == "PITCH_CAMERA" || name == "ROTATE_CAMERA")
	{
		if (p.size() == 2)
		{
			p.push_back(LegacyParameter(RealNumber));
			p.push_back(LegacyParameter(RealNumber));
		}
	}
	else if (name == "CAMERA_LOOK_TOWARD_WAYPOINT")
	{
		if (p.size() == 2)
		{
			p.push_back(LegacyParameter(RealNumber));
			p.push_back(LegacyParameter(RealNumber));
			p.push_back(LegacyParameter(Boolean, 0));
		}
		else if (p.size() == 4)
			p.push_back(LegacyParameter(Boolean, 0));
	}
	const LegacyCallTemplate *entry = FindLegacyTemplate(LegacyActionTemplates, call.name);
	if (entry == nullptr || entry->parameters != p.size())
	{
		call.name = "NO_OP";
		p.clear();
	}
}

// Condition::ParseConditionDataChunk after the name is matched: a version 1 chunk's seven area conditions gain their
// SURFACES_ALLOWED (3: air and ground), an old SKIRMISH_SPECIAL_POWER_READY its side (this player), then a condition
// whose parameter count is not its template's, or whose name no template has, is CONDITION_FALSE with none.
inline void HealLegacyCondition(engine::level::ScriptCall &call)
{
	using namespace legacy_parameter;
	auto &p = call.parameters;
	static constexpr std::array<std::string_view, 7> ParameterChangesVer2{"TEAM_INSIDE_AREA_PARTIALLY", "TEAM_INSIDE_AREA_ENTIRELY",
		"TEAM_OUTSIDE_AREA_ENTIRELY", "TEAM_ENTERED_AREA_PARTIALLY", "TEAM_ENTERED_AREA_ENTIRELY", "TEAM_EXITED_AREA_ENTIRELY",
		"TEAM_EXITED_AREA_PARTIALLY"};
	if (call.version < 2 && std::ranges::find(ParameterChangesVer2, std::string_view{call.name}) != ParameterChangesVer2.end())
	{
		// m_parms[m_numParms] = SURFACES_ALLOWED 3; m_numParms = 3
		if (p.size() < 3)
			p.push_back(LegacyParameter(SurfacesAllowed, 3));
		p.resize(3);
	}
	if (call.name == "SKIRMISH_SPECIAL_POWER_READY" && p.size() == 1)
	{
		auto side = LegacyParameter(Side);
		side.text = "<This Player>";
		p.insert(p.begin(), std::move(side));
	}
	const LegacyCallTemplate *entry = FindLegacyTemplate(LegacyConditionTemplates, call.name);
	if (entry == nullptr || entry->parameters != p.size())
	{
		call.name = "CONDITION_FALSE";
		p.clear();
	}
}

// The runtime's names for calls stored without one, by code.
inline std::vector<std::string> LegacyNames(std::span<const LegacyCallTemplate> templates)
{
	std::vector<std::string> names;
	names.reserve(templates.size());
	for (const LegacyCallTemplate &entry : templates)
		names.emplace_back(entry.name);
	return names;
}

// The vocabulary reads stored calls as Zero Hour does: unnamed (old) calls by their code, every call's parameters and
// the call itself healed as the original's parsers do.
inline void UseLegacyCallReading(engine::scripting::Vocabulary &vocabulary, std::span<const std::string_view> kindOfNames)
{
	vocabulary.SetActionKinds(LegacyNames(LegacyActionTemplates));
	vocabulary.SetConditionKinds(LegacyNames(LegacyConditionTemplates));
	vocabulary.SetActionHealer([kindOfNames](engine::level::ScriptCall &call) {
		HealLegacyParameters(call, kindOfNames);
		HealLegacyAction(call);
	});
	vocabulary.SetConditionHealer([kindOfNames](engine::level::ScriptCall &call) {
		HealLegacyParameters(call, kindOfNames);
		HealLegacyCondition(call);
	});
}
}
