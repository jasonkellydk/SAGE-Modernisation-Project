export module games.generalszh.presentation.audio.resources.audio_resources;
import std;
export import engine.gameplay.common.identity.resources.relationships;

export import engine.audio.playback.sound_player;
export import games.generalszh.presentation.audio.audio_content;
import engine.ecs.system.system;

// Sound's singleton components (world resources, never hashed or saved):
// the player, mixer and sound content it plays through; its state (the
// music track, the speech queue, the levels the scripts set, the sounds they
// disabled); the scripts' audio commands this frame; and where the camera
// is, for the microphone. Plus the small utilities the sound systems share.
export namespace generalszh::presentation
{
struct AudioHandle
{
	const AudioContent *content{nullptr};
	engine::audio::SoundPlayer *player{nullptr};
	engine::audio::Mixer *mixer{nullptr};
};

struct AudioCommand
{
	enum class Kind : std::uint8_t
	{
		MusicTrack,   // MUSIC_SET_TRACK (`flag`: fade the old one out)
		MusicVolume,  // `share` of the default level
		SoundVolume,
		SpeechVolume,
		SpeechPlay,   // queued, played in turn
		SoundDisable, // SOUND_DISABLE_TYPE: setAudioEventVolumeOverride(`text`, 0): what plays goes silent, what comes is culled
		SoundEnable,  // SOUND_ENABLE_TYPE / AUDIO_RESTORE_VOLUME_TYPE: its override gone (empty text: every override gone)
		SoundStop,    // SOUND_REMOVE_TYPE: removeAudioEvent(`text`): what plays of it stops
		SoundStopMuted, // SOUND_REMOVE_ALL_DISABLED: removeAllDisabledAudio: what plays at no volume stops
		VolumeOverride, // AUDIO_OVERRIDE_VOLUME_TYPE: `text` plays at `share` (negative: its own again; empty text: all their own)
		Interface,      // a front-end sound (`text`, e.g. GUIClick), heard without a position (`flag`: the menu's
		                // voice, in place of the one before; empty text: that voice stops)
		UserMusic,      // the player's own volumes (Options.ini), `share` 0..1, in place of the settings' defaults
		UserSound,
		UserSound3D,
		UserSpeech,
		Eva,            // EVA's line (`text`), heard without a position: its voice
		FlatSoundsPaused, // SUSPEND_ / RESUME_BACKGROUND_SOUNDS: pauseAudio / resumeAudio(AudioAffect_Sound) (`flag`: paused)
	};
	Kind kind{Kind::MusicTrack};
	std::string text;
	bool flag{false};
	float share{1.0f};
};

struct AudioCommands
{
	std::vector<AudioCommand> pending;
};

struct AudioState
{
	std::string musicName;
	engine::audio::SoundHandle music{0};
	std::uint32_t musicCompletions{0}; // how often the track has played through since it was set (hasMusicTrackCompleted)
	// The progress last told the simulation (a single-player mission's scripts ask it through a MusicProgress command).
	std::string reportedMusic;
	std::uint32_t reportedCompletions{0};
	std::vector<std::string> speech; // queued, in turn
	engine::audio::SoundHandle speaking{0};
	engine::audio::SoundHandle interfaceVoice{0}; // the last front-end voice (a challenge general's preview)
	// EVA's voice (Eva::m_evaSpeech): its last line, how many lines it was handed, and whether it still speaks.
	engine::audio::SoundHandle eva{0};
	std::uint32_t evaServed{0};
	bool evaSpeaking{false};
	float scriptMusic{1.0f};
	float scriptSound{1.0f};
	float scriptSpeech{1.0f};
	bool levelsApplied{false};
	std::vector<std::pair<std::string, float>> volumeOverrides; // by event name, sorted
	// The player's volumes (negative: the settings' defaults).
	float userMusic{-1.0f};
	float userSound{-1.0f};
	float userSound3D{-1.0f};
	float userSpeech{-1.0f};
	// AudioManager::update's zoom volume: world (3D) sounds' share for how near the camera is to the microphone.
	float zoomVolume{1.0f};
};

// AudioManager::shouldPlayLocally: who hears a sound. Music, a sound for no one in particular (no PLAYER, ALLIES,
// ENEMIES or EVERYONE) and an EVERYONE sound play; a player-bound UI sound without a player plays; otherwise it needs
// its player and the viewer's eyes (none: nobody's, as an observer looking at nobody): a PLAYER sound only its own
// player hears, an ALLIES one its allies (not itself), an ENEMIES one its enemies.
inline bool Audible(const engine::audio::SoundEventDefinition &sound, std::uint32_t owner, std::uint32_t viewer,
	const engine::gameplay::Relationships *relationships)
{
	namespace type = engine::audio::sound_type;
	constexpr std::uint32_t Nobody = 0xFFFFFFFFu;
	if (sound.bus == engine::audio::Bus::Music)
		return true;
	if ((sound.type & (type::Player | type::Allies | type::Enemies | type::Everyone)) == 0 || (sound.type & type::Everyone) != 0)
		return true;
	if ((sound.type & type::Player) != 0 && (sound.type & type::Interface) != 0 && owner == Nobody)
		return true;
	if (owner == Nobody || viewer == Nobody)
		return false;
	if ((sound.type & type::Player) != 0)
		return owner == viewer;
	const auto between = [&] {
		return relationships != nullptr ? relationships->Between(owner, viewer)
										: (owner == viewer ? engine::gameplay::Relationship::Allies : engine::gameplay::Relationship::Neutral);
	};
	if ((sound.type & type::Allies) != 0)
		return owner != viewer && between() == engine::gameplay::Relationship::Allies;
	if ((sound.type & type::Enemies) != 0)
		return between() == engine::gameplay::Relationship::Enemies;
	return false;
}

// The volume a script set for `event`, if any.
inline std::optional<float> VolumeOverride(const AudioState &state, std::string_view event)
{
	const auto found = std::lower_bound(state.volumeOverrides.begin(), state.volumeOverrides.end(), event,
		[](const auto &entry, std::string_view name) { return entry.first < name; });
	return found != state.volumeOverrides.end() && found->first == event ? std::optional(found->second) : std::nullopt;
}

// Plays `sound` as the scripts want it heard (at their volume for it, if they set one).
inline engine::audio::SoundHandle PlaySound(engine::audio::SoundPlayer &player, const AudioState &state, const engine::audio::SoundEventDefinition &sound,
	std::optional<engine::audio::Vec3> position = std::nullopt)
{
	return player.Play(sound, position, VolumeOverride(state, sound.name));
}

struct ListenerPose
{
	std::array<float, 3> eye{};
	std::array<float, 3> ground{}; // the ground point the camera looks at
	std::array<float, 3> right{1, 0, 0};
	bool placed{false};
};

// The sound `event` names, unless it is empty or a script turned it down to nothing (AudioManager::addAudioEvent culls
// what an override leaves below MinSampleVolume).
inline const engine::audio::SoundEventDefinition *AllowedSound(const AudioContent &content, const AudioState &state, std::string_view event)
{
	if (event.empty())
		return nullptr;
	if (const auto volume = VolumeOverride(state, event); volume && (*volume <= 0.0f || *volume < content.settings.minSampleVolume))
		return nullptr;
	return content.Find(event);
}

// Keeps `handle` playing the loop `event` at `position` while `on`.
inline void FollowLoop(engine::audio::SoundPlayer &player, const AudioContent &content, const AudioState &state, engine::audio::SoundHandle &handle,
	std::string_view event, bool on, engine::audio::Vec3 position)
{
	if (!on || event.empty())
	{
		if (handle != 0)
			player.Stop(handle);
		handle = 0;
		return;
	}
	if (handle != 0 && player.Playing(handle))
	{
		player.Move(handle, position);
		return;
	}
	handle = 0;
	if (const auto *sound = AllowedSound(content, state, event); sound != nullptr && sound->Loops())
		handle = PlaySound(player, state, *sound, position);
}

// The four levels the original plays at (AudioManager m_musicVolume, m_speechVolume, m_soundVolume for 2D sounds and
// m_sound3DVolume for world sounds).
struct SoundLevels
{
	float music{1.0f};
	float speech{1.0f};
	float flat{1.0f};
	float positional{1.0f};
};

// OptionPreferences::getSoundVolume / get3DSoundVolume without an Options.ini value (in parseAudioSettingsDefinition,
// the system volumes): the default, the 2D one scaled by 1 + Relative2DVolume when that is below 0 and the 3D one by
// 1 - Relative2DVolume when it is above 0.
inline float PreferredSoundVolume(const AudioSettings &settings)
{
	return settings.relative2DVolume < 0.0f ? settings.defaultSoundVolume * (1.0f + settings.relative2DVolume) : settings.defaultSoundVolume;
}
inline float PreferredSound3DVolume(const AudioSettings &settings)
{
	return settings.relative2DVolume > 0.0f ? settings.default3DSoundVolume * (1.0f - settings.relative2DVolume) : settings.default3DSoundVolume;
}

// AudioManager::setVolume and set3DVolumeAdjustment: the player's volumes (else the settings' preferred ones) times the
// scripts' shares; world sounds times the zoom volume too, clamped to 0..1.
inline SoundLevels LevelsFor(const AudioSettings &settings, const AudioState &state)
{
	const auto level = [](float own, float fallback) { return own >= 0.0f ? own : fallback; };
	SoundLevels levels;
	levels.music = level(state.userMusic, settings.defaultMusicVolume) * state.scriptMusic;
	levels.speech = level(state.userSpeech, settings.defaultSpeechVolume) * state.scriptSpeech;
	levels.flat = level(state.userSound, PreferredSoundVolume(settings)) * state.scriptSound;
	levels.positional = std::clamp(level(state.userSound3D, PreferredSound3DVolume(settings)) * state.scriptSound * state.zoomVolume, 0.0f, 1.0f);
	return levels;
}

// The levels, onto the mixer's buses.
inline void ApplyLevels(engine::audio::Mixer &mixer, const AudioSettings &settings, const AudioState &state)
{
	const SoundLevels levels = LevelsFor(settings, state);
	mixer.SetBusGain(engine::audio::Bus::Music, levels.music);
	mixer.SetBusGain(engine::audio::Bus::Speech, levels.speech);
	mixer.SetBusGain(engine::audio::Bus::Effects, levels.positional);
	mixer.SetBusGain(engine::audio::Bus::Ambient, levels.positional);
	mixer.SetBusGain(engine::audio::Bus::Interface, levels.flat);
}

// AudioSettings onto the player: the sample pools (SampleCount2D / SampleCount3D: MilesAudioManager::initSamplePools),
// the muted-sound cull (MinSampleVolume) and the Global sounds' ranges (GlobalMinRange / GlobalMaxRange).
inline void ConfigurePlayer(engine::audio::SoundPlayer &player, const AudioSettings &settings)
{
	player.SetSampleLimits({settings.sampleCount2D, settings.sampleCount3D});
	player.SetCullSettings({settings.minSampleVolume, settings.globalMinRange, settings.globalMaxRange});
}

// TimeToFadeAudio as output frames: the original fades over that many logic frames (30 a second); presentation runs on
// real time, so the same span of the mixer's output.
inline std::uint32_t FadeMixerFrames(const AudioSettings &settings, std::uint32_t sampleRate)
{
	return static_cast<std::uint32_t>(settings.fadeAudioFrames * sampleRate / 30u);
}

// AudioManager::update's zoom volume: 1 less ZoomSoundVolumePercentageAmount, all of it back as the camera comes
// within ZoomMaxDistance of the microphone (in step with the distance) and full within ZoomMinDistance.
inline float ZoomVolume(const AudioSettings &settings, const ListenerPose &pose, const engine::audio::Vec3 &microphone)
{
	const float amount = settings.zoomVolumeAmount;
	float volume = 1.0f - amount;
	if (amount > 0.0f)
	{
		const float dx = pose.eye[0] - microphone.x, dy = pose.eye[1] - microphone.y, dz = pose.eye[2] - microphone.z;
		const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (distance < settings.zoomMinDistance)
			volume = 1.0f;
		else if (distance < settings.zoomMaxDistance)
			volume = 1.0f - (distance - settings.zoomMinDistance) / (settings.zoomMaxDistance - settings.zoomMinDistance) * amount;
	}
	return volume;
}

// The microphone, as AudioManager::update: from the ground point the camera looks at toward the camera,
// MicrophoneDesiredHeightAboveTerrain up but no further than MicrophoneMaxPercentageBetweenGroundAndCamera of the way
// (that share alone when the camera is no higher than the desired height above the ground point, or not above it at
// all); panned by the camera's right. EA's original compares the camera's absolute height with the desired height
// above the terrain; this is the corrected, relative comparison (a retail quirk fixed).
inline engine::audio::Listener MicrophoneFor(const AudioSettings &settings, const ListenerPose &pose)
{
	const float height = pose.eye[2] - pose.ground[2];
	const float desired = settings.microphoneHeightAboveTerrain;
	float share = settings.microphoneMaxBetweenGroundAndCamera;
	if (!(pose.eye[2] <= desired + pose.ground[2] || height <= 0.0f))
		share = std::min(share, desired / height);
	const engine::audio::Vec3 microphone{pose.ground[0] + (pose.eye[0] - pose.ground[0]) * share,
		pose.ground[1] + (pose.eye[1] - pose.ground[1]) * share, pose.ground[2] + (pose.eye[2] - pose.ground[2]) * share};
	const float length = std::sqrt(pose.right[0] * pose.right[0] + pose.right[1] * pose.right[1]);
	return {microphone, length > 0.0f ? engine::audio::Vec3{pose.right[0] / length, pose.right[1] / length, 0.0f} : engine::audio::Vec3{1, 0, 0}};
}
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::AudioHandle>
{
	static constexpr std::string_view StableName = "generalszh.presentation.audio";
};
template<>
struct ResourceTraits<generalszh::presentation::AudioCommands>
{
	static constexpr std::string_view StableName = "generalszh.presentation.audio_commands";
};
template<>
struct ResourceTraits<generalszh::presentation::AudioState>
{
	static constexpr std::string_view StableName = "generalszh.presentation.audio_state";
};
template<>
struct ResourceTraits<generalszh::presentation::ListenerPose>
{
	static constexpr std::string_view StableName = "generalszh.presentation.listener_pose";
};
}
