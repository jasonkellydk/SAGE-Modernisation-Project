export module engine.gameplay.rts.combat.algorithms.weapon_fitness;
import std;

export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.common.health.resources.armor_catalog;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.health.components.subdual;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.rts.construction.components.under_construction;

// WeaponTemplate::estimateWeaponDamage with ActiveBody::estimateDamage: how much a weapon is reckoned to hurt a victim,
// which decides whether a unit may attack it at all (WeaponSet::getAbleToAttackSpecificObject) and which of its weapons
// it picks (chooseBestWeaponForTarget). Shared by what picks, keeps and fires at targets, and the attack cursor.
export namespace engine::gameplay
{
// What the estimate looks at in a victim: its target classes (this tick's airborne ones and what it holds among them:
// Emptied, Clearable), its armor, and for the weapons that care, whether it is under construction and can be subdued.
struct VictimFitness
{
	std::uint32_t classes{0};
	std::uint32_t armor{0};
	bool underConstruction{false};
	bool subduable{false};

	bool Clearable() const noexcept { return (classes & target_class::Clearable) != 0; }
};

// Weapon::isDamageWeapon: DEPLOY and DISARM always, HACK never, else one that does primary or secondary damage.
inline bool IsDamageWeapon(const WeaponDefinition &weapon, const WeaponCatalog &weapons) noexcept
{
	if (weapon.damageType == weapons.deploy || weapon.damageType == weapons.disarm)
		return true;
	if (weapon.damageType == weapons.hack)
		return false;
	return weapon.primaryDamage > Engine::Math::Fixed{} || weapon.secondaryDamage > Engine::Math::Fixed{};
}

// Whether the estimate needs more of the victim than its classes and armor (its construction, subdual or contents).
inline bool NeedsVictimDetails(const WeaponDefinition &weapon, const WeaponCatalog &weapons, const ArmorCatalog &armors) noexcept
{
	return weapon.allowAttackGarrisoned || weapon.damageType == weapons.sniper || weapon.damageType == weapons.surrender ||
		weapon.damageType == weapons.killGarrisoned || armors.Subdual(weapon.damageType);
}

// Whether the estimate is above zero for every victim but shrubbery, whatever it is (no armor stops its damage).
inline bool AlwaysHurts(const WeaponDefinition &weapon, Engine::Math::Fixed damage, const WeaponCatalog &weapons, const ArmorCatalog &armors) noexcept
{
	return damage > Engine::Math::Fixed{} && !NeedsVictimDetails(weapon, weapons, armors) && weapon.damageType != weapons.disarm &&
		weapon.damageType != weapons.deploy && !armors.SomeStop(weapon.damageType);
}

// WeaponTemplate::estimateWeaponDamage (retail: RETAIL_COMPATIBLE_CRC) for `damage`, its primary damage with the firer's
// bonus: shrubbery only a burning weapon, reckoned at 1; a sniper nothing in an empty structure that holds units; a
// surrender weapon or one allowed at garrisons 1 against a garrison it can clear; a disarming weapon 1 against a mine or
// trap, else nothing; a deploying one 1 against anything on the ground. Then ActiveBody::estimateDamage: subdual damage
// nothing to what cannot be subdued, a garrison-killing weapon 1 against a garrison it can clear else nothing, a sniper
// nothing to a structure under construction; else what the victim's armor lets through.
inline Engine::Math::Fixed EstimateWeaponDamage(const WeaponDefinition &weapon, Engine::Math::Fixed damage, const VictimFitness &victim,
	const WeaponCatalog &weapons, const ArmorCatalog &armors) noexcept
{
	using Engine::Math::Fixed;
	const std::uint32_t type = weapon.damageType;
	if ((victim.classes & target_class::Shrubbery) != 0)
		return weapon.deathType == weapons.burnedDeath ? Fixed::One() : Fixed{};
	if ((victim.classes & target_class::Structure) != 0 && type == weapons.sniper && (victim.classes & target_class::Emptied) != 0)
		return {};
	if ((type == weapons.surrender || weapon.allowAttackGarrisoned) && victim.Clearable())
		return Fixed::One();
	if (type == weapons.disarm)
		return (victim.classes & (target_class::Mine | target_class::Trap)) != 0 ? Fixed::One() : Fixed{};
	if (type == weapons.deploy && (victim.classes & (target_class::AirborneVehicle | target_class::AirborneInfantry)) == 0)
		return Fixed::One();
	if (armors.Subdual(type) && !victim.subduable)
		return {};
	if (type == weapons.killGarrisoned)
		return victim.Clearable() ? Fixed::One() : Fixed{};
	if (type == weapons.sniper && (victim.classes & target_class::Structure) != 0 && victim.underConstruction)
		return {};
	return AdjustDamage(armors.At(victim.armor), type, damage);
}

// A victim's fitness: its classes (this tick's, from its spatial entry) and its armor, with its construction and subdual
// when `details` (a lookup allowing Health, Subdual and UnderConstruction).
template<typename Lookup>
VictimFitness FitnessOf(const SpatialEntry &entry, const Lookup &lookup, bool details)
{
	VictimFitness victim{entry.classes};
	if (!lookup.IsAlive(entry.entity))
		return victim;
	if (const Health *health = lookup.template Get<Health>(entry.entity))
		victim.armor = health->armor;
	if (details)
	{
		victim.underConstruction = lookup.template Get<UnderConstruction>(entry.entity) != nullptr;
		victim.subduable = lookup.template Get<Subdual>(entry.entity) != nullptr;
	}
	return victim;
}

// WeaponSet::m_hasDamageWeapon: any of its weapons is a damage weapon (without a set, its one weapon).
inline bool HasDamageWeapon(const WeaponCatalog &weapons, const Armament &armament, const WeaponSlots *set) noexcept
{
	if (set == nullptr)
		return armament.weapon != WeaponCatalog::None && IsDamageWeapon(weapons.At(armament.weapon), weapons);
	for (const WeaponSlot &slot : set->slots)
		if (slot.weapon != WeaponCatalog::None && IsDamageWeapon(weapons.At(slot.weapon), weapons))
			return true;
	return false;
}

// The weapons getAbleToUseWeaponAgainstTarget weighs, TERTIARY first: a locked set only its weapon in hand, else every
// slot's; a KILL_PILOT weapon of a hero whose PRIMARY is in hand is passed over (its snipe waits for its own mode).
template<typename Visit>
bool AnyWeighedWeapon(const WeaponCatalog &weapons, const Armament &armament, const WeaponSlots *set, std::uint32_t sourceClasses, Visit &&visit)
{
	const auto weigh = [&](std::uint32_t index, std::uint8_t current) {
		if (index == WeaponCatalog::None)
			return false;
		const WeaponDefinition &weapon = weapons.At(index);
		if (weapon.damageType == weapons.killPilot && (sourceClasses & target_class::Hero) != 0 && current == 0)
			return false;
		return visit(weapon);
	};
	if (set == nullptr)
		return weigh(armament.weapon, 0);
	if (set->locked < WeaponSlotCount)
		return weigh(set->slots[set->current].weapon, set->current);
	for (int index = static_cast<int>(WeaponSlotCount) - 1; index >= 0; --index)
		if (weigh(set->slots[static_cast<std::size_t>(index)].weapon, set->current))
			return true;
	return false;
}

// Whether the victim's fitness is needed at all: not when one of the weighed weapons hurts everything but shrubbery.
inline bool FitnessMatters(const WeaponCatalog &weapons, const ArmorCatalog &armors, const Armament &armament, const WeaponSlots *set,
	std::uint32_t sourceClasses, std::uint32_t victimClasses)
{
	if ((victimClasses & target_class::Shrubbery) != 0)
		return true;
	return !AnyWeighedWeapon(weapons, armament, set, sourceClasses,
		[&](const WeaponDefinition &weapon) { return AlwaysHurts(weapon, weapon.primaryDamage, weapons, armors); });
}

// Whether the victim's details (construction, subdual) matter to any weighed weapon.
inline bool DetailsMatter(const WeaponCatalog &weapons, const ArmorCatalog &armors, const Armament &armament, const WeaponSlots *set,
	std::uint32_t sourceClasses)
{
	return AnyWeighedWeapon(weapons, armament, set, sourceClasses,
		[&](const WeaponDefinition &weapon) { return NeedsVictimDetails(weapon, weapons, armors); });
}

// getAbleToUseWeaponAgainstTarget's damage test: some weighed weapon is reckoned to hurt the victim (its estimate with
// the firer's bonus above zero).
inline bool AnyWeaponHurts(const WeaponCatalog &weapons, const ArmorCatalog &armors, const Armament &armament, const WeaponSlots *set,
	std::uint32_t sourceClasses, std::uint32_t conditions, const VictimFitness &victim)
{
	return AnyWeighedWeapon(weapons, armament, set, sourceClasses, [&](const WeaponDefinition &weapon) {
		const Engine::Math::Fixed damage = weapon.primaryDamage * weapons.Bonus(weapon, conditions).Get(WeaponBonusField::Damage);
		return EstimateWeaponDamage(weapon, damage, victim, weapons, armors) != Engine::Math::Fixed{};
	});
}
}
