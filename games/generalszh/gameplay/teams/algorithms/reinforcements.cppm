export module games.generalszh.gameplay.teams.algorithms.reinforcements;
import games.generalszh.gameplay.effects.algorithms.radius_decals;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.containment.components.scripted_evacuation;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.orders.algorithms.wander_orders;
import games.generalszh.gameplay.orders.algorithms.group_orders;
import games.generalszh.gameplay.containment.algorithms.garrisons;
import games.generalszh.gameplay.containment.algorithms.parachuting;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.content.powers.special_powers;
import games.generalszh.content.objects.model_conditions;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.components.cargo_size;
import engine.gameplay.rts.delivery.components.delivery;
import engine.gameplay.common.weapons.components.armament;

// ScriptActions::doCreateReinforcements (CREATE_REINFORCEMENT_TEAM): a new instance of the team (createInactiveTeam) at
// its reinforcement origin (none: the waypoint), and its transport:
// - its TeamTransport first, at the origin; then its units, each type in a row along x (2.25 of its major radius
//   apart), rows stacked in y (twice its major radius);
// - TeamStartsFull: its units load into the team's own TRANSPORTs (PartitionSolver, PREFER_FAST_SOLUTION: the units
//   by slots, the transports by room, most first, each unit into the first with room left);
// - with a transport that holds anything: each unit not yet inside that may ride it goes in, a new transport made at
//   the waypoint (a transport's major radius further along x each time) when the last is full; one that delivers by
//   air (DeliverPayloadAIUpdate) packs each first in its PutInContainer (a parachute);
// - then each of the team (newest first, as the team's list): a transport of that type flies its delivery to the
//   waypoint (deliverPayloadViaModuleData), or moves there to let its riders out (aiMoveToAndEvacuate, ...AndExit
//   with TeamTransportsExit: ScriptedEvacuation); every other one not held moves to the waypoint (aiMoveToPosition);
//   the team becomes active as its riders get out (never for a delivery by air);
// - without a transport the team is active at once, and moves to the waypoint as a group when that is not where it
//   came (groupMoveToPosition).
// Two retail quirks are not kept: every transport of the team flies its own delivery (the original has the first one
// fly it once for each, the others idle where they were made), and ties in the partition keep the team's order (the
// original's std::sort leaves them unspecified).
export namespace generalszh::gameplay
{
namespace gameplay = engine::gameplay;
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;

namespace reinforcement_detail
{
// Object::getTransportSlotCount (0: not transportable).
inline std::uint32_t SlotsOf(const GameWorld &game, ecs::Entity unit)
{
	const auto *size = game.world.Get<gameplay::CargoSize>(unit);
	return size != nullptr ? size->slots : 0u;
}

// TransportContain::isValidContainerFor: its kinds and standing allow the unit (same player), which takes room;
// `room`: and there is room for it.
inline bool Fits(GameWorld &game, ecs::Entity container, ecs::Entity unit, bool room)
{
	const auto *transport = game.world.Get<gameplay::Transport>(container);
	const std::uint32_t slots = SlotsOf(game, unit);
	if (transport == nullptr || slots == 0 || !MayContain(game, container, unit))
		return false;
	return !room || transport->occupied + slots <= transport->definition.slots;
}

inline bool Held(const GameWorld &game, ecs::Entity unit)
{
	if (game.world.Has<gameplay::Passenger>(unit))
		return true;
	const auto *off = game.world.Get<gameplay::Disabled>(unit);
	return off != nullptr && (off->mask & gameplay::disabled_type::Held) != 0;
}

inline bool HasModule(const content::ObjectDefinition &object, std::string_view type)
{
	return std::any_of(object.modules.begin(), object.modules.end(), [&](const content::ModuleEntry &module) { return module.type == type; });
}

// The team's members, newest first (Team::iterate_TeamMemberList: members are prepended).
inline std::vector<ecs::Entity> NewestFirst(const GameWorld &game, std::uint32_t team)
{
	const auto &members = game.roster.TeamAt(team).members;
	return {members.rbegin(), members.rend()};
}

// DeliverPayloadAIUpdate::deliverPayloadViaModuleData(destination): its module's run, to and over the destination.
inline void DeliverViaModuleData(GameWorld &game, ecs::Entity carrier, const content::PayloadModule &data, FixedVector2 destination)
{
	auto &world = game.world;
	if (!world.IsAlive(carrier) || !world.Has<gameplay::MoveOrder>(carrier))
		return;
	gameplay::Delivery delivery;
	delivery.target = destination;
	delivery.moveTo = destination;
	delivery.dropOffset = data.dropOffset;
	delivery.dropVariance = data.dropVariance;
	delivery.distance = data.deliveryDistance;
	delivery.maxAttempts = data.maxAttempts;
	delivery.doorDelay = static_cast<std::uint32_t>(data.doorDelay);
	delivery.dropDelay = static_cast<std::uint32_t>(data.dropDelay);
	delivery.doorOpening = content::ModelConditionBit("DOOR_1_OPENING");
	delivery.doorClosing = content::ModelConditionBit("DOOR_1_CLOSING");
	delivery.entered = 1;
	if (!world.Has<gameplay::Delivery>(carrier))
		world.Add<gameplay::Delivery>(carrier);
	*world.Get<gameplay::Delivery>(carrier) = delivery;
	*world.Get<gameplay::MoveOrder>(carrier) = gameplay::MoveToPoint(destination);
	// deliverPayload: its module's DeliveryDecal on the destination, until it heads off the map.
	LayRadiusDecal(game, carrier, data.deliveryDecal, data.deliveryDecalRadius, {destination.x, destination.y, game.ground.At(destination)}, RadiusDecalUntil::HeadsOffMap);
}

// aiMoveToAndEvacuate / aiMoveToAndEvacuateAndExit (CMD_FROM_SCRIPT), not for what cannot move.
inline void MoveToAndEvacuate(GameWorld &game, ecs::Entity transport, FixedVector2 destination, bool exits, bool chinook)
{
	auto &world = game.world;
	if (!world.IsAlive(transport) || !world.Has<gameplay::MoveOrder>(transport) || !detail::Mobile(game, transport))
		return;
	Commanded(game, transport);
	const auto *at = world.Get<gameplay::Transform>(transport);
	ScriptedEvacuation evacuation;
	evacuation.destination = destination;
	evacuation.origin = at != nullptr ? at->position.XY() : destination;
	evacuation.exits = exits ? 1 : 0;
	evacuation.chinook = chinook ? 1 : 0;
	evacuation.locked = chinook ? 0 : 1;
	if (!world.Has<ScriptedEvacuation>(transport))
		world.Add<ScriptedEvacuation>(transport);
	*world.Get<ScriptedEvacuation>(transport) = evacuation;
	*world.Get<gameplay::MoveOrder>(transport) = gameplay::MoveToPoint(destination);
	if (auto *attack = world.Get<gameplay::AttackTarget>(transport))
		*attack = {};
	detail::EndStance(game, transport);
}
}

void CreateReinforcements(GameWorld &game, const std::string &team, const std::string &waypoint,
	const std::function<void(const std::string &)> &runActions = {})
{
	using namespace reinforcement_detail;
	auto &world = game.world;
	const auto named = ResolveTeam(game, team);
	const std::uint32_t at = game.waypoints.Find(waypoint);
	if (!named || at == gameplay::WaypointGraph::None)
		return;
	const std::uint32_t prototype = game.roster.PrototypeOf(*named);
	const engine::level::Properties &info = game.teams.At(prototype);
	const FixedVector2 destination = game.waypoints.Position(at).XY();
	FixedVector2 origin = destination;
	bool needToMove = false;
	if (const auto start = info.Get<std::string>("teamReinforcementOrigin"))
		if (const std::uint32_t from = game.waypoints.Find(*start); from != gameplay::WaypointGraph::None)
		{
			origin = game.waypoints.Position(from).XY();
			needToMove = origin.x != destination.x || origin.y != destination.y;
		}
	const std::uint32_t instance = CreateInactiveTeam(game, prototype, runActions);
	const content::GameContent &content = game.templates.Content();

	// The transport first, at the origin.
	const std::string transportType = info.Get<std::string>("teamTransport").value_or("");
	const content::ObjectDefinition *transportKind = transportType.empty() ? nullptr : content.objects.Find(transportType);
	ecs::Entity transport;
	std::uint32_t transportIndex = 0xFFFFFFFFu;
	if (transportKind != nullptr)
	{
		transport = SpawnObject(game, transportType, origin, {}, instance, {});
		if (const auto *ref = world.IsAlive(transport) ? world.Get<gameplay::DefinitionRef>(transport) : nullptr)
			transportIndex = ref->index;
	}
	const std::optional<content::PayloadModule> payload =
		transportKind != nullptr ? content::ReadDeliverPayloadModule(*transportKind, game.step) : std::nullopt;
	const std::string putInContainer = payload ? payload->putInContainer : std::string{};
	const bool packs = !putInContainer.empty() && content.objects.Find(putInContainer) != nullptr;

	// The units, each type in a row.
	for (int slot = 1; slot <= 7; ++slot)
	{
		const auto type = info.Get<std::string>("teamUnitType" + std::to_string(slot));
		const auto count = info.Get<std::int64_t>("teamUnitMaxCount" + std::to_string(slot));
		const content::ObjectDefinition *object = type && !type->empty() ? content.objects.Find(*type) : nullptr;
		if (object == nullptr)
			continue;
		FixedVector2 pos = origin;
		const Fixed radius = object->geometry.majorRadius;
		for (std::int64_t index = 0; count && index < *count; ++index)
		{
			pos.x = origin.x + Fixed::FromRatio(9, 4) * Fixed::FromInt(index) * radius;
			SpawnObject(game, *type, pos, {}, instance, {});
		}
		if (count && *count > 0)
			pos.y = pos.y + radius * 2;
		origin.y = pos.y;
	}
	origin = destination;

	// TeamStartsFull: into the team's own transports.
	if (info.Get<bool>("teamStartsFull").value_or(false))
	{
		std::vector<std::pair<ecs::Entity, std::uint32_t>> units, spaces;
		for (const ecs::Entity member : NewestFirst(game, instance))
		{
			if (member == transport || !world.IsAlive(member))
				continue;
			const auto *ref = world.Get<gameplay::DefinitionRef>(member);
			if (ref != nullptr && game.templates.DefinitionAt(ref->index).Is("TRANSPORT"))
			{
				if (const auto *room = world.Get<gameplay::Transport>(member))
					spaces.emplace_back(member, room->definition.slots);
			}
			else
			{
				const std::uint32_t slots = SlotsOf(game, member);
				units.emplace_back(member, slots == 0 ? 0x7fffffu : slots);
			}
		}
		const auto most = [](const auto &a, const auto &b) { return a.second > b.second; };
		std::stable_sort(units.begin(), units.end(), most);
		std::stable_sort(spaces.begin(), spaces.end(), most);
		for (const auto &[unit, slots] : units)
			for (auto &[carrier, room] : spaces)
				if (slots <= room)
				{
					room -= slots;
					PutInside(game, carrier, unit, slots);
					break;
				}
	}

	// Into the transport (and more of them as they fill).
	std::uint32_t transportCount = 1;
	if (world.IsAlive(transport) && world.Has<gameplay::Transport>(transport))
	{
		for (const ecs::Entity member : NewestFirst(game, instance))
		{
			if (!world.IsAlive(member) || world.Get<gameplay::DefinitionRef>(member)->index == transportIndex || world.Has<gameplay::Passenger>(member))
				continue;
			FixedVector2 pos = origin;
			pos.x = pos.x + Fixed::FromInt(transportCount) * transportKind->geometry.majorRadius;
			if (!Fits(game, transport, member, false))
				continue;
			if (!Fits(game, transport, member, true))
			{
				// Full: another.
				transport = SpawnObject(game, transportType, pos, {}, instance, {});
				++transportCount;
				if (!world.IsAlive(transport) || !world.Has<gameplay::Transport>(transport))
					break;
			}
			ecs::Entity rider = member;
			const std::uint32_t slots = SlotsOf(game, member);
			if (packs)
			{
				const ecs::Entity chute = SpawnObject(game, putInContainer, pos, {}, instance, {});
				if (world.IsAlive(chute) && PutInParachute(game, chute, member))
				{
					if (!world.Has<gameplay::OffMap>(member))
						world.Add<gameplay::OffMap>(member);
					rider = chute;
				}
			}
			PutInside(game, transport, rider, slots);
		}
	}

	if (transportKind == nullptr)
	{
		// No transport: active now, and on its way if it came from elsewhere (groupMoveToPosition).
		game.roster.SetActive(instance);
		if (needToMove)
			GroupMoveToPosition(game, NewestFirst(game, instance), destination, false);
		return;
	}
	const bool chinook = HasModule(*transportKind, "ChinookAIUpdate");
	const bool exits = info.Get<bool>("teamTransportsExit").value_or(false);
	for (const ecs::Entity member : NewestFirst(game, instance))
	{
		const auto *ref = world.IsAlive(member) ? world.Get<gameplay::DefinitionRef>(member) : nullptr;
		if (ref == nullptr)
			continue;
		if (ref->index == transportIndex)
		{
			NormalLocomotors(game, member);
			if (payload)
				DeliverViaModuleData(game, member, *payload, destination);
			else
				MoveToAndEvacuate(game, member, destination, exits, chinook);
		}
		else if (!Held(game, member))
		{
			NormalLocomotors(game, member);
			OrderMove(game, member, destination);
		}
	}
}

// What the scripted evacuations did beyond their transports (ScriptedEvacuationSystem), in order: teams made active,
// transports removed (TheGameLogic->destroyObject: gone, riders and all).
inline void ApplyScriptedEvacuations(GameWorld &game)
{
	auto *resource = game.world.FindResource<ScriptedEvacuationEvents>();
	if (resource == nullptr)
		return;
	std::vector<ScriptedEvacuationEvent> events;
	resource->AppendTo(events);
	resource->Reset(0);
	std::vector<ecs::Entity> removed;
	for (const ScriptedEvacuationEvent &event : events)
	{
		if (!game.world.IsAlive(event.unit))
			continue;
		if (event.kind == ScriptedEvacuationEvent::Kind::ActivateTeam)
		{
			if (const auto *member = game.world.Get<gameplay::TeamMember>(event.unit))
				game.roster.SetActive(member->team);
		}
		else
			removed.push_back(event.unit);
	}
	if (!removed.empty())
		RetireNow(game, std::move(removed));
}
}
