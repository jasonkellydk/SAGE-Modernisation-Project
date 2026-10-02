export module games.generalszh.session.composition.status;
import std;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.status.systems.disable_systems;
import engine.gameplay.common.status.components.ai_activity;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.status.components.disabled_until;
import engine.gameplay.common.status.components.script_status;
import engine.gameplay.common.status.components.status_damage;
import engine.gameplay.common.status.components.status_flags;

// The status domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The status domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceStatusResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::DisableRequests>();
}

inline void RegisterStatusComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::ScriptStatus>();
	world.RegisterComponent<engine::gameplay::AiActivity>();
	world.RegisterComponent<engine::gameplay::StatusDamage>();
	world.RegisterComponent<engine::gameplay::Disabled>();
	world.RegisterComponent<engine::gameplay::StatusFlags>();
	world.RegisterComponent<engine::gameplay::DisabledUntil>();
}

// The status domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterStatusSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::DisableExpirySystem disableExpiry;
	registry.Register(disableExpiry);
	static engine::gameplay::DisableApplySystem disableApply;
	registry.Register(disableApply);
}
}
