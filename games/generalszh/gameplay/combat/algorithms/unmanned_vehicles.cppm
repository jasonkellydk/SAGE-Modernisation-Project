export module games.generalszh.gameplay.combat.algorithms.unmanned_vehicles;
import engine.gameplay.common.health.components.pending_damage;
import std;
import engine.gameplay.common.identity.components.captured;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.crates.algorithms.car_bombs;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.teams.algorithms.defection;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.gameplay.veterancy.algorithms.veterancy_placement;
import games.generalszh.gameplay.powers.algorithms.special_power_launch;
import games.generalszh.content.combat.combat_catalog;
import games.generalszh.gameplay.combat.systems.pilot_kill_system;
import engine.gameplay.common.health.systems.health_system;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.rts.containment.components.transport;
import games.generalszh.gameplay.containment.components.rider_change;
import games.generalszh.gameplay.containment.algorithms.garrisons;
import engine.gameplay.rts.lifecycle.resources.kill_requests;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.veterancy.components.experience;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.common.identity.components.team_member;
import games.generalszh.gameplay.scripts.algorithms.command_button_targets;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.spatial.components.transform;
import engine.ecs.query.query;
import games.generalszh.gameplay.world.resources.deselections;

// Vehicles whose pilot is killed, and infantry taking them over:
// - ActiveBody::attemptDamage, DAMAGE_KILLPILOT (a handled damage type: it takes no health, whatever its amount): a
//   VEHICLE hit by it loses its pilot (a rider-change container's rider is not ported: a combat bike is left as it is).
//   PilotKillSystem disables it (DISABLED_UNMANNED) and blows up a car bomb within the step (its sniper's kill credit,
//   scoreTheKill, waits on credited kills). The rest of Object::setDisabled, here: one not a DRONE drops to no
//   experience (setExperienceAndLevel(0), through its experience sink: an AutoHealBehavior upgrade to undo is not
//   ported). Then it idles (aiIdle, CMD_FROM_AI) and goes to the neutral player's team (setTeam), grey and anyone's.
// - ActionManager::canEnterObject's special case: any INFANTRY not REJECT_UNMANNED may enter an unmanned vehicle; on
//   touching it (PhysicsBehavior::onCollide with its ignored obstacle, here the boarding's arrival) the vehicle is
//   manned again, captured, and defects to the infantry's team at once (defect(team, 0)); the infantry's script name
//   passes to it (transferObjectName) and the infantry is gone (destroyObject). AIEnterState fails, and the infantry
//   idles, once the vehicle may no longer be entered.
export namespace generalszh::gameplay
{
namespace unmanned_detail
{
namespace gp = engine::gameplay;

inline const content::ObjectDefinition *DefinitionOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *ref = game.world.IsAlive(entity) ? game.world.Get<gp::DefinitionRef>(entity) : nullptr;
	return ref != nullptr ? &game.templates.DefinitionAt(ref->index) : nullptr;
}

inline bool Unmanned(const GameWorld &game, ecs::Entity entity)
{
	const auto *off = game.world.IsAlive(entity) ? game.world.Get<gp::Disabled>(entity) : nullptr;
	return off != nullptr && (off->mask & gp::disabled_type::Unmanned) != 0;
}

inline void SetDisabledFlag(GameWorld &game, ecs::Entity entity, std::uint32_t type, bool on)
{
	auto &world = game.world;
	if (!world.Has<gp::Disabled>(entity))
	{
		if (!on)
			return;
		world.Add<gp::Disabled>(entity);
	}
	auto &mask = world.Get<gp::Disabled>(entity)->mask;
	mask = on ? (mask | type) : (mask & ~type);
}

// ExperienceTracker::setExperienceAndLevel(0, FALSE): into its sink while there is one; a trainable one's points go to
// none and its level to what none earns (the level change as a placement's).
inline void ClearExperience(GameWorld &game, ecs::Entity entity)
{
	auto *experience = game.world.Get<gp::Experience>(entity);
	if (experience == nullptr)
		return;
	if (experience->sink != ecs::Entity{} && game.world.IsAlive(experience->sink) && game.world.Get<gp::Experience>(experience->sink) != nullptr)
	{
		ClearExperience(game, experience->sink);
		return;
	}
	if (!experience->trainable)
		return;
	PlaceAtVeterancy(game, entity, 0);
	game.world.Get<gp::Experience>(entity)->points = 0;
}
}

