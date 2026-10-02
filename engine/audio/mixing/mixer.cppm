export module engine.audio.mixing.mixer;
import std;

export import engine.audio.mixing.pcm;
export import engine.audio.mixing.spatial;
export import engine.audio.definitions.sound_event;

// The software mixer. The game side (any thread) sends it commands, which
// take effect at the start of the next Mix; the device's audio thread calls
// Mix for each block it needs. Voices play a decoded buffer (resampled for
// pitch) or drain a stream, at a gain, on a bus, either flat (2D) or placed
// in the world and spatialized for the listener every block. Finished
// voices are reported back so the player can sequence and release them.
export namespace engine::audio
{
using VoiceId = std::uint64_t;

struct OutputStatistics
{
	std::uint64_t stereoFrames{};
	std::uint64_t nonSilentFrames{};
};

struct VoiceStart
{
	std::shared_ptr<const PcmBuffer> buffer; // or
	std::shared_ptr<PcmStream> stream;
	Bus bus{Bus::Effects};
	float gain{1.0f};
	float pitch{1.0f};
	bool loop{false};
	bool positional{false};
	Vec3 position;
	float minRange{0.0f};
	float maxRange{1000.0f};
	// Silence before it starts.
	std::uint32_t delayFrames{0};
	// Fades in over this many frames (0: starts at full gain).
	std::uint32_t fadeInFrames{0};
};

class Mixer
{
public:
	explicit Mixer(std::uint32_t sampleRate) : m_sampleRate(sampleRate) { m_busGain.fill(1.0f); }

	std::uint32_t SampleRate() const noexcept { return m_sampleRate; }
	// Cumulative output delivered to the device adapter, after bus and master
	// gains. Safe to read from the game thread; an in-flight block may appear
	// in one counter before the other. This does not measure the loudspeaker.
	OutputStatistics OutputStats() const noexcept
	{
		return {m_outputFrames.load(std::memory_order_relaxed),m_nonSilentFrames.load(std::memory_order_relaxed)};
	}

	// Game side ----------------------------------------------------------------

	VoiceId Play(VoiceStart start)
	{
		const std::lock_guard lock(m_commandMutex);
		const VoiceId id = ++m_nextId;
		m_commands.emplace_back(StartCommand{id, std::move(start)});
		return id;
	}

	// Stops a voice (fading out over `fadeFrames` so it does not click).
	void Stop(VoiceId id, std::uint32_t fadeFrames = 64) { Send(StopCommand{id, fadeFrames}); }
	void SetPosition(VoiceId id, Vec3 position) { Send(PositionCommand{id, position}); }
	void SetGain(VoiceId id, float gain) { Send(GainCommand{id, gain}); }
	// Change looping at the next mix block without rewinding the voice. Turning
	// it off lets the current playback cycle reach its end.
	void SetLooping(VoiceId id, bool loop) { Send(LoopCommand{id, loop}); }
	void SetBusGain(Bus bus, float gain) { Send(BusCommand{bus, gain}); }
	void SetMasterGain(float gain) { Send(MasterCommand{gain}); }
	void SetListener(const Listener &listener) { Send(listener); }
	void StopAll() { Send(StopAllCommand{}); }
	// Holds a voice where it is (silent, not advancing, not finishing) until resumed (Miles AIL_stop_sample /
	// AIL_resume_sample).
	void Pause(VoiceId id, bool paused) { Send(PauseCommand{id, paused}); }

	// Voices that ended since the last call (played out, stopped or dropped).
	std::vector<VoiceId> TakeFinished()
	{
		const std::lock_guard lock(m_finishedMutex);
		return std::exchange(m_finished, {});
	}

	// Audio thread ---------------------------------------------------------------

