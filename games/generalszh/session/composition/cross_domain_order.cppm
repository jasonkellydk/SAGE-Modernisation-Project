export module games.generalszh.session.composition.cross_domain_order;
import std;

import engine.ecs.system.system;
import engine.gameplay.rts.construction.systems.construction_system;
import engine.gameplay.rts.aircraft.systems.jet_system;
import engine.gameplay.rts.containment.systems.unloading_system;
import engine.gameplay.rts.containment.systems.contained_definitions_system;
import engine.gameplay.rts.containment.systems.boarding_system;
import engine.gameplay.rts.combat.systems.attack_move_system;
import engine.gameplay.rts.combat.systems.targeting_system;
import engine.gameplay.rts.delivery.systems.delivery_system;
import engine.gameplay.rts.docking.systems.dock_system;
import engine.gameplay.rts.movement.systems.movement_system;
import games.generalszh.gameplay.combat_drop.systems.combat_drop_systems;
import games.generalszh.gameplay.containment.systems.assault_transport_system;
import games.generalszh.gameplay.containment.systems.railed_transport_systems;
import games.generalszh.gameplay.containment.systems.heal_seek_system;
import games.generalszh.gameplay.ai.systems.mob_member_system;
import games.generalszh.gameplay.ai.systems.exact_path_follow_system;
import games.generalszh.gameplay.crates.systems.pilot_seek_system;
import games.generalszh.gameplay.abilities.systems.special_ability_system;
import games.generalszh.gameplay.combat.systems.deploy_system;
import games.generalszh.gameplay.walls.systems.wall_piece_system;
import games.generalszh.gameplay.economy.systems.warehouse_crippling_system;
import games.generalszh.gameplay.orders.systems.formation_speed_system;

// Orderings between systems of different domains that touch the same data, set after every domain's own (the
// scheduler refuses a conflict left unordered).
export namespace generalszh::session::composition
{
inline void OrderCrossDomainSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	// A formation member's group speed is kept or dropped on the tick's final orders: after every system that gives
	// units move orders, before movement carries them out.
	registry.OrderBefore<gameplay::JetSystem, domain::FormationSpeedSystem>();
	registry.OrderBefore<gameplay::UnloadingSystem, domain::FormationSpeedSystem>();
	registry.OrderBefore<gameplay::AttackMoveSystem, domain::FormationSpeedSystem>();
	registry.OrderBefore<gameplay::BoardingSystem, domain::FormationSpeedSystem>();
	registry.OrderBefore<gameplay::DeliverySystem, domain::FormationSpeedSystem>();
	registry.OrderBefore<gameplay::TargetingSystem, domain::FormationSpeedSystem>();
	registry.OrderBefore<domain::DeploySystem, domain::FormationSpeedSystem>();
	// An exact team follow sets its next waypoint after movement, once every other order of the tick is given.
	registry.OrderBefore<gameplay::ConstructionSystem, domain::ExactPathFollowSystem>();
	registry.OrderBefore<domain::AssaultTransportSystem, domain::ExactPathFollowSystem>();
	registry.OrderBefore<domain::MobMemberSystem, domain::ExactPathFollowSystem>();
	registry.OrderBefore<domain::PilotSeekSystem, domain::ExactPathFollowSystem>();
	registry.OrderBefore<domain::RailedTransportSystem, domain::ExactPathFollowSystem>();
	registry.OrderBefore<domain::SpecialAbilitySystem, domain::ExactPathFollowSystem>();
	registry.OrderBefore<domain::HealSeekSystem, domain::ExactPathFollowSystem>();
	// Riders dropped out of a combat drop leave its contained definitions the same tick.
	registry.OrderBefore<domain::CombatDropSystem, gameplay::ContainedDefinitionsSystem>();
	// A wall piece reads its body's health after the tick's crippling changed it.
	registry.OrderBefore<domain::WarehouseCripplingSystem, domain::WallPieceSystem>();
}
}
