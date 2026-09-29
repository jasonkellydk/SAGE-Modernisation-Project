export module engine.audio.mixing.pcm;
import std;

// Sample data the mixer plays: decoded sounds (whole, shared by every voice
// playing them) and streams (music, speech) filled ahead by a decoder while
// the device thread drains them. Everything is interleaved stereo float at
// the mixer's rate.
export namespace engine::audio
{
struct PcmBuffer
{
	std::uint32_t sampleRate{0};
	std::vector<float> samples; // interleaved stereo

	std::size_t Frames() const noexcept { return samples.size() / 2; }
};

// One producer (the decoder feeding it), one consumer (the mixer).
class PcmStream
{
public:
	explicit PcmStream(std::size_t capacityFrames) : m_samples((capacityFrames + 1) * 2) {}

	// Producer: frames there is room for now.
	std::size_t Space() const noexcept
	{
		const std::size_t read = m_read.load(std::memory_order_acquire);
		const std::size_t write = m_write.load(std::memory_order_relaxed);
		const std::size_t capacity = m_samples.size() / 2;
		return (read + capacity - write - 1) % capacity;
	}

	// Producer: appends up to Space() frames; returns the frames taken.
	std::size_t Write(std::span<const float> interleaved)
	{
		const std::size_t capacity = m_samples.size() / 2;
		const std::size_t frames = std::min(interleaved.size() / 2, Space());
		std::size_t write = m_write.load(std::memory_order_relaxed);
		for (std::size_t frame = 0; frame < frames; ++frame)
		{
			m_samples[write * 2] = interleaved[frame * 2];
			m_samples[write * 2 + 1] = interleaved[frame * 2 + 1];
			write = (write + 1) % capacity;
		}
		m_write.store(write, std::memory_order_release);
		return frames;
	}

	// Producer: nothing more will be written.
	void Finish() noexcept { m_finished.store(true, std::memory_order_release); }

	// Consumer: frames ready.
	std::size_t Available() const noexcept
	{
		const std::size_t write = m_write.load(std::memory_order_acquire);
		const std::size_t read = m_read.load(std::memory_order_relaxed);
		const std::size_t capacity = m_samples.size() / 2;
		return (write + capacity - read) % capacity;
	}

	// Consumer: one frame (call only when Available() > 0).
	void Pop(float &left, float &right) noexcept
	{
		const std::size_t capacity = m_samples.size() / 2;
		const std::size_t read = m_read.load(std::memory_order_relaxed);
		left = m_samples[read * 2];
		right = m_samples[read * 2 + 1];
		m_read.store((read + 1) % capacity, std::memory_order_release);
	}

	// Consumer: drained for good.
	bool Ended() const noexcept { return m_finished.load(std::memory_order_acquire) && Available() == 0; }

private:
	std::vector<float> m_samples;
	std::atomic<std::size_t> m_read{0};
	std::atomic<std::size_t> m_write{0};
	std::atomic<bool> m_finished{false};
};
}
