export module games.generalszh.presentation.interaction.resources.interaction_resources;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
export import games.generalszh.commands.game_commands;
export import games.generalszh.content.objects.kind_of;
export import games.generalszh.content.global.mouse;
export import games.generalszh.content.crates.crate_content;
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
	// The tooltip the window under the pointer gives (GameWindowManager::winProcessMouseEvent: its TOOLTIPTEXT and
	// TOOLTIPDELAY; none: no window's).
	std::optional<std::pair<std::u16string, int>> windowTooltip;
	// Over the radar (the control bar's LeftHUD, LeftHUDInput): the world's hints go on as if no window were there, with
	// nothing picked (the radar is not see-through) at the ground behind it (InGameUI::createCommandHint's underWindow
	// loop); and whether the local player has a radar (rts::localPlayerHasRadar: a move hint there shows the arrow
	// without).
	bool overRadar{false};
	bool hasRadar{false};
	// A replay playing back (RecorderClass::getMode() == RECORDERMODETYPE_PLAYBACK): no command hints.
	bool playback{false};
};

// What lies under the pointer this frame (W3DView::pickDrawable's cast into the scene): every drawn object the pick
// ray meets on its geometry as drawn, nearest first (the fraction along the ray); at the pointer's pixel.
struct PointerHit
{
	std::uint64_t key{0}; // the entity (index and generation)
	float fraction{1.0f};
};

struct PointerHits
{
	float x{-1.0f}, y{-1.0f};
	std::vector<PointerHit> hits;
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
	float farClip{1290.0f}; // the camera's far clip (W3D's Get_Depth): the pick ray reaches twice as far

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
	// SelectionTranslator::m_displayedMaxWarning (said once a game), and the GUI:MaxSelectionSize message for the host
	// to show (the selection's count cap reached this frame).
	bool displayedMaxWarning{false};
	bool maxSelectionWarning{false};
};

// Mouse.ini: DragTolerance (pixels), DragTolerance3D (world units the camera may move), DragToleranceMS.
struct MouseSettings
{
	float dragTolerance{25};
	float dragTolerance3D{25};
	std::uint32_t dragToleranceMs{250};
	// Each cursor's Directions (Mouse.ini's MouseCursor blocks, by content::MouseCursorKind).
	std::array<int, static_cast<std::size_t>(content::MouseCursorKind::Count)> cursorDirections{};
	// InGameUI.ini MaxSelectionSize: the most a selection takes (below 1: no cap).
	int maxSelectionSize{-1};
};

// The mouse cursor the world asks for (InGameUI::setMouseCursor) and
// which of its directions (W3DMouse::setCursorDirection: 0 unless it has several and the camera is scrolling).
struct CursorState
{
	content::MouseCursorKind cursor{content::MouseCursorKind::Arrow}; // W3DMouse::init
	std::uint8_t direction{0};
	// The move hint's quick path (CommandTranslator::handleDefaultMoveCommand, DO_HINT): whether the frame's hint was a
	// move and asked, the point it would go to (the object's place under the pointer, else the ground's), and the host's
	// answer for the point it last asked about (rule_queries QueryQuickPath): true where no selected unit could path there
	// at a glance and the local player's view of it is clear (MSG_DO_INVALID_HINT).
	bool quickPathAsked{false};
	std::array<float, 2> quickPathAt{};
	std::array<float, 2> quickPathAnsweredAt{};
	bool quickPathBlocked{false};
	// LookAtTranslator's m_lastMouseMoveTimeMsec: when the pointer last moved (ms), and where it was.
	std::uint32_t lastMoveMs{0};
	std::array<float, 2> lastPointer{};
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
	PlaceBeacon, // PLACE_BEACON (GUICOMMANDMODE_PLACE_BEACON): doPlaceBeacon
	CombatDrop,  // COMBATDROP: issueCombatDropCommand
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
	// Its RadiusCursorType (the radius cursor following the pointer; 0 none) and its special power's RadiusCursorRadius.
	std::uint8_t radiusCursor{0};
	Engine::Math::Fixed powerCursorRadius;
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
inline constexpr std::uint16_t ClickThrough = 1u << 12;    // KINDOF_CLICK_THROUGH
inline constexpr std::uint16_t Bridge = 1u << 13;          // KINDOF_BRIDGE
inline constexpr std::uint16_t BridgeTower = 1u << 14;     // KINDOF_BRIDGE_TOWER
}

// W3DModelDraw's collision type (View.h PickType): what a pick may take it as.
namespace pick_type
{
inline constexpr std::uint8_t Selectable = 1u << 0;      // PICK_TYPE_SELECTABLE
inline constexpr std::uint8_t Shrubbery = 1u << 1;       // PICK_TYPE_SHRUBBERY
inline constexpr std::uint8_t Mines = 1u << 2;           // PICK_TYPE_MINES
inline constexpr std::uint8_t ForceAttackable = 1u << 3; // PICK_TYPE_FORCEATTACKABLE
}

