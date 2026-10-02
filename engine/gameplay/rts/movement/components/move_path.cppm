export module engine.gameplay.rts.movement.components.move_path;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// Points still to go to after the current move (the original's goal path): its points are a block of PathPoints
// (`block`, `count` of them, as many as it needs), `next` the next to head for. Which state follows it: a path the AI
// follows (AI_FOLLOW_PATH: a player's waypoints, a group's columns) or a way out of a factory
// (AI_FOLLOW_EXITPRODUCTION_PATH: the waypoint display never shows it). The move path system starts the next point once
// its move order is done.
export namespace engine::gameplay
{
enum class MovePathKind : std::uint8_t
{
	Follow,         // AI_FOLLOW_PATH (aiFollowPath, aiFollowPathAppend)
	ExitProduction, // AI_FOLLOW_EXITPRODUCTION_PATH (aiFollowExitProductionPath)
};

struct MovePath
{
	std::uint32_t block{0xFFFFFFFFu};
	std::uint32_t count{0};
	std::uint32_t next{0};
	MovePathKind kind{MovePathKind::Follow};
	std::uint8_t reserved[3]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::MovePath>
{
	static constexpr std::string_view StableName = "engine.gameplay.move_path";
	static constexpr std::uint32_t Version = 3; // 3: its points in PathPoints
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
