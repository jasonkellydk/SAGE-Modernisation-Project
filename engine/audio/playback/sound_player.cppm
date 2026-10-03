export module engine.audio.playback.sound_player;
import std;

export import engine.audio.mixing.mixer;

// Plays sound events: turns a definition into voices on the mixer and runs
// them over time. It picks the sound (at random or in turn), varies volume
// and pitch, waits out delays, keeps to the event's limit (refusing, or
// interrupting the oldest), keeps to the sample pools (a new sound over a
// full pool stops the lowest-priority, oldest one below it, else does not
// play: the original's killLowestPrioritySoundImmediately), chains attack / body / decay, loops a set number
// of times or forever, and streams single-file sounds (music, speech).
// Presentation only: its randomness never touches the simulation.
export namespace engine::audio
{
using SoundHandle = std::uint64_t;

// Decodes a file ahead into a stream as the mixer drains it.
class StreamFeed
{
public:
	virtual ~StreamFeed() = default;
	virtual std::shared_ptr<PcmStream> Output() = 0;
	// Decodes as much as the stream has room for.
	virtual void Pump() = 0;
};

// Decoded sounds and streams by name; the game resolves names to files.
class SoundLibrary
{
public:
	virtual ~SoundLibrary() = default;
	// A sound of a list ("vgenlo2a"): decoded once and shared; null if missing.
	virtual std::shared_ptr<const PcmBuffer> Sound(std::string_view name) = 0;
	// A file to stream (music on the Music bus, speech otherwise: the game picks the folder by it); null if missing.
	virtual std::shared_ptr<StreamFeed> Stream(std::string_view filename, Bus bus) = 0;
};

// How many flat (2D) and world (3D) sounds may play at once; streams (music, speech) apart. 0: no limit.
struct SampleLimits
{
	std::uint32_t flat{0};
	std::uint32_t positional{0};
};

// The original's culling of quiet sounds and its Global sounds' range (AudioSettings MinSampleVolume, GlobalMinRange,
// GlobalMaxRange). A global max range of 0: Global sounds keep their own minimum range and are heard at any distance.
struct CullSettings
{
	float minSampleVolume{0.0f};
	float globalMinRange{0.0f};
	float globalMaxRange{0.0f};
};

// MilesAudioManager::getEffectiveVolume's distance scale: 1 within the minimum range, min / distance beyond it
// (0 for a minimum range of 0), nothing from the maximum range on.
inline float EffectiveDistanceScale(float distance, float minRange, float maxRange) noexcept
{
	float scale = 1.0f;
	if (distance > minRange)
		scale = minRange > 0.0f ? minRange / distance : 0.0f;
	if (distance >= maxRange)
		scale = 0.0f;
	return scale;
}

class SoundPlayer
{
public:
	void SetSampleLimits(SampleLimits limits) noexcept { m_limits = limits; }
	void SetCullSettings(CullSettings cull) noexcept { m_cull = cull; }
	// Where the microphone is (AudioManager::setListenerPosition): world sounds out of their range are not started and
	// world sounds too quiet there are culled (CullQuiet). Until it is first set nothing is culled for distance.
	void SetListenerPosition(Vec3 position) noexcept
	{
		m_listener = position;
		m_listenerPlaced = true;
	}

	SoundPlayer(Mixer &mixer, SoundLibrary &library, std::uint64_t seed = 0x50D) : m_mixer(mixer), m_library(library), m_random(seed) {}

