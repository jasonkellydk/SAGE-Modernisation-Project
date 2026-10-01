export module games.generalszh.session.composition.containment;
import std;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.containment.resources.drop_settings;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.healing.systems.healing_system;
import engine.gameplay.common.health.systems.pending_damage_system;
import engine.gameplay.common.spatial.systems.spatial_index_system;
import engine.gameplay.rts.aircraft.systems.jet_system;
import engine.gameplay.rts.combat.systems.assist_system;
import engine.gameplay.rts.combat.systems.firing_tracker_system;
import engine.gameplay.rts.combat.systems.impact_system;
import engine.gameplay.rts.combat.systems.missile_flight_system;
import engine.gameplay.rts.combat.systems.neutron_flight_system;
import engine.gameplay.rts.combat.systems.projectile_flight_system;
import engine.gameplay.rts.construction.systems.sale_system;
import engine.gameplay.rts.death.systems.slow_death_system;
import engine.gameplay.rts.docking.systems.dock_system;
import engine.gameplay.rts.lifecycle.systems.removal_system;
import engine.gameplay.rts.mines.systems.minefield_system;
import engine.gameplay.rts.movement.systems.locomotor_damage_system;
import engine.gameplay.rts.slaves.systems.hive_damage_system;
import engine.gameplay.rts.stealth.systems.defector_system;
import engine.gameplay.rts.stealth.systems.stealth_detector_system;
import games.generalszh.gameplay.ai.systems.guard_system;
import games.generalszh.gameplay.ai.systems.tunnel_guard_system;
import games.generalszh.gameplay.combat.systems.battle_bus_system;
import games.generalszh.gameplay.combat.systems.cleanup_hazard_system;
import games.generalszh.gameplay.combat.systems.enemy_near_system;
import games.generalszh.gameplay.combat.systems.weapon_bonus_pulse_system;
import games.generalszh.gameplay.crates.systems.hijacker_system;
import games.generalszh.gameplay.crates.systems.pilot_seek_system;
import games.generalszh.gameplay.creation.systems.ocl_timer_system;
import games.generalszh.gameplay.economy.systems.warehouse_crippling_system;
import games.generalszh.gameplay.hacking.systems.internet_hack_system;
import games.generalszh.gameplay.railroad.systems.railroad_system;
import engine.gameplay.rts.containment.systems.boarding_system;
import engine.gameplay.rts.containment.systems.cargo_transfer_system;
import engine.gameplay.rts.containment.systems.container_classes_system;
import engine.gameplay.rts.containment.systems.drop_homing_system;
import engine.gameplay.rts.containment.systems.garrison_clear_system;
import engine.gameplay.rts.containment.systems.garrison_kill_damage_system;
import engine.gameplay.rts.containment.systems.heal_pad_system;
import engine.gameplay.rts.containment.systems.passed_bonus_system;
import engine.gameplay.rts.containment.systems.passenger_ride_system;
import engine.gameplay.rts.containment.systems.rider_regen_system;
import engine.gameplay.rts.containment.systems.unloading_system;
import games.generalszh.gameplay.containment.systems.assault_transport_system;
import games.generalszh.gameplay.containment.systems.heal_seek_system;
import games.generalszh.gameplay.containment.systems.railed_transport_systems;
import games.generalszh.gameplay.containment.systems.scripted_evacuation_system;
import engine.gameplay.rts.containment.components.cargo_size;
import engine.gameplay.rts.containment.components.drop_homing;
import engine.gameplay.rts.containment.components.garrison;
import engine.gameplay.rts.containment.components.garrison_points;
import engine.gameplay.rts.containment.components.heal_pad;
import engine.gameplay.rts.containment.components.mount;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.components.tunnel;
import games.generalszh.gameplay.containment.components.assault_transport;
import games.generalszh.gameplay.containment.components.heal_seeker;
import games.generalszh.gameplay.containment.components.held_aboard;
import games.generalszh.gameplay.containment.components.initial_payload;
import games.generalszh.gameplay.containment.components.railed_transport;
import games.generalszh.gameplay.containment.components.rider_change;
import games.generalszh.gameplay.containment.components.scripted_evacuation;

// The containment domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The containment domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceContainmentResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::DropSettings>();
	world.EmplaceResource<engine::gameplay::BoardRequests>();
	world.EmplaceResource<engine::gameplay::ExitRequests>();
	world.EmplaceResource<engine::gameplay::DropExits>();
	world.EmplaceResource<engine::gameplay::PlacedExits>();
	world.EmplaceResource<generalszh::gameplay::RailedTransportEvents>();
	world.EmplaceResource<generalszh::gameplay::RailedCaptures>();
	world.EmplaceResource<engine::gameplay::RiderExits>();
	world.EmplaceResource<engine::gameplay::IntentExits>();
	world.EmplaceResource<generalszh::gameplay::AssaultOrders>();
}

