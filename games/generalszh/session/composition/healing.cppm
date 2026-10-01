export module games.generalszh.session.composition.healing;
import std;
import engine.gameplay.common.healing.resources.heal_pulses;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.health.systems.pending_damage_system;
import engine.gameplay.rts.combat.systems.damage_reaction_system;
import engine.gameplay.rts.construction.systems.construction_system;
import engine.gameplay.rts.construction.systems.sale_system;
import engine.gameplay.rts.economy.systems.overcharge_system;
import engine.gameplay.rts.propaganda.systems.propaganda_system;
import engine.gameplay.rts.slaves.systems.slaved_system;
import engine.gameplay.rts.veterancy.systems.veterancy_system;
import games.generalszh.gameplay.combat.systems.pilot_kill_system;
import engine.gameplay.common.healing.systems.area_healing_system;
import engine.gameplay.common.healing.systems.healing_system;
import engine.gameplay.common.healing.systems.self_healing_system;
import engine.gameplay.common.healing.components.healing;

// The healing domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The healing domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceHealingResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::HealOffers>();
	world.EmplaceResource<engine::gameplay::HealPulses>();
}

inline void RegisterHealingComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::HealLock>();
	world.RegisterComponent<engine::gameplay::SelfHealing>();
	world.RegisterComponent<engine::gameplay::AreaHealing>();
}

// The healing domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterHealingSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::SelfHealingSystem selfHealing;
	registry.Register(selfHealing);
	static engine::gameplay::AreaHealingSystem areaHealing;
	registry.Register(areaHealing);
	static engine::gameplay::HealingSystem healing;
	registry.Register(healing);
}

// What the healing domain's systems run after (and the few they must precede), within the tick.
inline void OrderHealingSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::SlaveRepairSystem, gameplay::HealingSystem>();
	registry.OrderBefore<gameplay::VeterancySystem, gameplay::SelfHealingSystem>();
	registry.OrderBefore<gameplay::VeterancySystem, gameplay::HealingSystem>();
	registry.OrderBefore<gameplay::DamageReactionSystem, gameplay::HealingSystem>();
	registry.OrderBefore<gameplay::DamageReactionSystem, gameplay::SelfHealingSystem>();
	registry.OrderBefore<gameplay::PendingDamageSystem, gameplay::HealingSystem>();
	registry.OrderBefore<domain::PilotKillSystem, gameplay::SelfHealingSystem>();
	registry.OrderBefore<domain::PilotKillSystem, gameplay::HealingSystem>();
	registry.OrderBefore<gameplay::PropagandaInfluenceSystem, gameplay::HealingSystem>();
	registry.OrderBefore<gameplay::OverchargeLimitSystem, gameplay::SelfHealingSystem>();
	registry.OrderBefore<gameplay::OverchargeLimitSystem, gameplay::HealingSystem>();
	// Poison hurts join this tick's damage (after fire's).
	registry.OrderBefore<gameplay::SaleSystem, gameplay::SelfHealingSystem>();
	registry.OrderBefore<gameplay::ConstructionSystem, gameplay::HealingSystem>();
}
}
