export module engine.gameplay.rts.veterancy.algorithms.promotion;
import std;

export import engine.gameplay.rts.veterancy.resources.veterancy_catalog;
export import engine.gameplay.common.health.algorithms.max_health;
export import engine.gameplay.common.weapons.components.weapon_bonus_conditions;

// Object::onVeterancyLevelChanged on an object's data: the level's weapon
// bonus bits alone (each level's set or cleared in level order; a change
// re-times its weapons) and ActiveBody's max health times the
// new level's HealthBonus over the old one's, keeping the ratio. The level's
// veterancy upgrade is the caller's to give (VeterancyCatalog::levelUpgrade).
export namespace engine::gameplay
{
inline void SetLevelBonus(WeaponBonusConditions &conditions, const VeterancyCatalog &catalog, std::uint8_t level, std::uint64_t tick) noexcept
{
	for (std::size_t each = 0; each < VeterancyLevelCount; ++each)
		if (catalog.levelBonus[each] != 0)
			SetWeaponBonus(conditions, catalog.levelBonus[each], each == level, tick);
}

inline void ScaleHealthForLevel(Health &health, const VeterancyCatalog &catalog, std::uint8_t from, std::uint8_t to) noexcept
{
	if (from == to)
		return;
	SetMaxHealth(health, health.maximum * catalog.healthBonus[to] / catalog.healthBonus[from], MaxHealthChange::PreserveRatio);
}
}