inline void RegisterContainmentComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::InitialPayload>();
	world.RegisterComponent<generalszh::gameplay::RiderChange>();
	world.RegisterComponent<generalszh::gameplay::ScriptedEvacuation>();
	world.RegisterComponent<generalszh::gameplay::AssaultTransport>();
	world.RegisterComponent<generalszh::gameplay::RailedTransport>();
	world.RegisterComponent<generalszh::gameplay::RailedHaul>();
	world.RegisterComponent<engine::gameplay::Garrison>();
	world.RegisterComponent<engine::gameplay::Tunnel>();
	world.RegisterComponent<engine::gameplay::HealPad>();
	world.RegisterComponent<engine::gameplay::GarrisonPoints>();
	world.RegisterComponent<engine::gameplay::TransportFirePoints>();
	world.RegisterComponent<engine::gameplay::Transport>();
	world.RegisterComponent<engine::gameplay::ExitIntent>();
	world.RegisterComponent<generalszh::gameplay::HealSeeker>();
	world.RegisterComponent<engine::gameplay::Passenger>();
	world.RegisterComponent<engine::gameplay::Boarding>();
	world.RegisterComponent<engine::gameplay::CargoSize>();
	world.RegisterComponent<engine::gameplay::DropHoming>();
	world.RegisterComponent<generalszh::gameplay::HeldAboard>();
	world.RegisterComponent<engine::gameplay::Mount>();
	world.RegisterComponent<engine::gameplay::Mounted>();
}

// The containment domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterContainmentSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::DropHomingSystem dropHomings;
	registry.Register(dropHomings);
	static engine::gameplay::GarrisonClearSystem garrisonClears;
	registry.Register(garrisonClears);
	static engine::gameplay::PassedBonusSystem passedBonuses;
	registry.Register(passedBonuses);
	static engine::gameplay::GarrisonKillDamageSystem garrisonKillDamage;
	registry.Register(garrisonKillDamage);
	static engine::gameplay::ContainerClassesSystem containerClasses;
	registry.Register(containerClasses);
	static engine::gameplay::BoardingSystem boarding;
	registry.Register(boarding);
	static engine::gameplay::UnloadingSystem unloading;
	registry.Register(unloading);
	static generalszh::gameplay::HealSeekSystem healSeekers;
	registry.Register(healSeekers);
	static generalszh::gameplay::AssaultTransportSystem assaultTransports;
	registry.Register(assaultTransports);
	static generalszh::gameplay::RailedDockerSystem railedDockers;
	registry.Register(railedDockers);
	static generalszh::gameplay::RailedTransportSystem railedTransports;
	registry.Register(railedTransports);
	static generalszh::gameplay::ScriptedEvacuationSystem scriptedEvacuations;
	registry.Register(scriptedEvacuations);
	static engine::gameplay::RiderRegenSystem riderRegen;
	registry.Register(riderRegen);
	static engine::gameplay::HealPadSystem healPads;
	registry.Register(healPads);
	static engine::gameplay::CargoTransferSystem cargo;
	registry.Register(cargo);
	static engine::gameplay::PassengerRideSystem passengerRide;
	registry.Register(passengerRide);
}

