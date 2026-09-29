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

// The levels: the settings' defaults times the scripts' shares.
inline void ApplyLevels(engine::audio::Mixer &mixer, const AudioSettings &settings, const AudioState &state)
{
	const auto level = [](float own, float fallback) { return own >= 0.0f ? own : fallback; };
	mixer.SetBusGain(engine::audio::Bus::Music, level(state.userMusic, settings.defaultMusicVolume) * state.scriptMusic);
	mixer.SetBusGain(engine::audio::Bus::Speech, level(state.userSpeech, settings.defaultSpeechVolume) * state.scriptSpeech);
	mixer.SetBusGain(engine::audio::Bus::Effects, level(state.userSound3D, settings.default3DSoundVolume) * state.scriptSound * state.zoomVolume);
	mixer.SetBusGain(engine::audio::Bus::Ambient, level(state.userSound3D, settings.default3DSoundVolume) * state.scriptSound * state.zoomVolume);
	mixer.SetBusGain(engine::audio::Bus::Interface, level(state.userSound, settings.defaultSoundVolume) * state.scriptSound);
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

// The microphone, as the original: from the ground point the camera looks at
// toward the camera, the configured height up but no further than the
// configured share of the way; panned by the camera's right.
inline engine::audio::Listener MicrophoneFor(const AudioSettings &settings, const ListenerPose &pose)
{
	const float height = pose.eye[2] - pose.ground[2];
	float share = settings.microphoneMaxBetweenGroundAndCamera;
	if (height > 0.0f && settings.microphoneHeightAboveTerrain > 0.0f)
		share = std::min(share, settings.microphoneHeightAboveTerrain / height);
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
