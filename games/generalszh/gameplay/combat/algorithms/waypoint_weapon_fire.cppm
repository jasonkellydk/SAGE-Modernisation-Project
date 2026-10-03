export module games.generalszh.gameplay.combat.algorithms.waypoint_weapon_fire;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.combat.algorithms.spot_fire;
import engine.gameplay.rts.combat.algorithms.waypoint_weapon;
import engine.gameplay.rts.combat.systems.projectile_launch_system;
import engine.gameplay.rts.combat.systems.missile_waypoint_path_system;
import engine.gameplay.rts.combat.components.firing_tracker;
import engine.gameplay.rts.combat.resources.launch_layouts;
import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
import engine.gameplay.rts.veterancy.components.experience;
import engine.gameplay.common.random.resources.random_seed;

// A script firing a unit's waypoint-following weapon down a path (ScriptActions::doNamedFireWeaponFollowingWaypointPath).
export namespace generalszh::gameplay
{
// doNamedFireWeaponFollowingWaypointPath: the path's waypoint closest to the unit (TerrainLogic::getClosestWaypointOnPath,
// 2D; none: nothing), its weapon that can follow waypoints (WeaponSet::findWaypointFollowingCapableWeapon, from its last
// slot down; none: nothing) force-fired at the unit's own position (Weapon::forceFireWeapon: no range check, only when
// that weapon is ready, its clip and reload as any shot's); its guided missile, out at once, follows the path from that
// waypoint on its normal locomotor (aiFollowWaypointPath). The shot joins the tick's shots for its effects. Returns the
// missile (none when nothing flew).
inline ecs::Entity FireWeaponFollowingWaypointPath(GameWorld &game, ecs::Entity unit, std::string_view pathLabel)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (!world.IsAlive(unit))
		return {};
	auto *armament = world.Get<gp::Armament>(unit);
	const auto *transform = world.Get<gp::Transform>(unit);
	const auto *weapons = world.FindResource<gp::WeaponCatalog>();
	if (armament == nullptr || transform == nullptr || weapons == nullptr || pathLabel.empty())
		return {};
	const std::uint32_t waypoint = game.waypoints.ClosestOnPath(transform->position.XY(), pathLabel);
	if (waypoint == gp::WaypointGraph::None)
		return {};
	auto *slots = world.Get<gp::WeaponSlots>(unit);
	const auto slot = gp::WaypointFollowingSlot(slots, *armament, *weapons);
	if (!slot)
		return {};
	// That weapon fires (the set's current weapon for the shot, then back as it was).
	const std::uint8_t current = slots != nullptr ? slots->current : std::uint8_t{0};
	if (slots != nullptr && *slot != current)
	{
		gp::StoreSlot(slots->slots[current], *armament);
		slots->current = *slot;
		gp::LoadSlot(*armament, slots->slots[*slot], armament->turret);
	}
	gp::SpotFirer firer;
	firer.entity = unit;
	firer.player = world.Has<gp::Owner>(unit) ? world.Get<gp::Owner>(unit)->player : 0u;
	firer.transform = transform;
	if (const auto *definition = world.Get<gp::DefinitionRef>(unit))
		if (const auto *layouts = world.FindResource<gp::LaunchLayouts>())
			firer.layout = layouts->Of(definition->index);
	firer.armament = armament;
	firer.slots = slots;
	firer.tracker = world.Get<gp::FiringTracker>(unit);
	firer.conditions = world.Has<gp::WeaponBonusConditions>(unit) ? world.Get<gp::WeaponBonusConditions>(unit)->Effective() : 0u;
	firer.veterancy = world.Has<gp::Experience>(unit) ? world.Get<gp::Experience>(unit)->level : std::uint8_t{0};
	const std::uint64_t seed = world.Resource<gp::RandomSeed>().value ^ 0x5C21u;
	std::optional<gp::Shot> shot = gp::FireCurrentWeaponAt(firer, transform->position, *weapons, game.ground, game.tick, seed);
	if (slots != nullptr && *slot != current)
	{
		gp::StoreSlot(slots->slots[*slot], *armament);
		slots->current = current;
		gp::LoadSlot(*armament, slots->slots[current], armament->turret);
	}
	if (!shot)
		return {};
	const gp::WeaponDefinition &weapon = weapons->At(shot->weapon);
	ecs::Entity missile{};
	if (weapon.guided && !weapon.objectFlown)
	{
		// Out at once (forceFireWeapon hands its projectile back), following the path.
		gp::ProjectileLaunchSystem::MissileParts parts = gp::ProjectileLaunchSystem::PartsOf(*shot, weapon);
		const gp::MissileWaypointPath path = gp::FollowWaypointPath(parts.flight, game.waypoints, game.ground, waypoint);
		missile = world.Create();
		const auto put = [&]<typename T>(const T &value) {
			world.Add<T>(missile);
			*world.Get<T>(missile) = value;
		};
		put(parts.transform);
		put(parts.definition);
		put(parts.owner);
		put(parts.attitude);
		put(parts.flight);
		put(path);
		shot->launched = 1;
	}
	world.Resource<gp::ScriptShots>().Add(*shot);
	return missile;
}
}
