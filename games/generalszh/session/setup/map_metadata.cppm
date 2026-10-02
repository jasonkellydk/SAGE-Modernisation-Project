export module games.generalszh.session.setup.map_metadata;
import std;

export import games.generalszh.content.maps.map_cache;
export import engine.level.model.level;

// MapUtil.cpp loadMap and MapCache::addMap: what the map cache records of a map file it loads for the purpose (a user
// map new or changed): its waypoints (WaypointMap::update keeps InitialCameraPosition and Player_1_Start on, up to the
// first missing, at least one start counted), multiplayer from two start spots on, its tech buildings and preview
// supplies in object order, its extent (0,0 to the playable cells times MAP_XY_FACTOR 10) and its WorldInfo mapName as
// the name tag.
export namespace generalszh::session::setup
{
// What an object type is to the map preview (ThingTemplate::isKindOf; a waypoint is neither).
enum class MapPreviewKind : std::uint8_t
{
	None,
	TechBuilding,   // KINDOF_TECH_BUILDING
	SupplyOnPreview, // KINDOF_SUPPLY_SOURCE_ON_PREVIEW
};

inline content::MapMetaData DescribeMap(const engine::level::Level &level, std::uint32_t crc,
	const std::function<MapPreviewKind(std::string_view)> &kindOf)
{
	content::MapMetaData map;
	map.crc = crc;
	// ParseObjectDataChunk: (*m_waypoints)[name] = loc (a name met again: the later one).
	const auto waypoint = [&level](std::string_view name) -> std::optional<Engine::Math::FixedVector3> {
		std::optional<Engine::Math::FixedVector3> found;
		for (const engine::level::Marker &marker : level.markers)
			if (marker.name == name)
				found = marker.position;
		return found;
	};
	if (const auto camera = waypoint("InitialCameraPosition"))
	{
		map.initialCamera = *camera;
		map.hasInitialCamera = true;
	}
	for (int player = 1; player <= 8; ++player)
	{
		const auto start = waypoint("Player_" + std::to_string(player) + "_Start");
		if (!start)
			break;
		map.starts.push_back(*start);
	}
	map.players = std::max(1, static_cast<int>(map.starts.size()));
	map.multiplayer = map.players >= 2;
	// The objects (the waypoints are markers already): tech buildings first, else preview supplies.
	for (const engine::level::Placement &placement : level.placements)
	{
		const MapPreviewKind kind = kindOf ? kindOf(placement.type) : MapPreviewKind::None;
		if (kind == MapPreviewKind::TechBuilding)
			map.techs.push_back(placement.position);
		else if (kind == MapPreviewKind::SupplyOnPreview)
			map.supplies.push_back(placement.position);
	}
	// getExtent: m_mapDX/DY (the cells less the border on each side) times MAP_XY_FACTOR.
	const std::int64_t width = static_cast<std::int64_t>(level.terrain.width) - 2 * static_cast<std::int64_t>(level.terrain.border);
	const std::int64_t height = static_cast<std::int64_t>(level.terrain.height) - 2 * static_cast<std::int64_t>(level.terrain.border);
	map.extentMin = {};
	map.extentMax = {Engine::Math::Fixed::FromInt(width * 10), Engine::Math::Fixed::FromInt(height * 10), Engine::Math::Fixed{}};
	map.nameLookupTag = level.properties.Get<std::string>("mapName").value_or("");
	return map;
}
}
