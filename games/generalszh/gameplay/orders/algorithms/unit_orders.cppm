export module games.generalszh.gameplay.orders.algorithms.unit_orders;
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
		*docking = {};
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
		activity->busy = 0;
	}
}

// An order from its own AI (CMD_FROM_AI): the last command source is its AI again.
inline void AiCommanded(GameWorld &game, ecs::Entity unit)
{
	if (auto *activity = game.world.Get<gameplay::AiActivity>(unit))
		activity->commanded = 0;
}

// AIUpdateInterface::aiIdle(CMD_FROM_AI): it stops moving and attacking, guarding and hunting end, and it is no longer
// busy; no command reached it from outside.
inline void AiIdle(GameWorld &game, ecs::Entity unit)
{
	auto &world = game.world;
	if (!world.IsAlive(unit))
		return;
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

// aiMoveToPosition: `commanded` from a player or a script (else CMD_FROM_AI: AiMove).
void OrderMove(GameWorld &game, ecs::Entity unit, FixedVector2 destination, bool fromPlayer = false, bool commanded = true)
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
		*order = gameplay::MoveToPoint(destination);
	if (auto *attack = world.Get<gameplay::AttackTarget>(unit))
		*attack = {};
	detail::EndStance(game, unit);
}

// aiAttackMoveToPosition (CMD_FROM_PLAYER): a move there (ending what it did), as AIAttackMoveToState: taking on what it
// comes across on the way.
void OrderAttackMove(GameWorld &game, ecs::Entity unit, FixedVector2 destination)
{
	auto &world = game.world;
	if (!world.IsAlive(unit) || world.Get<gameplay::MoveOrder>(unit) == nullptr || world.Get<gameplay::Aggression>(unit) == nullptr)
		return;
	OrderMove(game, unit, destination, true);
	if (world.Get<gameplay::MoveOrder>(unit)->mode == gameplay::MoveMode::Idle)
		return; // it could not take the move (locked, carried, immobile)
	if (!world.Has<gameplay::AttackMove>(unit))
		world.Add<gameplay::AttackMove>(unit);
	*world.Get<gameplay::AttackMove>(unit) = gameplay::AttackMove{destination};
}

// A player's or script's attack order (the only ones that come here). A TransportAIUpdate carrier whose passengers may
// fire passes it on to them (TransportAIUpdate::privateAttackObject: CMD_FROM_PLAYER or CMD_FROM_SCRIPT), in the order
// they got in; a portable structure (an Overlord's or Helix's add-on) that is hacked, EMPed, subdued or paralyzed is
// left out.
// AIUpdateInterface::aiAttackPosition: attack the spot on the ground (its turrets too), at most `maxShots` shots (0: no
// limit); `commanded` from a player or a script, else its own AI's.
void OrderAttackPosition(GameWorld &game, ecs::Entity unit, Engine::Math::FixedVector3 position, std::uint32_t maxShots, bool commanded = true)
{
	auto &world = game.world;
	if (!world.IsAlive(unit) || detail::Locked(game, unit))
		return;
	if (commanded)
		Commanded(game, unit);
	else
		AiCommanded(game, unit);
	auto *attack = world.Get<gameplay::AttackTarget>(unit);
	if (attack == nullptr)
		return;
	*attack = gameplay::AttackTarget{.ordered = true, .atPosition = 1, .shotsLeft = maxShots, .position = position};
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
	}
	if (auto *set = world.Get<gameplay::WeaponSlots>(unit))
		for (gameplay::WeaponSlot &slot : set->slots)
			if (slot.weapon != gameplay::WeaponCatalog::None)
			{
				slot.clip = weapons->At(slot.weapon).clipSize;
				slot.readyTick = game.tick;
				slot.reloading = false;
			}
}

void OrderAttack(GameWorld &game, ecs::Entity unit, ecs::Entity target, std::uint32_t maxShots = 0)
{
	auto &world = game.world;
	if (!world.IsAlive(unit) || !world.IsAlive(target) || detail::Locked(game, unit))
		return;
	Commanded(game, unit);
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
				*attack = {target, true};
		}
	}
	if (auto *attack = world.Get<gameplay::AttackTarget>(unit))
	{
		*attack = {target, true};
		attack->shotsLeft = maxShots;
	}
}

void OrderStop(GameWorld &game, ecs::Entity unit, bool fromPlayer = false, bool commanded = true)
{
	auto &world = game.world;
	if (!world.IsAlive(unit) || detail::Locked(game, unit))
		return;
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
