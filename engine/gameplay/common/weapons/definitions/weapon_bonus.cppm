export module engine.gameplay.common.weapons.definitions.weapon_bonus;
import std;

export import Engine.Core.Math.Fixed;

// Weapon bonuses (the original's WeaponBonus / WeaponBonusSet): an object's
// bonus conditions (bits the game gives meaning to) pick
// rows of the global table and of a weapon's own table; each picked row adds
// its (multiplier - 1) to every field, so bonuses stack by addition
// (WeaponBonus::appendBonuses). The fields scale damage, damage radius,
// attack range, rate of fire (delays and clip reloads divide by it) and the
// pre-attack delay.
export namespace engine::gameplay
{
// Bonus conditions are the game's: bits 0..31 of an object's WeaponBonusConditions, each with a row in a table.
inline constexpr std::size_t WeaponBonusConditionCount = 32;

enum class WeaponBonusField : std::uint8_t
{
	Damage,
	Radius,
	Range,
	RateOfFire,
	PreAttack,
	Count
};

// TheWeaponBonusFieldNames.
inline constexpr std::array<std::string_view, 5> WeaponBonusFieldNames{"DAMAGE", "RADIUS", "RANGE", "RATE_OF_FIRE", "PRE_ATTACK"};

struct WeaponBonus
{
	std::array<Engine::Math::Fixed, 5> fields{Engine::Math::Fixed::One(), Engine::Math::Fixed::One(), Engine::Math::Fixed::One(),
		Engine::Math::Fixed::One(), Engine::Math::Fixed::One()};

	Engine::Math::Fixed Get(WeaponBonusField field) const noexcept { return fields[static_cast<std::size_t>(field)]; }
	void Set(WeaponBonusField field, Engine::Math::Fixed value) noexcept { fields[static_cast<std::size_t>(field)] = value; }
};

struct WeaponBonusSet
{
	std::array<WeaponBonus, WeaponBonusConditionCount> byCondition{};
};

// WeaponBonusSet::appendBonuses: each set condition's row adds (multiplier - 1) to every field.
inline void AppendBonuses(const WeaponBonusSet &set, std::uint32_t flags, WeaponBonus &bonus) noexcept
{
	for (std::size_t condition = 0; flags != 0 && condition < WeaponBonusConditionCount; ++condition)
	{
		if ((flags & (1u << condition)) == 0)
			continue;
		for (std::size_t field = 0; field < bonus.fields.size(); ++field)
			bonus.fields[field] += set.byCondition[condition].fields[field] - Engine::Math::Fixed::One();
	}
}

// RATIONALIZE_ATTACK_RANGE: ranges are undersized by a quarter of a pathfind cell (10), so a unit's goal is never
// teetering on the edge of firing range.
inline constexpr Engine::Math::Fixed AttackRangeUndersize = Engine::Math::Fixed::FromRatio(5, 2);

// WeaponTemplate::getAttackRange: AttackRange times the range bonus, less the undersize, never below zero.
inline Engine::Math::Fixed BonusAttackRange(Engine::Math::Fixed attackRange, const WeaponBonus &bonus) noexcept
{
	const Engine::Math::Fixed range = attackRange * bonus.Get(WeaponBonusField::Range) - AttackRangeUndersize;
	return range > Engine::Math::Fixed{} ? range : Engine::Math::Fixed{};
}

// WeaponTemplate::getMinimumAttackRange: MinimumAttackRange less the undersize, never below zero.
inline Engine::Math::Fixed UndersizedMinimumRange(Engine::Math::Fixed minimumRange) noexcept
{
	const Engine::Math::Fixed range = minimumRange - AttackRangeUndersize;
	return range > Engine::Math::Fixed{} ? range : Engine::Math::Fixed{};
}

// getDelayBetweenShots / getClipReloadTime: divided by the rate-of-fire bonus, rounded down.
inline std::uint64_t BonusDelay(std::uint64_t ticks, const WeaponBonus &bonus) noexcept
{
	const Engine::Math::Fixed rate = bonus.Get(WeaponBonusField::RateOfFire);
	if (rate <= Engine::Math::Fixed{})
		return ticks;
	const std::int64_t delay = (Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(ticks)) / rate).Floor();
	return delay > 0 ? static_cast<std::uint64_t>(delay) : 0u;
}
}
