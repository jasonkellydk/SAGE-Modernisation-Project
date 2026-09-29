export module engine.gameplay.rts.combat.algorithms.attack_goal;
import std;

export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.spatial.components.targetable;

// What an attack aims at, as the systems that aim and fire see it: the object's entry in the spatial index (none once it
// is gone), or for an attack on a spot (aiAttackPosition) a point there, of no size, on the ground, seen by everyone,
// belonging to no one, written to `point`.
export namespace engine::gameplay
{
inline bool Attacking(const AttackTarget &attack) noexcept { return attack.atPosition != 0 || attack.target != ecs::Entity{}; }

inline const SpatialEntry *AttackGoal(const SpatialIndex &spatial, const AttackTarget &attack, SpatialEntry &point)
{
	if (attack.atPosition != 0)
	{
		point = SpatialEntry{};
		point.position = attack.position;
		point.classes = target_class::Ground;
		point.player = 0xFFFFFFFFu;
		return &point;
	}
	return attack.target == ecs::Entity{} ? nullptr : spatial.Find(attack.target);
}

// An attack that has fired its last allowed shot is over (AIAttackState with maxShotsToFire).
inline void CountShot(AttackTarget &attack) noexcept
{
	if (attack.shotsLeft == 0)
		return;
	if (--attack.shotsLeft == 0)
		attack = AttackTarget{};
}
}
