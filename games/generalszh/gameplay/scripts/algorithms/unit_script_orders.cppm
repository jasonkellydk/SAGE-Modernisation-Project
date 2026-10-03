export module games.generalszh.gameplay.scripts.algorithms.unit_script_orders;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.orders.algorithms.wander_orders;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.rts.containment.components.transport;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.ai.algorithms.guards;
import games.generalszh.gameplay.ai.components.repulsion;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.status.components.script_status;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.rts.combat.components.aggression;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.movement.components.face_target;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.common.identity.components.owner;
import engine.ecs.query.query;
import games.generalszh.gameplay.scripts.algorithms.object_edits;

// Script orders to single units and a player's units (ScriptActions):
// - FaceToward / FaceObject (aiFacePosition / aiFaceObject, CMD_FROM_SCRIPT): the command source is the script's; a
//   mobile unit's attack ends and it turns in place to the point, or wherever the object goes, then idles.
// - NamedFace / TeamFace (doNamedFaceWaypoint, doNamedFaceNamed, doTeamFaceNamed, doTeamFaceWaypoint): each unit with an
//   AI, back on its normal locomotors, faces it; no such waypoint or object: nothing.
// - NamedEnterNamed (doNamedEnterNamed): a unit with an AI, back on its normal locomotors, goes into the other (aiEnter).
// - SetRepulsor (Object::setStatus(OBJECT_STATUS_REPULSOR)): the status on or off; turned on (it was off) on an object
//   that can be repulsed, its repulsor helper turns it off again two seconds on (ObjectRepulsorHelper).
// - SetStealthEnabled (doNamedEnableStealth): OBJECT_STATUS_SCRIPT_UNSTEALTHED off or on.
// - ExitContainer (AIUpdateInterface::aiExit(nullptr)): a unit in a container that is not subdued asks to get out
//   (ExitIntent); `instant`: out at once (removeAllContained).
// - NamedExitBuilding / TeamExitAllBuildings (doUnitExitBuilding / doTeamExitAllBuildings): each unit with an AI, back
//   on its normal locomotors, gets out of whatever holds it.
// - Evacuate (aiEvacuate(FALSE): orderAllPassengersToExit): a container not subdued has each rider with an AI get out.
// - ExitSpecificBuilding (doExitSpecificBuilding): a structure with an AI (on its normal locomotors) evacuates; one
//   without has everyone inside out at once. PlayerExitAllBuildings (Player::ungarrisonAllUnits): each of the player's
//   structures with an AI evacuates.
// - PlayerHunt (Player::setUnitsShouldHunt(true)): the player's units all hunt from now on, and each of its members but
//   dozers, harvesters and those select-all ignores that has an AI hunts (aiHunt).
export namespace generalszh::gameplay
{
inline void FaceToward(GameWorld &game, ecs::Entity unit, Engine::Math::FixedVector2 point, ecs::Entity object = {})
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (!world.IsAlive(unit))
		return;
	Commanded(game, unit);
	if (!detail::Mobile(game, unit) || !world.Has<gp::MoveOrder>(unit))
		return;
	if (object != ecs::Entity{})
	{
		if (!world.Has<gp::FaceTarget>(unit))
			world.Add<gp::FaceTarget>(unit);
		world.Get<gp::FaceTarget>(unit)->target = object;
	}
	*world.Get<gp::MoveOrder>(unit) = gp::MoveOrder{point, 0xFFFFFFFFu, object != ecs::Entity{} ? gp::MoveMode::FaceObject : gp::MoveMode::Face};
	if (auto *attack = world.Get<gp::AttackTarget>(unit))
		*attack = {};
	detail::EndStance(game, unit);
}

namespace script_order_detail
{
inline void Face(GameWorld &game, ecs::Entity unit, Engine::Math::FixedVector2 point, ecs::Entity object)
{
	if (!game.world.IsAlive(unit) || !HasAi(game, unit))
		return;
	NormalLocomotors(game, unit);
	FaceToward(game, unit, point, object);
}
}

// A waypoint (`object` none) or an object to face.
inline void NamedFace(GameWorld &game, ecs::Entity unit, const std::string &waypoint, ecs::Entity object)
{
	namespace gp = engine::gameplay;
	if (object != ecs::Entity{})
	{
		if (game.world.IsAlive(object))
			script_order_detail::Face(game, unit, game.world.Get<gp::Transform>(object)->position.XY(), object);
		return;
	}
	if (const std::uint32_t way = game.waypoints.Find(waypoint); way != gp::WaypointGraph::None)
		script_order_detail::Face(game, unit, game.waypoints.Position(way).XY(), {});
}

inline void TeamFace(GameWorld &game, const std::string &team, const std::string &waypoint, ecs::Entity object)
{
	namespace gp = engine::gameplay;
	if (object != ecs::Entity{} ? !game.world.IsAlive(object) : game.waypoints.Find(waypoint) == gp::WaypointGraph::None)
		return;
	ForTeam(game, team, [&](ecs::Entity unit) { NamedFace(game, unit, waypoint, object); });
}