	// Starts `sound` (at `position` for world sounds; flat without), at
	// `volume` instead of its own when given (a script's override). 0 when it
	// does not play (unknown files, too quiet, over its limit).
	SoundHandle Play(const SoundEventDefinition &sound, std::optional<Vec3> position = std::nullopt, std::optional<float> volume = std::nullopt)
	{
		// AudioManager::addAudioEvent: an event whose volume (a script's override, else its own) is below
		// MinSampleVolume is culled as muted.
		if ((volume ? *volume : sound.volume) < m_cull.minSampleVolume)
			return 0;
		// SoundManager::canPlayNow: a world sound at or beyond its maximum range from the microphone does not start,
		// unless it is Global or Critical.
		if (sound.Positional() && position && m_listenerPlaced && !Unculled(sound) && Distance(*position) >= sound.maxRange)
			return 0;
		auto &playing = m_byName[sound.name];
		std::erase_if(playing, [&](SoundHandle handle) { return !m_instances.contains(handle); });
		if (sound.limit != 0 && playing.size() >= sound.limit)
		{
			if ((sound.control & sound_control::Interrupt) == 0)
				return 0;
			Stop(playing.front(), false);
			playing.erase(playing.begin());
		}
		const bool streamed = !sound.filename.empty();
		const bool positional = sound.Positional() && position.has_value();
		if (!streamed && !MakeRoom(sound, positional))
			return 0;
		Instance instance;
		instance.definition = &sound;
		instance.position = position;
		instance.streamed = streamed;
		instance.positional = positional;
		// AudioEventRTS::generatePlayInfo: the volume shift is GameAudioRandomValueReal(1 + VolumeShift, 1) (VolumeShift is
		// negative, e.g. -10%: a factor between 0.9 and 1); a low end not below 1 gives the high end, 1.
		instance.volume = (volume ? *volume : sound.volume) * VolumeShiftFactor(sound.volumeShift);
		if (instance.volume <= 0.0f || instance.volume < sound.minVolume)
			return 0;
		instance.loopsLeft = sound.loopCount;
		const SoundHandle handle = ++m_nextHandle;
		instance.phase = sound.attack.empty() ? Phase::Body : Phase::Attack;
		if (!StartNext(handle, instance, (sound.control & sound_control::PostDelay) == 0))
			return 0;
		m_instances.emplace(handle, std::move(instance));
		playing.push_back(handle);
		return handle;
	}

	// Stops a sound: loops play their decay if they have one, others fade out.
	void Stop(SoundHandle handle, bool withDecay = true)
	{
		const auto found = m_instances.find(handle);
		if (found == m_instances.end())
			return;
		Instance &instance = found->second;
		if (instance.voice != 0)
			m_mixer.Stop(instance.voice);
		m_voiceOwner.erase(instance.voice);
		if (withDecay && instance.phase != Phase::Decay && !instance.definition->decay.empty())
		{
			instance.phase = Phase::Decay;
			instance.stopping = true;
			if (StartNext(handle, instance, false))
				return;
		}
		m_instances.erase(found);
	}

	// StopAll but for `keep` (sounds whose owner outlives what stops the rest).
	void StopAllExcept(std::span<const SoundHandle> keep)
	{
		std::vector<SoundHandle> stopping;
		for (const auto &[handle, instance] : m_instances)
			if (std::ranges::find(keep, handle) == keep.end())
				stopping.push_back(handle);
		for (const SoundHandle handle : stopping)
			Stop(handle, false);
	}

	void StopAll()
	{
		m_mixer.StopAll();
		m_instances.clear();
		m_voiceOwner.clear();
		m_byName.clear();
		m_feeds.clear();
	}

	void Move(SoundHandle handle, Vec3 position)
	{
		const auto found = m_instances.find(handle);
		if (found == m_instances.end())
			return;
		found->second.position = position;
		if (found->second.voice != 0)
			m_mixer.SetPosition(found->second.voice, position);
	}

	// Stops a sound now, fading it out linearly over `mixerFrames` of output (MilesAudioManager::processFadingList: the
	// music faded over TimeToFadeAudio); it no longer counts as playing.
	void FadeOut(SoundHandle handle, std::uint32_t mixerFrames)
	{
		const auto found = m_instances.find(handle);
		if (found == m_instances.end())
			return;
		if (found->second.voice != 0)
		{
			m_mixer.Stop(found->second.voice, std::max<std::uint32_t>(mixerFrames, 1));
			m_voiceOwner.erase(found->second.voice);
		}
		m_instances.erase(found);
	}

