export module engine.gameplay.common.weapons.components.weapon_bonus_conditions;
import std;

export import engine.ecs.core.component_registry;
export import engine.gameplay.common.weapons.definitions.weapon_bonus;

// The weapon bonus conditions an object has now (the original's
// Object::m_weaponBonusCondition: bits the game gives meaning to), set and
// cleared by whatever grants them (promotions, hordes, slaves, upgrades),
// and the tick they last changed: its weapons re-time then
// (Object::setWeaponBonusCondition -> WeaponSet::weaponSetOnWeaponBonusChange).
export namespace engine::gameplay
{
struct WeaponBonusConditions
{
	std::uint32_t flags{0};
	// Its container's conditions passed to it (Weapon::computeBonus: a container with WeaponBonusPassedToPassengers).
	std::uint32_t passed{0};
	std::uint64_t changedTick{0};

	// The conditions its weapons' bonuses are computed from (Weapon::computeBonus's flags).
	std::uint32_t Effective() const noexcept { return flags | passed; }
};

// Object::setWeaponBonusCondition / clearWeaponBonusCondition in sequence on `conditions`: whether any of them
// changed the flags (each change re-times the weapons; the last one's bonus stands).
inline bool SetWeaponBonus(WeaponBonusConditions &conditions, std::uint32_t bit, bool on, std::uint64_t tick) noexcept
{
	const std::uint32_t before = conditions.flags;
	conditions.flags = on ? before | bit : before & ~bit;
	if (conditions.flags == before)
		return false;
	conditions.changedTick = tick;
	return true;
}
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::WeaponBonusConditions>
{
	static constexpr std::string_view StableName = "engine.gameplay.weapon_bonus_conditions";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::WeaponBonusConditions &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.flags | (std::uint64_t{value.passed} << 32));
		hasher.AppendU64(value.changedTick);
	}
};
}
