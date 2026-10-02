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
import engine.gameplay.rts.construction.components.builder;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.healing.components.healing;
import engine.gameplay.common.spatial.components.off_map;
import games.generalszh.gameplay.construction.algorithms.building;
import games.generalszh.gameplay.combat_drop.algorithms.combat_drop_orders;
import games.generalszh.gameplay.aircraft.algorithms.airfields;
import engine.gameplay.rts.aircraft.components.jet;
import engine.gameplay.rts.aircraft.components.airfield;
import engine.gameplay.common.spatial.algorithms.find_position;
import engine.gameplay.common.spatial.resources.spatial_index;
import Engine.Core.Math.FixedRandom;

// Sending vehicles to be repaired (AIUpdateInterface::privateGetRepaired: canGetRepairedAt, then aiDock), infantry to
// be healed (privateGetHealed: canGetHealedAt, then aiEnter), and dozers to resume a structure's construction or repair
// it (DozerAIUpdate / WorkerAIUpdate::privateResumeConstruction and privateRepair).
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

// ActionManager::canGetHealedAt: an ally's HEAL_PAD, not dead, taking a hurt INFANTRY; neither being built, the pad not
// being sold, and (a player's order) the pad not hidden from the player.
inline bool CanGetHealedAt(const GameWorld &game, ecs::Entity unit, ecs::Entity pad, bool fromPlayer)
{
	namespace gp = engine::gameplay;
	const auto &world = game.world;
	if (!world.IsAlive(unit) || !world.IsAlive(pad))
		return false;
	if (RelationOf(game, unit, pad) != gp::Relationship::Allies)
		return false;
	if (ai_detail::EffectivelyDead(game, pad))
		return false;
	if (world.Has<gp::UnderConstruction>(unit) || world.Has<gp::UnderConstruction>(pad) || world.Has<gp::Sale>(pad))
		return false;
	const auto *self = world.Get<gp::DefinitionRef>(unit);
	const auto *padRef = world.Get<gp::DefinitionRef>(pad);
	if (self == nullptr || padRef == nullptr || !game.templates.DefinitionAt(self->index).Is("INFANTRY") ||
		!game.templates.DefinitionAt(padRef->index).Is("HEAL_PAD"))
		return false;
	if (fromPlayer && ShroudedForAction(game, unit, pad))
		return false;
	const auto *health = world.Get<gp::Health>(unit);
	return health == nullptr || health->current != health->maximum;
}

// ActionManager::canResumeConstructionOf: a DOZER of the structure's own player (the fork's fix), alive, the structure
// still under construction, its builder (getBuilderID: the last one set to build it) not alive at work building it, and
// (a player's order) the structure not hidden from the player.
inline bool CanResumeConstruction(const GameWorld &game, ecs::Entity dozer, ecs::Entity structure, bool fromPlayer)
{
	namespace gp = engine::gameplay;
	const auto &world = game.world;
	if (!world.IsAlive(dozer) || !world.IsAlive(structure))
		return false;
	const auto *ref = world.Get<gp::DefinitionRef>(dozer);
	const auto *mine = world.Get<gp::Owner>(dozer);
	const auto *theirs = world.Get<gp::Owner>(structure);
	if (ref == nullptr || mine == nullptr || theirs == nullptr || !game.templates.DefinitionAt(ref->index).Is("DOZER"))
		return false;
	if (mine->player != theirs->player)
		return false;
	const auto *building = world.Get<gp::UnderConstruction>(structure);
	if (building == nullptr || ai_detail::EffectivelyDead(game, dozer))
		return false;
	if (const ecs::Entity builder = building->builder; world.IsAlive(builder) && !ai_detail::EffectivelyDead(game, builder))
		if (const auto *task = world.Get<gp::Builder>(builder); task != nullptr && task->repair == 0 && task->target == structure)
			return false;
	return !(fromPlayer && ShroudedForAction(game, dozer, structure));
}

// DozerAIUpdate / WorkerAIUpdate::privateResumeConstruction: if it may, newTask(DOZER_TASK_BUILD): it becomes the
// structure's builder (setBuilder) and goes to build it. Anything without a dozer's or worker's AI does nothing.
inline bool OrderResumeConstruction(GameWorld &game, ecs::Entity dozer, ecs::Entity structure, bool fromPlayer)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const auto *ref = world.IsAlive(dozer) ? world.Get<gp::DefinitionRef>(dozer) : nullptr;
	if (ref == nullptr || !building_detail::IsBuilder(game.templates.DefinitionAt(ref->index)) || world.Get<gp::Transform>(structure) == nullptr ||
		!CanResumeConstruction(game, dozer, structure, fromPlayer))
		return false;
	world.Get<gp::UnderConstruction>(structure)->builder = dozer;
	SendToWork(game, dozer, structure, false, {});
	return true;
}

