export module games.generalszh.presentation.interaction.resources.mouse_tooltip;
import std;

export import engine.ecs.core.entity;
export import games.generalszh.content.global.mouse;
import engine.ecs.system.system;

// The mouse's tooltip and cursor text (Core/GameEngine/Source/GameClient/Input/Mouse.cpp): Mouse.ini's look and each
// cursor's CursorText as shown; the tooltip's and the cursor text's state (Mouse's m_tooltip* and m_cursorText*
// members, InGameUI's m_mousedOverDrawableID); and what the in-game mouse-over tooltip names (InGameUI::
// createMouseoverHint). World resources of the match's presentation; mouse_tooltip (algorithms) keeps them, the host
// draws them (W3DMouse::draw: drawCursorText, then drawTooltip).
export namespace generalszh::presentation
{
using TooltipColor = std::array<std::uint8_t, 4>; // RGBA, 0-255 (RGBAColorInt)

inline constexpr std::size_t MouseCursorKinds = static_cast<std::size_t>(content::MouseCursorKind::Count);

struct MouseTooltipSettings
{
	content::MouseTooltipContent look;
	// Each cursor's CursorText fetched (TheGameText->fetch; empty: none) and its colours.
	std::array<std::u16string, MouseCursorKinds> cursorTexts{};
	std::array<TooltipColor, MouseCursorKinds> cursorTextColors{};
	std::array<TooltipColor, MouseCursorKinds> cursorTextDropColors{};
};

struct MouseTooltip
{
	// setCursorTooltip: the text (the display string keeps the last one set; m_isTooltipEmpty says whether this frame
	// set one), its delay (-1: TooltipDelayTime), the width share it was last wrapped for and the wrap in pixels
	// (onResolutionChanged: 120), its colours (the constructor's white on black until one is set).
	std::u16string text;
	bool empty{true};
	int delay{-1};
	float lastWidth{0.0f};
	int wrapWidth{120};
	TooltipColor textColor{255, 255, 255, 255};
	TooltipColor backColor{0, 0, 0, 255};
	// createStreamMessages / resetTooltipDelay: when the pointer last moved (ms), whether the tooltip shows, and how far
	// its highlight has run (pixels) since it began (ms).
	std::uint32_t stillTime{0};
	bool display{false};
	int highlightPos{0};
	std::uint32_t highlightStart{0};
	// The pointer's last position (a move this frame restarts the delay).
	float lastX{0.0f}, lastY{0.0f};
	// setCursor / setMouseText: the cursor last set (Mouse::init: ARROW) and the text drawn at the pointer with its
	// colours (the constructor's white).
	content::MouseCursorKind cursor{content::MouseCursorKind::Arrow};
	std::u16string cursorText;
	TooltipColor cursorTextColor{255, 255, 255, 255};
	TooltipColor cursorTextDropColor{255, 255, 255, 255};
	// InGameUI::m_mousedOverDrawableID: the object the last mouse-over named (none: the ground or nothing).
	ecs::Entity mousedOver;
};

// What the mouse-over tooltip says of an object: by definition index its DisplayName as shown (empty: none) and the
// game's text for "ThingTemplate:<name>" (used when it has none), OBJECT:Prop (objects named so get no tooltip),
// TOOLTIP:SupplyWarehouse (a warehouse's worth, "%d"), GameData's ValuePerSupplyBox; by player index its name as shown
// and its colour (Player::getPlayerColor, 0xRRGGBB); and whether the game is a multiplayer one (RecorderClass::
// isMultiplayer: a skirmish or network game, or a replay of one).
struct MouseoverNames
{
	std::vector<std::u16string> displayNames;
	std::vector<std::u16string> templateNames;
	// The game's texts for every object's DisplayName label and, for those without one, "ThingTemplate:<name>" (by
	// template name), fetched once; the definitions the session takes on take theirs from here (KnowMouseoverNames).
	std::map<std::string, std::u16string, std::less<>> labelTexts;
	std::map<std::string, std::u16string, std::less<>> templateTexts;
	std::u16string propText;
	std::u16string warehouseText{u"\n$%d"};
	std::int64_t valuePerSupplyBox{100};
	std::vector<std::u16string> playerNames;
	std::vector<std::uint32_t> playerColors;
	bool multiplayer{false};
};
}

export namespace ecs
{
template<> struct ResourceTraits<generalszh::presentation::MouseTooltipSettings> { static constexpr std::string_view StableName = "generalszh.presentation.mouse_tooltip_settings"; };
template<> struct ResourceTraits<generalszh::presentation::MouseTooltip> { static constexpr std::string_view StableName = "generalszh.presentation.mouse_tooltip"; };
template<> struct ResourceTraits<generalszh::presentation::MouseoverNames> { static constexpr std::string_view StableName = "generalszh.presentation.mouseover_names"; };
}
