export module games.generalszh.gameplay.powers.algorithms.power_targeting;
import std;

export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.rts.vision.resources.shroud_map;

// Where a player may aim a special power (ActionManager::canDoSpecialPowerAtLocation, GeneralsMD/Code/GameEngine/Source/
// Common/RTS/ActionManager.cpp), by its SpecialPowerType, its source's readiness aside: the drops that land things not
// on water; the damaging ones (and the scans of an area) not where the ground is black to the source's player (fogged is
// fine); spy satellites, radar scans, spy drones and Helix napalm anywhere on the map; the Baikonur rocket anywhere; the
// sneak attack not in the shroud (the retail rule); every other power (those needing an object, or none) nowhere.
export namespace generalszh::gameplay
{
namespace power_targeting_detail
{
inline bool Any(std::string_view type, std::initializer_list<std::string_view> names) noexcept
{
	return std::ranges::find(names, type) != names.end();
}
}

inline bool CanDoSpecialPowerAtLocation(std::string_view type, std::uint32_t player, Engine::Math::FixedVector2 at, const engine::gameplay::GroundHeight &ground,
	const engine::gameplay::ShroudMap &shroud) noexcept
{
	using power_targeting_detail::Any;
	// TheTerrainLogic->isUnderwater: water above the ground there.
	if (Any(type, {"SPECIAL_PARADROP_AMERICA", "INFA_SPECIAL_PARADROP_AMERICA", "SPECIAL_CRATE_DROP", "SPECIAL_TANK_PARADROP"}))
		if (Engine::Math::Fixed water; ground.Water(at, water) && water > ground.At(at))
			return false;
	const auto clear = [&] { return shroud.StatusAt(player, at.x, at.y) != engine::gameplay::CellShroud::Shrouded; };
	if (Any(type, {"SPECIAL_DAISY_CUTTER", "AIRF_SPECIAL_DAISY_CUTTER", "SPECIAL_PARADROP_AMERICA", "SPECIAL_TANK_PARADROP",
			"INFA_SPECIAL_PARADROP_AMERICA", "SPECIAL_CARPET_BOMB", "SPECIAL_CHINA_CARPET_BOMB", "SPECIAL_LEAFLET_DROP", "EARLY_SPECIAL_LEAFLET_DROP",
			"EARLY_SPECIAL_CHINA_CARPET_BOMB", "AIRF_SPECIAL_CARPET_BOMB", "SUPR_SPECIAL_CRUISE_MISSILE", "SPECIAL_CLUSTER_MINES",
			"NUKE_SPECIAL_CLUSTER_MINES", "SPECIAL_EMP_PULSE", "SPECIAL_CRATE_DROP", "SPECIAL_NAPALM_STRIKE", "SPECIAL_BLACK_MARKET_NUKE",
			"SPECIAL_ANTHRAX_BOMB", "SPECIAL_TERROR_CELL", "SPECIAL_AMBUSH", "SPECIAL_NEUTRON_MISSILE", "NUKE_SPECIAL_NEUTRON_MISSILE",
			"SUPW_SPECIAL_NEUTRON_MISSILE", "SPECIAL_SCUD_STORM", "SPECIAL_A10_THUNDERBOLT_STRIKE", "AIRF_SPECIAL_A10_THUNDERBOLT_STRIKE",
			"SPECIAL_SPECTRE_GUNSHIP", "AIRF_SPECIAL_SPECTRE_GUNSHIP", "SPECIAL_REPAIR_VEHICLES", "EARLY_SPECIAL_REPAIR_VEHICLES",
			"SPECIAL_GPS_SCRAMBLER", "SLTH_SPECIAL_GPS_SCRAMBLER", "SPECIAL_ARTILLERY_BARRAGE", "SPECIAL_FRENZY", "EARLY_SPECIAL_FRENZY",
			"SPECIAL_PARTICLE_UPLINK_CANNON", "SUPW_SPECIAL_PARTICLE_UPLINK_CANNON", "LAZR_SPECIAL_PARTICLE_UPLINK_CANNON",
			"SPECIAL_CLEANUP_AREA", "SPECIAL_BATTLESHIP_BOMBARDMENT", "SPECIAL_SNEAK_ATTACK"}))
		return clear();
	// isPointOnMap: inside the map's extent, its edges excluded (Region3D::isInRegionNoZ).
	if (Any(type, {"SPECIAL_SPY_SATELLITE", "SPECIAL_RADAR_VAN_SCAN", "SPECIAL_SPY_DRONE", "SPECIAL_HELIX_NAPALM_BOMB"}))
	{
		const auto [low, high] = ground.Extent();
		return low.x < at.x && at.x < high.x && low.y < at.y && at.y < high.y;
	}
	return type == "SPECIAL_LAUNCH_BAIKONUR_ROCKET";
}
}
