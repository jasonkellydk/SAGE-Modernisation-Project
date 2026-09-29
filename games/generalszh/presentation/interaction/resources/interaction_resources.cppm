export module games.generalszh.presentation.interaction.resources.interaction_resources;
import std;

export import engine.ecs.core.entity;
export import games.generalszh.commands.game_commands;
export import games.generalszh.content.objects.kind_of;
export import games.generalszh.content.global.mouse;
import engine.ecs.system.system;

// What the player's pointer did this frame, how the view projects the world,
// what the interaction remembers between frames (the original's
// SelectionTranslator members), what can be selected (per definition), and
// the orders the player gave this frame for the host to submit.
export namespace generalszh::presentation
{
namespace pointer_button
{
inline constexpr std::uint8_t Left = 1u << 0;
inline constexpr std::uint8_t Right = 1u << 1;
}

// Written by the host every frame before the frame's systems run.
struct PointerInput
{
	float x{0}, y{0};          // pixels
	std::uint8_t down{0};      // pointer_button bits held now
	std::uint8_t pressed{0};   // went down this frame
	std::uint8_t released{0};  // came up this frame
	bool shift{false}, ctrl{false}, alt{false};
	std::uint32_t timeMs{0};   // the platform clock
	bool overInterface{false}; // a window takes the pointer (the world does not see it)
	// The camera scrolling (LookAtTranslator: a right-button drag or the arrow keys) and this frame's scroll offset
	// in pixels (InGameUI::getScrollAmount).
	bool scrolling{false};
	float scrollX{0}, scrollY{0};
};

// The camera as presentation projects with it (eye, its basis, the view plane's half extents at depth 1) and
// the viewport in pixels.
struct InteractionView
{
	std::array<float, 3> eye{};
	std::array<float, 3> right{1, 0, 0};
	std::array<float, 3> up{0, 0, 1};
	std::array<float, 3> forward{0, 1, 0};
	float halfWidth{1}, halfHeight{1};
	float width{800}, height{600};
	bool valid{false};

	// World to pixels; false behind the eye.
	bool Project(float x, float y, float z, float &sx, float &sy) const noexcept
	{
		const float dx = x - eye[0], dy = y - eye[1], dz = z - eye[2];
		const float depth = dx * forward[0] + dy * forward[1] + dz * forward[2];
		if (depth <= 0.01f)
			return false;
		const float ndcX = (dx * right[0] + dy * right[1] + dz * right[2]) / (depth * halfWidth);
		const float ndcY = (dx * up[0] + dy * up[1] + dz * up[2]) / (depth * halfHeight);
		sx = (ndcX + 1.0f) * 0.5f * width;
		sy = (1.0f - ndcY) * 0.5f * height;
		return true;
	}

