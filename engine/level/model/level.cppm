export module engine.level.model.level;
import std;

export import engine.level.model.properties;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAffineTransform3;
export import Engine.Core.Math.FixedOrientedBox3;

// A level: the authored, static description of a playable space, independent
// of the file format it came from and of any game's rules. Format adapters
// (engine/level/adapters/<format>) produce it; games interpret its placement
// types and properties and instantiate entities from it. Simulation-facing
// values are fixed point; data no adapter decodes yet is kept as raw sections.
export namespace engine::level
{
namespace math = Engine::Math;

// Regular grid of height samples; sample (x, y) sits at (x, y) * cellSize.
struct Heightfield
{
	std::uint32_t width{0};
	std::uint32_t height{0};
	math::Fixed cellSize;
	std::vector<math::Fixed> heights; // row-major, width * height
	// Samples along each edge outside the playable area.
	std::uint32_t border{0};
	// Playable extents (in cells) the level may switch between.
	std::vector<std::array<std::int32_t, 2>> playableExtents;

	math::Fixed At(std::uint32_t x, std::uint32_t y) const { return heights[static_cast<std::size_t>(y) * width + x]; }
};

// Tile-splatted terrain texturing. Materials are named textures cut into
// square tiles; every height sample picks a base tile and optionally up to
// two blend overlays (a neighbouring tile faded in along an edge or corner)
// and a cliff mapping (explicit UVs for steep faces). Which texture a
// material name refers to is game content, resolved outside the level.
struct TerrainMaterial
{
	std::string name;
	std::uint32_t firstTile{0};
	std::uint32_t tileCount{0};
	std::uint32_t widthInTiles{0};
};

struct TerrainBlend
{
	std::uint32_t tile{0};
	bool horizontal{false};
	bool vertical{false};
	bool rightDiagonal{false};
	bool leftDiagonal{false};
	std::uint8_t inverted{0}; // bit 0 inverted, bit 1 flipped (3-way blends)
	bool longDiagonal{false};
	std::int32_t edgeMaterial{-1}; // index into edgeMaterials, or -1
};

struct CliffMapping
{
	std::uint32_t tile{0};
	std::array<math::Fixed, 8> uv{}; // u0 v0 u1 v1 u2 v2 u3 v3
	bool flip{false};
	bool mutant{false};
};

struct TerrainSurface
{
	// Per height sample, row-major. Index 0 in blends/cliffs means "none".
	std::vector<std::uint16_t> tiles;
	std::vector<std::uint16_t> blends;
	std::vector<std::uint16_t> extraBlends;
	std::vector<std::uint16_t> cliffs;
	// One bit per sample: the sample is part of a cliff (row stride = bytesPerRow).
	std::vector<std::uint8_t> cliffFlags;
	std::uint32_t cliffFlagBytesPerRow{0};
	std::uint32_t tileCount{0};
	std::vector<TerrainMaterial> materials;
	std::uint32_t edgeTileCount{0};
	std::vector<TerrainMaterial> edgeMaterials;
	std::vector<TerrainBlend> blendTable;  // [0] unused
	std::vector<CliffMapping> cliffTable; // [0] unused
};

enum class PlacementKind : std::uint8_t { Object, Geometry, Trigger, SpawnPoint };

// Something placed in the level: a unit, building, prop, tree, light, sound.
struct Placement
{
	std::string type;
	math::FixedVector3 position;
	math::TurnAngle orientation;
	std::uint32_t flags{0}; // format-defined bits (road/bridge endpoints, ...)
	Properties properties;
	// Optional full pose for authored 3D worlds; existing planar maps continue
	// to use position/orientation. Identity is format-neutral and level-local.
	std::optional<math::FixedAffineTransform3> transform;
	std::uint64_t id{};
	std::uint32_t definition{};
	std::string model;
	PlacementKind kind{PlacementKind::Object};
};

// Volumes and event-driven behavior bindings complement polygon regions and
// scenario condition/action scripts. The game supplies filtering and program
// vocabulary; the level describes geometry and ordered authored bindings.
struct TriggerVolume
{
	std::uint64_t subject{};
	math::FixedOrientedBox3 bounds;
};
struct BehaviorBinding
{
	std::uint64_t id{}, subject{};
	std::string program, parameters;
};

// An authored region which transfers an actor to a full destination pose.
// Games supply eligibility, interaction and state rules. The geometry and
// placement operation are independent of ladders, vehicles or teleports.
struct TraversalPortal
{
	std::uint64_t subject{};
	std::uint32_t ordinal{};
	math::FixedOrientedBox3 bounds;
	math::FixedAffineTransform3 destination;
	Properties properties;
};

inline std::expected<TraversalPortal, std::string> PlaceTraversalPortal(
	TraversalPortal portal, const math::FixedAffineTransform3 &parent)
{
	portal.bounds.center = parent.Point(portal.bounds.center);
	for (auto &axis : portal.bounds.axes) {
		const auto &m = parent.elements;
		axis = {m[0]*axis.x+m[1]*axis.y+m[2]*axis.z,
			m[4]*axis.x+m[5]*axis.y+m[6]*axis.z,
			m[8]*axis.x+m[9]*axis.y+m[10]*axis.z};
	}
	if (!portal.bounds.IsValid()) return std::unexpected("traversal portal requires a rigid parent pose");
	portal.destination = parent * portal.destination;
	return portal;
}

// A named point used for navigation paths and scripting.
struct Marker
{
	std::uint32_t id{0};
	std::string name;
	math::FixedVector3 position;
	std::vector<std::uint32_t> links; // directed links to other marker ids
	Properties properties;
};

// An authored traversal through markers in a specified order. Links describe
// connectivity; paths describe an actual route, including repeated markers.
// Format-specific access masks and actions remain properties of the adapter.
struct NavigationPath
{
	std::uint32_t id{};
	std::string name;
	std::vector<std::uint32_t> markers;
	bool looping{};
	Properties properties;
};

// A named polygon area (trigger areas, water, rivers).
struct Region
{
	std::uint32_t id{0};
	std::string name;
	std::string layer;
	std::vector<math::FixedVector3> points;
	bool water{false};
	bool river{false};
	std::uint32_t riverStart{0};
};

struct Light
{
	std::array<math::Fixed, 3> ambient{};
	std::array<math::Fixed, 3> diffuse{};
	math::FixedVector3 direction;
};

// Lighting for one time of day: lights for the terrain and for objects.
struct LightingSet
{
	std::vector<Light> terrain;
	std::vector<Light> objects;
};

struct Lighting
{
	std::uint32_t current{0}; // index into sets
	std::vector<LightingSet> sets;
	std::uint32_t shadowColor{0}; // packed ARGB as authored
};

// Script data: groups of scripts, each with conditions (an OR of AND
// clauses) and actions. Condition/action kinds are the format's codes;
// the game maps them onto its script vocabulary.
// A parameter carries either a position (for position-typed parameters) or
// the integer/number/text triple; which fields matter depends on `kind`.
struct ScriptParameter
{
	std::uint32_t kind{0};
	std::int64_t integer{0};
	math::Fixed number;
	std::string text;
	math::FixedVector3 position;
};

struct ScriptCall
{
	std::uint32_t kind{0};
	std::string name; // the format's internal name for the kind, when stored
	std::vector<ScriptParameter> parameters;
};

struct Script
{
	std::string name;
	std::string comment;
	std::string conditionComment;
	std::string actionComment;
	bool active{true};
	bool oneShot{false};
	bool subroutine{false};
	std::array<bool, 3> difficulty{true, true, true};
	std::uint32_t evaluationDelaySeconds{0};
	std::vector<std::vector<ScriptCall>> conditions; // OR of AND clauses
	std::vector<ScriptCall> actions;
	std::vector<ScriptCall> falseActions;
};

struct ScriptGroup
{
	std::string name;
	bool active{true};
	bool subroutine{false};
	std::vector<Script> scripts;
};

struct ScriptList
{
	std::vector<Script> scripts;
	std::vector<ScriptGroup> groups;
};

// Something a participant plans to have placed (an AI base layout).
struct PlannedPlacement
{
	std::string name;
	std::string type;
	math::FixedVector3 position;
	math::TurnAngle orientation;
	bool initiallyPlaced{false};
	std::int32_t rebuilds{0};
	std::string script;
	std::int32_t health{100};
	bool reportsWhenAttacked{false};
	bool sellable{true};
	bool repairable{true};
};

// Scenario setup: participants and their groups ("sides"/"teams" in RTS
// terms), described by properties, plus each participant's plan and scripts.
struct Participant
{
	Properties properties;
	std::vector<PlannedPlacement> plan;
	ScriptList scripts;
};

struct Scenario
{
	std::vector<Participant> participants;
	std::vector<Properties> groups;
};

// Format data kept verbatim until an adapter decodes it.
struct Section
{
	std::string name;
	std::uint32_t version{0};
	std::vector<std::byte> data;
};

struct Level
{
	Properties properties;
	Heightfield terrain;
	TerrainSurface surface;
	std::vector<Placement> placements;
	std::vector<Marker> markers;
	// Every marker link (from, to) in the order the level gives them (each marker's `links` keeps only its own).
	std::vector<std::array<std::uint32_t, 2>> markerLinks;
	std::vector<NavigationPath> navigationPaths;
	std::vector<Region> regions;
	Lighting lighting;
	Scenario scenario;
	std::vector<Section> sections;
	std::vector<TriggerVolume> volumes;
	std::vector<BehaviorBinding> behaviors;
	std::vector<TraversalPortal> traversalPortals;

