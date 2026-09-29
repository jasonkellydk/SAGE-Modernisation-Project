export module engine.gameplay.common.status.components.disabled;
import std;

export import engine.ecs.core.component_registry;

// What disables an entity (the original's DisabledMaskType, in its bit
// order). A disabled entity's behaviours run only where they process one of
// its disabled types (UpdateModule::getDisabledTypesToProcess; GameLogic runs
// a module if the entity is not disabled or any of its types is allowed):
// its AI (movement, targeting, weapons, turrets), stealth and self-healing
// only while HELD; physics and dying always; most other updates never.
export namespace engine::gameplay
{
namespace disabled_type
{
inline constexpr std::uint32_t Default = 1u << 0;
inline constexpr std::uint32_t Hacked = 1u << 1;
inline constexpr std::uint32_t Emp = 1u << 2;
inline constexpr std::uint32_t Held = 1u << 3;
inline constexpr std::uint32_t Paralyzed = 1u << 4;
inline constexpr std::uint32_t Unmanned = 1u << 5;
inline constexpr std::uint32_t Underpowered = 1u << 6;
inline constexpr std::uint32_t Freefall = 1u << 7;
inline constexpr std::uint32_t Awestruck = 1u << 8;
inline constexpr std::uint32_t Brainwashed = 1u << 9;
inline constexpr std::uint32_t Subdued = 1u << 10;
inline constexpr std::uint32_t ScriptDisabled = 1u << 11;
inline constexpr std::uint32_t ScriptUnderpowered = 1u << 12;
inline constexpr std::uint32_t None = 0;
inline constexpr std::uint32_t All = (1u << 13) - 1;
}

struct Disabled
{
	std::uint32_t mask{0};
};

// GameLogic's gate: a behaviour processing `allowed` runs on this entity.
inline bool RunsWhileDisabled(const Disabled &disabled, std::uint32_t allowed) noexcept
{
	return disabled.mask == 0 || (disabled.mask & allowed) != 0;
}
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Disabled>
{
	static constexpr std::string_view StableName = "engine.gameplay.disabled";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
