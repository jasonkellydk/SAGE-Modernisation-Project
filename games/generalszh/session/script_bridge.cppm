export module games.generalszh.session.script_bridge;
import games.generalszh.gameplay.scripts.algorithms.value_groups;
import games.generalszh.gameplay.world.resources.music_progress;
import games.generalszh.gameplay.orders.algorithms.group_orders;
import engine.gameplay.rts.containment.components.garrison;
import games.generalszh.gameplay.world.algorithms.water_levels;
import games.generalszh.gameplay.railroad.components.railcar;
import games.generalszh.gameplay.ai.algorithms.ai_players;
import games.generalszh.gameplay.combat.algorithms.unmanned_vehicles;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.gameplay.world.resources.solo_play;
import games.generalszh.gameplay.score.resources.score_keepers;
import games.generalszh.gameplay.scripts.algorithms.object_status_queries;
import games.generalszh.gameplay.ai.algorithms.tunnel_guards;
import games.generalszh.gameplay.scripts.algorithms.object_edits;
import games.generalszh.gameplay.ai.algorithms.attack_squads;
import games.generalszh.gameplay.world.algorithms.difficulty_bonuses;
import games.generalszh.gameplay.orders.algorithms.wander_orders;
import games.generalszh.gameplay.ai.algorithms.ai_base_building;
import games.generalszh.gameplay.ai.algorithms.ai_superweapon;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import games.generalszh.gameplay.sciences.algorithms.general_ranks;
import games.generalszh.gameplay.ai.algorithms.ai_team_building;
import games.generalszh.gameplay.containment.algorithms.garrisons;
import std;
import games.generalszh.gameplay.combat.algorithms.waypoint_weapon_fire;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.scripting.unit_vocabulary;
export import games.generalszh.scripting.player_vocabulary;
import games.generalszh.gameplay.scripts.algorithms.object_counting;
import games.generalszh.gameplay.construction.algorithms.building;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.rts.economy.resources.player_energy;
import engine.gameplay.common.health.components.health;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.common.identity.components.definition_ref;
import games.generalszh.content.combat.combat_catalog;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.teams.algorithms.reinforcements;
import games.generalszh.gameplay.ai.algorithms.guards;
import games.generalszh.gameplay.production.algorithms.team_building;
import games.generalszh.gameplay.powers.algorithms.special_power_launch;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.gameplay.upgrades.algorithms.grant_upgrade;
import games.generalszh.gameplay.upgrades.algorithms.research;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.vision.resources.shroud_map;
import engine.gameplay.common.health.components.inactive_body;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.status.components.disabled;
import engine.ecs.query.query;
import games.generalszh.gameplay.construction.algorithms.selling;
import games.generalszh.gameplay.scripts.algorithms.team_sequences;
import games.generalszh.gameplay.orders.algorithms.command_buttons;
import games.generalszh.gameplay.orders.algorithms.command_availability;
import games.generalszh.gameplay.orders.algorithms.command_button_readiness;
import games.generalszh.gameplay.scripts.algorithms.unit_script_orders;
import games.generalszh.gameplay.scripts.algorithms.command_button_targets;
import games.generalszh.gameplay.abilities.algorithms.command_button_hunts;
import games.generalszh.content.objects.kind_of;
import engine.gameplay.rts.combat.resources.attack_priorities;
import engine.gameplay.rts.combat.components.aggression;
import games.generalszh.content.objects.kind_of;
import engine.gameplay.common.spatial.components.transform;
import games.generalszh.gameplay.scripts.algorithms.trigger_area_queries;
import games.generalszh.gameplay.abilities.algorithms.special_objects;
import games.generalszh.gameplay.bridges.algorithms.bridges;
import games.generalszh.gameplay.orders.resources.command_bar_overrides;
import games.generalszh.gameplay.orders.resources.buildable_overrides;
import engine.gameplay.rts.death.resources.hulk_lifetime;
import engine.gameplay.rts.topple.components.topple;
import engine.gameplay.rts.death.components.structure_topple;

// The unit script actions and conditions, answered by the gameplay domains:
// this only turns script names (units, teams, waypoints) into entities and
// positions and forwards to the domain algorithms.
export namespace generalszh::session
{
namespace gameplay = engine::gameplay;
namespace domain = generalszh::gameplay;

class ScriptBridge final : public scripting::UnitScriptHost, public scripting::PlayerScriptHost
{
public:
	explicit ScriptBridge(domain::GameWorld &game) : m_game(game) {}

	// A script's actions run by name for no team (friend_executeAction; the session's runtime, set once it exists).
	std::function<void(const std::string &)> runActions;
	// How the computer players reach the scripts (the session's, set once they exist).
	const domain::AiScriptHooks *aiHooks{nullptr};

	void CreateNamedOnTeam(const std::string &name, const std::string &type, const std::string &team, const std::string &waypoint) override
	{
		domain::CreateOnTeamAt(m_game, name, type, team, waypoint);
	}

	void DeleteNamed(const std::string &name) override { domain::RetireNow(m_game, {m_game.names.Find(name)}); }
	void KillNamed(const std::string &name) override { domain::KillNow(m_game, m_game.names.Find(name)); }
	void NamedFollowWaypoints(const std::string &name, const std::string &pathLabel) override
	{
		domain::OrderFollowPath(m_game, m_game.names.Find(name), pathLabel);
	}
	void NamedFireWeaponFollowingWaypointPath(const std::string &name, const std::string &pathLabel) override
	{
		domain::FireWeaponFollowingWaypointPath(m_game, m_game.names.Find(name), pathLabel);
	}
	void MoveNamedTo(const std::string &name, const std::string &waypoint) override
	{
		// doNamedMoveToWaypoint: on its normal locomotors first.
		if (const auto at = Waypoint(waypoint))
		{
			domain::NormalLocomotors(m_game, m_game.names.Find(name));
			// aiMoveToPosition(waypoint->getLocation()): the waypoint on the ground (TerrainLogic::addWaypoint), its layer
			// found from that height (computePath's getLayerForDestination).
			domain::OrderMoveTo(m_game, m_game.names.Find(name), *at, false, true, domain::LayerAt(m_game, *at, m_game.ground.At(*at)));
		}
	}

	void CreateReinforcements(const std::string &team, const std::string &waypoint) override
	{
		domain::CreateReinforcements(m_game, team, waypoint, runActions);
	}
	void TeamFollowWaypoints(const std::string &team, const std::string &pathLabel, bool asTeam) override
	{
		domain::TeamFollowWaypoints(m_game, team, pathLabel, asTeam);
	}
	void TeamDelete(const std::string &team) override { domain::TeamDelete(m_game, team); }
	void TeamMergeInto(const std::string &source, const std::string &target) override { domain::TeamMergeInto(m_game, source, target); }
	bool TeamCreated(const std::string &team) const override { return domain::TeamCreated(m_game, team); }
	void SetTeamState(const std::string &team, const std::string &state) override
	{
		if (const auto index = domain::ResolveTeam(m_game, team))
			m_game.roster.TeamAt(*index).state = state;
	}
	std::optional<std::string> TeamState(const std::string &team) const override
	{
		const auto index = domain::ResolveTeam(m_game, team);
		return index ? std::optional(m_game.roster.TeamAt(*index).state) : std::nullopt;
	}
	bool UnitEmptied(const std::string &unit) override { return domain::UnitEmptied(m_game, m_game.names.Find(unit)); }
	bool TeamOwnedBy(const std::string &team, std::size_t participant, const std::string &player) override
	{
		const auto index = domain::ResolveTeam(m_game, team);
		const auto who = ResolvePlayer(participant, player);
		return index && who && m_game.roster.TeamAt(*index).owner == *who;
	}
	bool TeamDestroyed(const std::string &team) const override { return domain::TeamDestroyed(m_game, team); }
	bool NamedHasObjectStatus(const std::string &unit, const std::string &status) override
	{
		const ecs::Entity entity = m_game.names.Find(unit);
		return entity != ecs::Entity{} && domain::UnitHasObjectStatus(m_game, entity, status, m_game.tick);
	}
	bool TeamHasObjectStatus(const std::string &team, const std::string &status, bool entireTeam) override
	{
		return domain::TeamHasObjectStatus(m_game, team, status, entireTeam, m_game.tick);
	}

