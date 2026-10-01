export module games.generalszh.gameplay.orders.algorithms.unit_orders;
export import games.generalszh.gameplay.containment.components.assault_transport;
import engine.gameplay.rts.docking.components.dock_look;
import games.generalszh.gameplay.containment.components.railed_transport;
import games.generalszh.gameplay.flight_deck.components.flight_deck;
import engine.gameplay.rts.combat.components.attack_move;
import games.generalszh.gameplay.containment.components.rider_change;
import std;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import games.generalszh.content.objects.kind_of;
import games.generalszh.gameplay.ai.components.tunnel_guard;
import games.generalszh.gameplay.ai.components.attack_squad;
import games.generalszh.gameplay.containment.components.scripted_evacuation;
import games.generalszh.gameplay.ai.components.guard;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.rts.construction.components.builder;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.mines.algorithms.mine_clearing;
import games.generalszh.gameplay.hacking.algorithms.hack_orders;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.movement.systems.movement_system;
import engine.gameplay.rts.combat.components.aggression;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.components.mount;
import games.generalszh.content.containment.transport_content;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.weapons.resources.weapon_catalog;
import engine.gameplay.common.weapons.components.weapon_slots;
import engine.gameplay.rts.harvesting.components.harvester;
import engine.gameplay.rts.harvesting.components.resource_store;
import engine.gameplay.rts.docking.components.dock;
import engine.gameplay.rts.docking.components.docking;
import engine.gameplay.rts.navigation.components.navigation;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.status.components.ai_activity;
import engine.gameplay.rts.navigation.components.ignored_obstacle;