// What the containment domain's systems run after (and the few they must precede), within the tick.
inline void OrderContainmentSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	// SmartBombTargetHomingUpdate: a normal-phase update (Simulation), between one tick's physics (PreSimulation) and the next.
	registry.OrderBefore<domain::HijackerSystem, gameplay::DropHomingSystem>();
	registry.OrderBefore<domain::BattleBusSystem, gameplay::DropHomingSystem>();
	registry.OrderBefore<domain::EnemyNearSystem, gameplay::DropHomingSystem>();
	registry.OrderBefore<domain::OclTimerSystem, gameplay::DropHomingSystem>();
	registry.OrderBefore<domain::WeaponBonusPulseSystem, gameplay::DropHomingSystem>();
	registry.OrderBefore<domain::TunnelGuardSystem, gameplay::DropHomingSystem>();
	// Projectiles that clear garrisons: decided between their flight and the tick's impacts.
	registry.OrderBefore<gameplay::ProjectileFlightSystem, gameplay::GarrisonClearSystem>();
	registry.OrderBefore<gameplay::MissileFlightSystem, gameplay::GarrisonClearSystem>();
	// Direct garrison killing after the impacts, before the damage.
	registry.OrderBefore<gameplay::ImpactSystem, gameplay::GarrisonKillDamageSystem>();
	// What containers hold, as target classes, for the step's damage estimates (in the spatial index).
	registry.OrderBefore<gameplay::DefectorSystem, gameplay::ContainerClassesSystem>();
	registry.OrderBefore<domain::RailroadSystem, gameplay::DropHomingSystem>();
	registry.OrderBefore<gameplay::DropHomingSystem, domain::RailedDockerSystem>();
	registry.OrderBefore<domain::WarehouseCripplingSystem, domain::RailedDockerSystem>();
	registry.OrderBefore<gameplay::NeutronFlightSystem, domain::RailedDockerSystem>();
	registry.OrderBefore<domain::RailedDockerSystem, domain::RailedTransportSystem>();
	registry.OrderBefore<gameplay::DropHomingSystem, domain::RailedTransportSystem>();
	registry.OrderBefore<domain::WarehouseCripplingSystem, domain::RailedTransportSystem>();
	registry.OrderBefore<gameplay::NeutronFlightSystem, domain::RailedTransportSystem>();
	registry.OrderBefore<domain::WarehouseCripplingSystem, domain::AssaultTransportSystem>();
	registry.OrderBefore<gameplay::MinefieldSystem, domain::AssaultTransportSystem>();
	registry.OrderBefore<gameplay::JetSystem, domain::AssaultTransportSystem>();
	registry.OrderBefore<gameplay::UnloadingSystem, domain::AssaultTransportSystem>();
	registry.OrderBefore<gameplay::DockSystem, domain::AssaultTransportSystem>();
	registry.OrderBefore<domain::TunnelGuardSystem, domain::AssaultTransportSystem>();
	registry.OrderBefore<domain::InternetHackSystem, domain::ScriptedEvacuationSystem>();
	registry.OrderBefore<domain::InternetHackSystem, domain::HealSeekSystem>();
	registry.OrderBefore<gameplay::HealPadSystem, gameplay::RiderRegenSystem>();
	registry.OrderBefore<domain::HealSeekSystem, gameplay::RiderRegenSystem>();
	registry.OrderBefore<gameplay::LocomotorDamageSystem, gameplay::UnloadingSystem>();
	// Seekers look once the spatial index is up; pads heal alongside the tick's other healing, before the tick's
	// riders move.
	registry.OrderBefore<gameplay::SpatialIndexSystem, domain::HealSeekSystem>();
	registry.OrderBefore<gameplay::HealPadSystem, domain::HealSeekSystem>();
	registry.OrderBefore<gameplay::HealingSystem, domain::HealSeekSystem>();
	registry.OrderBefore<gameplay::SaleSystem, domain::HealSeekSystem>();
	registry.OrderBefore<domain::PilotSeekSystem, domain::HealSeekSystem>();
	registry.OrderBefore<gameplay::MissileFlightSystem, domain::HealSeekSystem>();
	registry.OrderBefore<gameplay::ProjectileFlightSystem, domain::HealSeekSystem>();
	registry.OrderBefore<gameplay::JetSystem, domain::HealSeekSystem>();
	registry.OrderBefore<gameplay::UnloadingSystem, domain::HealSeekSystem>();
	registry.OrderBefore<gameplay::DockSystem, domain::HealSeekSystem>();
	registry.OrderBefore<gameplay::HealingSystem, gameplay::HealPadSystem>();
	registry.OrderBefore<gameplay::HealPadSystem, gameplay::CargoTransferSystem>();
	registry.OrderBefore<gameplay::StealthDetectorSystem, gameplay::HealPadSystem>();
	registry.OrderBefore<gameplay::AssistSystem, gameplay::HealPadSystem>();
	registry.OrderBefore<gameplay::FiringTrackerSystem, gameplay::PassedBonusSystem>();
	registry.OrderBefore<gameplay::HiveDamageSystem, gameplay::GarrisonKillDamageSystem>();
	registry.OrderBefore<gameplay::PendingDamageSystem, gameplay::GarrisonKillDamageSystem>();
	registry.OrderBefore<gameplay::RemovalSystem, gameplay::PassengerRideSystem>();
	registry.OrderBefore<gameplay::PassedBonusSystem, domain::ScriptedEvacuationSystem>();
	registry.OrderBefore<domain::CleanupHazardSystem, domain::ScriptedEvacuationSystem>();
	registry.OrderBefore<domain::GuardSystem, domain::ScriptedEvacuationSystem>();
	// Transports know who is on the way to them before they decide to come down.
	registry.OrderBefore<gameplay::BoardingSystem, gameplay::UnloadingSystem>();
	registry.OrderBefore<gameplay::SlowDeathSystem, gameplay::PassengerRideSystem>();
}
}
