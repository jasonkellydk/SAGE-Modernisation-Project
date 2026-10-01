export module engine.gameplay.rts.combat.algorithms.target_pitch;
import std;

export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.common.spatial.components.body_extent;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;

// Weapon::isWithinTargetPitch and WeaponSet::isAnyWithinTargetPitch: a weapon with MinTargetPitch / MaxTargetPitch may
// target only a victim some part of which lies within those pitches from its firer's centre. Shared by what picks,
// keeps and fires at targets.
export namespace engine::gameplay
{
// Where a body is and how it stands (GeometryInfo): its centre's height above its position, and how far it reaches
// above and below its position.
struct PitchBody
{
	Engine::Math::FixedVector3 position;
	Engine::Math::Fixed centerZ;
	Engine::Math::Fixed above;
	Engine::Math::Fixed below;
};

inline PitchBody PitchBodyOf(Engine::Math::FixedVector3 position, const BodyExtent *extent) noexcept
{
	if (extent == nullptr)
		return {position, {}, {}, {}};
	return {position, extent->centerZ, extent->maxHeight, extent->below};
}

// WeaponTemplate::isContactWeapon (RATIONALIZE_ATTACK_RANGE): its reach less a quarter of a pathfinding cell (10) is
// under a cell.
inline bool IsContactWeapon(const WeaponDefinition &weapon) noexcept
{
	return weapon.attackRange - Engine::Math::Fixed::FromRatio(5, 2) < Engine::Math::Fixed::FromInt(10);
}

// Weapon::m_pitchLimited: its limits narrower than -PI to PI.
inline bool PitchLimited(const WeaponDefinition &weapon) noexcept
{
	return weapon.minTargetPitch > std::numeric_limits<std::int32_t>::min() || weapon.maxTargetPitch < std::numeric_limits<std::int32_t>::max();
}

// Weapon::isWithinTargetPitch: a contact or unlimited weapon, or a victim within 10 of its firer's height, always; else
// the pitches from the firer's centre up to the victim's top and down to its bottom (GeometryInfo::calcPitches) must
// overlap the weapon's.
inline bool WithinTargetPitch(const WeaponDefinition &weapon, const PitchBody &source, const PitchBody &victim) noexcept
{
	if (IsContactWeapon(weapon) || !PitchLimited(weapon))
		return true;
	const Engine::Math::Fixed dz = victim.position.z - source.position.z;
	if ((dz < Engine::Math::Fixed{} ? -dz : dz) < Engine::Math::Fixed::FromInt(10))
		return true;
	const Engine::Math::Fixed center = source.position.z + source.centerZ;
	const Engine::Math::Fixed across = Engine::Math::Length(victim.position.XY() - source.position.XY());
	const auto pitch = [&](Engine::Math::Fixed height) { return static_cast<std::int32_t>(Engine::Math::Atan2(height - center, across).units); };
	const std::int32_t highest = pitch(victim.position.z + victim.above);
	const std::int32_t lowest = pitch(victim.position.z - victim.below);
	const std::int32_t low = weapon.minTargetPitch, high = weapon.maxTargetPitch;
	return (lowest >= low && lowest <= high) || (highest >= low && highest <= high) || (lowest <= low && highest >= high);
}

// WeaponSet::m_hasPitchLimit: any of its weapons is pitch limited (without a weapon set, its one weapon).
inline bool AnyPitchLimited(const WeaponCatalog &weapons, const Armament &armament, const WeaponSlots *set) noexcept
{
	if (set == nullptr)
		return armament.weapon != WeaponCatalog::None && PitchLimited(weapons.At(armament.weapon));
	for (const WeaponSlot &slot : set->slots)
		if (slot.weapon != WeaponCatalog::None && PitchLimited(weapons.At(slot.weapon)))
			return true;
	return false;
}

// WeaponSet::isAnyWithinTargetPitch: none limited, yes; else whether any of its weapons (every slot, locked or not) may.
inline bool AnyWithinTargetPitch(const WeaponCatalog &weapons, const Armament &armament, const WeaponSlots *set, const PitchBody &source,
	const PitchBody &victim) noexcept
{
	if (!AnyPitchLimited(weapons, armament, set))
		return true;
	if (set == nullptr)
		return WithinTargetPitch(weapons.At(armament.weapon), source, victim);
	for (const WeaponSlot &slot : set->slots)
		if (slot.weapon != WeaponCatalog::None && WithinTargetPitch(weapons.At(slot.weapon), source, victim))
			return true;
	return false;
}
}
