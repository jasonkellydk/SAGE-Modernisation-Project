export module engine.gameplay.common.appearance.components.special_model_state;
import std;

export import engine.ecs.core.component_registry;

// A special model condition state held for a while (the original's Object::setSpecialModelConditionState: m_smcUntil and
// the ObjectSMCHelper that wakes then): the condition bit it set, and the tick it is taken off again. One at a time.
export namespace engine::gameplay
{
struct SpecialModelState
{
	std::uint64_t until{0};
	std::uint32_t bit{0};
	std::uint32_t reserved{0}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::SpecialModelState>
{
	static constexpr std::string_view StableName = "engine.gameplay.special_model_state";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::SpecialModelState &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.until);
		hasher.AppendU64(value.bit);
	}
};
}
