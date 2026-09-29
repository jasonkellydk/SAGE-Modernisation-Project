export module engine.net.lockstep.input_delay;
import std;

// Adaptive input delay: enough ticks of headroom that the slowest player's
// input arrives before its tick is due, and no more (more is sluggish).
// Fed each player's measured round trips; recommends the delay covering the
// 90th percentile of the worst player's recent round trips plus a tick of
// jitter margin, raised at once but lowered only after it has stayed lower
// for a while (no flapping). The delay is not simulation state: changing it
// only moves where future input goes.
export namespace engine::net
{
struct InputDelaySettings
{
	std::uint32_t minimum{1};
	std::uint32_t maximum{12};
	std::chrono::microseconds tick{33'333};
	// Recommendations below the current delay must hold this many samples in a row before it drops.
	std::uint32_t lowerAfter{20};
	std::size_t window{32};
};

class InputDelayPolicy
{
public:
	explicit InputDelayPolicy(InputDelaySettings settings, std::uint32_t initial) : m_settings(settings), m_delay(initial) {}

	// A measured round trip for `seat`; returns the delay to use from now on.
	std::uint32_t Sample(std::uint32_t seat, std::chrono::microseconds roundTrip)
	{
		auto &samples = m_samples[seat];
		samples.push_back(roundTrip);
		while (samples.size() > m_settings.window)
			samples.pop_front();
		const std::uint32_t wanted = Recommended();
		if (wanted > m_delay)
		{
			m_delay = wanted;
			m_lowerStreak = 0;
		}
		else if (wanted < m_delay && ++m_lowerStreak >= m_settings.lowerAfter)
		{
			m_delay = std::max(wanted, m_delay - 1); // one step at a time
			m_lowerStreak = 0;
		}
		else if (wanted == m_delay)
			m_lowerStreak = 0;
		return m_delay;
	}

	void Forget(std::uint32_t seat) { m_samples.erase(seat); }
	std::uint32_t Delay() const noexcept { return m_delay; }

private:
	std::uint32_t Recommended() const
	{
		std::chrono::microseconds worst{0};
		for (const auto &[seat, samples] : m_samples)
		{
			std::vector<std::chrono::microseconds> sorted(samples.begin(), samples.end());
			std::sort(sorted.begin(), sorted.end());
			worst = std::max(worst, sorted[std::min(sorted.size() - 1, sorted.size() * 9 / 10)]);
		}
		// Input travels one way to the relay and the bundle one way back: a round trip.
		const auto ticks = static_cast<std::uint32_t>(worst / m_settings.tick) + 1u + 1u;
		return std::clamp(ticks, m_settings.minimum, m_settings.maximum);
	}

	InputDelaySettings m_settings;
	std::uint32_t m_delay;
	std::uint32_t m_lowerStreak{0};
	std::map<std::uint32_t, std::deque<std::chrono::microseconds>> m_samples;
};
}
