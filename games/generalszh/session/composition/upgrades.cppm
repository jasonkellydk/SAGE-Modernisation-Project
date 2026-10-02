export module games.generalszh.session.composition.upgrades;
import std;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.upgrades.systems.upgrade_system;
import games.generalszh.gameplay.upgrades.systems.research_completion_system;
import games.generalszh.gameplay.upgrades.systems.upgrade_effect_system;
import engine.gameplay.rts.upgrades.components.upgradable;
import games.generalszh.gameplay.upgrades.components.command_set_override;

// The upgrades domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
inline void RegisterUpgradesComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::CommandSetOverride>();
	world.RegisterComponent<engine::gameplay::Upgradable>();
}

// The upgrades domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterUpgradesSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::UpgradeSystem upgradeTriggers;
	registry.Register(upgradeTriggers);
	static generalszh::gameplay::UpgradeEffectSystem upgradeEffects;
	registry.Register(upgradeEffects);
	static generalszh::gameplay::ResearchCompletionSystem researchCompletion;
	registry.Register(researchCompletion);
}
}
