export module engine.gameplay.rts.stealth.components.stealth;
import std;

export import Engine.Core.Math.Fixed;
import engine.ecs.core.component_registry;

// Hiding from enemies (the original's StealthUpdate): an entity that can
// stealth is stealthed once none of its forbidden conditions has held for
// `delay` ticks; detected, it shows until `detectedUntil`. Stealthed and not
// detected, enemies cannot pick it as a target (area damage still finds it).
export namespace engine::gameplay
{
namespace stealth_forbidden
{
// The original's StealthForbiddenConditions, in its bit order.
inline constexpr std::uint32_t Attacking = 1u << 0;
inline constexpr std::uint32_t Moving = 1u << 1;
inline constexpr std::uint32_t UsingAbility = 1u << 2;
inline constexpr std::uint32_t FiringPrimary = 1u << 3;
inline constexpr std::uint32_t FiringSecondary = 1u << 4;
inline constexpr std::uint32_t FiringTertiary = 1u << 5;
inline constexpr std::uint32_t NoBlackMarket = 1u << 6;
inline constexpr std::uint32_t TakingDamage = 1u << 7;
inline constexpr std::uint32_t RidersAttacking = 1u << 8;
inline constexpr std::uint32_t FiringAny = FiringPrimary | FiringSecondary | FiringTertiary;
}

namespace stealth_flag
{
inline constexpr std::uint32_t CanStealth = 1u << 0; // innate, or granted (upgrades, powers)
inline constexpr std::uint32_t Stealthed = 1u << 1;
inline constexpr std::uint32_t Detected = 1u << 2;
// Kept from stealth this tick while in one of its hint conditions (StealthUpdate::hintDetectableWhileUnstealthed):
// its own player sees it flash.
inline constexpr std::uint32_t HintDetectable = 1u << 3;
}

// The original's HintDetectableConditions this port knows: firing a weapon, using an ability.
namespace stealth_hint
{
inline constexpr std::uint32_t FiringWeapon = 1u << 0;
inline constexpr std::uint32_t UsingAbility = 1u << 1;
}

struct Stealth
{
	std::uint32_t forbidden{0};
	std::uint32_t flags{0};
	std::uint64_t delay{0};
	std::uint64_t allowedAt{0};
	std::uint64_t detectedUntil{0};
	Engine::Math::Fixed moveThreshold; // per tick
	std::uint32_t hint{0};             // stealth_hint bits
	std::uint32_t reserved{0};

	bool Has(std::uint32_t flag) const noexcept { return (flags & flag) != 0; }
	void Set(std::uint32_t flag, bool on) noexcept { flags = on ? (flags | flag) : (flags & ~flag); }
	// Hidden from enemies: stealthed and not detected.
	bool Hidden() const noexcept { return Has(stealth_flag::Stealthed) && !Has(stealth_flag::Detected); }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Stealth>
{
	static constexpr std::string_view StableName = "engine.gameplay.stealth";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
