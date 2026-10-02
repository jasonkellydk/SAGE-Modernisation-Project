export module games.generalszh.gameplay.containment.algorithms.player_evacuation;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.gameplay.containment.algorithms.railed_transports;
import games.generalszh.gameplay.teams.algorithms.reinforcements;
import games.generalszh.gameplay.scripts.algorithms.unit_script_orders;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.targetable;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.common.weapons.components.weapon_slots;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.docking.components.dock;
import games.generalszh.gameplay.orders.algorithms.unit_orders;

// AIGroup::groupEvacuate (GameLogic::onEvacuate, a player's EVACUATE button: MSG_EVACUATE), for one of the group: one
// with an AI lets its riders out (aiEvacuate(FALSE)), an AIRCRAFT in the air first coming down where it is
// (aiMoveToAndEvacuate at its own spot: a Chinook lands to let them out); a STRUCTURE without one orders them all out
// (orderAllPassengersToExit).
// PlayerExit: GameLogic::onExit (MSG_EXIT, a player's rider button in its container's inventory).
export namespace generalszh::gameplay
{
inline void GroupEvacuate(GameWorld &game, ecs::Entity unit)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const auto *ref = world.IsAlive(unit) ? world.Get<gp::DefinitionRef>(unit) : nullptr;
	if (ref == nullptr)
		return;
	const content::ObjectDefinition &kind = game.templates.DefinitionAt(ref->index);
	// RailedTransportAIUpdate::privateEvacuate: a railed transport lets them out through its dock.
	if (world.Has<RailedTransport>(unit))
	{
		RailedEvacuate(game, unit);
		return;
	}
	if (HasAi(game, unit))
	{
		const auto *targetable = world.Get<gp::Targetable>(unit);
		const bool airborne = targetable != nullptr && (targetable->classes & gp::target_class::AirborneVehicle) != 0;
		if (kind.Is("AIRCRAFT") && airborne)
		{
			reinforcement_detail::MoveToAndEvacuate(game, unit, world.Get<gp::Transform>(unit)->position.XY(), false, reinforcement_detail::HasModule(kind, "ChinookAIUpdate"));
			return;
		}
		Evacuate(game, unit);
		return;
	}
	if (kind.Is("STRUCTURE"))
		ExitSpecificBuilding(game, unit);
}

// GameLogic::onExit (MSG_EXIT, a rider's inventory button: GUI_COMMAND_EXIT_CONTAINER) for the sender's `rider` and the
// `container` it has selected: the rider's temporary weapon lock goes (releaseWeaponLock(LOCKED_TEMPORARILY)); with an AI
// it asks to get out of that container (aiExit(container, CMD_FROM_PLAYER) -> privateExit: nothing when it is not inside
// it (a tunnel: anywhere in its network, TunnelContain::isContained), nor while the container is subdued), through
// AIExitState:
// - a railed transport lets exactly one out through its dock, its first aboard whoever asked
//   (RailedTransportContain::exitObjectViaDoor -> RailedTransportDockUpdate::unloadSingleObject), but not while its dock
//   is shut under way (isSpecificRiderFreeToExit false: DOOR_NONE_AVAILABLE, the exit fails);
// - anything else lets it out when its exit is next free, a transport aloft that must land first coming down for it
//   (getAiFreeToExit: WAIT_TO_EXIT; the ExitIntent the unloading system serves); a tunnel lets it out of the one
//   selected (the network's riders are one list: it is moved to that tunnel's).
inline void PlayerExit(GameWorld &game, ecs::Entity rider, ecs::Entity container)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (!world.IsAlive(rider) || !world.IsAlive(container))
		return;
	if (auto *slots = world.Get<gp::WeaponSlots>(rider))
		gp::ReleaseTemporaryLock(*slots);
	if (!HasAi(game, rider))
		return;
	gp::Passenger *seat = world.Get<gp::Passenger>(rider);
	if (seat == nullptr)
		return;
	gp::CargoManifest &manifest = game.manifest;
	if (seat->transport != container)
	{
		const std::optional<std::uint32_t> network = manifest.NetworkOf(container);
		if (!network || manifest.NetworkOf(seat->transport) != network)
			return;
	}
	if (script_order_detail::Subdued(game, container))
		return;
	if (world.Has<RailedTransport>(container))
	{
		Commanded(game, rider);
		if (const auto *dock = world.Get<gp::Dock>(container); dock != nullptr && dock->open)
			RailedExitOne(game, container);
		return;
	}
	if (seat->transport != container && manifest.Move(seat->transport, container, rider))
	{
		seat->transport = container;
		if (auto *away = world.Get<gp::OffMap>(rider))
			away->holder = container;
	}
	ExitContainer(game, rider);
}
}
