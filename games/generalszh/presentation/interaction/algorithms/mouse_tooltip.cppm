export module games.generalszh.presentation.interaction.algorithms.mouse_tooltip;
import std;

export import games.generalszh.presentation.interaction.resources.mouse_tooltip;

// The mouse's tooltip and cursor text as the original keeps and draws them (Core/GameEngine/Source/GameClient/Input/
// Mouse.cpp; the in-game mouse-over's text from GeneralsMD/Code/GameEngine/Source/GameClient/InGameUI.cpp
// createMouseoverHint). Free functions over the MouseTooltip resource; times are the platform's milliseconds
// (currentMilliseconds), sizes pixels.
export namespace generalszh::presentation
{
// Mouse's m_tooltipWidth: the constructor's 15 unless Mouse.ini gives TooltipWidth, read by INI::scanPercentToReal (atof
// of the number, a '%' sign apart, / 100, in floats).
inline float TooltipWidthShare(const content::MouseTooltipContent &look) noexcept
{
	if (look.widthText.empty())
		return 15.0f;
	std::string number(look.widthText);
	std::erase(number, '%');
	return static_cast<float>(std::atof(number.c_str())) / 100.0f;
}

// Mouse::setCursorTooltip: the tooltip to show (empty: none this frame), its delay (-1: TooltipDelayTime), the colour
// it is in (RGBColor, 0-1; none: TooltipTextColor and TooltipBackgroundColor) and its width (a share of TooltipWidth of
// the screen's width; a new one rewraps it: under 10 pixels 120, at most the screen).
inline void SetCursorTooltip(MouseTooltip &tooltip, const content::MouseTooltipContent &look, std::u16string_view text, int delay,
	const std::optional<std::array<float, 3>> &color, float width, int displayWidth)
{
	tooltip.empty = text.empty();
	tooltip.delay = delay;
	bool forceRecalc = false;
	if (!text.empty() && width != tooltip.lastWidth)
	{
		forceRecalc = true;
		int pixels = static_cast<int>(static_cast<float>(displayWidth) * TooltipWidthShare(look) * width);
		if (pixels < 10)
			pixels = 120;
		else if (pixels > displayWidth)
			pixels = displayWidth;
		tooltip.wrapWidth = pixels;
		tooltip.lastWidth = width;
	}
	if (forceRecalc || (!tooltip.empty && text != tooltip.text))
		tooltip.text = std::u16string(text);
	if (color)
	{
		const auto &[red, green, blue] = *color;
		if (look.useAltTextColor)
		{
			if (look.adjustAltColor)
			{
				tooltip.textColor[0] = static_cast<std::uint8_t>(static_cast<int>((red + 1.0f) * 255.0f / 2.0f));
				tooltip.textColor[1] = static_cast<std::uint8_t>(static_cast<int>((green + 1.0f) * 255.0f / 2.0f));
				tooltip.textColor[2] = static_cast<std::uint8_t>(static_cast<int>((blue + 1.0f) * 255.0f / 2.0f));
			}
			else
			{
				tooltip.textColor[0] = static_cast<std::uint8_t>(static_cast<int>(red * 255.0f));
				tooltip.textColor[1] = static_cast<std::uint8_t>(static_cast<int>(green * 255.0f));
				tooltip.textColor[2] = static_cast<std::uint8_t>(static_cast<int>(blue * 255.0f));
			}
			tooltip.textColor[3] = look.textColor[3];
		}
		if (look.useAltBackColor)
		{
			if (look.adjustAltColor)
			{
				tooltip.backColor[0] = static_cast<std::uint8_t>(static_cast<int>(red * 255.0f * 0.5f));
				tooltip.backColor[1] = static_cast<std::uint8_t>(static_cast<int>(green * 255.0f * 0.5f));
				tooltip.backColor[2] = static_cast<std::uint8_t>(static_cast<int>(blue * 255.0f * 0.5f));
			}
			else
			{
				tooltip.backColor[0] = static_cast<std::uint8_t>(static_cast<int>(red * 255.0f));
				tooltip.backColor[1] = static_cast<std::uint8_t>(static_cast<int>(green * 255.0f));
				tooltip.backColor[2] = static_cast<std::uint8_t>(static_cast<int>(blue * 255.0f));
			}
			tooltip.backColor[3] = look.backgroundColor[3];
		}
	}
	else
	{
		tooltip.textColor = look.textColor;
		tooltip.backColor = look.backgroundColor;
	}
}

// Mouse::createStreamMessages, before the frame's mouse events: once the pointer has been still for the tooltip's delay
// (its own, else TooltipDelayTime) the tooltip shows, its highlight starting over when it was hidden; else it hides.
inline void UpdateTooltipShowing(MouseTooltip &tooltip, const content::MouseTooltipContent &look, std::uint32_t now)
{
	int delay = look.delayTimeMs;
	if (tooltip.delay >= 0)
		delay = tooltip.delay;
	if (now - tooltip.stillTime >= static_cast<std::uint32_t>(delay))
	{
		if (!tooltip.display)
		{
			tooltip.highlightPos = 0;
			tooltip.highlightStart = now;
		}
		tooltip.display = true;
	}
	else
		tooltip.display = false;
}

// Mouse::createStreamMessages' events: a move (a non-zero delta) restarts the stillness.
inline void PointerMoved(MouseTooltip &tooltip, std::uint32_t now) noexcept
{
	tooltip.stillTime = now;
}

// Mouse::resetTooltipDelay.
inline void ResetTooltipDelay(MouseTooltip &tooltip, std::uint32_t now) noexcept
{
	tooltip.stillTime = now;
	tooltip.display = false;
}

// Mouse::setCursor's text (only on a change of cursor): the cursor's CursorText fetched in its colours, else no text
// (setMouseText(L"", nullptr, nullptr): the colours kept).
inline void SetCursorText(MouseTooltip &tooltip, const MouseTooltipSettings &settings, content::MouseCursorKind cursor)
{
	if (tooltip.cursor == cursor)
		return;
	const auto index = static_cast<std::size_t>(cursor);
	if (index < MouseCursorKinds && !settings.cursorTexts[index].empty())
	{
		tooltip.cursorText = settings.cursorTexts[index];
		tooltip.cursorTextColor = settings.cursorTextColors[index];
		tooltip.cursorTextDropColor = settings.cursorTextDropColors[index];
	}
	else
		tooltip.cursorText.clear();
	tooltip.cursor = cursor;
}

// Mouse::drawCursorText: the cursor text centred on the pointer (its size halved, whole pixels), in its colour with
// its drop colour.
inline std::array<int, 2> CursorTextAt(int pointerX, int pointerY, int textWidth, int textHeight) noexcept
{
	return {pointerX - textWidth / 2, pointerY - textHeight / 2};
}

// Mouse::drawTooltip, for text `textWidth` x `textHeight` as wrapped: its box 20 right of the pointer at its height,
// flipped left of it (by 20 and its width) when it would spill past the screen's right (4 pixels for the spill) and up
// by its height past the bottom; the background as wide as the text (with TooltipAnimateBackground only as far as the
// highlight has run), 2 wider and taller, filled in its back colour and outlined in TooltipBorderColor; the text 2 in
// and 1 down, drawn up to the highlight in its text colour, its last 15 pixels again in TooltipHighlightColor, both
// with TooltipShadowColor drops. Nothing while a script fade runs, nor without text.
struct TooltipDraw
{
	bool shown{false};
	int x{0}, y{0};             // the box's corner
	int boxWidth{0}, boxHeight{0}; // the filled and outlined box (boxWidth + 2, height + 2)
	int textX{0}, textY{0};
	// The text's clip (x from textX to textRight, y from textY to textY + text height) and the highlight's.
	int textRight{0};
	int highlightLeft{0}, highlightRight{0};
	int clipTop{0}, clipBottom{0};
	TooltipColor back{}, border{}, text{}, highlight{}, shadow{};
};

inline constexpr int TooltipHighlightWidth = 15; // HIGHLIGHT_WIDTH

inline TooltipDraw LayOutTooltip(const MouseTooltip &tooltip, const content::MouseTooltipContent &look, int pointerX, int pointerY, int textWidth,
	int textHeight, int maxX, int maxY, bool scriptFade) noexcept
{
	TooltipDraw draw;
	if (scriptFade || !tooltip.display || tooltip.text.empty() || tooltip.empty)
		return draw;
	draw.shown = true;
	int xPos = pointerX + 20;
	int yPos = pointerY;
	if (xPos + textWidth + 4 > maxX)
		xPos -= 20 + textWidth;
	if (yPos + textHeight + 4 > maxY)
		yPos -= textHeight;
	const int boxWidth = look.animateBackground ? std::min(textWidth, tooltip.highlightPos) : textWidth;
	draw.x = xPos;
	draw.y = yPos;
	draw.boxWidth = boxWidth + 2;
	draw.boxHeight = textHeight + 2;
	draw.textX = xPos + 2;
	draw.textY = yPos + 1;
	draw.textRight = xPos + 2 + tooltip.highlightPos;
	draw.highlightLeft = xPos + 2 + tooltip.highlightPos - TooltipHighlightWidth;
	draw.highlightRight = xPos + 2 + tooltip.highlightPos;
	draw.clipTop = yPos + 1;
	draw.clipBottom = yPos + 1 + textHeight;
	draw.back = tooltip.backColor;
	draw.border = look.borderColor;
	draw.text = tooltip.textColor;
	draw.highlight = look.highlightColor;
	draw.shadow = look.shadowColor;
	return draw;
}

// Mouse::drawTooltip's last step, once drawn: until it has passed the text's end by the highlight's width, the
// highlight runs over the text in TooltipFillTime ((width * elapsed) / fill time, in the original's unsigned numbers).
inline void AdvanceTooltipHighlight(MouseTooltip &tooltip, const content::MouseTooltipContent &look, int textWidth, std::uint32_t now) noexcept
{
	if (tooltip.highlightPos >= textWidth + TooltipHighlightWidth || look.fillTimeMs <= 0)
		return; // (a zero fill time would divide by zero in the original)
	tooltip.highlightPos = static_cast<int>(static_cast<std::uint32_t>(textWidth) * (now - tooltip.highlightStart) / static_cast<std::uint32_t>(look.fillTimeMs));
}

// What InGameUI::createMouseoverHint knows of the object under the pointer.
struct MouseoverSubject
{
	std::u16string_view displayName;  // its template's DisplayName as shown (a disguise's: the disguise's)
	std::u16string_view templateName; // GameText's "ThingTemplate:<name>" for its own template
	bool warehouse{false};            // a SupplyWarehouseDockUpdate
	std::int64_t boxes{0};            // its boxes stored
	bool hasPlayer{false};            // a player controls it (apparently)
	std::u16string_view playerName;   // Player::getPlayerDisplayName
	bool playableSide{false};         // Player::isPlayableSide
	bool clear{false};                // its cell CELLSHROUD_CLEAR for the observing (or local) player
	std::uint32_t color{0};           // the colour shown: 0xRRGGBB
};

// A u16 printf of one "%d" (the game's text formats: UnicodeString::format).
inline std::u16string FormatInteger(std::u16string_view pattern, std::int64_t value)
{
	std::u16string out;
	const std::size_t at = pattern.find(u"%d");
	if (at == std::u16string_view::npos)
		return std::u16string(pattern);
	out.append(pattern.substr(0, at));
	for (const char digit : std::to_string(value))
		out.push_back(static_cast<char16_t>(digit));
	out.append(pattern.substr(at + 2));
	return out;
}

// createMouseoverHint's tooltip: the template's name as shown (none: "ThingTemplate:<name>" fetched), a warehouse's
// worth after it (TOOLTIP:SupplyWarehouse with boxes x ValuePerSupplyBox); with a controlling player, in a
// multiplayer game and of a playable side its name on the next line; set (no delay of its own, at the normal width, in
// the object's colour as RGBColor::setFromInt) only where the observing player's view of it is clear and its
// DisplayName is not OBJECT:Prop's. None: no tooltip.
inline std::optional<std::pair<std::u16string, std::array<float, 3>>> MouseoverTooltip(const MouseoverSubject &subject, const MouseoverNames &names)
{
	std::u16string text(subject.displayName.empty() ? subject.templateName : subject.displayName);
	if (subject.warehouse)
		text += FormatInteger(names.warehouseText, subject.boxes * names.valuePerSupplyBox);
	if (!subject.hasPlayer || !subject.clear)
		return std::nullopt;
	if (names.multiplayer && subject.playableSide)
		text = text + u"\n" + std::u16string(subject.playerName);
	if (subject.displayName == names.propText)
		return std::nullopt;
	const std::array<float, 3> rgb{static_cast<float>((subject.color >> 16) & 0xFFu) / 255.0f, static_cast<float>((subject.color >> 8) & 0xFFu) / 255.0f,
		static_cast<float>(subject.color & 0xFFu) / 255.0f};
	return std::pair{std::move(text), rgb};
}
}
