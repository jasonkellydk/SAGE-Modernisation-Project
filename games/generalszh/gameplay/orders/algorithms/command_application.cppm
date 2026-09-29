export module games.generalszh.gameplay.orders.algorithms.command_application;
import std;
import games.generalszh.gameplay.crates.algorithms.hijacking;
import games.generalszh.gameplay.orders.algorithms.repair_orders;
import games.generalszh.gameplay.ai.resources.retaliation_modes;
import games.generalszh.gameplay.crates.algorithms.car_bombs;
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
import games.generalszh.gameplay.sciences.algorithms.general_ranks;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.rts.containment.components.transport;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import engine.gameplay.rts.economy.algorithms.overcharge_switch;
import engine.gameplay.common.weapons.components.weapon_slots;
import games.generalszh.gameplay.appearance.components.building_extensions;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.orders.algorithms.group_orders;
import games.generalszh.gameplay.ai.algorithms.guards;
import games.generalszh.gameplay.production.algorithms.rally_points;
import games.generalszh.gameplay.containment.algorithms.player_evacuation;

// A player's command off the lockstep bus, applied to the units that player
// owns (anything else in it is ignored: a peer cannot order another's units).
export namespace generalszh::gameplay
{
void ApplyCommand(GameWorld &game, std::uint32_t player, const commands::GameCommand &command)
{
	const auto owns = [&](ecs::Entity unit) {
		const auto *owner = game.world.IsAlive(unit) ? game.world.Get<engine::gameplay::Owner>(unit) : nullptr;
		return owner != nullptr && owner->player == player;
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
				GroupMoveToPosition(game, group, order.destination, true);
			}
			else if constexpr (std::is_same_v<T, commands::AttackPosition>)
			{
				// GameLogic::onDoForceAttackGround -> groupAttackPosition(no limit, CMD_FROM_PLAYER): each unit, and the
				// riders of one whose passengers may fire. (The primary weapon's temporary lock for a busy group is not
				// ported; slaves are not ordered.)
				const Engine::Math::FixedVector3 spot{order.position.x, order.position.y, game.ground.At(order.position)};
				for (const ecs::Entity unit : order.units)
				{
					if (!owns(unit))
						continue;
					if (const auto *transport = game.world.Get<engine::gameplay::Transport>(unit); transport != nullptr && transport->definition.passengersFire)
						for (const ecs::Entity rider : game.manifest.Aboard(unit))
							OrderAttackPosition(game, rider, spot, 0);
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
					OrderAttackPosition(game, unit, spot, order.maxShots);
				}
			}
			// GameLogic's MSG_DO_ATTACKMOVETO -> groupAttackMoveToPosition (CMD_FROM_PLAYER): each of the sender's units.
			else if constexpr (std::is_same_v<T, commands::AttackMoveTo>)
			{
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						OrderAttackMove(game, unit, order.position);
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
			else if constexpr (std::is_same_v<T, commands::Attack>)
			{
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						OrderAttack(game, unit, order.target);
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
				// aiEnter (CMD_FROM_PLAYER), as canEnterObject goes: an unmanned vehicle taken over by infantry, a vehicle
				// made a car bomb, else a transport boarded.
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
					{
						if (MayTakeOver(game, unit, order.target, false))
							OrderTakeOver(game, unit, order.target, false);
						else if (!OrderHijack(game, unit, order.target, true, false) && !OrderConvertToCarBomb(game, unit, order.target, true, false))
							OrderBoard(game, unit, order.target, true);
					}
			}
			else if constexpr (std::is_same_v<T, commands::GetRepaired>)
			{
				for (const ecs::Entity unit : order.units)
					if (owns(unit))
						OrderGetRepaired(game, unit, order.depot, true);
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
			else if constexpr (std::is_same_v<T, commands::UseSpecialPowerAtObject>)
			{
				if (owns(order.source))
					FireSpecialPowerAtObject(game, order.source, order.power, order.target);
			}
			else if (owns(order.source))
				FireSpecialPower(game, order.source, order.power, order.target, false, order.atLocation, order.options);
		},
		command);
}
}
