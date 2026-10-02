export module games.generalszh.presentation.hud.resources.in_game_overlay;
import std;

export import games.generalszh.presentation.objects.components.object_icons;
export import games.generalszh.presentation.objects.algorithms.object_icon_layout;
export import games.generalszh.hud.superweapon_timers;
import engine.ecs.system.system;

// What the presentation draws over the world this frame, in screen pixels (InGameUI::postDraw and Drawable's UI): the
// frame packet the interface's drawing reads, extracted from the world by ExtractInGameOverlay.
export namespace generalszh::presentation
{
// What the interaction draws over the world: the selection box being dragged, and each selected object's
// health bar (pixels: its top-left corner and width; health as a share).
struct SelectedMarker
{
	float x{0}, y{0}, width{0};
	float health{1};
	bool damaged{false}, reallyDamaged{false}, disabled{false};
};

// A floating text as it shows this frame (InGameUI::drawFloatingText): centred on x, its top at y, colour and alpha.
struct OverlayText
{
	std::u16string text;
	float x{0}, y{0};
	std::array<float, 4> color{1, 1, 1, 1};
	// A drawable's caption (drawConstructPercent's string): in the drawable caption font (InGameUI's DrawableCaption*,
	// Language.ini's DrawableCaptionFont), not the display string font.
	bool caption{false};
};

// A drawable's caption (Drawable::drawCaption: a beacon's text): its text, the screen point of the drawable's centre
// (the text goes half its width left of it, its top there), over a translucent black box a pixel larger with a dark
// outline; DrawableCaptionColor with a black drop shadow, in the drawable caption font.
struct OverlayDrawableCaption
{
	std::u16string text;
	std::int32_t x{0};
	std::int32_t y{0};
	std::array<float, 4> color{1, 1, 1, 1};
};

// A selected unit's control group number (Drawable::drawUIText): the numeral NUMBER:<group> at (x, y), its colour, its
// drop shadow's colour and offset (DrawGroupInfo), in DrawGroupInfo's font.
struct OverlayGroupNumber
{
	std::int32_t group{0};
	std::int32_t x{0};
	std::int32_t y{0};
	std::array<float, 4> color{1, 1, 1, 1};
	std::array<float, 4> dropColor{0, 0, 0, 1};
	std::int32_t dropX{0};
	std::int32_t dropY{0};
};

// A world animation's image this frame (InGameUI::updateAndDrawWorldAnimations): its mapped image, centred on
// its screen point, its own size times `scale` (1.3 over the camera zoom), at `alpha`.
struct OverlayImage
{
	std::string image;
	float x{0}, y{0};
	float scale{1};
	float alpha{1};
	// An object's icon instead (Drawable::drawIconUI): placed by object_icon_layout once its image's size is known,
	// against its health region (`region`) or, for veterancy, its health box position (x, y) and width over the zoom.
	enum class Placement : std::uint8_t
	{
		Centred,
		Icon,
		Veterancy,
		AmmoPip, // Drawable::drawAmmo's `pip`th pip, placed by PlaceAmmoPip once its image's size is known
		ContainerPip, // Drawable::drawContained's `pip`th pip (`pipFull`), placed by PlaceContainerPip
	};
	Placement placement{Placement::Centred};
	ObjectIcon icon{ObjectIcon::Disabled};
	IconRegion region;
	float iconScale{1};
	float healthBoxWidth{0};
	float zoom{1};
	int pip{0};
	int pipCenterY{0};
	float pipOffset{0};
	float pipBounding{0};
	bool pipFull{false};
	// The colour it is drawn in (drawImage's colour: a full container pip green for infantry, blue for others).
	std::array<float, 3> tint{1, 1, 1};
};

// The messages at the top of the screen (InGameUI::postDraw): oldest first, each line under the last from `messageAt`
// (pixels), in its colour with a black drop at its alpha.
struct OverlayMessage
{
	std::u16string text;
	std::array<float, 4> color{1, 1, 1, 1};
};

// A superweapon countdown line (InGameUI's SuperweaponInfo as drawn): none shown is a skipped one (still being built);
// its power's name (GUI:<name> labels it), m:ss, its colour this frame, and whether it is ready (the bold font).
struct OverlaySuperweapon
{
	bool shown{false};
	std::string power;
	std::u16string time;
	std::array<float, 4> color{1, 1, 1, 1};
	bool ready{false};
};

// The military caption (InGameUI's military subtitle as drawn): its lines typed so far, its colour this frame, whether
// the block after the last letter shows, and where it starts (on an 800x600 screen).
struct OverlayCaption
{
	bool shown{false};
	std::vector<std::u16string> lines;
	std::array<float, 4> color{1, 1, 1, 1};
	bool block{false};
	std::array<float, 2> at{10, 380};
};

struct InGameOverlay
{
	bool boxActive{false};
	std::vector<OverlayMessage> messages;
	std::array<float, 2> messageAt{10, 10};
	std::array<float, 4> box{};
	std::vector<SelectedMarker> selected;
	std::vector<OverlayText> texts;
	std::vector<OverlayDrawableCaption> captions;
	std::vector<OverlayGroupNumber> groupNumbers;
	std::vector<OverlayImage> images;
	// The superweapon countdowns (none while a script hides them), from their position (a share of the screen).
	std::vector<OverlaySuperweapon> superweapons;
	std::array<float, 2> superweaponAt{0.9f, 0.01f};
	OverlayCaption caption;
	// The named timers (InGameUI's, as drawn): from `namedTimerAt` (a share of the screen) up, a line each; right-aligned
	// there from the middle rightwards; the ready font for a countdown at 0:00; its colour.
	struct NamedTimerLine
	{
		std::u16string text;
		bool ready{false};
		std::array<float, 4> color{1, 1, 1, 1};
	};
	std::vector<NamedTimerLine> namedTimers;
	std::array<float, 2> namedTimerAt{0.7f, 0.7f};
	// The screen fade (ScriptEngine's m_fade as W3DStatusCircle draws it): none, add, subtract, saturate, multiply; value.
	std::uint8_t fade{0};
	float fadeValue{0.0f};
	// The right-button scroll's anchor while it scrolls, with DrawRMBScrollAnchor (LookAtTranslator::getRMBScrollAnchor).
	bool rmbAnchorShown{false};
	std::array<int, 2> rmbAnchor{};
};
}

// The ready superweapon countdowns' flash (InGameUI's m_superweaponLastFlashFrame / m_superweaponUsedFlashColor, kept
// across frames): presentation state in the world.
export namespace ecs
{
template<>
struct ResourceTraits<generalszh::hud::SuperweaponFlash>
{
	static constexpr std::string_view StableName = "generalszh.presentation.superweapon_flash";
};
}