// What losing its pilot does to a vehicle after the step (see above; PilotKillSystem disabled it, and a car bomb is dying).
inline void KillPilot(GameWorld &game, ecs::Entity vehicle)
{
	using namespace unmanned_detail;
	const content::ObjectDefinition *definition = DefinitionOf(game, vehicle);
	if (definition == nullptr || EffectivelyDead(game, vehicle) || !pilot_kill_detail::LosesPilot(*definition) || !Unmanned(game, vehicle))
		return;
	if (!definition->Is("DRONE"))
		ClearExperience(game, vehicle);
	// ActiveBody::attemptDamage: deselectObject(PLAYERMASK_ALL).
	if (auto *deselections = game.world.FindResource<Deselections>())
		deselections->list.push_back(vehicle);
	AiIdle(game, vehicle);
	if (const auto neutral = game.roster.FindTeam("team"))
		ChangeTeam(game, vehicle, *neutral);
}

// ActiveBody::attemptDamage's DAMAGE_KILLPILOT on a bike (a VEHICLE with a RiderChangeContain): moving, the bike is
// killed; standing, its rider is put out at once (aiEvacuateInstantly: the bike scuttles, RiderChanges) and killed.
inline void KillBikeRider(GameWorld &game, ecs::Entity bike, ecs::Entity damager)
{
	using namespace unmanned_detail;
	auto &world = game.world;
	const content::ObjectDefinition *definition = DefinitionOf(game, bike);
	if (definition == nullptr || !definition->Is("VEHICLE") || !world.Has<RiderChange>(bike) || EffectivelyDead(game, bike))
		return;
	// damager->scoreTheKill then Object::kill (DAMAGE_UNRESISTABLE, DEATH_NORMAL, its most health): unresistable damage of
	// all its health from the damager, so the kill is scored to it.
	const std::uint32_t unresistable = content::DamageTypeIndex("UNRESISTABLE").value_or(0);
	const std::uint32_t normal = content::DeathTypeIndex("NORMAL").value_or(0);
	const ecs::Entity credit = world.IsAlive(damager) ? damager : ecs::Entity{};
	const auto kill = [&](ecs::Entity victim) {
		const auto *health = world.Get<gp::Health>(victim);
		if (health == nullptr)
			return;
		if (!world.Has<gp::PendingDamage>(victim))
			world.Add<gp::PendingDamage>(victim);
		*world.Get<gp::PendingDamage>(victim) = gp::PendingDamage{credit, world.Get<gp::Health>(victim)->maximum, unresistable, normal};
	};
	if (const auto *order = world.Get<gp::MoveOrder>(bike); order != nullptr && order->mode != gp::MoveMode::Idle)
	{
		kill(bike);
		return;
	}
	const auto aboard = game.manifest.Aboard(bike);
	if (aboard.empty())
		return;
	const ecs::Entity rider = aboard.front();
	TakeOutNow(game, bike, rider);
	kill(rider);
}

// The tick's pilot kills (KILL_PILOT hits, in the order dealt).
inline void ApplyPilotKills(GameWorld &game)
{
	const auto *hits = game.world.FindResource<engine::gameplay::Hits>();
	const auto killPilot = content::DamageTypeIndex("KILL_PILOT");
	if (hits == nullptr || !killPilot)
		return;
	std::vector<std::pair<ecs::Entity, ecs::Entity>> struck; // (vehicle, damager)
	hits->ForEach([&](const engine::gameplay::Hit &hit) {
		if (hit.handled && hit.damageType == *killPilot &&
			std::ranges::none_of(struck, [&](const auto &entry) { return entry.first == hit.target; }))
			struck.emplace_back(hit.target, hit.source);
	});
	for (const auto &[vehicle, damager] : struck)
	{
		KillBikeRider(game, vehicle, damager);
		KillPilot(game, vehicle);
	}
}

