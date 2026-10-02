export module games.generalszh.session.composition.stealth;
import std;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.healing.systems.healing_system;
import engine.gameplay.rts.combat.systems.missile_flight_system;
import engine.gameplay.rts.combat.systems.neutron_flight_system;
import engine.gameplay.rts.combat.systems.projectile_flight_system;
import engine.gameplay.rts.combat.systems.weapon_system;
import engine.gameplay.rts.death.systems.slow_death_system;
import engine.gameplay.rts.death.systems.structure_topple_system;
import engine.gameplay.rts.mines.systems.minefield_system;
import engine.gameplay.rts.parachute.systems.parachute_systems;
import games.generalszh.gameplay.abilities.systems.special_ability_system;
import games.generalszh.gameplay.combat.systems.enemy_near_system;
import engine.gameplay.rts.stealth.resources.detections;
import engine.gameplay.rts.stealth.systems.defector_system;
import engine.gameplay.rts.stealth.systems.stealth_detector_system;
import engine.gameplay.rts.stealth.systems.stealth_system;
import games.generalszh.gameplay.stealth.systems.supply_stealth_grant_system;
import engine.gameplay.rts.stealth.components.grant_stealth;
import engine.gameplay.rts.stealth.components.stealth;
import engine.gameplay.rts.stealth.components.stealth_detector;
import engine.gameplay.rts.stealth.components.stealth_rider;
import engine.gameplay.rts.stealth.components.undetected_defector;
import games.generalszh.gameplay.stealth.components.supply_stealth_grant;

// The stealth domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The stealth domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceStealthResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::DetectionOffers>();
	world.EmplaceResource<engine::gameplay::Detections>();
	world.EmplaceResource<engine::gameplay::DetectorPings>();
	world.EmplaceResource<engine::gameplay::RevealWakes>();
	world.EmplaceResource<engine::gameplay::DetectionWakes>();
	world.EmplaceResource<engine::gameplay::StealthDiscoveries>();
	world.EmplaceResource<engine::gameplay::DisguiseEvents>();
	world.EmplaceResource<engine::gameplay::GrantOffers>();
	world.EmplaceResource<engine::gameplay::StealthGrants>();
	world.EmplaceResource<engine::gameplay::TemporaryStealthGrants>();
}

inline void RegisterStealthComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::SupplyStealthGrant>();
	world.RegisterComponent<engine::gameplay::UndetectedDefector>();
	world.RegisterComponent<engine::gameplay::Stealth>();
	world.RegisterComponent<engine::gameplay::StealthDetector>();
	world.RegisterComponent<engine::gameplay::GrantStealth>();
	world.RegisterComponent<engine::gameplay::StealthRider>();
}

// The stealth domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterStealthSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::StealthSystem stealth;
	registry.Register(stealth);
	static engine::gameplay::StealthRiderSystem stealthRiders;
	registry.Register(stealthRiders);
	static engine::gameplay::WakeIdleEnemiesSystem<engine::gameplay::RevealWakes> wakeOnReveal;
	registry.Register(wakeOnReveal);
	static engine::gameplay::DefectorSystem defectors;
	registry.Register(defectors);
	static generalszh::gameplay::SupplyStealthGrantSystem supplyStealthGrants;
	registry.Register(supplyStealthGrants);
	static engine::gameplay::StealthDetectorSystem stealthDetectors;
	registry.Register(stealthDetectors);
	static engine::gameplay::StealthRevealSystem stealthReveal;
	registry.Register(stealthReveal);
	static engine::gameplay::GrantStealthSystem stealthGrants;
	registry.Register(stealthGrants);
	static engine::gameplay::WakeIdleEnemiesSystem<engine::gameplay::DetectionWakes> wakeOnDetection;
	registry.Register(wakeOnDetection);
}

// What the stealth domain's systems run after (and the few they must precede), within the tick.
inline void OrderStealthSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::FreeFallSystem, gameplay::StealthSystem>();
	registry.OrderBefore<gameplay::StealthSystem, gameplay::DefectorSystem>();
	registry.OrderBefore<gameplay::NeutronFlightSystem, gameplay::StealthRevealSystem>();
	registry.OrderBefore<gameplay::StructureToppleSystem, gameplay::WakeIdleEnemiesSystem<gameplay::DetectionWakes>>();
	registry.OrderBefore<gameplay::SlowDeathSystem, gameplay::WakeIdleEnemiesSystem<gameplay::DetectionWakes>>();
	registry.OrderBefore<domain::EnemyNearSystem, gameplay::WakeIdleEnemiesSystem<gameplay::DetectionWakes>>();
	registry.OrderBefore<domain::SpecialAbilitySystem, gameplay::GrantStealthSystem>();
	registry.OrderBefore<gameplay::MinefieldSystem, gameplay::GrantStealthSystem>();
	// Detectors reveal after the tick's shots, for the next tick's targeting.
	registry.OrderBefore<gameplay::WeaponSystem, gameplay::StealthDetectorSystem>();
	registry.OrderBefore<gameplay::HealingSystem, gameplay::StealthDetectorSystem>();
	registry.OrderBefore<gameplay::WeaponSystem, gameplay::GrantStealthSystem>();
	registry.OrderBefore<gameplay::MissileFlightSystem, gameplay::GrantStealthSystem>();
	registry.OrderBefore<gameplay::ProjectileFlightSystem, gameplay::GrantStealthSystem>();
}
}