	// MilesAudioManager::processPlayingList, each frame: a world sound playing at a position whose effective volume at
	// the microphone (its volume and shift, times `positionalLevel`, times EffectiveDistanceScale) over the flat
	// sounds' level (`flatLevel`; the original divides by the 2D level whenever the 3D one is above 0) is below
	// MinSampleVolume is stopped at once, unless it is Global or Critical. Nothing before the listener is placed.
	void CullQuiet(float positionalLevel, float flatLevel)
	{
		if (!m_listenerPlaced)
			return;
		std::vector<SoundHandle> quiet;
		for (const auto &[handle, instance] : m_instances)
		{
			if (!instance.positional || !instance.position || Unculled(*instance.definition))
				continue;
			const SoundEventDefinition &sound = *instance.definition;
			float volume = instance.volume * positionalLevel * EffectiveDistanceScale(Distance(*instance.position), sound.minRange, sound.maxRange);
			const float divisor = positionalLevel > 0.0f ? flatLevel : 1.0f;
			if (divisor <= 0.0f)
				continue; // x / 0: never below
			volume /= divisor;
			if (volume < m_cull.minSampleVolume)
				quiet.push_back(handle);
		}
		std::ranges::sort(quiet);
		for (const SoundHandle handle : quiet)
			Stop(handle, false);
	}

	// Every playing instance of the event `name` at `volume` from now.
	void SetVolume(std::string_view name, float volume)
	{
		const auto found = m_byName.find(std::string(name));
		if (found == m_byName.end())
			return;
		for (const SoundHandle handle : found->second)
			if (const auto instance = m_instances.find(handle); instance != m_instances.end())
			{
				instance->second.volume = volume;
				if (instance->second.voice != 0)
					m_mixer.SetGain(instance->second.voice, volume);
			}
	}

	// Every playing instance of the event `name` stopped at once (AudioManager::removeAudioEvent by name).
	void StopEvent(std::string_view name)
	{
		const auto found = m_byName.find(std::string(name));
		if (found == m_byName.end())
			return;
		const std::vector<SoundHandle> handles = found->second;
		for (const SoundHandle handle : handles)
			Stop(handle, false);
	}

	// Every instance playing at no volume stopped at once (removeAllDisabledAudio: the sounds a script silenced).
	void StopMuted()
	{
		std::vector<SoundHandle> muted;
		for (const auto &[handle, instance] : m_instances)
			if (instance.volume <= 0.0f)
				muted.push_back(handle);
		std::ranges::sort(muted);
		for (const SoundHandle handle : muted)
			Stop(handle, false);
	}

	// Every flat (2D) sound playing now held where it is, or let go on (MilesAudioManager::pauseAudio / resumeAudio with
	// AudioAffect_Sound: AIL_stop_sample / AIL_resume_sample on each playing 2D sample; world sounds and streams go on,
	// and sounds started later play as usual).
	void PauseFlat(bool paused)
	{
		for (const auto &[handle, instance] : m_instances)
			if (!instance.streamed && !instance.positional && instance.voice != 0)
				m_mixer.Pause(instance.voice, paused);
	}
	// Scope suspension to this owner's voices. Scene and menu owners can
	// share a mixer without pausing each other's sounds or losing cursors.
	void PauseAll(bool paused) {
		m_paused=paused;
		for(const auto& [handle,instance]:m_instances) if(instance.voice) m_mixer.Pause(instance.voice,paused);
	}

	bool Playing(SoundHandle handle) const { return m_instances.contains(handle); }
	std::size_t PlayingCount() const noexcept { return m_instances.size(); }

