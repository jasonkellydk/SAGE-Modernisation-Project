export module games.generalszh.scripting.unit_vocabulary;
import std;

export import engine.scripting.runtime.script_runtime;
import games.generalszh.scripting.script_parameters;
import games.generalszh.scripting.core_vocabulary;

// The Zero Hour script actions and conditions on units and teams, mapped
// onto whatever runs the simulation through UnitScriptHost (the session).
// Names are the level's script names; teams and players by name.
export namespace generalszh::scripting
{
class UnitScriptHost
{
public:
	virtual ~UnitScriptHost() = default;

	// Units by script name.
	virtual void CreateNamedOnTeam(const std::string &name, const std::string &type, const std::string &team, const std::string &waypoint) = 0;
	virtual void DeleteNamed(const std::string &name) = 0;
	virtual void KillNamed(const std::string &name) = 0;
	virtual void NamedFollowWaypoints(const std::string &name, const std::string &pathLabel) = 0;
	virtual void MoveNamedTo(const std::string &name, const std::string &waypoint) = 0;

	// Teams.
	virtual void CreateReinforcements(const std::string &team, const std::string &waypoint) = 0;
	virtual void TeamFollowWaypoints(const std::string &team, const std::string &pathLabel, bool asTeam) = 0;
	// The skirmish approach path `pathLabel` toward the calling player's skirmish enemy: followed, or its closest
	// waypoint moved to.
	virtual void TeamFollowApproachPath(std::size_t participant, const std::string &team, const std::string &pathLabel, bool asTeam) = 0;
	virtual void TeamMoveToApproachPath(std::size_t participant, const std::string &team, const std::string &pathLabel) = 0;
	// The unit's / team's AI attitude (AttitudeType: sleep -2 ... aggressive 2).
	virtual void NamedSetAttitude(const std::string &unit, std::int64_t attitude) = 0;
	virtual void TeamSetAttitude(const std::string &team, std::int64_t attitude) = 0;
	// Relationship overrides by team (Team::setOverrideTeamRelationship / setOverridePlayerRelationship,
	// Player::setTeamRelationship): `relationship` as the original's (0 enemies, 1 neutral, 2 allies); none removes it.
	enum class OverrideKind : std::uint8_t
	{
		TeamToTeam,
		TeamToPlayer,
		PlayerToTeam,
	};
	virtual void SetRelationOverride(std::size_t participant, OverrideKind kind, const std::string &from, const std::string &to,
		std::optional<std::int64_t> relationship) = 0;
	virtual void ClearTeamOverrides(const std::string &team) = 0;
	// doNamedFollowWaypointsExact / doTeamFollowWaypointsExact (aiFollowWaypointPathExact).
	virtual void FollowWaypointsExact(const std::string &unitOrTeam, const std::string &pathLabel, bool team, bool asTeam) = 0;
	// aiAttackTeam: the unit (NAMED_ATTACK_TEAM) or the team's AI group (TEAM_ATTACK_TEAM) attacks the other team's
	// members one after another; CHOOSE_VICTIM_ALWAYS_USES_NORMAL.
	virtual void NamedAttackTeam(const std::string &unit, const std::string &team) = 0;
	virtual void TeamAttackTeam(const std::string &team, const std::string &victims) = 0;
	virtual void ChooseVictimAlwaysUsesNormal(bool enable) = 0;
	// Player::setUnitsShouldIdleOrResume for every human player (IDLE_ALL_UNITS / RESUME_SUPPLY_TRUCKING).
	virtual void HumansIdleOrResume(bool idle) = 0;
	// aiWander / aiPanic along the path for each member (doTeamWander / doTeamPanic) and aiWanderInPlace (doTeamWanderInPlace).
	virtual void TeamWander(const std::string &team, const std::string &pathLabel, bool panic) = 0;
	virtual void TeamWanderInPlace(const std::string &team) = 0;
	// NAMED_CUSTOM_COLOR: the unit's colour in place of its player's (0xAARRGGBB; 0: its player's).
	virtual void NamedCustomColor(const std::string &unit, std::uint32_t argb) = 0;
	// OBJECT_ALLOW_BONUSES: whether objects get the single-player difficulty bonus; DELETE_ALL_UNMANNED.
	virtual void AllowBonuses(bool allow) = 0;
	virtual void DeleteAllUnmanned() = 0;
	// changeObjectPanelFlagForSingleObject for the unit or each of the team's members.
	virtual void ObjectPanelFlag(const std::string &unitOrTeam, bool team, const std::string &flag, bool value) = 0;
	// The unit's special power countdown: 0 set to seconds from now, 1 seconds added, 2 stopped, 3 started again.
	virtual void NamedPowerCountdown(const std::string &unit, const std::string &power, int edit, std::int64_t seconds) = 0;
	// doNamedHunt; doTeamAttackNamed (the team as an AIGroup attacks the unit); doCreateObject.
	virtual void NamedHunt(const std::string &unit) = 0;
	virtual void TeamAttackNamed(const std::string &team, const std::string &unit) = 0;
	virtual void CreateObject(const std::string &name, const std::string &type, const std::string &team, Engine::Math::FixedVector3 at,
		Engine::Math::Fixed angle) = 0;
	// The team guards its player's tunnel network.
	virtual void TeamGuardInTunnelNetwork(const std::string &team) = 0;
	// The team goes to take over the nearest unmanned vehicle not an ally's.
	virtual void TeamCaptureNearestUnowned(const std::string &team) = 0;
	virtual void TeamDelete(const std::string &team) = 0;
	virtual void TeamMergeInto(const std::string &source, const std::string &target) = 0;
	virtual bool TeamCreated(const std::string &team) const = 0;
	// Team::setState / getState (none: no such team); UNIT_EMPTIED (evaluateUnitHasEmptied); whether the team's
	// player is the named one (evaluateTeamOwnedByPlayer).
	virtual void SetTeamState(const std::string &team, const std::string &state) = 0;
	virtual std::optional<std::string> TeamState(const std::string &team) const = 0;
	virtual bool UnitEmptied(const std::string &unit) = 0;
	virtual bool TeamOwnedBy(const std::string &team, std::size_t participant, const std::string &player) = 0;
	virtual bool TeamDestroyed(const std::string &team) const = 0;
	// Whether the unit / all or some of the team's members have the object status (by name).
	virtual bool NamedHasObjectStatus(const std::string &unit, const std::string &status) = 0;
	virtual bool TeamHasObjectStatus(const std::string &team, const std::string &status, bool entireTeam) = 0;

	// Transports.
	virtual void TeamEnterNamed(const std::string &team, const std::string &transport) = 0;
	virtual void NamedGarrisonSpecific(const std::string &unit, const std::string &building) = 0;
	virtual void TeamGarrisonSpecific(const std::string &team, const std::string &building) = 0;
	virtual void NamedGarrisonNearest(const std::string &unit) = 0;
	virtual void TeamGarrisonNearest(const std::string &team) = 0;
	virtual void PlayerGarrisonAll(const std::string &player) = 0;
	virtual void NamedExitAll(const std::string &transport) = 0;
	// doNamedEnterNamed; doNamedFaceWaypoint / doNamedFaceNamed and the team's (a waypoint, or an object when named);
	// OBJECT_STATUS_REPULSOR on a unit or a team's members; doNamedEnableStealth.
	virtual void NamedEnterNamed(const std::string &unit, const std::string &transport) = 0;
	virtual void NamedFace(const std::string &unit, const std::string &waypoint, const std::string &object) = 0;
	virtual void TeamFace(const std::string &team, const std::string &waypoint, const std::string &object) = 0;
	virtual void SetRepulsor(const std::string &unit, bool on) = 0;
	virtual void TeamSetRepulsor(const std::string &team, bool on) = 0;
	virtual void SetStealthEnabled(const std::string &unit, bool enabled) = 0;
	// doTeamEnableStealth: OBJECT_STATUS_SCRIPT_UNSTEALTHED on each member as doNamedEnableStealth.
	virtual void TeamSetStealthEnabled(const std::string &team, bool enabled) = 0;
	// NAMED_SET_BOOBYTRAPPED(object type, unit) / TEAM_SET_BOOBYTRAPPED(object type, team): doNamedSetBoobytrapped /
	// doTeamSetBoobytrapped.
	virtual void NamedSetBoobytrapped(const std::string &type, const std::string &unit) = 0;
	virtual void TeamSetBoobytrapped(const std::string &type, const std::string &team) = 0;
	// doUnitExitBuilding / doTeamExitAllBuildings / doExitSpecificBuilding; doNamedGuard; doNamedSetUnmanned /
	// doTeamSetUnmanned; doTeamUseCommandButtonAbilityAtWaypoint (each member).
	virtual void NamedExitBuilding(const std::string &unit) = 0;
	virtual void TeamExitAllBuildings(const std::string &team) = 0;
	virtual void ExitSpecificBuilding(const std::string &building) = 0;
	// PLAYER_ENABLE_FACTORIES / PLAYER_DISABLE_FACTORIES(player, object type): Player::setObjectsEnabled.
	virtual void PlayerObjectsEnabled(const std::string &player, const std::string &type, bool enable) = 0;
	// NAMED_SET_STOPPING_DISTANCE(unit, distance) / SET_STOPPING_DISTANCE(team, distance).
	virtual void NamedStoppingDistance(const std::string &unit, Engine::Math::Fixed distance) = 0;
	virtual void TeamStoppingDistance(const std::string &team, Engine::Math::Fixed distance) = 0;
	// doNamedGuard; false when nothing was done (no such unit with an AI).
	virtual bool NamedGuard(const std::string &unit) = 0;
	virtual void SetUnmanned(const std::string &unit) = 0;
	virtual void TeamSetUnmanned(const std::string &team) = 0;
	virtual void TeamUseCommandButtonAtWaypoint(const std::string &team, const std::string &button, const std::string &waypoint) = 0;
	virtual void TeamExitAll(const std::string &team) = 0;

