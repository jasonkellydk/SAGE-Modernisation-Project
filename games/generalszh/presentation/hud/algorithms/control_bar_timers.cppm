export module games.generalszh.presentation.hud.algorithms.control_bar_timers;
import std;

// What the control bar's construction and OCL timer panels show, worked out as the original does it in floats
// (ControlBar::updateConstructionTextDisplay, updateContextOCLTimer / updateOCLTimerTextDisplay, OCLUpdate::
// getCountdownPercent): presentation only, from the simulation's whole ticks and fixed-point percent.
export namespace generalszh::presentation
{
// UnicodeString::format of a pattern's "%d"s, in order, and its "%%"s.
inline std::u16string FormatInts(std::u16string_view pattern, std::initializer_list<std::int64_t> values)
{
	std::u16string out;
	auto next = values.begin();
	for (std::size_t at = 0; at < pattern.size(); ++at)
	{
		if (pattern[at] == u'%' && at + 1 < pattern.size() && pattern[at + 1] == u'%')
		{
			out.push_back(u'%');
			++at;
		}
		else if (pattern[at] == u'%' && at + 1 < pattern.size() && pattern[at + 1] == u'd' && next != values.end())
		{
			for (const char digit : std::to_string(*next++))
				out.push_back(static_cast<char16_t>(digit));
			++at;
		}
		else
			out.push_back(pattern[at]);
	}
	return out;
}

// CONTROLBAR:UnderConstructionDesc ("Building: %.0f%%") with the construction percent (`raw`: 16.16 fixed point) as
// "%.0f" prints it: to the nearest whole, a tie to the even one.
inline std::u16string FormatConstructionPercent(std::u16string_view pattern, std::int64_t raw)
{
	const double percent = static_cast<double>(raw) / 65536.0;
	const auto shown = static_cast<std::int64_t>(std::nearbyint(percent));
	std::u16string out;
	const std::size_t at = pattern.find(u"%.0f");
	const std::u16string_view rest = at == std::u16string_view::npos ? pattern : pattern.substr(at + 4);
	if (at != std::u16string_view::npos)
	{
		out = std::u16string(pattern.substr(0, at));
		for (const char digit : std::to_string(shown))
			out.push_back(static_cast<char16_t>(digit));
	}
	return out + FormatInts(rest, {});
}

// OCLUpdate::getCountdownPercent (1 - remaining / (next - started), in floats) as the bar takes it
// (GadgetProgressBarSetProgress(percent * 100): truncated to an Int).
inline int OclTimerBarProgress(std::uint64_t remaining, std::uint64_t total)
{
	if (total == 0)
		return 0;
	const float percent = 1.0f - (static_cast<float>(static_cast<std::uint32_t>(remaining)) / static_cast<float>(static_cast<std::uint32_t>(total)));
	return static_cast<int>(percent * 100);
}

// updateOCLTimerTextDisplay: the remaining frames as whole seconds (LOGICFRAMES_PER_SECOND 30), minutes and seconds,
// CONTROLBAR:OCLTimerDescWithPadding ("%d:0%d") under ten seconds, else CONTROLBAR:OCLTimerDesc ("%d:%d").
inline std::u16string FormatOclTimer(std::u16string_view pattern, std::u16string_view paddedPattern, std::uint64_t remainingTicks)
{
	const auto totalSeconds = static_cast<std::int64_t>(static_cast<std::uint32_t>(remainingTicks) / 30u);
	const std::int64_t minutes = totalSeconds / 60;
	const std::int64_t seconds = totalSeconds - minutes * 60;
	return FormatInts(seconds < 10 ? paddedPattern : pattern, {minutes, seconds});
}
}
