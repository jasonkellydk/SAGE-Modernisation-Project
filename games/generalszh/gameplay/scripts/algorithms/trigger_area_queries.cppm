export module games.generalszh.gameplay.scripts.algorithms.trigger_area_queries;
import std;
import engine.gameplay.rts.harvesting.components.resource_store;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.ai.algorithms.guards;
import games.generalszh.gameplay.teams.algorithms.team_states;
export import engine.gameplay.common.areas.resources.trigger_areas;
export import engine.gameplay.common.areas.components.area_presence;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.orders.algorithms.wander_orders;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.common.identity.components.definition_ref;
import games.generalszh.gameplay.scripts.algorithms.object_counting;
import games.generalszh.gameplay.scripts.resources.script_records;
import games.generalszh.content.objects.kind_of;
import engine.gameplay.rts.combat.components.aggression;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.off_map;
import engine.ecs.query.query;

// The scripts' questions about trigger areas (ScriptConditions' area conditions over Object::isInside / didEnter /
// didExit and Team::someInsideSomeOutside / allInside / didAllEnter / didPartialEnter / didAllExit / didPartialExit),
// and the skirmish perimeter names (ScriptEngine::getQualifiedTriggerAreaByName).
export namespace generalszh::gameplay
{
enum class AreaTest : std::uint8_t
{
	NamedInside,
	NamedOutside,
	NamedEntered,
	NamedExited,
	TeamInsidePartially,
	TeamInsideEntirely,
	TeamOutsideEntirely,
	TeamEnteredEntirely,
	TeamEnteredPartially,
	TeamExitedEntirely,
	TeamExitedPartially,
};

// getQualifiedTriggerAreaByName: "[Skirmish]MyInnerPerimeter" / "...MyOuterPerimeter" name the calling player's
// InnerPerimeter<n> / OuterPerimeter<n> (its start index + 1), the "Enemy" ones its enemy's (-1 with none); any other
// name as it is. `ownStart` / `enemyStart`: 0-based start indices, if known.
inline std::uint32_t QualifiedArea(const engine::gameplay::TriggerAreas &areas, const std::string &name, std::optional<std::int64_t> ownStart,
	std::optional<std::int64_t> enemyStart)
{
	std::string resolved = name;
	if (name == "[Skirmish]MyInnerPerimeter" || name == "[Skirmish]MyOuterPerimeter")
	{
		if (!ownStart)
			return engine::gameplay::TriggerAreas::None;
		resolved = (name == "[Skirmish]MyInnerPerimeter" ? "InnerPerimeter" : "OuterPerimeter") + std::to_string(*ownStart + 1);
	}
	else if (name == "[Skirmish]EnemyInnerPerimeter" || name == "[Skirmish]EnemyOuterPerimeter")
		resolved = (name == "[Skirmish]EnemyInnerPerimeter" ? "InnerPerimeter" : "OuterPerimeter") + std::to_string(enemyStart ? *enemyStart + 1 : -1);
	return areas.Find(resolved);
}

namespace trigger_area_detail
{
// locoSetMatches: the surfaces parameter (1 ground, 2 air) against what it moves over (no locomotor: the ground).
inline bool Considered(const GameWorld &game, ecs::Entity entity, std::int64_t surfaces)
{
	const std::uint32_t wanted = static_cast<std::uint32_t>((surfaces & 0x01) | ((surfaces & 0x02) << 2));
	const auto *motion = game.world.Get<engine::gameplay::Locomotion>(entity);
	const std::uint32_t moves = motion != nullptr ? motion->locomotor.surfaces : 1u;
	return (wanted & moves) != 0;
}
}

// What a player has in an area (the skirmish area conditions), over every member of the player's teams inside it:
//   Types (PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA): of the type or type list, living and not inert (a dead
//   crate still counts, as the original's);
//   Kind (..._UNIT_KIND_IN_TRIGGER_AREA): of the KindOf `kind` (its index in the original's enum), living, not inert;
//   Any (SKIRMISH_PLAYER_HAS_UNITS_IN_AREA): living, not inert, not a projectile;
//   Value (SKIRMISH_VALUE_IN_AREA): the build cost summed of the living that are not inert.
// The original caches the answer until a team enters or exits or objects come or go; it is worked out afresh here
// (the same answer, and not stale when something dies in place).
enum class AreaCount : std::uint8_t
{
	Types,
	Kind,
	Any,
	Value,
};

inline std::int64_t CountInArea(const GameWorld &game, AreaCount what, std::uint32_t player, std::uint32_t area, const std::string &types = {},
	std::int64_t kind = -1)
{
	namespace gp = engine::gameplay;
	if (area == gp::TriggerAreas::None)
		return 0;
	std::vector<std::string> typeNames;
	if (what == AreaCount::Types)
		if (const auto *records = game.world.FindResource<ScriptRecords>())
			typeNames = ObjectTypesFrom(game, *records, types);
	const std::string_view kindName =
		kind >= 0 && static_cast<std::size_t>(kind) < content::KindOfNames.size() ? content::KindOfNames[static_cast<std::size_t>(kind)] : std::string_view{};
	std::int64_t total = 0;
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
	{
		if (game.roster.TeamAt(team).owner != player)
			continue;
		for (const ecs::Entity member : game.roster.TeamAt(team).members)
		{
			const auto *in = game.world.IsAlive(member) ? game.world.Get<gp::AreaPresence>(member) : nullptr;
			const auto *ref = in != nullptr ? game.world.Get<gp::DefinitionRef>(member) : nullptr;
			if (ref == nullptr || !in->Inside(area))
				continue;
			const auto &definition = game.templates.DefinitionAt(ref->index);
			const bool living = !EffectivelyDead(game, member) && !definition.Is("INERT");
			switch (what)
			{
			case AreaCount::Types:
				if (std::ranges::find(typeNames, definition.name) != typeNames.end() && (living || definition.Is("CRATE")))
					++total;
				break;
			case AreaCount::Kind:
				if (!kindName.empty() && definition.Is(kindName) && living)
					++total;
				break;
			case AreaCount::Any:
				if (living && !definition.Is("PROJECTILE"))
					++total;
				break;
			case AreaCount::Value:
				if (living)
					total += definition.buildCost;
				break;
			}
		}
	}
	return total;
}

// AIGroup::groupAttackArea / groupGuardArea (TEAM_ATTACK_AREA, NAMED_ATTACK_AREA, TEAM_GUARD_AREA): each unit that
// fights for itself (has an AI: Aggression) is ordered to
//   attack the area (AIAttackAreaState): hunt the enemies inside it, its first look within a second (a random delay:
//   GameLogicRandomValue(0, ENEMY_SCAN_RATE)); done (idle) once none are left;
//   guard the area (aiGuardArea, GUARDMODE_NORMAL: the AIGuardMachine, GuardArea).
inline void OrderAreaAttack(GameWorld &game, std::span<const ecs::Entity> units, std::uint32_t area)
{
	namespace gp = engine::gameplay;
	if (area == gp::TriggerAreas::None)
		return;
	for (const ecs::Entity unit : units)
	{
		auto *aggression = game.world.IsAlive(unit) ? game.world.Get<gp::Aggression>(unit) : nullptr;
		if (aggression == nullptr)
			continue;
		Commanded(game, unit);
		aggression->stance = gp::Stance::Hunt;
		aggression->area = area;
		aggression->nextScan = game.tick + static_cast<std::uint64_t>(Engine::Math::UniformInt(game.random, 0, game.step.TicksPerSecond()));
		if (auto *target = game.world.Get<gp::AttackTarget>(unit))
			*target = {};
	}
}

inline void OrderAreaGuard(GameWorld &game, std::span<const ecs::Entity> units, std::uint32_t area)
{
	for (const ecs::Entity unit : units)
		if (game.world.IsAlive(unit) && HasAi(game, unit))
			GuardArea(game, unit, area);
}

// The closest thing of the type (or of a type in the list) inside the area to `from`, measured from its centre in 2D, on
// the map as `offMap` says (PartitionFilterThing + PartitionFilterPolygonTrigger + PartitionFilterSameMapStatus through
// getClosestObject, as doMoveUnitTowardsNearest and doMoveTeamTowardsNearest ask it). One chunked pass over the things;
// a chunk on the other side of the map edge is skipped whole.
inline std::optional<Engine::Math::FixedVector2> NearestInArea(
	const GameWorld &game, Engine::Math::FixedVector2 from, bool offMap, const std::string &type, const engine::gameplay::TriggerArea &polygon)
{
	namespace gp = engine::gameplay;
	std::vector<std::string> types;
	if (game.templates.Content().objects.Find(type) != nullptr)
		types = {type};
	else if (const auto *records = game.world.FindResource<ScriptRecords>())
		types = ObjectTypesFrom(game, *records, type);
	if (types.empty())
		return std::nullopt;
	const auto whole = [](Engine::Math::Fixed value) {
		const std::int64_t raw = value.Raw();
		return static_cast<std::int32_t>(raw >= 0 ? raw >> 16 : -((-raw) >> 16));
	};
	std::optional<Engine::Math::FixedVector2> best;
	Engine::Math::Fixed bestDistance;
	ecs::Query<ecs::Read<gp::DefinitionRef>, ecs::Read<gp::Transform>, ecs::Optional<gp::OffMap>> query(const_cast<ecs::World &>(game.world));
	query.ForEachChunk([&](auto chunk) {
		const auto refs = chunk.template Get<gp::DefinitionRef>();
		const auto transforms = chunk.template Get<gp::Transform>();
		if (chunk.template Get<gp::OffMap>().empty() == offMap)
			return;
		for (std::size_t row = 0; row < refs.size(); ++row)
		{
			const auto &position = transforms[row].position;
			if (std::ranges::find(types, game.templates.DefinitionAt(refs[row].index).name) == types.end() ||
				!polygon.Contains(whole(position.x), whole(position.y)))
				continue;
			const Engine::Math::Fixed distance = Engine::Math::DistanceSquared(position.XY(), from);
			if (!best || distance < bestDistance)
			{
				best = position.XY();
				bestDistance = distance;
			}
		}
	});
	return best;
}

// doMoveTeamTowardsNearest (TEAM_MOVE_TOWARDS_NEAREST_OBJECT_TYPE): the thing of the type (or of a type in the list)
// inside the area nearest the team's first member (getEstimateTeamPosition), on the map as that member is; each member
// with an AI moves to it on its normal locomotors. (The original stopped ordering at the first member without an AI: a
// quirk fixed.)
inline void TeamMoveTowardsNearest(GameWorld &game, const std::string &team, const std::string &type, std::uint32_t area)
{
	namespace gp = engine::gameplay;
	const auto *areas = game.world.FindResource<gp::TriggerAreas>();
	const auto index = ResolveTeam(game, team);
	if (areas == nullptr || area >= areas->areas.size() || !index || game.roster.TeamAt(*index).members.empty())
		return;
	const ecs::Entity first = game.roster.TeamAt(*index).members.front();
	const auto *from = game.world.IsAlive(first) ? game.world.Get<gp::Transform>(first) : nullptr;
	if (from == nullptr)
		return;
	const auto best = NearestInArea(game, from->position.XY(), game.world.Get<gp::OffMap>(first) != nullptr, type, areas->areas[area]);
	if (!best)
		return;
	ForTeam(game, "#" + std::to_string(*index), [&](ecs::Entity member) {
		if (game.world.Get<gp::MoveOrder>(member) == nullptr)
			return;
		NormalLocomotors(game, member);
		OrderMove(game, member, *best);
	});
}

// doMoveUnitTowardsNearest (UNIT_MOVE_TOWARDS_NEAREST_OBJECT_TYPE): a unit with an AI moves, on its normal locomotors, to
// the thing of the type (or of a type in the list) inside the area nearest to it, on the map as it is. Nothing found:
// nothing done. (With a list and nothing found the original still ordered a move to no object, clearing the unit's
// orders into a move without a goal: a quirk fixed, as its single-type case already returned.)
inline void UnitMoveTowardsNearest(GameWorld &game, ecs::Entity unit, const std::string &type, std::uint32_t area)
{
	namespace gp = engine::gameplay;
	const auto *areas = game.world.FindResource<gp::TriggerAreas>();
	if (areas == nullptr || area >= areas->areas.size() || !game.world.IsAlive(unit) || !HasAi(game, unit))
		return;
	const auto *from = game.world.Get<gp::Transform>(unit);
	if (from == nullptr)
		return;
	const auto best = NearestInArea(game, from->position.XY(), game.world.Get<gp::OffMap>(unit) != nullptr, type, areas->areas[area]);
	if (!best)
		return;
	NormalLocomotors(game, unit);
	if (game.world.Get<gp::MoveOrder>(unit) != nullptr)
		OrderMove(game, unit, *best);
}

// evaluateSkirmishPlayerTechBuildingWithinDistancePerimeter (SKIRMISH_TECH_BUILDING_WITHIN_DISTANCE): a tech building on
// the map neither the player's nor an ally's within `distance` more than the area's radius of its middle (from its
// centre, in 2D). The original kept its first answer for good; it is asked afresh (a captured one counts no more).
inline bool TechBuildingNear(const GameWorld &game, std::uint32_t player, std::uint32_t area, Engine::Math::Fixed distance)
{
	namespace gp = engine::gameplay;
	const auto *areas = game.world.FindResource<gp::TriggerAreas>();
	const auto *relationships = game.world.FindResource<gp::Relationships>();
	if (areas == nullptr || relationships == nullptr || area >= areas->areas.size())
		return false;
	const gp::TriggerArea &polygon = areas->areas[area];
	const auto twice = polygon.CenterTimesTwo();
	const Engine::Math::FixedVector2 center{Engine::Math::Fixed::FromRaw(twice[0] << 15), Engine::Math::Fixed::FromRaw(twice[1] << 15)};
	const Engine::Math::Fixed reach =
		Engine::Math::Sqrt(Engine::Math::Fixed::FromInt(polygon.RadiusSquaredTimesFour())) / Engine::Math::Fixed::FromInt(2) + distance;
	bool found = false;
	ecs::Query<ecs::Read<gp::DefinitionRef>, ecs::Read<gp::Owner>, ecs::Read<gp::Transform>> query(const_cast<ecs::World &>(game.world));
	query.ForEachChunk([&](auto chunk) {
		if (found)
			return;
		const auto refs = chunk.template Get<gp::DefinitionRef>();
		const auto owners = chunk.template Get<gp::Owner>();
		const auto transforms = chunk.template Get<gp::Transform>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < refs.size() && !found; ++row)
		{
			if (owners[row].player == player || relationships->Allies(player, owners[row].player) ||
				!game.templates.DefinitionAt(refs[row].index).Is("TECH_BUILDING") || game.world.Get<gp::OffMap>(entities[row]) != nullptr)
				continue;
			found = Engine::Math::DistanceSquared(transforms[row].position.XY(), center) <= reach * reach;
		}
	});
	return found;
}

