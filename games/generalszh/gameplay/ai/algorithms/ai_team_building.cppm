export module games.generalszh.gameplay.ai.algorithms.ai_team_building;
import std;
import games.generalszh.gameplay.battleplans.algorithms.battle_plan_bonuses;
import games.generalszh.gameplay.production.algorithms.build_cost;

export import games.generalszh.gameplay.ai.algorithms.ai_base_building;
import games.generalszh.gameplay.construction.components.rebuild_hole;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import engine.gameplay.common.status.components.script_status;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.object_id;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.movement.components.move_path;
import engine.gameplay.rts.harvesting.components.harvester;
import engine.gameplay.common.health.components.health;
import engine.gameplay.rts.construction.components.builder;
import games.generalszh.gameplay.construction.algorithms.building;
import engine.gameplay.rts.economy.resources.player_money;
import games.generalszh.gameplay.sciences.algorithms.general_ranks;
import engine.gameplay.rts.harvesting.components.resource_store;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.rts.navigation.definitions.pathfind_cell;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.rts.death.components.dying;

// A computer player building its teams (AIPlayer::update's team half: checkReadyTeams, checkQueuedTeams,
// doTeamBuilding; selectTeamToBuild / selectTeamToReinforce, isAGoodIdeaToBuildTeam, isPossibleToBuildTeam,
// buildSpecificAITeam, queueUnits, Team::tryToRecruit, onUnitProduced; the AISkirmishPlayer variants), once a tick after
// the tick's objects.
export namespace generalszh::gameplay
{
// How the computer players reach the scripts: whether a script exists and is for its player's difficulty, its
// evaluation delay, whether its conditions hold (for no team: evaluateConditions(script, null, player)), its actions
// run for a team or none (friend_executeAction), and the skirmish scripts' team-building flags cleared
// (ScriptEngine::clearTeamFlags).
struct AiScriptHooks
{
	std::function<bool(const std::string &)> allowed;
	std::function<std::uint32_t(const std::string &)> delaySeconds;
	std::function<bool(const std::string &)> conditions;
	std::function<void(const std::string &, std::optional<std::uint32_t>)> actions;
	std::function<void()> clearTeamFlags;
};

namespace ai_team_detail
{
namespace gp = engine::gameplay;
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;

inline constexpr std::uint32_t NoTeam = gp::Team::Own;

// TCreateUnitsInfo: the slots with a type and a most above none.
struct UnitsInfo
{
	std::string type;
	std::int32_t min{0};
	std::int32_t max{0};
};

inline const engine::level::Properties &InfoOf(const GameWorld &game, std::uint32_t team) { return game.teams.At(game.roster.PrototypeOf(team)); }

inline std::vector<UnitsInfo> UnitsOf(const engine::level::Properties &info)
{
	// The slots' keys, spelled once (no strings built per look).
	static constexpr std::array<std::string_view, 7> Types{"teamUnitType1", "teamUnitType2", "teamUnitType3", "teamUnitType4", "teamUnitType5",
		"teamUnitType6", "teamUnitType7"};
	static constexpr std::array<std::string_view, 7> Mins{"teamUnitMinCount1", "teamUnitMinCount2", "teamUnitMinCount3", "teamUnitMinCount4",
		"teamUnitMinCount5", "teamUnitMinCount6", "teamUnitMinCount7"};
	static constexpr std::array<std::string_view, 7> Maxes{"teamUnitMaxCount1", "teamUnitMaxCount2", "teamUnitMaxCount3", "teamUnitMaxCount4",
		"teamUnitMaxCount5", "teamUnitMaxCount6", "teamUnitMaxCount7"};
	std::vector<UnitsInfo> out;
	for (std::size_t slot = 0; slot < 7; ++slot)
	{
		const auto type = info.Get<std::string>(Types[slot]);
		const auto min = info.Get<std::int64_t>(Mins[slot]).value_or(0);
		const auto max = info.Get<std::int64_t>(Maxes[slot]).value_or(0);
		if (max > 0 && type)
			out.push_back({*type, static_cast<std::int32_t>(min), static_cast<std::int32_t>(max)});
	}
	return out;
}

inline std::int64_t InfoInt(const GameWorld &game, std::uint32_t team, std::string_view key) { return InfoOf(game, team).Get<std::int64_t>(key).value_or(0); }
inline bool InfoFlag(const GameWorld &game, std::uint32_t team, std::string_view key) { return InfoOf(game, team).Get<bool>(key).value_or(false); }

// m_homeLocation: the waypoint named teamHome (the last of that name), if any.
inline std::optional<FixedVector2> HomeOf(const GameWorld &game, std::uint32_t team)
{
	const auto name = InfoOf(game, team).Get<std::string>("teamHome");
	if (!name)
		return std::nullopt;
	const std::uint32_t at = game.waypoints.Find(*name);
	if (at == gp::WaypointGraph::None)
		return std::nullopt;
	return game.waypoints.Position(at).XY();
}

// The team's production priority now.
inline std::int32_t PriorityOf(const GameWorld &game, AiPlayers &ais, std::uint32_t team)
{
	const std::uint32_t prototype = game.roster.PrototypeOf(team);
	const AiTeamProduction &production = ais.Production(prototype);
	return production.priorityChanged ? production.priority : static_cast<std::int32_t>(InfoInt(game, prototype, "teamProductionPriority"));
}

// Player::getPlayerTeams: the level's teams of the player, in order.
inline std::vector<std::uint32_t> PlayerTeams(const GameWorld &game, std::uint32_t player)
{
	std::vector<std::uint32_t> out;
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
	{
		const auto &record = game.roster.TeamAt(team);
		if (record.prototype == gp::Team::Own && record.alive && record.owner == player)
			out.push_back(team);
	}
	return out;
}

inline bool Queued(const GameWorld &game, const AiPlayer &ai, std::uint32_t prototype)
{
	return std::any_of(ai.buildQueue.begin(), ai.buildQueue.end(),
		[&](const AiTeamInQueue &entry) { return entry.team != NoTeam && game.roster.PrototypeOf(entry.team) == prototype; });
}

inline const content::ObjectDefinition *KindOf(const GameWorld &game, ecs::Entity unit)
{
	const auto *ref = game.world.IsAlive(unit) ? game.world.Get<gp::DefinitionRef>(unit) : nullptr;
	return ref != nullptr ? &game.templates.DefinitionAt(ref->index) : nullptr;
}

// Object::setTeam.
inline void SetTeam(GameWorld &game, ecs::Entity unit, std::uint32_t team)
{
	auto *member = game.world.Get<gp::TeamMember>(unit);
	if (member == nullptr || member->team == team)
		return;
	game.roster.Leave(member->team, unit);
	game.roster.Join(team, unit, false);
	*member = {team};
	if (auto *owner = game.world.Get<gp::Owner>(unit))
	{
		const std::uint32_t from = owner->player;
		*owner = {game.roster.TeamAt(team).owner};
		// Player::becomingTeamMember's battle plan bonuses, and a Strategy Center's plan (onCapture).
		MoveBattlePlan(game, unit, from, owner->player);
	}
	OnCapture(game, unit);
}

// Team::hasAnyUnits: anything alive that is not a structure, projectile or mine.
inline bool TeamHasAnyUnits(const GameWorld &game, std::uint32_t team)
{
	for (const ecs::Entity member : game.roster.TeamAt(team).members)
	{
		const content::ObjectDefinition *definition = KindOf(game, member);
		if (definition == nullptr || game.world.Get<gp::Dying>(member) != nullptr)
			continue;
		if (definition->Is("STRUCTURE") || definition->Is("PROJECTILE") || definition->Is("MINE"))
			continue;
		return true;
	}
	return false;
}

// Team::isIdle: every living member with an AI is idle.
inline bool TeamIdle(const GameWorld &game, std::uint32_t team)
{
	for (const ecs::Entity member : game.roster.TeamAt(team).members)
	{
		if (!game.world.IsAlive(member) || !HasAi(game, member) || game.world.Get<gp::Dying>(member) != nullptr)
			continue;
		if (!IsIdle(game, member))
			return false;
	}
	return true;
}

// Any member with an AI (dead or not) is idle.
inline bool AnyIdle(const GameWorld &game, std::uint32_t team)
{
	for (const ecs::Entity member : game.roster.TeamAt(team).members)
		if (game.world.IsAlive(member) && HasAi(game, member) && IsIdle(game, member))
			return true;
	return false;
}

// Whether the team's production condition names a script that runs as it starts (TeamExecutesActionsOnCreate).
inline std::optional<std::string> StartActions(const GameWorld &game, std::uint32_t team, const AiScriptHooks &hooks)
{
	if (!InfoFlag(game, team, "teamExecutesActionsOnCreate"))
		return std::nullopt;
	const auto name = InfoOf(game, team).Get<std::string>("teamProductionCondition");
	if (!name || name->empty() || !hooks.allowed || !hooks.allowed(*name))
		return std::nullopt;
	return *name;
}

// WorkOrder::validateFactory.
inline void ValidateFactory(const GameWorld &game, const AiPlayer &ai, AiWorkOrder &order)
{
	if (order.factory == ecs::Entity{})
		return;
	const auto *owner = game.world.IsAlive(order.factory) ? game.world.Get<gp::Owner>(order.factory) : nullptr;
	if (owner == nullptr || owner->player != ai.player)
		order.factory = {};
}

// TeamInQueue's tests.
inline bool AllBuilt(const AiTeamInQueue &entry)
{
	return std::all_of(entry.orders.begin(), entry.orders.end(), [](const AiWorkOrder &order) { return order.required <= order.completed; });
}
inline bool BuildTimeExpired(const GameWorld &game, const AiTeamInQueue &entry)
{
	const std::int64_t frames = InfoInt(game, entry.team, "teamInitialIdleFrames");
	if (frames < 1)
		return false;
	return game.tick > entry.started + static_cast<std::uint64_t>(frames);
}
inline bool MinimumBuilt(const AiTeamInQueue &entry)
{
	for (const AiWorkOrder &order : entry.orders)
	{
		const std::int32_t count = order.completed + (order.factory != ecs::Entity{} ? 1 : 0);
		if (order.required > count && order.mandatory)
			return false;
	}
	return true;
}
inline bool BuildsComplete(const AiTeamInQueue &entry)
{
	return std::none_of(entry.orders.begin(), entry.orders.end(), [](const AiWorkOrder &order) { return order.factory != ecs::Entity{}; });
}

// ~TeamInQueue: a team it still holds is started.
inline void Release(GameWorld &game, const AiTeamInQueue &entry)
{
	if (entry.team != NoTeam)
		game.roster.SetActive(entry.team);
}

// TeamInQueue::disband: its units go to the player's default team and the team is deleted (a singleton kept).
inline void Disband(GameWorld &game, AiTeamInQueue &entry)
{
	const std::uint32_t owner = game.roster.TeamAt(entry.team).owner;
	const auto fallback = game.roster.DefaultTeam(owner);
	if (!fallback || *fallback == entry.team)
		return;
	const std::vector<ecs::Entity> members = game.roster.TeamAt(entry.team).members;
	for (const ecs::Entity member : members)
		if (game.world.IsAlive(member))
			SetTeam(game, member, *fallback);
	game.roster.TeamAt(entry.team).members.clear();
	if (!game.roster.TeamAt(game.roster.PrototypeOf(entry.team)).singleton)
		game.roster.DeleteInstance(entry.team);
	entry.team = NoTeam;
}

// AIUpdateInterface::joinTeam: the unit falls in with the first other member (the newest) with an AI that is not held:
// to where it stands when it is idle, else on what it is doing.
inline void JoinTeam(GameWorld &game, ecs::Entity unit)
{
	if (!game.world.IsAlive(unit) || game.world.Get<gp::Dying>(unit) != nullptr)
		return;
	const auto *member = game.world.Get<gp::TeamMember>(unit);
	if (member == nullptr || game.world.Get<gp::MoveOrder>(unit) == nullptr)
		return;
	const auto &members = game.roster.TeamAt(member->team).members;
	ecs::Entity other;
	for (auto it = members.rbegin(); it != members.rend(); ++it)
	{
		if (*it == unit || !game.world.IsAlive(*it) || !HasAi(game, *it))
			continue;
		if (const auto *off = game.world.Get<gp::Disabled>(*it); off != nullptr && (off->mask & gp::disabled_type::Held) != 0)
			continue;
		other = *it;
		break;
	}
	if (other == ecs::Entity{})
		return;
	if (IsIdle(game, other))
	{
		if (const auto *where = game.world.Get<gp::Transform>(other))
			OrderMove(game, unit, where->position.XY(), false, false);
		return;
	}
	if (const auto *order = game.world.Get<gp::MoveOrder>(other))
		*game.world.Get<gp::MoveOrder>(unit) = *order;
	if (const auto *attack = game.world.Get<gp::AttackTarget>(other))
		if (auto *mine = game.world.Get<gp::AttackTarget>(unit))
			*mine = *attack;
}
}

// Team::tryToRecruit: of the player's objects (newest first) of that type or one of its build variations, on an
// active team of lower production priority that may be recruited from (its default team always; a team its template
// or TEAM_AVAILABLE_FOR_RECRUITMENT allows) and not held: the nearest to `home` within `maxDistance` (the first of the
// default team's counts wherever it is, and sets the distance to beat).
inline ecs::Entity TryToRecruit(GameWorld &game, AiPlayers &ais, std::uint32_t team, const content::ObjectDefinition &type,
	Engine::Math::FixedVector2 home, Engine::Math::Fixed maxDistance)
{
	namespace gp = engine::gameplay;
	using namespace ai_team_detail;
	const std::uint32_t player = game.roster.TeamAt(team).owner;
	const auto defaultTeam = game.roster.DefaultTeam(player);
	const std::int32_t priority = PriorityOf(game, ais, team);
	// Only objects of the type (or a variation) can be picked: they alone are sorted newest first (the others were
	// passed over first thing, so the order among these is unchanged).
	const auto ofType = [&](ecs::Entity object) {
		const content::ObjectDefinition *kind = KindOf(game, object);
		return kind != nullptr &&
			(kind->name == type.name || std::find(type.buildVariations.begin(), type.buildVariations.end(), kind->name) != type.buildVariations.end());
	};
	std::vector<std::pair<std::uint32_t, ecs::Entity>> objects;
	ai_detail::ForPlayerObjects(game, player, [&](ecs::Entity entity) {
		if (const auto *id = game.world.Get<gp::ObjectId>(entity); id != nullptr && ofType(entity))
			objects.emplace_back(id->value, entity);
	});
	std::sort(objects.begin(), objects.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
	std::int64_t distance = ai_detail::Squared(maxDistance, Engine::Math::Fixed{});
	ecs::Entity recruit;
	for (const auto &[id, object] : objects)
	{
		const auto *owner = game.world.Get<gp::Owner>(object);
		const auto *member = game.world.Get<gp::TeamMember>(object);
		if (owner == nullptr || member == nullptr || owner->player != player)
			continue;
		const bool isDefault = defaultTeam && member->team == *defaultTeam;
		const gp::Team &theirs = game.roster.TeamAt(member->team);
		if (!theirs.active)
			continue;
		if (PriorityOf(game, ais, member->team) >= priority)
			continue;
		bool recruitable = isDefault || InfoFlag(game, member->team, "teamIsAIRecruitable");
		if (theirs.recruitable >= 0)
			recruitable = theirs.recruitable == 1;
		if (!recruitable)
			continue;
		// AIUpdateInterface::isRecruitable (a script or map property can say no).
		if (const auto *script = game.world.Get<gp::ScriptStatus>(object); script != nullptr && script->Has(gp::script_status::NotRecruitable))
			continue;
		if (const auto *off = game.world.Get<gp::Disabled>(object); off != nullptr && (off->mask & gp::disabled_type::Held) != 0)
			continue;
		const auto &at = game.world.Get<gp::Transform>(object)->position;
		const std::int64_t away = ai_detail::Squared(home.x - at.x, home.y - at.y);
		if (isDefault && recruit == ecs::Entity{})
		{
			recruit = object;
			distance = away;
		}
		if (away > distance)
			continue;
		distance = away;
		recruit = object;
	}
	return recruit;
}

// isPossibleToBuildTeam: a factory for each of its unit types (an idle one for at least one when asked), and the money
// for TeamResourcesToStart of its middling cost (each type's cost times the halfway of its least and most).
inline bool IsPossibleToBuildTeam(GameWorld &game, AiPlayer &ai, std::uint32_t prototype, bool requireIdleFactory, bool &notEnoughMoney)
{
	using namespace ai_team_detail;
	bool anyIdle = false;
	std::int64_t cost = 0;
	notEnoughMoney = false;
	for (const UnitsInfo &unit : UnitsOf(game.teams.At(prototype)))
	{
		const content::ObjectDefinition *thing = game.templates.Content().objects.Find(unit.type);
		if (thing == nullptr)
			continue;
		if (FindFactory(game, ai, *thing, true) == ecs::Entity{})
			return false;
		if (FindFactory(game, ai, *thing, false) != ecs::Entity{})
			anyIdle = true;
		// cost += thingCost * ((max + min) / 2.0f): Int += Real, truncated.
		cost += CostToBuild(game, ai.player, *thing) * (unit.max + unit.min) / 2;
	}
	cost = (Engine::Math::Fixed::FromInt(cost) * game.templates.Content().aiData.teamResourcesToStart).Floor();
	if (game.world.Resource<engine::gameplay::PlayerMoney>().Balance(ai.player) < cost)
	{
		notEnoughMoney = true;
		return false;
	}
	return anyIdle || !requireIdleFactory;
}

// TeamPrototype::evaluateProductionCondition: none, a script that is missing or not for the difficulty: never; else
// its conditions (for the player, no team), looked at no sooner than the prototype's own copy's evaluation delay allows.
inline bool EvaluateProductionCondition(GameWorld &game, AiPlayers &ais, std::uint32_t prototype, const AiScriptHooks &hooks)
{
	const auto name = game.teams.At(prototype).Get<std::string>("teamProductionCondition");
	if (!name || name->empty() || !hooks.allowed || !hooks.allowed(*name))
		return false;
	AiTeamProduction &production = ais.Production(prototype);
	if (game.tick < production.evaluateAt)
		return false;
	if (const std::uint32_t delay = hooks.delaySeconds(*name); delay > 0)
		production.evaluateAt = game.tick + std::uint64_t{delay} * game.step.TicksPerSecond();
	return hooks.conditions(*name);
}

// isAGoodIdeaToBuildTeam: its condition holds, it has fewer than MaxInstances, none is being built, and it can be built
// now (an idle factory).
inline bool IsAGoodIdeaToBuildTeam(GameWorld &game, AiPlayers &ais, AiPlayer &ai, std::uint32_t prototype, const AiScriptHooks &hooks)
{
	using namespace ai_team_detail;
	if (!EvaluateProductionCondition(game, ais, prototype, hooks))
		return false;
	if (static_cast<std::int64_t>(game.roster.Instances(prototype).size()) >= InfoInt(game, prototype, "teamMaxInstances"))
		return false;
	if (Queued(game, ai, prototype))
		return false;
	bool needMoney = false;
	return IsPossibleToBuildTeam(game, ai, prototype, true, needMoney);
}

// buildSpecificAITeam: unless units may not be built (or a script's singleton already stands), a new inactive instance
// of the team goes in the build queue (a script's first, flagged; one picked by the AI last) with orders for its extra
// units (its most less its least) and its needed ones (its least), each put before the last; one that cannot be built
// (no factory or tech) is not queued, one short of money is. A team that executes actions on create runs its production
// condition's actions (again, now for the team).
inline void BuildSpecificAiTeam(GameWorld &game, AiPlayers &ais, AiPlayer &ai, std::uint32_t prototype, bool priorityBuild, const AiScriptHooks &hooks)
{
	using namespace ai_team_detail;
	if (!game.roster.PlayerAt(ai.player).unitConstructionEnabled)
		return;
	const gp::Team &level = game.roster.TeamAt(prototype);
	if (priorityBuild && level.singleton && !TeamDestroyed(game, level.name))
		return;
	bool needMoney = false;
	if (!IsPossibleToBuildTeam(game, ai, prototype, false, needMoney) && !needMoney)
		return;
	const std::vector<UnitsInfo> units = UnitsOf(game.teams.At(prototype));
	std::vector<AiWorkOrder> orders;
	for (const UnitsInfo &unit : units)
		if (const content::ObjectDefinition *thing = game.templates.Content().objects.Find(unit.type))
			if (const std::int32_t count = unit.max - unit.min; count > 0)
				orders.insert(orders.begin(), AiWorkOrder{game.templates.Definition(*thing), {}, 0, count, false, false});
	for (const UnitsInfo &unit : units)
		if (const content::ObjectDefinition *thing = game.templates.Content().objects.Find(unit.type))
			orders.insert(orders.begin(), AiWorkOrder{game.templates.Definition(*thing), {}, 0, unit.min, true, false});
	if (orders.empty())
		return;
	AiTeamInQueue entry;
	entry.priorityBuild = priorityBuild;
	entry.orders = std::move(orders);
	entry.started = game.tick;
	entry.team = CreateInactiveTeam(game, prototype, [&](const std::string &name) { hooks.actions(name, std::nullopt); });
	const std::uint32_t team = entry.team;
	if (priorityBuild)
		ai.buildQueue.insert(ai.buildQueue.begin(), std::move(entry));
	else
		ai.buildQueue.push_back(std::move(entry));
	ai.teamDelay = 0;
	if (InfoFlag(game, team, "teamExecutesActionsOnCreate"))
		if (const auto name = InfoOf(game, team).Get<std::string>("teamProductionCondition"); name && !name->empty() && hooks.allowed && hooks.allowed(*name))
			hooks.actions(*name, team);
}

// selectTeamToReinforce: of its teams not being built that reinforce themselves, above `minPriority` (each one found
// raising the bar): an instance with units short of a type an idle factory can make; one of that is recruited (made
// idle) or trained, as a one-unit reinforcement first in the build queue.
inline bool SelectTeamToReinforce(GameWorld &game, AiPlayers &ais, AiPlayer &ai, std::int32_t minPriority)
{
	using namespace ai_team_detail;
	std::uint32_t curTeam = NoTeam;
	std::int32_t curPriority = minPriority;
	const content::ObjectDefinition *curThing = nullptr;
	for (const std::uint32_t prototype : PlayerTeams(game, ai.player))
	{
		if (Queued(game, ai, prototype))
			continue;
		if (!InfoFlag(game, prototype, "teamAutoReinforce") || PriorityOf(game, ais, prototype) <= curPriority)
			continue;
		for (const std::uint32_t team : game.roster.Instances(prototype))
		{
			if (!TeamHasAnyUnits(game, team))
				continue;
			for (const UnitsInfo &unit : UnitsOf(game.teams.At(prototype)))
			{
				if (unit.max < 1)
					continue;
				const content::ObjectDefinition *thing = game.templates.Content().objects.Find(unit.type);
				if (thing == nullptr)
					continue;
				const auto count = std::count_if(game.roster.TeamAt(team).members.begin(), game.roster.TeamAt(team).members.end(), [&](ecs::Entity member) {
					const content::ObjectDefinition *kind = KindOf(game, member);
					return kind != nullptr && kind->name == thing->name;
				});
				if (count < unit.max && FindFactory(game, ai, *thing, false) != ecs::Entity{})
				{
					curTeam = team;
					curPriority = PriorityOf(game, ais, prototype);
					curThing = thing;
				}
			}
		}
	}
	if (curTeam == NoTeam || curThing == nullptr)
		return false;
	AiTeamInQueue entry;
	entry.reinforcement = true;
	entry.orders.push_back({game.templates.Definition(*curThing), {}, 0, 1, true, false});
	entry.started = game.tick;
	entry.team = curTeam;
	ai.buildQueue.insert(ai.buildQueue.begin(), std::move(entry));
	AiTeamInQueue &queued = ai.buildQueue.front();
	FixedVector2 origin = HomeOf(game, curTeam).value_or(FixedVector2{});
	if (const auto &members = game.roster.TeamAt(curTeam).members; !members.empty() && game.world.IsAlive(members.back()))
		origin = game.world.Get<gp::Transform>(members.back())->position.XY();
	const ecs::Entity unit = TryToRecruit(game, ais, curTeam, *curThing, origin, game.templates.Content().aiData.maxRecruitRadius);
	if (unit != ecs::Entity{})
	{
		queued.orders.front().completed = 1;
		SetTeam(game, unit, curTeam);
		queued.reinforcementUnit = unit;
		OrderStop(game, unit, false, false);
	}
	else
		StartTraining(game, ai, queued.orders.front(), queued.priorityBuild);
	ai.teamDelay = 0;
	return true;
}

// selectTeamToBuild: the good ideas at the highest priority (a reinforcement above it comes first); one of them at
// random is built, and the next team waits TeamSeconds (divided by TeamsPoorRate below Poor money, by TeamsWealthyRate
// above Wealthy).
inline bool SelectTeamToBuild(GameWorld &game, AiPlayers &ais, AiPlayer &ai, const AiScriptHooks &hooks)
{
	using namespace ai_team_detail;
	constexpr std::int32_t InvalidPriority = -99999;
	std::int32_t highest = InvalidPriority;
	std::vector<std::uint32_t> good;
	for (const std::uint32_t prototype : PlayerTeams(game, ai.player))
		if (IsAGoodIdeaToBuildTeam(game, ais, ai, prototype, hooks))
		{
			good.push_back(prototype);
			highest = std::max(highest, PriorityOf(game, ais, prototype));
		}
	if (SelectTeamToReinforce(game, ais, ai, highest))
		return true;
	if (highest == InvalidPriority)
		return false;
	std::vector<std::uint32_t> candidates;
	for (const std::uint32_t prototype : good)
		if (PriorityOf(game, ais, prototype) == highest)
			candidates.push_back(prototype);
	const auto which = Engine::Math::UniformInt(game.random, 0, static_cast<std::int64_t>(candidates.size()) - 1);
	BuildSpecificAiTeam(game, ais, ai, candidates[static_cast<std::size_t>(which)], false, hooks);
	ai.readyToBuildTeam = false;
	ai.teamTimer = ai.teamSeconds * game.step.TicksPerSecond();
	const std::int64_t money = game.world.Resource<engine::gameplay::PlayerMoney>().Balance(ai.player);
	const content::AiData &data = game.templates.Content().aiData;
	if (money < data.poor)
		ai.teamTimer = (Engine::Math::Fixed::FromInt(ai.teamTimer) / data.teamsPoorRate).Floor();
	else if (money > data.wealthy)
		ai.teamTimer = (Engine::Math::Fixed::FromInt(ai.teamTimer) / data.teamsWealthyRate).Floor();
	return true;
}

namespace ai_team_detail
{
// A supply truck's AI (SupplyTruckAIInterface): a harvester with an AI.
inline gp::Harvester *SupplyTruck(GameWorld &game, ecs::Entity unit)
{
	const content::ObjectDefinition *kind = KindOf(game, unit);
	if (kind == nullptr || !kind->Is("HARVESTER") || !HasAi(game, unit))
		return nullptr;
	return game.world.Get<gp::Harvester>(unit);
}

// isCurrentlyFerryingSupplies: wanting supplies or docking.
inline bool Ferrying(const gp::Harvester &truck) { return truck.state == gp::HarvesterState::Wanting || truck.state == gp::HarvesterState::Docking; }

// The player's supply trucks, in its teams' order (Player::getPlayerTeams, each instance, each member).
inline std::vector<ecs::Entity> SupplyTrucks(GameWorld &game, std::uint32_t player)
{
	std::vector<ecs::Entity> out;
	for (const std::uint32_t prototype : PlayerTeams(game, player))
		for (const std::uint32_t team : game.roster.Instances(prototype))
			for (auto it = game.roster.TeamAt(team).members.rbegin(); it != game.roster.TeamAt(team).members.rend(); ++it)
				if (game.world.IsAlive(*it) && SupplyTruck(game, *it) != nullptr)
					out.push_back(*it);
	return out;
}

// The supply source closest to `centre` (bounding sphere, 2D) within `radius`, of anyone but the player, on the map.
inline ecs::Entity ClosestSupplySource(GameWorld &game, std::uint32_t player, FixedVector2 centre, Fixed radius)
{
	ecs::Entity best;
	std::int64_t bestDistance = 0;
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
	{
		if (game.roster.TeamAt(team).owner == player)
			continue;
		for (const ecs::Entity member : game.roster.TeamAt(team).members)
		{
			const content::ObjectDefinition *kind = KindOf(game, member);
			if (kind == nullptr || !kind->Is("SUPPLY_SOURCE") || game.world.Get<gp::OffMap>(member) != nullptr)
				continue;
			const auto &at = game.world.Get<gp::Transform>(member)->position;
			const Fixed gap = Engine::Math::Distance(at.XY(), centre) - content::BoundingSphereRadius(kind->geometry);
			if (gap > radius)
				continue;
			const std::int64_t distance = gap.Raw();
			if (best == ecs::Entity{} || distance < bestDistance)
			{
				best = member;
				bestDistance = distance;
			}
		}
	}
	return best;
}
}

// AIPlayer::onStructureProduced (Player::onStructureConstructionComplete): the next looks come at once; the build list
// entry it was put up for is no longer under construction and is checked for a supply centre (rebuild holes are not
// ported).
inline void OnAiStructureProduced(GameWorld &game, AiPlayer &ai, ecs::Entity structure)
{
	ai.teamDelay = 0;
	ai.buildDelay = 0;
	for (AiBuildSlot &slot : ai.buildList)
	{
		if (slot.built != structure)
			continue;
		slot.underConstruction = false;
		CheckForSupplyCenter(game, ai, slot, structure);
		return;
	}
	// Spawned from a hole: the entry whose hole put it up takes it.
	for (AiBuildSlot &slot : ai.buildList)
		if (const auto *hole = game.world.IsAlive(slot.built) ? game.world.Get<RebuildHole>(slot.built) : nullptr; hole != nullptr && hole->reconstructing == structure)
		{
			slot.built = structure;
			return;
		}
}

// queueSupplyTruck: unless one is being built, each supply building of the plan that has its gatherers checks it still
// has supplies near it (a supply source within 20 cells of its bounding circle, not its own, with boxes and not an
// enemy's), counts again the trucks that dock there (sending those not ferrying back to it); one short of gatherers
// first takes a loose truck (one whose dock is gone but still ferrying or told to), else, unless it already has three
// times its want, orders one at a factory that can make one now (the free one that comes with a new supply centre is
// counted as that factory's).
inline void QueueSupplyTruck(GameWorld &game, AiPlayer &ai)
{
	using namespace ai_team_detail;
	for (const AiTeamInQueue &entry : ai.buildQueue)
		for (const AiWorkOrder &order : entry.orders)
			if (order.resourceGatherer)
				return;
	const std::vector<ecs::Entity> trucks = SupplyTrucks(game, ai.player);
	const std::int32_t totalHarvesters = static_cast<std::int32_t>(trucks.size());
	const auto *relationships = game.world.FindResource<gp::Relationships>();
	for (AiBuildSlot &slot : ai.buildList)
	{
		if (!slot.supplyBuilding)
			continue;
		const std::int32_t desired = slot.desiredGatherers;
		if (slot.currentGatherers >= desired)
		{
			const ecs::Entity center = ai_base_detail::Standing(game, slot);
			if (center == ecs::Entity{})
				continue;
			const content::ObjectDefinition *centerKind = KindOf(game, center);
			if (centerKind == nullptr || centerKind->Is("REBUILD_HOLE"))
				continue;
			const FixedVector2 at = game.world.Get<gp::Transform>(center)->position.XY();
			const Fixed radius = Fixed::FromInt(20 * gp::PathfindCellSize) + content::BoundingSphereRadius(centerKind->geometry);
			const ecs::Entity source = ClosestSupplySource(game, ai.player, at, radius);
			if (source == ecs::Entity{})
				continue;
			if (const auto *store = game.world.Get<gp::ResourceStore>(source))
			{
				if (static_cast<std::int64_t>(store->boxes) * game.templates.Content().gameData.valuePerSupplyBox <= 0)
					continue;
				const auto *owner = game.world.Get<gp::Owner>(source);
				if (relationships != nullptr && owner != nullptr && relationships->Enemies(ai.player, owner->player))
					continue;
			}
			CheckForSupplyCenter(game, ai, slot, center);
			std::int32_t gatherers = 0;
			for (const ecs::Entity truck : trucks)
			{
				gp::Harvester *harvester = SupplyTruck(game, truck);
				if (harvester == nullptr || harvester->preferredDock != center)
					continue;
				++gatherers;
				if (!Ferrying(*harvester))
					OrderDock(game, truck, center, true);
			}
			slot.currentGatherers = gatherers;
			continue;
		}
		for (const ecs::Entity truck : trucks)
		{
			gp::Harvester *harvester = SupplyTruck(game, truck);
			if (harvester == nullptr || game.world.IsAlive(harvester->preferredDock))
				continue;
			if (Ferrying(*harvester) || harvester->forceWanting)
			{
				const ecs::Entity center = ai_base_detail::Standing(game, slot);
				if (center != ecs::Entity{})
				{
					++slot.currentGatherers;
					OrderDock(game, truck, center, true);
					return;
				}
			}
		}
		if (totalHarvesters >= desired * 3)
			continue;
		// ThingFactory order is its own; here the catalog's (by name).
		for (const auto &[name, definition] : game.templates.Content().objects)
		{
			if (!definition.Is("HARVESTER"))
				continue;
			const ecs::Entity factory = FindFactory(game, ai, definition, false);
			if (factory == ecs::Entity{})
				continue;
			AiTeamInQueue entry;
			entry.priorityBuild = true;
			entry.orders.push_back({game.templates.Definition(definition), {}, 0, 1, true, true});
			entry.started = game.tick;
			entry.team = game.roster.DefaultTeam(ai.player).value_or(NoTeam);
			ai.buildQueue.insert(ai.buildQueue.begin(), std::move(entry));
			ai.teamDelay = 0;
			AiWorkOrder &order = ai.buildQueue.front().orders.front();
			if (slot.currentGatherers == -1)
			{
				// The first one is automatic.
				order.factory = factory;
				slot.currentGatherers = 0;
			}
			else
				StartTraining(game, ai, order, true);
			break;
		}
	}
}

// queueUnits: supply trucks first (queueSupplyTruck); for each order of each team being built, units that can be
// recruited near the team's home (or the base) join it first (sent home, or stopped); what is still missing is trained, else the factory at work is checked.
inline void QueueUnits(GameWorld &game, AiPlayers &ais, AiPlayer &ai)
{
	using namespace ai_team_detail;
	QueueSupplyTruck(game, ai);
	for (std::size_t index = 0; index < ai.buildQueue.size(); ++index)
	{
		AiTeamInQueue &entry = ai.buildQueue[index];
		for (AiWorkOrder &order : entry.orders)
		{
			std::optional<FixedVector2> home = HomeOf(game, entry.team);
			const bool hasHome = home.has_value() || ai.baseCenterSet;
			if (!home)
				home = ai.baseCenter;
			const content::ObjectDefinition &thing = game.templates.DefinitionAt(order.definition);
			while (order.WaitingToBuild())
			{
				const ecs::Entity unit = TryToRecruit(game, ais, entry.team, thing, *home, game.templates.Content().aiData.maxRecruitRadius);
				if (unit == ecs::Entity{})
					break;
				++order.completed;
				SetTeam(game, unit, entry.team);
				if (hasHome)
					OrderMove(game, unit, *home, false, false);
				else
					OrderStop(game, unit, false, false);
			}
			if (order.WaitingToBuild())
				StartTraining(game, ai, order, entry.priorityBuild);
			else
				ValidateFactory(game, ai, order);
		}
	}
}

// checkReadyTeams: a built team starts (setActive; a skirmish player's team-building flags are cleared) once it is
// idle, once any member is idle when it has start actions, or after 60 s; a reinforcement falls in with its team. After
// each start the list is walked again from its second entry (the original's iterator reset then advance).
inline void CheckReadyTeams(GameWorld &game, AiPlayer &ai, const AiScriptHooks &hooks)
{
	using namespace ai_team_detail;
	for (std::size_t index = 0; index < ai.readyQueue.size();)
	{
		AiTeamInQueue &entry = ai.readyQueue[index];
		const bool timeExpired = entry.started + 60 * game.step.TicksPerSecond() < game.tick;
		bool allIdle = true;
		bool anyIdle = false;
		if (entry.reinforcement)
		{
			if (game.world.IsAlive(entry.reinforcementUnit) && HasAi(game, entry.reinforcementUnit))
			{
				allIdle = IsIdle(game, entry.reinforcementUnit);
				anyIdle = allIdle;
			}
		}
		else
		{
			allIdle = TeamIdle(game, entry.team);
			anyIdle = AnyIdle(game, entry.team);
		}
		if (anyIdle && StartActions(game, entry.team, hooks))
			allIdle = true;
		if (timeExpired)
			allIdle = true;
		if (!allIdle)
		{
			++index;
			continue;
		}
		entry.sentToStartLocation = true;
		AiTeamInQueue started = std::move(entry);
		ai.readyQueue.erase(ai.readyQueue.begin() + static_cast<std::ptrdiff_t>(index));
		if (started.reinforcement)
			JoinTeam(game, started.reinforcementUnit);
		else
		{
			game.roster.SetActive(started.team);
			if (ai.skirmish && hooks.clearTeamFlags)
				hooks.clearTeamFlags();
		}
		Release(game, started);
		index = 1;
	}
}

// checkQueuedTeams: a team out of build time moves to the ready queue once its minimum is built and nothing is still
// in production, else is disbanded; then a team with everything built moves to the ready queue, and one still being
// built with an idle member runs its start actions. Each move walks the queue again from its second entry.
inline void CheckQueuedTeams(GameWorld &game, AiPlayer &ai, const AiScriptHooks &hooks)
{
	using namespace ai_team_detail;
	for (std::size_t index = 0; index < ai.buildQueue.size();)
	{
		AiTeamInQueue &entry = ai.buildQueue[index];
		if (!BuildTimeExpired(game, entry))
		{
			++index;
			continue;
		}
		if (MinimumBuilt(entry))
		{
			if (!BuildsComplete(entry))
			{
				++index;
				continue;
			}
			AiTeamInQueue ready = std::move(entry);
			ai.buildQueue.erase(ai.buildQueue.begin() + static_cast<std::ptrdiff_t>(index));
			ai.readyQueue.insert(ai.readyQueue.begin(), std::move(ready));
		}
		else
		{
			AiTeamInQueue gone = std::move(entry);
			ai.buildQueue.erase(ai.buildQueue.begin() + static_cast<std::ptrdiff_t>(index));
			Disband(game, gone);
			Release(game, gone);
			if (ai.skirmish && hooks.clearTeamFlags)
				hooks.clearTeamFlags();
		}
		index = 1;
	}
	for (std::size_t index = 0; index < ai.buildQueue.size();)
	{
		AiTeamInQueue &entry = ai.buildQueue[index];
		if (AllBuilt(entry))
		{
			AiTeamInQueue ready = std::move(entry);
			ai.buildQueue.erase(ai.buildQueue.begin() + static_cast<std::ptrdiff_t>(index));
			ai.readyQueue.insert(ai.readyQueue.begin(), std::move(ready));
			index = 1;
			continue;
		}
		if (AnyIdle(game, entry.team))
			if (const auto actions = StartActions(game, entry.team, hooks))
				hooks.actions(*actions, entry.team);
		++index;
	}
}

// doTeamBuilding: while it may build units, the team timer runs down (a skirmish player's held to 3 s) to ready it;
// every look (2 s for a skirmish player, else 5 s; sooner when something was built) queues units and, when ready,
// picks the next team.
inline void DoTeamBuilding(GameWorld &game, AiPlayers &ais, AiPlayer &ai, const AiScriptHooks &hooks)
{
	if (!game.roster.PlayerAt(ai.player).unitConstructionEnabled)
		return;
	const std::int64_t second = game.step.TicksPerSecond();
	if (!ai.readyToBuildTeam)
	{
		if (--ai.teamTimer <= 0)
		{
			ai.readyToBuildTeam = true;
			ai.teamDelay = 0;
		}
		if (ai.skirmish && ai.teamTimer > 3 * second)
			ai.teamTimer = 3 * second;
	}
	if (--ai.teamDelay < 1)
	{
		QueueUnits(game, ais, ai);
		if (ai.readyToBuildTeam && SelectTeamToBuild(game, ais, ai, hooks))
			QueueUnits(game, ais, ai);
		ai.teamDelay = (ai.skirmish ? 2 : 5) * second;
	}
}

// AIPlayer::onUnitProduced: the first order of the build queue its factory was making it for (of its type, still
// short) counts it: it joins that team (a reinforcement's is noted), follows its exit path home if the team has one, and a supply
// gatherer is told whether to gather. A dozer that is not a supply truck (one awaited for a repair) takes the repair, else brings the next structure look
// forward. The next team look comes at once.
inline void OnAiUnitProduced(GameWorld &game, AiPlayer &ai, ecs::Entity factory, ecs::Entity unit)
{
	namespace gp = engine::gameplay;
	using namespace ai_team_detail;
	if (factory == ecs::Entity{} || !game.world.IsAlive(unit))
		return;
	const content::ObjectDefinition *kind = KindOf(game, unit);
	// Only a matched order's supply truck sets it. (The retail VS6 build started it true, an uninitialised flag's value:
	// a produced dozer then never reached the repair job below and a queued repair waited forever; fixed as the
	// original's later builds have it.)
	bool supplyTruck = false;
	bool found = false;
	for (AiTeamInQueue &entry : ai.buildQueue)
	{
		if (found)
			break;
		for (AiWorkOrder &order : entry.orders)
		{
			if (order.factory != factory || order.completed >= order.required || kind == nullptr || game.templates.DefinitionAt(order.definition).name != kind->name)
				continue;
			++order.completed;
			if (entry.team != NoTeam)
				SetTeam(game, unit, entry.team);
			if (entry.reinforcement)
				entry.reinforcementUnit = unit;
			// aiFollowExitProductionPath: on from where it is headed (its goal: the rally point), then home.
			if (const auto home = entry.team != NoTeam ? HomeOf(game, entry.team) : std::nullopt)
				if (auto *move = game.world.Get<gp::MoveOrder>(unit))
				{
					const FixedVector2 goal = move->mode != gp::MoveMode::Idle ? move->destination : game.world.Get<gp::Transform>(unit)->position.XY();
					OrderMove(game, unit, goal, false, false);
					gp::MovePath path;
					path.count = 2;
					path.next = 1; // the first leg is under way
					path.points[0] = goal;
					path.points[1] = *home;
					if (game.world.Has<gp::MovePath>(unit))
						*game.world.Get<gp::MovePath>(unit) = path;
					else
					{
						game.world.Add<gp::MovePath>(unit);
						*game.world.Get<gp::MovePath>(unit) = path;
					}
				}
			order.factory = {};
			if (auto *harvester = SupplyTruck(game, unit))
			{
				supplyTruck = order.resourceGatherer;
				harvester->forceWanting = supplyTruck;
				// Sent to a supply building still short of gatherers (each such one: the last it is sent to holds).
				if (supplyTruck)
					for (AiBuildSlot &slot : ai.buildList)
						if (slot.supplyBuilding && slot.desiredGatherers > 0 && slot.desiredGatherers > slot.currentGatherers)
							if (const ecs::Entity center = ai_base_detail::Standing(game, slot); center != ecs::Entity{})
							{
								++slot.currentGatherers;
								OrderDock(game, unit, center, true);
							}
			}
			found = true;
			break;
		}
	}
	if (!supplyTruck && kind != nullptr && kind->Is("DOZER"))
	{
		if (ai.dozerQueuedForRepair)
		{
			ai.repairDozer = unit;
			ai.dozerQueuedForRepair = false;
		}
		else
		{
			ai.buildDelay = 0;
			ai.structureTimer = 1;
		}
	}
	ai.teamDelay = 0;
}

// AIPlayer::doUpgradesAndSkills: from tick 2, with purchase points to spend: a skill set of its side's is chosen once
// (a skirmish player's at random among the first ones that have skills, set 1 up to the last of 2..5 in a row with
// any; a plain one's is set 1), and each of its sciences it can buy now is bought, in order.
inline void DoUpgradesAndSkills(GameWorld &game, AiPlayer &ai)
{
	if (game.tick < 2)
		return;
	const engine::gameplay::PlayerRank *rank = game.world.Resource<engine::gameplay::PlayerRanks>().Find(ai.player);
	if (rank == nullptr || rank->purchasePoints <= 0)
		return;
	const content::AiSideInfo *side = game.templates.Content().aiData.Side(ai.side);
	if (side == nullptr)
		return;
	const auto skills = [&](std::size_t set) { return set < side->skillSets.size() ? side->skillSets[set].size() : std::size_t{0}; };
	if (ai.skillset < 0)
	{
		std::int32_t limit = 0;
		while (limit < 4 && skills(static_cast<std::size_t>(limit) + 1) > 0)
			++limit;
		ai.skillset = ai.skirmish ? static_cast<std::int32_t>(Engine::Math::UniformInt(game.random, 0, limit)) : 0;
	}
	const std::size_t set = static_cast<std::size_t>(std::clamp(ai.skillset, 0, 4));
	if (set >= side->skillSets.size())
		return;
	for (const std::string &name : side->skillSets[set])
		if (const auto science = game.templates.Content().Science(name))
			PurchaseScience(game, ai.player, *science);
}

// AIPlayer::repairStructure (PLAYER_REPAIR_NAMED_STRUCTURE): a damaged structure (not pristine: at most UnitDamagedThresh
// of its most health) joins the repair list once, while it has room (2).
inline void RepairStructure(GameWorld &game, AiPlayer &ai, ecs::Entity structure)
{
	using namespace ai_team_detail;
	const auto *health = game.world.IsAlive(structure) ? game.world.Get<gp::Health>(structure) : nullptr;
	if (health == nullptr || health->current > health->maximum * game.templates.Content().gameData.unitDamaged)
		return;
	if (std::find(ai.toRepair.begin(), ai.toRepair.end(), structure) != ai.toRepair.end() || ai.toRepair.size() >= 2)
		return;
	ai.toRepair.push_back(structure);
}

// AIPlayer::updateBridgeRepair: once a second while something waits: the first still standing is repaired by its
// repair dozer (found near it, else one queued and awaited); a dozer done (no task left) with it pristine takes the next,
// or with none left goes back to the base (or where it started); a lost dozer is replaced on the next tick.
inline void UpdateStructureRepair(GameWorld &game, AiPlayer &ai)
{
	using namespace ai_team_detail;
	if (ai.toRepair.empty())
		return;
	if (--ai.repairTimer > 0)
		return;
	ai.repairTimer = game.step.TicksPerSecond();
	while (!ai.toRepair.empty() && !game.world.IsAlive(ai.toRepair.front()))
		ai.toRepair.erase(ai.toRepair.begin());
	if (ai.toRepair.empty())
		return;
	const ecs::Entity structure = ai.toRepair.front();
	const FixedVector2 at = game.world.Get<gp::Transform>(structure)->position.XY();
	const auto *health = game.world.Get<gp::Health>(structure);
	const bool pristine = health == nullptr || health->current > health->maximum * game.templates.Content().gameData.unitDamaged;
	if (ai.repairDozer == ecs::Entity{})
	{
		ai.dozerIsRepairing = false;
		if (ai.dozerQueuedForRepair)
			return; // waiting for one
		if (const ecs::Entity dozer = FindDozer(game, ai, at); dozer != ecs::Entity{})
		{
			ai.repairDozer = dozer;
			ai.repairDozerOrigin = game.world.Get<gp::Transform>(dozer)->position.XY();
			OrderWork(game, dozer, structure); // aiRepair
			ai.dozerIsRepairing = true;
			return;
		}
		QueueDozer(game, ai);
		ai.dozerQueuedForRepair = true;
		return;
	}
	if (!game.world.IsAlive(ai.repairDozer))
	{
		ai.repairDozer = {};
		ai.repairTimer = 0;
		return;
	}
	if (ai.dozerIsRepairing)
	{
		if (game.world.Get<gp::Builder>(ai.repairDozer) != nullptr)
			return; // at work
		if (pristine)
		{
			ai.toRepair.erase(ai.toRepair.begin());
			ai.dozerIsRepairing = false;
			if (ai.toRepair.empty())
			{
				OrderMove(game, ai.repairDozer, ai.baseCenterSet ? ai.baseCenter : ai.repairDozerOrigin, false, false);
				return;
			}
		}
	}
	OrderWork(game, ai.repairDozer, ai.toRepair.front());
	ai.dozerIsRepairing = true;
}

// AIPlayer::update: base building, then its teams, then its upgrades and skills, then its repairs.
inline void UpdateAiPlayer(GameWorld &game, AiPlayers &ais, AiPlayer &ai, const AiScriptHooks &hooks)
{
	DoBaseBuilding(game, ai);
	CheckReadyTeams(game, ai, hooks);
	CheckQueuedTeams(game, ai, hooks);
	DoTeamBuilding(game, ais, ai, hooks);
	DoUpgradesAndSkills(game, ai);
	UpdateStructureRepair(game, ai);
}

// recruitSpecificAITeam (RECRUIT_TEAM): unless a singleton of it stands, a new inactive instance takes, per unit type,
// up to its most of the player's units recruitable within `radius` (below 1: anywhere) of the team's home, each sent
// home; with anyone it goes first in the ready queue, else the instance goes (a singleton stays).
inline void RecruitSpecificAiTeam(GameWorld &game, AiPlayers &ais, AiPlayer &ai, std::uint32_t prototype, Engine::Math::Fixed radius, const AiScriptHooks &hooks)
{
	using namespace ai_team_detail;
	if (radius < Fixed::One())
		radius = Fixed::FromInt(99999);
	const gp::Team &level = game.roster.TeamAt(prototype);
	if (level.singleton && !TeamDestroyed(game, level.name))
		return;
	const std::uint32_t team = CreateInactiveTeam(game, prototype, [&](const std::string &name) { hooks.actions(name, std::nullopt); });
	// m_homeLocation: the teamHome waypoint (none: the origin).
	const FixedVector2 home = HomeOf(game, prototype).value_or(FixedVector2{});
	std::int32_t recruited = 0;
	for (const UnitsInfo &unit : UnitsOf(game.teams.At(prototype)))
	{
		const content::ObjectDefinition *thing = game.templates.Content().objects.Find(unit.type);
		if (thing == nullptr)
			continue;
		for (std::int32_t count = unit.max; count > 0; --count)
		{
			const ecs::Entity recruit = TryToRecruit(game, ais, team, *thing, home, radius);
			if (recruit == ecs::Entity{})
				break;
			++recruited;
			SetTeam(game, recruit, team);
			if (HasAi(game, recruit))
				OrderMove(game, recruit, home, false, false);
		}
	}
	if (recruited > 0)
	{
		AiTeamInQueue entry;
		entry.started = game.tick;
		entry.team = team;
		ai.readyQueue.insert(ai.readyQueue.begin(), std::move(entry));
	}
	else if (!game.roster.TeamAt(game.roster.PrototypeOf(team)).singleton)
		game.roster.DeleteInstance(team);
}

// TeamPrototype::increaseAIPriorityForSuccess / decreaseAIPriorityForFailure (TEAM_INCREASE_PRIORITY /
// TEAM_DECREASE_PRIORITY): its production priority up by ProductionPrioritySuccessIncrease or down by
// ProductionPriorityFailureDecrease.
inline void ChangeProductionPriority(GameWorld &game, AiPlayers &ais, std::uint32_t team, bool success)
{
	using namespace ai_team_detail;
	const std::uint32_t prototype = game.roster.PrototypeOf(team);
	const std::int32_t now = PriorityOf(game, ais, prototype);
	AiTeamProduction &production = ais.Production(prototype);
	production.priorityChanged = true;
	production.priority = success ? now + static_cast<std::int32_t>(InfoInt(game, prototype, "teamProductionPrioritySuccessIncrease"))
								  : now - static_cast<std::int32_t>(InfoInt(game, prototype, "teamProductionPriorityFailureDecrease"));
}

// BUILD_TEAM (Player::buildSpecificTeam): the team's player, if a computer player, builds it first.
inline void BuildTeamByScript(GameWorld &game, AiPlayers &ais, const std::string &name, const AiScriptHooks &hooks)
{
	const auto prototype = game.roster.FindTeam(name);
	if (!prototype)
		return;
	if (AiPlayer *ai = ais.Of(game.roster.TeamAt(*prototype).owner))
		BuildSpecificAiTeam(game, ais, *ai, *prototype, true, hooks);
}
}

export namespace generalszh::gameplay
{
// RECRUIT_TEAM (doRecruitTeam -> Player::recruitSpecificTeam): the team's player, if a computer player, recruits it.
inline void RecruitTeamByScript(GameWorld &game, AiPlayers &ais, const std::string &name, Engine::Math::Fixed radius, const AiScriptHooks &hooks)
{
	const auto named = ResolveTeam(game, name);
	if (!named)
		return;
	const std::uint32_t prototype = game.roster.PrototypeOf(*named);
	if (AiPlayer *ai = ais.Of(game.roster.TeamAt(prototype).owner))
		RecruitSpecificAiTeam(game, ais, *ai, prototype, radius, hooks);
}
}