// W3DModelDraw (as its render object is made): its collision type set from its KindOf in turn, each replacing the
// one before (SELECTABLE, SHRUBBERY, MINE, FORCEATTACKABLE; CLICK_THROUGH: none); then, unless a bridge or bridge
// tower, a dead object (and a structure's rubble) takes none: clicks go to the ground there.
inline std::uint8_t PickTypes(std::uint16_t kinds, bool dead) noexcept
{
	std::uint8_t type = 0;
	if ((kinds & select_kind::Selectable) != 0)
		type = pick_type::Selectable;
	if ((kinds & select_kind::Shrubbery) != 0)
		type = pick_type::Shrubbery;
	if ((kinds & select_kind::Mine) != 0)
		type = pick_type::Mines;
	if ((kinds & select_kind::ForceAttackable) != 0)
		type = pick_type::ForceAttackable;
	if ((kinds & select_kind::ClickThrough) != 0)
		type = 0;
	if ((kinds & (select_kind::Bridge | select_kind::BridgeTower)) == 0 && dead)
		type = 0;
	return type;
}


// A definition's contain module as ActionManager::canEnterObject asks it (ContainModuleInterface::isValidContainerFor,
// isHealContain, isGarrisonable): none; a plain OpenContain (ParachuteContain too); TransportContain (and
// InternetHackContain); HelixContain; OverlordContain; RiderChangeContain; MobNexusContain; GarrisonContain;
// HealContain; TunnelContain; CaveContain.
enum class ContainKind : std::uint8_t
{
	None,
	Open,
	Transport,
	Helix,
	Overlord,
	RiderChange,
	MobNexus,
	Garrison,
	Heal,
	Tunnel,
	Cave,
};

// The special powers context commands use (their SpecialPowerType): SPECIAL_INFANTRY_CAPTURE_BUILDING,
// SPECIAL_BLACKLOTUS_CAPTURE_BUILDING, SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK, SPECIAL_BLACKLOTUS_STEAL_CASH_HACK and
// SPECIAL_HACKER_DISABLE_BUILDING.
enum class ContextPower : std::uint8_t
{
	None,
	InfantryCapture,
	BlackLotusCapture,
	DisableVehicleHack,
	StealCashHack,
	DisableBuildingHack,
};

// A command set's GUI_COMMAND_SPECIAL_POWER button whose power a context command uses, in slot order: its power's type,
// the power (its SpecialPower name) and the button's Options.
struct ContextButton
{
	ContextPower power{ContextPower::None};
	std::string specialPower;
	std::uint32_t options{0};
};

// A saboteur's Sabotage*CrateCollide: what it sabotages and its CrateCollide's rules for what it may touch.
struct SaboteurCollide
{
	content::SabotageKind kind{content::SabotageKind::PowerPlant};
	content::KindOfMask required{};
	content::KindOfMask forbidden{};
	bool buildingPickup{false};
	bool forbidOwner{false};
};

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
	// What context commands ask of it: whether it has an AI (AIUpdateInterface), its TransportSlotCount, its contain
	// module (with OpenContain's AllowInsideKindOf (none given: any), ForbidInsideKindOf, AllowAlliesInside /
	// AllowEnemiesInside / AllowNeutralInside), whether it is a Chinook (ChinookAIUpdate: supplying only while empty),
	// whether it is a salvage crate (SalvageCrateCollide), its Sabotage*CrateCollides (`saboteurCount` of
	// SelectionCatalog::saboteurs from `saboteur`), and its command set's context buttons (an index into
	// SelectionCatalog::contextSets; NoCarBomber: none).
	bool hasAi{false};
	std::uint32_t transportSlots{0};
	ContainKind contain{ContainKind::None};
	// HelixContain ShouldDrawPips (default Yes): No hides its container pips (getContainerPipsToShow).
	bool drawsPips{true};
	bool anyInside{true};
	content::KindOfMask allowInside{};
	content::KindOfMask forbidInside{};
	bool alliesInside{true}, enemiesInside{true}, neutralInside{true};
	bool chinook{false};
	bool salvageCrate{false};
	std::uint32_t saboteur{0};
	std::uint32_t saboteurCount{0};
	std::uint32_t commandSet{NoCarBomber};
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
	std::vector<SaboteurCollide> saboteurs;
	// Command sets' context buttons, by name (`contextSetNames`); the sets upgrades swap in, by their CommandSetOverride
	// id (an index into contextSets; NoCarBomber: none); and each special power template's (by index) ContextPower.
	std::vector<std::vector<ContextButton>> contextSets;
	std::vector<std::string> contextSetNames;
	std::vector<std::uint32_t> overrideSets;
	std::vector<ContextPower> powers;
	// The simulation's last tick (for the powers' readiness and the heal locks, as the original's client reads the logic
	// frame).
	std::uint64_t tick{0};

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
template<> struct ResourceTraits<generalszh::presentation::PointerHits> { static constexpr std::string_view StableName = "generalszh.presentation.pointer_hits"; };
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
