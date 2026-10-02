export module engine.gameplay.common.weapons.components.temp_weapon_bonus;
import std;

export import engine.ecs.core.component_registry;
export import engine.gameplay.common.weapons.components.weapon_bonus_conditions;

// A weapon bonus condition given for a while (the original's TempWeaponBonusHelper): the one it holds now (its bit; 0:
// none) and the tick it goes. Giving one clears a different one held, sets it (a fresh end each time); on its tick it
// is cleared. Simulation state: checkpointed.
export namespace engine::gameplay
{
struct TempWeaponBonus
{
	std::uint64_t removeTick{0};
	std::uint32_t bit{0};
	std::uint32_t reserved{0}; // no padding: checkpoints hold its bytes
};

// TempWeaponBonusHelper::doTempWeaponBonus.
inline void GiveTempWeaponBonus(TempWeaponBonus &temp, WeaponBonusConditions &conditions, std::uint32_t bit, std::uint64_t duration, std::uint64_t tick) noexcept
{
	if (temp.bit != 0 && temp.bit != bit)
		SetWeaponBonus(conditions, temp.bit, false, tick);
	SetWeaponBonus(conditions, bit, true, tick);
	temp.bit = bit;
	temp.removeTick = tick + duration;
}
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::TempWeaponBonus>
{
	static constexpr std::string_view StableName = "engine.gameplay.temp_weapon_bonus";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
