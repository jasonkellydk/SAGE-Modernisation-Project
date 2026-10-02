export module engine.gameplay.rts.movement.components.move_ended;
import std;

export import engine.ecs.core.component_registry;

// The tick its last move ended, the movement system bringing its order to idle (the move or follow-path state
// succeeding: AIIdleState::onEnter on that frame). A side table added on its first ended move. Simulation state:
// checkpointed.
export namespace engine::gameplay
{
struct MoveEnded
{
	std::uint64_t tick{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::MoveEnded>
{
	static constexpr std::string_view StableName = "engine.gameplay.move_ended";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::MoveEnded &value, StateHasher &hasher) noexcept { hasher.AppendU64(value.tick); }
};
}
