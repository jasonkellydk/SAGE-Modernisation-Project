export module games.generalszh.content.global.mouse;
import std;

export import engine.config.binding.schema;

// Mouse.ini (Core/GameEngine/Source/GameClient/Input/Mouse.cpp): the Mouse block's drag tolerances
// (TheMouseFieldParseTable, parseUnsignedInt) and tooltip look (Mouse::Mouse's defaults: Times New Roman 12, the
// background animated, 50 ms to fill and to wait, its colours; TooltipWidth parsePercentToReal, its default 15 kept as
// the constructor sets it) and the MouseCursor blocks (TheMouseCursorFieldParseTable, CursorInfo's defaults: a 16,16
// hotspot, one frame at 20 FPS, one direction, no CursorText, its colours black). Only the cursors the original knows by name are kept
// (INI::parseMouseCursorDefinition ignores the rest). With WinCursors (the default) the cursors are system ones,
// Win32Mouse::initCursorResources' "Data/Cursors/<Texture>.ANI", or "<Texture><direction>.ANI" per direction when
// there is more than one.
export namespace generalszh::content
{
// The original's MouseCursor order (Mouse::MouseCursor, CursorNames).
enum class MouseCursorKind : std::uint8_t
{
	None,
	Normal,
	Arrow,
	Scroll,
	Target,
	Move,
	AttackMove,
	AttackObj,
	ForceAttackObj,
	ForceAttackGround,
	Build,
	InvalidBuild,
	GenericInvalid,
	Select,
	EnterFriendly,
	EnterAggressive,
	SetRallyPoint,
	GetRepaired,
	GetHealed,
	DoRepair,
	ResumeConstruction,
	CaptureBuilding,
	SnipeVehicle,
	LaserGuidedMissiles,
	TankHunterTntAttack,
	StabAttack,
	PlaceRemoteCharge,
	PlaceTimedCharge,
	Defector,
	Dock,
	FireFlame,
	FireBomb,
	PlaceBeacon,
	DisguiseAsVehicle,
	Waypoint,
	OutRange,
	StabAttackInvalid,
	PlaceChargeInvalid,
	Hack,
	ParticleUplinkCannon,
	Count,
};

constexpr std::array<std::string_view, static_cast<std::size_t>(MouseCursorKind::Count)> MouseCursorNames{
	"None", "Normal", "Arrow", "Scroll", "Target", "Move", "AttackMove", "AttackObj", "ForceAttackObj", "ForceAttackGround",
	"Build", "InvalidBuild", "GenericInvalid", "Select", "EnterFriendly", "EnterAggressive", "SetRallyPoint", "GetRepaired",
	"GetHealed", "DoRepair", "ResumeConstruction", "CaptureBuilding", "SnipeVehicle", "LaserGuidedMissiles",
	"TankHunterTNTAttack", "StabAttack", "PlaceRemoteCharge", "PlaceTimedCharge", "Defector", "Dock", "FireFlame",
	"FireBomb", "PlaceBeacon", "DisguiseAsVehicle", "Waypoint", "OutRange", "StabAttackInvalid", "PlaceChargeInvalid",
	"Hack", "ParticleUplinkCannon"};

struct MouseCursorDefinition
{
	std::string texture, image;
	std::array<int, 2> hotSpot{16, 16};
	int frames{1};
	Engine::Math::Fixed fps{Engine::Math::Fixed::FromInt(20)};
	int directions{1};
	// CursorText (a string label, fetched when the cursor is set: Mouse::setCursor) and its colours (parseRGBAColorInt;
	// CursorInfo leaves their alpha unset, here 0).
	std::string cursorText;
	std::array<std::uint8_t, 4> cursorTextColor{0, 0, 0, 0};
	std::array<std::uint8_t, 4> cursorTextDropColor{0, 0, 0, 0};
};

// The mouse's tooltip (Mouse::Mouse's defaults; TheMouseFieldParseTable's fields).
struct MouseTooltipContent
{
	std::string fontName{"Times New Roman"}; // TooltipFontName
	int fontSize{12};                        // TooltipFontSize
	bool fontBold{false};                    // TooltipFontIsBold
	bool animateBackground{true};            // TooltipAnimateBackground
	int fillTimeMs{50};                      // TooltipFillTime
	int delayTimeMs{50};                     // TooltipDelayTime
	std::array<std::uint8_t, 4> textColor{220, 220, 220, 255};    // TooltipTextColor
	std::array<std::uint8_t, 4> highlightColor{255, 255, 0, 255}; // TooltipHighlightColor
	std::array<std::uint8_t, 4> shadowColor{0, 0, 0, 255};        // TooltipShadowColor
	std::array<std::uint8_t, 4> backgroundColor{20, 20, 0, 127};  // TooltipBackgroundColor
	std::array<std::uint8_t, 4> borderColor{0, 0, 0, 255};        // TooltipBorderColor
	std::string widthText;       // TooltipWidth as written (a percent of the screen's width; none: the constructor's 15)
	bool useAltTextColor{false}; // UseTooltipAltTextColor
	bool useAltBackColor{false}; // UseTooltipAltBackColor
	bool adjustAltColor{false};  // AdjustTooltipAltColor
};

struct MouseContent
{
	unsigned dragTolerance{0}, dragTolerance3D{0}, dragToleranceMs{0};
	MouseTooltipContent tooltip;
	std::array<MouseCursorDefinition, static_cast<std::size_t>(MouseCursorKind::Count)> cursors{};