// Orders to units, the same whether a player gave them (through the command
// bus) or a script did: one path, one set of rules. Such an order is no command of the unit's own AI
// (CMD_FROM_PLAYER / CMD_FROM_SCRIPT): the ability it was carrying out ends (Commanded); a computer player's own orders
// (CMD_FROM_AI) go through AiMove and AiIdle instead.
export namespace generalszh::gameplay
{
namespace gameplay = engine::gameplay;
using Engine::Math::FixedVector2;

namespace detail
{
// Object::isMobile: not while disabled (any type); the AI's move orders return early then.
// Its AI's machine is locked (StateMachine::lock: a scripted AIMoveAndEvacuateState / AIMoveAndDeleteState): no order
// changes its state.
inline bool Locked(const GameWorld &game, ecs::Entity unit)
{
	const auto *evacuation = game.world.Get<ScriptedEvacuation>(unit);
	return evacuation != nullptr && evacuation->locked != 0;
}

inline bool Mobile(const GameWorld &game, ecs::Entity unit)
{
	const auto *off = game.world.Get<gameplay::Disabled>(unit);
	return off == nullptr || off->mask == 0;
}

// A new order ends any docking (AIDockMachine::halt: the dock lets it go) and starts its route afresh; a supply
// truck's player taking it over takes it off its round (SupplyTruckStateMachine: ownerPlayerCommanded).
void TakeOver(GameWorld &game, ecs::Entity unit, bool fromPlayer)
{
	auto &world = game.world;
	// A hacker hacking packs up first (HackInternetAIUpdate::aiDoCommand: the order waits).
	InterruptHack(game, unit);
	if (auto *docking = world.Get<gameplay::Docking>(unit))
	{
		// DockUpdate::cancelDock: the dock's active mover gone, its docking looks clear.
		if (docking->granted)
			if (auto *look = world.Get<gameplay::DockLook>(docking->dock))
				look->flags = 0;
		*docking = {};
	}
	if (auto *route = world.Get<gameplay::Route>(unit))
		route->planned = false;
	// A new order: the obstacle it was walking out of counts again.
	if (world.Has<gameplay::IgnoredObstacle>(unit))
		world.Remove<gameplay::IgnoredObstacle>(unit);
	if (auto *harvester = world.Get<gameplay::Harvester>(unit))
		harvester->directed = fromPlayer;
	// A player's order ends a builder's task (DozerAIUpdate: a new command cancels the build or repair; an abandoned
	// structure waits to be resumed).
	if (fromPlayer && world.Has<gameplay::Builder>(unit))
		world.Remove<gameplay::Builder>(unit);
}

// A direct order ends guarding and hunting (holding stays), as the original.
void EndStance(GameWorld &game, ecs::Entity unit)
{
	if (auto *aggression = game.world.Get<gameplay::Aggression>(unit); aggression != nullptr && aggression->stance != gameplay::Stance::Hold)
		aggression->stance = gameplay::Stance::Idle;
	// Guarding a tunnel network ends too (a new AI state).
	if (game.world.Has<TunnelGuard>(unit))
		game.world.Remove<TunnelGuard>(unit);
	if (game.world.Has<AttackSquad>(unit))
		game.world.Remove<AttackSquad>(unit);
	// Guarding ends too (AIGuardState left).
	if (game.world.Has<Guard>(unit))
		game.world.Remove<Guard>(unit);
	// And attack-moving (AIAttackMoveToState left).
	if (game.world.Has<gameplay::AttackMove>(unit))
		game.world.Remove<gameplay::AttackMove>(unit);
}
}

// An order from a player or a script reached the unit (its AI's last command source is no longer CMD_FROM_AI): what its
// own behaviours had it do ends at their next update (SpecialAbilityUpdate, CommandButtonHuntUpdate), and its AI is no
// longer busy on its own.
inline void Commanded(GameWorld &game, ecs::Entity unit)
{
	if (auto *activity = game.world.Get<gameplay::AiActivity>(unit))
	{
		activity->commanded = 1;
		activity->fromPlayer = 0; // (a player's order marks it after: ApplyCommand)
		activity->busy = 0;
	}
	// AssaultTransportAIUpdate::aiDoCommand: any order from outside starts it over (an attack-move or attack order then
	// says which; OrderStop calls its members back first).
	if (auto *assault = game.world.Get<AssaultTransport>(unit))
		assault->Reset();
}

// An order from its own AI (CMD_FROM_AI): the last command source is its AI again.
inline void AiCommanded(GameWorld &game, ecs::Entity unit)
{
	if (auto *activity = game.world.Get<gameplay::AiActivity>(unit))
	{
		activity->commanded = 0;
		activity->fromPlayer = 0;
	}
}

// AIUpdateInterface::aiIdle(CMD_FROM_AI): it stops moving and attacking, guarding and hunting end, and it is no longer
// busy; no command reached it from outside.
inline void AiIdle(GameWorld &game, ecs::Entity unit)
{
	auto &world = game.world;
	if (!world.IsAlive(unit))
		return;
	// HackInternetAIUpdate::aiDoCommand: a hacker hacking packs up first (the idle waits).
	InterruptHack(game, unit);
	if (auto *route = world.Get<gameplay::Route>(unit))
		route->planned = false;
	if (auto *order = world.Get<gameplay::MoveOrder>(unit))
		order->mode = gameplay::MoveMode::Idle;
	if (auto *attack = world.Get<gameplay::AttackTarget>(unit))
		*attack = {};
	detail::EndStance(game, unit);
	if (auto *activity = world.Get<gameplay::AiActivity>(unit))
	{
		activity->busy = 0;
		activity->commanded = 0;
	}
}

// AIUpdateInterface::aiBusy(CMD_FROM_AI): it stops and is busy on its own (not idle, looking for no targets).
inline void AiBusyOn(GameWorld &game, ecs::Entity unit)
{
	AiIdle(game, unit);
	if (auto *activity = game.world.Get<gameplay::AiActivity>(unit))
		activity->busy = 1;
}

// aiMoveToPosition: `commanded` from a player or a script (else CMD_FROM_AI: AiMove). Its goal adjusted and claimed
// (AIMoveToState), unless the move is another state's (`claim`: aiEnter's AIEnterState claims nothing).
void OrderMove(GameWorld &game, ecs::Entity unit, FixedVector2 destination, bool fromPlayer = false, bool commanded = true,
	gameplay::GoalClaim claim = gameplay::GoalClaim::Adjust)
{
	auto &world = game.world;
	if (!world.IsAlive(unit) || detail::Locked(game, unit))
		return;
	if (commanded)
		Commanded(game, unit);
	else
		AiCommanded(game, unit);
	if (world.Has<gameplay::Passenger>(unit) || !detail::Mobile(game, unit))
		return;
	detail::TakeOver(game, unit, fromPlayer);
	if (auto *order = world.Get<gameplay::MoveOrder>(unit))
		*order = gameplay::MoveToPoint(destination, claim);
	if (auto *attack = world.Get<gameplay::AttackTarget>(unit))
		*attack = {};
	detail::EndStance(game, unit);
}

// aiAttackMoveToPosition (CMD_FROM_PLAYER): a move there (ending what it did), as AIAttackMoveToState: taking on what it
// comes across on the way.
void OrderAttackMove(GameWorld &game, ecs::Entity unit, FixedVector2 destination)
{
	auto &world = game.world;
	if (DesignateFlightDeck(world, unit, DeckOrder::AttackMove, {}, destination, false))
		return;
	if (!world.IsAlive(unit) || world.Get<gameplay::MoveOrder>(unit) == nullptr || world.Get<gameplay::Aggression>(unit) == nullptr)
		return;
	OrderMove(game, unit, destination, true);
	if (auto *assault = world.Get<AssaultTransport>(unit))
	{
		assault->attackMoveGoal = destination;
		assault->isAttackMove = 1;
	}
	if (world.Get<gameplay::MoveOrder>(unit)->mode == gameplay::MoveMode::Idle)
		return; // it could not take the move (locked, carried, immobile)
	if (!world.Has<gameplay::AttackMove>(unit))
		world.Add<gameplay::AttackMove>(unit);
	*world.Get<gameplay::AttackMove>(unit) = gameplay::AttackMove{destination};
}

// An attack order. A TransportAIUpdate carrier whose passengers may fire passes it on to them
// (TransportAIUpdate::privateAttackObject), in the order they got in; a portable structure (an Overlord's or Helix's
// add-on) that is hacked, EMPed, subdued or paralyzed is left out.
void OrderAttack(GameWorld &game, ecs::Entity unit, ecs::Entity target, std::uint32_t maxShots = 0, gameplay::CommandSource source = gameplay::CommandSource::Player);

// AIUpdateInterface::aiAttackPosition: attack the spot on the ground (its turrets too), at most `maxShots` shots (0: no
// limit); `commanded` from a player or a script, else its own AI's.
void OrderAttackPosition(GameWorld &game, ecs::Entity unit, Engine::Math::FixedVector3 position, std::uint32_t maxShots, bool commanded = true)
{
	auto &world = game.world;
	if (DesignateFlightDeck(world, unit, DeckOrder::AttackPosition, {}, position.XY(), !commanded))
		return;
	if (!world.IsAlive(unit) || detail::Locked(game, unit))
		return;
	// TransportAIUpdate::privateAttackPosition: a transport letting its riders fire passes a direct order (a player's or a
	// script's) on to each of them first (aiAttackPosition), a portable structure not while hacked, EMPed, subdued or
	// paralyzed.
	if (commanded)
	{
		const auto *transport = world.Get<gameplay::Transport>(unit);
		const auto *kind = world.Get<gameplay::DefinitionRef>(unit);
		if (transport != nullptr && transport->definition.passengersFire && kind != nullptr &&
			std::ranges::any_of(game.templates.DefinitionAt(kind->index).modules, [](const content::ModuleEntry &module) { return module.type == "TransportAIUpdate"; }))
		{
			static constexpr std::size_t PortableStructure = content::KindOfBit("PORTABLE_STRUCTURE");
			constexpr std::uint32_t Stopped = gameplay::disabled_type::Hacked | gameplay::disabled_type::Emp | gameplay::disabled_type::Subdued |
				gameplay::disabled_type::Paralyzed;
			const std::vector<ecs::Entity> riders(game.manifest.Aboard(unit).begin(), game.manifest.Aboard(unit).end());
			for (const ecs::Entity passenger : riders)
			{
				const auto *passengerKind = world.Get<gameplay::DefinitionRef>(passenger);
				const auto *off = world.Get<gameplay::Disabled>(passenger);
				if (passengerKind != nullptr && content::HasKindOf(game.templates.DefinitionAt(passengerKind->index).kinds, PortableStructure) && off != nullptr &&
					(off->mask & Stopped) != 0)
					continue;
				OrderAttackPosition(game, passenger, position, maxShots, true);
			}
		}
	}
	// privateAttackPosition: a weapon in hand with a ContinueAttackRange looks (seeing through stealth) for the closest it
	// may attack within that range of the spot and attacks that instead; finding none, it fires one shot at the spot.
	const gameplay::CommandSource source = commanded ? gameplay::CommandSource::Player : gameplay::CommandSource::Ai;
	if (const auto *armament = world.Get<gameplay::Armament>(unit); armament != nullptr && armament->weapon != gameplay::WeaponCatalog::None)
	{
		const Engine::Math::Fixed range = game.templates.weapons.At(armament->weapon).continueAttackRange;
		if (range > Engine::Math::Fixed{})
		{
			if (const ecs::Entity victim = mine_clearing_detail::Closest(game, unit, position.XY(), range, source, true, std::nullopt, {}); victim != ecs::Entity{})
			{
				OrderAttack(game, unit, victim, maxShots, source);
				return;
			}
			maxShots = 1;
		}
	}
	if (commanded)
		Commanded(game, unit);
	else
		AiCommanded(game, unit);
	auto *attack = world.Get<gameplay::AttackTarget>(unit);
	if (attack == nullptr)
		return;
	*attack = gameplay::AttackTarget{.ordered = true, .atPosition = 1, .source = commanded ? gameplay::CommandSource::Player : gameplay::CommandSource::Ai,
		.shotsLeft = maxShots, .position = position};
	detail::EndStance(game, unit);
}

// Object::reloadAllAmmo(now): every weapon's clip full and ready to fire at once.
void ReloadAllAmmo(GameWorld &game, ecs::Entity unit)
{
	auto &world = game.world;
	const auto *weapons = world.FindResource<gameplay::WeaponCatalog>();
	if (weapons == nullptr || !world.IsAlive(unit))
		return;
	if (auto *armament = world.Get<gameplay::Armament>(unit); armament != nullptr && armament->weapon != gameplay::WeaponCatalog::None)
	{
		armament->clip = weapons->At(armament->weapon).clipSize;
		armament->readyTick = game.tick;
		armament->reloading = false;
		armament->scatterUsed = 0;
	}
	if (auto *set = world.Get<gameplay::WeaponSlots>(unit))
		for (gameplay::WeaponSlot &slot : set->slots)
			if (slot.weapon != gameplay::WeaponCatalog::None)
			{
				slot.clip = weapons->At(slot.weapon).clipSize;
				slot.readyTick = game.tick;
				slot.reloading = false;
				slot.scatterUsed = 0;
			}
}

// AIUpdateInterface::aiAttackObject: attack `target`, at most `maxShots` shots (0: no limit), ordered from `source` (its
// own AI's order leaves it uncommanded; the attack remembers the source for its choice of weapon).
void OrderAttack(GameWorld &game, ecs::Entity unit, ecs::Entity target, std::uint32_t maxShots, gameplay::CommandSource source)
{
	auto &world = game.world;
	// FlightDeckBehavior::aiDoCommand: a carrier's attack goes to its jets.
	if (DesignateFlightDeck(world, unit, DeckOrder::Attack, target, {}, source == gameplay::CommandSource::Ai))
		return;
	if (!world.IsAlive(unit) || !world.IsAlive(target) || detail::Locked(game, unit))
		return;
	if (source == gameplay::CommandSource::Ai)
		AiCommanded(game, unit);
	else
		Commanded(game, unit);
	if (auto *assault = world.Get<AssaultTransport>(unit))
		assault->isAttackObject = 1;
	const auto *transport = world.Get<gameplay::Transport>(unit);
	const auto *kind = world.Get<gameplay::DefinitionRef>(unit);
	if (transport != nullptr && transport->definition.passengersFire && kind != nullptr &&
		std::ranges::any_of(game.templates.DefinitionAt(kind->index).modules, [](const content::ModuleEntry &module) { return module.type == "TransportAIUpdate"; }))
	{
		static constexpr std::size_t PortableStructure = content::KindOfBit("PORTABLE_STRUCTURE");
		constexpr std::uint32_t Stopped = gameplay::disabled_type::Hacked | gameplay::disabled_type::Emp | gameplay::disabled_type::Subdued |
			gameplay::disabled_type::Paralyzed;
		for (const ecs::Entity passenger : game.manifest.Aboard(unit))
		{
			const auto *passengerKind = world.Get<gameplay::DefinitionRef>(passenger);
			const auto *off = world.Get<gameplay::Disabled>(passenger);
			if (passengerKind != nullptr && content::HasKindOf(game.templates.DefinitionAt(passengerKind->index).kinds, PortableStructure) && off != nullptr &&
				(off->mask & Stopped) != 0)
				continue;
			if (auto *attack = world.Get<gameplay::AttackTarget>(passenger))
				*attack = {.target = target, .ordered = true, .source = source};
		}
	}
	if (auto *attack = world.Get<gameplay::AttackTarget>(unit))
		*attack = {.target = target, .ordered = true, .source = source, .shotsLeft = maxShots};
}

void OrderBoard(GameWorld &game, ecs::Entity unit, ecs::Entity transport, bool commanded);

void OrderStop(GameWorld &game, ecs::Entity unit, bool fromPlayer = false, bool commanded = true)
{
	auto &world = game.world;
	if (DesignateFlightDeck(world, unit, DeckOrder::Idle, {}, {}, !commanded))
		return;
	if (!world.IsAlive(unit) || detail::Locked(game, unit))
		return;
	// AssaultTransportAIUpdate::aiDoCommand(AICMD_IDLE): its members outside called back in (retrieveMembers).
	if (const auto *assault = commanded ? world.Get<AssaultTransport>(unit) : nullptr)
		for (std::uint32_t index = 0; index < assault->count; ++index)
		{
			const ecs::Entity member = assault->members[index];
			if (world.IsAlive(member) && !world.Has<gameplay::Passenger>(member) && !world.Has<gameplay::Boarding>(member))
				OrderBoard(game, member, unit, false);
		}
	if (commanded)
		Commanded(game, unit);
	else
		AiCommanded(game, unit);
	detail::TakeOver(game, unit, fromPlayer);
	// SupplyTruckAIUpdate::privateIdle: a player's stop takes a truck off its round for good.
	if (auto *harvester = fromPlayer ? world.Get<gameplay::Harvester>(unit) : nullptr)
		harvester->forceBusy = true;
	if (auto *order = world.Get<gameplay::MoveOrder>(unit))
		order->mode = gameplay::MoveMode::Idle;
	if (auto *attack = world.Get<gameplay::AttackTarget>(unit))
		*attack = {};
	detail::EndStance(game, unit);
}

// Along the waypoint path labelled `label`, from its waypoint closest to `from`.
void OrderFollowPath(GameWorld &game, ecs::Entity unit, const std::string &label, std::optional<FixedVector2> from = std::nullopt, bool exact = false)
{
	auto &world = game.world;
	if (world.IsAlive(unit))
		Commanded(game, unit);
	auto *order = world.IsAlive(unit) && detail::Mobile(game, unit) ? world.Get<gameplay::MoveOrder>(unit) : nullptr;
	if (order == nullptr)
		return;
	const FixedVector2 near = from.value_or(world.Get<gameplay::Transform>(unit)->position.XY());
	*order = gameplay::FollowPath(game.waypoints, game.waypoints.ClosestOnPath(near, label), exact);
}

// Held units may not move (they still shoot what comes into range).
void OrderHold(GameWorld &game, ecs::Entity unit, bool held)
{
	auto &world = game.world;
	if (!world.IsAlive(unit))
		return;
	Commanded(game, unit);
	if (auto *aggression = world.Get<gameplay::Aggression>(unit))
		aggression->stance = held ? gameplay::Stance::Hold : gameplay::Stance::Idle;
	if (auto *order = held ? world.Get<gameplay::MoveOrder>(unit) : nullptr)
		order->mode = gameplay::MoveMode::Idle;
}

// Whether `unit` may dock at `dock` (ActionManager::canTransferSuppliesAt): a truck at a warehouse with boxes left,
// not an enemy's; at its own player's supply centre, carrying something.
bool CanDockAt(const GameWorld &game, ecs::Entity unit, ecs::Entity dock)
{
	const auto &world = game.world;
	if (!world.IsAlive(unit) || !world.IsAlive(dock) || !world.Has<gameplay::Dock>(dock) || !world.Has<gameplay::Docking>(unit))
		return false;
	const auto *harvester = world.Get<gameplay::Harvester>(unit);
	const auto *owner = world.Get<gameplay::Owner>(unit);
	const auto *dockOwner = world.Get<gameplay::Owner>(dock);
	if (harvester == nullptr || owner == nullptr || dockOwner == nullptr)
		return false;
	if (const auto *store = world.Get<gameplay::ResourceStore>(dock))
		return store->boxes > 0 && RelationOf(game, dock, unit) != gameplay::Relationship::Enemies;
	if (world.Has<gameplay::ResourceDepot>(dock))
		return harvester->boxes > 0 && dockOwner->player == owner->player;
	return false;
}

// AIUpdateInterface::aiDock (SupplyTruckAIUpdate::privateDock: from the player, the dock becomes its preferred one).
void OrderDock(GameWorld &game, ecs::Entity unit, ecs::Entity dock, bool fromPlayer = false)
{
	auto &world = game.world;
	// ActionManager::canDockAt: a railed transport any VEHICLE or INFANTRY docks with (its dock machine, no delay between
	// its business frames).
	if (world.IsAlive(dock) && world.Has<RailedTransport>(dock) && world.Has<gameplay::Dock>(dock))
	{
		const auto *ref = world.IsAlive(unit) ? world.Get<gameplay::DefinitionRef>(unit) : nullptr;
		if (ref == nullptr || !world.Has<gameplay::MoveOrder>(unit) || !detail::Mobile(game, unit) || world.Has<gameplay::Passenger>(unit) ||
			detail::Locked(game, unit))
			return;
		const content::ObjectDefinition &kind = game.templates.DefinitionAt(ref->index);
		if (!kind.Is("VEHICLE") && !kind.Is("INFANTRY"))
			return;
		Commanded(game, unit);
		detail::TakeOver(game, unit, fromPlayer);
		if (!world.Has<gameplay::Docking>(unit))
			world.Add<gameplay::Docking>(unit);
		gameplay::StartDocking(*world.Get<gameplay::Docking>(unit), dock, 0);
		if (auto *attack = world.Get<gameplay::AttackTarget>(unit))
			*attack = {};
		detail::EndStance(game, unit);
		return;
	}
	if (!CanDockAt(game, unit, dock) || !detail::Mobile(game, unit) || world.Has<gameplay::Passenger>(unit) || detail::Locked(game, unit))
		return;
	Commanded(game, unit);
	detail::TakeOver(game, unit, fromPlayer);
	auto &harvester = *world.Get<gameplay::Harvester>(unit);
	if (fromPlayer)
		harvester.preferredDock = dock;
	gameplay::StartDocking(*world.Get<gameplay::Docking>(unit), dock, world.Has<gameplay::ResourceStore>(dock) ? harvester.storeDelay : harvester.depotDelay);
	if (auto *attack = world.Get<gameplay::AttackTarget>(unit))
		*attack = {};
	detail::EndStance(game, unit);
}

void OrderUnload(GameWorld &game, ecs::Entity transport)
{
	if (auto *carrier = game.world.IsAlive(transport) ? game.world.Get<gameplay::Transport>(transport) : nullptr)
		carrier->state = gameplay::TransportState::Unloading;
}

// ContainModuleInterface::isValidContainerFor (by kind and standing; not room): whether `rider` may go into `container`;
// an Overlord carrying a Battle Bunker asks the bunker (getRedirectedContain).
inline bool MayContain(GameWorld &game, ecs::Entity container, ecs::Entity rider)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (!world.IsAlive(container) || !world.IsAlive(rider))
		return false;
	ecs::Entity asked = container;
	if (const auto *transport = world.Get<gp::Transport>(container); transport != nullptr && transport->definition.redirectsToMount)
		if (const auto *mount = world.Get<gp::Mount>(container); mount != nullptr && world.IsAlive(mount->rider) && world.Has<gp::Transport>(mount->rider))
			asked = mount->rider;
	const auto *containerRef = world.Get<gp::DefinitionRef>(asked);
	const auto *riderRef = world.Get<gp::DefinitionRef>(rider);
	if (containerRef == nullptr || riderRef == nullptr)
		return false;
	// RiderChangeContain::isValidContainerFor: not once scuttled, and only its listed riders.
	if (const RiderChange *change = world.Get<RiderChange>(asked))
	{
		if (change->scuttledTick != 0)
			return false;
		bool listed = false;
		for (std::uint32_t slot = 0; slot < change->count; ++slot)
			listed = listed || change->definitions[slot] == riderRef->index;
		if (!listed)
			return false;
	}
	int relation = 0;
	const auto *riderOwner = world.Get<gp::Owner>(rider);
	const auto *containerOwner = world.Get<gp::Owner>(asked);
	if (riderOwner != nullptr && containerOwner != nullptr)
	{
		const gp::Relationship between = RelationOf(game, rider, asked);
		relation = between == gp::Relationship::Allies ? 0 : between == gp::Relationship::Enemies ? 1 : 2;
	}
	return content::AllowsInside(game.templates.DefinitionAt(containerRef->index), game.templates.DefinitionAt(riderRef->index), relation);
}

// AIUpdateInterface::aiEnter: into `transport` if it may hold the unit (isValidContainerFor); `commanded` from a player or
// a script, else its own AI's.
void OrderBoard(GameWorld &game, ecs::Entity unit, ecs::Entity transport, bool commanded = true)
{
	auto &world = game.world;
	if (unit == transport || !world.IsAlive(unit) || !world.IsAlive(transport) || !world.Has<gameplay::Transport>(transport) || detail::Locked(game, unit) ||
		world.Has<gameplay::Passenger>(unit) || !world.Has<gameplay::MoveOrder>(unit) || !MayContain(game, transport, unit))
		return;
	if (commanded)
		Commanded(game, unit);
	else
		AiCommanded(game, unit);
	InterruptHack(game, unit);
	if (!world.Has<gameplay::Boarding>(unit))
		world.Add<gameplay::Boarding>(unit);
	*world.Get<gameplay::Boarding>(unit) = {transport};
}
}
