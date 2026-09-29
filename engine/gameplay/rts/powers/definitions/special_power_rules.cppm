export module engine.gameplay.rts.powers.definitions.special_power_rules;
import std;

import engine.ecs.system.system;

// Special power templates as the timers need them (SpecialPowerTemplate), by index: how long a power recharges, the
// science a player needs to use it (NoScience: none), whether its timer is shared by the player's objects and whether
// its countdown shows to everyone.
// Level data: not checkpointed.
export namespace engine::gameplay
{
struct SpecialPowerRule
{
	static constexpr std::uint32_t NoScience = 0xFFFFFFFFu;
	std::uint64_t reloadTicks{0};
	std::uint32_t requiredScience{NoScience};
	bool sharedSynced{false};
	bool publicTimer{false}; // PublicTimer: its countdown shows to everyone
};

struct SpecialPowerRules
{
	std::vector<SpecialPowerRule> powers;

	const SpecialPowerRule *Of(std::uint32_t power) const noexcept { return power < powers.size() ? &powers[power] : nullptr; }
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::SpecialPowerRules>
{
	static constexpr std::string_view StableName = "engine.gameplay.special_power_rules";
};
}