	void TeamEnterNamed(const std::string &team, const std::string &transport) override
	{
		const ecs::Entity carrier = m_game.names.Find(transport);
		domain::ForTeam(m_game, team, [&](ecs::Entity entity) { domain::OrderBoard(m_game, entity, carrier); });
	}
	void NamedGarrisonSpecific(const std::string &unit, const std::string &building) override
	{
		const ecs::Entity one = m_game.names.Find(unit);
		domain::GarrisonSpecific(m_game, std::span(&one, 1), m_game.names.Find(building));
	}
	void TeamGarrisonSpecific(const std::string &team, const std::string &building) override
	{
		std::vector<ecs::Entity> members;
		domain::ForTeam(m_game, team, [&](ecs::Entity entity) { members.push_back(entity); });
		domain::GarrisonSpecific(m_game, members, m_game.names.Find(building));
	}
	void NamedGarrisonNearest(const std::string &unit) override
	{
		const ecs::Entity one = m_game.names.Find(unit);
		domain::GarrisonNearest(m_game, std::span(&one, 1));
	}
	void TeamGarrisonNearest(const std::string &team) override
	{
		std::vector<ecs::Entity> members;
		domain::ForTeam(m_game, team, [&](ecs::Entity entity) { members.push_back(entity); });
		domain::GarrisonNearest(m_game, members);
	}
	void PlayerGarrisonAll(const std::string &player) override
	{
		if (const auto index = m_game.roster.FindPlayer(player))
			domain::GarrisonAll(m_game, *index);
	}
	void NamedEnterNamed(const std::string &unit, const std::string &transport) override
	{
		domain::NamedEnterNamed(m_game, m_game.names.Find(unit), m_game.names.Find(transport));
	}
	void NamedFace(const std::string &unit, const std::string &waypoint, const std::string &object) override
	{
		const ecs::Entity target = object.empty() ? ecs::Entity{} : m_game.names.Find(object);
		if (!object.empty() && !m_game.world.IsAlive(target))
			return;
		domain::NamedFace(m_game, m_game.names.Find(unit), waypoint, target);
	}
	void TeamFace(const std::string &team, const std::string &waypoint, const std::string &object) override
	{
		const ecs::Entity target = object.empty() ? ecs::Entity{} : m_game.names.Find(object);
		if (!object.empty() && !m_game.world.IsAlive(target))
			return;
		domain::TeamFace(m_game, team, waypoint, target);
	}
	void SetRepulsor(const std::string &unit, bool on) override { domain::SetRepulsor(m_game, m_game.names.Find(unit), on); }
	void TeamSetRepulsor(const std::string &team, bool on) override { domain::TeamSetRepulsor(m_game, team, on); }
	void SetStealthEnabled(const std::string &unit, bool enabled) override { domain::SetStealthEnabled(m_game, m_game.names.Find(unit), enabled); }
	void NamedSetBoobytrapped(const std::string &type, const std::string &unit) override
	{
		domain::SetBoobytrapped(m_game, type, m_game.names.Find(unit));
	}
	// The team's members newest first (iterate_TeamMemberList), each drawing its own random point.
	void TeamSetBoobytrapped(const std::string &type, const std::string &team) override
	{
		if (const auto index = domain::ResolveTeam(m_game, team))
		{
			const std::vector<ecs::Entity> members = m_game.roster.TeamAt(*index).members;
			for (auto it = members.rbegin(); it != members.rend(); ++it)
				domain::SetBoobytrapped(m_game, type, *it);
		}
	}
	void TeamSetStealthEnabled(const std::string &team, bool enabled) override
	{
		domain::ForTeam(m_game, team, [&](ecs::Entity entity) { domain::SetStealthEnabled(m_game, entity, enabled); });
	}
	void NamedExitBuilding(const std::string &unit) override { domain::NamedExitBuilding(m_game, m_game.names.Find(unit)); }
	void TeamExitAllBuildings(const std::string &team) override { domain::TeamExitAllBuildings(m_game, team); }
	void ExitSpecificBuilding(const std::string &building) override { domain::ExitSpecificBuilding(m_game, m_game.names.Find(building)); }
	bool NamedGuard(const std::string &unit) override { return domain::NamedGuard(m_game, m_game.names.Find(unit)); }
	void PlayerObjectsEnabled(const std::string &player, const std::string &type, bool enable) override
	{
		if (const auto index = player.empty() ? std::nullopt : m_game.roster.FindPlayer(player))
			domain::SetPlayerObjectsEnabled(m_game, *index, type, enable);
	}
	void NamedStoppingDistance(const std::string &unit, Engine::Math::Fixed distance) override
	{
		domain::SetStoppingDistance(m_game, m_game.names.Find(unit), distance);
	}
	void TeamStoppingDistance(const std::string &team, Engine::Math::Fixed distance) override { domain::TeamSetStoppingDistance(m_game, team, distance); }
	void SetUnmanned(const std::string &unit) override { domain::SetUnmanned(m_game, m_game.names.Find(unit)); }
	void TeamSetUnmanned(const std::string &team) override
	{
		domain::ForTeam(m_game, team, [&](ecs::Entity entity) { domain::SetUnmanned(m_game, entity); });
	}
	// doTeamUseCommandButtonAbilityAtWaypoint -> groupDoCommandButtonAtPosition: every member uses the button there.
	void TeamUseCommandButtonAtWaypoint(const std::string &team, const std::string &button, const std::string &waypoint) override
	{
		const auto at = Waypoint(waypoint);
		const auto *found = m_game.templates.Content().commands.Button(button);
		if (!at || found == nullptr)
			return;
		domain::ForTeam(m_game, team, [&](ecs::Entity entity) { domain::DoCommandButtonAtPosition(m_game, entity, *found, *at); });
	}
	void NamedExitAll(const std::string &transport) override { domain::OrderUnload(m_game, m_game.names.Find(transport)); }
	void TeamExitAll(const std::string &team) override
	{
		domain::ForTeam(m_game, team, [&](ecs::Entity entity) { domain::OrderUnload(m_game, entity); });
	}

	// doNamedUseCommandButtonAbilityAtWaypoint: each button of the unit's command set so named used at the waypoint
	// (doCommandButtonAtPosition).
	void NamedUseCommandButtonAtWaypoint(const std::string &name, const std::string &button, const std::string &waypoint) override
	{
		const ecs::Entity unit = m_game.names.Find(name);
		const auto at = Waypoint(waypoint);
		if (!m_game.world.IsAlive(unit) || !at)
			return;
		const content::GameContent &content = m_game.templates.Content();
		const auto set = domain::EffectiveCommandSet(content.commands, m_game.world.FindResource<domain::CommandBarOverrides>(), domain::CommandSetOf(m_game, unit));
		if (!set)
			return;
		for (const std::string &slot : set->buttons)
			if (const auto *found = slot.empty() || slot != button ? nullptr : content.commands.Button(slot))
				domain::DoCommandButtonAtPosition(m_game, unit, *found, *at);
	}
	// Object::doCommandButtonUsingWaypoints for each button of the unit's command set so named: only one that may use
	// waypoints (CAN_USE_WAYPOINTS) and fires a special power does anything; not while the unit is disabled.
	void NamedUseCommandButtonUsingWaypointPath(const std::string &name, const std::string &button, const std::string &path) override
	{
		const ecs::Entity unit = m_game.names.Find(name);
		const auto *at = m_game.world.IsAlive(unit) ? m_game.world.Get<engine::gameplay::Transform>(unit) : nullptr;
		if (at == nullptr)
			return;
		const std::uint32_t waypoint = m_game.waypoints.ClosestOnPath(at->position.XY(), path);
		if (waypoint == engine::gameplay::WaypointGraph::None)
			return;
		const content::GameContent &content = m_game.templates.Content();
		const auto set = domain::EffectiveCommandSet(content.commands, m_game.world.FindResource<domain::CommandBarOverrides>(), domain::CommandSetOf(m_game, unit));
		if (!set)
			return;
		for (const std::string &slot : set->buttons)
		{
			const auto *found = slot.empty() || slot != button ? nullptr : content.commands.Button(slot);
			if (found == nullptr || (found->options & content::button_option::CanUseWaypoints) == 0 || found->command != content::ButtonCommand::SpecialPower ||
				found->specialPower.empty())
				continue;
			domain::FireSpecialPowerUsingWaypoints(m_game, unit, found->specialPower, waypoint);
		}
	}
	void NamedFireSpecialPowerAtWaypoint(const std::string &name, const std::string &power, const std::string &waypoint) override
	{
		if (const auto at = Waypoint(waypoint))
			domain::FireSpecialPower(m_game, m_game.names.Find(name), power, *at, true);
	}

	void NamedHideSpecialPowerDisplay(const std::string &name, bool hide) override
	{
		const ecs::Entity unit = m_game.names.Find(name);
		if (auto *timers = m_game.world.IsAlive(unit) ? m_game.world.Get<gameplay::SpecialPowerTimers>(unit) : nullptr)
			gameplay::HidePublicTimers(*timers, hide);
	}
	// doNamedFireSpecialPowerAtNamed: straight to the unit's module (no science or readiness asked).
	void NamedFireSpecialPowerAtNamed(const std::string &name, const std::string &power, const std::string &target) override
	{
		const ecs::Entity victim = m_game.names.Find(target);
		if (m_game.world.IsAlive(victim))
			domain::FireSpecialPowerAtObject(m_game, m_game.names.Find(name), power, victim, true);
	}

