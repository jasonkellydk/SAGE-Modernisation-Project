export module engine.gameplay.rts.movement.components.move_goal;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// Where a move to a point really ends (AIInternalMoveToState's m_goalPosition after Pathfinder::adjustDestination: the
// ordered point moved off cells an ally already claims, onto a cell centre), for the order's point `ordered`. Routes
// are planned to `goal` while the order is still for `ordered`. Simulation state: checkpointed.
export namespace engine::gameplay
{
struct MoveGoal
{
	Engine::Math::FixedVector2 ordered;
	Engine::Math::FixedVector2 goal;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::MoveGoal>
{
	static constexpr std::string_view StableName = "engine.gameplay.move_goal";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::MoveGoal &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.ordered.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.ordered.y.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.goal.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.goal.y.Raw()));
	}
};
}