	// The ray through a pixel (from the eye), normalised.
	std::array<float, 3> Ray(float sx, float sy) const noexcept
	{
		const float ndcX = sx / width * 2.0f - 1.0f;
		const float ndcY = 1.0f - sy / height * 2.0f;
		std::array<float, 3> direction{};
		for (std::size_t axis = 0; axis < 3; ++axis)
			direction[axis] = forward[axis] + right[axis] * ndcX * halfWidth + up[axis] * ndcY * halfHeight;
		const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
		for (float &value : direction)
			value /= length > 0.0f ? length : 1.0f;
		return direction;
	}
};

// SelectionTranslator's state between frames.
struct InteractionState
{
	bool leftDown{false};
	float leftAnchorX{0}, leftAnchorY{0};
	bool dragSelecting{false};
	bool rightDown{false};
	float rightAnchorX{0}, rightAnchorY{0};
	std::uint32_t rightDownTimeMs{0};
	std::array<float, 3> rightDownCamera{};
};

// Mouse.ini: DragTolerance (pixels), DragTolerance3D (world units the camera may move), DragToleranceMS.
struct MouseSettings
{
	float dragTolerance{25};
	float dragTolerance3D{25};
	std::uint32_t dragToleranceMs{250};
	// Each cursor's Directions (Mouse.ini's MouseCursor blocks, by content::MouseCursorKind).
	std::array<int, static_cast<std::size_t>(content::MouseCursorKind::Count)> cursorDirections{};
};

// The mouse cursor the world asks for (InGameUI::setMouseCursor) and
// which of its directions (W3DMouse::setCursorDirection: 0 unless it has several and the camera is scrolling).
struct CursorState
{
	content::MouseCursorKind cursor{content::MouseCursorKind::Arrow}; // W3DMouse::init
	std::uint8_t direction{0};
};

// InGameUI's MOUSEMODE_GUI_COMMAND (setGUICommand): a command button waiting for its target: whose it is (the object
// the control bar shows), the special power it fires and that power's SpecialPowerType, its Options (NEED_TARGET_*,
// content::button_option), and its cursors over a valid and an invalid target (CursorName / InvalidCursorName; CROSS,
// the INI's Target, when unknown).
// Which kind of command waits: a special power (a context command: handleGuiCommand) or a guard button
// (GUICommandTranslator's doGuardCommand, in `guardMode`: 0 normal, 1 without pursuit, 2 flying units only).
enum class GuiCommandKind : std::uint8_t
{
	SpecialPower,
	Guard,
	RallyPoint, // SET_RALLY_POINT: doSetRallyPointCommand
	AttackMove, // ATTACK_MOVE: doAttackMoveCommand
	FireWeapon, // FIRE_WEAPON: doFireWeaponCommand, or handleGuiCommand as a context command
};

struct GuiTargeting
{
	bool active{false};
	ecs::Entity source;
	std::string power;
	std::string powerType;
	std::uint32_t options{0};
	content::MouseCursorKind cursor{content::MouseCursorKind::Target};
	content::MouseCursorKind invalidCursor{content::MouseCursorKind::Target};
	// For a command needing an object: the object under the pointer (the interaction's pick), and the one the host
	// found the power may be fired at (canDoSpecialPowerAtObject through the session; none: not that one).
	ecs::Entity hovered;
	ecs::Entity validFor;
	GuiCommandKind kind{GuiCommandKind::SpecialPower};
	std::uint8_t guardMode{0};
	// FIRE_WEAPON: its WeaponSlot and MaxShotsToFire (0: no limit).
	std::uint8_t weaponSlot{0};
	std::uint32_t maxShots{0};
	// SPECIAL_POWER_FROM_SHORTCUT: the power's type; its source is the player's most ready object with it, as it stands
	// each frame (CommandXlat's findMostReadyShortcutSpecialPowerOfType; none: no target is valid). Empty: not one.
	std::string shortcutType;
};

// The selection box being dragged (for drawing), in pixels.
struct SelectionBox
{
	bool active{false};
	float x0{0}, y0{0}, x1{0}, y1{0};
};

namespace select_kind
{
inline constexpr std::uint16_t Selectable = 1u << 0;       // KINDOF_SELECTABLE
inline constexpr std::uint16_t AlwaysSelectable = 1u << 1; // KINDOF_ALWAYS_SELECTABLE
inline constexpr std::uint16_t ForceAttackable = 1u << 2;  // KINDOF_FORCEATTACKABLE
inline constexpr std::uint16_t Structure = 1u << 3;        // KINDOF_STRUCTURE
inline constexpr std::uint16_t Infantry = 1u << 4;         // KINDOF_INFANTRY
inline constexpr std::uint16_t Crate = 1u << 5;            // KINDOF_CRATE
inline constexpr std::uint16_t Mine = 1u << 6;             // KINDOF_MINE
inline constexpr std::uint16_t Shrubbery = 1u << 7;        // KINDOF_SHRUBBERY
inline constexpr std::uint16_t IgnoredInGui = 1u << 8;     // KINDOF_IGNORED_IN_GUI
inline constexpr std::uint16_t Vehicle = 1u << 9;          // KINDOF_VEHICLE
inline constexpr std::uint16_t Aircraft = 1u << 10;        // KINDOF_AIRCRAFT
inline constexpr std::uint16_t RepairPad = 1u << 11;       // KINDOF_REPAIR_PAD
}

// Per definition (DefinitionRef::index): its kinds that matter to selection, and its pick volume (a sphere
// of `radius` whose centre is `center` up from its position).
struct SelectionLook
{
	std::uint16_t kinds{0};
	float radius{1};
	float center{0};
	// Object::getHealthBoxPosition / getHealthBoxDimensions: the box sits 10 above its top
	// (GeometryInfo::getMaxHeightAbovePosition), max(20, clamp(major + minor radius, 20, 150) * 2) pixels wide at
	// zoom 1 (none for IGNORED_IN_GUI).
	float top{0};
	float healthBoxWidth{0};
	// A car bomber's ConvertToCarBombCrateCollide (an index into SelectionCatalog::carBombers; NoCarBomber: none), and
	// whether it may be made a car bomb: with an AI, not AIRCRAFT or BOAT, with a CARBOMB weapon set.
	static constexpr std::uint32_t NoCarBomber = 0xFFFFFFFFu;
	std::uint32_t carBomber{NoCarBomber};
	bool carBombable{false};
	// A hijacker's ConvertToHijackedVehicleCrateCollide (an index into SelectionCatalog::hijackers; NoCarBomber: none).
	std::uint32_t hijacker{NoCarBomber};
};

// A car bomber's collide kinds (RequiredKindOf, ForbiddenKindOf).
struct CarBomberKinds
{
	content::KindOfMask required{};
	content::KindOfMask forbidden{};
};

struct SelectionCatalog
{
	std::vector<SelectionLook> byDefinition;
	std::vector<content::KindOfMask> kinds; // each definition's KindOf
	std::vector<CarBomberKinds> carBombers;
	std::vector<CarBomberKinds> hijackers; // each hijacker's collide kinds

