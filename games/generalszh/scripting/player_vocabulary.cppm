export module games.generalszh.scripting.player_vocabulary;
import std;

export import engine.scripting.runtime.script_runtime;
import games.generalszh.scripting.script_parameters;
import games.generalszh.scripting.core_vocabulary;

// The Zero Hour script conditions about players, their objects and their teams, and the object type lists
// (ScriptConditions / ScriptEngine), over whatever runs the simulation through PlayerScriptHost. A player parameter
// may name the script's own player ("<This Player>") or its enemy ("<This Player's Enemy>"): the host resolves it
// for the script's participant. Comparison codes as the original's Parameter (less than 0 .. not equal 5).
export namespace generalszh::scripting
{
class PlayerScriptHost
{
public:
	virtual ~PlayerScriptHost() = default;

	// The player a script of `participant` names (see UnitScriptHost::PlayerNamed); none: no such player.
	virtual std::optional<std::uint32_t> ResolvePlayer(std::size_t participant, const std::string &name) = 0;
	virtual std::int64_t Money(std::uint32_t player) const = 0;
	// AudioManager::hasMusicTrackCompleted: the track has played through at least `times` times (as last reported).
	virtual bool MusicCompleted(const std::string &track, std::int64_t times) const = 0;
	// Energy::getEnergyProduction / getEnergyConsumption.
	virtual std::pair<std::int64_t, std::int64_t> Power(std::uint32_t player) const = 0;
	virtual std::int64_t StartIndex(std::uint32_t player) const = 0; // 0-based (getMpStartIndex)
	virtual std::string Side(std::uint32_t player) const = 0;       // Player::getSide
	virtual bool HasAnyObjects(std::uint32_t player) const = 0;
	// Player::hasAnyBuildFacility; countBuildings, or with `faction` countObjects(MP_COUNT_FOR_VICTORY + STRUCTURE).
	virtual bool HasAnyBuildFacility(std::uint32_t player) const = 0;
	virtual std::int64_t CountBuildings(std::uint32_t player, bool faction) const = 0;
	// The player's garrisons with anyone inside, and its captured objects.
	virtual std::int64_t CountGarrisoned(std::uint32_t player) const = 0;
	virtual std::int64_t CountCaptured(std::uint32_t player) const = 0;
	// Player::canBuild for any of a type or type list (ObjectTypes::canBuildAny).
	virtual bool CanBuildAny(std::uint32_t player, const std::string &types) = 0;
	// Any of the player's objects shown to `by` (getShroudedStatus: clear or partly clear).
	virtual bool Discovered(std::uint32_t player, std::uint32_t by) const = 0;
	// The player's objects of a type or type list (countObjectsByThingTemplate); `ignoreDead`.
	virtual std::int64_t CountObjects(std::uint32_t player, const std::string &types, bool ignoreDead) const = 0;
	// The count of a type last seen for the player (ScriptEngine::getObjectCount; 0 for an inactive player), and set.
	virtual std::int64_t RecordedCount(std::uint32_t player, const std::string &types) const = 0;
	virtual void RecordCount(std::uint32_t player, const std::string &types, std::int64_t count) = 0;
	virtual bool TakeCompletedUpgrade(std::uint32_t player, const std::string &upgrade) = 0;
	// Likewise, only one made by the named unit (none by that name alive: never).
	virtual bool TakeCompletedUpgradeFrom(std::uint32_t player, const std::string &upgrade, const std::string &unit) = 0;
	virtual bool TakeAcquiredScience(std::uint32_t player, const std::string &science) = 0;
	virtual bool NamedAlive(const std::string &name) const = 0; // exists and not effectively dead
	// TerrainLogic::isBridgeBroken (`broken`) / isBridgeRepaired of the named bridge object, for the tick's scripts.
	virtual bool BridgeBroken(const std::string &name, bool broken) const = 0;
	// A named unit as the conditions see it: its object is there (getUnitNamed), effectively dead, the name ever
	// known (didUnitExist), and the player controlling it.
	struct NamedUnit
	{
		bool exists{false};
		bool dead{false};
		bool known{false};
		std::uint32_t owner{0};
	};
	virtual NamedUnit NamedUnitState(const std::string &name) const = 0;
	// Player::setPlayerRelationship: how `from` regards `to` from now (the original's Relationship: 0 enemies, 1 neutral,
	// 2 allies).
	virtual void SetPlayerRelationship(std::uint32_t from, std::uint32_t to, std::int64_t relationship) = 0;
	// The last damage of a named unit / of each of a team's members (their bodies' getLastDamageInfo): the attacker's
	// player as it hit (none: not known), the attacker's current player while it is there, and its type as it hit.
	struct LastAttack
	{
		std::optional<std::uint32_t> player;
		std::optional<std::uint32_t> attackerPlayer;
		std::string attackerType;
	};
	virtual std::optional<LastAttack> NamedLastAttack(const std::string &unit) const = 0;
	virtual std::vector<LastAttack> TeamLastAttacks(const std::string &team) const = 0;
	// Whether a thing of that type (or type list, or one equivalent to it) matches `type`.
	virtual bool TypeMatches(const std::string &types, const std::string &type) const = 0;
	// A named container's count and room (none: no such unit or not a container).
	virtual std::optional<std::pair<std::uint32_t, std::uint32_t>> NamedContain(const std::string &unit) const = 0;
	// evaluateNamedDiscovered / evaluateTeamDiscovered (any member).
	virtual bool NamedDiscovered(const std::string &unit, std::uint32_t player) const = 0;
	virtual bool TeamDiscovered(const std::string &team, std::uint32_t player) const = 0;
	// evaluateNamedReachedWaypointsEnd / evaluateTeamReachedWaypointsEnd (any member).
	virtual bool NamedReachedPathEnd(const std::string &unit, const std::string &label) const = 0;
	virtual bool TeamReachedPathEnd(const std::string &team, const std::string &label) const = 0;
	// evaluateEnemySighted (the relationship as the original's 0 enemies, 1 neutral, 2 allies) / evaluateTypeSighted.
	virtual bool EnemySighted(const std::string &unit, std::int64_t alliance, std::uint32_t player) const = 0;
	virtual bool TypeSighted(const std::string &unit, const std::string &types, std::uint32_t player) const = 0;
	// evaluateBuildingEntered.
	virtual bool BuildingEnteredBy(const std::string &building, std::uint32_t player) const = 0;
	// Player::getAttackedBy.
	virtual bool AttackedBy(std::uint32_t player, std::uint32_t attacker) const = 0;
	// A named unit's health as the original's percent of its initial health, rounded; none: no such unit or body.
	virtual std::optional<std::int64_t> NamedHealthPercent(const std::string &name) const = 0;
	// Any instance of the team has a unit that is alive and not a structure... (Team::hasAnyUnits).
	virtual bool TeamHasUnits(const std::string &team) const = 0;
	virtual void ObjectTypeList(const std::string &list, const std::string &type, bool add) = 0;
	virtual void SetCanBuildBase(std::uint32_t player, bool can) = 0;
	// Player::setListInScoreScreen.
	virtual void SetListInScoreScreen(std::uint32_t player, bool list) = 0;
	// A computer player marks the next unbuilt entry of that structure in its base plan to be built first
	// (Player::buildSpecificBuilding -> AISkirmishPlayer::buildSpecificAIBuilding).
	virtual void BuildSpecificBuilding(std::uint32_t player, const std::string &structure) = 0;
	// A computer player plans a structure by the supply warehouse it picks (Player::buildBySupplies).
	virtual void BuildBySupplies(std::uint32_t player, std::int64_t minimumCash, const std::string &structure) = 0;
	// The player's AI plans `structure` where the team is.
	virtual void BuildNearestTeam(std::uint32_t player, const std::string &structure, const std::string &team) = 0;
	// A computer player's base defence at its front or flank: its side's (empty structure) or the one named.
	virtual void BuildBaseDefense(std::uint32_t player, const std::string &structure, bool flank) = 0;
	// The player's rank as a general: skill points, rank levels, sciences granted or bought, the skill point scale.
	virtual void AddSkillPoints(std::uint32_t player, std::int64_t points) = 0;
	// Player::setUnitsShouldHunt(true, CMD_FROM_SCRIPT).
	virtual void PlayerHunt(std::uint32_t player) = 0;
	// Player::ungarrisonAllUnits.
	virtual void PlayerExitAllBuildings(std::uint32_t player) = 0;
	// GameLogic::enableScoring.
	virtual void EnableScoring(bool on) = 0;
	// doMapSwitchBorder: TerrainLogic::setActiveBoundary.
	virtual void SwitchBoundary(std::int64_t boundary) = 0;
	// doWaterChangeHeightOverTime: TerrainLogic::changeWaterHeightOverTime of the named water area (none: nothing).
	virtual void ChangeWaterHeight(const std::string &water, Engine::Math::Fixed height, Engine::Math::Fixed seconds, Engine::Math::Fixed damage) = 0;
	// doWaterChangeHeight: TerrainLogic::setWaterHeight of the named water area at once.
	virtual void SetWaterHeight(const std::string &water, Engine::Math::Fixed height) = 0;
	// doOverrideHulkLifetime: hulks last (Int)(seconds * 30) ticks from now on (below zero: their own).
	virtual void OverrideHulkLifetime(Engine::Math::Fixed seconds) = 0;
	// doModifyBuildableStatus: GameLogic::setBuildableStatusOverride for the object type (none: nothing).
	virtual void SetBuildable(const std::string &objectType, std::int64_t status) = 0;
	// doAddCommandBarButton / doRemoveCommandBarButton: the object type's command set gets the button at the slot (0-based),
	// or loses it wherever it has it now (no slot).
	virtual void CommandBarButton(const std::string &button, const std::string &objectType, std::optional<std::int64_t> slot) = 0;
	virtual void RepairStructure(std::uint32_t player, const std::string &structure) = 0;
	// doAffectPlayerSkillset (AIPlayer::selectSkillset, 0-based) and updateBaseConstructionSpeed (setTeamDelaySeconds):
	// a computer player's.
	virtual void SelectSkillset(std::uint32_t player, std::int64_t skillset) = 0;
	virtual void SetTeamDelaySeconds(std::uint32_t player, std::int64_t seconds) = 0;
	// ScriptEngine::doFreezeTime / doUnfreezeTime.
	virtual void FreezeTime(bool frozen) = 0;
	// isSpecialPowerTriggered / isSpecialPowerMidway / isSpecialPowerComplete (0, 1, 2): found for the player (from any
	// object, or the one named: none if it is gone), then taken.
	virtual bool TakeSpecialPowerEvent(int stage, std::uint32_t player, const std::string &power, const std::optional<std::string> &unit) = 0;
	// doSkirmishFireSpecialPowerAtMostCost: the named player's ready objects fire at the script player's skirmish enemy.
	virtual void FireSpecialPowerAtMostCost(std::uint32_t scriptPlayer, std::uint32_t player, const std::string &power) = 0;
	// Whether the power exists; whether one of the player's objects has it ready; else the soonest tick one will be
	// (at most `latest`).
	virtual std::tuple<bool, bool, std::uint64_t> SpecialPowerReadiness(std::uint32_t player, const std::string &power, std::uint64_t latest) = 0;
	virtual void AddRankLevels(std::uint32_t player, std::int64_t levels) = 0;
	// isSpeechComplete / isAudioComplete: whether the speech (or sound) is done, its length from the first time asked.
	virtual bool SoundComplete(bool speech, const std::string &name) = 0;
	virtual void SetRankLevel(std::uint32_t player, std::int64_t level) = 0;
	virtual void GrantScience(std::uint32_t player, const std::string &science) = 0;
	virtual void PurchaseScience(std::uint32_t player, const std::string &science) = 0;
	// setScienceAvailability ("Available", "Disabled", "Hidden", any case; anything else is ignored).
	virtual void SetScienceAvailability(std::uint32_t player, const std::string &science, const std::string &availability) = 0;
	// isCapableOfPurchasingScience (an unknown science: false), getSciencePurchasePoints.
	virtual bool CanPurchaseScience(std::uint32_t player, const std::string &science) const = 0;
	virtual std::int64_t SciencePurchasePoints(std::uint32_t player) const = 0;
	virtual void SetSkillPointsModifier(std::uint32_t player, Engine::Math::Fixed modifier) = 0;
	// The shroud by script, for the player or, none named (empty, or no such player), every human player
	// (getHumanPlayerMask): a look or a dollop of shroud at a waypoint undone at once (doRevealMapAtWaypoint,
	// doShroudMapAtWaypoint), and over the whole map (revealMapForPlayer, revealMapForPlayerPermanently and its undo,
	// shroudMapForPlayer).
	enum class MapShroud : std::uint8_t
	{
		RevealAll,
		RevealAllPermanently,
		UndoRevealAllPermanently,
		ShroudAll,
	};
	virtual void ShroudAtWaypoint(const std::string &waypoint, Engine::Math::Fixed radius, std::optional<std::uint32_t> player, bool cover) = 0;
	virtual void ShroudEntireMap(std::optional<std::uint32_t> player, MapShroud what) = 0;
	// A named standing reveal at a waypoint for the player (none: it reveals nothing), and its undoing
	// (doRevealMapAtWaypointPermanent / doUndoRevealMapAtWaypointPermanent).
	virtual void RevealNamed(const std::string &name, const std::string &waypoint, Engine::Math::Fixed radius, std::optional<std::uint32_t> player) = 0;
	virtual void UndoRevealNamed(const std::string &name) = 0;
};

inline void AddPlayerVocabulary(engine::scripting::Vocabulary &vocabulary, PlayerScriptHost *host)
{
	using engine::scripting::ScriptCallContext;
	using parameters::Integer;
	using parameters::Text;
	const auto player = [host](ScriptCallContext &c, std::size_t index) { return host->ResolvePlayer(c.participant, Text(c, index)); };

	// PLAYER_HAS_CREDITS(credits, comparison, player): evaluatePlayerHasCredits compares the credits to the money
	// ("credits < money" for less than), the parameters' way round.
	// MUSIC_TRACK_HAS_COMPLETED(track, times): evaluateMusicHasCompleted, in a single-player mission's own scripts.
	vocabulary.AddCondition("MUSIC_TRACK_HAS_COMPLETED", [host](ScriptCallContext &c) { return host->MusicCompleted(Text(c, 0), Integer(c, 1)); });
	vocabulary.AddCondition("PLAYER_HAS_CREDITS", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 2);
		return who && detail::Compare(Integer(c, 0), Integer(c, 1), host->Money(*who));
	});
	// PLAYER_HAS_POWER / PLAYER_HAS_NO_POWER(player): hasSufficientPower (production at least consumption).
	vocabulary.AddCondition("PLAYER_HAS_POWER", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		if (!who)
			return false;
		const auto [produced, used] = host->Power(*who);
		return produced >= used;
	});
	vocabulary.AddCondition("PLAYER_HAS_NO_POWER", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		if (!who)
			return true; // !evaluatePlayerHasPower
		const auto [produced, used] = host->Power(*who);
		return produced < used;
	});
	// PLAYER_EXCESS_POWER_COMPARE_VALUE(player, comparison, kilowatts): production less consumption.
	vocabulary.AddCondition("PLAYER_EXCESS_POWER_COMPARE_VALUE", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		if (!who)
			return false;
		const auto [produced, used] = host->Power(*who);
		return detail::Compare(produced - used, Integer(c, 1), Integer(c, 2));
	});
	// PLAYER_ALL_BUILDFACILITIES_DESTROYED(player): no such player has none. PLAYER_HAS_N_OR_FEWER_BUILDINGS /
	// _FACTION_BUILDINGS(player, count). PLAYER_DESTROYED_N_BUILDINGS_PLAYER: never implemented (always false).
	vocabulary.AddCondition("PLAYER_ALL_BUILDFACILITIES_DESTROYED", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		return !who || !host->HasAnyBuildFacility(*who);
	});
	for (const auto &[name, faction] : {std::pair{"PLAYER_HAS_N_OR_FEWER_BUILDINGS", false}, std::pair{"PLAYER_HAS_N_OR_FEWER_FACTION_BUILDINGS", true}})
		vocabulary.AddCondition(name, [host, player, faction](ScriptCallContext &c) {
			const auto who = player(c, 0);
			return who && Integer(c, 1) >= host->CountBuildings(*who, faction);
		});
	vocabulary.AddCondition("PLAYER_DESTROYED_N_BUILDINGS_PLAYER", [](ScriptCallContext &) { return false; });
	// PLAYER_POWER_COMPARE_PERCENT(player, comparison, percent): getEnergySupplyRatio (production over consumption; the
	// production itself when nothing is used) against percent / 100, compared exactly as fractions.
	vocabulary.AddCondition("PLAYER_POWER_COMPARE_PERCENT", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		if (!who)
			return false;
		const auto [produced, used] = host->Power(*who);
		const std::int64_t percent = Integer(c, 2);
		// ratio ? percent / 100  <=>  produced * 100 ? percent * max(used, 1) (used 0: the ratio is `produced`).
		const std::int64_t left = produced * 100, right = percent * std::max<std::int64_t>(used, 1);
		return detail::Compare(left, Integer(c, 1), right);
	});
	// START_POSITION_IS(player, start 1..8).
	vocabulary.AddCondition("START_POSITION_IS", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		return who && host->StartIndex(*who) == Integer(c, 1) - 1;
	});
	// SKIRMISH_PLAYER_FACTION(player, side): evaluateSkirmishPlayerIsFaction.
	vocabulary.AddCondition("SKIRMISH_PLAYER_FACTION", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		return who && host->Side(*who) == Text(c, 1);
	});
	// BRIDGE_BROKEN / BRIDGE_REPAIRED(bridge): evaluateBridgeBroken / evaluateBridgeRepaired (the named bridge object).
	vocabulary.AddCondition("BRIDGE_BROKEN", [host](ScriptCallContext &c) { return host->BridgeBroken(Text(c, 0), true); });
	vocabulary.AddCondition("BRIDGE_REPAIRED", [host](ScriptCallContext &c) { return host->BridgeBroken(Text(c, 0), false); });
	vocabulary.AddCondition("NAMED_NOT_DESTROYED", [host](ScriptCallContext &c) { return host->NamedAlive(Text(c, 0)); });
	// NAMED_DESTROYED / NAMED_DYING / NAMED_TOTALLY_DEAD / NAMED_CREATED(unit) (evaluateNamedUnitDestroyed, ...Dying,
	// ...TotallyDead, evaluateNamedCreated: its object there at all) and NAMED_OWNED_BY_PLAYER(unit, player).
	vocabulary.AddCondition("NAMED_DESTROYED", [host](ScriptCallContext &c) {
		const auto unit = host->NamedUnitState(Text(c, 0));
		return unit.exists ? unit.dead : unit.known;
	});
	vocabulary.AddCondition("NAMED_DYING", [host](ScriptCallContext &c) {
		const auto unit = host->NamedUnitState(Text(c, 0));
		return unit.exists && unit.dead;
	});
	vocabulary.AddCondition("NAMED_TOTALLY_DEAD", [host](ScriptCallContext &c) {
		const auto unit = host->NamedUnitState(Text(c, 0));
		return !unit.exists && unit.known;
	});
	// PLAYER_RELATES_PLAYER(player, other player, relationship): updatePlayerRelationTowardPlayer.
	vocabulary.AddAction("PLAYER_RELATES_PLAYER", [host, player](ScriptCallContext &c) {
		const auto from = player(c, 0);
		const auto to = player(c, 1);
		if (from && to)
			host->SetPlayerRelationship(*from, *to, Integer(c, 2));
	});
	// NAMED_ATTACKED_BY_PLAYER(unit, player): evaluateNamedAttackedByPlayer (the player as it hit, else the attacker's now).
	vocabulary.AddCondition("NAMED_ATTACKED_BY_PLAYER", [host, player](ScriptCallContext &c) {
		const auto attack = host->NamedLastAttack(Text(c, 0));
		const auto who = player(c, 1);
		if (!attack || (!attack->player && !attack->attackerPlayer))
			return false;
		if (attack->player && attack->player == who)
			return true;
		return attack->attackerPlayer.has_value() && attack->attackerPlayer == who;
	});
	// TEAM_ATTACKED_BY_PLAYER(team, player): evaluateTeamAttackedByPlayer (a member's attacker still there, its player's).
	vocabulary.AddCondition("TEAM_ATTACKED_BY_PLAYER", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 1);
		for (const auto &attack : host->TeamLastAttacks(Text(c, 0)))
			if (attack.attackerPlayer && attack.attackerPlayer == who)
				return true;
		return false;
	});
	// NAMED_ / TEAM_ATTACKED_BY_OBJECTTYPE(unit or team, type): evaluateNamedAttackedByType / evaluateTeamAttackedByType
	// (the attacker's type as it hit, alive or not).
	vocabulary.AddCondition("NAMED_ATTACKED_BY_OBJECTTYPE", [host](ScriptCallContext &c) {
		const auto attack = host->NamedLastAttack(Text(c, 0));
		return attack && !attack->attackerType.empty() && host->TypeMatches(Text(c, 1), attack->attackerType);
	});
	vocabulary.AddCondition("TEAM_ATTACKED_BY_OBJECTTYPE", [host](ScriptCallContext &c) {
		for (const auto &attack : host->TeamLastAttacks(Text(c, 0)))
			if (!attack.attackerType.empty() && host->TypeMatches(Text(c, 1), attack.attackerType))
				return true;
		return false;
	});
	// SKIRMISH_PLAYER_HAS_BEEN_ATTACKED_BY_PLAYER(player, attacker): Player::getAttackedBy.
	vocabulary.AddCondition("SKIRMISH_PLAYER_HAS_BEEN_ATTACKED_BY_PLAYER", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		const auto by = player(c, 1);
		return who && by && host->AttackedBy(*who, *by);
	});
	// NAMED_BUILDING_IS_EMPTY(building): evaluateIsBuildingEmpty (a container holding none);
	// NAMED_HAS_FREE_CONTAINER_SLOTS(unit): evaluateNamedHasFreeContainerSlots (fewer aboard than its room).
	vocabulary.AddCondition("NAMED_BUILDING_IS_EMPTY", [host](ScriptCallContext &c) {
		const auto contain = host->NamedContain(Text(c, 0));
		return contain && contain->first == 0;
	});
	vocabulary.AddCondition("NAMED_HAS_FREE_CONTAINER_SLOTS", [host](ScriptCallContext &c) {
		const auto contain = host->NamedContain(Text(c, 0));
		return contain && contain->first < contain->second;
	});
	// NAMED_REACHED_WAYPOINTS_END(unit, path label) / TEAM_REACHED_WAYPOINTS_END(team, path label)
	vocabulary.AddCondition("NAMED_REACHED_WAYPOINTS_END", [host](ScriptCallContext &c) { return host->NamedReachedPathEnd(Text(c, 0), Text(c, 1)); });
	vocabulary.AddCondition("TEAM_REACHED_WAYPOINTS_END", [host](ScriptCallContext &c) { return host->TeamReachedPathEnd(Text(c, 0), Text(c, 1)); });
	// ENEMY_SIGHTED(unit, relationship, player) / TYPE_SIGHTED(unit, type, player)
	vocabulary.AddCondition("ENEMY_SIGHTED", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 2);
		return who && host->EnemySighted(Text(c, 0), Integer(c, 1), *who);
	});
	vocabulary.AddCondition("TYPE_SIGHTED", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 2);
		return who && host->TypeSighted(Text(c, 0), Text(c, 1), *who);
	});
	// BUILDING_ENTERED_BY_PLAYER(player, building)
	vocabulary.AddCondition("BUILDING_ENTERED_BY_PLAYER", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		return who && host->BuildingEnteredBy(Text(c, 1), *who);
	});
	// NAMED_DISCOVERED(unit, player) / TEAM_DISCOVERED(team, player)
	vocabulary.AddCondition("NAMED_DISCOVERED", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 1);
		return who && host->NamedDiscovered(Text(c, 0), *who);
	});
	vocabulary.AddCondition("TEAM_DISCOVERED", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 1);
		return who && host->TeamDiscovered(Text(c, 0), *who);
	});
	vocabulary.AddCondition("NAMED_CREATED", [host](ScriptCallContext &c) { return host->NamedUnitState(Text(c, 0)).exists; });
	vocabulary.AddCondition("NAMED_OWNED_BY_PLAYER", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 1);
		const auto unit = host->NamedUnitState(Text(c, 0));
		return who && unit.exists && unit.owner == *who;
	});
	// BUILT_BY_PLAYER(object type, player): any of the type (the dead too).
	vocabulary.AddCondition("BUILT_BY_PLAYER", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 1);
		return who && host->CountObjects(*who, Text(c, 0), false) != 0;
	});
	// PLAYER_BUILT_UPGRADE(player, upgrade): a finished upgrade not yet noticed (taken).
	vocabulary.AddCondition("PLAYER_BUILT_UPGRADE", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		return who && host->TakeCompletedUpgrade(*who, Text(c, 1));
	});
	// PLAYER_BUILT_UPGRADE_FROM_NAMED(player, upgrade, unit): evaluateUpgradeFromUnitComplete, the named unit's alone.
	vocabulary.AddCondition("PLAYER_BUILT_UPGRADE_FROM_NAMED", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		return who && host->TakeCompletedUpgradeFrom(*who, Text(c, 1), Text(c, 2));
	});
	// Never true in the original: MISSION_ATTEMPTS (evaluateMissionAttempts returns false), the DEFUNCT_ general
	// selections ("no longer in use") and the OBSOLETE_ ones (no case at all).
	for (const char *never : {"MISSION_ATTEMPTS", "DEFUNCT_PLAYER_SELECTED_GENERAL", "DEFUNCT_PLAYER_SELECTED_GENERAL_FROM_NAMED", "OBSOLETE_SCRIPT_1",
			 "OBSOLETE_SCRIPT_2"})
		vocabulary.AddCondition(never, [](ScriptCallContext &) { return false; });
	// PLAYER_ACQUIRED_SCIENCE(player, science): likewise.
	vocabulary.AddCondition("PLAYER_ACQUIRED_SCIENCE", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		return who && host->TakeAcquiredScience(*who, Text(c, 1));
	});
	// PLAYER_CAN_PURCHASE_SCIENCE(player, science): evaluateCanPurchaseScience; PLAYER_HAS_SCIENCEPURCHASEPOINTS(player,
	// points): evaluateSciencePurchasePoints (at least that many). No such player: false.
	vocabulary.AddCondition("PLAYER_CAN_PURCHASE_SCIENCE", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		return who && host->CanPurchaseScience(*who, Text(c, 1));
	});
	vocabulary.AddCondition("PLAYER_HAS_SCIENCEPURCHASEPOINTS", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		return who && host->SciencePurchasePoints(*who) >= Integer(c, 1);
	});
	// PLAYER_ALL_DESTROYED(player): nothing left (no such player: true).
	vocabulary.AddCondition("PLAYER_ALL_DESTROYED", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		return !who || !host->HasAnyObjects(*who);
	});
	// PLAYER_LOST_OBJECT_TYPE(player, type): fewer (living) than last seen; the count seen is kept.
	vocabulary.AddCondition("PLAYER_LOST_OBJECT_TYPE", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		if (!who)
			return false;
		const std::int64_t now = host->CountObjects(*who, Text(c, 1), true);
		const std::int64_t seen = host->RecordedCount(*who, Text(c, 1));
		if (now != seen)
			host->RecordCount(*who, Text(c, 1), now);
		return now < seen;
	});
	// SKIRMISH_PLAYER_HAS_COMPARISON_GARRISONED / _CAPTURED_UNITS(player, comparison, count).
	vocabulary.AddCondition("SKIRMISH_PLAYER_HAS_COMPARISON_GARRISONED", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		return who && detail::Compare(host->CountGarrisoned(*who), Integer(c, 1), Integer(c, 2));
	});
	vocabulary.AddCondition("SKIRMISH_PLAYER_HAS_COMPARISON_CAPTURED_UNITS", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		return who && detail::Compare(host->CountCaptured(*who), Integer(c, 1), Integer(c, 2));
	});
	// SKIRMISH_PLAYER_HAS_PREREQUISITE_TO_BUILD(player, object type): evaluateSkirmishPlayerHasPrereqsToBuild.
	vocabulary.AddCondition("SKIRMISH_PLAYER_HAS_PREREQUISITE_TO_BUILD", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		return who && host->CanBuildAny(*who, Text(c, 1));
	});
	// SKIRMISH_PLAYER_HAS_DISCOVERED_PLAYER(player, discovered by): evaluateSkirmishPlayerHasDiscoveredPlayer.
	vocabulary.AddCondition("SKIRMISH_PLAYER_HAS_DISCOVERED_PLAYER", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		const auto by = player(c, 1);
		return who && by && host->Discovered(*who, *by);
	});
	// PLAYER_HAS_OBJECT_COMPARISON(player, comparison, count, object type): evaluatePlayerUnitCondition (the dead count).
	vocabulary.AddCondition("PLAYER_HAS_OBJECT_COMPARISON", [host, player](ScriptCallContext &c) {
		const auto who = player(c, 0);
		return who && detail::Compare(host->CountObjects(*who, Text(c, 3), false), Integer(c, 1), Integer(c, 2));
	});
	vocabulary.AddCondition("TEAM_HAS_UNITS", [host](ScriptCallContext &c) { return host->TeamHasUnits(Text(c, 0)); });
	// UNIT_HEALTH(unit, comparison, percent).
	vocabulary.AddCondition("UNIT_HEALTH", [host](ScriptCallContext &c) {
		const auto health = host->NamedHealthPercent(Text(c, 0));
		return health && detail::Compare(*health, Integer(c, 1), Integer(c, 2));
	});
	// PLAYER_ENABLE / DISABLE_BASE_CONSTRUCTION(player).
	vocabulary.AddAction("PLAYER_ENABLE_BASE_CONSTRUCTION", [host, player](ScriptCallContext &c) {
		if (const auto who = player(c, 0))
			host->SetCanBuildBase(*who, true);
	});
	vocabulary.AddAction("PLAYER_DISABLE_BASE_CONSTRUCTION", [host, player](ScriptCallContext &c) {
		if (const auto who = player(c, 0))
			host->SetCanBuildBase(*who, false);
	});
	// ENABLE_SCORING / DISABLE_SCORING: the score keepers count (or stop counting) what is built, lost and destroyed.
	vocabulary.AddAction("ENABLE_SCORING", [host](ScriptCallContext &) { host->EnableScoring(true); });
	// MAP_SWITCH_BORDER(boundary).
	vocabulary.AddAction("MAP_SWITCH_BORDER", [host](ScriptCallContext &c) { host->SwitchBoundary(Integer(c, 0)); });
	// WATER_CHANGE_HEIGHT_OVER_TIME(water area, height, seconds, damage).
	// WATER_CHANGE_HEIGHT(water area, height).
	vocabulary.AddAction("WATER_CHANGE_HEIGHT", [host](ScriptCallContext &c) { host->SetWaterHeight(Text(c, 0), parameters::Number(c, 1)); });
	vocabulary.AddAction("WATER_CHANGE_HEIGHT_OVER_TIME", [host](ScriptCallContext &c) { host->ChangeWaterHeight(Text(c, 0), parameters::Number(c, 1), parameters::Number(c, 2), parameters::Number(c, 3)); });
	// SCRIPTING_OVERRIDE_HULK_LIFETIME(seconds).
	vocabulary.AddAction("SCRIPTING_OVERRIDE_HULK_LIFETIME", [host](ScriptCallContext &c) { host->OverrideHulkLifetime(parameters::Number(c, 0)); });
	// TECHTREE_MODIFY_BUILDABILITY_OBJECT(object type, status: 0 yes, 1 ignore prerequisites, 2 no, 3 only by AI).
	vocabulary.AddAction("TECHTREE_MODIFY_BUILDABILITY_OBJECT", [host](ScriptCallContext &c) { host->SetBuildable(Text(c, 0), Integer(c, 1)); });
	// COMMANDBAR_REMOVE_BUTTON_OBJECTTYPE(button, object type); COMMANDBAR_ADD_BUTTON_OBJECTTYPE_SLOT(button, object type,
	// slot 1..18).
	vocabulary.AddAction("COMMANDBAR_REMOVE_BUTTON_OBJECTTYPE", [host](ScriptCallContext &c) { host->CommandBarButton(Text(c, 0), Text(c, 1), std::nullopt); });
	vocabulary.AddAction("COMMANDBAR_ADD_BUTTON_OBJECTTYPE_SLOT",
		[host](ScriptCallContext &c) { host->CommandBarButton(Text(c, 0), Text(c, 1), Integer(c, 2) - 1); });
	vocabulary.AddAction("DISABLE_SCORING", [host](ScriptCallContext &) { host->EnableScoring(false); });
	// PLAYER_EXIT_ALL_BUILDINGS(player): doPlayerExitAllBuildings.
	vocabulary.AddAction("PLAYER_EXIT_ALL_BUILDINGS", [host, player](ScriptCallContext &c) {
		if (const auto who = player(c, 0))
			host->PlayerExitAllBuildings(*who);
	});
	// PLAYER_HUNT(player): doPlayerHunt.
	vocabulary.AddAction("PLAYER_HUNT", [host, player](ScriptCallContext &c) {
		if (const auto who = player(c, 0))
			host->PlayerHunt(*who);
	});
	// PLAYER_EXCLUDE_FROM_SCORE_SCREEN(player): excludePlayerFromScoreScreen.
	vocabulary.AddAction("PLAYER_EXCLUDE_FROM_SCORE_SCREEN", [host, player](ScriptCallContext &c) {
		if (const auto who = player(c, 0))
			host->SetListInScoreScreen(*who, false);
	});
	// SKIRMISH_BUILD_BUILDING(structure): always the script's own player (doBuildBuilding: getCurrentPlayer).
	vocabulary.AddAction("SKIRMISH_BUILD_BUILDING", [host](ScriptCallContext &c) {
		if (const auto who = host->ResolvePlayer(c.participant, "<This Player>"))
			host->BuildSpecificBuilding(*who, Text(c, 0));
	});
	// AI_PLAYER_BUILD_SUPPLY_CENTER(player, structure, cash): doBuildSupplyCenter.
	vocabulary.AddAction("AI_PLAYER_BUILD_SUPPLY_CENTER", [host, player](ScriptCallContext &c) {
		if (const auto who = player(c, 0))
			host->BuildBySupplies(*who, Integer(c, 2), Text(c, 1));
	});
	// AI_PLAYER_BUILD_TYPE_NEAREST_TEAM(player, structure, team): doBuildObjectNearestTeam.
	vocabulary.AddAction("AI_PLAYER_BUILD_TYPE_NEAREST_TEAM", [host, player](ScriptCallContext &c) {
		if (const auto who = player(c, 0))
			host->BuildNearestTeam(*who, Text(c, 1), Text(c, 2));
	});
	// SKIRMISH_BUILD_BASE_DEFENSE_FRONT / _FLANK and SKIRMISH_BUILD_STRUCTURE_FRONT / _FLANK(structure): always the
	// script's own player (doBuildBaseDefense / doBuildBaseStructure: getCurrentPlayer).
	for (const auto &[name, flank, named] : {std::tuple{"SKIRMISH_BUILD_BASE_DEFENSE_FRONT", false, false}, std::tuple{"SKIRMISH_BUILD_BASE_DEFENSE_FLANK", true, false},
			 std::tuple{"SKIRMISH_BUILD_STRUCTURE_FRONT", false, true}, std::tuple{"SKIRMISH_BUILD_STRUCTURE_FLANK", true, true}})
		vocabulary.AddAction(name, [host, flank, named](ScriptCallContext &c) {
			if (const auto who = host->ResolvePlayer(c.participant, "<This Player>"))
				host->BuildBaseDefense(*who, named ? Text(c, 0) : std::string{}, flank);
		});
	// SKIRMISH_SPECIAL_POWER_READY(player, power): evaluateSkirmishSpecialPowerIsReady. The call keeps the tick worth
	// looking again (the original's friend_setInt on the power parameter): -1 for a power that does not exist (never
	// true); while not ready, the soonest tick one of the player's will be, or 10 s on.
	vocabulary.AddCondition("SKIRMISH_SPECIAL_POWER_READY", [host, player](ScriptCallContext &c) {
		std::int64_t scratch = 0;
		std::int64_t &memo = c.memo != nullptr ? *c.memo : scratch;
		if (memo == -1)
			return false;
		if (memo > 0 && memo > static_cast<std::int64_t>(c.tick))
			return false;
		const std::uint64_t latest = c.tick + 10 * 30;
		const auto who = player(c, 0);
		const auto [known, ready, next] = who ? host->SpecialPowerReadiness(*who, Text(c, 1), latest) : std::tuple<bool, bool, std::uint64_t>{true, false, latest};
		if (!known)
		{
			memo = -1;
			return false;
		}
		if (!who)
			return false;
		if (ready)
			return true;
		memo = static_cast<std::int64_t>(next);
		return false;
	});
	// SKIRMISH_FIRE_SPECIAL_POWER_AT_MOST_COST(player, power): doSkirmishFireSpecialPowerAtMostCost.
	vocabulary.AddAction("SKIRMISH_FIRE_SPECIAL_POWER_AT_MOST_COST", [host, player](ScriptCallContext &c) {
		const auto self = host->ResolvePlayer(c.participant, "<This Player>");
		if (const auto who = player(c, 0); who && self)
			host->FireSpecialPowerAtMostCost(*self, *who, Text(c, 1));
	});
	// PLAYER_TRIGGERED / MIDWAY / COMPLETED_SPECIAL_POWER(player, power) and their _FROM_NAMED(player, power, unit)
	// forms (evaluatePlayerSpecialPowerFromUnitTriggered and the rest): each one heard is taken by the first to ask.
	const std::array<std::pair<std::string_view, int>, 3> stages{{{"TRIGGERED", 0}, {"MIDWAY", 1}, {"COMPLETED", 2}}};
	for (const auto &[stage, index] : stages)
	{
		vocabulary.AddCondition("PLAYER_" + std::string(stage) + "_SPECIAL_POWER", [host, player, index](ScriptCallContext &c) {
			const auto who = player(c, 0);
			return who && host->TakeSpecialPowerEvent(index, *who, Text(c, 1), std::nullopt);
		});
		vocabulary.AddCondition("PLAYER_" + std::string(stage) + "_SPECIAL_POWER_FROM_NAMED", [host, player, index](ScriptCallContext &c) {
			const auto who = player(c, 0);
			return who && host->TakeSpecialPowerEvent(index, *who, Text(c, 1), Text(c, 2));
		});
	}
	// HAS_FINISHED_SPEECH(speech), HAS_FINISHED_AUDIO(sound): evaluateSpeechHasCompleted / evaluateAudioHasCompleted.
	vocabulary.AddCondition("HAS_FINISHED_SPEECH", [host](ScriptCallContext &c) { return host->SoundComplete(true, Text(c, 0)); });
	// isVideoComplete: nothing ever tells the scripts a movie finished (InGameUI::stopMovie's notifyOfCompletedVideo is
	// left out of the original as a sync error source), so this is never true.
	vocabulary.AddCondition("HAS_FINISHED_VIDEO", [](ScriptCallContext &) { return false; });
	// MULTIPLAYER_ALLIED_DEFEAT in a map's own scripts (a challenge's): evaluateMultiplayerAlliedDefeat ->
	// VictoryConditions::isLocalAlliedDefeat, which needs a single alliance left (m_singleAllianceRemaining); outside a
	// multiplayer game VictoryConditions::update never runs, so it is never true. (In a network match the original asked
	// each machine's own local player; the lockstep simulation cannot, and the local player's scripts in the host answer
	// it there.)
	vocabulary.AddCondition("MULTIPLAYER_ALLIED_DEFEAT", [](ScriptCallContext &) { return false; });
	vocabulary.AddCondition("HAS_FINISHED_AUDIO", [host](ScriptCallContext &c) { return host->SoundComplete(false, Text(c, 0)); });
	// PLAYER_REPAIR_NAMED_STRUCTURE(player, structure): doPlayerRepairStructure (a computer player's repair list).
	vocabulary.AddAction("FREEZE_TIME", [host](ScriptCallContext &) { host->FreezeTime(true); });
	vocabulary.AddAction("UNFREEZE_TIME", [host](ScriptCallContext &) { host->FreezeTime(false); });
	// PLAYER_SELECT_SKILLSET(player, skill set 1..5); SET_BASE_CONSTRUCTION_SPEED(player, seconds between teams).
	vocabulary.AddAction("PLAYER_SELECT_SKILLSET", [host, player](ScriptCallContext &c) {
		if (const auto who = player(c, 0))
			host->SelectSkillset(*who, Integer(c, 1) - 1);
	});
	vocabulary.AddAction("SET_BASE_CONSTRUCTION_SPEED", [host, player](ScriptCallContext &c) {
		if (const auto who = player(c, 0))
			host->SetTeamDelaySeconds(*who, Integer(c, 1));
	});
	vocabulary.AddAction("PLAYER_REPAIR_NAMED_STRUCTURE", [host, player](ScriptCallContext &c) {
		if (const auto who = player(c, 0))
			host->RepairStructure(*who, Text(c, 1));
	});
	// PLAYER_ADD_SKILLPOINTS(player, points), PLAYER_ADD_RANKLEVEL(player, levels), PLAYER_SET_RANKLEVEL(player, level),
	// PLAYER_GRANT_SCIENCE(player, science), PLAYER_PURCHASE_SCIENCE(player, science), PLAYER_AFFECT_RECEIVING_EXPERIENCE
	// (player, modifier): doPlayerAddSkillPoints and the rest, on the player named.
	vocabulary.AddAction("PLAYER_ADD_SKILLPOINTS", [host, player](ScriptCallContext &c) {
		if (const auto who = player(c, 0))
			host->AddSkillPoints(*who, Integer(c, 1));
	});
	vocabulary.AddAction("PLAYER_ADD_RANKLEVEL", [host, player](ScriptCallContext &c) {
		if (const auto who = player(c, 0))
			host->AddRankLevels(*who, Integer(c, 1));
	});
	vocabulary.AddAction("PLAYER_SET_RANKLEVEL", [host, player](ScriptCallContext &c) {
		if (const auto who = player(c, 0))
			host->SetRankLevel(*who, Integer(c, 1));
	});
	vocabulary.AddAction("PLAYER_GRANT_SCIENCE", [host, player](ScriptCallContext &c) {
		if (const auto who = player(c, 0))
			host->GrantScience(*who, Text(c, 1));
	});
	vocabulary.AddAction("PLAYER_PURCHASE_SCIENCE", [host, player](ScriptCallContext &c) {
		if (const auto who = player(c, 0))
			host->PurchaseScience(*who, Text(c, 1));
	});
	// PLAYER_SCIENCE_AVAILABILITY(player, science, availability): doPlayerSetScienceAvailability.
	vocabulary.AddAction("PLAYER_SCIENCE_AVAILABILITY", [host, player](ScriptCallContext &c) {
		if (const auto who = player(c, 0))
			host->SetScienceAvailability(*who, Text(c, 1), Text(c, 2));
	});
	vocabulary.AddAction("PLAYER_AFFECT_RECEIVING_EXPERIENCE", [host, player](ScriptCallContext &c) {
		if (const auto who = player(c, 0))
			host->SetSkillPointsModifier(*who, parameters::Number(c, 1));
	});
	// The shroud: MAP_REVEAL_AT_WAYPOINT / MAP_SHROUD_AT_WAYPOINT(waypoint, radius, player), MAP_REVEAL_ALL /
	// MAP_REVEAL_ALL_PERM / MAP_REVEAL_ALL_UNDO_PERM / MAP_SHROUD_ALL(player), MAP_REVEAL_PERMANENTLY_AT_WAYPOINT(waypoint,
	// radius, player, look name), MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT(look name).
	const auto named = [player](ScriptCallContext &c, std::size_t index) -> std::optional<std::uint32_t> {
		return Text(c, index).empty() ? std::nullopt : player(c, index);
	};
	vocabulary.AddAction("MAP_REVEAL_AT_WAYPOINT",
		[host, named](ScriptCallContext &c) { host->ShroudAtWaypoint(Text(c, 0), parameters::Number(c, 1), named(c, 2), false); });
	vocabulary.AddAction("MAP_SHROUD_AT_WAYPOINT",
		[host, named](ScriptCallContext &c) { host->ShroudAtWaypoint(Text(c, 0), parameters::Number(c, 1), named(c, 2), true); });
	using MapShroud = PlayerScriptHost::MapShroud;
	vocabulary.AddAction("MAP_REVEAL_ALL", [host, named](ScriptCallContext &c) { host->ShroudEntireMap(named(c, 0), MapShroud::RevealAll); });
	vocabulary.AddAction("MAP_REVEAL_ALL_PERM", [host, named](ScriptCallContext &c) { host->ShroudEntireMap(named(c, 0), MapShroud::RevealAllPermanently); });
	vocabulary.AddAction("MAP_REVEAL_ALL_UNDO_PERM", [host, named](ScriptCallContext &c) { host->ShroudEntireMap(named(c, 0), MapShroud::UndoRevealAllPermanently); });
	vocabulary.AddAction("MAP_SHROUD_ALL", [host, named](ScriptCallContext &c) { host->ShroudEntireMap(named(c, 0), MapShroud::ShroudAll); });
	vocabulary.AddAction("MAP_REVEAL_PERMANENTLY_AT_WAYPOINT",
		[host, player](ScriptCallContext &c) { host->RevealNamed(Text(c, 3), Text(c, 0), parameters::Number(c, 1), player(c, 2)); });
	vocabulary.AddAction("MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT", [host](ScriptCallContext &c) { host->UndoRevealNamed(Text(c, 0)); });
	vocabulary.AddAction("OBJECTLIST_ADDOBJECTTYPE", [host](ScriptCallContext &c) { host->ObjectTypeList(Text(c, 0), Text(c, 1), true); });
	vocabulary.AddAction("OBJECTLIST_REMOVEOBJECTTYPE", [host](ScriptCallContext &c) { host->ObjectTypeList(Text(c, 0), Text(c, 1), false); });
}
}
