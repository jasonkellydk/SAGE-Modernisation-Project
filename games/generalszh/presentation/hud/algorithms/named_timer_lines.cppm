export module games.generalszh.presentation.hud.algorithms.named_timer_lines;
import std;

export import games.generalszh.presentation.hud.resources.named_timers;

// InGameUI::addNamedTimer / removeNamedTimer and the named timers' part of InGameUI::postDraw.
export namespace generalszh::presentation
{
// addNamedTimer: in place of one of that name, kept in name order.
inline void AddNamedTimer(NamedTimers &timers, std::string name, std::u16string text, bool countdown)
{
	std::erase_if(timers.timers, [&](const NamedTimer &timer) { return timer.name == name; });
	const auto at = std::lower_bound(timers.timers.begin(), timers.timers.end(), name,
		[](const NamedTimer &timer, const std::string &key) { return timer.name < key; });
	timers.timers.insert(at, NamedTimer{std::move(name), std::move(text), countdown});
}

inline void RemoveNamedTimer(NamedTimers &timers, std::string_view name)
{
	std::erase_if(timers.timers, [&](const NamedTimer &timer) { return timer.name == name; });
}

struct NamedTimerLine
{
	std::u16string text;
	bool ready{false};      // a countdown at 0:00: the ready font (and colour)
	bool flashColor{false}; // ready and flashing: in the flash colour this time
};

namespace named_timer_detail
{
inline std::u16string Number(std::int64_t value)
{
	const std::string text = std::to_string(value);
	return std::u16string(text.begin(), text.end());
}
// SECONDS_PER_LOGICFRAME_REAL * frames left (none below one), whole seconds.
inline std::int64_t ReadySeconds(std::int64_t framesLeft) { return framesLeft > 0 ? framesLeft / 30 : 0; }
}

// A ready countdown shown flips the flash colour each NamedTimerCountdownFlashDuration ticks (from the first logic frame).
inline void StepNamedTimerFlash(NamedTimers &timers, const std::function<std::int64_t(std::string_view)> &counter, std::uint64_t tick)
{
	if (tick == 0 || !timers.shown || timers.flashTicks == 0)
		return;
	bool anyReady = false;
	for (const NamedTimer &timer : timers.timers)
		anyReady = anyReady || (timer.countdown && named_timer_detail::ReadySeconds(counter(timer.name)) == 0);
	if (anyReady && tick >= timers.lastFlashTick + timers.flashTicks)
	{
		timers.usedFlashColor = !timers.usedFlashColor;
		timers.lastFlashTick = tick;
	}
}

// What shows (none before the first logic frame, or while hidden): each timer's label and its counter's value: a
// counter's as a number, a countdown's as m:ss of the frames left (m:0s under ten seconds).
inline std::vector<NamedTimerLine> NamedTimerLines(const NamedTimers &timers, const std::function<std::int64_t(std::string_view)> &counter, std::uint64_t tick)
{
	using namespace named_timer_detail;
	std::vector<NamedTimerLine> lines;
	if (tick == 0 || !timers.shown)
		return lines;
	for (const NamedTimer &timer : timers.timers)
	{
		const std::int64_t framesLeft = counter(timer.name);
		const std::int64_t seconds = ReadySeconds(framesLeft);
		NamedTimerLine line;
		if (!timer.countdown)
			line.text = timer.text + u" " + Number(framesLeft);
		else
		{
			const std::int64_t minutes = seconds / 60, rest = seconds - minutes * 60;
			line.text = timer.text + u" " + Number(minutes) + (rest >= 10 ? u":" : u":0") + Number(rest);
			line.ready = seconds == 0;
			line.flashColor = line.ready && timers.flashTicks != 0 && !timers.usedFlashColor;
		}
		lines.push_back(std::move(line));
	}
	return lines;
}
}