	// Special powers.
	virtual void NamedUseCommandButtonAtWaypoint(const std::string &name, const std::string &button, const std::string &waypoint) = 0;
	virtual void NamedFireSpecialPowerAtWaypoint(const std::string &name, const std::string &power, const std::string &waypoint) = 0;
	virtual void NamedFireSpecialPowerAtNamed(const std::string &name, const std::string &power, const std::string &target) = 0;
	// hide / showObjectSuperweaponDisplayByScript: the unit's countdowns on screen.
	virtual void NamedHideSpecialPowerDisplay(const std::string &name, bool hide) = 0;

	// Combat.
	virtual void TeamHunt(const std::string &team) = 0;
	// The team's player builds it (an AI's factories), as the original's BUILD_TEAM.
	virtual void BuildTeam(const std::string &team) = 0;
	virtual void RecruitTeam(const std::string &team, Engine::Math::Fixed radius) = 0;
	virtual void ChangeTeamPriority(const std::string &team, bool success) = 0;
	virtual void TeamGuard(const std::string &team, const std::string &waypoint) = 0;
	// doTeamGuardObject(team, unit); doGuardSupplyCenter(team, supplies).
	virtual void TeamGuardObject(const std::string &team, const std::string &unit) = 0;
	virtual void TeamGuardSupplyCenter(const std::string &team, std::int64_t supplies) = 0;
	virtual void NamedAttackNamed(const std::string &attacker, const std::string &target) = 0;
	virtual void NamedSetHeld(const std::string &name, bool held) = 0;
	virtual void NamedStop(const std::string &name) = 0;
	// aiIdle(CMD_FROM_SCRIPT) on a unit with an AI (false: none).
	virtual bool NamedIdle(const std::string &name) = 0;
	virtual void TeamStop(const std::string &team, bool disband) = 0;
	virtual void TeamMoveTo(const std::string &team, const std::string &waypoint) = 0;
	virtual void NamedDamage(const std::string &name, std::int64_t amount) = 0;
	virtual void TeamDamage(const std::string &team, Engine::Math::Fixed amount) = 0;
	virtual void TeamKill(const std::string &team) = 0;
	virtual void TeamDeleteLiving(const std::string &team) = 0;
	virtual void PlayerKill(const std::string &player) = 0;
	// doPlayerTransferAssetsToPlayer: Player::transferAssetsFromThat (the first player's to the second).
	virtual void PlayerTransferAssets(const std::string &from, const std::string &to) = 0;
	// doDestroyAllContained: everything the unit holds killed.
	virtual void DestroyAllContained(const std::string &unit) = 0;
	// evaluateNamedSelected: the local player has the named unit selected (never in a multiplayer game).
	virtual bool NamedSelected(const std::string &unit) = 0;
	// ScriptEngine::setToppleDirection: the named thing falls that way when it topples.
	virtual void SetToppleDirection(const std::string &unit, Engine::Math::FixedVector3 direction) = 0;
	virtual void NamedTransferToPlayer(const std::string &name, const std::string &player) = 0;
	virtual void TeamTransferToPlayer(const std::string &team, const std::string &player) = 0;
	// Money::deposit / withdraw (a withdrawal takes at most what there is).
	virtual void PlayerGiveMoney(const std::string &player, std::int64_t amount) = 0;
	virtual void PlayerSetMoney(const std::string &player, std::int64_t amount) = 0;
	// Trigger areas (ScriptConditions' area conditions): `test` of the unit or team `subject` against the area (the
	// skirmish perimeter names resolved for the script's player), members of the surfaces asked (1 ground, 2 air).
	enum class AreaQuery : std::uint8_t
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
	virtual bool InArea(std::size_t participant, AreaQuery test, const std::string &subject, const std::string &area, std::int64_t surfaces) = 0;
	// What `player` has in the area (none: no such player or area; see CountInArea): 0 of the types, 1 of the kind,
	// 2 anything, 3 their build cost. AreaExists: the area (as the script's player names it) is on the map.
	virtual std::optional<std::int64_t> CountInArea(std::size_t participant, int what, const std::string &player, const std::string &area,
		const std::string &types, std::int64_t kind) = 0;
	virtual bool AreaExists(std::size_t participant, const std::string &area) = 0;
	// TEAM_ATTACK_AREA / NAMED_ATTACK_AREA / TEAM_GUARD_AREA: the team (or unit) attacks or guards the area.
	virtual void AreaOrder(std::size_t participant, const std::string &subject, bool team, const std::string &area, bool guard) = 0;
	// Team::setRecruitable (TEAM_AVAILABLE_FOR_RECRUITMENT): whether other teams may recruit its units, over its template's.
	virtual void TeamRecruitable(const std::string &team, bool recruitable) = 0;
	// SKIRMISH_TECH_BUILDING_WITHIN_DISTANCE(player, distance, area); SKIRMISH_UNOWNED_FACTION_UNIT_EXISTS(player,
	// comparison, count): the neutral player's unmanned things; SUPPLY_SOURCE_SAFE(player, minimum supplies).
	virtual bool TechBuildingNear(std::size_t participant, const std::string &player, Engine::Math::Fixed distance, const std::string &area) = 0;
	// SKIRMISH_SUPPLIES_VALUE_WITHIN_DISTANCE(player, distance, area, value).
	virtual bool SuppliesNear(std::size_t participant, const std::string &player, Engine::Math::Fixed distance, const std::string &area,
		Engine::Math::Fixed value) = 0;
	virtual std::int64_t UnownedFactionUnits() = 0;
	// PLAYER_SELL_EVERYTHING(player): Player::sellEverythingUnderTheSun.
	virtual void PlayerSellEverything(const std::string &player) = 0;
	// TEAM_LOAD_TRANSPORTS(team); TEAM_MOVE_TOWARDS_NEAREST_OBJECT_TYPE(team, type or list, area).
	virtual void TeamLoadTransports(const std::string &team) = 0;
	// Sequential scripts' teams: the team instance a name means (its sequence's subject), whether it is aboard things
	// (all of it, or any), and it guarding where each member stands (TEAM_GUARD_FOR_FRAMECOUNT).
	virtual std::optional<std::uint32_t> TeamIndex(const std::string &team) = 0;
	virtual bool TeamContained(const std::string &team, bool all) = 0;
	// evaluateSkirmishCommandButtonIsReady: the team's members with the button's power (or upgrade) have it ready: all
	// of them, or any.
	virtual bool TeamCommandButtonReady(const std::string &team, const std::string &button, bool all) = 0;
	// TEAM_USE_COMMANDBUTTON_ABILITY(team, button): each member uses it (AIGroup::groupDoCommandButton);
	// TEAM_PARTIAL_USE_COMMANDBUTTON(percentage, team, button): the first percentage of the members it is valid on.
	virtual void TeamUseCommandButton(const std::string &team, const std::string &button, std::optional<Engine::Math::Fixed> percentage) = 0;
	// Command buttons at objects: NAMED_USE_COMMANDBUTTON_ABILITY(_ON_NAMED)(unit, button[, target]): the unit's own buttons
	// of that name; TEAM_USE_COMMANDBUTTON_ABILITY_ON_NAMED(team, button, target): every member, valid or not;
	// TEAM_ALL_USE_COMMANDBUTTON_ON_NAMED / _ON_NEAREST_...(team, button[, ...]): every member, at a target the button's
	// source member may use it on. `nearest`: ENEMY_UNIT 0, GARRISONED_BUILDING 1, KINDOF 2, ENEMY_BUILDING 3,
	// ENEMY_BUILDING_CLASS 4, OBJECTTYPE 5 (with `kind`, a KindOf bit, or `type`).
	// TEAM_HUNT_WITH_COMMAND_BUTTON(team, button): the members hunt with it (CommandButtonHuntUpdate).
	virtual void TeamHuntWithCommandButton(const std::string &team, const std::string &button) = 0;
	virtual void NamedUseCommandButton(const std::string &unit, const std::string &button, const std::optional<std::string> &target) = 0;
	virtual void TeamUseCommandButtonOnNamed(const std::string &team, const std::string &button, const std::string &target, bool checked) = 0;
	virtual void TeamUseCommandButtonOnNearest(const std::string &team, const std::string &button, int nearest, std::int64_t kind, const std::string &type) = 0;
	// Attack priority sets (ScriptEngine::setPriorityThing / setPriorityKind / setPriorityDefault; updateNamed / Team
	// AttackPrioritySet): a set's priority for a type (or each type of a list) or every template of a kind (a KindOf
	// index); its default; the set a unit or team picks its targets by.
	virtual void SetAttackPriorityThing(const std::string &set, const std::string &type, std::int64_t priority) = 0;
	virtual void SetAttackPriorityKind(const std::string &set, std::int64_t kind, std::int64_t priority) = 0;
	virtual void SetDefaultAttackPriority(const std::string &set, std::int64_t priority) = 0;
	virtual void ApplyAttackPrioritySet(const std::string &subject, bool team, const std::string &set) = 0;
	virtual void TeamMoveTowardsNearest(std::size_t participant, const std::string &team, const std::string &type, const std::string &area) = 0;
	// UNIT_MOVE_TOWARDS_NEAREST_OBJECT_TYPE(unit, type or list, area): doMoveUnitTowardsNearest.
	virtual void UnitMoveTowardsNearest(std::size_t participant, const std::string &unit, const std::string &type, const std::string &area) = 0;
	virtual bool SupplySourceSafe(const std::string &player, std::int64_t minimumSupplies) = 0;
	// evaluateSkirmishSupplySourceAttacked: the player's AI finds a source of supplies under attack.
	virtual bool SupplySourceAttacked(const std::string &player) = 0;

