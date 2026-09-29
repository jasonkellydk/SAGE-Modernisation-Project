export module games.generalszh.gameplay.scripts.algorithms.object_status_queries;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.content.objects.object_status;
import engine.gameplay.common.status.components.status_flags;
import engine.gameplay.common.status.components.ai_activity;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.stealth.components.stealth;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.common.spatial.components.object_shroud;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.movement.components.path_completed;
import engine.gameplay.common.spatial.resources.spatial_index;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.health.components.health;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import games.generalszh.gameplay.scripts.components.emptied_watch;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import engine.gameplay.common.spatial.components.off_map;

// The scripts' questions about object status (ScriptConditions::evaluateUnitHasObjectStatus / evaluateTeamHasObjectStatus:
// Object::getStatusBits().testForAny). The port keeps a status where its state lives: UNDER_CONSTRUCTION (being built),
// SOLD (being sold), CAN_STEALTH / STEALTHED / DETECTED (its stealth), USING_ABILITY (its AI's activity) and
// IS_FIRING_WEAPON; the rest as the game set them (StatusFlags). IS_FIRING_WEAPON is AIAttackFireWeaponState's, from
// its onEnter to its exit: the port has no attack state machine yet and folds that onEnter into the shot's tick, so
// it holds the tick after one fired or wound up (preFireWeapon), a tick after the original's.
export namespace generalszh::gameplay
{
// Its status as the scripts of tick `now` see it (they run before that tick's systems).
inline std::uint64_t ObjectStatusBits(const GameWorld &game, ecs::Entity entity, std::uint64_t now)
{
	namespace gp = engine::gameplay;
	const auto &world = game.world;
	if (!world.IsAlive(entity))
		return 0;
	const auto bit = [](std::string_view name) { return std::uint64_t{1} << content::ObjectStatusBit(name); };
	std::uint64_t bits = 0;
	if (const auto *flags = world.Get<gp::StatusFlags>(entity))
		bits = flags->bits;
	bits &= ~(bit("UNDER_CONSTRUCTION") | bit("SOLD") | bit("CAN_STEALTH") | bit("STEALTHED") | bit("DETECTED") | bit("USING_ABILITY") |
		bit("IS_FIRING_WEAPON"));
	if (world.Has<gp::UnderConstruction>(entity))
		bits |= bit("UNDER_CONSTRUCTION");
	if (world.Has<gp::Sale>(entity))
		bits |= bit("SOLD");
	if (const auto *stealth = world.Get<gp::Stealth>(entity))
	{
		if (stealth->Has(gp::stealth_flag::CanStealth))
			bits |= bit("CAN_STEALTH");
		if (stealth->Has(gp::stealth_flag::Stealthed))
			bits |= bit("STEALTHED");
		if (stealth->Has(gp::stealth_flag::Detected))
			bits |= bit("DETECTED");
	}
	if (const auto *activity = world.Get<gp::AiActivity>(entity); activity != nullptr && activity->usingAbility != 0)
		bits |= bit("USING_ABILITY");
	if (const auto *armament = world.Get<gp::Armament>(entity))
	{
		const bool fired = armament->firedTick != 0 && armament->firedTick + 1 == now;
		const bool windingUp = armament->preAttackUntil != 0 && armament->preAttackSeen + 1 == now;
		if (fired || windingUp)
			bits |= bit("IS_FIRING_WEAPON");
	}
	return bits;
}

// ContainModuleInterface::getContainCount / getContainMax: how many are aboard (a tunnel: its whole network) and how
// many it has room for; none: not a container.
struct ContainState
{
	std::uint32_t count{0};
	std::uint32_t max{0};
};
inline std::optional<ContainState> ContainOf(const GameWorld &game, ecs::Entity entity);

// evaluateUnitHasEmptied (UNIT_EMPTIED): true when it was last asked about on the tick before, had someone inside then
// and has nobody now (that look is kept, so every ask this tick is true); else it remembers what it sees now. The
// first ask only looks.
inline bool UnitEmptied(GameWorld &game, ecs::Entity unit)
{
	if (!game.world.IsAlive(unit))
		return false;
	const auto contain = ContainOf(game, unit);
	const std::uint64_t count = contain ? contain->count : 0u;
	if (!game.world.Has<EmptiedWatch>(unit))
	{
		game.world.Add<EmptiedWatch>(unit);
		*game.world.Get<EmptiedWatch>(unit) = {game.tick, count};
		return false;
	}
	EmptiedWatch &watch = *game.world.Get<EmptiedWatch>(unit);
	if (watch.tick + 1 == game.tick && watch.count > 0 && count == 0)
		return true;
	watch = {game.tick, count};
	return false;
}

inline std::optional<ContainState> ContainOf(const GameWorld &game, ecs::Entity entity)
{
	namespace gp = engine::gameplay;
	const auto *transport = game.world.IsAlive(entity) ? game.world.Get<gp::Transport>(entity) : nullptr;
	if (transport == nullptr)
		return std::nullopt;
	const auto network = game.manifest.NetworkOf(entity);
	const auto count = network ? game.manifest.NetworkCount(*network) : game.manifest.Count(entity);
	return ContainState{static_cast<std::uint32_t>(count), transport->definition.slots};
}

// evaluateBuildingEntered as the scripts of tick `now` see it: the last one into the container last tick was the
// player's (getPlayerWhoEntered, cleared at the container's next update).
inline bool EnteredBy(const GameWorld &game, ecs::Entity container, std::uint32_t player, std::uint64_t now)
{
	const auto *transport = game.world.IsAlive(container) ? game.world.Get<engine::gameplay::Transport>(container) : nullptr;
	return transport != nullptr && transport->enteredTick != 0 && transport->enteredTick + 1 == now && transport->enteredBy == player;
}

// evaluateNamedDiscovered for one object: not held, not stealthed out of sight (disguises are not ported), and clear or
// partly clear to the player through the shroud (getShroudedStatus).
inline bool DiscoveredBy(const GameWorld &game, ecs::Entity entity, std::uint32_t player)
{
	namespace gp = engine::gameplay;
	if (!game.world.IsAlive(entity))
		return false;
	if (const auto *off = game.world.Get<gp::Disabled>(entity); off != nullptr && (off->mask & gp::disabled_type::Held) != 0)
		return false;
	if (const auto *stealth = game.world.Get<gp::Stealth>(entity); stealth != nullptr && stealth->Hidden())
		return false;
	const auto *shroud = game.world.Get<gp::ObjectShroud>(entity);
	return shroud == nullptr || shroud->SeenBy(player);
}

// evaluateNamedReachedWaypointsEnd for one object, as the scripts of tick `now` see it: the path it completed last tick
// (getCompletedWaypoint) ends at a waypoint labelled `label`.
inline bool ReachedWaypointsEnd(const GameWorld &game, ecs::Entity entity, std::string_view label, std::uint64_t now)
{
	const auto *completed = game.world.IsAlive(entity) ? game.world.Get<engine::gameplay::PathCompleted>(entity) : nullptr;
	return completed != nullptr && completed->SeenAt(now) && game.waypoints.HasLabel(completed->waypoint, label);
}

// evaluateEnemySighted / evaluateTypeSighted: of the things within the unit's vision range of its centre (2D; the
// spatial index as the tick began: the original's partition), alive, not stealthed out of sight, on the map as it is,
// the first of `player`'s that `accept` takes. Its own team's view of them for the relationship (ALLIES / NEUTRAL /
// ENEMIES as the original's Relationship: 0 enemies, 1 neutral, 2 allies).
template<typename Accept>
bool Sighted(const GameWorld &game, ecs::Entity unit, std::uint32_t player, Accept &&accept)
{
	namespace gp = engine::gameplay;
	const auto &world = game.world;
	const auto *where = world.IsAlive(unit) ? world.Get<gp::Transform>(unit) : nullptr;
	const auto *ref = where != nullptr ? world.Get<gp::DefinitionRef>(unit) : nullptr;
	const auto *spatial = world.FindResource<gp::SpatialIndex>();
	if (where == nullptr || ref == nullptr || spatial == nullptr || world.Has<gp::OffMap>(unit))
		return false;
	const Engine::Math::Fixed range = game.templates.DefinitionAt(ref->index).visionRange;
	const Engine::Math::FixedVector2 centre = where->position.XY();
	bool found = false;
	spatial->ForEachWithin(centre, range, [&](const gp::SpatialEntry &entry) {
		if (found || entry.player != player || (entry.classes & gp::target_class::Hidden) != 0 || !world.IsAlive(entry.entity))
			return;
		if (Engine::Math::DistanceSquared(entry.position.XY(), centre) > range * range || EffectivelyDead(game, entry.entity))
			return;
		found = accept(entry);
	});
	return found;
}

inline bool EnemySighted(const GameWorld &game, ecs::Entity unit, std::int64_t alliance, std::uint32_t player)
{
	namespace gp = engine::gameplay;
	const gp::Relationship wanted = alliance == 0 ? gp::Relationship::Enemies : alliance == 2 ? gp::Relationship::Allies : gp::Relationship::Neutral;
	return Sighted(game, unit, player, [&](const gp::SpatialEntry &entry) { return RelationOf(game, unit, entry.entity) == wanted; });
}

// A status parameter's mask (Parameter::getStatus: the name matched without case; none unknown).
inline std::uint64_t ObjectStatusParameter(std::string_view name)
{
	const std::uint32_t index = content::ObjectStatusBit(name);
	return index == content::NoStatus ? 0 : std::uint64_t{1} << index;
}

// UNIT_HAS_OBJECT_STATUS(unit, status), asked by the scripts of tick `now` (the running tick's: game.tick).
inline bool UnitHasObjectStatus(const GameWorld &game, ecs::Entity unit, std::string_view status, std::uint64_t now)
{
	return (ObjectStatusBits(game, unit, now) & ObjectStatusParameter(status)) != 0;
}

// TEAM_ALL_HAS_OBJECT_STATUS / TEAM_SOME_HAVE_OBJECT_STATUS(team, status): every member has it (an empty team does), or
// some member does, the members in team order.
inline bool TeamHasObjectStatus(GameWorld &game, const std::string &team, std::string_view status, bool entireTeam, std::uint64_t now)
{
	const auto index = ResolveTeam(game, team);
	if (!index)
		return false;
	const std::uint64_t mask = ObjectStatusParameter(status);
	for (const ecs::Entity member : game.roster.TeamAt(*index).members)
	{
		const bool has = (ObjectStatusBits(game, member, now) & mask) != 0;
		if (entireTeam && !has)
			return false;
		if (!entireTeam && has)
			return true;
	}
	return entireTeam;
}
}
