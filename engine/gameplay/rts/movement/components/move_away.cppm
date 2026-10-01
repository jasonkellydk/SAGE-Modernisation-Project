export module engine.gameplay.rts.movement.components.move_away;
import std;

export import engine.ecs.core.component_registry;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.navigation.components.navigation;

// A unit making way for another (AIUpdateInterface::privateMoveAwayFromUnit: the temporary AI_MOVE_OUT_OF_THE_WAY state,
// AIMoveOutOfTheWayState): for as long as it lasts it moves along its own route out of the way (`order` to the route's
// end), its own order held for after; it ends once there (STATE_SUCCESS), after `until` (the state's frame limit, ten
// seconds), or when it dies. Its order is then planned anew from where it got to (the state's exit destroys the path).
// `done`: it reached the route's end. Simulation state: checkpointed.
export namespace engine::gameplay
{
struct MoveAway
{
	MoveOrder order;
	Route route;
	std::uint64_t until{0};
	std::uint8_t done{0};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};

// AI_MOVE_OUT_OF_THE_WAY's frame limit: 10 * LOGICFRAMES_PER_SECOND.
inline constexpr std::uint64_t MoveAwayTicks = 300;
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::MoveAway>
{
	static constexpr std::string_view StableName = "engine.gameplay.move_away";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::MoveAway &value, StateHasher &hasher) noexcept
	{
		ComponentTraits<engine::gameplay::MoveOrder>::HashState(value.order, hasher);
		ComponentTraits<engine::gameplay::Route>::HashState(value.route, hasher);
		hasher.AppendU64(value.until);
		hasher.AppendU64(value.done);
	}
};
}