	// Mixes the next block into interleaved stereo `out` (overwritten).
	void Mix(std::span<float> out)
	{
		ApplyCommands();
		std::fill(out.begin(), out.end(), 0.0f);
		const std::size_t frames = out.size() / 2;
		std::vector<VoiceId> ended;
		for (auto &[id, voice] : m_voices)
			if (!MixVoice(voice, out, frames))
				ended.push_back(id);
		for (const VoiceId id : ended)
			m_voices.erase(id);
		for (float &sample : out)
			sample = SoftClip(sample * m_master);
		std::uint64_t nonSilent{};
		for (std::size_t frame = 0; frame < frames; ++frame)
			if (out[frame * 2] != 0.0f || out[frame * 2 + 1] != 0.0f)
				++nonSilent;
		m_nonSilentFrames.fetch_add(nonSilent,std::memory_order_relaxed);
		m_outputFrames.fetch_add(frames,std::memory_order_relaxed);
		if (!ended.empty())
		{
			const std::lock_guard lock(m_finishedMutex);
			m_finished.insert(m_finished.end(), ended.begin(), ended.end());
		}
	}

	std::size_t ActiveVoices() const noexcept { return m_voices.size(); }

private:
	struct StartCommand
	{
		VoiceId id;
		VoiceStart start;
	};
	struct StopCommand
	{
		VoiceId id;
		std::uint32_t fadeFrames;
	};
	struct PositionCommand
	{
		VoiceId id;
		Vec3 position;
	};
	struct GainCommand
	{
		VoiceId id;
		float gain;
	};
	struct BusCommand
	{
		Bus bus;
		float gain;
	};
	struct MasterCommand
	{
		float gain;
	};
	struct StopAllCommand
	{
	};
	struct PauseCommand
	{
		VoiceId id;
		bool paused;
	};
	struct LoopCommand
	{
		VoiceId id;
		bool loop;
	};
	using Command =
		std::variant<StartCommand, StopCommand, PositionCommand, GainCommand, BusCommand, MasterCommand, Listener, StopAllCommand, PauseCommand, LoopCommand>;

	struct Voice
	{
		VoiceStart start;
		double cursor{0.0};
		std::uint32_t delay{0};
		// Envelope: current gain factor and its per-frame change.
		float envelope{1.0f};
		float envelopeStep{0.0f};
		bool stopping{false};
		bool paused{false};
		StereoGain lastGain{};
	};

	template<typename C>
	void Send(C command)
	{
		const std::lock_guard lock(m_commandMutex);
		m_commands.emplace_back(std::move(command));
	}

	void ApplyCommands()
	{
		std::vector<Command> commands;
		{
			const std::lock_guard lock(m_commandMutex);
			commands.swap(m_commands);
		}
		for (Command &command : commands)
			std::visit([&](auto &c) { Apply(c); }, command);
	}

	void Apply(StartCommand &command)
	{
		Voice voice{std::move(command.start)};
		voice.delay = voice.start.delayFrames;
		if (voice.start.fadeInFrames > 0)
		{
			voice.envelope = 0.0f;
			voice.envelopeStep = 1.0f / static_cast<float>(voice.start.fadeInFrames);
		}
		voice.lastGain = TargetGain(voice);
		m_voices.emplace(command.id, std::move(voice));
	}
	void Apply(const StopCommand &command)
	{
		const auto found = m_voices.find(command.id);
		if (found == m_voices.end())
			return;
		Voice &voice = found->second;
		voice.stopping = true;
		voice.envelopeStep = -voice.envelope / static_cast<float>(std::max<std::uint32_t>(command.fadeFrames, 1));
	}
	void Apply(const PositionCommand &command)
	{
		if (const auto found = m_voices.find(command.id); found != m_voices.end())
			found->second.start.position = command.position;
	}
	void Apply(const GainCommand &command)
	{
		if (const auto found = m_voices.find(command.id); found != m_voices.end())
			found->second.start.gain = command.gain;
	}
	void Apply(const LoopCommand &command)
	{
		if (const auto found = m_voices.find(command.id); found != m_voices.end())
			found->second.start.loop = command.loop;
	}
	void Apply(const BusCommand &command) { m_busGain[static_cast<std::size_t>(command.bus)] = command.gain; }
	void Apply(const MasterCommand &command) { m_master = command.gain; }
	void Apply(const Listener &listener) { m_listener = listener; }
	void Apply(const StopAllCommand &)
	{
		for (auto &[id, voice] : m_voices)
			Apply(StopCommand{id, 256});
	}
	void Apply(const PauseCommand &command)
	{
		if (const auto found = m_voices.find(command.id); found != m_voices.end())
			found->second.paused = command.paused;
	}

