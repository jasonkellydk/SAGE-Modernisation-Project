export module games.generalszh.presentation.scripted.resources.scripted_presentation;
import std;

export import engine.core.serialization.byte_stream;

export namespace generalszh::presentation
{
// What the map's scripts set for the player's presentation: music, levels
// (percent), speech, and display switches. Audio and the in-game UI read it.
struct ScriptedPresentation
{
	std::string musicTrack;
	std::int64_t musicVolume{100};
	std::int64_t soundVolume{100};
	std::int64_t speechVolume{100};
	std::vector<std::string> speechQueue;
	std::vector<std::string> disabledSounds;
	std::vector<std::pair<std::string, std::int64_t>> soundVolumeOverrides;
	bool evaEnabled{true};
	bool radarForced{false};
	// VictoryConditions' m_localPlayerDefeated: the local player's defeat was seen (the radar forced on then, once).
	bool localDefeatSeen{false};
	bool radarHidden{false}; // RADAR_DISABLE: the local player's radar is hidden (Radar::hide)
	bool borderShroudDisabled{false};
	bool drawIconUi{true};
	bool occlusion{true};
	bool particleCap{true};
	bool specialPowerDisplayDisabled{false};
	// The end of the game as the local player's scripts declared it (VICTORY, DEFEAT, LOCALDEFEAT: which window shows),
	// and whether that took the player's input away (doDisableInput: victory and defeat).
	enum class MatchEnd : std::uint8_t { None, Victory, Defeat, LocalDefeat, QuickVictory };
	MatchEnd matchEnd{MatchEnd::None};
	std::uint64_t matchEndTick{0};
	bool inputDisabled{false};
	// CAMERA_LETTERBOX_BEGIN / END (doLetterBoxMode): the bars on (the control bar hidden) or off, and how many times it
	// changed (the host times their fade on real time).
	bool letterbox{false};
	std::uint32_t letterboxChanges{0};
	// CAMERA_BW_MODE_BEGIN / END: the view in black and white, faded over `blackWhiteFrames`.
	bool blackWhite{false};
	std::int64_t blackWhiteFrames{0};
	// MOVIE_PLAY_FULLSCREEN / MOVIE_PLAY_RADAR: movies the scripts asked for, not yet played (the host takes them).
	std::vector<std::string> movies;
	// DRAW_SKYBOX_BEGIN / END (GlobalData m_drawSkyBox): the water's sky box drawn around the camera.
	bool skyBox{false};
	// SHOW_WEATHER (SnowManager::setVisible): the map's weather shows (on by default, when the map has any).
	bool weatherShown{true};
	// SET_ / RESET_INFANTRY_LIGHTING_OVERRIDE (GlobalData m_scriptOverrideInfantryLightScale): -1 for none.
	float infantryLightOverride{-1.0f};
	// REFRESH_RADAR: how many times the scripts asked for the radar's terrain to be drawn again (the host redraws it).
	std::uint32_t radarRefreshes{0};
};

// A new match on (GameClient's start of a game): what the last one's scripts set that goes with it. GameLogic::reset:
// the icon UI drawn again (m_drawIconUI = TRUE, which a shell map's DRAW_ICON_UI turned off); the game's end undeclared
// and input given back; Radar::reset (newMap): no longer forced on (a hidden radar stays hidden: the retail code keeps
// it), the local defeat unseen; W3DShroud::init: the new map's border shrouded again.
inline void ResetForNewMatch(ScriptedPresentation &settings) noexcept
{
	settings.drawIconUi = true;
	settings.matchEnd = ScriptedPresentation::MatchEnd::None;
	settings.inputDisabled = false;
	settings.radarForced = false;
	settings.localDefeatSeen = false;
	settings.borderShroudDisabled = false;
	// SnowManager::reset (GameClient::reset): the map's weather shows again.
	settings.weatherShown = true;
}

// Its part of a saved game (the scripts' settings for the player's presentation), in order.
inline void SaveScriptedPresentation(const ScriptedPresentation &settings, engine::core::serialization::ByteWriter &writer)
{
	writer.Text(settings.musicTrack);
	writer.I64(settings.musicVolume);
	writer.I64(settings.soundVolume);
	writer.I64(settings.speechVolume);
	writer.U32(static_cast<std::uint32_t>(settings.disabledSounds.size()));
	for (const std::string &sound : settings.disabledSounds)
		writer.Text(sound);
	writer.U32(static_cast<std::uint32_t>(settings.soundVolumeOverrides.size()));
	for (const auto &[sound, volume] : settings.soundVolumeOverrides)
	{
		writer.Text(sound);
		writer.I64(volume);
	}
	for (const bool flag : {settings.evaEnabled, settings.radarForced, settings.radarHidden, settings.borderShroudDisabled, settings.drawIconUi,
			 settings.occlusion, settings.particleCap, settings.specialPowerDisplayDisabled, settings.inputDisabled, settings.localDefeatSeen})
		writer.Flag(flag);
}

// Read back over `settings` (what a save does not hold kept); false when the bytes run out.
inline bool LoadScriptedPresentation(ScriptedPresentation &settings, engine::core::serialization::ByteReader &reader)
{
	settings.musicTrack = reader.Text().value_or("");
	settings.musicVolume = reader.I64().value_or(100);
	settings.soundVolume = reader.I64().value_or(100);
	settings.speechVolume = reader.I64().value_or(100);
	settings.disabledSounds.clear();
	for (std::uint32_t count = reader.U32().value_or(0); count > 0 && !reader.Failed(); --count)
		settings.disabledSounds.push_back(reader.Text().value_or(""));
	settings.soundVolumeOverrides.clear();
	for (std::uint32_t count = reader.U32().value_or(0); count > 0 && !reader.Failed(); --count)
	{
		std::string sound = reader.Text().value_or("");
		settings.soundVolumeOverrides.emplace_back(std::move(sound), reader.I64().value_or(100));
	}
	for (bool *flag : {&settings.evaEnabled, &settings.radarForced, &settings.radarHidden, &settings.borderShroudDisabled, &settings.drawIconUi,
			 &settings.occlusion, &settings.particleCap, &settings.specialPowerDisplayDisabled, &settings.inputDisabled, &settings.localDefeatSeen})
		*flag = reader.Flag().value_or(*flag);
	return !reader.Failed();
}
}
