export module games.generalszh.session.composition.collision;
import std;
import engine.gameplay.rts.collision.resources.collision_settings;
import engine.gameplay.rts.collision.resources.contacts;
import engine.gameplay.rts.collision.resources.crush_damage;
import games.generalszh.content.combat.combat_catalog;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.fire.systems.flammability_system;
import engine.gameplay.common.physics.systems.physics_system;
import engine.gameplay.common.poison.systems.poison_system;
import engine.gameplay.rts.combat.systems.impact_system;
import engine.gameplay.rts.combat.systems.missile_flight_system;
import engine.gameplay.rts.combat.systems.point_defense_system;
import engine.gameplay.rts.combat.systems.projectile_flight_system;
import engine.gameplay.rts.movement.systems.movement_system;
import games.generalszh.gameplay.abilities.systems.special_ability_system;
import engine.gameplay.rts.collision.systems.body_collision_system;
import engine.gameplay.rts.collision.systems.collide_weapon_system;
import engine.gameplay.rts.collision.systems.collision_systems;
import engine.gameplay.rts.collision.systems.crush_system;
import engine.gameplay.rts.collision.components.body_collision;
import engine.gameplay.rts.collision.components.collide_weapon;
import engine.gameplay.rts.collision.components.collider;
import engine.gameplay.rts.collision.components.squishable;

// The collision domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The collision domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceCollisionResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::CollisionSettings>(engine::gameplay::CollisionSettings{setup.content.gameData.structureStiffness, setup.content.gameData.structureRubbleHeight});
	world.EmplaceResource<engine::gameplay::ColliderIndex>(Engine::Math::FixedVector2{setup.playable.x, setup.playable.y}, Engine::Math::Fixed::FromInt(100));
	world.EmplaceResource<engine::gameplay::ColliderGather>();
	world.EmplaceResource<engine::gameplay::ContactOffers>();
	world.EmplaceResource<engine::gameplay::Contacts>();
	world.EmplaceResource<engine::gameplay::CrushSettings>(engine::gameplay::CrushSettings{*content::DamageTypeIndex("CRUSH"), *content::DeathTypeIndex("CRUSHED")});
	world.EmplaceResource<engine::gameplay::CrushDamage>();
}

inline void RegisterCollisionComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::BodyCollision>();
	world.RegisterComponent<engine::gameplay::CollideWeapon>();
	world.RegisterComponent<engine::gameplay::Collider>();
	world.RegisterComponent<engine::gameplay::Squishable>();
}

// The collision domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterCollisionSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::BodyCollisionSystem bodyCollisions;
	registry.Register(bodyCollisions);
	static engine::gameplay::CollideWeaponSystem collideWeapons;
	registry.Register(collideWeapons);
	static engine::gameplay::ColliderIndexSystem colliderIndex;
	registry.Register(colliderIndex);
	static engine::gameplay::ContactSystem contacts;
	registry.Register(contacts);
	static engine::gameplay::CrushSystem crush;
	registry.Register(crush);
}

// What the collision domain's systems run after (and the few they must precede), within the tick.
inline void OrderCollisionSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	// Bodies pushed apart once everything moved and the colliders are indexed; crash weapons land with the tick's impacts.
	registry.OrderBefore<gameplay::ColliderIndexSystem, gameplay::BodyCollisionSystem>();
	registry.OrderBefore<gameplay::MovementSystem, gameplay::BodyCollisionSystem>();
	registry.OrderBefore<gameplay::CollideWeaponSystem, gameplay::BodyCollisionSystem>();
	registry.OrderBefore<gameplay::PointDefenseSystem, gameplay::BodyCollisionSystem>();
	registry.OrderBefore<gameplay::MissileFlightSystem, gameplay::BodyCollisionSystem>();
	registry.OrderBefore<gameplay::ProjectileFlightSystem, gameplay::BodyCollisionSystem>();
	// Collide weapons fire at those who moved into them, after movement, landing with the tick's impacts.
	registry.OrderBefore<gameplay::ColliderIndexSystem, gameplay::CollideWeaponSystem>();
	registry.OrderBefore<gameplay::MovementSystem, gameplay::CollideWeaponSystem>();
	registry.OrderBefore<gameplay::PointDefenseSystem, gameplay::CollideWeaponSystem>();
	registry.OrderBefore<gameplay::MissileFlightSystem, gameplay::CollideWeaponSystem>();
	registry.OrderBefore<gameplay::ProjectileFlightSystem, gameplay::CollideWeaponSystem>();
	registry.OrderBefore<domain::SpecialAbilitySystem, gameplay::ContactSystem>();
	// Bodies are indexed where they ended up.
	registry.OrderBefore<gameplay::PhysicsSystem, gameplay::ColliderIndexSystem>();
	// Crushing joins the tick's damage after the impacts and the burning.
	registry.OrderBefore<gameplay::ImpactSystem, gameplay::CrushSystem>();
	registry.OrderBefore<gameplay::FlammabilitySystem, gameplay::CrushSystem>();
	registry.OrderBefore<gameplay::PoisonSystem, gameplay::CrushSystem>();
}
}
