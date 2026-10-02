export module games.generalszh.session.composition.effects;
import std;
import games.generalszh.gameplay.effects.resources.effect_cues;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.death.systems.crash_collision_system;
import engine.gameplay.rts.death.systems.slow_death_system;
import engine.gameplay.rts.loadout.systems.loadout_system;
import games.generalszh.gameplay.effects.systems.bone_fx_system;
import games.generalszh.gameplay.effects.systems.transition_creation_system;
import games.generalszh.gameplay.effects.systems.radius_decal_system;
import games.generalszh.gameplay.effects.components.bone_fx;
import games.generalszh.gameplay.effects.components.radius_decal;

// The effects domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The effects domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceEffectsResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<generalszh::gameplay::EffectCues>();
	world.EmplaceResource<generalszh::gameplay::BoneFxEvents>();
	world.EmplaceResource<generalszh::gameplay::TransitionCreationEvents>();
}

inline void RegisterEffectsComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::BoneFx>();
	world.RegisterComponent<generalszh::gameplay::RadiusDecal>();
}

// The effects domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterEffectsSystems(ecs::SystemRegistry &registry)
{
	static generalszh::gameplay::BoneFxSystem boneFx;
	registry.Register(boneFx);
	static generalszh::gameplay::RadiusDecalSystem radiusDecals;
	registry.Register(radiusDecals);
	static generalszh::gameplay::TransitionCreationSystem transitionCreations;
	registry.Register(transitionCreations);
}

// What the effects domain's systems run after (and the few they must precede), within the tick.
inline void OrderEffectsSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::CrashCollisionSystem, domain::RadiusDecalSystem>();
	registry.OrderBefore<gameplay::SlowDeathSystem, domain::RadiusDecalSystem>();
	registry.OrderBefore<gameplay::LoadoutSystem, domain::BoneFxSystem>();
	registry.OrderBefore<gameplay::LoadoutSystem, domain::TransitionCreationSystem>();
}
}