// DozerAIUpdate / WorkerAIUpdate::privateRepair: not the structure it is repairing already (canAcceptNewRepair); if
// canRepairObject lets it (MayRepair, the builder not inside anything, and, a player's order, the structure not hidden
// from the player) and nobody else is its sole healer now (getSoleHealingBenefactor: another's heal lock), newTask
// (DOZER_TASK_REPAIR). Anything without a dozer's or worker's AI does nothing.
inline bool OrderRepair(GameWorld &game, ecs::Entity dozer, ecs::Entity structure, bool fromPlayer)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const auto *ref = world.IsAlive(dozer) ? world.Get<gp::DefinitionRef>(dozer) : nullptr;
	if (ref == nullptr || !building_detail::IsBuilder(game.templates.DefinitionAt(ref->index)))
		return false;
	if (const auto *task = world.Get<gp::Builder>(dozer); task != nullptr && task->repair != 0 && task->target == structure)
		return false;
	if (!MayRepair(game, dozer, structure) || (fromPlayer && ShroudedForAction(game, dozer, structure)))
		return false;
	if (const auto *lock = world.Get<gp::HealLock>(structure); lock != nullptr && game.tick <= lock->until && lock->healer != dozer)
		return false;
	SendToWork(game, dozer, structure, true, RepairShare(game, game.templates.DefinitionAt(ref->index)));
	return true;
}

// JetAIUpdate::doLandingCommand at `airfield`: a helicopter (PRODUCED_AT_HELIPAD) picks its landing spot by it
// (findPositionAround the airfield's position, a random start angle, out to ten times its bounding circle; none: the
// airfield's position), a jet reserves a space there (its own, or a free one: none, nothing happens); either takes the
// airfield as its producer and turns back to land (RETURNING_FOR_LANDING: Jet::Recall).
inline void DoLandingCommand(GameWorld &game, ecs::Entity unit, ecs::Entity airfield)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	auto &world = game.world;
	gp::Jet *jet = world.Get<gp::Jet>(unit);
	const auto *at = world.Get<gp::Transform>(airfield);
	if (jet == nullptr || at == nullptr || !world.Has<gp::Airfield>(airfield))
		return;
	if (jet->helicopter != 0)
	{
		const Engine::Math::TurnAngle start{static_cast<std::uint32_t>(Engine::Math::UniformInt(game.random, 0, 0xFFFFFFFFll))};
		const gp::SpatialEntry *self = world.Resource<gp::SpatialIndex>().Find(airfield);
		const Fixed radius = self != nullptr ? self->radius : Fixed{};
		const auto spot = gp::FindPositionAround(at->position.XY(), Fixed{}, radius * Fixed::FromInt(10), start, SpotLegal(game)).value_or(at->position.XY());
		jet->landingSpot = {spot.x, spot.y, game.ground.At(spot)};
		jet->airfield = airfield;
		if (jet->state == gp::JetState::HeliReturning)
			jet->goal = spot; // RETURNING_FOR_LANDING afresh
		jet->order = gp::Jet::Recall;
		return;
	}
	if (jet->airfield != airfield || jet->space == gp::Jet::NoSpace)
	{
		const auto space = FreeSpace(game, airfield);
		if (!space)
			return;
		jet->airfield = airfield;
		jet->space = *space;
	}
	jet->order = gp::Jet::Recall;
}

// privateGetRepaired: if it may, it docks with the pad (aiDock: its action delay none, not being a supply truck at
// a supply dock). An aircraft's repairs at an airfield are its parking place's (JetAIUpdate), not a dock's.
inline void OrderGetRepaired(GameWorld &game, ecs::Entity unit, ecs::Entity depot, bool fromPlayer)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	// JetAIUpdate::privateGetRepaired: ignored while landing (LANDING_IN_PROGRESS); else, canGetRepairedAt letting it, it
	// lands at the airfield (doLandingCommand).
	if (const gp::Jet *jet = world.Get<gp::Jet>(unit))
	{
		const bool landing = jet->state == gp::JetState::Landing || jet->state == gp::JetState::TaxiToParking || jet->state == gp::JetState::OrientForParking ||
			jet->state == gp::JetState::HeliLanding;
		if (!landing && CanGetRepairedAt(game, unit, depot, fromPlayer))
		{
			Commanded(game, unit);
			DoLandingCommand(game, unit, depot);
		}
		return;
	}
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
