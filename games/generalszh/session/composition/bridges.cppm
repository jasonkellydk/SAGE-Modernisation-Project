export module games.generalszh.session.composition.bridges;
import std;
import games.generalszh.gameplay.bridges.resources.bridge_cues;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.health.systems.health_system;
import engine.gameplay.rts.mines.systems.minefield_system;
import games.generalszh.gameplay.production.systems.drone_repair_system;
import games.generalszh.gameplay.bridges.systems.bridge_damage_system;
import games.generalszh.gameplay.bridges.components.bridge;
import games.generalszh.gameplay.walls.systems.wall_piece_system;

// The bridges domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The bridges domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceBridgesResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<generalszh::gameplay::BridgeEvents>();
	world.EmplaceResource<generalszh::gameplay::BridgeCues>();
	world.EmplaceResource<generalszh::gameplay::WallEvents>();
}

inline void RegisterBridgesComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::Bridge>();
	world.RegisterComponent<generalszh::gameplay::BridgeTower>();
	world.RegisterComponent<generalszh::gameplay::WallPiece>();
}

// The bridges domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterBridgesSystems(ecs::SystemRegistry &registry)
{
	static generalszh::gameplay::BridgeDamageSystem bridgeDamage;
	registry.Register(bridgeDamage);
	static generalszh::gameplay::WallPieceSystem wallPieces;
	registry.Register(wallPieces);
}

// What the bridges domain's systems run after (and the few they must precede), within the tick.
inline void OrderBridgesSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::HealthSystem, domain::BridgeDamageSystem>();
	registry.OrderBefore<gameplay::HealthSystem, domain::WallPieceSystem>();
	registry.OrderBefore<domain::DroneRepairSystem, domain::BridgeDamageSystem>();
	registry.OrderBefore<gameplay::MinefieldSystem, domain::BridgeDamageSystem>();
	registry.OrderBefore<domain::DroneRepairSystem, domain::WallPieceSystem>();
	registry.OrderBefore<gameplay::MinefieldSystem, domain::WallPieceSystem>();
}
}