	const Section *FindSection(const std::string &name) const noexcept
	{
		for (const Section &section : sections)
			if (section.name == name)
				return &section;
		return nullptr;
	}
};

inline std::expected<void, std::string> ValidateSpatialLevel(const Level &level)
{
	std::set<std::uint32_t> marker_ids, path_ids;
	for (const auto &marker : level.markers)
		if (!marker_ids.insert(marker.id).second) return std::unexpected("duplicate level marker identity");
	for (const auto &path : level.navigationPaths) {
		if (!path_ids.insert(path.id).second) return std::unexpected("duplicate level navigation path identity");
		for (const auto marker : path.markers)
			if (!marker_ids.contains(marker)) return std::unexpected("navigation path references a missing level marker");
	}
	std::set<std::uint64_t> identities;
	for (const auto &placement : level.placements) {
		if (placement.id && !identities.insert(placement.id).second) return std::unexpected("duplicate level placement identity");
		if (placement.kind == PlacementKind::Geometry && !placement.transform) return std::unexpected("geometry placement requires a full pose");
	}
	for (const auto &volume : level.volumes) {
		if (!volume.subject || !identities.contains(volume.subject)) return std::unexpected("trigger volume references a missing level placement");
		if (!volume.bounds.IsValid()) return std::unexpected("invalid oriented trigger volume");
	}
	std::set<std::pair<std::uint64_t, std::uint32_t>> portals;
	for (const auto &portal : level.traversalPortals) {
		if (!portal.subject || !identities.contains(portal.subject)) return std::unexpected("traversal portal references a missing level placement");
		if (!portals.emplace(portal.subject, portal.ordinal).second) return std::unexpected("duplicate level traversal portal identity");
		if (!portal.bounds.IsValid()) return std::unexpected("invalid oriented traversal portal");
	}
	std::set<std::uint64_t> bindings;
	for (const auto &binding : level.behaviors) {
		if (!binding.id || !bindings.insert(binding.id).second) return std::unexpected("duplicate or null level behavior identity");
		if (!binding.subject || !identities.contains(binding.subject) || binding.program.empty()) return std::unexpected("invalid level behavior subject or program");
	}
	return {};
}
}
