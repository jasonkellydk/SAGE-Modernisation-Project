export module games.generalszh.session.composition.appearance;
import std;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.combat.systems.weapon_bonus_retime_system;
import engine.gameplay.rts.death.systems.crash_collision_system;
import engine.gameplay.rts.lifecycle.systems.removal_system;
import engine.gameplay.rts.movement.systems.movement_system;
import engine.gameplay.rts.production.systems.production_system;
import games.generalszh.gameplay.appearance.systems.appearance_system;
import games.generalszh.gameplay.appearance.systems.extension_look_system;
import games.generalszh.gameplay.appearance.systems.panic_look_system;
import games.generalszh.gameplay.appearance.systems.steering_look_system;
import engine.gameplay.common.appearance.components.appearance;
import engine.gameplay.common.appearance.components.debris_look;
import engine.gameplay.common.appearance.components.draw_offset;
import engine.gameplay.common.appearance.components.indicator_color;
import engine.gameplay.common.appearance.components.model_override;
import engine.gameplay.common.appearance.components.part_overrides;
import games.generalszh.gameplay.appearance.components.building_extensions;
import games.generalszh.gameplay.appearance.components.steering_look;

// The appearance domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The appearance domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceAppearanceResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<generalszh::gameplay::AppearanceSettings>(generalszh::gameplay::AppearanceSettings{setup.content.gameData.unitDamaged, setup.content.gameData.unitReallyDamaged});
}

inline void RegisterAppearanceComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::IndicatorColor>();
	world.RegisterComponent<generalszh::gameplay::SteeringLook>();
	world.RegisterComponent<generalszh::gameplay::RadarDish>();
	world.RegisterComponent<generalszh::gameplay::ControlRods>();
	world.RegisterComponent<engine::gameplay::Appearance>();
	world.RegisterComponent<engine::gameplay::DrawOffset>();
	world.RegisterComponent<engine::gameplay::ModelOverride>();
	world.RegisterComponent<engine::gameplay::DebrisLook>();
	world.RegisterComponent<engine::gameplay::PartOverrides>();
}

// The appearance domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterAppearanceSystems(ecs::SystemRegistry &registry)
{
	static generalszh::gameplay::AppearanceSystem appearance;
	registry.Register(appearance);
	static generalszh::gameplay::SteeringLookSystem steeringLooks;
	registry.Register(steeringLooks);
	static generalszh::gameplay::ExtensionLookSystem extensionLooks;
	registry.Register(extensionLooks);
	static generalszh::gameplay::PanicLookSystem panicLooks;
	registry.Register(panicLooks);
}

// What the appearance domain's systems run after (and the few they must precede), within the tick.
inline void OrderAppearanceSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::WeaponBonusRetimeSystem, domain::AppearanceSystem>();
	registry.OrderBefore<gameplay::CrashCollisionSystem, domain::AppearanceSystem>();
	// The look shows the doors as production left them this tick.
	registry.OrderBefore<gameplay::ProductionSystem, domain::AppearanceSystem>();
	registry.OrderBefore<gameplay::RemovalSystem, domain::AppearanceSystem>();
	// Turn looks follow this tick's steering, over the conditions the appearance system keeps.
	registry.OrderBefore<gameplay::MovementSystem, domain::SteeringLookSystem>();
	registry.OrderBefore<domain::AppearanceSystem, domain::SteeringLookSystem>();
	// Radar dishes and control rods, over the conditions the appearance system keeps.
	registry.OrderBefore<domain::AppearanceSystem, domain::ExtensionLookSystem>();
	registry.OrderBefore<domain::SteeringLookSystem, domain::ExtensionLookSystem>();
	registry.OrderBefore<domain::SteeringLookSystem, domain::PanicLookSystem>();
	registry.OrderBefore<domain::PanicLookSystem, domain::ExtensionLookSystem>();
}
}
