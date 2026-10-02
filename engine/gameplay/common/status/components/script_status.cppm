export module engine.gameplay.common.status.components.script_status;
import std;

export import engine.ecs.core.component_registry;

// What scripts and map properties set on an object beyond its disabled types (the original's ObjectScriptStatusBits and
// the flags beside them): unsellable, targetable by a player's order whatever the relationship (not an ally), no longer
// recruitable by its AI player's teams (AIUpdateInterface::setIsRecruitable(false)), and whether it may be selected when
// that was set outright (Object::setSelectable), and kept from stealth (NAMED_SET_STEALTH_ENABLED). Simulation state: checkpointed.
export namespace engine::gameplay
{
namespace script_status
{
inline constexpr std::uint8_t Unsellable = 1u << 0;
inline constexpr std::uint8_t Targetable = 1u << 1;
inline constexpr std::uint8_t NotRecruitable = 1u << 2;
inline constexpr std::uint8_t SelectableSet = 1u << 3;   // setSelectable was called
inline constexpr std::uint8_t SelectableValue = 1u << 4; // what it was set to
inline constexpr std::uint8_t Unstealthed = 1u << 5;     // OBJECT_STATUS_SCRIPT_UNSTEALTHED: it may not stealth
}

struct ScriptStatus
{
	std::uint8_t bits{0};
	std::uint8_t reserved[3]{};

	bool Has(std::uint8_t bit) const noexcept { return (bits & bit) != 0; }
	void Set(std::uint8_t bit, bool on) noexcept { bits = on ? static_cast<std::uint8_t>(bits | bit) : static_cast<std::uint8_t>(bits & ~bit); }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::ScriptStatus>
{
	static constexpr std::string_view StableName = "engine.gameplay.script_status";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
