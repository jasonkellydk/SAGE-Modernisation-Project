export module engine.gameplay.common.health.definitions.armor;
import std;

export import Engine.Core.Math.Fixed;

// How much of each damage type gets through: one coefficient per damage
// type (1 = full damage). Damage types are game data, identified by index;
// types in `bypass` ignore armor entirely.
export namespace engine::gameplay
{
inline constexpr std::size_t MaxDamageTypes = 64;

struct ArmorDefinition
{
	std::array<Engine::Math::Fixed, MaxDamageTypes> coefficient = [] {
		std::array<Engine::Math::Fixed, MaxDamageTypes> all{};
		all.fill(Engine::Math::Fixed::One());
		return all;
	}();
	std::uint64_t bypass{0};
};

inline Engine::Math::Fixed AdjustDamage(const ArmorDefinition &armor, std::uint32_t damageType, Engine::Math::Fixed amount) noexcept
{
	if (damageType >= MaxDamageTypes || (armor.bypass >> damageType & 1u) != 0)
		return amount;
	return std::max(Engine::Math::Fixed{}, amount * armor.coefficient[damageType]);
}
}
