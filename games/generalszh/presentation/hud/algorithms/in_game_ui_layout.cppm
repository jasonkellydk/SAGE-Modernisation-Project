export module games.generalszh.presentation.hud.algorithms.in_game_ui_layout;
import std;

export import games.generalszh.content.global.in_game_ui;

// Where and how InGameUI places its own texts and marks, from InGameUI.ini: the military caption's origin, the named
// timers' start, the floating texts' motion, the right-button scroll anchor, and the selection cap.
export namespace generalszh::presentation
{
// InGameUI::militarySubtitle: MilitaryCaptionPosition is on an 800 x 600 screen; scaled by the display over those
// (Coord2D multiplier) and kept in an ICoord2D (cut to an Int).
inline std::array<int, 2> MilitaryCaptionOrigin(const std::array<int, 2> &position, int width, int height) noexcept
{
	const float multiplierX = static_cast<float>(width) / 800.0f;
	const float multiplierY = static_cast<float>(height) / 600.0f;
	return {static_cast<int>(static_cast<float>(position[0]) * multiplierX), static_cast<int>(static_cast<float>(position[1]) * multiplierY)};
}

// InGameUI::postDraw's named timers: their start at NamedTimerCountdownPosition's share of the display (cut to an Int),
// each line drawn from there to the left (its right edge there) when that share across is 0.5 or more.
struct NamedTimerStart
{
	int x{0};
	int y{0};
	bool fromRight{false};
};

inline NamedTimerStart NamedTimerOrigin(float shareX, float shareY, int width, int height) noexcept
{
	return {static_cast<int>(shareX * static_cast<float>(width)), static_cast<int>(shareY * static_cast<float>(height)), shareX >= 0.5f};
}

// The floating texts' motion (InGameUI's m_floatingTextTimeOut, m_floatingTextMoveUpSpeed, m_floatingTextMoveVanishRate):
// FloatingTextTimeOut through INI::parseDurationUnsignedInt (ceilf of milliseconds x LOGICFRAMES_PER_MSEC_REAL), the
// speed and the vanish rate through INI::parseVelocityReal (a second x SECONDS_PER_LOGICFRAME_REAL); without them the
// constructor's LOGICFRAMES_PER_SECOND / 3 frames, 1 and 0.1 a frame.
struct FloatingTextMotion
{
	std::uint32_t timeoutTicks{10};
	float riseRate{1.0f};
	float vanishPerTick{0.1f};
};

inline FloatingTextMotion FloatingTextMotionFor(const content::InGameUiContent &ui) noexcept
{
	constexpr float framesPerMillisecond = 30.0f / 1000.0f; // LOGICFRAMES_PER_MSEC_REAL
	constexpr float secondsPerFrame = 1.0f / 30.0f;         // SECONDS_PER_LOGICFRAME_REAL
	const auto toFloat = [](const Engine::Math::Fixed &value) { return static_cast<float>(static_cast<double>(value.Raw()) / 65536.0); };
	FloatingTextMotion motion;
	if (ui.floatingTextTimeoutMs)
		motion.timeoutTicks = static_cast<std::uint32_t>(std::ceil(static_cast<float>(*ui.floatingTextTimeoutMs) * framesPerMillisecond));
	if (ui.floatingTextMoveUpSpeed)
		motion.riseRate = toFloat(*ui.floatingTextMoveUpSpeed) * secondsPerFrame;
	if (ui.floatingTextVanishRate)
		motion.vanishPerTick = toFloat(*ui.floatingTextVanishRate) * secondsPerFrame;
	return motion;
}

// LookAtTranslator's SCROLL_RMB with InGameUI::shouldMoveRMBScrollAnchor: the anchor kept within half the display of
// the pointer on each axis (dragged along behind it).
inline void FollowRmbScrollAnchor(int &anchorX, int &anchorY, int pointerX, int pointerY, int width, int height) noexcept
{
	const int maxX = width / 2;
	const int maxY = height / 2;
	if (pointerX + maxX < anchorX)
		anchorX = pointerX + maxX;
	else if (pointerX - maxX > anchorX)
		anchorX = pointerX - maxX;
	if (pointerY + maxY < anchorY)
		anchorY = pointerY + maxY;
	else if (pointerY - maxY > anchorY)
		anchorY = pointerY - maxY;
}

// InGameUI::postDraw with DrawRMBScrollAnchor, while a right-button scroll runs: a cross at the anchor, its black shadow
// (two bars) then its green (two bars): drawFillRect(x, y, width, height, colour) in that order.
struct AnchorRect
{
	int x{0};
	int y{0};
	int width{0};
	int height{0};
	bool green{false}; // GameMakeColor(0, 255, 0, 255); else black (0, 0, 0, 255)
};

inline std::array<AnchorRect, 4> RmbScrollAnchorRects(int x, int y) noexcept
{
	constexpr int w = 2, h = 2, r = 4;
	return {{
		{x - w * r - 1, y - h - 1, w * 2 * r + 3, h * 2 + 3, false},
		{x - w - 1, y - h * r - 1, w * 2 + 3, h * 2 * r + 3, false},
		{x - w * r, y - h, w * 2 * r + 1, h * 2 + 1, true},
		{x - w, y - h * r, w * 2 + 1, h * 2 * r + 1, true},
	}};
}

// The optional unit cap of the select-all / select-matching walks (kindOfSelection, similarUnitSelection, CommandXlat's
// select all): with MaxSelectionSize above 0, no more once that many are selected (GUI:MaxSelectionSize said once).
inline bool SelectionCapReached(int maxSelectionSize, std::size_t selected) noexcept
{
	return maxSelectionSize > 0 && selected >= static_cast<std::size_t>(maxSelectionSize);
}
}
