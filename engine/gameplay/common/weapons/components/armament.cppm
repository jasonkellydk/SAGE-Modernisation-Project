export module engine.gameplay.common.weapons.components.armament;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;

// An entity's weapon (an index into the WeaponCatalog), when it may fire
// next, what is left in its clip, and how it aims: a turret aims on its
// own; otherwise the body turns to face the target at `turnRate`.
export namespace engine::gameplay
{
// readyTick of a weapon that is out of ammo until it reloads at base.
inline constexpr std::uint64_t OutOfAmmo = ~std::uint64_t{0};

struct Armament
{
	std::uint64_t readyTick{0};
	// The tick of the last shot (0: none yet), and whether the wait until
	// readyTick is a clip reload (for how the unit looks).
	std::uint64_t firedTick{0};
	// Winding up to fire (Weapon::preFireWeapon: PRE_ATTACK): the shot goes on `preAttackUntil`, as long as the attack
	// carries on without a break (`preAttackSeen`, the last tick it was ready to fire at `preAttackVictim`); 0: not.
	std::uint64_t preAttackUntil{0};
	std::uint64_t preAttackSeen{0};
	ecs::Entity preAttackVictim;
	// Whom it last fired at (FiringTracker::m_victimID: a PER_ATTACK wind-up comes only before the first shot at one).
	ecs::Entity lastVictim;
	// Weapon::m_scatterTargetsUnused, inverted: the ScatterTarget entries this clip has aimed at (bit per entry); a
	// reload clears it.
	std::uint64_t scatterUsed{0};
	std::uint32_t weapon{0xFFFFFFFFu};
	std::uint32_t clip{0};
	Engine::Math::TurnAngle turnRate;
	bool turret{false};
	bool reloading{false};
	// Its barrels (the original's Weapon::m_curBarrel, one shot per barrel):
	// how many, the next to fire and the one that fired last.
	std::uint8_t barrels{1};
	std::uint8_t barrel{0};
	std::uint8_t firedBarrel{0};
	// The shots the barrel in turn has fired (Weapon::m_numShotsForCurBarrel, counted up): at ShotsPerBarrel the next goes.
	std::uint8_t barrelShots{0};
	std::uint8_t reserved[6]{}; // no padding: checkpoints hold its bytes
};

// Who the entity is attacking. `ordered` targets come from orders or
// scripts; others were picked by the entity's own targeting.
// What it attacks: an object (`target`), or a spot on the ground (`atPosition`: AIUpdateInterface::aiAttackPosition);
// `ordered` by a player, a script or its own AI rather than picked by its mood; `shotsLeft` the shots it may still fire
// (maxShotsToFire; 0: no limit), the attack over once they are fired.
// Where an order came from (the original's CommandSourceType, in its order: CMD_FROM_PLAYER, CMD_FROM_SCRIPT,
// CMD_FROM_AI, CMD_FROM_DOZER); a weapon set says which of them may pick each of its weapons (AutoChooseSources).
enum class CommandSource : std::uint8_t
{
	Player = 0,
	Script = 1,
	Ai = 2,
	Dozer = 3,
};

struct AttackTarget
{
	ecs::Entity target;
	bool ordered{false};
	std::uint8_t atPosition{0};
	// Weapon::m_leechWeaponRangeActive for this attack: the weapon slots (bit per slot) with a LeechRangeWeapon that has
	// fired or begun winding up at it, which reach any distance until the attack ends (a new attack starts with none).
	std::uint8_t leech{0};
	// Who ordered the attack (AIUpdateInterface::getLastCommandSource): its own AI when it picked the target itself.
	CommandSource source{CommandSource::Ai};
	// It has fired in this attack (a weapon with ContinueAttackRange sees through stealth until then).
	std::uint8_t fired{0};
	std::uint8_t reserved[3]{}; // no padding: checkpoints hold its bytes
	std::uint32_t shotsLeft{0};
	std::uint32_t reserved2{0};
	Engine::Math::FixedVector3 position;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Armament>
{
	static constexpr std::string_view StableName = "engine.gameplay.armament";
	static constexpr std::uint32_t Version = 4;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Armament &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.weapon);
		hasher.AppendU64(value.readyTick);
		hasher.AppendU64(value.clip);
		hasher.AppendU64(value.turnRate.units);
		hasher.AppendU64(value.turret ? 1u : 0u);
		hasher.AppendU64(value.firedTick);
		hasher.AppendU64(value.reloading ? 1u : 0u);
		hasher.AppendU64((std::uint64_t{value.barrelShots} << 24) | (std::uint64_t{value.barrels} << 16) | (std::uint64_t{value.barrel} << 8) | value.firedBarrel);
		hasher.AppendU64(value.preAttackUntil);
		hasher.AppendU64(value.preAttackSeen);
		hasher.AppendU64((std::uint64_t{value.preAttackVictim.index} << 32) | value.preAttackVictim.generation);
		hasher.AppendU64((std::uint64_t{value.lastVictim.index} << 32) | value.lastVictim.generation);
		hasher.AppendU64(value.scatterUsed);
	}
};

template<>
struct ComponentTraits<engine::gameplay::AttackTarget>
{
	static constexpr std::string_view StableName = "engine.gameplay.attack_target";
	static constexpr std::uint32_t Version = 4;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::AttackTarget &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.target.index);
		hasher.AppendU64(value.target.generation);
		hasher.AppendU64((value.ordered ? 1u : 0u) | (std::uint64_t{value.atPosition} << 8) | (std::uint64_t{value.leech} << 16) |
			(static_cast<std::uint64_t>(value.source) << 24) | (std::uint64_t{value.shotsLeft} << 32));
		hasher.AppendU64(value.fired);
		if (value.atPosition != 0)
		{
			hasher.AppendU64(static_cast<std::uint64_t>(value.position.x.Raw()));
			hasher.AppendU64(static_cast<std::uint64_t>(value.position.y.Raw()));
			hasher.AppendU64(static_cast<std::uint64_t>(value.position.z.Raw()));
		}
	}
};
}
