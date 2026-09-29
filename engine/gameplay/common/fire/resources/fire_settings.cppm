export module engine.gameplay.common.fire.resources.fire_settings;
import std;

import engine.ecs.system.system;

// Which of the game's damage types set things alight, and what burning
// deals (its damage and death types), configured by the game.
export namespace engine::gameplay
{
struct FireSettings
{
	std::uint64_t ignitingDamageTypes{0}; // bit per damage type
	std::uint32_t burnDamageType{0};
	std::uint32_t burnDeathType{0};

	bool Ignites(std::uint32_t damageType) const noexcept { return damageType < 64 && (ignitingDamageTypes >> damageType & 1u) != 0; }
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::FireSettings>
{
	static constexpr std::string_view StableName = "engine.gameplay.fire_settings";
};
}
