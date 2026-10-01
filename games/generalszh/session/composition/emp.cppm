export module games.generalszh.session.composition.emp;
import std;
import engine.gameplay.rts.emp.resources.emp_strikes;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.mines.systems.demo_trap_system;
import engine.gameplay.rts.emp.systems.emp_pulse_system;
import engine.gameplay.rts.emp.components.emp_pulse;

// The emp domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The emp domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceEmpResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::EmpStrikes>();
}

inline void RegisterEmpComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::EmpPulse>();
	world.RegisterComponent<engine::gameplay::EmpTraits>();
}

// The emp domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterEmpSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::EmpPulseSystem empPulses;
	registry.Register(empPulses);
}

// What the emp domain's systems run after (and the few they must precede), within the tick.
inline void OrderEmpSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	registry.OrderBefore<gameplay::DemoTrapSystem, gameplay::EmpPulseSystem>();
}
}