	StereoGain TargetGain(const Voice &voice) const
	{
		const float gain = voice.start.gain * m_busGain[static_cast<std::size_t>(voice.start.bus)];
		if (!voice.start.positional)
			return {gain, gain};
		const StereoGain placed = Spatialize(m_listener, voice.start.position, voice.start.minRange, voice.start.maxRange);
		return {placed.left * gain, placed.right * gain};
	}

	// Adds the voice's next `frames` into `out`; false once it has ended.
	bool MixVoice(Voice &voice, std::span<float> out, std::size_t frames)
	{
		// Held: silent and where it was (a stopped one still fades out and goes).
		if (voice.paused && !voice.stopping)
			return true;
		// Gains glide across the block so moving sounds and volume changes do not click.
		const StereoGain target = TargetGain(voice);
		const float step = 1.0f / static_cast<float>(std::max<std::size_t>(frames, 1));
		const float dl = (target.left - voice.lastGain.left) * step;
		const float dr = (target.right - voice.lastGain.right) * step;
		float left = voice.lastGain.left;
		float right = voice.lastGain.right;
		voice.lastGain = target;

		for (std::size_t frame = 0; frame < frames; ++frame)
		{
			left += dl;
			right += dr;
			if (voice.delay > 0)
			{
				--voice.delay;
				continue;
			}
			float l = 0.0f, r = 0.0f;
			if (!Next(voice, l, r))
				return false;
			voice.envelope = std::clamp(voice.envelope + voice.envelopeStep, 0.0f, 1.0f);
			if (voice.envelopeStep > 0.0f && voice.envelope >= 1.0f)
				voice.envelopeStep = 0.0f;
			if (voice.stopping && voice.envelope <= 0.0f)
				return false;
			out[frame * 2] += l * left * voice.envelope;
			out[frame * 2 + 1] += r * right * voice.envelope;
		}
		if (voice.start.stream)
			return !voice.start.stream->Ended();
		return true;
	}

	// The voice's next frame; false at its end.
	static bool Next(Voice &voice, float &left, float &right)
	{
		if (voice.start.stream)
		{
			if (voice.start.stream->Available() == 0)
				return !voice.start.stream->Ended(); // starved: silence until the decoder catches up
			voice.start.stream->Pop(left, right);
			return true;
		}
		const PcmBuffer &buffer = *voice.start.buffer;
		const std::size_t length = buffer.Frames();
		if (length == 0)
			return false;
		if (voice.cursor >= static_cast<double>(length))
		{
			if (!voice.start.loop)
				return false;
			voice.cursor = std::fmod(voice.cursor, static_cast<double>(length));
		}
		const auto index = static_cast<std::size_t>(voice.cursor);
		const float fraction = static_cast<float>(voice.cursor - static_cast<double>(index));
		const std::size_t following = index + 1 < length ? index + 1 : (voice.start.loop ? 0 : index);
		left = buffer.samples[index * 2] + (buffer.samples[following * 2] - buffer.samples[index * 2]) * fraction;
		right = buffer.samples[index * 2 + 1] + (buffer.samples[following * 2 + 1] - buffer.samples[index * 2 + 1]) * fraction;
		voice.cursor += voice.start.pitch;
		return true;
	}

	// Keeps many loud voices from wrapping: linear to 0.75, then a smooth knee to 1.
	static float SoftClip(float sample) noexcept
	{
		const float magnitude = std::fabs(sample);
		if (magnitude <= 0.75f)
			return sample;
		const float over = magnitude - 0.75f;
		const float shaped = 0.75f + 0.25f * std::tanh(over / 0.25f);
		return sample < 0.0f ? -shaped : shaped;
	}

	std::uint32_t m_sampleRate;
	std::mutex m_commandMutex;
	std::vector<Command> m_commands;
	VoiceId m_nextId{0};
	std::mutex m_finishedMutex;
	std::vector<VoiceId> m_finished;
	std::atomic<std::uint64_t> m_outputFrames{};
	std::atomic<std::uint64_t> m_nonSilentFrames{};

	// Audio thread only.
	std::unordered_map<VoiceId, Voice> m_voices;
	std::array<float, static_cast<std::size_t>(Bus::Count)> m_busGain{};
	float m_master{1.0f};
	Listener m_listener;
};
}
