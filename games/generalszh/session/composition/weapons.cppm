export module games.generalszh.session.composition.weapons;
import std;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.combat.systems.auto_fire_system;
import engine.gameplay.rts.combat.systems.damage_reaction_system;
import engine.gameplay.rts.combat.systems.turret_system;
import engine.gameplay.rts.horde.systems.horde_system;
import engine.gameplay.rts.propaganda.systems.propaganda_system;
import engine.gameplay.common.weapons.systems.temp_weapon_bonus_system;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.common.weapons.components.temp_weapon_bonus;
import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
import engine.gameplay.common.weapons.components.weapon_slots;

// The weapons domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
inline void RegisterWeaponsComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::TempWeaponBonus>();
	world.RegisterComponent<engine::gameplay::Armament>();
	world.RegisterComponent<engine::gameplay::WeaponBonusConditions>();
	world.RegisterComponent<engine::gameplay::AttackTarget>();
	world.RegisterComponent<engine::gameplay::WeaponSlots>();
}

// The weapons domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterWeaponsSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::TempWeaponBonusSystem tempWeaponBonuses;
	registry.Register(tempWeaponBonuses);
}

// What the weapons domain's systems run after (and the few they must precede), within the tick.
inline void OrderWeaponsSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	registry.OrderBefore<gameplay::PropagandaInfluenceSystem, gameplay::TempWeaponBonusSystem>();
	registry.OrderBefore<gameplay::DamageReactionSystem, gameplay::TempWeaponBonusSystem>();
	registry.OrderBefore<gameplay::AutoFireSystem, gameplay::TempWeaponBonusSystem>();
	registry.OrderBefore<gameplay::TurretSystem, gameplay::TempWeaponBonusSystem>();
	registry.OrderBefore<gameplay::HordeSystem, gameplay::TempWeaponBonusSystem>();
}
}
