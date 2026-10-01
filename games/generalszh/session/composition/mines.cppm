export module games.generalszh.session.composition.mines;
import std;
import games.generalszh.content.combat.combat_catalog;
import games.generalszh.content.combat.loadout_content;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.health.systems.health_system;
import engine.gameplay.common.health.systems.subdual_system;
import engine.gameplay.rts.combat.systems.countermeasures_system;
import engine.gameplay.rts.combat.systems.impact_system;
import games.generalszh.gameplay.ai.systems.guard_system;
import games.generalszh.gameplay.ai.systems.tunnel_guard_system;
import games.generalszh.gameplay.combat.systems.cooldown_creation_system;
import games.generalszh.gameplay.upgrades.systems.upgrade_effect_system;
import engine.gameplay.rts.mines.systems.demo_trap_system;
import engine.gameplay.rts.mines.systems.minefield_system;
import games.generalszh.gameplay.mines.systems.mine_clearing_detail_system;
import engine.gameplay.rts.mines.components.demo_trap;
import engine.gameplay.rts.mines.components.minefield;
import games.generalszh.gameplay.mines.components.mine_clearer;
import games.generalszh.gameplay.mines.components.minefield_generator;

// The mines domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The mines domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceMinesResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<generalszh::gameplay::MinefieldEffects>();
	world.EmplaceResource<generalszh::gameplay::MineClearingRules>(generalszh::gameplay::MineClearingRules{content::SetFlag(content::WeaponSetFlagNames, "MINE_CLEARING_DETAIL")});
	world.EmplaceResource<engine::gameplay::MineShots>();
	// MinefieldBehavior's drain: DAMAGE_UNRESISTABLE, DEATH_NORMAL, over LOGICFRAMES_PER_SECOND.
	world.EmplaceResource<engine::gameplay::MineSettings>(engine::gameplay::MineSettings{content::DamageTypeIndex("UNRESISTABLE").value_or(0),
		content::DeathTypeIndex("NORMAL").value_or(0), setup.step.TicksPerSecond()});
}

inline void RegisterMinesComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::Minefield>();
	world.RegisterComponent<engine::gameplay::MineSafe>();
	world.RegisterComponent<engine::gameplay::DemoTrap>();
	world.RegisterComponent<generalszh::gameplay::MinefieldGenerator>();
	world.RegisterComponent<generalszh::gameplay::MineClearer>();
}

// The mines domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterMinesSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::MineDrainSystem mineDrain;
	registry.Register(mineDrain);
	static engine::gameplay::MinefieldSystem minefields;
	registry.Register(minefields);
	static engine::gameplay::DemoTrapSystem demoTraps;
	registry.Register(demoTraps);
	static generalszh::gameplay::MineClearingDetailSystem mineClearingDetail;
	registry.Register(mineClearingDetail);
}

// What the mines domain's systems run after (and the few they must precede), within the tick.
inline void OrderMinesSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	// A mine drains with the tick's damage and follows its health once it is taken.
	registry.OrderBefore<gameplay::ImpactSystem, gameplay::MineDrainSystem>();
	registry.OrderBefore<gameplay::HealthSystem, gameplay::MinefieldSystem>();
	registry.OrderBefore<domain::TunnelGuardSystem, gameplay::DemoTrapSystem>();
	registry.OrderBefore<gameplay::SubdualSystem, gameplay::MinefieldSystem>();
	registry.OrderBefore<domain::CooldownCreationSystem, gameplay::DemoTrapSystem>();
	registry.OrderBefore<gameplay::CountermeasuresSystem, gameplay::MinefieldSystem>();
	registry.OrderBefore<domain::GuardSystem, gameplay::DemoTrapSystem>();
	// Supply trucks: their rounds, then their docking, before they are routed and moved.
	// Weapon and armor sets follow their flags after the tick's upgrades and promotions.
	registry.OrderBefore<domain::UpgradeEffectSystem, domain::MineClearingDetailSystem>();
}
}
