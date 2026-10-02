export module engine.gameplay.rts.combat.algorithms.projectile_collision;
import std;

export import engine.gameplay.rts.combat.resources.shots;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.weapons.definitions.weapon;

// What a projectile in flight runs into (the original's projectile collisions
// with WeaponTemplate::shouldProjectileCollideWith): anything whose footprint
// it touches (across both radii, from the thing's feet to twice its radius
// up), its intended victim always, never its launcher or itself; otherwise by
// the weapon's ProjectileCollidesWith: allies or enemies, structures (its own
// player's are "controlled"), projectiles and missiles. A thing its attackers miss for now (getSneakyTargetingOffset:
// `sneaky`) is never run into but as the intended victim.
export namespace engine::gameplay
{
inline bool CollidesWith(const WeaponDefinition &weapon, const Shot &shot, ecs::Entity self, const SpatialEntry &other,
	const Relationships &relationships, std::uint32_t sourceTeam = Relationships::NoTeam) noexcept
{
	if (other.entity == shot.target)
		return true;
	// Never its launcher, nor what its launcher rides in.
	if (other.entity == shot.source || other.entity == self || (shot.shelter != ecs::Entity{} && other.entity == shot.shelter))
		return false;
	std::uint32_t required = 0;
	switch (relationships.Between(sourceTeam, shot.sourcePlayer, other.team, other.player))
	{
	case Relationship::Allies: required = weapon_collides::Allies; break;
	case Relationship::Enemies: required = weapon_collides::Enemies; break;
	default: break;
	}
	if ((other.classes & target_class::Structure) != 0)
		required |= other.player == shot.sourcePlayer ? weapon_collides::ControlledStructures : weapon_collides::Structures;
	if ((other.classes & target_class::Projectile) != 0)
		required |= weapon_collides::Projectiles;
	if ((other.classes & target_class::SmallMissile) != 0)
		required |= weapon_collides::SmallMissiles;
	if ((other.classes & target_class::BallisticMissile) != 0)
		required |= weapon_collides::BallisticMissiles;
	return (weapon.collides & required) != 0;
}

// The first thing (in the index's order) the projectile at `at` with `radius` runs into, or none.
template<typename Sneaky>
inline const SpatialEntry *ProjectileCollision(const WeaponDefinition &weapon, const Shot &shot, ecs::Entity self, const Engine::Math::FixedVector3 &at,
	Engine::Math::Fixed radius, const SpatialIndex &spatial, const Relationships &relationships, Sneaky &&sneaky) noexcept
{
	const SpatialEntry *hit = nullptr;
	const SpatialEntry *launcher = spatial.Find(shot.source);
	const std::uint32_t sourceTeam = launcher != nullptr ? launcher->team : Relationships::NoTeam;
	spatial.ForEachWithin(at.XY(), radius, [&](const SpatialEntry &other) {
		if (hit != nullptr)
			return;
		if (at.z < other.position.z - radius || at.z > other.position.z + other.radius * Engine::Math::Fixed::FromInt(2) + radius)
			return;
		if (other.entity != shot.target && sneaky(other.entity))
			return;
		if (CollidesWith(weapon, shot, self, other, relationships, sourceTeam))
			hit = &other;
	});
	return hit;
}
}
