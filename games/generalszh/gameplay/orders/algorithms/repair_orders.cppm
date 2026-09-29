export module games.generalszh.gameplay.orders.algorithms.repair_orders;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import games.generalszh.gameplay.powers.algorithms.special_power_launch;
import games.generalszh.gameplay.ai.algorithms.ai_players;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.resources.ground_height;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.docking.components.dock;
import engine.gameplay.rts.docking.components.docking;
import engine.gameplay.rts.docking.components.repair_dock;

// Sending vehicles to be repaired (AIUpdateInterface::privateGetRepaired: canGetRepairedAt, then aiDock).
export namespace generalszh::gameplay
{
// ActionManager::canGetRepairedAt: an ally's repair pad (an airborne aircraft: an ally's airfield) taking a damaged,
// living, mobile vehicle; neither being built, the pad not being sold, and (a player's order) the pad not hidden
// from the player.
inline bool CanGetRepairedAt(const GameWorld &game, ecs::Entity unit, ecs::Entity depot, bool fromPlayer)
{
	namespace gp = engine::gameplay;
	const auto &world = game.world;
	if (!world.IsAlive(unit) || !world.IsAlive(depot) || unit == depot)
		return false;
	if (RelationOf(game, unit, depot) != gp::Relationship::Allies)
		return false;
	if (ai_detail::EffectivelyDead(game, unit) || !detail::Mobile(game, unit))
		return false;
	if (world.Has<gp::UnderConstruction>(unit) || world.Has<gp::UnderConstruction>(depot) || world.Has<gp::Sale>(depot))
		return false;
	const auto *self = world.Get<gp::DefinitionRef>(unit);
	const auto *pad = world.Get<gp::DefinitionRef>(depot);
	if (self == nullptr || pad == nullptr)
		return false;
	const content::ObjectDefinition &kind = game.templates.DefinitionAt(self->index);
	const content::ObjectDefinition &padKind = game.templates.DefinitionAt(pad->index);
	if (!kind.Is("VEHICLE"))
		return false;
	if (kind.Is("AIRCRAFT"))
	{
		const auto *at = world.Get<gp::Transform>(unit);
		if (at == nullptr || at->position.z - game.ground.At(at->position.XY()) <= Engine::Math::Fixed{} || !padKind.Is("FS_AIRFIELD"))
			return false;
	}
	else if (!padKind.Is("REPAIR_PAD"))
		return false;
	const auto *health = world.Get<gp::Health>(unit);
	if (health == nullptr || health->current == health->maximum)
		return false;
	if (fromPlayer && ShroudedForAction(game, unit, depot))
		return false;
	return true;
}

// privateGetRepaired: if it may, it docks with the pad (aiDock: its action delay none, not being a supply truck at
// a supply dock). An aircraft's repairs at an airfield are its parking place's (JetAIUpdate), not a dock's.
inline void OrderGetRepaired(GameWorld &game, ecs::Entity unit, ecs::Entity depot, bool fromPlayer)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (!CanGetRepairedAt(game, unit, depot, fromPlayer) || !world.Has<gp::RepairDock>(depot) || !world.Has<gp::Dock>(depot))
		return;
	auto *docking = world.Get<gp::Docking>(unit);
	if (docking == nullptr || world.Has<gp::Passenger>(unit) || detail::Locked(game, unit))
		return;
	Commanded(game, unit);
	detail::TakeOver(game, unit, fromPlayer);
	gp::StartDocking(*world.Get<gp::Docking>(unit), depot, 0);
	if (auto *attack = world.Get<gp::AttackTarget>(unit))
		*attack = {};
	detail::EndStance(game, unit);
}
}