	// Once per frame: keeps streams fed and sequences the sounds whose voices ended.
	void Update()
	{
		std::erase_if(m_feeds, [](const std::shared_ptr<StreamFeed> &feed) { return feed.use_count() == 1 && feed->Output()->Ended(); });
		for (const auto &feed : m_feeds)
			feed->Pump();
		for (const VoiceId voice : m_mixer.TakeFinished())
		{
			const auto owner = m_voiceOwner.find(voice);
			if (owner == m_voiceOwner.end())
				continue;
			const SoundHandle handle = owner->second;
			m_voiceOwner.erase(owner);
			const auto found = m_instances.find(handle);
			if (found == m_instances.end() || found->second.voice != voice)
				continue;
			found->second.voice = 0;
			if (!Advance(handle, found->second))
				m_instances.erase(found);
		}
	}

private:
	enum class Phase : std::uint8_t
	{
		Attack,
		Body,
		Decay,
	};

	struct Instance
	{
		const SoundEventDefinition *definition{nullptr};
		std::optional<Vec3> position;
		float volume{1.0f};
		Phase phase{Phase::Body};
		std::uint32_t loopsLeft{0};
		std::size_t allIndex{0};
		VoiceId voice{0};
		bool stopping{false};
		bool streamed{false};
		bool positional{false};
	};

	// A sample for a new sound of `sound`'s priority in its pool: free, or freed by stopping the
	// lowest-priority sound below it (the oldest of those); false when none is below it.
	bool MakeRoom(const SoundEventDefinition &sound, bool positional)
	{
		const std::uint32_t cap = positional ? m_limits.positional : m_limits.flat;
		if (cap == 0)
			return true;
		std::uint32_t used = 0;
		SoundHandle victim = 0;
		SoundPriority lowest = sound.priority;
		for (const auto &[handle, instance] : m_instances)
		{
			if (instance.streamed || instance.positional != positional)
				continue;
			++used;
			const SoundPriority priority = instance.definition->priority;
			if (priority >= sound.priority)
				continue;
			if (victim == 0 || priority < lowest || (priority == lowest && handle < victim))
			{
				lowest = priority;
				victim = handle;
			}
		}
		if (used < cap)
			return true;
		if (victim == 0)
			return false;
		Stop(victim, false);
		return true;
	}

	// Global and Critical sounds are never culled for distance or quietness.
	static bool Unculled(const SoundEventDefinition &sound) noexcept
	{
		return (sound.type & sound_type::Global) != 0 || sound.priority == SoundPriority::Critical;
	}

	float Distance(Vec3 position) const noexcept
	{
		const float dx = m_listener.x - position.x, dy = m_listener.y - position.y, dz = m_listener.z - position.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}

	float VolumeShiftFactor(float volumeShift)
	{
		const float low = 1.0f + volumeShift;
		return low >= 1.0f ? 1.0f : Uniform(low, 1.0f);
	}

	float Uniform(float low, float high)
	{
		if (high <= low)
			return low;
		return std::uniform_real_distribution<float>(low, high)(m_random);
	}

	const std::vector<std::string> &ListFor(const Instance &instance) const
	{
		const SoundEventDefinition &sound = *instance.definition;
		switch (instance.phase)
		{
		case Phase::Attack: return sound.attack;
		case Phase::Decay: return sound.decay;
		case Phase::Body: return sound.sounds;
		}
		return sound.sounds;
	}

	// The sound of the list to play now.
	std::string_view Pick(const Instance &instance)
	{
		const SoundEventDefinition &sound = *instance.definition;
		const std::vector<std::string> &list = ListFor(instance);
		if (list.empty())
			return {};
		if (instance.phase == Phase::Body && (sound.control & sound_control::All) != 0)
			return list[std::min(instance.allIndex, list.size() - 1)];
		if ((sound.control & sound_control::Random) != 0 || instance.phase != Phase::Body)
			return list[std::uniform_int_distribution<std::size_t>(0, list.size() - 1)(m_random)];
		// In turn, across every play of the event.
		std::size_t &turn = m_turns[sound.name];
		return list[turn++ % list.size()];
	}

