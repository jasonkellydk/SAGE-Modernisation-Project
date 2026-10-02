export module games.generalszh.session.composition.fire;
import std;
import engine.gameplay.common.fire.resources.fire_settings;
import engine.gameplay.common.fire.resources.ignitions;
import games.generalszh.content.fire.fire_content;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.physics.systems.physics_system;
import engine.gameplay.rts.combat.systems.impact_system;
import engine.gameplay.rts.combat.systems.missile_flight_system;
import engine.gameplay.rts.combat.systems.projectile_flight_system;
import engine.gameplay.rts.combat.systems.weapon_system;
import engine.gameplay.rts.movement.systems.movement_system;
import engine.gameplay.common.fire.systems.fire_spread_system;
import engine.gameplay.common.fire.systems.flammability_system;
import engine.gameplay.common.fire.components.fire_spread;
import engine.gameplay.common.fire.components.flammable;

// The fire domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The fire domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceFireResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::FireSettings>(content::ZeroHourFireSettings());
	world.EmplaceResource<engine::gameplay::BurnDamage>();
	world.EmplaceResource<engine::gameplay::IgnitionOffers>();
	world.EmplaceResource<engine::gameplay::Ignitions>();
	world.EmplaceResource<engine::gameplay::SpreadTries>();
}

inline void RegisterFireComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::Flammable>();
	world.RegisterComponent<engine::gameplay::FireSpread>();
}

// The fire domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterFireSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::FlammabilitySystem flammability;
	registry.Register(flammability);
	static engine::gameplay::FireSpreadSystem fireSpread;
	registry.Register(fireSpread);
}

// What the fire domain's systems run after (and the few they must precede), within the tick.
inline void OrderFireSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	registry.OrderBefore<gameplay::MissileFlightSystem, gameplay::FireSpreadSystem>();
	registry.OrderBefore<gameplay::ProjectileFlightSystem, gameplay::FireSpreadSystem>();
	// Fire reads this tick's impacts and adds its burning before health applies them.
	registry.OrderBefore<gameplay::ImpactSystem, gameplay::FlammabilitySystem>();
	registry.OrderBefore<gameplay::FallingDamageSystem, gameplay::FlammabilitySystem>();
	// Fire spreads from where things are after this tick's moves.
	registry.OrderBefore<gameplay::MovementSystem, gameplay::FireSpreadSystem>();
	registry.OrderBefore<gameplay::WeaponSystem, gameplay::FireSpreadSystem>();
}
}
