export module games.generalszh.content.global.mouse;
import std;

export import engine.config.binding.schema;

// Mouse.ini (Core/GameEngine/Source/GameClient/Input/Mouse.cpp): the Mouse block's drag tolerances
// (TheMouseFieldParseTable, parseUnsignedInt) and the MouseCursor blocks (TheMouseCursorFieldParseTable, CursorInfo's
// defaults: a 16,16 hotspot, one frame at 20 FPS, one direction). Only the cursors the original knows by name are kept
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
};

struct MouseContent
{
	unsigned dragTolerance{0}, dragTolerance3D{0}, dragToleranceMs{0};
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
