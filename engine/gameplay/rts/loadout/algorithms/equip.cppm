export module engine.gameplay.rts.loadout.algorithms.equip;
import std;

export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.common.weapons.resources.weapon_catalog;

// WeaponSet::updateWeaponSet for a new set: every slot's weapon fresh (a full
// clip, ready now), the PRIMARY in hand. One weapon: the Armament alone; more:
// the slots too (their aims and barrels kept when it had slots, else the
// PRIMARY on the turret when it has one and one barrel for the others). A
// weapon lock is released unless the set shares it (WeaponLockSharedAcrossSets),
// the locked weapon then staying in hand. The slots take on the set's rules.
// Returns the slots to give it when it had none and now needs them.
export namespace engine::gameplay
{
inline std::optional<WeaponSlots> EquipWeapons(Armament &armament, WeaponSlots *slots, const std::array<std::uint32_t, 3> &weapons,
	const WeaponCatalog &catalog, bool lockShared = false, const SlotRules &rules = {})
{
	const auto fresh = [&](std::uint32_t weapon) {
		WeaponSlot slot;
		slot.weapon = weapon;
		if (weapon != WeaponCatalog::None)
			slot.clip = catalog.At(weapon).clipSize;
		return slot;
	};
	const bool multi = slots != nullptr || weapons[1] != WeaponCatalog::None || weapons[2] != WeaponCatalog::None;
	if (!multi)
	{
		const WeaponSlot slot = fresh(weapons[0]);
		armament.weapon = slot.weapon;
		armament.clip = slot.clip;
		armament.readyTick = 0;
		armament.reloading = false;
		armament.barrel = 0;
		armament.barrelShots = 0;
		armament.scatterUsed = 0;
		return std::nullopt;
	}
	WeaponSlots set = slots != nullptr ? *slots : WeaponSlots{};
	for (std::size_t index = 0; index < WeaponSlotCount; ++index)
	{
		WeaponSlot slot = fresh(weapons[index]);
		slot.aim = slots != nullptr ? slots->slots[index].aim : index == 0 && armament.turret ? SlotAim::Turret : SlotAim::Body;
		slot.barrels = slots != nullptr ? slots->slots[index].barrels : index == 0 ? armament.barrels : std::uint8_t{1};
		slot.sources = rules.sources[index];
		slot.preferred = rules.preferred[index];
		set.slots[index] = slot;
	}
	if (!lockShared || set.locked >= WeaponSlotCount || set.slots[set.locked].weapon == WeaponCatalog::None)
		set.locked = WeaponSlots::Unlocked;
	set.current = set.locked < WeaponSlotCount ? set.locked : 0;
	set.sharedReload = rules.sharedReload ? 1 : 0;
	LoadSlot(armament, set.slots[set.current], set.current == 0 ? armament.turret : set.slots[set.current].aim == SlotAim::Turret);
	if (slots != nullptr)
	{
		*slots = set;
		return std::nullopt;
	}
	return set;
}
}