	void TeamHunt(const std::string &team) override { domain::TeamHunt(m_game, team); }
	void RecruitTeam(const std::string &team, Engine::Math::Fixed radius) override
	{
		if (auto *ais = m_game.world.FindResource<domain::AiPlayers>(); ais != nullptr && aiHooks != nullptr)
			domain::RecruitTeamByScript(m_game, *ais, team, radius, *aiHooks);
	}
	void ChangeTeamPriority(const std::string &team, bool success) override
	{
		if (const auto index = domain::ResolveTeam(m_game, team))
			if (auto *ais = m_game.world.FindResource<domain::AiPlayers>())
				domain::ChangeProductionPriority(m_game, *ais, *index, success);
	}
	void BuildTeam(const std::string &team) override
	{
		if (auto *ais = m_game.world.FindResource<domain::AiPlayers>(); ais != nullptr && aiHooks != nullptr)
			domain::BuildTeamByScript(m_game, *ais, team, *aiHooks);
	}
	void TeamGuard(const std::string &team, const std::string &waypoint) override { domain::TeamGuard(m_game, team, waypoint); }
	void TeamGuardObject(const std::string &team, const std::string &unit) override { domain::TeamGuardObject(m_game, team, m_game.names.Find(unit)); }
	void TeamGuardSupplyCenter(const std::string &team, std::int64_t supplies) override { domain::GuardSupplyCenter(m_game, team, supplies); }
	void NamedAttackNamed(const std::string &attacker, const std::string &target) override
	{
		// doNamedAttack: on its normal locomotors first.
		domain::NormalLocomotors(m_game, m_game.names.Find(attacker));
		domain::OrderAttack(m_game, m_game.names.Find(attacker), m_game.names.Find(target), 0, engine::gameplay::CommandSource::Script);
	}
	void NamedSetHeld(const std::string &name, bool held) override { domain::OrderHold(m_game, m_game.names.Find(name), held); }
	// ContainModuleInterface::setEvacDisposition: only a garrison's matters (GarrisonContain), measured by its geometry.
	void NamedSetEvacDisposition(const std::string &name, std::int64_t disposition) override
	{
		const ecs::Entity unit = m_game.names.Find(name);
		auto *garrison = m_game.world.IsAlive(unit) ? m_game.world.Get<gameplay::Garrison>(unit) : nullptr;
		const auto *ref = garrison != nullptr ? m_game.world.Get<gameplay::DefinitionRef>(unit) : nullptr;
		if (ref == nullptr)
			return;
		const auto &geometry = m_game.templates.DefinitionAt(ref->index).geometry;
		garrison->evac = disposition == 1 || disposition == 2 ? static_cast<std::uint8_t>(disposition) : 0u;
		garrison->halfLength = geometry.majorRadius;
		garrison->halfWidth = geometry.minorRadius;
	}
	void SetTrainHeld(const std::string &name, bool held) override
	{
		if (auto *train = m_game.world.IsAlive(m_game.names.Find(name)) ? m_game.world.Get<domain::Railcar>(m_game.names.Find(name)) : nullptr)
			train->held = held ? 1 : 0;
	}
	// doNamedStop: aiIdle.
	void NamedStop(const std::string &name) override { domain::OrderStop(m_game, m_game.names.Find(name)); }
	bool NamedIdle(const std::string &name) override
	{
		const ecs::Entity unit = m_game.names.Find(name);
		if (!m_game.world.IsAlive(unit) || !domain::HasAi(m_game, unit))
			return false;
		domain::OrderStop(m_game, unit);
		return true;
	}
	void TeamStop(const std::string &team, bool disband) override { domain::TeamStop(m_game, team, disband); }
	void TeamMoveTo(const std::string &team, const std::string &waypoint) override { domain::TeamMoveTo(m_game, team, waypoint); }
	void TeamCaptureNearestUnowned(const std::string &team) override { domain::TeamCaptureNearestUnowned(m_game, team); }
	void TeamGuardInTunnelNetwork(const std::string &team) override { domain::TeamGuardInTunnelNetwork(m_game, team); }
	void NamedHunt(const std::string &unit) override { domain::UnitHunt(m_game, m_game.names.Find(unit)); }
	void TeamWander(const std::string &team, const std::string &pathLabel, bool panic) override
	{
		if (panic)
			domain::TeamPanic(m_game, team, pathLabel);
		else
			domain::TeamWander(m_game, team, pathLabel);
	}
	void TeamWanderInPlace(const std::string &team) override { domain::TeamWanderInPlace(m_game, team); }
	void NamedCustomColor(const std::string &unit, std::uint32_t argb) override { domain::SetIndicatorColor(m_game, m_game.names.Find(unit), argb); }
	void AllowBonuses(bool allow) override { domain::AllowDifficultyBonuses(m_game, allow); }
	void DeleteAllUnmanned() override { domain::DeleteAllUnmanned(m_game); }
	void ObjectPanelFlag(const std::string &who, bool team, const std::string &flag, bool value) override
	{
		if (!team)
		{
			domain::SetObjectPanelFlag(m_game, m_game.names.Find(who), flag, value);
			return;
		}
		if (const auto index = domain::ResolveTeam(m_game, who))
		{
			const std::vector<ecs::Entity> members = m_game.roster.TeamAt(*index).members;
			for (const ecs::Entity member : members)
				domain::SetObjectPanelFlag(m_game, member, flag, value);
		}
	}
	void HumansIdleOrResume(bool idle) override
	{
		for (std::uint32_t player = 0; player < m_game.roster.PlayerCount(); ++player)
			if (m_game.roster.PlayerAt(player).human)
				domain::SetUnitsIdleOrResume(m_game, player, idle);
	}
	void NamedAttackTeam(const std::string &unit, const std::string &team) override { domain::NamedAttackTeam(m_game, m_game.names.Find(unit), team); }
	void TeamAttackTeam(const std::string &team, const std::string &victims) override { domain::TeamAttackTeam(m_game, team, victims); }
	void ChooseVictimAlwaysUsesNormal(bool enable) override
	{
		if (auto *squads = m_game.world.FindResource<domain::AttackSquads>())
			squads->victimsAlwaysNormal = enable;
	}
	void NamedPowerCountdown(const std::string &unit, const std::string &power, int edit, std::int64_t seconds) override
	{
		domain::EditPowerCountdown(m_game, m_game.names.Find(unit), power, static_cast<domain::CountdownEdit>(edit), seconds);
	}
	void FollowWaypointsExact(const std::string &who, const std::string &path, bool team, bool asTeam) override
	{
		if (team)
			domain::TeamFollowWaypoints(m_game, who, path, asTeam, true);
		else
			domain::OrderFollowPath(m_game, m_game.names.Find(who), path, std::nullopt, true);
	}
	void TeamAttackNamed(const std::string &team, const std::string &unit) override
	{
		const auto index = domain::ResolveTeam(m_game, team);
		const ecs::Entity victim = m_game.names.Find(unit);
		if (!index || !m_game.world.IsAlive(victim))
			return;
		// doTeamAttackNamed: the team as an AIGroup (getTeamAsAIGroup) -> groupAttackObject(victim, NO_MAX_SHOTS_LIMIT,
		// CMD_FROM_SCRIPT).
		domain::GroupAttackObject(m_game, domain::button_target_detail::GroupOf(m_game, *index), victim, 0, engine::gameplay::CommandSource::Script);
	}
	void CreateObject(const std::string &name, const std::string &type, const std::string &team, Engine::Math::FixedVector3 at, Engine::Math::Fixed angle) override
	{
		domain::CreateObjectAt(m_game, name, type, team, at, angle);
	}
	void SetRelationOverride(std::size_t participant, OverrideKind kind, const std::string &from, const std::string &to,
		std::optional<std::int64_t> relationship) override
	{
		auto &relationships = m_game.world.Resource<gameplay::Relationships>();
		const auto playerOf = [&](const std::string &name) { return m_game.roster.FindPlayer(PlayerNamed(participant, name)); };
		std::optional<gameplay::Relationship> kindOf;
		if (relationship)
			kindOf = *relationship == 0 ? gameplay::Relationship::Enemies : *relationship == 2 ? gameplay::Relationship::Allies : gameplay::Relationship::Neutral;
		switch (kind)
		{
		case OverrideKind::TeamToTeam:
			if (const auto a = domain::ResolveTeam(m_game, from), b = domain::ResolveTeam(m_game, to); a && b)
				relationships.SetTeamToTeam(*a, *b, kindOf);
			break;
		case OverrideKind::TeamToPlayer:
			if (const auto a = domain::ResolveTeam(m_game, from); a)
				if (const auto b = playerOf(to))
					relationships.SetTeamToPlayer(*a, *b, kindOf);
			break;
		case OverrideKind::PlayerToTeam:
			if (const auto a = playerOf(from))
				if (const auto b = domain::ResolveTeam(m_game, to))
					relationships.SetPlayerToTeam(*a, *b, kindOf);
			break;
		}
	}
	void ClearTeamOverrides(const std::string &team) override
	{
		if (const auto index = domain::ResolveTeam(m_game, team))
			m_game.world.Resource<gameplay::Relationships>().ClearTeam(*index);
	}
	void NamedSetAttitude(const std::string &unit, std::int64_t attitude) override { domain::SetAttitude(m_game, m_game.names.Find(unit), attitude); }
	void TeamSetAttitude(const std::string &team, std::int64_t attitude) override { domain::TeamSetAttitude(m_game, team, attitude); }
	void TeamFollowApproachPath(std::size_t participant, const std::string &team, const std::string &pathLabel, bool asTeam) override
	{
		if (const auto enemy = SkirmishEnemyOf(participant))
			domain::TeamFollowApproachPath(m_game, team, pathLabel, StartIndex(*enemy), asTeam);
	}
	void TeamMoveToApproachPath(std::size_t participant, const std::string &team, const std::string &pathLabel) override
	{
		if (const auto enemy = SkirmishEnemyOf(participant))
			domain::TeamMoveToApproachPath(m_game, team, pathLabel, StartIndex(*enemy));
	}
	// doNamedDamage: DAMAGE_UNRESISTABLE of the amount.
	void NamedDamage(const std::string &name, std::int64_t amount) override
	{
		domain::DamageNow(m_game, m_game.names.Find(name), Engine::Math::Fixed::FromInt(amount));
	}
	void TeamDamage(const std::string &team, Engine::Math::Fixed amount) override { domain::TeamDamage(m_game, team, amount); }
	void TeamKill(const std::string &team) override { domain::TeamKill(m_game, team); }
	void TeamDeleteLiving(const std::string &team) override { domain::TeamDelete(m_game, team, true); }
	void PlayerKill(const std::string &player) override
	{
		if (const auto index = m_game.roster.FindPlayer(player))
			domain::PlayerKill(m_game, *index);
	}
	void PlayerTransferAssets(const std::string &from, const std::string &to) override
	{
		const auto source = m_game.roster.FindPlayer(from), destination = m_game.roster.FindPlayer(to);
		if (source && destination)
			domain::PlayerTransferAssets(m_game, *destination, *source);
	}
	void SetToppleDirection(const std::string &unit, Engine::Math::FixedVector3 direction) override
	{
		// The thing named now (the original keeps it by name for whatever bears it when it topples).
		const ecs::Entity named = m_game.names.Find(unit);
		if (auto *topple = m_game.world.IsAlive(named) ? m_game.world.Get<gameplay::Topple>(named) : nullptr)
		{
			topple->scriptedDirection = direction.XY();
			topple->scripted = 1;
		}
		// A structure that topples when it dies (StructureToppleUpdate: adjustToppleDirection).
		else if (m_game.world.IsAlive(named))
		{
			if (!m_game.world.Has<gameplay::ScriptedTopple>(named))
				m_game.world.Add<gameplay::ScriptedTopple>(named);
			m_game.world.Get<gameplay::ScriptedTopple>(named)->direction = direction.XY();
		}
	}
	bool NamedSelected(const std::string &unit) override
	{
		const ecs::Entity named = m_game.names.Find(unit);
		return m_selected && m_game.world.IsAlive(named) && m_selected(named);
	}
	// Whether the local player has a unit selected (the presentation's answer; none: nobody, a multiplayer game).
	void SetSelectionQuery(std::function<bool(ecs::Entity)> selected) { m_selected = std::move(selected); }
	void DestroyAllContained(const std::string &unit) override
	{
		// iterateContained(killTheObject): each one aboard killed.
		const ecs::Entity holder = m_game.names.Find(unit);
		if (!m_game.world.IsAlive(holder))
			return;
		const auto aboard = m_game.manifest.Aboard(holder);
		const std::vector<ecs::Entity> riders(aboard.begin(), aboard.end());
		for (const ecs::Entity rider : riders)
			domain::KillNow(m_game, rider);
	}
	// doNamedTransferAssetsToPlayer: onto the player's default team.
	void NamedTransferToPlayer(const std::string &name, const std::string &player) override
	{
		const auto index = m_game.roster.FindPlayer(player);
		const auto home = index ? m_game.roster.DefaultTeam(*index) : std::nullopt;
		const ecs::Entity unit = m_game.names.Find(name);
		if (home && m_game.world.IsAlive(unit))
			domain::ChangeTeam(m_game, unit, *home);
	}
	// doTransferTeamToPlayer: Team::setControllingPlayer, its members with it.
	void TeamTransferToPlayer(const std::string &team, const std::string &player) override
	{
		const auto index = m_game.roster.FindPlayer(player);
		const auto which = domain::ResolveTeam(m_game, team);
		if (!index || !which)
			return;
		m_game.roster.TeamAt(*which).owner = *index;
		domain::ForTeam(m_game, "#" + std::to_string(*which), [&](ecs::Entity entity) {
			if (auto *owner = m_game.world.Get<gameplay::Owner>(entity))
				owner->player = *index;
		});
	}
	void PlayerGiveMoney(const std::string &player, std::int64_t amount) override
	{
		const auto index = m_game.roster.FindPlayer(player);
		if (!index)
			return;
		auto &money = m_game.world.Resource<gameplay::PlayerMoney>();
		// ScriptActions::doAddCash: a negative amount withdrawn (as much as there is), else deposited; both heard.
		if (amount < 0)
			money.WithdrawUpTo(*index, -amount);
		else
			money.Deposit(*index, amount);
	}
	// doSetMoney: all of it withdrawn, then the amount deposited.
	bool InArea(std::size_t participant, AreaQuery test, const std::string &subject, const std::string &area, std::int64_t surfaces) override
	{
		return domain::TestArea(m_game, static_cast<domain::AreaTest>(test), subject, QualifiedArea(participant, area), surfaces, m_game.tick);
	}
	std::optional<std::int64_t> CountInArea(std::size_t participant, int what, const std::string &player, const std::string &area,
		const std::string &types, std::int64_t kind) override
	{
		const auto index = player.empty() ? std::nullopt : m_game.roster.FindPlayer(player);
		const std::uint32_t which = QualifiedArea(participant, area);
		if (!index || which == gameplay::TriggerAreas::None)
			return std::nullopt;
		return domain::CountInArea(m_game, static_cast<domain::AreaCount>(what), *index, which, types, kind);
	}
	bool AreaExists(std::size_t participant, const std::string &area) override { return QualifiedArea(participant, area) != gameplay::TriggerAreas::None; }
	bool TechBuildingNear(std::size_t participant, const std::string &player, Engine::Math::Fixed distance, const std::string &area) override
	{
		const auto index = player.empty() ? std::nullopt : m_game.roster.FindPlayer(player);
		const std::uint32_t which = QualifiedArea(participant, area);
		return index && which != gameplay::TriggerAreas::None && domain::TechBuildingNear(m_game, *index, which, distance);
	}
	bool SuppliesNear(std::size_t participant, const std::string &player, Engine::Math::Fixed distance, const std::string &area,
		Engine::Math::Fixed value) override
	{
		const auto index = player.empty() ? std::nullopt : m_game.roster.FindPlayer(player);
		const std::uint32_t which = QualifiedArea(participant, area);
		return index && which != gameplay::TriggerAreas::None && domain::SuppliesNear(m_game, *index, which, distance, value);
	}
	// evaluateSkirmishUnownedFactionUnitComparison: the neutral player's things disabled as unmanned.
	std::int64_t UnownedFactionUnits() override
	{
		std::int64_t count = 0;
		for (std::uint32_t team = 0; team < m_game.roster.TeamCount(); ++team)
		{
			const std::uint32_t owner = m_game.roster.TeamAt(team).owner;
			if (owner >= m_game.roster.PlayerCount() || !m_game.roster.PlayerAt(owner).name.empty())
				continue;
			for (const ecs::Entity member : m_game.roster.TeamAt(team).members)
				if (const auto *off = m_game.world.IsAlive(member) ? m_game.world.Get<gameplay::Disabled>(member) : nullptr;
					off != nullptr && (off->mask & gameplay::disabled_type::Unmanned) != 0)
					++count;
		}
		return count;
	}
	// Player::isSupplySourceSafe -> AIPlayer::isSupplySourceSafe: its findSupplyCenter's warehouse (none: safe) has no enemy
	// near (isLocationSafe); a player without an AI is always safe. The original re-asked every 2 s; this asks each time.
	// Player::isSupplySourceAttacked: a computer player's AI looks (a human player's never finds one).
	bool SupplySourceAttacked(const std::string &player) override
	{
		const auto index = player.empty() ? std::nullopt : m_game.roster.FindPlayer(player);
		auto *ais = m_game.world.FindResource<domain::AiPlayers>();
		auto *ai = index && ais != nullptr ? ais->Of(*index) : nullptr;
		return ai != nullptr && domain::SupplySourceAttacked(m_game, *ai);
	}
	bool SupplySourceSafe(const std::string &player, std::int64_t minimumSupplies) override
	{
		const auto index = player.empty() ? std::nullopt : m_game.roster.FindPlayer(player);
		if (!index)
			return false;
		auto *ais = m_game.world.FindResource<domain::AiPlayers>();
		auto *ai = ais != nullptr ? ais->Of(*index) : nullptr;
		if (ai == nullptr)
			return true;
		const ecs::Entity warehouse = domain::FindSupplyCenter(m_game, *ais, *ai, minimumSupplies);
		const auto *at = m_game.world.IsAlive(warehouse) ? m_game.world.Get<gameplay::Transform>(warehouse) : nullptr;
		const auto *ref = at != nullptr ? m_game.world.Get<gameplay::DefinitionRef>(warehouse) : nullptr;
		if (ref == nullptr)
			return true;
		return domain::IsLocationSafe(m_game, *index, at->position.XY(), m_game.templates.DefinitionAt(ref->index));
	}
	// setPriorityThing: a type, or each type of the list of that name (the list first, as the original).
	void SetAttackPriorityThing(const std::string &set, const std::string &type, std::int64_t priority) override
	{
		auto &priorities = m_game.world.Resource<gameplay::AttackPriorities>();
		const std::vector<std::string> types = domain::ObjectTypesFrom(m_game, m_game.world.Resource<domain::ScriptRecords>(), type);
		if (types.empty())
			return; // no such type: nothing (and no set made)
		const std::uint16_t index = priorities.Find(set, true);
		for (const std::string &name : types)
			if (const auto *object = m_game.templates.Content().objects.Find(name))
				priorities.SetPriority(index, m_game.templates.Definition(*object), static_cast<std::int32_t>(priority));
	}
	// setPriorityKind: every template of the kind.
	void SetAttackPriorityKind(const std::string &set, std::int64_t kind, std::int64_t priority) override
	{
		if (kind < 0 || static_cast<std::size_t>(kind) >= content::KindOfNames.size())
			return;
		auto &priorities = m_game.world.Resource<gameplay::AttackPriorities>();
		const std::uint16_t index = priorities.Find(set, true);
		const std::string_view name = content::KindOfNames[static_cast<std::size_t>(kind)];
		for (const auto &[template_name, object] : m_game.templates.Content().objects)
			if (object.Is(name))
				priorities.SetPriority(index, m_game.templates.Definition(object), static_cast<std::int32_t>(priority));
	}
	void SetDefaultAttackPriority(const std::string &set, std::int64_t priority) override
	{
		auto &priorities = m_game.world.Resource<gameplay::AttackPriorities>();
		priorities.SetDefault(priorities.Find(set, true), static_cast<std::int32_t>(priority));
	}
	// getAttackInfo: the set of the name (none: the default, nearest first); every member with an AI takes it. A team
	// keeps a set that exists for the members it gets later (setAttackPriorityName, on its prototype).
	void ApplyAttackPrioritySet(const std::string &subject, bool team, const std::string &set) override
	{
		const std::uint16_t index = std::as_const(m_game.world.Resource<gameplay::AttackPriorities>()).Find(set);
		std::vector<ecs::Entity> units;
		if (team)
		{
			if (const auto resolved = domain::ResolveTeam(m_game, subject); resolved && index != 0)
				m_game.roster.TeamAt(m_game.roster.PrototypeOf(*resolved)).prioritySet = index;
			domain::ForTeam(m_game, subject, [&](ecs::Entity entity) { units.push_back(entity); });
		}
		else
			units.push_back(m_game.names.Find(subject));
		for (const ecs::Entity unit : units)
			if (auto *aggression = m_game.world.IsAlive(unit) ? m_game.world.Get<gameplay::Aggression>(unit) : nullptr)
				aggression->prioritySet = index;
	}
	std::optional<std::uint32_t> TeamIndex(const std::string &team) override { return domain::ResolveTeam(m_game, team); }
	bool TeamCommandButtonReady(const std::string &team, const std::string &button, bool all) override
	{
		return domain::TeamCommandButtonReady(m_game, team, button, all);
	}
	bool TeamContained(const std::string &team, bool all) override
	{
		const auto index = domain::ResolveTeam(m_game, team);
		return index && domain::TeamContained(m_game, *index, all);
	}
	void TeamUseCommandButton(const std::string &team, const std::string &button, std::optional<Engine::Math::Fixed> percentage) override
	{
		const auto *found = m_game.templates.Content().commands.Button(button);
		if (found == nullptr)
			return;
		std::vector<ecs::Entity> members;
		domain::ForTeam(m_game, team, [&](ecs::Entity entity) { members.push_back(entity); });
		if (percentage)
		{
			// doTeamPartialUseCommandButton: those it is valid on, the first (percentage / 100 of them, truncated).
			std::erase_if(members, [&](ecs::Entity entity) { return !domain::ButtonValidOn(m_game, entity, *found); });
			const auto count = static_cast<std::size_t>(std::max<std::int64_t>(
				0, (*percentage * Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(members.size())) / Engine::Math::Fixed::FromInt(100)).Floor()));
			members.resize(std::min(count, members.size()));
		}
		for (const ecs::Entity member : members)
			domain::DoCommandButton(m_game, member, *found);
	}
	void TeamHuntWithCommandButton(const std::string &team, const std::string &button) override { domain::TeamHuntWithCommandButton(m_game, team, button); }
	void NamedUseCommandButton(const std::string &unit, const std::string &button, const std::optional<std::string> &target) override
	{
		std::optional<ecs::Entity> at;
		if (target)
			at = m_game.names.Find(*target);
		domain::NamedUseCommandButton(m_game, m_game.names.Find(unit), button, at);
	}
	void TeamUseCommandButtonOnNamed(const std::string &team, const std::string &button, const std::string &target, bool checked) override
	{
		const ecs::Entity victim = m_game.names.Find(target);
		if (checked)
			domain::TeamAllUseCommandButtonOnNamed(m_game, team, button, victim);
		else
			domain::TeamUseCommandButtonAtObject(m_game, team, button, victim);
	}
	void TeamUseCommandButtonOnNearest(const std::string &team, const std::string &button, int nearest, std::int64_t kind, const std::string &type) override
	{
		using Target = domain::ButtonTarget;
		constexpr Target targets[] = {Target::Enemy, Target::GarrisonableBuilding, Target::KindOf, Target::Building, Target::BuildingClass, Target::ObjectType};
		if (nearest < 0 || nearest >= 6)
			return;
		const std::string kindName =
			kind >= 0 && static_cast<std::size_t>(kind) < content::KindOfNames.size() ? std::string(content::KindOfNames[static_cast<std::size_t>(kind)]) : std::string{};
		if ((targets[nearest] == Target::KindOf || targets[nearest] == Target::BuildingClass) && kindName.empty())
			return;
		domain::TeamAllUseCommandButtonOnNearest(m_game, team, button, targets[nearest], kindName, type);
	}
	void TeamUseCommandButtonOnMostValuable(const std::string &team, const std::string &button, Engine::Math::Fixed range) override
	{
		domain::TeamUseCommandButtonOnMostValuable(m_game, team, button, range);
	}
	void TeamAttackNearestGroupWithValue(const std::string &team, std::int32_t comparison, std::int64_t value) override
	{
		domain::SkirmishAttackNearestGroupWithValue(m_game, team, comparison, value);
	}
	void TeamLoadTransports(const std::string &team) override { domain::TeamLoadTransports(m_game, team); }
	void UnitMoveTowardsNearest(std::size_t participant, const std::string &unit, const std::string &type, const std::string &area) override
	{
		domain::UnitMoveTowardsNearest(m_game, m_game.names.Find(unit), type, QualifiedArea(participant, area));
	}
	void TeamMoveTowardsNearest(std::size_t participant, const std::string &team, const std::string &type, const std::string &area) override
	{
		domain::TeamMoveTowardsNearest(m_game, team, type, QualifiedArea(participant, area));
	}
	void PlayerSellEverything(const std::string &player) override
	{
		if (const auto index = player.empty() ? std::nullopt : m_game.roster.FindPlayer(player))
			domain::SellEverything(m_game, *index);
	}
	void TeamRecruitable(const std::string &team, bool recruitable) override
	{
		if (const auto index = domain::ResolveTeam(m_game, team))
			m_game.roster.TeamAt(*index).recruitable = static_cast<std::int8_t>(recruitable ? 1 : 0);
	}
	void AreaOrder(std::size_t participant, const std::string &subject, bool team, const std::string &area, bool guard) override
	{
		const std::uint32_t which = QualifiedArea(participant, area);
		std::vector<ecs::Entity> units;
		if (team)
			domain::ForTeam(m_game, subject, [&](ecs::Entity entity) { units.push_back(entity); });
		else
		{
			units.push_back(m_game.names.Find(subject));
			// doNamedAttackArea: on its normal locomotors first.
			if (!guard)
				domain::NormalLocomotors(m_game, units.front());
		}
		if (guard)
			domain::OrderAreaGuard(m_game, units, which);
		else
			domain::OrderAreaAttack(m_game, units, which);
	}
	void PlayerSetMoney(const std::string &player, std::int64_t amount) override
	{
		const auto index = m_game.roster.FindPlayer(player);
		if (!index)
			return;
		auto &money = m_game.world.Resource<gameplay::PlayerMoney>();
		// ScriptActions::doSetMoney as EA wrote it: withdraw(countMoney()), then deposit(money): both heard (the fork's
		// deposit(money, FALSE, FALSE) silences the second; EA's is the authority).
		money.WithdrawUpTo(*index, money.Balance(*index));
		money.Deposit(*index, std::max<std::int64_t>(0, amount));
	}
	// Player::buildUpgrade -> AIPlayer::buildUpgrade (computer players only): at the first structure of its build list
	// that may research it.
	void PlayerBuildUpgrade(const std::string &player, const std::string &upgrade) override
	{
		const auto index = m_game.roster.FindPlayer(player);
		auto *ais = m_game.world.FindResource<domain::AiPlayers>();
		const auto *ai = index && ais != nullptr ? ais->Of(*index) : nullptr;
		if (ai == nullptr)
			return;
		std::vector<ecs::Entity> factories;
		for (const domain::AiBuildSlot &slot : ai->buildList)
			factories.push_back(slot.built);
		domain::PlayerBuildUpgrade(m_game, *index, upgrade, factories);
	}
	void NamedReceiveUpgrade(const std::string &name, const std::string &upgrade) override
	{
		domain::GiveObjectUpgrade(m_game, m_game.names.Find(name), upgrade);
	}

