export module engine.gameplay.common.weapons.components.weapon_slots;
import std;

export import engine.gameplay.common.weapons.components.armament;

// An entity's weapon set beyond one weapon (the original's WeaponSet:
// PRIMARY, SECONDARY, TERTIARY): each slot's weapon and its own firing
// state (ready tick, clip, barrels), which turret aims it (none: the body),
// and the slot in use. The Armament holds the slot in use while it fires;
// the weapon system swaps the chosen slot in each tick.
export namespace engine::gameplay
{
inline constexpr std::size_t WeaponSlotCount = 3;

enum class SlotAim : std::uint8_t
{
	Body,    // the body turns to it (or it is fixed)
	Turret,  // the turret aims it
	AltTurret
};

struct WeaponSlot
{
	std::uint64_t readyTick{0};
	std::uint64_t firedTick{0};
	std::uint32_t weapon{0xFFFFFFFFu};
	std::uint32_t clip{0};
	bool reloading{false};
	std::uint8_t barrels{1};
	std::uint8_t barrel{0};
	std::uint8_t firedBarrel{0};
	SlotAim aim{SlotAim::Body};
	std::uint8_t reserved[3]{}; // no padding: checkpoints hold its bytes
};

struct WeaponSlots
{
	static constexpr std::uint8_t Unlocked = 0xFF;
	std::array<WeaponSlot, WeaponSlotCount> slots{};
	std::uint8_t current{0};
	// The slot it keeps whatever the target (WeaponSet::setWeaponLock LOCKED_PERMANENTLY); Unlocked: it picks.
	std::uint8_t locked{Unlocked};
	// The lock is only for the attack (LOCKED_TEMPORARILY): let go once that weapon empties its clip.
	std::uint8_t temporary{0};
	std::uint8_t reserved[5]{}; // no padding: checkpoints hold its bytes
};

// The Armament's firing state into its slot, and back.
inline void StoreSlot(WeaponSlot &slot, const Armament &armament) noexcept
{
	slot.readyTick = armament.readyTick;
	slot.clip = armament.clip;
	slot.firedTick = armament.firedTick;
	slot.reloading = armament.reloading;
	slot.barrels = armament.barrels;
	slot.barrel = armament.barrel;
	slot.firedBarrel = armament.firedBarrel;
}

inline void LoadSlot(Armament &armament, const WeaponSlot &slot, bool turretAimed) noexcept
{
	armament.weapon = slot.weapon;
	armament.readyTick = slot.readyTick;
	armament.clip = slot.clip;
	armament.firedTick = slot.firedTick;
	armament.reloading = slot.reloading;
	armament.barrels = slot.barrels;
	armament.barrel = slot.barrel;
	armament.firedBarrel = slot.firedBarrel;
	armament.turret = turretAimed;
}

// WeaponSet::setWeaponLock (LOCKED_PERMANENTLY): a slot holding a weapon becomes the current weapon at once (m_curWeapon)
// and stays so; one without a weapon changes nothing. Whether a turret aims it is settled at the next shot.
inline bool LockSlot(WeaponSlots &set, Armament &armament, std::uint8_t slot) noexcept
{
	if (slot >= WeaponSlotCount || set.slots[slot].weapon == 0xFFFFFFFFu)
		return false;
	StoreSlot(set.slots[set.current], armament);
	set.current = slot;
	set.locked = slot;
	set.temporary = 0;
	LoadSlot(armament, set.slots[slot], armament.turret);
	return true;
}

// WeaponSet::setWeaponLock (LOCKED_TEMPORARILY): as a permanent lock, unless one is already on (it stays), for the
// attack only; true when the slot holds a weapon either way.
inline bool LockSlotTemporarily(WeaponSlots &set, Armament &armament, std::uint8_t slot) noexcept
{
	if (slot >= WeaponSlotCount || set.slots[slot].weapon == 0xFFFFFFFFu)
		return false;
	if (set.locked < WeaponSlotCount && set.temporary == 0)
		return true;
	LockSlot(set, armament, slot);
	set.temporary = 1;
	return true;
}

// WeaponSet::releaseWeaponLock(LOCKED_TEMPORARILY): a temporary lock goes.
inline void ReleaseTemporaryLock(WeaponSlots &set) noexcept
{
	if (set.temporary != 0)
	{
		set.locked = WeaponSlots::Unlocked;
		set.temporary = 0;
	}
}
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::WeaponSlots>
{
	static constexpr std::string_view StableName = "engine.gameplay.weapon_slots";
	static constexpr std::uint32_t Version = 3;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::WeaponSlots &value, StateHasher &hasher) noexcept
	{
		for (const engine::gameplay::WeaponSlot &slot : value.slots)
		{
			hasher.AppendU64((std::uint64_t{slot.weapon} << 32) | slot.clip);
			hasher.AppendU64(slot.readyTick);
			hasher.AppendU64(slot.firedTick);
			hasher.AppendU64((std::uint64_t{slot.barrels} << 24) | (std::uint64_t{slot.barrel} << 16) | (std::uint64_t{slot.firedBarrel} << 8) |
				(slot.reloading ? 2u : 0u) | static_cast<std::uint64_t>(slot.aim) << 4);
		}
		hasher.AppendU64((std::uint64_t{value.temporary} << 16) | (std::uint64_t{value.locked} << 8) | value.current);
	}
};
}
