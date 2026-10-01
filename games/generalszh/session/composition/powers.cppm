export module games.generalszh.session.composition.powers;
import std;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.health.systems.pending_damage_system;
import engine.gameplay.rts.aircraft.systems.jet_system;
import engine.gameplay.rts.combat.systems.neutron_flight_system;
import engine.gameplay.rts.combat.systems.projectile_flight_system;
import engine.gameplay.rts.construction.systems.sale_system;
import engine.gameplay.rts.mines.systems.minefield_system;
import games.generalszh.gameplay.upgrades.systems.upgrade_effect_system;
import engine.gameplay.rts.powers.systems.special_power_pause_system;
import games.generalszh.gameplay.powers.systems.launcher_door_system;
import games.generalszh.gameplay.powers.systems.particle_cannon_system;
import games.generalszh.gameplay.powers.systems.spectre_gunship_system;
import games.generalszh.gameplay.powers.systems.spy_vision_system;
import engine.gameplay.rts.powers.components.special_power_timers;
import games.generalszh.gameplay.powers.components.launcher_door;
import games.generalszh.gameplay.powers.components.leaflet_drop;
import games.generalszh.gameplay.powers.components.particle_cannon;
import games.generalszh.gameplay.powers.components.spectre_gunship;
import games.generalszh.gameplay.powers.components.spy_vision;

// The powers domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The powers domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplacePowersResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<generalszh::gameplay::GunshipEvents>();
	world.EmplaceResource<generalszh::gameplay::ParticleCannonEvents>();
	world.EmplaceResource<generalszh::gameplay::LauncherDoorEffects>();
}

inline void RegisterPowersComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::SpecialPowerTimers>();
	world.RegisterComponent<generalszh::gameplay::SpyVision>();
	world.RegisterComponent<generalszh::gameplay::LeafletDrop>();
	world.RegisterComponent<generalszh::gameplay::LauncherDoor>();
	world.RegisterComponent<generalszh::gameplay::ParticleCannon>();
	world.RegisterComponent<generalszh::gameplay::SpectreGunship>();
}

// The powers domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterPowersSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::SpecialPowerPauseSystem powerPauses;
	registry.Register(powerPauses);
	static generalszh::gameplay::SpyVisionSystem spyVisions;
	registry.Register(spyVisions);
	static generalszh::gameplay::LauncherDoorSystem launcherDoors;
	registry.Register(launcherDoors);
	static generalszh::gameplay::ParticleCannonSystem particleCannons;
	registry.Register(particleCannons);
	static generalszh::gameplay::SpectreGunshipSystem spectreGunships;
	registry.Register(spectreGunships);
}

// What the powers domain's systems run after (and the few they must precede), within the tick.
inline void OrderPowersSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::NeutronFlightSystem, domain::SpectreGunshipSystem>();
	registry.OrderBefore<gameplay::SaleSystem, domain::ParticleCannonSystem>();
	registry.OrderBefore<gameplay::PendingDamageSystem, domain::ParticleCannonSystem>();
	registry.OrderBefore<gameplay::JetSystem, domain::ParticleCannonSystem>();
	registry.OrderBefore<gameplay::MinefieldSystem, domain::LauncherDoorSystem>();
	registry.OrderBefore<gameplay::ProjectileFlightSystem, domain::LauncherDoorSystem>();
	registry.OrderBefore<gameplay::JetSystem, domain::LauncherDoorSystem>();
	registry.OrderBefore<domain::UpgradeEffectSystem, domain::SpyVisionSystem>();
}
}
