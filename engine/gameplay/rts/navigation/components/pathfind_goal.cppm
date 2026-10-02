export module engine.gameplay.rts.navigation.components.pathfind_goal;
import std;

export import engine.ecs.core.component_registry;

// The cell a ground mover's goal claim is centred on (AIUpdateInterface's m_pathfindGoalCell; -1: none): the cells
// about it that its footprint covers are its in GoalCells, and come out when it claims elsewhere or lets go.
// Simulation state: checkpointed.
export namespace engine::gameplay
{
struct PathfindGoal
{
	std::int32_t x{-1}, y{-1};

	bool Claimed() const noexcept { return x >= 0 && y >= 0; }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::PathfindGoal>
{
	static constexpr std::string_view StableName = "engine.gameplay.pathfind_goal";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::PathfindGoal &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64((static_cast<std::uint64_t>(static_cast<std::uint32_t>(value.x)) << 32) | static_cast<std::uint32_t>(value.y));
	}
};
}
