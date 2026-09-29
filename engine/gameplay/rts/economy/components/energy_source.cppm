export module engine.gameplay.rts.economy.components.energy_source;
import std;

import engine.ecs.core.component_registry;

// What a thing does to its player's power (the original's EnergyProduction):
// positive produces, negative consumes, until it is removed; a producer's
// EnergyBonus counts once per active bonus source (PowerPlantUpgrade's
// control rods, an active OverchargeBehavior).
export namespace engine::gameplay
{
struct EnergySource
{
	std::int32_t amount{0};
	std::int32_t bonus{0};       // EnergyBonus
	std::uint8_t bonusSources{0}; // active bonus sources
	std::uint8_t reserved[3]{};
};

// KINDOF_POWERED: it is DISABLED_UNDERPOWERED while its player is short of power (Player::onPowerBrownOutChange).
struct Powered
{
	std::uint8_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::EnergySource>
{
	static constexpr std::string_view StableName = "engine.gameplay.energy_source";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::EnergySource &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64((static_cast<std::uint64_t>(static_cast<std::uint32_t>(value.amount)) << 32) | static_cast<std::uint32_t>(value.bonus));
		hasher.AppendU64(value.bonusSources);
	}
};

template<>
struct ComponentTraits<engine::gameplay::Powered>
{
	static constexpr std::string_view StableName = "engine.gameplay.powered";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Powered &, StateHasher &hasher) noexcept { hasher.AppendU64(1); }
};
}
