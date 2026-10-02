export module engine.gameplay.rts.blocking.resources.move_away_requests;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
import engine.ecs.system.system;

// Units asked to make way for another this tick (AIUpdateInterface::aiMoveAwayFromUnit: infantry in a vehicle's way, the
// lower path priority of two units deadlocked, allies standing on a new route: Pathfinder::moveAllies), each slot in its
// chunk's order, taken in that order by whoever moves them.
export namespace engine::gameplay
{
struct MoveAwayRequest
{
	ecs::Entity mover; // the unit to make way
	ecs::Entity from;  // the unit it makes way for
};

struct MoveAwayRequests : ecs::ChunkOutputs<MoveAwayRequest>
{
};

// Idle units found standing on top of another unit that is not moving (processCollision, not moving: within half a cell
// of each other): each moves to a spot it may stand on (adjustToPossibleDestination, aiMoveToPosition from its AI).
struct UnitSettles : ecs::ChunkOutputs<ecs::Entity>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::MoveAwayRequests>
{
	static constexpr std::string_view StableName = "engine.gameplay.move_away_requests";
};

template<>
struct ResourceTraits<engine::gameplay::UnitSettles>
{
	static constexpr std::string_view StableName = "engine.gameplay.unit_settles";
};
}
