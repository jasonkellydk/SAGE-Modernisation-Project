export module games.generalszh.session.composition.construction;
import std;
import engine.gameplay.rts.construction.resources.sales;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.healing.systems.area_healing_system;
import engine.gameplay.rts.economy.systems.auto_deposit_system;
import engine.gameplay.rts.mines.systems.demo_trap_system;
import engine.gameplay.rts.mines.systems.minefield_system;
import engine.gameplay.rts.propaganda.systems.propaganda_system;
import engine.gameplay.rts.slaves.systems.slaved_system;
import games.generalszh.gameplay.abilities.systems.special_ability_system;
import games.generalszh.gameplay.hacking.systems.internet_hack_system;
import engine.gameplay.rts.construction.systems.construction_system;
import engine.gameplay.rts.construction.systems.sale_system;
import games.generalszh.gameplay.construction.systems.builder_boredom_system;
import engine.gameplay.rts.construction.components.builder;
import engine.gameplay.rts.construction.components.construction_progress;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.construction.components.under_construction;
import games.generalszh.gameplay.construction.components.builder_boredom;
import games.generalszh.gameplay.construction.components.rebuild_hole;

// The construction domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The construction domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceConstructionResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::SaleSettings>();
	world.EmplaceResource<engine::gameplay::SalesDone>();
	world.EmplaceResource<engine::gameplay::ConstructionsDone>();
	world.EmplaceResource<generalszh::gameplay::BoredBuilders>();
}

inline void RegisterConstructionComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::RebuildHole>();
	world.RegisterComponent<generalszh::gameplay::RebuildWorker>();
	world.RegisterComponent<engine::gameplay::ConstructionProgress>();
	world.RegisterComponent<engine::gameplay::Sale>();
	world.RegisterComponent<engine::gameplay::UnderConstruction>();
	world.RegisterComponent<engine::gameplay::Builder>();
	world.RegisterComponent<generalszh::gameplay::BuilderBoredom>();
}

// The construction domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterConstructionSystems(ecs::SystemRegistry &registry)
{
	static generalszh::gameplay::BuilderBoredomSystem builderBoredom;
	registry.Register(builderBoredom);
	static engine::gameplay::SaleSystem sale;
	registry.Register(sale);
	static engine::gameplay::ConstructionSystem construction;
	registry.Register(construction);
}

// What the construction domain's systems run after (and the few they must precede), within the tick.
inline void OrderConstructionSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::SlaveRepairSystem, gameplay::ConstructionSystem>();
	registry.OrderBefore<domain::InternetHackSystem, gameplay::ConstructionSystem>();
	registry.OrderBefore<domain::SpecialAbilitySystem, gameplay::ConstructionSystem>();
	registry.OrderBefore<gameplay::AutoDepositSystem, gameplay::SaleSystem>();
	registry.OrderBefore<gameplay::DemoTrapSystem, gameplay::SaleSystem>();
	registry.OrderBefore<gameplay::MinefieldSystem, gameplay::ConstructionSystem>();
	registry.OrderBefore<gameplay::PropagandaScanSystem, gameplay::SaleSystem>();
	registry.OrderBefore<gameplay::PropagandaInfluenceSystem, gameplay::ConstructionSystem>();
	// Repairs add to the heal pulses area healing gathered, before they are applied.
	registry.OrderBefore<gameplay::AreaHealingSystem, gameplay::ConstructionSystem>();
}
}
