export module games.generalszh.presentation.hud.resources.named_timers;
import std;

import engine.ecs.system.system;

// The script counters and countdowns shown on screen (the original's InGameUI m_namedTimers, DISPLAY_COUNTER /
// DISPLAY_COUNTDOWN_TIMER): by counter name (in name order, as its map), the label shown before the value and whether
// it counts down (m:ss) or up (the number); whether any show (m_showNamedTimers), and the ready countdowns' flash
// (m_namedTimerLastFlashFrame, m_namedTimerUsedFlashColor). Presentation state.
export namespace generalszh::presentation
{
struct NamedTimer
{
	std::string name;
	std::u16string text;
	bool countdown{false};
};

struct NamedTimers
{
	std::vector<NamedTimer> timers; // by name
	bool shown{true};
	std::uint64_t lastFlashTick{0};
	bool usedFlashColor{true};
	std::uint64_t flashTicks{15}; // NamedTimerCountdownFlashDuration as whole ticks (0: no flash)
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::NamedTimers>
{
	static constexpr std::string_view StableName = "generalszh.presentation.named_timers";
};
}