// evaluateSkirmishSuppliesWithinDistancePerimeter (SKIRMISH_SUPPLIES_VALUE_WITHIN_DISTANCE): of the structures on the
// map within `distance` more than the area's radius of its middle (from their centre, in 2D) that the player counts
// neutral, or its own (PartitionFilterPlayerAffiliation ALLOW_NEUTRAL), the warehouses' (SupplyWarehouseDockUpdate)
// most valuable store (the player's box value, getSupplyBoxValue: ValuePerSupplyBox, times the boxes it holds) above
// `value`.
inline bool SuppliesNear(const GameWorld &game, std::uint32_t player, std::uint32_t area, Engine::Math::Fixed distance, Engine::Math::Fixed value)
{
	namespace gp = engine::gameplay;
	const auto *areas = game.world.FindResource<gp::TriggerAreas>();
	const auto *relationships = game.world.FindResource<gp::Relationships>();
	if (areas == nullptr || relationships == nullptr || area >= areas->areas.size())
		return false;
	const gp::TriggerArea &polygon = areas->areas[area];
	const auto twice = polygon.CenterTimesTwo();
	const Engine::Math::FixedVector2 center{Engine::Math::Fixed::FromRaw(twice[0] << 15), Engine::Math::Fixed::FromRaw(twice[1] << 15)};
	const Engine::Math::Fixed reach =
		Engine::Math::Sqrt(Engine::Math::Fixed::FromInt(polygon.RadiusSquaredTimesFour())) / Engine::Math::Fixed::FromInt(2) + distance;
	Engine::Math::Fixed best{};
	ecs::Query<ecs::Read<gp::ResourceStore>, ecs::Read<gp::DefinitionRef>, ecs::Read<gp::Owner>, ecs::Read<gp::Transform>> query(
		const_cast<ecs::World &>(game.world));
	query.ForEachChunk([&](auto chunk) {
		const auto stores = chunk.template Get<gp::ResourceStore>();
		const auto refs = chunk.template Get<gp::DefinitionRef>();
		const auto owners = chunk.template Get<gp::Owner>();
		const auto transforms = chunk.template Get<gp::Transform>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < stores.size(); ++row)
		{
			const std::uint32_t other = owners[row].player;
			const bool neutral = other != player && !relationships->Allies(player, other) && !relationships->Enemies(player, other);
			if ((!neutral && other != player) || !game.templates.DefinitionAt(refs[row].index).Is("STRUCTURE") ||
				game.world.Get<gp::OffMap>(entities[row]) != nullptr ||
				Engine::Math::DistanceSquared(transforms[row].position.XY(), center) > reach * reach)
				continue;
			const Engine::Math::Fixed stored = Engine::Math::Fixed::FromInt(game.templates.harvest.valuePerBox) * Engine::Math::Fixed::FromInt(stores[row].boxes);
			best = std::max(best, stored);
		}
	});
	return best > value;
}

