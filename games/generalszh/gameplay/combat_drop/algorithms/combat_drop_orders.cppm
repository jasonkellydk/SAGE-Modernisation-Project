export module games.generalszh.gameplay.combat_drop.algorithms.combat_drop_orders;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.combat_drop.components.combat_drop;
import games.generalszh.gameplay.objects.resources.object_templates;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.powers.algorithms.special_power_launch;
import games.generalszh.gameplay.containment.components.scripted_evacuation;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.algorithms.find_position;
import engine.gameplay.common.spatial.resources.spatial_index;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.health.components.inactive_body;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.rts.navigation.components.navigation;
import engine.gameplay.rts.navigation.resources.navigation_grid;
import Engine.Core.Math.FixedRandom;

// A combat drop ordered (MSG_COMBATDROP_AT_OBJECT / AT_LOCATION -> AIGroup::groupCombatDrop -> aiCombatDrop, the
// Chinook's ChinookAIUpdate::privateCombatDrop), between ticks:
//   CombatDropTarget: ActionManager::canEnterObject(COMBATDROP_INTO): not itself, not dead, not fogged or shrouded to a
//     human player's order, neither under construction, not being sold, neither ignored in the GUI (nor a mob nexus
//     dropping), not subdued, not dropping from a structure or something immobile; an airfield never (an aircraft
//     only enters its own airfield for a parking space, which a Chinook never has); something with room for riders,
//     not a healing one while the Chinook is unhurt, and never a faction structure. Neither room nor whose it is count.
//   OrderCombatDrop: a player's drop at something it may not drop into does nothing. At a spot, it drops at the
//     nearest legal place around it (findPositionAround from a random angle out to a hundred times its bounding
//     radius: no cliff, open pathfinding ground, no water, nothing within 5 of it; none found: the spot itself).
//     Landed, it takes off first; aloft it heads there at once (ChinookMoveToBldgState::onEnter). Only a Chinook drops:
//     the original's other transports hit a debug assertion and throw everyone out where they stand (the base
//     privateCombatDrop), a quirk left out. While a drop is under way the original keeps any new order for after it:
//     here it is dropped (not yet carried over).
//   EnterMoveToCombatDrop (ChinookMoveToBldgState::onEnter): over a building it hovers at least MinDropHeight above
//     its top (never lower than its locomotor's height); its height wanted above the ground under the goal is where
//     it must end up (within 3); it flies for the building, or the object, or the spot.
export namespace generalszh::gameplay
{
namespace combat_drop_detail
{
namespace gp = engine::gameplay;

inline const content::ObjectDefinition *DefinitionOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *ref = game.world.Get<gp::DefinitionRef>(entity);
	return ref != nullptr ? &game.templates.DefinitionAt(ref->index) : nullptr;
}

inline bool EffectivelyDead(const GameWorld &game, ecs::Entity entity)
{
	if (game.world.Get<gp::Dying>(entity) != nullptr || game.world.Get<gp::InactiveBody>(entity) != nullptr)
		return true;
	const auto *health = game.world.Get<gp::Health>(entity);
	return health != nullptr && IsDead(*health);
}

// Geometry::getMaxHeightAbovePosition: a sphere's radius, else its height.
inline Engine::Math::Fixed MaxHeightAbove(const content::ObjectDefinition &object)
{
	return object.geometry.shape == content::GeometryShape::Sphere ? object.geometry.majorRadius : object.geometry.height;
}

inline bool FactionStructure(const content::ObjectDefinition &object)
{
	for (const char *kind : {"FS_FACTORY", "FS_BASE_DEFENSE", "FS_TECHNOLOGY", "FS_SUPPLY_DROPZONE", "FS_SUPERWEAPON", "FS_BLACK_MARKET",
			 "FS_SUPPLY_CENTER", "FS_STRATEGY_CENTER", "FS_FAKE", "FS_INTERNET_CENTER", "FS_ADVANCED_TECH", "FS_BARRACKS", "FS_WARFACTORY", "FS_AIRFIELD"})
		if (object.Is(kind))
			return true;
	return false;
}

inline bool HasModule(const content::ObjectDefinition &object, std::string_view type)
{
	for (const content::ModuleEntry &module : object.modules)
		if (module.type == type)
			return true;
	return false;
}
}

