export module engine.gameplay.rts.movement.components.move_order;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// Where a mobile entity is going: nowhere, a point (routed around
// obstacles), straight to a point (nothing routed: into a dock), along a
// waypoint path (from `waypoint`, taking a random link at each waypoint
// until one has none), or turning to face a point where it stands (AIFaceState).
export namespace engine::gameplay
{
enum class MoveMode : std::uint8_t
{
	Idle,
	Point,
	Path,
	Direct,
	Face,
	// Along a waypoint path exactly (AIFollowWaypointPathExactState: setPathFromWaypoint, each waypoint's first link).
	PathExact,
	// Along a waypoint path as Path, each waypoint's goal off it by a random whole number of cells either way, up to its
	// locomotor's WanderWidthFactor rounded (at least one; none when it has none): AIWanderState, AIPanicState.
	Wander,
	// About its WanderAnchor for good: each goal reached, the next is off the anchor by a random whole number of cells
	// either way, up to its locomotor's WanderAboutPointRadius in cells rounded (AIWanderInPlaceState).
	WanderInPlace,
	// As Wander, panicking (AIPanicState: its PANICKING look while it lasts).
	Panic,
	// As Face, the point where its FaceTarget is now (AIFaceState with a goal object: aiFaceObject).
	FaceObject,
};

// Along a path that is wandered (Wander, Panic).
inline bool Wandering(MoveMode mode) noexcept { return mode == MoveMode::Wander || mode == MoveMode::Panic; }

// Moves whose goals a mover with a navigation agent is routed to (AIInternalMoveToState): a point, and each leg of a
// waypoint path (AIFollowWaypointPathState, its wanders and panics) or of a wander in place. An exact path is not routed
// (AIFollowWaypointPathExactState: setPathFromWaypoint).
inline bool RoutedMode(MoveMode mode) noexcept
{
	return mode == MoveMode::Point || mode == MoveMode::Path || Wandering(mode) || mode == MoveMode::WanderInPlace;
}

// A path waypoint's or wander point's offset in whole cells either way (m_groupOffset: GameLogicRandomValue(-delta,
// delta) * PATHFIND_CELL_SIZE_F on each axis).
template<typename Uniform>
Engine::Math::FixedVector2 WanderOffset(std::int64_t delta, std::int64_t cellSize, Uniform &&uniform)
{
	const std::int64_t x = uniform(-delta, delta);
	const std::int64_t y = uniform(-delta, delta);
	return {Engine::Math::Fixed::FromInt(x * cellSize), Engine::Math::Fixed::FromInt(y * cellSize)};
}

// The cells either way a wander along a path goes off each waypoint: floor(WanderWidthFactor + 0.5), at least 1; 0 when
// the locomotor has no width.
inline std::int64_t PathWanderCells(Engine::Math::Fixed width) noexcept
{
	if (width <= Engine::Math::Fixed{})
		return 0;
	return std::max<std::int64_t>((width + Engine::Math::Fixed::FromRatio(1, 2)).Floor(), 1);
}

// The cells either way a wander in place goes off its point: floor(WanderAboutPointRadius / cell + 0.5).
inline std::int64_t PointWanderCells(Engine::Math::Fixed radius, std::int64_t cellSize) noexcept
{
	return (radius / Engine::Math::Fixed::FromInt(cellSize) + Engine::Math::Fixed::FromRatio(1, 2)).Floor();
}

// How a move to a point claims its goal (AIInternalMoveToState's m_adjustDestinations; AIFollowPathState's legs):
//   Adjust: the goal is moved off cells others claim (Pathfinder::adjustDestination) and claimed (updateGoal);
//   Keep: the goal as ordered, the route's end claimed once planned (a factory's lone exit point: adjustment off at
//     its start, on for the path it gets);
//   None: a leg on the way; its claim is let go once its route is planned (removeGoal).
enum class GoalClaim : std::uint8_t
{
	Adjust,
	Keep,
	None,
};

struct MoveOrder
{
	Engine::Math::FixedVector2 destination;
	std::uint32_t waypoint{0xFFFFFFFFu};
	MoveMode mode{MoveMode::Idle};
	// Held where it is for now (AIUpdateInterface::setLocomotorGoalNone while busy: packing or unpacking), its order
	// kept for after: it brakes to a stop. Whoever holds it sets this each tick.
	std::uint8_t held{0};
	GoalClaim claim{GoalClaim::Adjust};
	// A goal its AI set straight on its locomotor (setLocomotorGoalPositionExplicit: POSITION_EXPLICIT): moved toward with
	// nothing left to go (onPathDistToGoal 0), never ended by the movement (its state judges arrival).
	std::uint8_t explicitGoal{0};
	// The layer its goal is on, when the order says (AIFollowWaypointPathState's m_goalLayer: a waypoint's goal is on the
	// ground, or on the wall where the waypoint is on it; Pathfinder::findPath's destinationLayer); NoGoalLayer: the
	// route finds the layer from where it stands and where it goes (RouteGoalLayer).
	std::uint8_t goalLayer{0xFF};
	// A new order its route must be planned for at once (the original's requestPath on every new move; the house rule
	// "a system writing MoveOrder clears Route.planned", for writers that cannot reach the Route): the route requests plan
	// it on their next run whatever their re-plan wait, and the movement lets it go once a route is planned for it.
	std::uint8_t replan{0};
	std::uint8_t reserved[6]{}; // no padding: checkpoints hold its bytes
};

// `order` as a new order whose route is planned at once (MoveOrder::replan).
inline MoveOrder Replanned(MoveOrder order) noexcept
{
	order.replan = 1;
	return order;
}

inline constexpr std::uint8_t NoGoalLayer = 0xFF;

inline MoveOrder MoveToPoint(Engine::Math::FixedVector2 destination, GoalClaim claim = GoalClaim::Adjust) noexcept
{
	MoveOrder order{destination, 0xFFFFFFFFu, MoveMode::Point};
	order.claim = claim;
	return order;
}

// A move to a point whose goal is on `layer` (the ground, a deck or the wall).
inline MoveOrder MoveToPointOn(Engine::Math::FixedVector2 destination, std::uint8_t layer, GoalClaim claim = GoalClaim::Adjust) noexcept
{
	MoveOrder order = MoveToPoint(destination, claim);
	order.goalLayer = layer;
	return order;
}

inline MoveOrder MoveStraightTo(Engine::Math::FixedVector2 destination) noexcept
{
	return {destination, 0xFFFFFFFFu, MoveMode::Direct};
}

// aiFacePosition / aiFaceObject: turn to face `point`, then idle.
inline MoveOrder FaceToward(Engine::Math::FixedVector2 point) noexcept
{
	return {point, 0xFFFFFFFFu, MoveMode::Face};
}
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::MoveOrder>
{
	static constexpr std::string_view StableName = "engine.gameplay.move_order";
	static constexpr std::uint32_t Version = 3;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::MoveOrder &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.destination.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.destination.y.Raw()));
		hasher.AppendU64(value.waypoint);
		hasher.AppendU64(static_cast<std::uint64_t>(value.mode) | static_cast<std::uint64_t>(value.claim) << 8 |
			static_cast<std::uint64_t>(value.explicitGoal) << 16 | static_cast<std::uint64_t>(value.goalLayer) << 24 |
			static_cast<std::uint64_t>(value.replan) << 32);
	}
};
}
