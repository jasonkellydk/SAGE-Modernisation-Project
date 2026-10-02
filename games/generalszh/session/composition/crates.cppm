export module games.generalszh.session.composition.crates;
import std;
import games.generalszh.gameplay.crates.resources.crates;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.health.systems.health_system;
import engine.gameplay.common.spatial.systems.spatial_index_system;
import engine.gameplay.rts.aircraft.systems.jet_system;
import engine.gameplay.rts.collision.systems.collision_systems;
import engine.gameplay.rts.combat.systems.missile_flight_system;
import engine.gameplay.rts.combat.systems.neutron_flight_system;
import engine.gameplay.rts.combat.systems.projectile_flight_system;
import engine.gameplay.rts.containment.systems.unloading_system;
import engine.gameplay.rts.docking.systems.dock_system;
import engine.gameplay.rts.mines.systems.minefield_system;
import games.generalszh.gameplay.abilities.systems.sticky_bomb_system;
import games.generalszh.gameplay.crates.systems.crate_touch_system;
import games.generalszh.gameplay.crates.systems.hijacker_system;
import games.generalszh.gameplay.crates.systems.pilot_seek_system;
import games.generalszh.gameplay.crates.components.crate;
import games.generalszh.gameplay.crates.components.hijacker;
import games.generalszh.gameplay.crates.components.pilot_seeker;

// The crates domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The crates domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceCratesResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<generalszh::gameplay::HijackerEvents>();
	world.EmplaceResource<generalszh::gameplay::CrateTouches>();
	world.EmplaceResource<generalszh::gameplay::CratePickups>();
}

inline void RegisterCratesComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::Hijacker>();
	world.RegisterComponent<generalszh::gameplay::PilotSeeker>();
	world.RegisterComponent<generalszh::gameplay::Crate>();
}

// The crates domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterCratesSystems(ecs::SystemRegistry &registry)
{
	static generalszh::gameplay::PilotSeekSystem pilotSeekers;
	registry.Register(pilotSeekers);
	static generalszh::gameplay::HijackerSystem hijackers;
	registry.Register(hijackers);
	static generalszh::gameplay::CrateTouchSystem crateTouches;
	registry.Register(crateTouches);
}

// What the crates domain's systems run after (and the few they must precede), within the tick.
inline void OrderCratesSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<domain::StickyBombSystem, domain::HijackerSystem>();
	registry.OrderBefore<gameplay::NeutronFlightSystem, domain::HijackerSystem>();
	// Pilots look for vehicles in this tick's world and bring their levels before the veterancy pass.
	registry.OrderBefore<gameplay::SpatialIndexSystem, domain::PilotSeekSystem>();
	registry.OrderBefore<gameplay::HealthSystem, domain::PilotSeekSystem>();
	registry.OrderBefore<gameplay::MissileFlightSystem, domain::PilotSeekSystem>();
	registry.OrderBefore<gameplay::ProjectileFlightSystem, domain::PilotSeekSystem>();
	registry.OrderBefore<gameplay::JetSystem, domain::PilotSeekSystem>();
	registry.OrderBefore<gameplay::UnloadingSystem, domain::PilotSeekSystem>();
	registry.OrderBefore<gameplay::DockSystem, domain::PilotSeekSystem>();
	registry.OrderBefore<gameplay::MinefieldSystem, domain::PilotSeekSystem>();
	// Crates are run into where the tick's movers ended up.
	registry.OrderBefore<gameplay::ColliderIndexSystem, domain::CrateTouchSystem>();
}
}