	void SetRankLimit(std::int64_t level) override
	{
		for (std::uint32_t player = 0; player < m_game.roster.PlayerCount(); ++player)
			m_game.roster.PlayerAt(player).rankLimit = static_cast<std::uint32_t>(std::max<std::int64_t>(1, level)); // setRankLevelLimit: at least 1
	}
	void DisableUnitConstruction(const std::string &player) override
	{
		if (const auto index = m_game.roster.FindPlayer(player))
			m_game.roster.PlayerAt(*index).unitConstructionEnabled = false;
	}
	// ScriptActions::doSetCaveIndex: findCave looks for the object's CaveContain module; no Zero Hour object has one
	// (module:CaveContain is not applicable: GLA tunnels are TunnelContain), so the action finds nothing to set.
	void SetCaveIndex(const std::string &cave, std::int64_t) override
	{
		const ecs::Entity object = m_game.names.Find(cave);
		const auto *ref = m_game.world.IsAlive(object) ? m_game.world.Get<gameplay::DefinitionRef>(object) : nullptr;
		if (ref == nullptr)
			return;
		const auto &modules = m_game.templates.DefinitionAt(ref->index).modules;
		if (std::none_of(modules.begin(), modules.end(), [](const content::ModuleEntry &module) { return module.type == "CaveContain"; }))
			return;
		// (A CaveContain would tryToSetCaveIndex here; none is ported, as none is shipped.)
	}
	void SetWarehouseValue(const std::string &warehouse, std::int64_t cash) override
	{
		domain::SetWarehouseCash(m_game, m_game.names.Find(warehouse), cash);
	}
	void EnableUnitConstruction(const std::string &player) override
	{
		if (const auto index = m_game.roster.FindPlayer(player))
			m_game.roster.PlayerAt(*index).unitConstructionEnabled = true;
	}
	// PlayerScriptHost.
	std::optional<std::uint32_t> ResolvePlayer(std::size_t participant, const std::string &name) override
	{
		const std::string resolved = PlayerNamed(participant, name);
		return resolved.empty() ? std::nullopt : m_game.roster.FindPlayer(resolved);
	}
	std::int64_t Money(std::uint32_t player) const override { return m_game.world.Resource<gameplay::PlayerMoney>().Balance(player); }
	bool MusicCompleted(const std::string &track, std::int64_t times) const override
	{
		const auto *music = m_game.world.FindResource<domain::MusicProgress>();
		return music != nullptr && music->Completed(track, times);
	}
	std::pair<std::int64_t, std::int64_t> Power(std::uint32_t player) const override
	{
		const auto &energy = m_game.world.Resource<gameplay::PlayerEnergy>();
		return {energy.Production(player), energy.Consumption(player)};
	}
	std::int64_t StartIndex(std::uint32_t player) const override
	{
		for (const auto &participant : m_game.level.scenario.participants)
			if (participant.properties.Get<std::string>("playerName") == m_game.roster.PlayerAt(player).name)
				return participant.properties.Get<std::int64_t>("multiplayerStartIndex").value_or(0);
		return 0;
	}
	bool HasAnyObjects(std::uint32_t player) const override { return domain::HasAnyObjects(m_game, player); }
	bool HasAnyBuildFacility(std::uint32_t player) const override { return domain::HasAnyBuildFacility(m_game, player); }
	std::int64_t CountBuildings(std::uint32_t player, bool faction) const override { return domain::CountBuildings(m_game, player, faction); }
	std::int64_t CountGarrisoned(std::uint32_t player) const override { return domain::CountGarrisonedBuildings(m_game, player); }
	std::int64_t CountCaptured(std::uint32_t player) const override { return domain::CountCapturedObjects(m_game, player); }
	bool CanBuildAny(std::uint32_t player, const std::string &types) override
	{
		const auto &records = m_game.world.Resource<domain::ScriptRecords>();
		for (const std::string &type : domain::ObjectTypesFrom(m_game, records, types))
			if (const content::ObjectDefinition *what = m_game.templates.Content().objects.Find(type); what != nullptr && domain::PlayerCanBuild(m_game, player, *what))
				return true;
		return false;
	}
	bool Discovered(std::uint32_t player, std::uint32_t by) const override { return domain::DiscoveredBy(m_game, player, by); }
	std::string Side(std::uint32_t player) const override
	{
		for (const auto &participant : m_game.level.scenario.participants)
			if (participant.properties.Get<std::string>("playerName") == m_game.roster.PlayerAt(player).name)
				for (const auto &faction : m_game.templates.Content().playerTemplates.templates)
					if (faction.name == participant.properties.Get<std::string>("playerFaction").value_or(""))
						return faction.side;
		return {};
	}
	std::int64_t CountObjects(std::uint32_t player, const std::string &types, bool ignoreDead) const override
	{
		const auto &records = m_game.world.Resource<domain::ScriptRecords>();
		return domain::CountPlayerObjects(m_game, player, domain::ObjectTypesFrom(m_game, records, types), ignoreDead);
	}
	std::int64_t RecordedCount(std::uint32_t player, const std::string &types) const override
	{
		// getObjectCount: an inactive player (nothing left) always has none.
		if (!domain::HasAnyObjects(m_game, player))
			return 0;
		const auto &counts = m_game.world.Resource<domain::ScriptRecords>().objectCounts;
		if (player >= counts.size())
			return 0;
		const auto found = counts[player].find(types);
		return found == counts[player].end() ? 0 : found->second;
	}
	void RecordCount(std::uint32_t player, const std::string &types, std::int64_t count) override
	{
		domain::ScriptRecords::ForPlayer(m_game.world.Resource<domain::ScriptRecords>().objectCounts, player)[types] = count;
	}
	bool TakeCompletedUpgrade(std::uint32_t player, const std::string &upgrade) override
	{
		return m_game.world.Resource<domain::ScriptRecords>().TakeUpgrade(player, upgrade);
	}
	bool TakeCompletedUpgradeFrom(std::uint32_t player, const std::string &upgrade, const std::string &unit) override
	{
		const ecs::Entity source = m_game.names.Find(unit);
		return m_game.world.IsAlive(source) && m_game.world.Resource<domain::ScriptRecords>().TakeUpgrade(player, upgrade, source);
	}
	bool TakeAcquiredScience(std::uint32_t player, const std::string &science) override
	{
		return m_game.world.Resource<domain::ScriptRecords>().TakeScience(player, science);
	}
	LastAttack AttackOn(ecs::Entity entity) const
	{
		LastAttack attack;
		const auto *health = m_game.world.IsAlive(entity) ? m_game.world.Get<gameplay::Health>(entity) : nullptr;
		if (health == nullptr)
			return attack;
		if (health->lastAttackerPlayer != gameplay::Health::None)
			attack.player = health->lastAttackerPlayer;
		if (m_game.world.IsAlive(health->lastAttacker))
			if (const auto *owner = m_game.world.Get<gameplay::Owner>(health->lastAttacker))
				attack.attackerPlayer = owner->player;
		if (health->lastAttackerDefinition != gameplay::Health::None && health->lastAttackerDefinition < m_game.templates.DefinitionCount())
			attack.attackerType = m_game.templates.DefinitionAt(health->lastAttackerDefinition).name;
		return attack;
	}
	std::optional<LastAttack> NamedLastAttack(const std::string &unit) const override
	{
		const ecs::Entity entity = m_game.names.Find(unit);
		if (!m_game.world.IsAlive(entity) || m_game.world.Get<gameplay::Health>(entity) == nullptr)
			return std::nullopt;
		return AttackOn(entity);
	}
	std::vector<LastAttack> TeamLastAttacks(const std::string &team) const override
	{
		std::vector<LastAttack> attacks;
		if (const auto index = domain::ResolveTeam(m_game, team))
			for (const ecs::Entity member : m_game.roster.TeamAt(*index).members)
				if (m_game.world.IsAlive(member) && m_game.world.Get<gameplay::Health>(member) != nullptr)
					attacks.push_back(AttackOn(member));
		return attacks;
	}
	bool TypeMatches(const std::string &types, const std::string &type) const override
	{
		if (m_game.templates.Content().objects.Find(types) != nullptr)
			return domain::EquivalentTypes(m_game, types, type);
		const auto *records = m_game.world.FindResource<domain::ScriptRecords>();
		if (records == nullptr)
			return false;
		for (const std::string &name : domain::ObjectTypesFrom(m_game, *records, types))
			if (domain::EquivalentTypes(m_game, name, type))
				return true;
		return false;
	}
	std::optional<std::pair<std::uint32_t, std::uint32_t>> NamedContain(const std::string &unit) const override
	{
		const auto contain = domain::ContainOf(m_game, m_game.names.Find(unit));
		return contain ? std::optional(std::pair{contain->count, contain->max}) : std::nullopt;
	}
	bool NamedDiscovered(const std::string &unit, std::uint32_t player) const override
	{
		return domain::DiscoveredBy(m_game, m_game.names.Find(unit), player);
	}
	bool TeamDiscovered(const std::string &team, std::uint32_t player) const override
	{
		const auto index = domain::ResolveTeam(m_game, team);
		if (!index)
			return false;
		for (const ecs::Entity member : m_game.roster.TeamAt(*index).members)
			if (domain::DiscoveredBy(m_game, member, player))
				return true;
		return false;
	}
	bool NamedReachedPathEnd(const std::string &unit, const std::string &label) const override
	{
		return domain::ReachedWaypointsEnd(m_game, m_game.names.Find(unit), label, m_game.tick);
	}
	bool TeamReachedPathEnd(const std::string &team, const std::string &label) const override
	{
		const auto index = domain::ResolveTeam(m_game, team);
		if (!index)
			return false;
		for (const ecs::Entity member : m_game.roster.TeamAt(*index).members)
			if (domain::ReachedWaypointsEnd(m_game, member, label, m_game.tick))
				return true;
		return false;
	}
	bool EnemySighted(const std::string &unit, std::int64_t alliance, std::uint32_t player) const override
	{
		return domain::EnemySighted(m_game, m_game.names.Find(unit), alliance, player);
	}
	bool TypeSighted(const std::string &unit, const std::string &types, std::uint32_t player) const override
	{
		return domain::Sighted(m_game, m_game.names.Find(unit), player, [&](const gameplay::SpatialEntry &entry) {
			const auto *ref = m_game.world.Get<gameplay::DefinitionRef>(entry.entity);
			return ref != nullptr && TypeMatches(types, m_game.templates.DefinitionAt(ref->index).name);
		});
	}
	bool BuildingEnteredBy(const std::string &building, std::uint32_t player) const override
	{
		return domain::EnteredBy(m_game, m_game.names.Find(building), player, m_game.tick);
	}
	bool AttackedBy(std::uint32_t player, std::uint32_t attacker) const override
	{
		return player < m_game.roster.PlayerCount() && attacker < 64 && (m_game.roster.PlayerAt(player).attackedBy >> attacker & 1u) != 0;
	}
	void SetPlayerRelationship(std::uint32_t from, std::uint32_t to, std::int64_t relationship) override
	{
		// The original's ENEMIES 0, NEUTRAL 1, ALLIES 2 (anything else: as it is, an unknown relationship: neutral).
		const gameplay::Relationship kind = relationship == 0 ? gameplay::Relationship::Enemies
			: relationship == 2 ? gameplay::Relationship::Allies
			: gameplay::Relationship::Neutral;
		m_game.world.Resource<gameplay::Relationships>().Set(from, to, kind);
	}
	NamedUnit NamedUnitState(const std::string &name) const override
	{
		NamedUnit unit;
		const ecs::Entity entity = m_game.names.Find(name);
		unit.known = m_game.names.Known(name);
		unit.exists = m_game.world.IsAlive(entity);
		if (unit.exists)
		{
			unit.dead = domain::EffectivelyDead(m_game, entity);
			if (const auto *owner = m_game.world.Get<gameplay::Owner>(entity))
				unit.owner = owner->player;
		}
		return unit;
	}
	bool BridgeBroken(const std::string &name, bool broken) const override { return domain::BridgeBroken(m_game, m_game.names.Find(name), broken); }
	bool NamedAlive(const std::string &name) const override
	{
		const ecs::Entity unit = m_game.names.Find(name);
		return m_game.world.IsAlive(unit) && m_game.world.Get<gameplay::Dying>(unit) == nullptr && m_game.world.Get<gameplay::InactiveBody>(unit) == nullptr;
	}
	std::optional<std::int64_t> NamedHealthPercent(const std::string &name) const override
	{
		const ecs::Entity unit = m_game.names.Find(name);
		const auto *health = m_game.world.IsAlive(unit) ? m_game.world.Get<gameplay::Health>(unit) : nullptr;
		const auto *ref = health != nullptr ? m_game.world.Get<gameplay::DefinitionRef>(unit) : nullptr;
		if (ref == nullptr)
			return std::nullopt;
		// (health x 100 + initial / 2) / initial, whole (evaluateUnitHealth).
		const auto combat = content::ReadObjectCombat(m_game.templates.DefinitionAt(ref->index), m_game.step);
		const Engine::Math::Fixed initial = combat.initialHealth > Engine::Math::Fixed{} ? combat.initialHealth : health->maximum;
		if (initial <= Engine::Math::Fixed{})
			return std::nullopt;
		return ((health->current * 100 + initial / Engine::Math::Fixed::FromInt(2)) / initial).Floor();
	}
	bool TeamHasUnits(const std::string &team) const override
	{
		// Team::hasAnyUnits: a member that is not effectively dead and not a structure.
		const auto index = domain::ResolveTeam(m_game, team);
		if (!index)
			return false;
		for (const ecs::Entity member : m_game.roster.TeamAt(*index).members)
		{
			if (!m_game.world.IsAlive(member) || m_game.world.Get<gameplay::Dying>(member) != nullptr || m_game.world.Get<gameplay::InactiveBody>(member) != nullptr)
				continue;
			const auto *ref = m_game.world.Get<gameplay::DefinitionRef>(member);
			if (ref != nullptr && !m_game.templates.DefinitionAt(ref->index).Is("STRUCTURE"))
				return true;
		}
		return false;
	}
	void SetCanBuildBase(std::uint32_t player, bool can) override { m_game.roster.PlayerAt(player).canBuildBase = can; }
	void SetListInScoreScreen(std::uint32_t player, bool list) override { m_game.roster.PlayerAt(player).listInScoreScreen = list; }
	void BuildSpecificBuilding(std::uint32_t player, const std::string &structure) override
	{
		if (auto *ais = m_game.world.FindResource<domain::AiPlayers>())
			if (auto *ai = ais->Of(player))
				domain::BuildSpecificAiBuilding(m_game, *ai, structure);
	}
	void BuildBySupplies(std::uint32_t player, std::int64_t minimumCash, const std::string &structure) override
	{
		if (auto *ais = m_game.world.FindResource<domain::AiPlayers>())
			if (auto *ai = ais->Of(player))
				domain::BuildBySupplies(m_game, *ais, *ai, minimumCash, structure);
	}
	void BuildNearestTeam(std::uint32_t player, const std::string &structure, const std::string &team) override
	{
		auto *ais = m_game.world.FindResource<domain::AiPlayers>();
		auto *ai = ais != nullptr ? ais->Of(player) : nullptr;
		const auto index = domain::ResolveTeam(m_game, team);
		if (ai != nullptr && index)
			domain::BuildNearestTeam(m_game, *ai, structure, *index);
	}
	void BuildBaseDefense(std::uint32_t player, const std::string &structure, bool flank) override
	{
		if (auto *ais = m_game.world.FindResource<domain::AiPlayers>())
			if (auto *ai = ais->Of(player))
			{
				if (structure.empty())
					domain::BuildAiBaseDefense(m_game, *ais, *ai, StartIndex(player), flank);
				else
					domain::BuildAiBaseDefenseStructure(m_game, *ais, *ai, StartIndex(player), structure, flank);
			}
	}
	std::tuple<bool, bool, std::uint64_t> SpecialPowerReadiness(std::uint32_t player, const std::string &power, std::uint64_t latest) override
	{
		const domain::PowerReadiness found = domain::SpecialPowerReadiness(m_game, player, power, latest);
		return {found.known, found.ready, found.next};
	}
	void FireSpecialPowerAtMostCost(std::uint32_t scriptPlayer, std::uint32_t player, const std::string &power) override
	{
		if (auto *ais = m_game.world.FindResource<domain::AiPlayers>())
			domain::SkirmishFireSpecialPowerAtMostCost(m_game, *ais, scriptPlayer, player, power, StartIndex(player));
	}
	bool TakeSpecialPowerEvent(int stage, std::uint32_t player, const std::string &power, const std::optional<std::string> &unit) override
	{
		std::optional<ecs::Entity> source;
		if (unit)
		{
			// getUnitNamed: a source that is gone can no longer be told apart.
			const ecs::Entity named = m_game.names.Find(*unit);
			if (!m_game.world.IsAlive(named))
				return false;
			source = named;
		}
		auto &records = m_game.world.Resource<domain::ScriptRecords>();
		if (stage == 0)
			return records.TakeTriggeredPower(player, power, source);
		if (stage == 2)
			return records.TakeCompletedPower(player, power, source);
		return false; // notifyOfMidwaySpecialPower: nothing in the game sends it
	}
	// TerrainLogic::setActiveBoundary: one the level has, not the active one, with an extent; then the partition (the
	// shroud) remade over it.
	void SetBuildable(const std::string &objectType, std::int64_t status) override
	{
		if (m_game.templates.Content().objects.Find(objectType) == nullptr || status < 0 ||
			status > static_cast<std::int64_t>(content::ObjectDefinition::Buildable::OnlyByAI))
			return;
		m_game.world.Resource<domain::BuildableOverrides>().types[objectType] = static_cast<content::ObjectDefinition::Buildable>(status);
	}
	void OverrideHulkLifetime(Engine::Math::Fixed seconds) override
	{
		auto &hulks = m_game.world.Resource<gameplay::HulkLifetime>();
		// (Int)(seconds * LOGICFRAMES_PER_SECOND): toward zero.
		hulks.overrideTicks = seconds < Engine::Math::Fixed{} ? -1 : (seconds * Engine::Math::Fixed::FromInt(30)).Floor();
	}
	void ChangeWaterHeight(const std::string &water, Engine::Math::Fixed height, Engine::Math::Fixed seconds, Engine::Math::Fixed damage) override
	{
		domain::ChangeWaterHeightOverTime(m_game, water, height, seconds, damage);
	}
	void SetWaterHeight(const std::string &water, Engine::Math::Fixed height) override { domain::ChangeWaterHeight(m_game, water, height); }
	void SwitchBoundary(std::int64_t boundary) override
	{
		auto &ground = m_game.ground;
		if (boundary < 0 || static_cast<std::size_t>(boundary) >= ground.BoundaryCount() || static_cast<std::uint32_t>(boundary) == ground.ActiveBoundary())
			return;
		const auto extent = ground.Boundary(static_cast<std::size_t>(boundary));
		if (extent[0] == 0 || extent[1] == 0)
			return;
		ground.SetActiveBoundary(static_cast<std::uint32_t>(boundary));
		const auto [low, high] = ground.Extent();
		m_game.world.Resource<gameplay::ShroudMap>().SwitchExtent(high.x, high.y);
	}
	void CommandBarButton(const std::string &button, const std::string &objectType, std::optional<std::int64_t> slot) override
	{
		// The object type's command set (none: nothing); a button added must exist and its slot be one of the set's.
		const content::ObjectDefinition *object = m_game.templates.Content().objects.Find(objectType);
		if (object == nullptr)
			return;
		const content::CommandCatalog &catalog = m_game.templates.Content().commands;
		auto &overrides = m_game.world.Resource<domain::CommandBarOverrides>();
		const auto set = domain::EffectiveCommandSet(catalog, &overrides, object->commandSet);
		if (!set)
			return;
		if (!slot)
		{
			// The first slot showing the button now (getCommandButton, overrides too) goes empty.
			for (std::uint32_t at = 0; at < set->buttons.size(); ++at)
				if (!set->buttons[at].empty() && set->buttons[at] == button)
				{
					overrides.Set(object->commandSet, at, {});
					return;
				}
			return;
		}
		if (catalog.Button(button) == nullptr || *slot < 0 || *slot >= static_cast<std::int64_t>(content::CommandSlots))
			return;
		overrides.Set(object->commandSet, static_cast<std::uint32_t>(*slot), button);
	}
	bool SoundComplete(bool speech, const std::string &name) override
	{
		auto &records = m_game.world.Resource<domain::ScriptRecords>();
		return domain::ScriptRecords::SoundComplete(speech ? records.testingSpeech : records.testingAudio, name, m_game.tick,
			[&] { return m_soundLengthTicks ? m_soundLengthTicks(name) : 0; });
	}
	// How many ticks a sound plays (TheAudio->getAudioLengthMS over MSEC_PER_LOGICFRAME_REAL, whole ticks): from the
	// install's files, the same on every machine.
	void SetSoundLengths(std::function<std::uint64_t(std::string_view)> lengthTicks) { m_soundLengthTicks = std::move(lengthTicks); }
	void FreezeTime(bool frozen) override { m_game.world.Resource<domain::ScriptRecords>().timeFrozen = frozen; }
	void SelectSkillset(std::uint32_t player, std::int64_t skillset) override
	{
		if (auto *ais = m_game.world.FindResource<domain::AiPlayers>())
			if (auto *ai = ais->Of(player))
				ai->skillset = static_cast<std::int32_t>(skillset);
	}
	void SetTeamDelaySeconds(std::uint32_t player, std::int64_t seconds) override
	{
		if (auto *ais = m_game.world.FindResource<domain::AiPlayers>())
			if (auto *ai = ais->Of(player))
				ai->teamSeconds = seconds;
	}
	void RepairStructure(std::uint32_t player, const std::string &structure) override
	{
		if (auto *ais = m_game.world.FindResource<domain::AiPlayers>())
			if (auto *ai = ais->Of(player))
				domain::RepairStructure(m_game, *ai, m_game.names.Find(structure));
	}
	void PlayerHunt(std::uint32_t player) override { domain::PlayerHunt(m_game, player); }
	void PlayerExitAllBuildings(std::uint32_t player) override { domain::PlayerExitAllBuildings(m_game, player); }
	void EnableScoring(bool on) override { m_game.world.Resource<domain::ScoreKeepers>().enabled = on; }
	void AddSkillPoints(std::uint32_t player, std::int64_t points) override { domain::AddSkillPoints(m_game, player, static_cast<std::int32_t>(points)); }
	void AddRankLevels(std::uint32_t player, std::int64_t levels) override
	{
		if (const auto *rank = m_game.world.Resource<gameplay::PlayerRanks>().Find(player))
			domain::SetRankLevel(m_game, player, rank->level + static_cast<std::int32_t>(levels));
	}
	void SetRankLevel(std::uint32_t player, std::int64_t level) override { domain::SetRankLevel(m_game, player, static_cast<std::int32_t>(level)); }
	void GrantScience(std::uint32_t player, const std::string &science) override
	{
		if (const auto bit = m_game.templates.Content().Science(science))
			domain::GrantScience(m_game, player, *bit);
	}
	void PurchaseScience(std::uint32_t player, const std::string &science) override
	{
		if (const auto bit = m_game.templates.Content().Science(science))
			domain::PurchaseScience(m_game, player, *bit);
	}
	void SetScienceAvailability(std::uint32_t player, const std::string &science, const std::string &availability) override
	{
		constexpr std::pair<std::string_view, gameplay::ScienceAvailability> names[] = {{"Available", gameplay::ScienceAvailability::Available},
			{"Disabled", gameplay::ScienceAvailability::Disabled}, {"Hidden", gameplay::ScienceAvailability::Hidden}};
		const auto same = [](std::string_view a, std::string_view b) {
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
				return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
			});
		};
		const auto bit = m_game.templates.Content().Science(science);
		for (const auto &[name, kind] : names)
			if (bit && same(name, availability))
				m_game.world.Resource<gameplay::PlayerSciences>().SetAvailability(player, *bit, kind);
	}
	bool CanPurchaseScience(std::uint32_t player, const std::string &science) const override
	{
		const auto bit = m_game.templates.Content().Science(science);
		const auto *rank = m_game.world.Resource<gameplay::PlayerRanks>().Find(player);
		return bit && rank != nullptr &&
			gameplay::CanPurchaseScience(*rank, m_game.world.Resource<gameplay::PlayerSciences>(), m_game.world.Resource<gameplay::RankRules>(), player, *bit);
	}
	std::int64_t SciencePurchasePoints(std::uint32_t player) const override
	{
		const auto *rank = m_game.world.Resource<gameplay::PlayerRanks>().Find(player);
		return rank != nullptr ? rank->purchasePoints : 0;
	}
	void SetSkillPointsModifier(std::uint32_t player, Engine::Math::Fixed modifier) override
	{
		m_game.world.Resource<gameplay::PlayerRanks>().Of(player).skillModifier = modifier;
	}
	void ShroudAtWaypoint(const std::string &waypoint, Engine::Math::Fixed radius, std::optional<std::uint32_t> player, bool cover) override
	{
		const auto at = Waypoint(waypoint);
		if (!at)
			return;
		auto &shroud = m_game.world.Resource<gameplay::ShroudMap>();
		const auto cell = shroud.CellOf(at->x, at->y);
		const std::int32_t cells = shroud.CellsFor(radius);
		const std::uint64_t mask = player ? std::uint64_t{1} << *player : HumanMask();
		// A quick look (a radar jammer still works), or a dollop of shroud (not active shroud).
		if (cover)
		{
			shroud.Cover(cell[0], cell[1], cells, mask);
			shroud.Uncover(cell[0], cell[1], cells, mask);
		}
		else
		{
			shroud.Reveal(cell[0], cell[1], cells, mask);
			shroud.Unreveal(cell[0], cell[1], cells, mask);
		}
	}
	void ShroudEntireMap(std::optional<std::uint32_t> player, MapShroud what) override
	{
		auto &shroud = m_game.world.Resource<gameplay::ShroudMap>();
		for (std::uint32_t index = 0; index < m_game.roster.PlayerCount(); ++index)
		{
			if (player ? index != *player : !m_game.roster.PlayerAt(index).human)
				continue;
			switch (what)
			{
			case MapShroud::RevealAll: shroud.RevealAll(index); break;
			case MapShroud::RevealAllPermanently: shroud.RevealAllPermanently(index); break;
			case MapShroud::UndoRevealAllPermanently: shroud.UndoRevealAllPermanently(index); break;
			case MapShroud::ShroudAll: shroud.ShroudAll(index); break;
			}
		}
	}
	void RevealNamed(const std::string &name, const std::string &waypoint, Engine::Math::Fixed radius, std::optional<std::uint32_t> player) override
	{
		auto &shroud = m_game.world.Resource<gameplay::ShroudMap>();
		gameplay::Sighting sighting;
		if (const auto at = Waypoint(waypoint); at && player)
		{
			const auto cell = shroud.CellOf(at->x, at->y);
			sighting = {cell[0], cell[1], shroud.CellsFor(radius), std::uint64_t{1} << *player, 1, {}};
		}
		shroud.AddNamedReveal(name, sighting);
		if (const auto *reveal = shroud.FindNamedReveal(name); reveal != nullptr && reveal->sighting.valid != 0)
			shroud.Reveal(reveal->sighting.cellX, reveal->sighting.cellY, reveal->sighting.cellRadius, reveal->sighting.mask);
	}
	void UndoRevealNamed(const std::string &name) override
	{
		auto &shroud = m_game.world.Resource<gameplay::ShroudMap>();
		if (const auto *reveal = shroud.FindNamedReveal(name); reveal != nullptr && reveal->sighting.valid != 0)
			shroud.Unreveal(reveal->sighting.cellX, reveal->sighting.cellY, reveal->sighting.cellRadius, reveal->sighting.mask);
		shroud.RemoveNamedReveal(name);
	}
	void ObjectTypeList(const std::string &list, const std::string &type, bool add) override
	{
		auto &records = m_game.world.Resource<domain::ScriptRecords>();
		if (add)
			records.AddToList(list, type);
		else
			records.RemoveFromList(list, type);
	}

	std::string TeamReference(std::uint64_t subject, const std::string &name) override
	{
		// getTeamNamed: <This Team> is the calling team; so is a name that is the calling team's.
		const bool calling = subject != engine::scripting::NoSubject && subject < m_game.roster.TeamCount();
		if (name == "<This Team>")
			return calling ? "#" + std::to_string(subject) : std::string{};
		if (calling && m_game.roster.TeamAt(static_cast<std::uint32_t>(subject)).name == name)
			return "#" + std::to_string(subject);
		return name;
	}
	// getUnitNamed: <This Object> is the unit the sequential script runs on (m_conditionObject).
	std::string UnitReference(std::uint64_t subject, const std::string &name) override
	{
		if (name == "<This Object>")
			return domain::IsUnitSubject(subject) ? engine::gameplay::NameRegistry::Reference(domain::SubjectUnit(subject)) : std::string{};
		return name;
	}
	std::optional<std::uint64_t> UnitSubject(const std::string &unit) override
	{
		const ecs::Entity entity = m_game.names.Find(unit);
		if (!m_game.world.IsAlive(entity))
			return std::nullopt;
		return domain::UnitSubject(entity);
	}
	std::string PlayerNamed(std::size_t participant, const std::string &name) override
	{
		const auto &participants = m_game.level.scenario.participants;
		const std::string own = participant < participants.size() ? participants[participant].properties.Get<std::string>("playerName").value_or("") : "";
		if (name == "<This Player>")
			return own;
		// getPlayerFromAsciiString: in a single-player game "<Local Player>" is the local human, and in a Generals'
		// Challenge so is "ThePlayer" (its maps' placeholder for it).
		if (const auto *solo = m_game.world.FindResource<domain::SoloPlay>();
			solo != nullptr && solo->singlePlayer && (name == "<Local Player>" || (solo->challenge && name == "ThePlayer")) &&
			solo->localPlayer < m_game.roster.PlayerCount())
			return m_game.roster.PlayerAt(solo->localPlayer).name;
		if (name == "<This Player's Enemy>")
		{
			const auto player = m_game.roster.FindPlayer(own);
			auto *ais = m_game.world.FindResource<domain::AiPlayers>();
			const auto enemy = player && ais != nullptr ? domain::SkirmishEnemy(m_game, *ais, *player) : std::nullopt;
			return enemy ? m_game.roster.PlayerAt(*enemy).name : std::string{};
		}
		return name;
	}