	const MouseCursorDefinition &Cursor(MouseCursorKind kind) const { return cursors[static_cast<std::size_t>(kind)]; }
};

// The .ANI a system cursor loads for one of its directions (empty: it has no Texture).
inline std::string MouseCursorFile(const MouseCursorDefinition &cursor, int direction)
{
	if (cursor.texture.empty())
		return {};
	return cursor.directions > 1 ? std::format("Data/Cursors/{}{}.ANI", cursor.texture, direction) : std::format("Data/Cursors/{}.ANI", cursor.texture);
}

inline MouseContent BindMouse(const engine::config::Document &document)
{
	MouseContent mouse;
	const auto integer = [](const engine::config::Node &field, auto &out) {
		if (!field.values.empty())
			std::from_chars(field.Value().data(), field.Value().data() + field.Value().size(), out);
	};
	// INI::parseBool.
	const auto boolean = [](const engine::config::Node &field, bool &out) {
		if (!field.values.empty())
			out = engine::config::values::ParseBool(field.Value()).value_or(out);
	};
	// INI::parseRGBAColorInt ("R:220 G:220 B:220 [A:255]": A 255 when left out).
	const auto color = [](const engine::config::Node &field, std::array<std::uint8_t, 4> &out) {
		std::array<int, 4> read{0, 0, 0, 255};
		for (const std::string_view value : field.values)
			for (const auto &[prefix, index] : {std::pair{"R:", 0}, std::pair{"G:", 1}, std::pair{"B:", 2}, std::pair{"A:", 3}})
				if (value.size() > 2 && (value[0] == prefix[0] || value[0] == static_cast<char>(std::tolower(static_cast<unsigned char>(prefix[0])))) && value[1] == ':')
					std::from_chars(value.data() + 2, value.data() + value.size(), read[static_cast<std::size_t>(index)]);
		for (std::size_t index = 0; index < 4; ++index)
			out[index] = static_cast<std::uint8_t>(std::clamp(read[index], 0, 255));
	};
	const auto equal = [](std::string_view left, std::string_view right) {
		return left.size() == right.size() && std::ranges::equal(left, right, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
	};
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key == "Mouse")
		{
			for (const engine::config::Node &field : root.children)
				if (field.key == "DragTolerance")
					integer(field, mouse.dragTolerance);
				else if (field.key == "DragTolerance3D")
					integer(field, mouse.dragTolerance3D);
				else if (field.key == "DragToleranceMS")
					integer(field, mouse.dragToleranceMs);
				else if (field.key == "TooltipFontName" && !field.values.empty())
					mouse.tooltip.fontName = std::string(field.Value());
				else if (field.key == "TooltipFontSize")
					integer(field, mouse.tooltip.fontSize);
				else if (field.key == "TooltipFontIsBold")
					boolean(field, mouse.tooltip.fontBold);
				else if (field.key == "TooltipAnimateBackground")
					boolean(field, mouse.tooltip.animateBackground);
				else if (field.key == "TooltipFillTime")
					integer(field, mouse.tooltip.fillTimeMs);
				else if (field.key == "TooltipDelayTime")
					integer(field, mouse.tooltip.delayTimeMs);
				else if (field.key == "TooltipTextColor")
					color(field, mouse.tooltip.textColor);
				else if (field.key == "TooltipHighlightColor")
					color(field, mouse.tooltip.highlightColor);
				else if (field.key == "TooltipShadowColor")
					color(field, mouse.tooltip.shadowColor);
				else if (field.key == "TooltipBackgroundColor")
					color(field, mouse.tooltip.backgroundColor);
				else if (field.key == "TooltipBorderColor")
					color(field, mouse.tooltip.borderColor);
				else if (field.key == "TooltipWidth" && !field.values.empty())
				{
					// INI::scanPercentToReal, worked out by the presentation (TooltipWidthShare): content holds no floats.
					mouse.tooltip.widthText = std::string(field.Value());
				}
				else if (field.key == "UseTooltipAltTextColor")
					boolean(field, mouse.tooltip.useAltTextColor);
				else if (field.key == "UseTooltipAltBackColor")
					boolean(field, mouse.tooltip.useAltBackColor);
				else if (field.key == "AdjustTooltipAltColor")
					boolean(field, mouse.tooltip.adjustAltColor);
			continue;
		}
		if (root.key != "MouseCursor" || root.values.empty())
			continue;
		// Mouse::getCursorIndex: a case-insensitive name match.
		const auto named = std::ranges::find_if(MouseCursorNames, [&](std::string_view name) { return equal(name, root.Value()); });
		if (named == MouseCursorNames.end())
			continue;
		MouseCursorDefinition &cursor = mouse.cursors[static_cast<std::size_t>(named - MouseCursorNames.begin())];
		for (const engine::config::Node &field : root.children)
		{
			const std::string_view key = field.key;
			if (key == "Texture" && !field.values.empty())
				cursor.texture = std::string(field.Value());
			else if (key == "Image" && !field.values.empty())
				cursor.image = std::string(field.Value());
			else if (key == "CursorText" && !field.values.empty())
				cursor.cursorText = std::string(field.Value());
			else if (key == "CursorTextColor")
				color(field, cursor.cursorTextColor);
			else if (key == "CursorTextDropColor")
				color(field, cursor.cursorTextDropColor);
			else if (key == "Frames")
				integer(field, cursor.frames);
			else if (key == "Directions")
				integer(field, cursor.directions);
			else if (key == "FPS" && !field.values.empty())
				cursor.fps = engine::config::values::ParseFixed(field.Value()).value_or(cursor.fps);
			else if (key == "HotSpot")
				// INI::parseICoord2D ("X:16 Y:16").
				for (const std::string_view value : field.values)
					for (const auto &[prefix, index] : {std::pair{"X:", 0}, std::pair{"Y:", 1}})
						if (value.starts_with(prefix))
							std::from_chars(value.data() + 2, value.data() + value.size(), cursor.hotSpot[static_cast<std::size_t>(index)]);
		}
	}
	return mouse;
}
}