inline void NamedEnterNamed(GameWorld &game, ecs::Entity unit, ecs::Entity transport)
{
	if (!game.world.IsAlive(unit) || !game.world.IsAlive(transport) || !HasAi(game, unit))
		return;
	NormalLocomotors(game, unit);
	OrderBoard(game, unit, transport);
}

inline void SetRepulsor(GameWorld &game, ecs::Entity unit, bool on)
{
	auto &world = game.world;
	if (!world.IsAlive(unit))
		return;
	RepulsorMark *mark = world.Get<RepulsorMark>(unit);
	if (!on)
	{
		if (mark != nullptr)
			mark->until = 0;
		return;
	}
	if (mark != nullptr && game.tick < mark->until)
		return;
	const bool helper = world.Has<Repulsable>(unit);
	if (mark == nullptr)
	{
		world.Add<RepulsorMark>(unit);
		mark = world.Get<RepulsorMark>(unit);
	}
	mark->until = helper ? game.tick + 2 * game.step.TicksPerSecond() : ~std::uint64_t{0};
}

inline void TeamSetRepulsor(GameWorld &game, const std::string &team, bool on)
{
	ForTeam(game, team, [&](ecs::Entity unit) { SetRepulsor(game, unit, on); });
}

inline void SetStealthEnabled(GameWorld &game, ecs::Entity unit, bool enabled)
{
	namespace gp = engine::gameplay;
	if (!game.world.IsAlive(unit))
		return;
	if (!game.world.Has<gp::ScriptStatus>(unit))
		game.world.Add<gp::ScriptStatus>(unit);
	game.world.Get<gp::ScriptStatus>(unit)->Set(gp::script_status::Unstealthed, !enabled);
}

namespace script_order_detail
{
inline bool Subdued(const GameWorld &game, ecs::Entity entity)
{
	const auto *off = game.world.Get<engine::gameplay::Disabled>(entity);
	return off != nullptr && (off->mask & engine::gameplay::disabled_type::Subdued) != 0;
}

inline bool IsStructure(const GameWorld &game, ecs::Entity entity)
{
	const auto *ref = game.world.Get<engine::gameplay::DefinitionRef>(entity);
	return ref != nullptr && game.templates.DefinitionAt(ref->index).Is("STRUCTURE");
}
}

inline void ExitContainer(GameWorld &game, ecs::Entity unit, bool instant = false)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const auto *seat = world.IsAlive(unit) ? world.Get<gp::Passenger>(unit) : nullptr;
	if (seat == nullptr || !world.IsAlive(seat->transport) || script_order_detail::Subdued(game, seat->transport))
		return;
	const ecs::Entity container = seat->transport;
	Commanded(game, unit);
	if (!world.Has<gp::ExitIntent>(unit))
		world.Add<gp::ExitIntent>(unit);
	*world.Get<gp::ExitIntent>(unit) = {container, instant ? 1u : 0u, 0u};
}

inline void NamedExitBuilding(GameWorld &game, ecs::Entity unit)
{
	if (!game.world.IsAlive(unit) || !HasAi(game, unit))
		return;
	NormalLocomotors(game, unit);
	ExitContainer(game, unit);
}

inline void TeamExitAllBuildings(GameWorld &game, const std::string &team)
{
	ForTeam(game, team, [&](ecs::Entity unit) { NamedExitBuilding(game, unit); });
}

inline void Evacuate(GameWorld &game, ecs::Entity container)
{
	if (!game.world.IsAlive(container) || script_order_detail::Subdued(game, container))
		return;
	const auto aboard = game.manifest.Aboard(container);
	const std::vector<ecs::Entity> riders(aboard.begin(), aboard.end());
	for (const ecs::Entity rider : riders)
		if (HasAi(game, rider))
			ExitContainer(game, rider);
}

inline void ExitSpecificBuilding(GameWorld &game, ecs::Entity building)
{
	if (!game.world.IsAlive(building) || !script_order_detail::IsStructure(game, building))
		return;
	if (HasAi(game, building))
	{
		NormalLocomotors(game, building);
		Evacuate(game, building);
		return;
	}
	// orderAllPassengersToExit over getContainedItemsList: a tunnel's is its network's (TunnelContain), all let out through
	// this one (the network's riders are one list: moved to its).
	namespace gp = engine::gameplay;
	if (const std::optional<std::uint32_t> network = game.manifest.NetworkOf(building))
		for (const ecs::Entity other : game.manifest.Network(*network))
		{
			if (other == building)
				continue;
			const auto theirs = game.manifest.Aboard(other);
			for (const ecs::Entity rider : std::vector<ecs::Entity>(theirs.begin(), theirs.end()))
				if (game.manifest.Move(other, building, rider))
				{
					if (auto *seat = game.world.Get<gp::Passenger>(rider))
						seat->transport = building;
					if (auto *away = game.world.Get<gp::OffMap>(rider))
						away->holder = building;
				}
		}
	const auto aboard = game.manifest.Aboard(building);
	const std::vector<ecs::Entity> riders(aboard.begin(), aboard.end());
	for (const ecs::Entity rider : riders)
	{
		if (!game.world.Has<engine::gameplay::ExitIntent>(rider))
			game.world.Add<engine::gameplay::ExitIntent>(rider);
		*game.world.Get<engine::gameplay::ExitIntent>(rider) = {building, 1u, 0u};
	}
}