	// Starts the instance's next voice; false if there is nothing to play.
	bool StartNext(SoundHandle handle, Instance &instance, bool withDelay)
	{
		const SoundEventDefinition &sound = *instance.definition;
		VoiceStart start;
		start.bus = sound.bus;
		start.gain = instance.volume;
		start.pitch = Uniform(sound.pitchMin, sound.pitchMax);
		start.positional = sound.Positional() && instance.position.has_value();
		if (start.positional)
			start.position = *instance.position;
		start.minRange = sound.minRange;
		start.maxRange = sound.maxRange;
		if ((sound.type & sound_type::Global) != 0)
		{
			// MilesAudioManager::playSample3D: Global sounds take AudioSettings' global ranges.
			if (m_cull.globalMaxRange > 0.0f)
			{
				start.minRange = m_cull.globalMinRange;
				start.maxRange = m_cull.globalMaxRange;
			}
			else
				start.maxRange = 1.0e9f;
		}
		if (withDelay && sound.delayMaxMs > 0)
			start.delayFrames = static_cast<std::uint32_t>(Uniform(static_cast<float>(sound.delayMinMs), static_cast<float>(sound.delayMaxMs)) *
				static_cast<float>(m_mixer.SampleRate()) / 1000.0f);
		if (!sound.filename.empty() && instance.phase == Phase::Body)
		{
			std::shared_ptr<StreamFeed> feed = m_library.Stream(sound.filename, sound.bus);
			if (feed == nullptr)
				return false;
			feed->Pump();
			start.stream = feed->Output();
			m_feeds.push_back(std::move(feed));
			start.pitch = 1.0f;
		}
		else
		{
			const std::string_view name = Pick(instance);
			start.buffer = name.empty() ? nullptr : m_library.Sound(name);
			if (start.buffer == nullptr)
				return false;
			// A single sound looping forever loops in the mixer, without gaps.
			start.loop = instance.phase == Phase::Body && sound.Loops() && sound.loopCount == 0 && sound.sounds.size() == 1 &&
				sound.delayMaxMs == 0 && (sound.control & sound_control::All) == 0;
		}
		instance.voice = m_mixer.Play(std::move(start));
		if(m_paused) m_mixer.Pause(instance.voice,true);
		m_voiceOwner[instance.voice] = handle;
		return true;
	}

	// A voice of the instance ended: what comes next; false when it is done.
	bool Advance(SoundHandle handle, Instance &instance)
	{
		const SoundEventDefinition &sound = *instance.definition;
		const bool postDelay = (sound.control & sound_control::PostDelay) != 0;
		switch (instance.phase)
		{
		case Phase::Attack:
			instance.phase = Phase::Body;
			return StartNext(handle, instance, false);
		case Phase::Decay:
			return false;
		case Phase::Body:
			break;
		}
		if (instance.stopping)
			return false;
		if ((sound.control & sound_control::All) != 0 && ++instance.allIndex < sound.sounds.size())
			return StartNext(handle, instance, postDelay);
		if (!sound.Loops())
			return false;
		if (sound.loopCount != 0 && --instance.loopsLeft == 0)
		{
			if (sound.decay.empty())
				return false;
			instance.phase = Phase::Decay;
			return StartNext(handle, instance, false);
		}
		instance.allIndex = 0;
		return StartNext(handle, instance, true);
	}

	Mixer &m_mixer;
	SoundLibrary &m_library;
	SampleLimits m_limits;
	CullSettings m_cull;
	Vec3 m_listener;
	bool m_listenerPlaced{false};
	bool m_paused{};
	std::mt19937_64 m_random;
	SoundHandle m_nextHandle{0};
	std::unordered_map<SoundHandle, Instance> m_instances;
	std::unordered_map<VoiceId, SoundHandle> m_voiceOwner;
	std::map<std::string, std::vector<SoundHandle>, std::less<>> m_byName;
	std::map<std::string, std::size_t, std::less<>> m_turns;
	std::vector<std::shared_ptr<StreamFeed>> m_feeds;
};
}
