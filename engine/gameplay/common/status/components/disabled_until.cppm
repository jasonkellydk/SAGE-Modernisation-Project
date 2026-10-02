export module engine.gameplay.common.status.components.disabled_until;
import std;

export import engine.ecs.core.entity;
export import engine.gameplay.common.status.components.disabled;
import engine.ecs.core.component_registry;
import engine.ecs.system.system;

// Disables that run out (the original's Object::setDisabledUntil / m_disabledTillFrame): per disabled type (its bit's
// index), the tick it ends (0: not timed; a type set without an end lasts until cleared). DisableRequests: this
// tick's timed disables to give (entity, disabled type bit, the tick it ends), applied in order; a later one for the
// same type replaces the earlier end (setDisabledUntil overwrites); one marked `clear` takes the type away again
// (clearDisabled). Simulation state: checkpointed.
export namespace engine::gameplay
{
inline constexpr std::size_t DisabledTypeCount = 13;
// setDisabled: a disable without an end (FOREVER), told apart from none.
inline constexpr std::uint64_t DisabledForever = ~std::uint64_t{0};

struct DisabledUntil
{
	std::array<std::uint64_t, DisabledTypeCount> until{};
};

struct DisableRequest
{
	ecs::Entity entity;
	std::uint32_t type{0}; // a disabled_type bit
	std::uint32_t clear{0};
	std::uint64_t until{0};
};

struct DisableRequests
{
	std::vector<DisableRequest> list;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::DisabledUntil>
{
	static constexpr std::string_view StableName = "engine.gameplay.disabled_until";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<engine::gameplay::DisableRequests>
{
	static constexpr std::string_view StableName = "engine.gameplay.disable_requests";
};
}