private:
	// getSkirmishEnemyPlayer for a script of `participant` (TheScriptEngine's current player).
	std::optional<std::uint32_t> SkirmishEnemyOf(std::size_t participant)
	{
		const std::string name = PlayerNamed(participant, "<This Player's Enemy>");
		return name.empty() ? std::nullopt : m_game.roster.FindPlayer(name);
	}
	// getQualifiedTriggerAreaByName for a script of `participant`: its perimeters, and its enemy's.
	std::uint32_t QualifiedArea(std::size_t participant, const std::string &area)
	{
		const auto *areas = m_game.world.FindResource<gameplay::TriggerAreas>();
		if (areas == nullptr)
			return gameplay::TriggerAreas::None;
		const auto startOf = [&](const std::string &name) -> std::optional<std::int64_t> {
			const auto player = name.empty() ? std::nullopt : m_game.roster.FindPlayer(name);
			return player ? std::optional(StartIndex(*player)) : std::nullopt;
		};
		return domain::QualifiedArea(*areas, area, startOf(PlayerNamed(participant, "<This Player>")), startOf(PlayerNamed(participant, "<This Player's Enemy>")));
	}
	// getHumanPlayerMask (retail's leaves the mask uninitialised and ANDs into it; its fix ORs, the defined behaviour).
	std::uint64_t HumanMask() const
	{
		std::uint64_t mask = 0;
		for (std::uint32_t index = 0; index < m_game.roster.PlayerCount(); ++index)
			if (m_game.roster.PlayerAt(index).human)
				mask |= std::uint64_t{1} << index;
		return mask;
	}
	std::optional<Engine::Math::FixedVector2> Waypoint(const std::string &name) const
	{
		const std::uint32_t at = m_game.waypoints.Find(name);
		return at != gameplay::WaypointGraph::None ? std::optional(m_game.waypoints.Position(at).XY()) : std::nullopt;
	}

	domain::GameWorld &m_game;
	std::function<std::uint64_t(std::string_view)> m_soundLengthTicks;
	std::function<bool(ecs::Entity)> m_selected;
};
}
