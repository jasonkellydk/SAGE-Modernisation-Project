export module engine.time.simulation_time;
import std;

export import Engine.Core.Math.Fixed;

export namespace engine::time
{
namespace math = Engine::Math;

// Authoring/setup duration. Runtime deadlines remain integer ticks in SoA
// components; no repeated floating-point countdown subtraction is required.
using Duration = std::chrono::nanoseconds;

class FixedStep
{
public:
	explicit constexpr FixedStep(std::uint32_t ticksPerSecond) : m_rate(ticksPerSecond)
	{
		if (ticksPerSecond == 0)
			throw std::invalid_argument("Simulation step rate must be positive");
	}

	constexpr std::uint32_t TicksPerSecond() const noexcept { return m_rate; }
	// Length of one tick in seconds (1/rate, rounded to fixed point).
	constexpr math::Fixed SecondsPerTick() const noexcept
	{
		return math::Fixed::FromRatio(1, m_rate);
	}

	// Converts a per-second rate (speed, turn rate, regen) to per-tick with a
	// single rounding, which is more exact than multiplying by SecondsPerTick().
	constexpr math::Fixed PerTick(math::Fixed perSecond) const noexcept
	{
		return perSecond / math::Fixed::FromInt(m_rate);
	}

	// Round up: a positive duration never expires early. Decompose before
	// multiplying so 30/60/etc Hz remain exact without 128-bit compiler extensions.
	constexpr std::uint64_t TicksFor(Duration duration) const
	{
		if (duration.count() < 0)
			throw std::invalid_argument("Simulation duration cannot be negative");
		constexpr std::uint64_t nanosPerSecond = 1000000000;
		const auto nanos = static_cast<std::uint64_t>(duration.count());
		const auto wholeSeconds = nanos / nanosPerSecond;
		const auto remainderProduct = (nanos % nanosPerSecond) * m_rate;
		const auto partialTicks = remainderProduct / nanosPerSecond +
			(remainderProduct % nanosPerSecond != 0 ? 1u : 0u);
		constexpr auto maxTick = (std::numeric_limits<std::uint64_t>::max)();
		if (wholeSeconds > (maxTick - partialTicks) / m_rate)
			throw std::overflow_error("Duration exceeds simulation tick range");
		return wholeSeconds * m_rate + partialTicks;
	}

	constexpr bool operator==(const FixedStep &) const noexcept = default;

private:
	std::uint32_t m_rate;
};

// Immutable through its public API. Supplied by the simulation owner, not
// sampled from a process clock. Copies belong to chunk contexts, not entities.
class SimulationTime
{
public:
	constexpr SimulationTime(std::uint64_t tick, FixedStep step) noexcept : m_tick(tick), m_step(step) {}
	constexpr std::uint64_t Tick() const noexcept { return m_tick; }
	constexpr FixedStep Step() const noexcept { return m_step; }
	constexpr math::Fixed SecondsPerTick() const noexcept { return m_step.SecondsPerTick(); }
	constexpr math::Fixed PerTick(math::Fixed perSecond) const noexcept { return m_step.PerTick(perSecond); }

private:
	std::uint64_t m_tick;
	FixedStep m_step;
};
}
