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
	std::uint64_t scatterUsed{0};
	std::uint32_t weapon{0xFFFFFFFFu};
	std::uint32_t clip{0};
	bool reloading{false};
	std::uint8_t barrels{1};
	std::uint8_t barrel{0};
	std::uint8_t firedBarrel{0};
	SlotAim aim{SlotAim::Body};
	std::uint8_t barrelShots{0};
	// Its weapon set's rules for it: which command sources may pick it (AutoChooseSources: a bit per CommandSource, and
	// DEFAULT_SWITCH_WEAPON's bit 4) and what it is always picked against (PreferredAgainst: target_class bits).
	std::uint8_t sources{0xFF};
	std::uint8_t reserved{0};
	std::uint32_t preferred{0};
	std::uint32_t reserved2{0}; // no padding: checkpoints hold its bytes
};

// A weapon set's per-slot rules (WeaponTemplateSet's m_autoChooseMask and m_preferredAgainst), which its slots take on
// with their weapons: all sources and nothing preferred unless it says otherwise.
struct SlotRules
{
	std::array<std::uint8_t, WeaponSlotCount> sources{0xFF, 0xFF, 0xFF};
	std::array<std::uint32_t, WeaponSlotCount> preferred{};
	// ShareWeaponReloadTime: a shot, reload or re-timed wait of one weapon holds them all.
	bool sharedReload{false};
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
	// Its set shares reload times (WeaponTemplateSet::m_isReloadTimeShared).
	std::uint8_t sharedReload{0};
	std::uint8_t reserved[4]{}; // no padding: checkpoints hold its bytes
};

// Weapon::getRemainingAmmo: the rounds left in its clip (a clip not yet counted: full), none while it reloads or is out
// of ammo for good.
inline std::uint32_t RemainingAmmo(std::uint32_t clipSize, std::uint32_t clip, std::uint64_t readyTick, bool reloading, std::uint64_t tick) noexcept
{
	if (readyTick == OutOfAmmo || (reloading && tick < readyTick))
		return 0;
	return clip == 0 ? clipSize : std::min(clip, clipSize);
}

// The Armament's firing state into its slot, and back.
inline void StoreSlot(WeaponSlot &slot, const Armament &armament) noexcept
{
	slot.readyTick = armament.readyTick;
	slot.clip = armament.clip;
	slot.firedTick = armament.firedTick;
	slot.scatterUsed = armament.scatterUsed;
	slot.reloading = armament.reloading;
	slot.barrels = armament.barrels;
	slot.barrel = armament.barrel;
	slot.firedBarrel = armament.firedBarrel;
	slot.barrelShots = armament.barrelShots;
}

inline void LoadSlot(Armament &armament, const WeaponSlot &slot, bool turretAimed) noexcept
{
	armament.weapon = slot.weapon;
	armament.readyTick = slot.readyTick;
	armament.clip = slot.clip;
	armament.firedTick = slot.firedTick;
	armament.scatterUsed = slot.scatterUsed;
	armament.reloading = slot.reloading;
	armament.barrels = slot.barrels;
	armament.barrel = slot.barrel;
	armament.firedBarrel = slot.firedBarrel;
	armament.barrelShots = slot.barrelShots;
	armament.turret = turretAimed;
}

// Object::isReloadTimeShared after one weapon's wait was set (Weapon::privateFireWeapon, reloadWithBonus,
// onWeaponBonusChange): every weapon of the set waits as long, between shots or reloading as `reloading` says.
inline void ShareReloadTime(WeaponSlots &set, std::uint64_t readyTick, bool reloading) noexcept
{
	if (set.sharedReload == 0)
		return;
	for (WeaponSlot &slot : set.slots)
		if (slot.weapon != 0xFFFFFFFFu)
		{
			slot.readyTick = readyTick;
			slot.reloading = reloading;
		}
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
	static constexpr std::uint32_t Version = 4;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::WeaponSlots &value, StateHasher &hasher) noexcept
	{
		for (const engine::gameplay::WeaponSlot &slot : value.slots)
		{
			hasher.AppendU64((std::uint64_t{slot.weapon} << 32) | slot.clip);
			hasher.AppendU64(slot.readyTick);
			hasher.AppendU64(slot.firedTick);
			hasher.AppendU64(slot.scatterUsed);
			hasher.AppendU64((std::uint64_t{slot.barrelShots} << 32) | (std::uint64_t{slot.barrels} << 24) | (std::uint64_t{slot.barrel} << 16) | (std::uint64_t{slot.firedBarrel} << 8) |
				(slot.reloading ? 2u : 0u) | static_cast<std::uint64_t>(slot.aim) << 4);
			hasher.AppendU64((std::uint64_t{slot.sources} << 32) | slot.preferred);
		}
		hasher.AppendU64((std::uint64_t{value.sharedReload} << 24) | (std::uint64_t{value.temporary} << 16) | (std::uint64_t{value.locked} << 8) | value.current);
	}
};
}