inline bool TestArea(const GameWorld &game, AreaTest test, const std::string &subject, std::uint32_t area, std::int64_t surfaces, std::uint64_t now)
{
	namespace gp = engine::gameplay;
	using trigger_area_detail::Considered;
	if (area == gp::TriggerAreas::None)
		return false;
	const auto presence = [&](ecs::Entity entity) { return game.world.IsAlive(entity) ? game.world.Get<gp::AreaPresence>(entity) : nullptr; };
	switch (test)
	{
	case AreaTest::NamedInside:
	case AreaTest::NamedOutside:
	case AreaTest::NamedEntered:
	case AreaTest::NamedExited: {
		const ecs::Entity unit = game.names.Find(subject);
		if (!game.world.IsAlive(unit))
			return test == AreaTest::NamedOutside; // evaluateNamedOutsideArea: !inside
		const gp::AreaPresence *in = presence(unit); // none: inert or a projectile
		if (test == AreaTest::NamedInside)
			return in != nullptr && in->Inside(area);
		if (test == AreaTest::NamedOutside)
			return in == nullptr || !in->Inside(area);
		if (test == AreaTest::NamedEntered)
			return in != nullptr && in->Entered(area, now);
		return in != nullptr && in->Exited(area, now);
	}
	default:
		break;
	}
	const auto team = ResolveTeam(game, subject);
	if (!team)
		return false;
	// The members counted: the living of the surfaces asked, not inert.
	std::vector<const gp::AreaPresence *> members;
	bool anyChanged = false; // the team's m_enteredOrExited
	for (const ecs::Entity member : game.roster.TeamAt(*team).members)
	{
		const gp::AreaPresence *in = presence(member);
		if (in == nullptr)
			continue;
		anyChanged = anyChanged || in->Recent(now);
		if (!Considered(game, member, surfaces) || EffectivelyDead(game, member))
			continue;
		members.push_back(in);
	}
	const auto count = [&](auto &&predicate) { return std::ranges::count_if(members, predicate); };
	const std::size_t inside = static_cast<std::size_t>(count([&](const gp::AreaPresence *in) { return in->Inside(area); }));
	const bool allInside = !members.empty() && inside == members.size();
	const bool someInSomeOut = inside > 0 && inside < members.size();
	switch (test)
	{
	case AreaTest::TeamInsidePartially:
		return someInSomeOut || allInside;
	case AreaTest::TeamInsideEntirely:
		return allInside;
	case AreaTest::TeamOutsideEntirely:
		return !(someInSomeOut || allInside);
	case AreaTest::TeamEnteredEntirely:
		// didAllEnter: someone entered and no one is outside.
		return anyChanged && count([&](const gp::AreaPresence *in) { return in->Entered(area, now); }) > 0 &&
			count([&](const gp::AreaPresence *in) { return !in->Entered(area, now) && !in->Inside(area); }) == 0;
	case AreaTest::TeamEnteredPartially:
		return anyChanged && count([&](const gp::AreaPresence *in) { return in->Entered(area, now); }) > 0;
	case AreaTest::TeamExitedEntirely:
		return anyChanged && count([&](const gp::AreaPresence *in) { return in->Exited(area, now); }) > 0 &&
			count([&](const gp::AreaPresence *in) { return !in->Exited(area, now) && in->Inside(area); }) == 0;
	case AreaTest::TeamExitedPartially:
		return anyChanged && count([&](const gp::AreaPresence *in) { return in->Exited(area, now); }) > 0;
	default:
		return false;
	}
}
}