// ActionManager::canEnterObject for an unmanned vehicle: not itself, not dead, not fogged to a human player's order,
// neither under construction, not sold, neither IGNORED_IN_GUI nor the infantry a MOB_NEXUS, not subdued, the infantry
// neither STRUCTURE nor IMMOBILE; then any INFANTRY not REJECT_UNMANNED.
inline bool MayTakeOver(const GameWorld &game, ecs::Entity infantry, ecs::Entity vehicle, bool fromScript)
{
	using namespace unmanned_detail;
	const content::ObjectDefinition *self = DefinitionOf(game, infantry);
	const content::ObjectDefinition *target = DefinitionOf(game, vehicle);
	if (self == nullptr || target == nullptr || infantry == vehicle || !Unmanned(game, vehicle) || EffectivelyDead(game, vehicle))
		return false;
	if (ShroudedForAction(game, infantry, vehicle, fromScript))
		return false;
	const auto &world = game.world;
	if (world.Has<gp::UnderConstruction>(infantry) || world.Has<gp::UnderConstruction>(vehicle) || world.Has<gp::Sale>(vehicle))
		return false;
	if (self->Is("IGNORED_IN_GUI") || self->Is("MOB_NEXUS") || target->Is("IGNORED_IN_GUI"))
		return false;
	if (const auto *off = world.Get<gp::Disabled>(vehicle); off != nullptr && (off->mask & gp::disabled_type::Subdued) != 0)
		return false;
	if (self->Is("STRUCTURE") || self->Is("IMMOBILE"))
		return false;
	return self->Is("INFANTRY") && !self->Is("REJECT_UNMANNED");
}

// AIUpdateInterface::aiEnter at an unmanned vehicle: a mobile unit that may take it over sets out for it (boarding it).
inline void OrderTakeOver(GameWorld &game, ecs::Entity infantry, ecs::Entity vehicle, bool fromScript)
{
	auto &world = game.world;
	if (!world.IsAlive(infantry) || world.Has<engine::gameplay::Passenger>(infantry) || !world.Has<engine::gameplay::MoveOrder>(infantry) ||
		!MayTakeOver(game, infantry, vehicle, fromScript))
		return;
	Commanded(game, infantry);
	if (!world.Has<engine::gameplay::Boarding>(infantry))
		world.Add<engine::gameplay::Boarding>(infantry);
	*world.Get<engine::gameplay::Boarding>(infantry) = {vehicle};
}

// The tick's boardings of unmanned vehicles (those without a Transport): one that may no longer be taken over ends
// (AIEnterState's STATE_FAILURE: the unit idles); one at the vehicle takes it over.
inline void ApplyTakeOvers(GameWorld &game)
{
	namespace gp = engine::gameplay;
	const auto *requests = game.world.FindResource<gp::BoardRequests>();
	if (requests == nullptr)
		return;
	std::vector<gp::BoardRequest> taking;
	requests->ForEach([&](const gp::BoardRequest &request) {
		if (request.transport != ecs::Entity{} && !request.touchOnly && game.world.IsAlive(request.transport) && !game.world.Has<gp::Transport>(request.transport) &&
			(unmanned_detail::Unmanned(game, request.transport) || !CarBomber(game, request.passenger)))
			taking.push_back(request);
	});
	for (const gp::BoardRequest &request : taking)
	{
		const ecs::Entity infantry = request.passenger, vehicle = request.transport;
		if (!game.world.IsAlive(infantry) || EffectivelyDead(game, infantry))
			continue;
		if (!MayTakeOver(game, infantry, vehicle, true))
		{
			game.world.Remove<gp::Boarding>(infantry);
			AiIdle(game, infantry);
			continue;
		}
		if (!request.arrived)
			continue;
		game.world.Remove<gp::Boarding>(infantry);
		unmanned_detail::SetDisabledFlag(game, vehicle, gp::disabled_type::Unmanned, false);
		// setCaptured(true), for the conditions that count captured units.
		if (!game.world.Has<gp::Captured>(vehicle))
			game.world.Add<gp::Captured>(vehicle);
		if (auto *member = game.world.Get<gp::TeamMember>(infantry))
			Defect(game, vehicle, member->team, 0);
		if (const auto name = game.names.NameOf(infantry))
		{
			game.names.Forget(vehicle);
			game.names.Assign(*name, vehicle);
		}
		RetireNow(game, {infantry});
	}
}

