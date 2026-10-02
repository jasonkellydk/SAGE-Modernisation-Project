export module engine.gameplay.rts.combat.algorithms.waypoint_weapon;
import std;

export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.common.weapons.resources.weapon_catalog;

// The weapon a script may fire to fly a path (ScriptActions::doNamedFireWeaponFollowingWaypointPath asks
// Object::findWaypointFollowingCapableWeapon).
export namespace engine::gameplay
{
// WeaponSet::findWaypointFollowingCapableWeapon: of its slots from the last (TERTIARY) down to PRIMARY, the first whose
// weapon is CapableOfFollowingWaypoints; none: nullopt. Without a weapon set, its one weapon is its PRIMARY.
inline std::optional<std::uint8_t> WaypointFollowingSlot(const WeaponSlots *set, const Armament &armament, const WeaponCatalog &weapons) noexcept
{
	const auto follows = [&](std::uint32_t weapon) { return weapon != WeaponCatalog::None && weapons.At(weapon).followsWaypoints; };
	if (set == nullptr)
		return follows(armament.weapon) ? std::optional<std::uint8_t>{0} : std::nullopt;
	for (int slot = static_cast<int>(WeaponSlotCount) - 1; slot >= 0; --slot)
		if (follows(set->slots[static_cast<std::size_t>(slot)].weapon))
			return static_cast<std::uint8_t>(slot);
	return std::nullopt;
}
}
