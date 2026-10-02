export module games.generalszh.session.composition.radar;
import std;
import engine.gameplay.rts.radar.resources.player_radar;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.radar.systems.radar_coverage_system;
import engine.gameplay.rts.radar.components.radar_provider;

// The radar domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The radar domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceRadarResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::PlayerRadar>();
}

inline void RegisterRadarComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::RadarProvider>();
}

// The radar domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterRadarSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::RadarCoverageSystem radarCoverage;
	registry.Register(radarCoverage);
}
}