inline void PlayerExitAllBuildings(GameWorld &game, std::uint32_t player)
{
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
	{
		if (game.roster.TeamAt(team).owner != player)
			continue;
		const std::vector<ecs::Entity> members = game.roster.TeamAt(team).members;
		for (const ecs::Entity unit : members)
			if (game.world.IsAlive(unit) && HasAi(game, unit) && script_order_detail::IsStructure(game, unit))
				Evacuate(game, unit);
	}
}

// Player::setObjectsEnabled (PLAYER_ENABLE_FACTORIES / PLAYER_DISABLE_FACTORIES): every one of the player's objects of
// exactly that template has DISABLED_SCRIPT_DISABLED cleared or set (setScriptStatus OBJECT_STATUS_SCRIPT_DISABLED).
inline void SetPlayerObjectsEnabled(GameWorld &game, std::uint32_t player, std::string_view type, bool enable)
{
	namespace gp = engine::gameplay;
	std::vector<ecs::Entity> matching;
	ecs::Query<ecs::Read<gp::Owner>, ecs::Read<gp::DefinitionRef>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		const auto owners = chunk.template Get<gp::Owner>();
		const auto refs = chunk.template Get<gp::DefinitionRef>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < owners.size(); ++row)
			if (owners[row].player == player && game.templates.DefinitionAt(refs[row].index).name == type)
				matching.push_back(entities[row]);
	});
	for (const ecs::Entity entity : matching)
		SetObjectPanelFlag(game, entity, "Enabled", enable);
}

// Locomotor::setCloseEnoughDist on the unit's current locomotor (doNamedSetStoppingDistance / doSetStoppingDistance): a
// distance under 0.5 changes nothing. False when the unit has no AI or no locomotor.
inline bool SetStoppingDistance(GameWorld &game, ecs::Entity unit, Engine::Math::Fixed distance)
{
	auto *locomotion = game.world.IsAlive(unit) ? game.world.Get<engine::gameplay::Locomotion>(unit) : nullptr;
	if (locomotion == nullptr || !HasAi(game, unit))
		return false;
	if (distance >= Engine::Math::Fixed::FromRatio(1, 2))
		locomotion->locomotor.closeEnough = distance;
	return true;
}

// doSetStoppingDistance (SET_STOPPING_DISTANCE): the team's members, newest first, until the first without an AI or
// a locomotor (the original returns there, the rest unchanged).
inline void TeamSetStoppingDistance(GameWorld &game, std::string_view team, Engine::Math::Fixed distance)
{
	const auto index = ResolveTeam(game, team);
	if (!index)
		return;
	const auto &members = game.roster.TeamAt(*index).members;
	for (auto it = members.rbegin(); it != members.rend(); ++it)
		if (!SetStoppingDistance(game, *it, distance))
			return;
}

// doNamedGuard: a unit with an AI, back on its normal locomotors, guards where it stands (aiGuardPosition, GUARDMODE_NORMAL).
// False when there is no such unit with an AI (nothing done).
inline bool NamedGuard(GameWorld &game, ecs::Entity unit)
{
	if (!game.world.IsAlive(unit) || !HasAi(game, unit))
		return false;
	NormalLocomotors(game, unit);
	GuardPosition(game, unit, game.world.Get<engine::gameplay::Transform>(unit)->position.XY());
	return true;
}

inline void PlayerHunt(GameWorld &game, std::uint32_t player)
{
	namespace gp = engine::gameplay;
	if (player >= game.roster.PlayerCount())
		return;
	game.roster.PlayerAt(player).unitsShouldHunt = true;
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
	{
		if (game.roster.TeamAt(team).owner != player)
			continue;
		const std::vector<ecs::Entity> members = game.roster.TeamAt(team).members;
		for (const ecs::Entity unit : members)
		{
			const auto *ref = game.world.IsAlive(unit) ? game.world.Get<gp::DefinitionRef>(unit) : nullptr;
			if (ref == nullptr)
				continue;
			const content::ObjectDefinition &kind = game.templates.DefinitionAt(ref->index);
			if (kind.Is("DOZER") || kind.Is("HARVESTER") || kind.Is("IGNORES_SELECT_ALL") || !HasAi(game, unit))
				continue;
			Commanded(game, unit);
			if (auto *aggression = game.world.Get<gp::Aggression>(unit))
				aggression->stance = gp::Stance::Hunt;
		}
	}
}
}
