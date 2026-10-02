export module games.generalszh.gameplay.orders.algorithms.command_application;
import engine.gameplay.common.status.components.ai_activity;
import engine.config.binding.values;
import engine.gameplay.rts.slaves.components.spawner;
import games.generalszh.gameplay.containment.algorithms.railed_transports;
import games.generalszh.gameplay.world.resources.music_progress;
import games.generalszh.gameplay.world.resources.chat_inbox;
import games.generalszh.gameplay.orders.resources.hotkey_squads;
import games.generalszh.gameplay.combat_drop.resources.deferred_orders;
import games.generalszh.gameplay.combat_drop.components.combat_drop;
import games.generalszh.gameplay.combat_drop.algorithms.combat_drop_orders;
import games.generalszh.gameplay.beacons.algorithms.beacons;
import games.generalszh.gameplay.mines.components.mine_clearer;
import engine.gameplay.rts.loadout.components.loadout;
import std;
import games.generalszh.gameplay.crates.algorithms.hijacking;
import games.generalszh.gameplay.orders.algorithms.repair_orders;
import games.generalszh.gameplay.ai.resources.retaliation_modes;
import games.generalszh.gameplay.crates.algorithms.car_bombs;
import games.generalszh.gameplay.crates.algorithms.sabotage;
import games.generalszh.gameplay.combat.algorithms.unmanned_vehicles;
import games.generalszh.gameplay.powers.algorithms.particle_cannons;
import games.generalszh.gameplay.powers.components.spectre_gunship;
import games.generalszh.gameplay.production.algorithms.unit_queue;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.commands.game_commands;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.powers.algorithms.special_power_launch;
import games.generalszh.gameplay.upgrades.algorithms.research;
import games.generalszh.gameplay.construction.algorithms.selling;
import games.generalszh.gameplay.construction.algorithms.building;
import games.generalszh.gameplay.construction.algorithms.construction_cancel;
import games.generalszh.gameplay.sciences.algorithms.general_ranks;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.rts.containment.components.transport;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import engine.gameplay.rts.economy.algorithms.overcharge_switch;
import engine.gameplay.common.weapons.components.weapon_slots;
import games.generalszh.gameplay.appearance.components.building_extensions;
import engine.gameplay.common.appearance.algorithms.special_model_states;
import games.generalszh.content.objects.model_conditions;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.orders.algorithms.group_orders;
import games.generalszh.gameplay.orders.algorithms.group_scatter;
import games.generalszh.gameplay.hacking.algorithms.hack_orders;
import games.generalszh.gameplay.ai.algorithms.guards;
import games.generalszh.gameplay.production.algorithms.rally_points;
import games.generalszh.gameplay.containment.algorithms.player_evacuation;
import games.generalszh.gameplay.aircraft.algorithms.airfields;
import games.generalszh.gameplay.crates.systems.pilot_seek_system;
import engine.gameplay.rts.aircraft.components.jet;
import engine.gameplay.rts.aircraft.components.airfield;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.physics.resources.physics_settings;
import engine.gameplay.rts.veterancy.components.experience;
import games.generalszh.gameplay.academy.algorithms.academy_records;