	// Upgrades: the unit gets it as its own (Object::giveUpgrade).
	virtual void NamedReceiveUpgrade(const std::string &name, const std::string &upgrade) = 0;

	// Players.
	virtual void SetRankLimit(std::int64_t level) = 0;
	virtual void EnableUnitConstruction(const std::string &player) = 0;
	// Player::setCanBuildUnits(false) (PLAYER_DISABLE_UNIT_CONSTRUCTION).
	virtual void DisableUnitConstruction(const std::string &player) = 0;
	// SupplyWarehouseDockUpdate::setCashValue: the named warehouse holds what that cash takes, in boxes rounded up.
	virtual void SetWarehouseValue(const std::string &warehouse, std::int64_t cash) = 0;
	// The player researches a player upgrade at one of its buildings (AI_PLAYER_BUILD_UPGRADE).
	virtual void PlayerBuildUpgrade(const std::string &player, const std::string &upgrade) = 0;
	// A player parameter as named by a script of `participant` (ScriptEngine::getPlayerFromAsciiString): "<This
	// Player>" is the script's own player, "<This Player's Enemy>" that player's skirmish enemy; any other name as
	// it is. Empty: no such player.
	virtual std::string PlayerNamed(std::size_t participant, const std::string &name) = 0;
	// A team parameter for a script run for `subject` (a team instance, or NoSubject): how the host names that team
	// ("<This Team>" and the calling team's own name: that instance).
	virtual std::string TeamReference(std::uint64_t subject, const std::string &name) = 0;
	// A unit parameter for a script run for `subject` ("<This Object>": the unit a sequential script runs on), and the
	// sequential-script subject a unit name means (none: not there).
	virtual std::string UnitReference(std::uint64_t subject, const std::string &name) = 0;
	virtual std::optional<std::uint64_t> UnitSubject(const std::string &unit) = 0;
};

// A call's parameter as the host takes it: a team (Parameter::TEAM, 3) through TeamReference, anything else as it is.
inline std::string Arg(UnitScriptHost *host, const engine::scripting::ScriptCallContext &c, std::size_t index)
{
	const std::string &text = parameters::Text(c, index);
	if (index < c.call.parameters.size() && c.call.parameters[index].kind == 3)
		return host->TeamReference(c.subject, text);
	if (index < c.call.parameters.size() && c.call.parameters[index].kind == 14)
		return host->UnitReference(c.subject, text);
	return text;
}

inline void AddUnitVocabulary(engine::scripting::Vocabulary &vocabulary, UnitScriptHost &host)
{
	using engine::scripting::ScriptCallContext;
	using parameters::Integer;
	using parameters::Text;
	UnitScriptHost *units = &host;

	// CREATE_NAMED_ON_TEAM_AT_WAYPOINT(name, object type, team, waypoint)
	vocabulary.AddAction("CREATE_NAMED_ON_TEAM_AT_WAYPOINT",
		[units](ScriptCallContext &c) { units->CreateNamedOnTeam(Arg(units, c, 0), Arg(units, c, 1), Arg(units, c, 2), Arg(units, c, 3)); });
	// CREATE_UNNAMED_ON_TEAM_AT_WAYPOINT(object type, team, waypoint)
	vocabulary.AddAction("CREATE_UNNAMED_ON_TEAM_AT_WAYPOINT",
		[units](ScriptCallContext &c) { units->CreateNamedOnTeam({}, Arg(units, c, 0), Arg(units, c, 1), Arg(units, c, 2)); });
	vocabulary.AddAction("AI_PLAYER_BUILD_UPGRADE", [units](ScriptCallContext &c) { units->PlayerBuildUpgrade(units->PlayerNamed(c.participant, Arg(units, c, 0)), Arg(units, c, 1)); });
	vocabulary.AddAction("NAMED_RECEIVE_UPGRADE", [units](ScriptCallContext &c) { units->NamedReceiveUpgrade(Arg(units, c, 0), Arg(units, c, 1)); });
	vocabulary.AddAction("NAMED_DELETE", [units](ScriptCallContext &c) { units->DeleteNamed(Arg(units, c, 0)); });
	vocabulary.AddAction("NAMED_KILL", [units](ScriptCallContext &c) { units->KillNamed(Arg(units, c, 0)); });
	// NAMED_FOLLOW_WAYPOINTS(name, path label): from the path's closest waypoint.
	vocabulary.AddAction("NAMED_FOLLOW_WAYPOINTS", [units](ScriptCallContext &c) { units->NamedFollowWaypoints(Arg(units, c, 0), Arg(units, c, 1)); });
	vocabulary.AddAction("MOVE_NAMED_UNIT_TO", [units](ScriptCallContext &c) { units->MoveNamedTo(Arg(units, c, 0), Arg(units, c, 1)); });

	// CREATE_REINFORCEMENT_TEAM(team, waypoint)
	vocabulary.AddAction("CREATE_REINFORCEMENT_TEAM", [units](ScriptCallContext &c) { units->CreateReinforcements(Arg(units, c, 0), Arg(units, c, 1)); });
	// TEAM_FOLLOW_WAYPOINTS(team, path label, as team)
	vocabulary.AddAction("TEAM_FOLLOW_WAYPOINTS",
		[units](ScriptCallContext &c) { units->TeamFollowWaypoints(Arg(units, c, 0), Arg(units, c, 1), Integer(c, 2) != 0); });
	// SKIRMISH_FOLLOW_APPROACH_PATH(team, path label, as team); SKIRMISH_MOVE_TO_APPROACH_PATH(team, path label)
	vocabulary.AddAction("SKIRMISH_FOLLOW_APPROACH_PATH",
		[units](ScriptCallContext &c) { units->TeamFollowApproachPath(c.participant, Arg(units, c, 0), Arg(units, c, 1), Integer(c, 2) != 0); });
	vocabulary.AddAction("SKIRMISH_MOVE_TO_APPROACH_PATH",
		[units](ScriptCallContext &c) { units->TeamMoveToApproachPath(c.participant, Arg(units, c, 0), Arg(units, c, 1)); });
	// NAMED_SET_ATTITUDE(unit, attitude); TEAM_SET_ATTITUDE(team, attitude)
	vocabulary.AddAction("NAMED_SET_ATTITUDE", [units](ScriptCallContext &c) { units->NamedSetAttitude(Arg(units, c, 0), Integer(c, 1)); });
	// TEAM_SET_STATE(team, state)
	vocabulary.AddAction("TEAM_SET_STATE", [units](ScriptCallContext &c) { units->SetTeamState(Arg(units, c, 0), Text(c, 1)); });
	vocabulary.AddAction("TEAM_SET_ATTITUDE", [units](ScriptCallContext &c) { units->TeamSetAttitude(Arg(units, c, 0), Integer(c, 1)); });
	// TEAM_SET_ / TEAM_REMOVE_OVERRIDE_RELATION_TO_TEAM(team, other team[, relationship]), TEAM_SET_ /
	// TEAM_REMOVE_OVERRIDE_RELATION_TO_PLAYER(team, player[, relationship]), PLAYER_SET_ /
	// PLAYER_REMOVE_OVERRIDE_RELATION_TO_TEAM(player, team[, relationship]), TEAM_REMOVE_ALL_OVERRIDE_RELATIONS(team).
	using Override = UnitScriptHost::OverrideKind;
	for (const auto &[name, kind, set] :
		{std::tuple{"TEAM_SET_OVERRIDE_RELATION_TO_TEAM", Override::TeamToTeam, true}, std::tuple{"TEAM_REMOVE_OVERRIDE_RELATION_TO_TEAM", Override::TeamToTeam, false},
			std::tuple{"TEAM_SET_OVERRIDE_RELATION_TO_PLAYER", Override::TeamToPlayer, true},
			std::tuple{"TEAM_REMOVE_OVERRIDE_RELATION_TO_PLAYER", Override::TeamToPlayer, false},
			std::tuple{"PLAYER_SET_OVERRIDE_RELATION_TO_TEAM", Override::PlayerToTeam, true},
			std::tuple{"PLAYER_REMOVE_OVERRIDE_RELATION_TO_TEAM", Override::PlayerToTeam, false}})
		vocabulary.AddAction(name, [units, kind, set](ScriptCallContext &c) {
			units->SetRelationOverride(c.participant, kind, Arg(units, c, 0), Arg(units, c, 1), set ? std::optional(Integer(c, 2)) : std::nullopt);
		});
	vocabulary.AddAction("TEAM_REMOVE_ALL_OVERRIDE_RELATIONS", [units](ScriptCallContext &c) { units->ClearTeamOverrides(Arg(units, c, 0)); });
	// NAMED_FOLLOW_WAYPOINTS_EXACT(unit, path label); TEAM_FOLLOW_WAYPOINTS_EXACT(team, path label, as team)
	vocabulary.AddAction("NAMED_FOLLOW_WAYPOINTS_EXACT", [units](ScriptCallContext &c) { units->FollowWaypointsExact(Arg(units, c, 0), Arg(units, c, 1), false, false); });
	vocabulary.AddAction("TEAM_FOLLOW_WAYPOINTS_EXACT",
		[units](ScriptCallContext &c) { units->FollowWaypointsExact(Arg(units, c, 0), Arg(units, c, 1), true, Integer(c, 2) != 0); });
	// UNIT_AFFECT_OBJECT_PANEL_FLAGS(unit, flag, value) / TEAM_AFFECT_OBJECT_PANEL_FLAGS(team, flag, value)
	vocabulary.AddAction("UNIT_AFFECT_OBJECT_PANEL_FLAGS",
		[units](ScriptCallContext &c) { units->ObjectPanelFlag(Arg(units, c, 0), false, Text(c, 1), Integer(c, 2) != 0); });
	vocabulary.AddAction("TEAM_AFFECT_OBJECT_PANEL_FLAGS",
		[units](ScriptCallContext &c) { units->ObjectPanelFlag(Arg(units, c, 0), true, Text(c, 1), Integer(c, 2) != 0); });
	// IDLE_ALL_UNITS / RESUME_SUPPLY_TRUCKING (no parameters: every human player)
	// NAMED_CUSTOM_COLOR(unit, color)
	vocabulary.AddAction("NAMED_CUSTOM_COLOR",
		[units](ScriptCallContext &c) { units->NamedCustomColor(Arg(units, c, 0), static_cast<std::uint32_t>(Integer(c, 1))); });
	// OBJECT_ALLOW_BONUSES(allow); DELETE_ALL_UNMANNED
	vocabulary.AddAction("OBJECT_ALLOW_BONUSES", [units](ScriptCallContext &c) { units->AllowBonuses(Integer(c, 0) != 0); });
	vocabulary.AddAction("DELETE_ALL_UNMANNED", [units](ScriptCallContext &) { units->DeleteAllUnmanned(); });
	vocabulary.AddAction("IDLE_ALL_UNITS", [units](ScriptCallContext &) { units->HumansIdleOrResume(true); });
	vocabulary.AddAction("RESUME_SUPPLY_TRUCKING", [units](ScriptCallContext &) { units->HumansIdleOrResume(false); });
	// NAMED_ATTACK_TEAM(unit, team); TEAM_ATTACK_TEAM(team, team); CHOOSE_VICTIM_ALWAYS_USES_NORMAL(enable)
	vocabulary.AddAction("NAMED_ATTACK_TEAM", [units](ScriptCallContext &c) { units->NamedAttackTeam(Arg(units, c, 0), Arg(units, c, 1)); });
	vocabulary.AddAction("TEAM_ATTACK_TEAM", [units](ScriptCallContext &c) { units->TeamAttackTeam(Arg(units, c, 0), Arg(units, c, 1)); });
	vocabulary.AddAction("CHOOSE_VICTIM_ALWAYS_USES_NORMAL", [units](ScriptCallContext &c) { units->ChooseVictimAlwaysUsesNormal(Integer(c, 0) != 0); });
	// NAMED_SET_ / NAMED_ADD_SPECIAL_POWER_COUNTDOWN(unit, power, seconds); NAMED_STOP_ / NAMED_START_SPECIAL_POWER_COUNTDOWN(unit, power)
	vocabulary.AddAction("NAMED_SET_SPECIAL_POWER_COUNTDOWN", [units](ScriptCallContext &c) { units->NamedPowerCountdown(Arg(units, c, 0), Text(c, 1), 0, Integer(c, 2)); });
	vocabulary.AddAction("NAMED_ADD_SPECIAL_POWER_COUNTDOWN", [units](ScriptCallContext &c) { units->NamedPowerCountdown(Arg(units, c, 0), Text(c, 1), 1, Integer(c, 2)); });
	vocabulary.AddAction("NAMED_STOP_SPECIAL_POWER_COUNTDOWN", [units](ScriptCallContext &c) { units->NamedPowerCountdown(Arg(units, c, 0), Text(c, 1), 2, 0); });
	vocabulary.AddAction("NAMED_START_SPECIAL_POWER_COUNTDOWN", [units](ScriptCallContext &c) { units->NamedPowerCountdown(Arg(units, c, 0), Text(c, 1), 3, 0); });
	// NAMED_HUNT(unit); TEAM_ATTACK_NAMED(team, unit); CREATE_OBJECT(type, team, position, angle);
	// UNIT_SPAWN_NAMED_LOCATION_ORIENTATION(name, type, team, position, angle)
	vocabulary.AddAction("NAMED_HUNT", [units](ScriptCallContext &c) { units->NamedHunt(Arg(units, c, 0)); });
	vocabulary.AddAction("TEAM_ATTACK_NAMED", [units](ScriptCallContext &c) { units->TeamAttackNamed(Arg(units, c, 0), Arg(units, c, 1)); });
	vocabulary.AddAction("CREATE_OBJECT", [units](ScriptCallContext &c) {
		units->CreateObject({}, Arg(units, c, 0), Arg(units, c, 1), parameters::Position(c, 2), parameters::Number(c, 3));
	});
	vocabulary.AddAction("UNIT_SPAWN_NAMED_LOCATION_ORIENTATION", [units](ScriptCallContext &c) {
		units->CreateObject(Arg(units, c, 0), Arg(units, c, 1), Arg(units, c, 2), parameters::Position(c, 3), parameters::Number(c, 4));
	});
	// TEAM_GUARD_IN_TUNNEL_NETWORK(team)
	vocabulary.AddAction("TEAM_GUARD_IN_TUNNEL_NETWORK", [units](ScriptCallContext &c) { units->TeamGuardInTunnelNetwork(Arg(units, c, 0)); });
	// TEAM_CAPTURE_NEAREST_UNOWNED_FACTION_UNIT(team)
	vocabulary.AddAction("TEAM_CAPTURE_NEAREST_UNOWNED_FACTION_UNIT", [units](ScriptCallContext &c) { units->TeamCaptureNearestUnowned(Arg(units, c, 0)); });
	// TEAM_PANIC(team, path label) / TEAM_WANDER(team, path label) / TEAM_WANDER_IN_PLACE(team)
	vocabulary.AddAction("TEAM_PANIC", [units](ScriptCallContext &c) { units->TeamWander(Arg(units, c, 0), Arg(units, c, 1), true); });
	vocabulary.AddAction("TEAM_WANDER", [units](ScriptCallContext &c) { units->TeamWander(Arg(units, c, 0), Arg(units, c, 1), false); });
	vocabulary.AddAction("TEAM_WANDER_IN_PLACE", [units](ScriptCallContext &c) { units->TeamWanderInPlace(Arg(units, c, 0)); });
	vocabulary.AddAction("TEAM_DELETE", [units](ScriptCallContext &c) { units->TeamDelete(Arg(units, c, 0)); });
	// TEAM_MERGE_INTO_TEAM(source, target)
	vocabulary.AddAction("TEAM_MERGE_INTO_TEAM", [units](ScriptCallContext &c) { units->TeamMergeInto(Arg(units, c, 0), Arg(units, c, 1)); });

	// TEAM_ENTER_NAMED(team, transport); NAMED_EXIT_ALL(transport); TEAM_EXIT_ALL(team of transports)
	vocabulary.AddAction("TEAM_ENTER_NAMED", [units](ScriptCallContext &c) { units->TeamEnterNamed(Arg(units, c, 0), Arg(units, c, 1)); });
	// NAMED_ / TEAM_GARRISON_SPECIFIC_BUILDING(unit or team, building); NAMED_ / TEAM_GARRISON_NEAREST_BUILDING(unit or
	// team); PLAYER_GARRISON_ALL_BUILDINGS(player)
	vocabulary.AddAction("NAMED_GARRISON_SPECIFIC_BUILDING", [units](ScriptCallContext &c) { units->NamedGarrisonSpecific(Arg(units, c, 0), Arg(units, c, 1)); });
	vocabulary.AddAction("TEAM_GARRISON_SPECIFIC_BUILDING", [units](ScriptCallContext &c) { units->TeamGarrisonSpecific(Arg(units, c, 0), Arg(units, c, 1)); });
	vocabulary.AddAction("NAMED_GARRISON_NEAREST_BUILDING", [units](ScriptCallContext &c) { units->NamedGarrisonNearest(Arg(units, c, 0)); });
	vocabulary.AddAction("TEAM_GARRISON_NEAREST_BUILDING", [units](ScriptCallContext &c) { units->TeamGarrisonNearest(Arg(units, c, 0)); });
	vocabulary.AddAction("PLAYER_GARRISON_ALL_BUILDINGS", [units](ScriptCallContext &c) { units->PlayerGarrisonAll(units->PlayerNamed(c.participant, Arg(units, c, 0))); });
	// NAMED_ENTER_NAMED(unit, transport)
	vocabulary.AddAction("NAMED_ENTER_NAMED", [units](ScriptCallContext &c) { units->NamedEnterNamed(Arg(units, c, 0), Arg(units, c, 1)); });
	// NAMED_FACE_NAMED(unit, object) / NAMED_FACE_WAYPOINT(unit, waypoint) / TEAM_FACE_NAMED(team, object) /
	// TEAM_FACE_WAYPOINT(team, waypoint)
	vocabulary.AddAction("NAMED_FACE_NAMED", [units](ScriptCallContext &c) { units->NamedFace(Arg(units, c, 0), {}, Arg(units, c, 1)); });
	vocabulary.AddAction("NAMED_FACE_WAYPOINT", [units](ScriptCallContext &c) { units->NamedFace(Arg(units, c, 0), Text(c, 1), {}); });
	vocabulary.AddAction("TEAM_FACE_NAMED", [units](ScriptCallContext &c) { units->TeamFace(Arg(units, c, 0), {}, Arg(units, c, 1)); });
	vocabulary.AddAction("TEAM_FACE_WAYPOINT", [units](ScriptCallContext &c) { units->TeamFace(Arg(units, c, 0), Text(c, 1), {}); });
	// NAMED_SET_REPULSOR(unit, on) / TEAM_SET_REPULSOR(team, on); NAMED_SET_STEALTH_ENABLED(unit, enabled)
	vocabulary.AddAction("NAMED_SET_REPULSOR", [units](ScriptCallContext &c) { units->SetRepulsor(Arg(units, c, 0), Integer(c, 1) != 0); });
	vocabulary.AddAction("TEAM_SET_REPULSOR", [units](ScriptCallContext &c) { units->TeamSetRepulsor(Arg(units, c, 0), Integer(c, 1) != 0); });
	vocabulary.AddAction("NAMED_SET_STEALTH_ENABLED", [units](ScriptCallContext &c) { units->SetStealthEnabled(Arg(units, c, 0), Integer(c, 1) != 0); });
	vocabulary.AddAction("TEAM_SET_STEALTH_ENABLED", [units](ScriptCallContext &c) { units->TeamSetStealthEnabled(Arg(units, c, 0), Integer(c, 1) != 0); });
	vocabulary.AddAction("NAMED_SET_BOOBYTRAPPED", [units](ScriptCallContext &c) { units->NamedSetBoobytrapped(Text(c, 0), Arg(units, c, 1)); });
	vocabulary.AddAction("TEAM_SET_BOOBYTRAPPED", [units](ScriptCallContext &c) { units->TeamSetBoobytrapped(Text(c, 0), Arg(units, c, 1)); });
	// NAMED_EXIT_BUILDING(unit); TEAM_EXIT_ALL_BUILDINGS(team); EXIT_SPECIFIC_BUILDING(building); NAMED_GUARD(unit);
	// NAMED_SET_UNMANNED_STATUS(unit) / TEAM_SET_UNMANNED_STATUS(team);
	// TEAM_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT(team, button, waypoint)
	vocabulary.AddAction("NAMED_EXIT_BUILDING", [units](ScriptCallContext &c) { units->NamedExitBuilding(Arg(units, c, 0)); });
	vocabulary.AddAction("TEAM_EXIT_ALL_BUILDINGS", [units](ScriptCallContext &c) { units->TeamExitAllBuildings(Arg(units, c, 0)); });
	vocabulary.AddAction("EXIT_SPECIFIC_BUILDING", [units](ScriptCallContext &c) { units->ExitSpecificBuilding(Arg(units, c, 0)); });
	vocabulary.AddAction("NAMED_GUARD", [units](ScriptCallContext &c) { units->NamedGuard(Arg(units, c, 0)); });
	vocabulary.AddAction("NAMED_SET_UNMANNED_STATUS", [units](ScriptCallContext &c) { units->SetUnmanned(Arg(units, c, 0)); });
	vocabulary.AddAction("TEAM_SET_UNMANNED_STATUS", [units](ScriptCallContext &c) { units->TeamSetUnmanned(Arg(units, c, 0)); });
	vocabulary.AddAction("TEAM_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT",
		[units](ScriptCallContext &c) { units->TeamUseCommandButtonAtWaypoint(Arg(units, c, 0), Arg(units, c, 1), Arg(units, c, 2)); });
	// UNIT_IDLE_FOR_FRAMECOUNT(unit, frames): doUnitIdleForFramecount: a unit with an AI idles (aiIdle(CMD_FROM_SCRIPT))
	// and its sequential scripts wait that long (setSequentialTimer).
	vocabulary.AddAction("UNIT_IDLE_FOR_FRAMECOUNT", [units](ScriptCallContext &c) {
		const std::string unit = Arg(units, c, 0);
		if (const auto subject = units->UnitSubject(unit); subject && units->NamedIdle(unit))
			c.runtime.SetSequenceWait(*subject, Integer(c, 1));
	});
	// UNIT_GUARD_FOR_FRAMECOUNT(unit, frames): doUnitGuardForFramecount: a unit with an AI guards where it stands on its
	// normal locomotors and its sequential scripts wait that long.
	vocabulary.AddAction("UNIT_GUARD_FOR_FRAMECOUNT", [units](ScriptCallContext &c) {
		const std::string unit = Arg(units, c, 0);
		if (const auto subject = units->UnitSubject(unit); subject && units->NamedGuard(unit))
			c.runtime.SetSequenceWait(*subject, Integer(c, 1));
	});
	vocabulary.AddAction("PLAYER_ENABLE_FACTORIES", [units](ScriptCallContext &c) {
		units->PlayerObjectsEnabled(units->PlayerNamed(c.participant, Arg(units, c, 0)), Arg(units, c, 1), true);
	});
	vocabulary.AddAction("PLAYER_DISABLE_FACTORIES", [units](ScriptCallContext &c) {
		units->PlayerObjectsEnabled(units->PlayerNamed(c.participant, Arg(units, c, 0)), Arg(units, c, 1), false);
	});
	vocabulary.AddAction("NAMED_SET_STOPPING_DISTANCE",
		[units](ScriptCallContext &c) { units->NamedStoppingDistance(Arg(units, c, 0), parameters::Number(c, 1)); });
	vocabulary.AddAction("SET_STOPPING_DISTANCE",
		[units](ScriptCallContext &c) { units->TeamStoppingDistance(Arg(units, c, 0), parameters::Number(c, 1)); });
	vocabulary.AddAction("NAMED_EXIT_ALL", [units](ScriptCallContext &c) { units->NamedExitAll(Arg(units, c, 0)); });
	vocabulary.AddAction("TEAM_EXIT_ALL", [units](ScriptCallContext &c) { units->TeamExitAll(Arg(units, c, 0)); });
	// NAMED_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT(unit, button, waypoint); NAMED_FIRE_SPECIAL_POWER_AT_WAYPOINT(unit, power, waypoint)
	vocabulary.AddAction("NAMED_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT",
		[units](ScriptCallContext &c) { units->NamedUseCommandButtonAtWaypoint(Arg(units, c, 0), Arg(units, c, 1), Arg(units, c, 2)); });
	vocabulary.AddAction("NAMED_FIRE_SPECIAL_POWER_AT_WAYPOINT",
		[units](ScriptCallContext &c) { units->NamedFireSpecialPowerAtWaypoint(Arg(units, c, 0), Arg(units, c, 1), Arg(units, c, 2)); });
	// NAMED_FIRE_SPECIAL_POWER_AT_NAMED(unit, power, target): doNamedFireSpecialPowerAtNamed.
	// NAMED_HIDE_SPECIAL_POWER_DISPLAY(unit) / NAMED_SHOW_SPECIAL_POWER_DISPLAY(unit).
	vocabulary.AddAction("NAMED_HIDE_SPECIAL_POWER_DISPLAY", [units](ScriptCallContext &c) { units->NamedHideSpecialPowerDisplay(Arg(units, c, 0), true); });
	vocabulary.AddAction("NAMED_SHOW_SPECIAL_POWER_DISPLAY", [units](ScriptCallContext &c) { units->NamedHideSpecialPowerDisplay(Arg(units, c, 0), false); });
	vocabulary.AddAction("NAMED_FIRE_SPECIAL_POWER_AT_NAMED",
		[units](ScriptCallContext &c) { units->NamedFireSpecialPowerAtNamed(Arg(units, c, 0), Arg(units, c, 1), Arg(units, c, 2)); });
	vocabulary.AddAction("TEAM_HUNT", [units](ScriptCallContext &c) { units->TeamHunt(Arg(units, c, 0)); });
	vocabulary.AddAction("BUILD_TEAM", [units](ScriptCallContext &c) { units->BuildTeam(Arg(units, c, 0)); });
	vocabulary.AddAction("RECRUIT_TEAM", [units](ScriptCallContext &c) { units->RecruitTeam(Arg(units, c, 0), parameters::Number(c, 1)); });
	vocabulary.AddAction("TEAM_INCREASE_PRIORITY", [units](ScriptCallContext &c) { units->ChangeTeamPriority(Arg(units, c, 0), true); });
	vocabulary.AddAction("TEAM_DECREASE_PRIORITY", [units](ScriptCallContext &c) { units->ChangeTeamPriority(Arg(units, c, 0), false); });
	vocabulary.AddAction("TEAM_GUARD", [units](ScriptCallContext &c) { units->TeamGuard(Arg(units, c, 0), {}); });
	// TEAM_GUARD_POSITION(team, waypoint)
	vocabulary.AddAction("TEAM_GUARD_POSITION", [units](ScriptCallContext &c) { units->TeamGuard(Arg(units, c, 0), Arg(units, c, 1)); });
	// TEAM_GUARD_OBJECT(team, unit).
	vocabulary.AddAction("TEAM_GUARD_OBJECT", [units](ScriptCallContext &c) { units->TeamGuardObject(Arg(units, c, 0), Arg(units, c, 1)); });
	// TEAM_GUARD_SUPPLY_CENTER(team, supplies).
	vocabulary.AddAction("TEAM_GUARD_SUPPLY_CENTER", [units](ScriptCallContext &c) { units->TeamGuardSupplyCenter(Arg(units, c, 0), Integer(c, 1)); });
	// NAMED_ATTACK_NAMED(attacker, target)
	vocabulary.AddAction("NAMED_ATTACK_NAMED", [units](ScriptCallContext &c) { units->NamedAttackNamed(Arg(units, c, 0), Arg(units, c, 1)); });
	// NAMED_SET_HELD(name, held)
	vocabulary.AddAction("NAMED_SET_HELD", [units](ScriptCallContext &c) { units->NamedSetHeld(Arg(units, c, 0), Integer(c, 1) != 0); });
	// NAMED_STOP(unit); TEAM_STOP(team); TEAM_STOP_AND_DISBAND(team); MOVE_TEAM_TO(team, waypoint)
	vocabulary.AddAction("NAMED_STOP", [units](ScriptCallContext &c) { units->NamedStop(Arg(units, c, 0)); });
	vocabulary.AddAction("TEAM_STOP", [units](ScriptCallContext &c) { units->TeamStop(Arg(units, c, 0), false); });
	vocabulary.AddAction("TEAM_STOP_AND_DISBAND", [units](ScriptCallContext &c) { units->TeamStop(Arg(units, c, 0), true); });
	vocabulary.AddAction("MOVE_TEAM_TO", [units](ScriptCallContext &c) { units->TeamMoveTo(Arg(units, c, 0), Arg(units, c, 1)); });
	// NAMED_DAMAGE(unit, amount); DAMAGE_MEMBERS_OF_TEAM(team, amount); TEAM_KILL(team); TEAM_DELETE_LIVING(team); PLAYER_KILL(player)
	vocabulary.AddAction("NAMED_DAMAGE", [units](ScriptCallContext &c) { units->NamedDamage(Arg(units, c, 0), Integer(c, 1)); });
	vocabulary.AddAction("DAMAGE_MEMBERS_OF_TEAM", [units](ScriptCallContext &c) { units->TeamDamage(Arg(units, c, 0), parameters::Number(c, 1)); });
	vocabulary.AddAction("TEAM_KILL", [units](ScriptCallContext &c) { units->TeamKill(Arg(units, c, 0)); });
	vocabulary.AddAction("TEAM_DELETE_LIVING", [units](ScriptCallContext &c) { units->TeamDeleteLiving(Arg(units, c, 0)); });
	vocabulary.AddAction("PLAYER_KILL", [units](ScriptCallContext &c) { units->PlayerKill(units->PlayerNamed(c.participant, Arg(units, c, 0))); });
	// PLAYER_TRANSFER_OWNERSHIP_PLAYER(from, to); UNIT_DESTROY_ALL_CONTAINED(unit).
	vocabulary.AddAction("PLAYER_TRANSFER_OWNERSHIP_PLAYER", [units](ScriptCallContext &c) {
		units->PlayerTransferAssets(units->PlayerNamed(c.participant, Arg(units, c, 0)), units->PlayerNamed(c.participant, Arg(units, c, 1)));
	});
	vocabulary.AddAction("UNIT_DESTROY_ALL_CONTAINED", [units](ScriptCallContext &c) { units->DestroyAllContained(Arg(units, c, 0)); });
	// NAMED_SELECTED(unit).
	vocabulary.AddCondition("NAMED_SELECTED", [units](ScriptCallContext &c) { return units->NamedSelected(Arg(units, c, 0)); });
	// NAMED_SET_TOPPLE_DIRECTION(unit, direction).
	vocabulary.AddAction("NAMED_SET_TOPPLE_DIRECTION",
		[units](ScriptCallContext &c) { units->SetToppleDirection(Arg(units, c, 0), parameters::Position(c, 1)); });
	// NAMED_TRANSFER_OWNERSHIP_PLAYER(unit, player); TEAM_TRANSFER_TO_PLAYER(team, player)
	vocabulary.AddAction("NAMED_TRANSFER_OWNERSHIP_PLAYER",
		[units](ScriptCallContext &c) { units->NamedTransferToPlayer(Arg(units, c, 0), units->PlayerNamed(c.participant, Arg(units, c, 1))); });
	vocabulary.AddAction("TEAM_TRANSFER_TO_PLAYER",
		[units](ScriptCallContext &c) { units->TeamTransferToPlayer(Arg(units, c, 0), units->PlayerNamed(c.participant, Arg(units, c, 1))); });
	// PLAYER_GIVE_MONEY(player, amount); PLAYER_SET_MONEY(player, amount)
	vocabulary.AddAction("PLAYER_GIVE_MONEY", [units](ScriptCallContext &c) { units->PlayerGiveMoney(units->PlayerNamed(c.participant, Arg(units, c, 0)), Integer(c, 1)); });
	vocabulary.AddAction("PLAYER_SET_MONEY", [units](ScriptCallContext &c) { units->PlayerSetMoney(units->PlayerNamed(c.participant, Arg(units, c, 0)), Integer(c, 1)); });

	// PLAYER_SET_RANKLEVELLIMIT(level): the limit for every player, as the original.
	vocabulary.AddAction("PLAYER_SET_RANKLEVELLIMIT", [units](ScriptCallContext &c) { units->SetRankLimit(Integer(c, 0)); });
	vocabulary.AddAction("PLAYER_ENABLE_UNIT_CONSTRUCTION", [units](ScriptCallContext &c) { units->EnableUnitConstruction(units->PlayerNamed(c.participant, Arg(units, c, 0))); });
	vocabulary.AddAction("PLAYER_DISABLE_UNIT_CONSTRUCTION", [units](ScriptCallContext &c) { units->DisableUnitConstruction(units->PlayerNamed(c.participant, Arg(units, c, 0))); });
	// WAREHOUSE_SET_VALUE(warehouse, cash)
	vocabulary.AddAction("WAREHOUSE_SET_VALUE", [units](ScriptCallContext &c) { units->SetWarehouseValue(Arg(units, c, 0), Integer(c, 1)); });

	// SKIRMISH_COMMAND_BUTTON_READY_ALL / _PARTIAL(player, team, button)
	vocabulary.AddCondition("SKIRMISH_COMMAND_BUTTON_READY_ALL",
		[units](ScriptCallContext &c) { return units->TeamCommandButtonReady(Arg(units, c, 1), Arg(units, c, 2), true); });
	vocabulary.AddCondition("SKIRMISH_COMMAND_BUTTON_READY_PARTIAL",
		[units](ScriptCallContext &c) { return units->TeamCommandButtonReady(Arg(units, c, 1), Arg(units, c, 2), false); });
	// TEAM_STATE_IS / TEAM_STATE_IS_NOT(team, state): no such team is in no state (both false).
	vocabulary.AddCondition("TEAM_STATE_IS", [units](ScriptCallContext &c) {
		const auto state = units->TeamState(Arg(units, c, 0));
		return state && *state == Text(c, 1);
	});
	vocabulary.AddCondition("TEAM_STATE_IS_NOT", [units](ScriptCallContext &c) {
		const auto state = units->TeamState(Arg(units, c, 0));
		return state && *state != Text(c, 1);
	});
	// UNIT_EMPTIED(unit); TEAM_OWNED_BY_PLAYER(team, player)
	vocabulary.AddCondition("UNIT_EMPTIED", [units](ScriptCallContext &c) { return units->UnitEmptied(Arg(units, c, 0)); });
	vocabulary.AddCondition("TEAM_OWNED_BY_PLAYER", [units](ScriptCallContext &c) { return units->TeamOwnedBy(Arg(units, c, 0), c.participant, Text(c, 1)); });
	vocabulary.AddCondition("TEAM_CREATED", [units](ScriptCallContext &c) { return units->TeamCreated(Arg(units, c, 0)); });
	vocabulary.AddCondition("TEAM_DESTROYED", [units](ScriptCallContext &c) { return units->TeamDestroyed(Arg(units, c, 0)); });
	// UNIT_HAS_OBJECT_STATUS(unit, status); TEAM_ALL_HAS_OBJECT_STATUS / TEAM_SOME_HAVE_OBJECT_STATUS(team, status)
	vocabulary.AddCondition("UNIT_HAS_OBJECT_STATUS", [units](ScriptCallContext &c) { return units->NamedHasObjectStatus(Arg(units, c, 0), Text(c, 1)); });
	vocabulary.AddCondition("TEAM_ALL_HAS_OBJECT_STATUS", [units](ScriptCallContext &c) { return units->TeamHasObjectStatus(Arg(units, c, 0), Text(c, 1), true); });
	vocabulary.AddCondition("TEAM_SOME_HAVE_OBJECT_STATUS", [units](ScriptCallContext &c) { return units->TeamHasObjectStatus(Arg(units, c, 0), Text(c, 1), false); });
	// NAMED_INSIDE_AREA / NAMED_OUTSIDE_AREA / NAMED_ENTERED_AREA / NAMED_EXITED_AREA(unit, area); the team ones (team,
	// area, surfaces).
	using Query = UnitScriptHost::AreaQuery;
	const auto named = [&vocabulary, units](const char *name, Query query) {
		vocabulary.AddCondition(name, [units, query](ScriptCallContext &c) { return units->InArea(c.participant, query, Arg(units, c, 0), Arg(units, c, 1), 3); });
	};
	const auto team = [&vocabulary, units](const char *name, Query query) {
		vocabulary.AddCondition(name, [units, query](ScriptCallContext &c) {
			return units->InArea(c.participant, query, Arg(units, c, 0), Arg(units, c, 1), Integer(c, 2));
		});
	};
	named("NAMED_INSIDE_AREA", Query::NamedInside);
	named("NAMED_OUTSIDE_AREA", Query::NamedOutside);
	named("NAMED_ENTERED_AREA", Query::NamedEntered);
	named("NAMED_EXITED_AREA", Query::NamedExited);
	team("TEAM_INSIDE_AREA_PARTIALLY", Query::TeamInsidePartially);
	team("TEAM_INSIDE_AREA_ENTIRELY", Query::TeamInsideEntirely);
	team("TEAM_OUTSIDE_AREA_ENTIRELY", Query::TeamOutsideEntirely);
	team("TEAM_ENTERED_AREA_ENTIRELY", Query::TeamEnteredEntirely);
	team("TEAM_ENTERED_AREA_PARTIALLY", Query::TeamEnteredPartially);
	team("TEAM_EXITED_AREA_ENTIRELY", Query::TeamExitedEntirely);
	team("TEAM_EXITED_AREA_PARTIALLY", Query::TeamExitedPartially);
	// PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA(player, comparison, count, type, area) and _KIND_ (a KindOf).
	vocabulary.AddCondition("PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA", [units](ScriptCallContext &c) {
		const auto count = units->CountInArea(c.participant, 0, units->PlayerNamed(c.participant, Arg(units, c, 0)), Arg(units, c, 4), Arg(units, c, 3), -1);
		return count && detail::Compare(*count, Integer(c, 1), Integer(c, 2));
	});
	vocabulary.AddCondition("PLAYER_HAS_COMPARISON_UNIT_KIND_IN_TRIGGER_AREA", [units](ScriptCallContext &c) {
		const auto count = units->CountInArea(c.participant, 1, units->PlayerNamed(c.participant, Arg(units, c, 0)), Arg(units, c, 4), {}, Integer(c, 3));
		return count && detail::Compare(*count, Integer(c, 1), Integer(c, 2));
	});
	// SKIRMISH_NAMED_AREA_EXIST(player, area); SKIRMISH_PLAYER_HAS_UNITS_IN_AREA(player, area);
	// SKIRMISH_PLAYER_IS_OUTSIDE_AREA(player, area): a player and area that exist, with nothing in it;
	// SKIRMISH_VALUE_IN_AREA(player, comparison, money, area).
	vocabulary.AddCondition("SKIRMISH_TECH_BUILDING_WITHIN_DISTANCE", [units](ScriptCallContext &c) {
		return units->TechBuildingNear(c.participant, units->PlayerNamed(c.participant, Arg(units, c, 0)), parameters::Number(c, 1), Arg(units, c, 2));
	});
	vocabulary.AddCondition("SKIRMISH_SUPPLIES_VALUE_WITHIN_DISTANCE", [units](ScriptCallContext &c) {
		return units->SuppliesNear(c.participant, units->PlayerNamed(c.participant, Arg(units, c, 0)), parameters::Number(c, 1), Arg(units, c, 2),
			parameters::Number(c, 3));
	});
	vocabulary.AddCondition("SKIRMISH_UNOWNED_FACTION_UNIT_EXISTS",
		[units](ScriptCallContext &c) { return detail::Compare(units->UnownedFactionUnits(), Integer(c, 1), Integer(c, 2)); });
	vocabulary.AddCondition("SUPPLY_SOURCE_SAFE",
		[units](ScriptCallContext &c) { return units->SupplySourceSafe(units->PlayerNamed(c.participant, Arg(units, c, 0)), Integer(c, 1)); });
	// SUPPLY_SOURCE_ATTACKED(player).
	vocabulary.AddCondition("SUPPLY_SOURCE_ATTACKED",
		[units](ScriptCallContext &c) { return units->SupplySourceAttacked(units->PlayerNamed(c.participant, Arg(units, c, 0))); });
	vocabulary.AddAction("SET_ATTACK_PRIORITY_THING", [units](ScriptCallContext &c) { units->SetAttackPriorityThing(Arg(units, c, 0), Arg(units, c, 1), Integer(c, 2)); });
	vocabulary.AddAction("SET_ATTACK_PRIORITY_KIND_OF", [units](ScriptCallContext &c) { units->SetAttackPriorityKind(Arg(units, c, 0), Integer(c, 1), Integer(c, 2)); });
	vocabulary.AddAction("SET_DEFAULT_ATTACK_PRIORITY", [units](ScriptCallContext &c) { units->SetDefaultAttackPriority(Arg(units, c, 0), Integer(c, 1)); });
	vocabulary.AddAction("NAMED_APPLY_ATTACK_PRIORITY_SET", [units](ScriptCallContext &c) { units->ApplyAttackPrioritySet(Arg(units, c, 0), false, Arg(units, c, 1)); });
	vocabulary.AddAction("TEAM_APPLY_ATTACK_PRIORITY_SET", [units](ScriptCallContext &c) { units->ApplyAttackPrioritySet(Arg(units, c, 0), true, Arg(units, c, 1)); });
	// TEAM_EXECUTE_SEQUENTIAL_SCRIPT(team, script) / _LOOPING(team, script, times): doTeamStartSequentialScript: the team
	// idles and the script runs on it an action at a time (loops: times less one); TEAM_STOP_SEQUENTIAL_SCRIPT(team).
	const auto startSequence = [units](ScriptCallContext &c, std::int64_t loops) {
		const auto team = units->TeamIndex(Arg(units, c, 0));
		const std::string &script = Text(c, 1);
		if (!team || !c.runtime.HasScript(script))
			return;
		units->TeamStop("#" + std::to_string(*team), false);
		c.runtime.AppendSequence(script, *team, loops);
	};
	vocabulary.AddAction("TEAM_EXECUTE_SEQUENTIAL_SCRIPT", [startSequence](ScriptCallContext &c) { startSequence(c, 0); });
	vocabulary.AddAction("TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING", [startSequence](ScriptCallContext &c) { startSequence(c, Integer(c, 2) - 1); });
	vocabulary.AddAction("TEAM_STOP_SEQUENTIAL_SCRIPT", [units](ScriptCallContext &c) {
		if (const auto team = units->TeamIndex(Arg(units, c, 0)))
			c.runtime.StopSequences(*team);
	});
	// UNIT_EXECUTE_SEQUENTIAL_SCRIPT(unit, script) / _LOOPING(unit, script, times): doUnitStartSequentialScript: the
	// script runs on the unit an action at a time (it is not idled first); UNIT_STOP_SEQUENTIAL_SCRIPT(unit).
	const auto startUnitSequence = [units](ScriptCallContext &c, std::int64_t loops) {
		const auto subject = units->UnitSubject(Arg(units, c, 0));
		const std::string &script = Text(c, 1);
		if (subject && c.runtime.HasScript(script))
			c.runtime.AppendSequence(script, *subject, loops);
	};
	vocabulary.AddAction("UNIT_EXECUTE_SEQUENTIAL_SCRIPT", [startUnitSequence](ScriptCallContext &c) { startUnitSequence(c, 0); });
	vocabulary.AddAction("UNIT_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING", [startUnitSequence](ScriptCallContext &c) { startUnitSequence(c, Integer(c, 2) - 1); });
	vocabulary.AddAction("UNIT_STOP_SEQUENTIAL_SCRIPT", [units](ScriptCallContext &c) {
		if (const auto subject = units->UnitSubject(Arg(units, c, 0)))
			c.runtime.StopSequences(*subject);
	});
	// TEAM_ / UNIT_COMPLETED_SEQUENTIAL_EXECUTION: ScriptConditions::evaluateCondition has no case for them (its default:
	// false), so they never hold.
	vocabulary.AddCondition("TEAM_COMPLETED_SEQUENTIAL_EXECUTION", [](ScriptCallContext &) { return false; });
	vocabulary.AddCondition("UNIT_COMPLETED_SEQUENTIAL_EXECUTION", [](ScriptCallContext &) { return false; });
	// TEAM_IDLE_FOR_FRAMECOUNT / TEAM_GUARD_FOR_FRAMECOUNT(team, frames): idle (or guard where each stands) and its
	// sequence waits that long (setSequentialTimer).
	vocabulary.AddAction("TEAM_IDLE_FOR_FRAMECOUNT", [units](ScriptCallContext &c) {
		if (const auto team = units->TeamIndex(Arg(units, c, 0)))
		{
			units->TeamStop("#" + std::to_string(*team), false);
			c.runtime.SetSequenceWait(*team, Integer(c, 1));
		}
	});
	// TEAM_SPIN_FOR_FRAMECOUNT(team, frames): doTeamSpinForFramecount: only the team's sequence waits.
	vocabulary.AddAction("TEAM_SPIN_FOR_FRAMECOUNT", [units](ScriptCallContext &c) {
		if (const auto team = units->TeamIndex(Arg(units, c, 0)))
			c.runtime.SetSequenceWait(*team, Integer(c, 1));
	});
	vocabulary.AddAction("TEAM_GUARD_FOR_FRAMECOUNT", [units](ScriptCallContext &c) {
		if (const auto team = units->TeamIndex(Arg(units, c, 0)))
		{
			units->TeamGuard("#" + std::to_string(*team), {});
			c.runtime.SetSequenceWait(*team, Integer(c, 1));
		}
	});
	// TEAM_WAIT_FOR_NOT_CONTAINED_ALL / _PARTIAL(team): the sequence stays on this action while the team is aboard.
	vocabulary.AddAction("TEAM_WAIT_FOR_NOT_CONTAINED_ALL", [units](ScriptCallContext &c) {
		if (units->TeamContained(Arg(units, c, 0), true))
			c.runtime.HoldSequence();
	});
	vocabulary.AddAction("TEAM_WAIT_FOR_NOT_CONTAINED_PARTIAL", [units](ScriptCallContext &c) {
		if (units->TeamContained(Arg(units, c, 0), false))
			c.runtime.HoldSequence();
	});
	// SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_ALL / _PARTIAL(player, team, button): the sequence stays on this action
	// until the button is ready (the player is not used).
	vocabulary.AddAction("SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_ALL", [units](ScriptCallContext &c) {
		if (!units->TeamCommandButtonReady(Arg(units, c, 1), Arg(units, c, 2), true))
			c.runtime.HoldSequence();
	});
	vocabulary.AddAction("SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_PARTIAL", [units](ScriptCallContext &c) {
		if (!units->TeamCommandButtonReady(Arg(units, c, 1), Arg(units, c, 2), false))
			c.runtime.HoldSequence();
	});
	vocabulary.AddAction("TEAM_USE_COMMANDBUTTON_ABILITY", [units](ScriptCallContext &c) { units->TeamUseCommandButton(Arg(units, c, 0), Arg(units, c, 1), std::nullopt); });
	vocabulary.AddAction("TEAM_PARTIAL_USE_COMMANDBUTTON",
		[units](ScriptCallContext &c) { units->TeamUseCommandButton(Arg(units, c, 1), Arg(units, c, 2), parameters::Number(c, 0)); });
	vocabulary.AddAction("TEAM_HUNT_WITH_COMMAND_BUTTON", [units](ScriptCallContext &c) { units->TeamHuntWithCommandButton(Arg(units, c, 0), Arg(units, c, 1)); });
	vocabulary.AddAction("NAMED_USE_COMMANDBUTTON_ABILITY", [units](ScriptCallContext &c) { units->NamedUseCommandButton(Arg(units, c, 0), Arg(units, c, 1), std::nullopt); });
	vocabulary.AddAction("NAMED_USE_COMMANDBUTTON_ABILITY_ON_NAMED",
		[units](ScriptCallContext &c) { units->NamedUseCommandButton(Arg(units, c, 0), Arg(units, c, 1), Arg(units, c, 2)); });
	vocabulary.AddAction("TEAM_USE_COMMANDBUTTON_ABILITY_ON_NAMED",
		[units](ScriptCallContext &c) { units->TeamUseCommandButtonOnNamed(Arg(units, c, 0), Arg(units, c, 1), Arg(units, c, 2), false); });
	vocabulary.AddAction("TEAM_ALL_USE_COMMANDBUTTON_ON_NAMED",
		[units](ScriptCallContext &c) { units->TeamUseCommandButtonOnNamed(Arg(units, c, 0), Arg(units, c, 1), Arg(units, c, 2), true); });
	const auto nearest = [&vocabulary, units](std::string_view action, int which, bool kind, bool type) {
		vocabulary.AddAction(std::string(action), [units, which, kind, type](ScriptCallContext &c) {
			units->TeamUseCommandButtonOnNearest(Arg(units, c, 0), Arg(units, c, 1), which, kind ? Integer(c, 2) : -1, type ? Arg(units, c, 2) : std::string{});
		});
	};
	nearest("TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_UNIT", 0, false, false);
	nearest("TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_GARRISONED_BUILDING", 1, false, false);
	nearest("TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_KINDOF", 2, true, false);
	nearest("TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_BUILDING", 3, false, false);
	nearest("TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_BUILDING_CLASS", 4, true, false);
	nearest("TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_OBJECTTYPE", 5, false, true);
	vocabulary.AddAction("TEAM_LOAD_TRANSPORTS", [units](ScriptCallContext &c) { units->TeamLoadTransports(Arg(units, c, 0)); });
	vocabulary.AddAction("UNIT_MOVE_TOWARDS_NEAREST_OBJECT_TYPE",
		[units](ScriptCallContext &c) { units->UnitMoveTowardsNearest(c.participant, Arg(units, c, 0), Arg(units, c, 1), Arg(units, c, 2)); });
	// PLAYER_CREATE_TEAM_FROM_CAPTURED_UNITS / TEAM_COLLECT_NEARBY_FOR_TEAM: doCreateTeamFromCapturedUnits only looks the
	// team up and doCollectNearbyForTeam is "not implemented"; neither changes anything.
	for (const char *unimplemented : {"PLAYER_CREATE_TEAM_FROM_CAPTURED_UNITS", "TEAM_COLLECT_NEARBY_FOR_TEAM"})
		vocabulary.AddAction(unimplemented, [](ScriptCallContext &) {});
	vocabulary.AddAction("TEAM_MOVE_TOWARDS_NEAREST_OBJECT_TYPE",
		[units](ScriptCallContext &c) { units->TeamMoveTowardsNearest(c.participant, Arg(units, c, 0), Arg(units, c, 1), Arg(units, c, 2)); });
	vocabulary.AddAction("PLAYER_SELL_EVERYTHING", [units](ScriptCallContext &c) { units->PlayerSellEverything(units->PlayerNamed(c.participant, Arg(units, c, 0))); });
	vocabulary.AddAction("TEAM_AVAILABLE_FOR_RECRUITMENT", [units](ScriptCallContext &c) { units->TeamRecruitable(Arg(units, c, 0), Integer(c, 1) != 0); });
	vocabulary.AddAction("TEAM_ATTACK_AREA", [units](ScriptCallContext &c) { units->AreaOrder(c.participant, Arg(units, c, 0), true, Arg(units, c, 1), false); });
	vocabulary.AddAction("NAMED_ATTACK_AREA", [units](ScriptCallContext &c) { units->AreaOrder(c.participant, Arg(units, c, 0), false, Arg(units, c, 1), false); });
	vocabulary.AddAction("TEAM_GUARD_AREA", [units](ScriptCallContext &c) { units->AreaOrder(c.participant, Arg(units, c, 0), true, Arg(units, c, 1), true); });
	vocabulary.AddCondition("SKIRMISH_NAMED_AREA_EXIST", [units](ScriptCallContext &c) { return units->AreaExists(c.participant, Arg(units, c, 1)); });
	vocabulary.AddCondition("SKIRMISH_PLAYER_HAS_UNITS_IN_AREA", [units](ScriptCallContext &c) {
		const auto count = units->CountInArea(c.participant, 2, units->PlayerNamed(c.participant, Arg(units, c, 0)), Arg(units, c, 1), {}, -1);
		return count && *count > 0;
	});
	vocabulary.AddCondition("SKIRMISH_PLAYER_IS_OUTSIDE_AREA", [units](ScriptCallContext &c) {
		const auto count = units->CountInArea(c.participant, 2, units->PlayerNamed(c.participant, Arg(units, c, 0)), Arg(units, c, 1), {}, -1);
		return count && *count == 0;
	});
	vocabulary.AddCondition("SKIRMISH_VALUE_IN_AREA", [units](ScriptCallContext &c) {
		const auto value = units->CountInArea(c.participant, 3, units->PlayerNamed(c.participant, Arg(units, c, 0)), Arg(units, c, 3), {}, -1);
		return value && detail::Compare(*value, Integer(c, 1), Integer(c, 2));
	});
}
}