	const SelectionLook *Of(std::uint32_t definition) const noexcept
	{
		return definition < byDefinition.size() ? &byDefinition[definition] : nullptr;
	}
};

// The local player (the seat's player index in the simulation).
struct LocalPlayer
{
	std::uint32_t player{0};
	bool valid{false};
};

// The orders the player gave this frame (the host submits them to the match).
struct PlayerOrders
{
	std::vector<commands::GameCommand> pending;
};
}

export namespace ecs
{
template<> struct ResourceTraits<generalszh::presentation::PointerInput> { static constexpr std::string_view StableName = "generalszh.presentation.pointer_input"; };
template<> struct ResourceTraits<generalszh::presentation::InteractionView> { static constexpr std::string_view StableName = "generalszh.presentation.interaction_view"; };
template<> struct ResourceTraits<generalszh::presentation::InteractionState> { static constexpr std::string_view StableName = "generalszh.presentation.interaction_state"; };
template<> struct ResourceTraits<generalszh::presentation::MouseSettings> { static constexpr std::string_view StableName = "generalszh.presentation.mouse_settings"; };
template<> struct ResourceTraits<generalszh::presentation::GuiTargeting> { static constexpr std::string_view StableName = "generalszh.presentation.gui_targeting"; };
template<> struct ResourceTraits<generalszh::presentation::CursorState> { static constexpr std::string_view StableName = "generalszh.presentation.cursor_state"; };
template<> struct ResourceTraits<generalszh::presentation::SelectionBox> { static constexpr std::string_view StableName = "generalszh.presentation.selection_box"; };
template<> struct ResourceTraits<generalszh::presentation::SelectionCatalog> { static constexpr std::string_view StableName = "generalszh.presentation.selection_catalog"; };
template<> struct ResourceTraits<generalszh::presentation::LocalPlayer> { static constexpr std::string_view StableName = "generalszh.presentation.local_player"; };
template<> struct ResourceTraits<generalszh::presentation::PlayerOrders> { static constexpr std::string_view StableName = "generalszh.presentation.player_orders"; };
}
