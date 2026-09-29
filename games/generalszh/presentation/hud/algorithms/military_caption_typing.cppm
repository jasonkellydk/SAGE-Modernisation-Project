export module games.generalszh.presentation.hud.algorithms.military_caption_typing;
import std;

export import games.generalszh.presentation.hud.resources.military_caption;

// InGameUI::militarySubtitle and the military subtitle's part of InGameUI::update, on logic frames.
export namespace generalszh::presentation
{
// A new caption in place of any (removeMilitarySubtitle first): none for no text or no time. `tick`: the logic frame
// its script ran on; it stays for LOGICFRAMES_PER_SECOND * `milliseconds` / 1000 ticks (whole ticks), its first letter
// after the delay, the block showing.
inline bool ShowCaption(MilitaryCaption &caption, std::u16string text, std::int64_t milliseconds, std::uint64_t tick)
{
	caption.shown = false;
	caption.lines.clear();
	if (text.empty() || milliseconds <= 0)
		return false;
	caption.shown = true;
	caption.text = std::move(text);
	caption.lifetime = tick + static_cast<std::uint64_t>(30 * milliseconds / 1000);
	caption.blockBeginTick = tick;
	caption.blockDrawn = true;
	caption.incrementOnTick = tick + caption.delayTicks;
	caption.index = 0;
	caption.lines.emplace_back();
	caption.color = caption.baseColor;
	return true;
}

// Once its time is up its alpha drops each tick by a tenth of the ticks since (whole numbers, REAL_TO_INT), and at
// below none it goes. Until then the block blinks every ten ticks, and once a letter is due the next one is typed (the
// true return: its typing sound plays), the next due `speedTicks` on; a line break starts a new line (up to MaxLines,
// past them the rest is dropped), the next letter due after the delay; after the last letter nothing more comes.
inline bool StepCaption(MilitaryCaption &caption, std::uint64_t tick)
{
	if (!caption.shown)
		return false;
	if (caption.lifetime < tick)
	{
		const auto amount = static_cast<std::int64_t>((tick - caption.lifetime) / 10);
		std::uint8_t &alpha = caption.color[3];
		if (alpha - amount < 0)
		{
			caption.shown = false;
			caption.lines.clear();
		}
		else
			alpha = static_cast<std::uint8_t>(alpha - amount);
		return false;
	}
	if (caption.blockBeginTick + 9 < tick)
	{
		caption.blockBeginTick = tick;
		caption.blockDrawn = !caption.blockDrawn;
	}
	if (caption.incrementOnTick >= tick)
		return false;
	bool typed = false;
	const char16_t letter = caption.index < caption.text.size() ? caption.text[caption.index] : u'\0';
	if (letter == u'\n')
	{
		if (caption.lines.size() < MilitaryCaption::MaxLines)
		{
			caption.lines.emplace_back();
			caption.blockDrawn = true;
			caption.incrementOnTick = tick + caption.delayTicks;
		}
		else
			caption.index = caption.text.size();
	}
	else
	{
		caption.lines.back().push_back(letter);
		caption.incrementOnTick = tick + caption.speedTicks;
		typed = true;
	}
	++caption.index;
	if (caption.index >= caption.text.size())
		caption.incrementOnTick = caption.lifetime + 1;
	return typed;
}
}