// PartitionManager::tryPosition with no options: not a cliff, open pathfinding ground, no water over it, and nothing
// within 5 of it.
inline auto SpotLegal(GameWorld &game)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	auto &world = game.world;
	const auto &spatial = world.Resource<gp::SpatialIndex>();
	const auto &grid = world.Resource<gp::NavigationGrid>();
	return [&game, &spatial, &grid](Engine::Math::FixedVector2 point) {
		const auto cellX = static_cast<std::int32_t>((point.x / Fixed::FromInt(gp::PathfindCellSize)).Floor());
		const auto cellY = static_cast<std::int32_t>((point.y / Fixed::FromInt(gp::PathfindCellSize)).Floor());
		if (grid.Width() > 0)
		{
			if (!grid.Contains(cellX, cellY))
				return false;
			const gp::PathfindCellType type = grid.Type(cellX, cellY);
			if (type == gp::PathfindCellType::Cliff || type == gp::PathfindCellType::Impassable)
				return false;
		}
		Fixed water;
		if (game.ground.Water(point, water) && water > game.ground.At(point))
			return false;
		bool free = true;
		const Fixed reach = Fixed::FromInt(5);
		spatial.ForEachWithin(point, reach, [&](const gp::SpatialEntry &entry) {
			if (!free)
				return;
			const Fixed apart = reach + entry.radius;
			if (Engine::Math::DistanceSquared(point, entry.position.XY()) < apart * apart)
				free = false;
		});
		return free;
	};
}

inline bool CombatDropTarget(GameWorld &game, ecs::Entity chinook, ecs::Entity target, bool fromScript = false)
{
	namespace gp = engine::gameplay;
	using namespace combat_drop_detail;
	auto &world = game.world;
	if (chinook == target || !world.IsAlive(chinook) || !world.IsAlive(target))
		return false;
	const content::ObjectDefinition *self = DefinitionOf(game, chinook);
	const content::ObjectDefinition *into = DefinitionOf(game, target);
	if (self == nullptr || into == nullptr || EffectivelyDead(game, target) || ShroudedForAction(game, chinook, target, fromScript))
		return false;
	if (world.Has<gp::UnderConstruction>(chinook) || world.Has<gp::UnderConstruction>(target) || world.Has<gp::Sale>(target))
		return false;
	if (self->Is("IGNORED_IN_GUI") || self->Is("MOB_NEXUS") || into->Is("IGNORED_IN_GUI"))
		return false;
	if (const auto *off = world.Get<gp::Disabled>(target); off != nullptr && (off->mask & gp::disabled_type::Subdued) != 0)
		return false;
	if (self->Is("STRUCTURE") || self->Is("IMMOBILE"))
		return false;
	if (self->Is("AIRCRAFT") && into->Is("FS_AIRFIELD"))
		return false;
	if (!world.Has<gp::Transport>(target))
		return false;
	if (HasModule(*into, "HealContain"))
		if (const auto *health = world.Get<gp::Health>(chinook); health != nullptr && health->current == health->maximum)
			return false;
	return !FactionStructure(*into);
}

// ChinookMoveToBldgState::onEnter on the transport's own components; `structure`: the goal object a live building
// `structureTop` tall (getMaxHeightAbovePosition), else the goal is `goal`.
inline void EnterMoveToCombatDrop(CombatDrop &drop, engine::gameplay::Locomotion &motion, engine::gameplay::MoveOrder &order, engine::gameplay::Route *route,
	const engine::gameplay::GroundHeight &ground, bool structure, Engine::Math::Fixed structureTop, Engine::Math::Fixed minDropHeight)
{
	drop.oldHeight = motion.locomotor.preferredHeight;
	drop.newHeight = drop.oldHeight;
	if (structure)
		drop.newHeight = std::max(structureTop + minDropHeight, drop.oldHeight);
	motion.locomotor.preferredHeight = drop.newHeight;
	drop.destZ = ground.At(drop.goal) + drop.newHeight;
	drop.stage = CombatDropStage::Moving;
	order = engine::gameplay::MoveToPoint(drop.goal);
	if (route != nullptr)
		route->planned = false;
}

