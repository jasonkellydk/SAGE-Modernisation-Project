export module engine.gameplay.rts.powers.components.special_power_timers;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// An object's special power modules (SpecialPowerModule): per module, the power it fires (a SpecialPowerRules
// index), the tick it is next available on, how many pauses hold it, when the pause began and how ready it was then,
// and its module flags, and when its countdown was put on screen (the order they show in). Powers with a shared timer
// read their player's instead (SharedPowerTimers).
export namespace engine::gameplay
{
namespace power_flag
{
inline constexpr std::uint32_t StartsPaused = 1u << 0;           // StartsPaused (an upgrade starts it)
inline constexpr std::uint32_t ScriptOnly = 1u << 1;             // ScriptedSpecialPowerOnly
inline constexpr std::uint32_t UpdateModuleStartsAttack = 1u << 2; // its update module fires it
// Its countdown shows on screen (InGameUI::addSuperweapon), unless its player lacked the power's science when it was
// added (m_hiddenByScience: never cleared) or a script hid it (m_hiddenByScript).
inline constexpr std::uint32_t PublicTimer = 1u << 3;
inline constexpr std::uint32_t HiddenByScience = 1u << 4;
inline constexpr std::uint32_t HiddenByScript = 1u << 5;
}

struct SpecialPowerTimer
{
	std::uint32_t power{0};
	std::uint32_t pausedCount{0};
	std::uint64_t availableOn{0}; // m_availableOnFrame (retail: 0 until something starts it)
	std::uint64_t pausedOn{0};
	std::int64_t pausedPercent{0}; // Fixed raw
	std::uint32_t flags{0};
	std::uint32_t publicOrder{0}; // with PublicTimer: its place among the countdowns shown (earlier first)
};

struct SpecialPowerTimers
{
	static constexpr std::uint32_t Capacity = 12;
	std::uint32_t count{0};
	std::uint32_t disabledSeen{0}; // the disabled types (but HELD) its countdowns are paused for
	std::array<SpecialPowerTimer, Capacity> timers{};

	SpecialPowerTimer *Find(std::uint32_t power) noexcept
	{
		for (std::uint32_t index = 0; index < count; ++index)
			if (timers[index].power == power)
				return &timers[index];
		return nullptr;
	}
	const SpecialPowerTimer *Find(std::uint32_t power) const noexcept { return const_cast<SpecialPowerTimers *>(this)->Find(power); }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::SpecialPowerTimers>
{
	static constexpr std::string_view StableName = "engine.gameplay.special_power_timers";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