// doTeamCaptureNearestUnownedFactionUnit (TEAM_CAPTURE_NEAREST_UNOWNED_FACTION_UNIT): of the things on the map left
// unmanned, those the team's player counts enemies or neutral, or its own (PartitionFilterPlayerAffiliation lets its
// own through), the nearest the team's centre (AIGroup::getCenter; getClosestObject FROM_CENTER_2D, ties in entity
// order); every member of the team as an AIGroup is told to enter it (groupEnter, CMD_FROM_SCRIPT: those that may).
inline void TeamCaptureNearestUnowned(GameWorld &game, const std::string &team)
{
	namespace gp = engine::gameplay;
	const auto index = ResolveTeam(game, team);
	const auto *relationships = game.world.FindResource<gp::Relationships>();
	if (!index || relationships == nullptr)
		return;
	const std::vector<ecs::Entity> group = button_target_detail::GroupOf(game, *index);
	const auto centre = button_target_detail::CenterOf(game, group);
	if (!centre)
		return;
	const std::uint32_t player = game.roster.TeamAt(*index).owner;
	ecs::Entity nearest;
	Engine::Math::Fixed nearestDistance;
	ecs::Query<ecs::Read<gp::Disabled>, ecs::Read<gp::Owner>, ecs::Read<gp::Transform>, ecs::Exclude<gp::OffMap>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		const auto disabled = chunk.template Get<gp::Disabled>();
		const auto owners = chunk.template Get<gp::Owner>();
		const auto transforms = chunk.template Get<gp::Transform>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < entities.size(); ++row)
		{
			if ((disabled[row].mask & gp::disabled_type::Unmanned) == 0)
				continue;
			const std::uint32_t theirs = owners[row].player;
			if (theirs != player && relationships->Allies(player, theirs))
				continue;
			const Engine::Math::Fixed distance = Engine::Math::DistanceSquared(transforms[row].position.XY(), *centre);
			if (nearest == ecs::Entity{} || distance < nearestDistance || (distance == nearestDistance && entities[row].index < nearest.index))
			{
				nearest = entities[row];
				nearestDistance = distance;
			}
		}
	});
	if (nearest == ecs::Entity{})
		return;
	for (const ecs::Entity member : group)
		if (HasAi(game, member))
			OrderTakeOver(game, member, nearest, true);
}

// ScriptActions::doNamedSetUnmanned / doTeamSetUnmanned (NAMED_ / TEAM_SET_UNMANNED_STATUS): DISABLED_UNMANNED on, out
// of every selection, and onto the neutral player's default team.
inline void SetUnmanned(GameWorld &game, ecs::Entity unit)
{
	if (!game.world.IsAlive(unit))
		return;
	unmanned_detail::SetDisabledFlag(game, unit, engine::gameplay::disabled_type::Unmanned, true);
	if (auto *deselections = game.world.FindResource<Deselections>())
		deselections->list.push_back(unit);
	if (const auto neutral = game.roster.FindTeam("team"))
		ChangeTeam(game, unit, *neutral);
}

// ScriptActions::deleteAllUnmanned (DELETE_ALL_UNMANNED): every object DISABLED_UNMANNED is destroyed (removed, not killed).
inline void DeleteAllUnmanned(GameWorld &game)
{
	std::vector<ecs::Entity> unmanned;
	ecs::Query<ecs::Read<engine::gameplay::Disabled>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		const auto masks = chunk.template Get<engine::gameplay::Disabled>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < masks.size(); ++row)
			if ((masks[row].mask & engine::gameplay::disabled_type::Unmanned) != 0)
				unmanned.push_back(entities[row]);
	});
	if (!unmanned.empty())
		RetireNow(game, std::move(unmanned));
}
}