inline void OrderCombatDrop(GameWorld &game, ecs::Entity unit, ecs::Entity target, Engine::Math::FixedVector2 position, bool fromPlayer)
{
	namespace gp = engine::gameplay;
	using namespace combat_drop_detail;
	using Engine::Math::Fixed;
	auto &world = game.world;
	const auto *ref = world.Get<gp::DefinitionRef>(unit);
	const ObjectTemplates::CombatDropConfig *config = ref != nullptr ? game.templates.CombatDropOf(ref->index) : nullptr;
	if (config == nullptr || EffectivelyDead(game, unit) || world.Get<gp::MoveOrder>(unit) == nullptr || world.Get<gp::Locomotion>(unit) == nullptr)
		return;
	if (const CombatDrop *under = world.Get<CombatDrop>(unit); under != nullptr && under->stage == CombatDropStage::Dropping)
		return;
	if (world.IsAlive(target) && fromPlayer && !CombatDropTarget(game, unit, target))
		return;
	if (!world.IsAlive(target))
		target = {};
	Engine::Math::FixedVector2 goal = position;
	if (target == ecs::Entity{})
	{
		// findPositionAround: a random start angle; out to a hundred times its bounding circle.
		const Engine::Math::TurnAngle start{static_cast<std::uint32_t>(Engine::Math::UniformInt(game.random, 0, 0xFFFFFFFFll))};
		const auto &spatial = world.Resource<gp::SpatialIndex>();
		const gp::SpatialEntry *self = spatial.Find(unit);
		const Fixed radius = self != nullptr ? self->radius : Fixed{};
		const auto legal = SpotLegal(game);
		goal = gp::FindPositionAround(position, Fixed{}, radius * Fixed::FromInt(100), start, legal).value_or(position);
	}
	else
		goal = world.Get<gp::Transform>(target)->position.XY();
	// setGoalPositionClipped: inside the map.
	const auto [low, high] = game.ground.Extent();
	goal.x = std::clamp(goal.x, low.x, high.x);
	goal.y = std::clamp(goal.y, low.y, high.y);
	if (fromPlayer)
		Commanded(game, unit);
	else
		AiCommanded(game, unit);
	if (world.Has<ScriptedEvacuation>(unit))
		world.Remove<ScriptedEvacuation>(unit);
	if (!world.Has<CombatDrop>(unit))
		world.Add<CombatDrop>(unit);
	CombatDrop &drop = *world.Get<CombatDrop>(unit);
	drop = CombatDrop{};
	drop.target = target;
	drop.goal = goal;
	// Landed or on its way up or down: it takes off first (TAKING_OFF, the drop pending).
	gp::Transport *transport = world.Get<gp::Transport>(unit);
	if (transport != nullptr && (transport->landing || transport->takingOff))
	{
		transport->landRequested = false;
		drop.stage = CombatDropStage::TakingOff;
		return;
	}
	const content::ObjectDefinition *building = target != ecs::Entity{} ? DefinitionOf(game, target) : nullptr;
	const bool structure = building != nullptr && building->Is("STRUCTURE") && !EffectivelyDead(game, target);
	EnterMoveToCombatDrop(drop, *world.Get<gp::Locomotion>(unit), *world.Get<gp::MoveOrder>(unit), world.Get<gp::Route>(unit), game.ground, structure,
		structure ? MaxHeightAbove(*building) : Fixed{}, config->content.minDropHeight);
}
}