// A player's command off the lockstep bus, applied to the units that player
// owns (anything else in it is ignored: a peer cannot order another's units).
export namespace generalszh::gameplay
{
// ChinookAIUpdate::aiDoCommand: a transport of the player's busy with a combat drop keeps the order for afterwards
// (DeferredOrders: the last one given it); any other unit of the order takes it now, and an order reaching a unit forgets
// what was kept for it (passItThru). False: nobody is left to take it now.
inline bool DeferDropOrders(GameWorld &game, std::uint32_t player, commands::GameCommand &command)
{
	auto *deferred = game.world.FindResource<DeferredOrders>();
	return std::visit(
		[&](auto &order) -> bool {
			using T = std::decay_t<decltype(order)>;
			// A hotkey squad is no unit order (Player::processCreateTeamGameMessage): nothing of it waits for a drop.
			if constexpr (std::is_same_v<T, commands::CreateTeam> || std::is_same_v<T, commands::Cheer> || std::is_same_v<T, commands::CreateFormation> ||
				std::is_same_v<T, commands::SetMineClearingDetail> ||
				// A scatter keeps a dropping transport's own move to its place from the whole group (GroupScatter).
				std::is_same_v<T, commands::Scatter>)
				return true;
			else if constexpr (requires { { order.units } -> std::same_as<std::vector<ecs::Entity> &>; })
			{
				if (deferred == nullptr || order.units.empty())
					return true;
				std::vector<ecs::Entity> now;
				for (const ecs::Entity unit : order.units)
				{
					const bool alive = game.world.IsAlive(unit);
					const auto *owner = alive ? game.world.Get<engine::gameplay::Owner>(unit) : nullptr;
					const CombatDrop *drop = alive ? game.world.Get<CombatDrop>(unit) : nullptr;
					if (owner != nullptr && owner->player == player && drop != nullptr && drop->stage == CombatDropStage::Dropping)
					{
						T one = order;
						one.units = {unit};
						deferred->Keep(unit, player, commands::Encode(one));
						continue;
					}
					deferred->Forget(unit);
					now.push_back(unit);
				}
				order.units = std::move(now);
				return !order.units.empty();
			}
			else
				return true;
		},
		command);
}

namespace enter_detail
{
namespace gp = engine::gameplay;

// ActionManager::canEnterObject's first checks, for a player's order: not itself, the target not dead nor fogged to a
// human player, neither under construction, the target not sold, neither IGNORED_IN_GUI nor the unit a MOB_NEXUS, the
// target not subdued, the unit neither STRUCTURE nor IMMOBILE.
inline bool MayEnterAtAll(GameWorld &game, ecs::Entity unit, ecs::Entity target)
{
	auto &world = game.world;
	const auto *self = world.IsAlive(unit) ? world.Get<gp::DefinitionRef>(unit) : nullptr;
	const auto *into = world.IsAlive(target) ? world.Get<gp::DefinitionRef>(target) : nullptr;
	if (self == nullptr || into == nullptr || unit == target || world.Has<gp::Dying>(target) || ShroudedForAction(game, unit, target))
		return false;
	if (world.Has<gp::UnderConstruction>(unit) || world.Has<gp::UnderConstruction>(target) || world.Has<gp::Sale>(target))
		return false;
	const content::ObjectDefinition &kind = game.templates.DefinitionAt(self->index);
	const content::ObjectDefinition &intoKind = game.templates.DefinitionAt(into->index);
	if (kind.Is("IGNORED_IN_GUI") || kind.Is("MOB_NEXUS") || intoKind.Is("IGNORED_IN_GUI"))
		return false;
	if (const auto *off = world.Get<gp::Disabled>(target); off != nullptr && (off->mask & gp::disabled_type::Subdued) != 0)
		return false;
	return !kind.Is("STRUCTURE") && !kind.Is("IMMOBILE");
}

// canEnterObject for an AIRCRAFT at an FS_AIRFIELD (after MayEnterAtAll): above the ground, of the airfield's own player,
// holding a space there (hasReservedSpace), or one free there for it (shouldReserveDoorWhenQueued and
// hasAvailableSpaceFor: not a PRODUCED_AT_HELIPAD one's).
inline bool MayLandAt(GameWorld &game, ecs::Entity unit, ecs::Entity airfield)
{
	auto &world = game.world;
	const auto *jet = world.Get<gp::Jet>(unit);
	const auto *at = world.Get<gp::Transform>(unit);
	const auto *mine = world.Get<gp::Owner>(unit);
	const auto *theirs = world.Get<gp::Owner>(airfield);
	if (at == nullptr || mine == nullptr || theirs == nullptr || at->position.z - game.ground.At(at->position.XY()) <= Engine::Math::Fixed{})
		return false;
	if (mine->player != theirs->player || !world.Has<gp::Airfield>(airfield))
		return false;
	if (jet != nullptr && jet->airfield == airfield)
		return true;
	return !game.templates.DefinitionAt(world.Get<gp::DefinitionRef>(unit)->index).Is("PRODUCED_AT_HELIPAD") && FreeSpace(game, airfield).has_value();
}

// JetAIUpdate::privateEnter at an airfield: ignored while landing (LANDING_IN_PROGRESS: coming down or taxiing in) or
// parked there (aiDoCommand's isParkedAt); else, canEnterObject letting it, doLandingCommand: it reserves a space there
// (its old one let go) and turns back to land (RETURNING_FOR_LANDING, as its airfield's recall).
inline void OrderJetLanding(GameWorld &game, ecs::Entity unit, ecs::Entity airfield)
{
	auto &world = game.world;
	gp::Jet *jet = world.Get<gp::Jet>(unit);
	if (jet == nullptr || jet->state == gp::JetState::Landing || jet->state == gp::JetState::TaxiToParking || jet->state == gp::JetState::OrientForParking)
		return;
	if (!MayEnterAtAll(game, unit, airfield) || !MayLandAt(game, unit, airfield))
		return;
	if (jet->airfield != airfield)
	{
		const auto space = FreeSpace(game, airfield);
		if (!space)
			return;
		jet->airfield = airfield;
		jet->space = *space;
	}
	Commanded(game, unit);
	jet->order = gp::Jet::Recall;
}

// canEnterObject for a pilot (its VeterancyCrateCollide would like to collide with the vehicle, after MayEnterAtAll),
// then privateEnter (a mobile unit): it goes for the vehicle and climbs in on reaching it (PilotSeekSystem). False: no
// pilot's enter.
inline bool OrderPilotJoin(GameWorld &game, ecs::Entity unit, ecs::Entity vehicle)
{
	auto &world = game.world;
	PilotSeeker *seeker = world.Get<PilotSeeker>(unit);
	const auto *owner = world.Get<gp::Owner>(unit);
	const auto *experience = world.Get<gp::Experience>(unit);
	const auto *physics = world.FindResource<gp::PhysicsSettings>();
	if (seeker == nullptr || owner == nullptr || physics == nullptr || !MayEnterAtAll(game, unit, vehicle))
		return false;
	const std::uint32_t levels = seeker->addsOwnerVeterancy != 0 ? (experience != nullptr ? experience->level : 0u) : 1u;
	if (!PilotMayJoin(*seeker, owner->player, levels, vehicle, world, game.templates, game.ground, physics->SignificantHeight()))
		return false;
	if (!detail::Mobile(game, unit) || world.Has<gp::Passenger>(unit))
		return true;
	OrderMove(game, unit, world.Get<gp::Transform>(vehicle)->position.XY(), true, true, gp::GoalClaim::None); // AIEnterState
	seeker->goal = vehicle;
	return true;
}
}

// aiEnter (CMD_FROM_PLAYER), as canEnterObject goes: a jet lands at its player's airfield, an unmanned vehicle is taken
// over by infantry, a vehicle hijacked or made a car bomb, a building sabotaged, a vehicle joined by a pilot, else a
// transport boarded.
inline void EnterAsOrdered(GameWorld &game, ecs::Entity unit, ecs::Entity target)
{
	namespace gp = engine::gameplay;
	const auto *self = game.world.IsAlive(unit) ? game.world.Get<gp::DefinitionRef>(unit) : nullptr;
	const auto *into = game.world.IsAlive(target) ? game.world.Get<gp::DefinitionRef>(target) : nullptr;
	if (self != nullptr && into != nullptr && game.world.Has<gp::Jet>(unit) && game.templates.DefinitionAt(self->index).Is("AIRCRAFT") &&
		game.templates.DefinitionAt(into->index).Is("FS_AIRFIELD"))
	{
		enter_detail::OrderJetLanding(game, unit, target);
		return;
	}
	if (MayTakeOver(game, unit, target, false))
		OrderTakeOver(game, unit, target, false);
	else if (!OrderHijack(game, unit, target, true, false) && !OrderConvertToCarBomb(game, unit, target, true, false) &&
		!OrderSabotage(game, unit, target, true, false) && !enter_detail::OrderPilotJoin(game, unit, target))
		OrderBoard(game, unit, target, true);
}

void ApplyCommand(GameWorld &game, std::uint32_t player, const commands::GameCommand &given)
{
	commands::GameCommand command = given;
	if (!DeferDropOrders(game, player, command))
		return;
	// RailedTransportAIUpdate::aiDoCommand: a railed transport takes no order of its player's but to set off or to let its
	// riders out.
	const bool railedOrder = std::holds_alternative<commands::ExecuteRailedTransport>(command) || std::holds_alternative<commands::Evacuate>(command);
	// AIUpdateInterface::isAllowedToRespondToAiCommands: an AIUpdate with ForbidPlayerCommands (the Spectre gunship's) takes
	// no CMD_FROM_PLAYER order (aiDoCommand); its own update commands it. A special power's orders (its destination, its
	// use) go to the power's module, not its AI.
	const bool aiCommand = !std::holds_alternative<commands::SpecialPowerDestination>(command) && !std::holds_alternative<commands::UseSpecialPower>(command) &&
		!std::holds_alternative<commands::UseSpecialPowerAtObject>(command) && !std::holds_alternative<commands::SetMineClearingDetail>(command);
	const auto forbids = [&](ecs::Entity unit) {
		if (!aiCommand)
			return false;
		const auto *ref = game.world.Get<engine::gameplay::DefinitionRef>(unit);
		if (ref == nullptr)
			return false;
		for (const content::ModuleEntry &module : game.templates.DefinitionAt(ref->index).modules)
			if (module.block != nullptr && std::string_view(module.type).ends_with("AIUpdateInterface"))
				if (const auto *node = module.block->Find("ForbidPlayerCommands"))
					return engine::config::values::ParseBool(node->Value()).value_or(false);
		return false;
	};
	// AIGroup::groupAttackObject / groupAttackPosition: a spawner whose slaves have no free will sends them after it first
	// (SpawnBehavior::orderSlavesToAttackTarget: aiForceAttackObject; orderSlavesToAttackPosition: aiAttackPosition).
	const auto slavesOf = [&](ecs::Entity unit) {
		std::vector<ecs::Entity> slaves;
		if (const auto *spawner = game.world.Get<engine::gameplay::Spawner>(unit); spawner != nullptr && !spawner->freeWill)
			for (std::uint8_t index = 0; index < spawner->spawnedCount; ++index)
				if (game.world.IsAlive(spawner->spawned[index]))
					slaves.push_back(spawner->spawned[index]);
		return slaves;
	};
	const auto owns = [&](ecs::Entity unit) {
		const auto *owner = game.world.IsAlive(unit) ? game.world.Get<engine::gameplay::Owner>(unit) : nullptr;
		return owner != nullptr && owner->player == player && (railedOrder || !game.world.Has<RailedTransport>(unit)) && !forbids(unit);
	};
	std::visit(
		[&](const auto &order) {
			using T = std::decay_t<decltype(order)>;
			if constexpr (std::is_same_v<T, commands::MoveTo>)
			{
				// The player's selection as one group (AIGroup::groupMoveToPosition, CMD_FROM_PLAYER).
				std::vector<ecs::Entity> group;
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						group.push_back(unit);
				// getLayerForDestination(groupDest): the layer the click is on, from its height.
				GroupMoveToPosition(game, group, order.destination, true, LayerAt(game, order.destination, order.height));
			}
			else if constexpr (std::is_same_v<T, commands::AttackPosition>)
			{
				// GameLogic::onDoForceAttackGround -> groupAttackPosition(no limit, CMD_FROM_PLAYER): each unit, and the
				// riders of one whose passengers may fire. (The primary weapon's temporary lock for a busy group is not
				// ported.) A spawner whose slaves have no free will sends them first.
				const Engine::Math::FixedVector3 spot{order.position.x, order.position.y, game.ground.At(order.position)};
				for (const ecs::Entity unit : order.units)
				{
					if (!owns(unit))
						continue;
					if (const auto *transport = game.world.Get<engine::gameplay::Transport>(unit); transport != nullptr && transport->definition.passengersFire)
						for (const ecs::Entity rider : game.manifest.Aboard(unit))
							OrderAttackPosition(game, rider, spot, 0);
					for (const ecs::Entity slave : slavesOf(unit))
						OrderAttackPosition(game, slave, spot, 0);
					OrderAttackPosition(game, unit, spot, 0);
				}
			}
			// GameLogic::onDoGuardPosition / onDoGuardObject -> groupGuardPosition / groupGuardObject (CMD_FROM_PLAYER): each
			// unit guards the spot (clipped to the map) or the object in the order's mode.
			else if constexpr (std::is_same_v<T, commands::GuardPosition>)
			{
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						GuardPosition(game, unit, order.position, static_cast<GuardMode>(order.mode), true);
			}
			// GameLogic::onDoWeapon / onDoWeaponAtLocation / onDoWeaponAtObject: the slot locked for the attack
			// (setWeaponLockForGroup(LOCKED_TEMPORARILY)); held by any of them, the group attacks (groupAttackPosition: where
			// each stands, or the spot, riders whose container lets them fire included; groupAttackObject), at most so many shots.
			else if constexpr (std::is_same_v<T, commands::FireWeapon>)
			{
				bool any = false;
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
					{
						auto *set = game.world.Get<engine::gameplay::WeaponSlots>(unit);
						auto *armament = game.world.Get<engine::gameplay::Armament>(unit);
						if (set != nullptr && armament != nullptr)
							any = engine::gameplay::LockSlotTemporarily(*set, *armament, order.slot) || any;
						else if (armament != nullptr && armament->weapon != engine::gameplay::WeaponCatalog::None && order.slot == 0)
							any = true;
					}
				if (!any)
					return;
				for (const ecs::Entity unit : order.units)
				{
					if (!owns(unit))
						continue;
					if (order.at == 2)
					{
						for (const ecs::Entity slave : slavesOf(unit))
							OrderAttack(game, slave, order.target, order.maxShots);
						OrderAttack(game, unit, order.target, order.maxShots);
						continue;
					}
					const auto *transform = game.world.Get<engine::gameplay::Transform>(unit);
					if (transform == nullptr)
						continue;
					const Engine::Math::FixedVector2 where = order.at == 1 ? order.position : transform->position.XY();
					const Engine::Math::FixedVector3 spot{where.x, where.y, game.ground.At(where)};
					if (const auto *transport = game.world.Get<engine::gameplay::Transport>(unit); transport != nullptr && transport->definition.passengersFire)
						for (const ecs::Entity rider : game.manifest.Aboard(unit))
							OrderAttackPosition(game, rider, spot, order.maxShots);
					for (const ecs::Entity slave : slavesOf(unit))
						OrderAttackPosition(game, slave, spot, order.maxShots);
					OrderAttackPosition(game, unit, spot, order.maxShots);
				}
			}
			// GameLogic's MSG_DO_ATTACKMOVETO -> groupAttackMoveToPosition (CMD_FROM_PLAYER): each of the sender's units.
			else if constexpr (std::is_same_v<T, commands::AttackMoveTo>)
			{
				// Each to the spot clicked (computePath's getLayerForDestination of it, at its height).
				const std::uint8_t layer = LayerAt(game, order.position, order.height);
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						OrderAttackMove(game, unit, order.position, layer);
			}
			// GameLogic::onEvacuate -> groupEvacuate (CMD_FROM_PLAYER): each of the sender's units.
			else if constexpr (std::is_same_v<T, commands::Evacuate>)
			{
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						GroupEvacuate(game, unit);
			}
			// GameLogic::onSetRallyPoint: only for a factory of the sender's (the source object's owner check).
			else if constexpr (std::is_same_v<T, commands::SetRallyPoint>)
			{
				if (owns(order.factory))
					SetRallyPoint(game, player, order.factory, order.position);
			}
			else if constexpr (std::is_same_v<T, commands::GuardObject>)
			{
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						GuardObject(game, unit, order.target, static_cast<GuardMode>(order.mode), true);
			}
			// GameLogicDispatch MSG_DO_ATTACK_OBJECT -> AIGroup::groupAttackObject (CMD_FROM_PLAYER).
			else if constexpr (std::is_same_v<T, commands::Attack>)
			{
				std::vector<ecs::Entity> group;
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						group.push_back(unit);
				GroupAttackObject(game, group, order.target, 0, engine::gameplay::CommandSource::Player);
			}
			else if constexpr (std::is_same_v<T, commands::Dock>)
			{
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						OrderDock(game, unit, order.dock, true);
			}
			else if constexpr (std::is_same_v<T, commands::SpecialPowerDestination>)
			{
				// setSpecialPowerOverridableDestination: a Particle Cannon (not disabled) drives its beam there.
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
					{
						if (auto *cannon = game.world.Get<ParticleCannon>(unit); cannon != nullptr && detail::Mobile(game, unit)) // isDisabled: none
							DriveCannonBeam(*cannon, {order.position.x, order.position.y, game.ground.At(order.position)}, game.tick);
						// SpectreGunshipUpdate::setSpecialPowerOverridableDestination: not disabled, its reticle goes there.
						if (auto *gunship = game.world.Get<SpectreGunship>(unit); gunship != nullptr && detail::Mobile(game, unit))
							gunship->reticle = {order.position.x, order.position.y, game.ground.At(order.position)};
					}
			}
			else if constexpr (std::is_same_v<T, commands::Enter>)
			{
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						EnterAsOrdered(game, unit, order.target);
			}
			else if constexpr (std::is_same_v<T, commands::GetRepaired>)
			{
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						OrderGetRepaired(game, unit, order.depot, true);
			}
			// GameLogic::onGetHealed -> groupGetHealed -> privateGetHealed: canGetHealedAt, then aiEnter the pad.
			else if constexpr (std::is_same_v<T, commands::GetHealed>)
			{
				for (const ecs::Entity unit : order.units)
					if (owns(unit) && CanGetHealedAt(game, unit, order.target, true))
						EnterAsOrdered(game, unit, order.target);
			}
			// GameLogic::onResumeConstruction -> groupResumeConstruction: each member's aiResumeConstruction (only one of
			// them gets to build it: it becomes its builder).
			else if constexpr (std::is_same_v<T, commands::ResumeConstruction>)
			{
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						OrderResumeConstruction(game, unit, order.target, true);
			}
			// GameLogic::onDoRepair -> groupRepair: each member's aiRepair (only one of them gets to: its heal lock).
			else if constexpr (std::is_same_v<T, commands::Repair>)
			{
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						OrderRepair(game, unit, order.target, true);
			}
			else if constexpr (std::is_same_v<T, commands::Stop>)
			{
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						OrderStop(game, unit, true);
			}
			else if constexpr (std::is_same_v<T, commands::ToggleOvercharge>)
			{
				// OverchargeBehavior::toggle -> enable: on, its rods come out (PowerPlantUpdate::extendRods(TRUE), unless
				// they are already); off, they go in (the extension look draws them in).
				for (const ecs::Entity unit : order.units)
				{
					if (!owns(unit))
						continue;
					auto *overcharge = game.world.Get<engine::gameplay::Overcharge>(unit);
					if (overcharge == nullptr)
						continue;
					const bool on = overcharge->active == 0;
					engine::gameplay::SetOvercharge(*overcharge, game.world.Get<engine::gameplay::EnergySource>(unit), on, game.tick);
					if (auto *rods = game.world.Get<ControlRods>(unit); on && rods != nullptr && rods->state == ExtensionState::Retracted)
						*rods = ControlRods{rods->ticks, game.tick + rods->ticks, ExtensionState::Extending};
				}
			}
			else if constexpr (std::is_same_v<T, commands::SwitchWeapon>)
			{
				// WeaponSet::setWeaponLock: only a slot it has a weapon in, current at once.
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						if (auto *slots = game.world.Get<engine::gameplay::WeaponSlots>(unit); slots != nullptr && order.slot < engine::gameplay::WeaponSlotCount)
							if (auto *armament = game.world.Get<engine::gameplay::Armament>(unit))
								engine::gameplay::LockSlot(*slots, *armament, static_cast<std::uint8_t>(order.slot));
			}
			else if constexpr (std::is_same_v<T, commands::SignalUi>)
				return; // for the scripts (the session signals them)
			else if constexpr (std::is_same_v<T, commands::ResearchUpgrade>)
			{
				if (owns(order.building))
					QueueResearch(game, order.building, order.upgrade);
			}
			else if constexpr (std::is_same_v<T, commands::CancelResearch>)
			{
				if (owns(order.building))
					CancelResearch(game, order.building, order.upgrade);
			}
			else if constexpr (std::is_same_v<T, commands::WorkOn>)
			{
				if (owns(order.builder))
					OrderWork(game, order.builder, order.structure);
			}
			else if constexpr (std::is_same_v<T, commands::BuildStructure>)
			{
				if (owns(order.builder))
					BeginConstruction(game, order.builder, order.structure, order.position, Engine::Math::TurnAngle{order.facing});
			}
			else if constexpr (std::is_same_v<T, commands::Sell>)
			{
				if (owns(order.building))
					BeginSale(game, order.building);
			}
			else if constexpr (std::is_same_v<T, commands::CancelConstruction>)
				CancelConstruction(game, player, order.building);
			else if constexpr (std::is_same_v<T, commands::QueueUnit>)
			{
				if (owns(order.factory))
					QueueUnit(game, player, order.factory, order.unit);
			}
			else if constexpr (std::is_same_v<T, commands::CancelUnit>)
			{
				if (owns(order.factory))
					CancelUnit(game, player, order.factory, order.productionId);
			}
			else if constexpr (std::is_same_v<T, commands::KillAllEnemies>)
			{
				// Everything of each player who counts the sender an enemy (getRelationship(local team) == ENEMIES).
				const auto *relationships = game.world.FindResource<engine::gameplay::Relationships>();
				if (relationships == nullptr)
					return;
				for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
				{
					const auto &members = game.roster.TeamAt(team);
					if (relationships->Enemies(members.owner, player))
						for (const ecs::Entity member : members.members)
							KillNow(game, member);
				}
			}
			else if constexpr (std::is_same_v<T, commands::SelfDestruct>)
				PlayerSelfDestruct(game, player, order.transferToAlly);
			else if constexpr (std::is_same_v<T, commands::CombatDrop>)
			{
				// AIGroup::groupCombatDrop (CMD_FROM_PLAYER): each of the player's units, in order; at an object, its
				// position.
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						OrderCombatDrop(game, unit, order.target, order.position, true);
			}
			else if constexpr (std::is_same_v<T, commands::PlaceBeacon>)
				PlaceBeacon(game, player, order.position);
			else if constexpr (std::is_same_v<T, commands::RemoveBeacon>)
				RemoveBeacons(game, player, order.units);
			// GameLogicDispatch MSG_CREATE_TEAMn -> Player::processCreateTeamGameMessage (the sender's squads).
			else if constexpr (std::is_same_v<T, commands::CreateTeam>)
			{
				if (auto *squads = game.world.FindResource<HotkeySquads>())
					CreateHotkeySquad(*squads, player, order.squad, order.units, [&](ecs::Entity unit) { return game.world.IsAlive(unit); });
			}
			// GameLogicDispatch MSG_SELECT_TEAMn -> Player::processSelectTeamGameMessage: a squad with live (selectable)
			// members counts for the sender's academy (recordControlGroupsUsed); the selection itself is the presentation's.
			else if constexpr (std::is_same_v<T, commands::SelectTeam>)
			{
				if (const auto *squads = game.world.FindResource<HotkeySquads>(); squads != nullptr && order.squad >= 0 && order.squad < HotkeySquadCount)
					if (SquadHasLiveMember(game, squads->Members(player, order.squad)))
						RecordAcademy(game, player, AcademyCount::ControlGroupsUsed);
			}
			// GameLogic::onDoCheer -> AIGroup::groupCheer: each of the sender's selected objects
			// setSpecialModelConditionState(MODELCONDITION_SPECIAL_CHEERING, LOGICFRAMES_PER_SECOND * 3).
			// GameLogicDispatch MSG_CREATE_FORMATION -> AIGroup::groupCreateFormation: the sender's selected group.
			else if constexpr (std::is_same_v<T, commands::CreateFormation>)
			{
				std::vector<ecs::Entity> group;
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						group.push_back(unit);
				GroupCreateFormation(game, group);
			}
			// GameLogicDispatch MSG_DO_SCATTER -> AIGroup::groupScatter (CMD_FROM_PLAYER): the sender's selected group.
			else if constexpr (std::is_same_v<T, commands::Scatter>)
			{
				std::vector<ecs::Entity> group;
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						group.push_back(unit);
				GroupScatter(game, player, group);
			}
			else if constexpr (std::is_same_v<T, commands::Cheer>)
			{
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						engine::gameplay::SetSpecialModelState(game.world, unit, content::ModelConditionBit("SPECIAL_CHEERING"), game.tick, 90);
			}
			// GameLogic::onAddWaypoint -> AIGroup::groupMoveToPosition(addWaypoint, CMD_FROM_PLAYER): the sender's as one group.
			else if constexpr (std::is_same_v<T, commands::AddWaypoint>)
			{
				std::vector<ecs::Entity> group;
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						group.push_back(unit);
				GroupAddWaypoint(game, group, order.destination, true, LayerAt(game, order.destination, order.height));
			}
			// GameLogic::onInternetHack -> AIGroup::groupHackInternet(CMD_FROM_PLAYER): each of the sender's aiHackInternet.
			else if constexpr (std::is_same_v<T, commands::HackInternet>)
			{
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						HackInternet(game, unit, true);
			}
			else if constexpr (std::is_same_v<T, commands::SetBeaconText>)
				SetBeaconText(game, player, order.units, order.text);
			else if constexpr (std::is_same_v<T, commands::ExecuteRailedTransport>)
			{
				// AIGroup::groupExecuteRailedTransport: each of the sender's railed transports.
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						ExecuteRailedTransport(game, unit);
			}
			// GameLogic::onExit: the rider must be the sender's (getControllingPlayer() == msgPlayer); it gets out of the
			// container the sender has selected.
			else if constexpr (std::is_same_v<T, commands::Exit>)
			{
				if (owns(order.rider))
					PlayerExit(game, order.rider, order.container);
			}
			else if constexpr (std::is_same_v<T, commands::Chat>)
			{
				// NETCOMMANDTYPE_CHAT: nothing in the game; the line goes to each machine's host to show (processChat).
				if (auto *inbox = game.world.FindResource<ChatInbox>())
					inbox->lines.push_back({player, order.text, order.slots});
			}
			else if constexpr (std::is_same_v<T, commands::MusicProgress>)
			{
				// The local music's progress (only a single-player session's scripts ask it).
				if (auto *music = game.world.FindResource<MusicProgress>())
				{
					music->track = order.track;
					music->completions = order.completions;
				}
			}
			else if constexpr (std::is_same_v<T, commands::EnableRetaliation>)
			{
				// GameLogic::onEnableRetaliationMode: the sender's own player only.
				if (auto *modes = game.world.FindResource<RetaliationModes>())
					modes->Set(player, order.enabled);
			}
			else if constexpr (std::is_same_v<T, commands::PurchaseScience>)
			{
				// GameLogic::onPurchaseScience -> Player::attemptToPurchaseScience.
				if (const auto science = game.templates.Content().Science(order.science))
					PurchaseScience(game, player, *science);
			}
			else if constexpr (std::is_same_v<T, commands::SetMineClearingDetail>)
			{
				// GameLogic::onSetMineClearingDetail -> AIGroup::setMineClearingDetail(TRUE): each of the sender's on its
				// MINE_CLEARING_DETAIL weapon set (setWeaponSetFlag).
				if (const auto *rules = game.world.FindResource<MineClearingRules>())
					for (const ecs::Entity unit : order.units)
						if (owns(unit))
							if (auto *loadout = game.world.Get<engine::gameplay::Loadout>(unit))
								loadout->weaponFlags |= rules->weaponFlag;
			}
			else if constexpr (std::is_same_v<T, commands::UseSpecialPowerAtObject>)
			{
				if (owns(order.source))
					FireSpecialPowerAtObject(game, order.source, order.power, order.target);
			}
			else if (owns(order.source))
				FireSpecialPower(game, order.source, order.power, order.target, false, order.atLocation, order.options, Engine::Math::TurnAngle{order.angle});
		},
		command);
	// aiDoCommand(CMD_FROM_PLAYER): the units a player's AI order reached last took an order from their player
	// (getLastCommandSource), which ends a temporary stealth grant.
	const auto fromPlayer = [&](ecs::Entity unit) {
		if (owns(unit))
			if (auto *activity = game.world.Get<engine::gameplay::AiActivity>(unit))
				activity->fromPlayer = 1;
	};
	std::visit(
		[&](const auto &order) {
			using T = std::decay_t<decltype(order)>;
			if constexpr (std::is_same_v<T, commands::MoveTo> || std::is_same_v<T, commands::Attack> || std::is_same_v<T, commands::Stop> ||
				std::is_same_v<T, commands::AttackPosition> || std::is_same_v<T, commands::Dock> || std::is_same_v<T, commands::GetRepaired> ||
				std::is_same_v<T, commands::Enter> || std::is_same_v<T, commands::ResumeConstruction> || std::is_same_v<T, commands::Repair> ||
				std::is_same_v<T, commands::GetHealed> || std::is_same_v<T, commands::GuardPosition> || std::is_same_v<T, commands::FireWeapon> ||
				std::is_same_v<T, commands::AttackMoveTo> || std::is_same_v<T, commands::Evacuate> || std::is_same_v<T, commands::GuardObject> ||
				std::is_same_v<T, commands::CombatDrop> || std::is_same_v<T, commands::ExecuteRailedTransport> ||
				std::is_same_v<T, commands::AddWaypoint> || std::is_same_v<T, commands::HackInternet> || std::is_same_v<T, commands::Scatter>)
				for (const ecs::Entity unit : order.units)
					fromPlayer(unit);
			else if constexpr (std::is_same_v<T, commands::BuildStructure> || std::is_same_v<T, commands::WorkOn>)
				fromPlayer(order.builder);
			else if constexpr (std::is_same_v<T, commands::Exit>)
				fromPlayer(order.rider);
		},
		command);
}

// ChinookAIUpdate::update: its drop over (idle again), the order kept for a transport is carried out (and forgotten);
// one whose transport is gone is forgotten.
void ApplyDeferredOrders(GameWorld &game)
{
	auto *deferred = game.world.FindResource<DeferredOrders>();
	if (deferred == nullptr || deferred->list.empty())
		return;
	std::vector<DeferredOrder> due;
	std::erase_if(deferred->list, [&](const DeferredOrder &kept) {
		if (!game.world.IsAlive(kept.unit))
			return true;
		if (const CombatDrop *drop = game.world.Get<CombatDrop>(kept.unit); drop != nullptr && drop->stage == CombatDropStage::Dropping)
			return false;
		due.push_back(kept);
		return true;
	});
	for (const DeferredOrder &kept : due)
		if (const auto order = commands::Decode(kept.order))
			ApplyCommand(game, kept.player, *order);
}
}
