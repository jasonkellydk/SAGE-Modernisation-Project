export module games.generalszh.gameplay.teams.algorithms.team_actions;
import std;
import games.generalszh.gameplay.battleplans.algorithms.battle_plan_bonuses;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.common.status.components.script_status;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.orders.algorithms.group_orders;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.combat.components.aggression;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.containment.components.cargo_size;
import engine.gameplay.rts.containment.components.transport;
import games.generalszh.gameplay.construction.algorithms.selling;
import games.generalszh.gameplay.score.algorithms.scoring;
import games.generalszh.gameplay.upgrades.algorithms.research;
import engine.gameplay.rts.upgrades.resources.player_upgrades;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.rts.match.resources.match_outcome;
import engine.gameplay.common.identity.resources.relationships;

// Players and teams as the level authored them, and what scripts do with
// teams: reinforcements, paths, stances, merges, deletes, and the questions
// they ask about them.
export namespace generalszh::gameplay
{
namespace gameplay = engine::gameplay;
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;

// Players (with their allies and enemies), their relationships and the
// level's teams (with what each was authored with).
void SetUpTeams(const engine::level::Scenario &scenario, gameplay::TeamRoster &roster, gameplay::Relationships &relationships, TeamTemplates &templates)
{
	for (const auto &participant : scenario.participants)
		roster.AddPlayer({participant.properties.Get<std::string>("playerName").value_or(""), {}, {}});
	const auto relations = [&](const std::string &names) {
		std::vector<std::uint32_t> out;
		std::istringstream stream(names);
		for (std::string name; stream >> name;)
			if (const auto index = roster.FindPlayer(name))
				out.push_back(*index);
		return out;
	};
	for (std::uint32_t index = 0; index < scenario.participants.size(); ++index)
	{
		const auto &properties = scenario.participants[index].properties;
		roster.PlayerAt(index).allies = relations(properties.Get<std::string>("playerAllies").value_or(""));
		roster.PlayerAt(index).enemies = relations(properties.Get<std::string>("playerEnemies").value_or(""));
	}
	if (roster.PlayerCount() == 0)
		roster.AddPlayer({});
	relationships = gameplay::Relationships(roster.PlayerCount());
	for (std::uint32_t player = 0; player < roster.PlayerCount(); ++player)
	{
		for (const std::uint32_t ally : roster.PlayerAt(player).allies)
			relationships.Set(player, ally, gameplay::Relationship::Allies);
		for (const std::uint32_t enemy : roster.PlayerAt(player).enemies)
			relationships.Set(player, enemy, gameplay::Relationship::Enemies);
	}
	for (const auto &group : scenario.groups)
	{
		gameplay::Team team;
		team.name = group.Get<std::string>("teamName").value_or("");
		team.owner = roster.FindPlayer(group.Get<std::string>("teamOwner").value_or("")).value_or(0);
		team.singleton = group.Get<bool>("teamIsSingleton").value_or(false);
		team.attackCommonTarget = group.Get<bool>("teamAttackCommonTarget").value_or(false);
		// Inactive until something activates it (objects placed on it, a player's default team, reinforcements).
		team.active = false;
		roster.AddTeam(std::move(team));
		templates.Add(&group);
	}
	if (roster.TeamCount() == 0)
	{
		roster.AddTeam({"team", 0, true, false, {}});
		templates.Add(nullptr);
	}
}

// GameLogic::startNewGame in a Generals' Challenge: the local player takes the placeholder "ThePlayer"'s enemies as its
// own, both ways; a map without "ThePlayer" makes it allied with itself and mutual enemies with every player but the
// neutral one and "PlyrCivilian" (neutral both ways).
void ApplyChallengeRelationships(gameplay::TeamRoster &roster, gameplay::Relationships &relationships, std::uint32_t local)
{
	const auto set = [&](std::uint32_t other, gameplay::Relationship relationship) {
		relationships.Set(other, local, relationship);
		relationships.Set(local, other, relationship);
		if (relationship != gameplay::Relationship::Enemies || other == local)
			return;
		for (const auto [from, to] : {std::pair{other, local}, std::pair{local, other}})
			if (std::ranges::find(roster.PlayerAt(from).enemies, to) == roster.PlayerAt(from).enemies.end())
				roster.PlayerAt(from).enemies.push_back(to);
	};
	if (const auto placeholder = roster.FindPlayer("ThePlayer"))
	{
		for (std::uint32_t other = 0; other < roster.PlayerCount(); ++other)
			if (relationships.Enemies(*placeholder, other))
				set(other, gameplay::Relationship::Enemies);
		return;
	}
	for (std::uint32_t other = 0; other < roster.PlayerCount(); ++other)
	{
		const std::string &name = roster.PlayerAt(other).name;
		set(other, other == local ? gameplay::Relationship::Allies
				: name.empty() || name == "PlyrCivilian" ? gameplay::Relationship::Neutral : gameplay::Relationship::Enemies);
	}
}

// A team as scripts name it (ScriptEngine::getTeamNamed): "#<index>" is that instance (the calling team, resolved
// before); a name is its first instance (TeamFactory::findTeam: the newest), else the team the level made.
std::optional<std::uint32_t> ResolveTeam(const GameWorld &game, std::string_view name)
{
	if (name.size() > 1 && name.front() == '#')
	{
		std::uint32_t index = 0;
		if (std::from_chars(name.data() + 1, name.data() + name.size(), index).ec == std::errc{} && index < game.roster.TeamCount())
			return index;
		return std::nullopt;
	}
	const auto prototype = game.roster.FindTeam(name);
	if (!prototype)
		return std::nullopt;
	const auto instances = game.roster.Instances(*prototype);
	return instances.empty() ? *prototype : instances.front();
}

std::uint32_t TeamIndex(const GameWorld &game, std::string_view name) { return ResolveTeam(game, name).value_or(0); }

// TeamFactory::createInactiveTeam: a new inactive instance of the team (a singleton: the one it has); a team that
// executes actions on create runs its production condition's actions then (for no team).
std::uint32_t CreateInactiveTeam(GameWorld &game, std::uint32_t prototype, const std::function<void(const std::string &)> &runActions)
{
	const std::uint32_t instance = game.roster.CreateInstance(prototype);
	const engine::level::Properties &info = game.teams.At(prototype);
	if (runActions && info.Get<bool>("teamExecutesActionsOnCreate").value_or(false))
		if (const auto condition = info.Get<std::string>("teamProductionCondition"); condition && !condition->empty())
			runActions(*condition);
	return instance;
}

// getTeamNamed, else TeamFactory::createTeam: the team as the scripts name it, or a new instance of it made and set
// active when it has none.
std::optional<std::uint32_t> TeamToCreateOn(GameWorld &game, const std::string &team)
{
	if (const auto prototype = game.roster.FindTeam(team); prototype && !team.empty() && team.front() != '#' &&
		game.roster.Instances(*prototype).empty() && !game.roster.TeamAt(*prototype).active)
	{
		const std::uint32_t index = CreateInactiveTeam(game, *prototype, {});
		game.roster.SetActive(index);
		return index;
	}
	return ResolveTeam(game, team);
}

// Object::setTeam: out of its team into `team` (and its player's).
// Object::onCapture (every setTeam from one team to another): whatever made it unsellable is forgotten, so a player
// can sell what they take (OBJECT_STATUS_SCRIPT_UNSELLABLE cleared), and its new owner's score keeper counts it
// (addObjectCaptured).
inline void OnCapture(GameWorld &game, ecs::Entity entity)
{
	if (auto *script = game.world.Get<engine::gameplay::ScriptStatus>(entity))
		script->Set(engine::gameplay::script_status::Unsellable, false);
	if (const auto *owner = game.world.Get<engine::gameplay::Owner>(entity))
		ScoreCapture(game, owner->player, entity);
}

inline void ChangeTeam(GameWorld &game, ecs::Entity entity, std::uint32_t team)
{
	namespace gp = engine::gameplay;
	auto *member = game.world.Get<gp::TeamMember>(entity);
	if (member == nullptr || member->team == team || team >= game.roster.TeamCount())
		return;
	game.roster.Leave(member->team, entity);
	member->team = team;
	game.roster.Join(team, entity);
	if (auto *owner = game.world.Get<gp::Owner>(entity))
	{
		const std::uint32_t from = owner->player;
		owner->player = game.roster.TeamAt(team).owner;
		// Player::becomingTeamMember's battle plan bonuses, and a Strategy Center's plan (onCapture).
		MoveBattlePlan(game, entity, from, owner->player);
	}
	// Its AI takes the new team's attitude, and its attack priority set when it has one.
	if (auto *aggression = game.world.Get<gp::Aggression>(entity))
	{
		aggression->attitude = TeamAttitude(game, team);
		if (const std::uint16_t set = TeamPrioritySet(game, team); set != 0)
			aggression->prioritySet = set;
	}
	OnCapture(game, entity);
}

template<typename Visit>
void ForTeam(GameWorld &game, const std::string &team, Visit &&visit)
{
	if (const auto index = ResolveTeam(game, team))
	{
		const std::vector<ecs::Entity> members = game.roster.TeamAt(*index).members; // visits may change membership
		for (const ecs::Entity entity : members)
			visit(entity);
	}
}

// The centre of the team's members (the scripts' iterate_TeamMemberList average); none for an empty team.
std::optional<FixedVector2> TeamCentre(GameWorld &game, std::uint32_t team)
{
	const auto &members = game.roster.TeamAt(team).members;
	FixedVector2 centre;
	std::int64_t count = 0;
	for (const ecs::Entity entity : members)
		if (const auto *at = game.world.IsAlive(entity) ? game.world.Get<gameplay::Transform>(entity) : nullptr)
		{
			centre += at->position.XY();
			++count;
		}
	if (count == 0)
		return std::nullopt;
	return centre / Fixed::FromInt(count);
}

// doTeamFollowWaypoints: along the labelled path from its waypoint closest to the team's centre, the same waypoint for
// every member (groupFollowWaypointPath / ...AsTeam both take the one waypoint); none on the path: nothing.
void TeamFollowWaypoints(GameWorld &game, const std::string &team, const std::string &label, bool asTeam, bool exact = false)
{
	(void)asTeam; // groupFollowWaypointPathAsTeam's keeping together is not ported yet: both follow the path
	const auto teamIndex = ResolveTeam(game, team);
	const auto centre = teamIndex ? TeamCentre(game, *teamIndex) : std::nullopt;
	if (!centre || game.waypoints.ClosestOnPath(*centre, label) == gameplay::WaypointGraph::None)
		return;
	ForTeam(game, team, [&](ecs::Entity entity) { OrderFollowPath(game, entity, label, *centre, exact); });
}

// doTeamFollowSkirmishApproachPath (SKIRMISH_FOLLOW_APPROACH_PATH) / doTeamMoveToSkirmishApproachPath
// (SKIRMISH_MOVE_TO_APPROACH_PATH): the path labelled `label` followed by the skirmish enemy's start position (1-based,
// getMpStartIndex()+1): followed from the waypoint closest to the team's centre, or that waypoint moved to
// (groupMoveToPosition). No enemy, no members or no such path: nothing. (checkBridges, the AI's repair of broken bridges
// on the way, waits on bridges.)
std::string ApproachPathLabel(const std::string &label, std::int64_t enemyStartIndex) { return label + std::to_string(enemyStartIndex + 1); }

void TeamFollowApproachPath(GameWorld &game, const std::string &team, const std::string &label, std::int64_t enemyStartIndex, bool asTeam)
{
	TeamFollowWaypoints(game, team, ApproachPathLabel(label, enemyStartIndex), asTeam);
}

void TeamMoveToApproachPath(GameWorld &game, const std::string &team, const std::string &label, std::int64_t enemyStartIndex)
{
	const auto teamIndex = ResolveTeam(game, team);
	const auto centre = teamIndex ? TeamCentre(game, *teamIndex) : std::nullopt;
	if (!centre)
		return;
	const std::uint32_t way = game.waypoints.ClosestOnPath(*centre, ApproachPathLabel(label, enemyStartIndex));
	if (way == gameplay::WaypointGraph::None)
		return;
	const FixedVector2 destination = game.waypoints.Position(way).XY();
	ForTeam(game, team, [&](ecs::Entity entity) { OrderMove(game, entity, destination); });
}

// Player::setUnitsShouldIdleOrResume (IDLE_ALL_UNITS / RESUME_SUPPLY_TRUCKING, for each human player, or the one named):
// each of the player's things not a STRUCTURE with an AI, in its teams' order: to idle, told to move where it stands
// (aiMoveToPosition, CMD_FROM_SCRIPT); to resume, an idle supply truck wants to harvest again (setForceWantingState).
void SetUnitsIdleOrResume(GameWorld &game, std::uint32_t player, bool idle)
{
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
	{
		if (game.roster.TeamAt(team).owner != player)
			continue;
		const std::vector<ecs::Entity> members = game.roster.TeamAt(team).members;
		for (const ecs::Entity unit : members)
		{
			const auto *ref = game.world.IsAlive(unit) ? game.world.Get<gameplay::DefinitionRef>(unit) : nullptr;
			if (ref == nullptr || game.templates.DefinitionAt(ref->index).Is("STRUCTURE") || !HasAi(game, unit))
				continue;
			if (idle)
			{
				if (const auto *where = game.world.Get<gameplay::Transform>(unit))
					OrderMove(game, unit, where->position.XY());
			}
			else if (IsIdle(game, unit))
				if (auto *harvester = game.world.Get<gameplay::Harvester>(unit))
					harvester->forceWanting = true;
		}
	}
}

// doNamedHunt (NAMED_HUNT): the unit hunts (aiHunt, CMD_FROM_SCRIPT).
void UnitHunt(GameWorld &game, ecs::Entity unit)
{
	if (!game.world.IsAlive(unit) || !HasAi(game, unit))
		return;
	Commanded(game, unit);
	if (auto *aggression = game.world.Get<gameplay::Aggression>(unit))
		aggression->stance = gameplay::Stance::Hunt;
}

// ScriptActions::doCreateObject (CREATE_OBJECT, UNIT_SPAWN_NAMED_LOCATION_ORIENTATION): as createUnitOnTeamAt, at `at`
// (its height too) facing `angle` (radians).
ecs::Entity CreateObjectAt(GameWorld &game, const std::string &name, const std::string &type, const std::string &team,
	Engine::Math::FixedVector3 at, Fixed angle)
{
	if (!name.empty())
		if (const ecs::Entity old = game.names.Find(name); game.world.IsAlive(old) && !EffectivelyDead(game, old))
			return {};
	const std::optional<std::uint32_t> index = TeamToCreateOn(game, team);
	if (!index || game.templates.Content().objects.Find(type) == nullptr)
		return {};
	const ecs::Entity made = SpawnObject(game, type, at.XY(), Engine::Math::TurnFromRadians(angle), *index, name);
	if (auto *transform = game.world.IsAlive(made) ? game.world.Get<gameplay::Transform>(made) : nullptr)
		transform->position.z = at.z;
	return made;
}

void TeamHunt(GameWorld &game, const std::string &team)
{
	ForTeam(game, team, [&](ecs::Entity entity) {
		Commanded(game, entity);
		if (auto *aggression = game.world.Get<gameplay::Aggression>(entity))
			aggression->stance = gameplay::Stance::Hunt;
	});
}

// Team::deleteTeam: its members destroyed (ignoreDead: those already dying left to die); a player's default team first
// lets everyone out of its containers (so a garrison's occupants are not what goes).
void TeamDelete(GameWorld &game, const std::string &team, bool ignoreDead = false)
{
	const auto index = ResolveTeam(game, team);
	if (!index)
		return;
	const std::uint32_t owner = game.roster.TeamAt(*index).owner;
	if (game.roster.DefaultTeam(owner) == index)
		ForTeam(game, team, [&](ecs::Entity entity) {
			const auto aboard = game.manifest.Aboard(entity);
			if (!aboard.empty())
				selling_detail::PutOut(game, entity, {aboard.begin(), aboard.end()});
		});
	std::vector<ecs::Entity> members;
	ForTeam(game, team, [&](ecs::Entity entity) {
		if (!ignoreDead || !EffectivelyDead(game, entity))
			members.push_back(entity);
	});
	RetireNow(game, std::move(members));
}

// Team::killTeam: everyone out of its containers (evacuateTeam), then each living member killed, a tech building handed
// to the neutral player's default team instead.
// Team::evacuateTeam: everyone aboard its living containers put out.
void TeamEvacuate(GameWorld &game, const std::string &team)
{
	ForTeam(game, team, [&](ecs::Entity entity) {
		if (EffectivelyDead(game, entity))
			return;
		const auto aboard = game.manifest.Aboard(entity);
		if (!aboard.empty())
			selling_detail::PutOut(game, entity, {aboard.begin(), aboard.end()});
	});
}

void TeamKill(GameWorld &game, const std::string &team)
{
	TeamEvacuate(game, team);
	const std::optional<std::uint32_t> neutral = game.roster.FindTeam("team");
	ForTeam(game, team, [&](ecs::Entity entity) {
		if (EffectivelyDead(game, entity))
			return;
		const auto *ref = game.world.Get<gameplay::DefinitionRef>(entity);
		if (ref != nullptr && game.templates.DefinitionAt(ref->index).Is("TECH_BUILDING") && neutral)
			ChangeTeam(game, entity, *neutral);
		else
			KillNow(game, entity);
	});
}

// Team::damageTeamMembers: every living member takes `amount` (below zero: killed outright).
void TeamDamage(GameWorld &game, const std::string &team, Fixed amount)
{
	ForTeam(game, team, [&](ecs::Entity entity) {
		if (EffectivelyDead(game, entity))
			return;
		if (amount < Fixed{})
			KillNow(game, entity);
		else
			DamageNow(game, entity, amount);
	});
}

void TeamMergeInto(GameWorld &game, const std::string &source, const std::string &target);

// doTeamStop: the team idles (groupIdle); disbanding, it then joins its player's default team (where anyone may recruit
// it: setIsRecruitable, doMergeTeamIntoTeam).
void TeamStop(GameWorld &game, const std::string &team, bool disband)
{
	ForTeam(game, team, [&](ecs::Entity entity) { OrderStop(game, entity); });
	if (!disband)
		return;
	if (const auto index = ResolveTeam(game, team))
		if (const auto home = game.roster.DefaultTeam(game.roster.TeamAt(*index).owner); home && *home != *index)
			TeamMergeInto(game, "#" + std::to_string(*index), "#" + std::to_string(*home));
}

// updateNamedSetAttitude / updateTeamSetAttitude (NAMED_ / TEAM_SET_ATTITUDE): the unit's AI (each of the team's members
// with one: AIGroup::setAttitude) takes the attitude (AttitudeType, sleep -2 ... aggressive 2).
void SetAttitude(GameWorld &game, ecs::Entity unit, std::int64_t attitude)
{
	if (!game.world.IsAlive(unit) || !HasAi(game, unit))
		return;
	if (auto *aggression = game.world.Get<gameplay::Aggression>(unit))
		aggression->attitude = static_cast<std::int8_t>(attitude);
}

void TeamSetAttitude(GameWorld &game, const std::string &team, std::int64_t attitude)
{
	ForTeam(game, team, [&](ecs::Entity entity) { SetAttitude(game, entity, attitude); });
}

// ScriptActions::createUnitOnTeamAt (CREATE_NAMED_ / CREATE_UNNAMED_ON_TEAM_AT_WAYPOINT): a named one only while no
// living unit (not effectively dead) has the name; the team as the scripts name it, or a new instance of it made and
// set active when it has none (TeamFactory::createTeam); the object at the waypoint (none: where it was made, the map's
// origin), its script name (taken over from a dead namesake: transferObjectName) given. Unknown team or type: nothing.
ecs::Entity CreateOnTeamAt(GameWorld &game, const std::string &name, const std::string &type, const std::string &team, const std::string &waypoint)
{
	if (!name.empty())
		if (const ecs::Entity old = game.names.Find(name); game.world.IsAlive(old) && !EffectivelyDead(game, old))
			return {};
	const std::optional<std::uint32_t> index = TeamToCreateOn(game, team);
	if (!index || game.templates.Content().objects.Find(type) == nullptr)
		return {};
	const std::uint32_t at = game.waypoints.Find(waypoint);
	const FixedVector2 where = at != gameplay::WaypointGraph::None ? game.waypoints.Position(at).XY() : FixedVector2{};
	return SpawnObject(game, type, where, {}, *index, name);
}

// doMoveToWaypoint (MOVE_TEAM_TO): the team moves to the waypoint (groupMoveToPosition).
void TeamMoveTo(GameWorld &game, const std::string &team, const std::string &waypoint)
{
	const std::uint32_t at = game.waypoints.Find(waypoint);
	if (at == gameplay::WaypointGraph::None)
		return;
	const FixedVector2 destination = game.waypoints.Position(at).XY();
	if (const auto index = ResolveTeam(game, team))
	{
		// The team as an AIGroup (getTeamAsAIGroup: newest first).
		const auto &members = game.roster.TeamAt(*index).members;
		const std::vector<ecs::Entity> group(members.rbegin(), members.rend());
		GroupMoveToPosition(game, group, destination, false);
	}
}

// doLoadAllTransports (TEAM_LOAD_TRANSPORTS): the team's transports (KINDOF_TRANSPORT, by their slots) and the rest (by
// the slots each takes), the largest of each first, parted out by PartitionSolver's fast solution: each unit into the
// first transport with room left, those that fit nowhere left out; each placed unit told to board its transport. Equal
// sizes keep their team order (the original's unstable sort left it open).
void TeamLoadTransports(GameWorld &game, const std::string &team)
{
	struct Place
	{
		ecs::Entity entity;
		std::int64_t size;
	};
	std::vector<Place> units, transports;
	ForTeam(game, team, [&](ecs::Entity entity) {
		const auto *ref = game.world.IsAlive(entity) ? game.world.Get<gameplay::DefinitionRef>(entity) : nullptr;
		if (ref == nullptr)
			return;
		if (game.templates.DefinitionAt(ref->index).Is("TRANSPORT"))
		{
			if (const auto *transport = game.world.Get<gameplay::Transport>(entity))
				transports.push_back({entity, static_cast<std::int64_t>(transport->definition.slots)});
			return;
		}
		const auto *size = game.world.Get<gameplay::CargoSize>(entity);
		units.push_back({entity, size != nullptr ? static_cast<std::int64_t>(size->slots) : 0}); // getTransportSlotCount: none, 0
	});
	const auto larger = [](const Place &a, const Place &b) { return a.size > b.size; };
	std::ranges::stable_sort(units, larger);
	std::ranges::stable_sort(transports, larger);
	for (const Place &unit : units)
		for (Place &transport : transports)
			if (unit.size <= transport.size)
			{
				transport.size -= unit.size;
				OrderBoard(game, unit.entity, transport.entity);
				break;
			}
}

// Player::killPlayer: every team of the player emptied (evacuateTeam), then every one killed (killTeam).
void PlayerKill(GameWorld &game, std::uint32_t player)
{
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
		if (game.roster.TeamAt(team).owner == player)
			TeamEvacuate(game, "#" + std::to_string(team));
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
		if (game.roster.TeamAt(team).owner == player)
			TeamKill(game, "#" + std::to_string(team));
}

// Player::transferAssetsFromThat: `from`'s player upgrades in production that `to` has or is making are cancelled
// (their money back), what `from` is making counts as `to`'s too (both TheSuperHackers fixes: no upgrade bought twice);
// everything on `from`'s teams goes onto `to`'s default team; then all `from`'s money is `to`'s (not counted as earned).
void PlayerTransferAssets(GameWorld &game, std::uint32_t to, std::uint32_t from)
{
	namespace gp = engine::gameplay;
	const auto home = game.roster.DefaultTeam(to);
	if (!home)
		return;
	std::vector<ecs::Entity> owned;
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
		if (game.roster.TeamAt(team).owner == from)
			for (const ecs::Entity member : game.roster.TeamAt(team).members)
				owned.push_back(member);
	auto &upgrades = game.world.Resource<gp::PlayerUpgrades>();
	const auto &catalog = game.templates.Content().upgrades.upgrades;
	for (std::uint32_t bit = 0; bit < catalog.size(); ++bit)
		if (catalog[bit].player && upgrades.InProduction(from, bit) && (upgrades.Completed(to).Has(bit) || upgrades.InProduction(to, bit)))
			for (const ecs::Entity entity : owned)
				CancelResearch(game, entity, catalog[bit].name);
	for (std::uint32_t bit = 0; bit < catalog.size(); ++bit)
		if (upgrades.InProduction(from, bit))
			upgrades.StartProduction(to, bit);
	for (const ecs::Entity entity : owned)
		if (game.world.IsAlive(entity))
			ChangeTeam(game, entity, *home);
	auto &money = game.world.Resource<gp::PlayerMoney>();
	const std::int64_t all = money.Balance(from);
	money.Withdraw(from, all);
	money.Deposit(to, all);
}

// GameLogic::onSelfDestruct (MSG_SELF_DESTRUCT: the quit menu's Surrender): with `transfer`, the first other player
// that is mutually allied with it (toward its default team) and not beaten takes its assets, and it is killed; with no
// such ally, or without `transfer`, it is killed.
void PlayerSelfDestruct(GameWorld &game, std::uint32_t player, bool transfer)
{
	namespace gp = engine::gameplay;
	if (transfer)
	{
		const auto *relationships = game.world.FindResource<gp::Relationships>();
		const auto &outcome = game.world.Resource<gp::MatchOutcome>();
		const auto ownTeam = game.roster.DefaultTeam(player);
		for (std::uint32_t other = 0; relationships != nullptr && other < game.roster.PlayerCount(); ++other)
		{
			const auto otherTeam = game.roster.DefaultTeam(other);
			if (other == player ||
				!relationships->Allies(gp::Relationships::NoTeam, player, otherTeam.value_or(gp::Relationships::NoTeam), other) ||
				!relationships->Allies(gp::Relationships::NoTeam, other, ownTeam.value_or(gp::Relationships::NoTeam), player))
				continue;
			// VictoryConditions::hasSinglePlayerBeenDefeated (VICTORY_NOBUILDINGS: nothing standing that counts; with no
			// victory conditions, outside a match, no one).
			const gp::MatchStanding *standing = outcome.Of(other);
			if (outcome.enabled && standing != nullptr && standing->defeated)
				continue;
			PlayerTransferAssets(game, other, player);
			PlayerKill(game, player); // what did not go (beacons and the like)
			return;
		}
	}
	PlayerKill(game, player);
}

void TeamMergeInto(GameWorld &game, const std::string &source, const std::string &target)
{
	const auto from = ResolveTeam(game, source);
	const auto to = ResolveTeam(game, target);
	if (!from || !to || *from == *to)
		return;
	ForTeam(game, source, [&](ecs::Entity entity) {
		game.roster.Leave(*from, entity);
		game.roster.Join(*to, entity);
		if (auto *member = game.world.Get<gameplay::TeamMember>(entity))
			*member = {*to};
		if (auto *owner = game.world.Get<gameplay::Owner>(entity))
			*owner = {game.roster.TeamAt(*to).owner};
		OnCapture(game, entity);
	});
}

bool TeamCreated(const GameWorld &game, const std::string &team)
{
	// isCreated: set when the team is first activated, until the teams next update (after the scripts).
	const auto index = ResolveTeam(game, team);
	return index && game.roster.TeamAt(*index).justCreated;
}

// evaluateIsDestroyed: the team named (getTeamNamed makes one of a team with none) has nothing that counts (Team::
// hasAnyObjects: alive, not a projectile, inert thing or mine).
bool TeamDestroyed(const GameWorld &game, const std::string &team)
{
	const auto index = ResolveTeam(game, team);
	if (!index)
		return false;
	for (const ecs::Entity member : game.roster.TeamAt(*index).members)
	{
		if (!game.world.IsAlive(member) || game.world.Get<gameplay::Dying>(member) != nullptr)
			continue;
		if (const auto *ref = game.world.Get<gameplay::DefinitionRef>(member))
		{
			const auto &definition = game.templates.DefinitionAt(ref->index);
			if (definition.Is("PROJECTILE") || definition.Is("INERT") || definition.Is("MINE"))
				continue;
		}
		return false;
	}
	return true;
}

std::int64_t PlayerObjectCount(const GameWorld &game, const std::string &player, const std::string &type)
{
	const auto playerIndex = game.roster.FindPlayer(player);
	if (!playerIndex)
		return 0;
	std::int64_t count = 0;
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
	{
		const gameplay::Team &info = game.roster.TeamAt(team);
		if (info.owner != *playerIndex)
			continue;
		for (const ecs::Entity entity : info.members)
			if (const auto *definition = game.world.Get<gameplay::DefinitionRef>(entity))
				count += game.templates.DefinitionAt(definition->index).name == type;
	}
	return count;
}
}
