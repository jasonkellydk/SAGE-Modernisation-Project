export module engine.gameplay.rts.blocking.algorithms.blocking_rules;
import std;

export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;
export import engine.gameplay.rts.blocking.components.blocking_unit;

// The rules a ground unit's AI weighs a unit in its way by (AIUpdate.cpp, EA's Zero Hour source): pure functions over
// a snapshot of each unit as the tick left it.
export namespace engine::gameplay
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;
using Engine::Math::TurnAngle;

// A unit as the blocking rules see it.
struct BlockingBody
{
	FixedVector2 position;
	FixedVector2 direction; // getUnitDirectionVector2D: (cos, sin) of its facing
	TurnAngle facing;
	FixedVector2 goal;      // its state machine's goal position (where it was ordered)
	Fixed speed;            // how fast it goes (its physics' velocity, on the ground: along its facing)
	std::uint32_t id{0};    // its ObjectID (ties go to the older)
	std::uint32_t frames{0}; // getNumFramesBlocked
	std::uint8_t kinds{0};  // blocking_kind
	bool moving{false};     // its locomotor has a goal (m_locomotorGoalType != NONE)
	bool ground{false};     // isDoingGroundMovement
	bool dead{false};       // isAiInDeadState
	bool backwards{false};  // its locomotor isMovingBackwards
	bool goalCell{false};   // it holds a pathfind goal cell (getPathfindGoalCell x and y above 0)
	bool waiting{false};    // isWaitingForPath
	bool wanderer{false};   // its locomotor's WanderWidthFactor is above 0
	bool panicking{false};  // in AI_PANIC
	std::uint32_t formation{0}; // its user formation (getFormationID; 0: none)
};

namespace blocking_detail
{
inline constexpr std::int64_t HalfPi = std::int64_t{1} << 30;             // PI/2 in turn units
inline constexpr std::int64_t QuarterPi = std::int64_t{1} << 29;          // PI/4
inline constexpr std::int64_t ThirtiethPi = (std::int64_t{1} << 32) / 60; // PI/30
inline constexpr std::int64_t CellSize = 10;                              // PATHFIND_CELL_SIZE_F

inline std::int64_t Magnitude(std::int64_t angle) noexcept { return angle < 0 ? -angle : angle; }
}

inline FixedVector2 UnitDirection(TurnAngle facing) noexcept { return {Engine::Math::Cos(facing), Engine::Math::Sin(facing)}; }

// PartitionManager::getRelativeAngle2D: from its facing to the way to `point`, -PI to PI, positive to its left; 0 when
// it stands on it.
inline std::int64_t RelativeAngle(const BlockingBody &body, FixedVector2 point) noexcept
{
	const FixedVector2 to = point - body.position;
	if (to.x == Fixed{} && to.y == Fixed{})
		return 0;
	return Engine::Math::DeltaTo(body.facing, Engine::Math::Heading(to));
}

// AIUpdateInterface::hasHigherPathPriority: a dozer goes before anything else, a vehicle before infantry; otherwise, going
// opposite ways, the older unit; going the same way, the one ahead (along their two ways together), ties to the older.
inline bool HasHigherPathPriority(const BlockingBody &self, const BlockingBody &other) noexcept
{
	using namespace blocking_detail;
	const bool selfDozer = (self.kinds & blocking_kind::Dozer) != 0, otherDozer = (other.kinds & blocking_kind::Dozer) != 0;
	if (selfDozer != otherDozer)
		return selfDozer;
	if ((self.kinds & blocking_kind::Vehicle) != 0 && (other.kinds & blocking_kind::Infantry) != 0)
		return true;
	if ((self.kinds & blocking_kind::Infantry) != 0 && (other.kinds & blocking_kind::Vehicle) != 0)
		return false;
	if (Engine::Math::Dot(self.direction, other.direction) <= Fixed{})
		return self.id < other.id;
	const FixedVector2 combined = self.direction + other.direction;
	const Fixed ahead = Engine::Math::Dot(combined, other.position - self.position);
	if (ahead > Fixed{})
		return false;
	if (ahead < Fixed{})
		return true;
	return self.id < other.id;
}

// AIUpdateInterface::calculateMaxBlockedSpeed: the fastest it may go behind `other` without closing on it (how fast the
// other goes away from it over how much of its own way points at the other), no faster than `current`; 0 when the other
// comes at it; behind one of its own formation (both in it), x 0.55: formations do not crowd each other.
inline Fixed MaxBlockedSpeed(const BlockingBody &self, const BlockingBody &other, Fixed current) noexcept
{
	using namespace blocking_detail;
	FixedVector2 toOther = other.position - self.position;
	if (const Fixed length = Engine::Math::Length(toOther); length > Fixed{})
		toOther = FixedVector2{toOther.x / length, toOther.y / length};
	const Fixed away = Engine::Math::Dot(toOther, other.direction);
	if (away < Fixed{})
		return {};
	const Fixed awaySpeed = Engine::Math::Abs(other.speed) * away;
	const Fixed toward = Engine::Math::Dot(toOther, self.direction);
	if (toward <= Fixed{})
		return current;
	Fixed most = awaySpeed / toward;
	if (other.formation != 0 && self.formation == other.formation)
		most = most * Fixed::FromRatio(55, 100);
	return most > current ? current : most;
}

// AIUpdateInterface::needToRotate: waiting for a route it will likely turn; a wanderer never needs to; otherwise whether
// the point it heads for along its route is more than PI/30 off its facing. (computePointOnPath's point is the point
// its locomotor steers at: in the port, its route's next point.)
inline bool NeedToRotate(const BlockingBody &body, FixedVector2 pointOnPath) noexcept
{
	if (body.waiting)
		return true;
	if (body.wanderer)
		return false;
	return blocking_detail::Magnitude(RelativeAngle(body, pointOnPath)) > blocking_detail::ThirtiethPi;
}

// AIUpdateInterface::blockedBy: whether `other` keeps `self` from going on. Never near its own goal (within a cell either
// way, holding a goal cell), nor by what it crushes or squishes (`canCrush`), nor by something not on the ground, nor while
// it backs up; infantry crossing infantry passes through; two on one spot: the higher path priority is blocked; blocked
// over a second, it passes through what goes another way; it is not blocked by what is behind it (over PI/2 off its
// facing), nor by what is off to its side (over PI/4, or 3/4 of that when the other stands) unless both are heading into
// each other and it gives way; and never by the dead.
inline bool BlockedBy(const BlockingBody &self, const BlockingBody &other, bool canCrush) noexcept
{
	using namespace blocking_detail;
	if (self.goalCell)
	{
		const Fixed cell = Fixed::FromInt(CellSize);
		if (Engine::Math::Abs(self.goal.x - self.position.x) < cell && Engine::Math::Abs(self.goal.y - self.position.y) < cell)
			return false;
	}
	if (canCrush)
		return false;
	if (!other.ground)
		return false;
	if (self.backwards)
		return false;
	const bool otherMoving = other.moving;
	Fixed dx = self.position.x - other.position.x;
	Fixed dy = self.position.y - other.position.y;
	const Fixed distanceSquared = dx * dx + dy * dy;
	const Fixed dot = Engine::Math::Dot(self.direction, other.direction);
	if ((self.kinds & blocking_kind::Infantry) != 0 && (other.kinds & blocking_kind::Infantry) != 0 && dot <= Fixed::FromRatio(1, 4))
		return false;
	// PATHFIND_CELL_SIZE_F^2 x 0.0001.
	if (distanceSquared < Fixed::FromRatio(CellSize * CellSize, 10000))
		return HasHigherPathPriority(self, other);
	if (self.frames > 30 && dot <= Fixed{})
		return false;
	const std::int64_t collisionAngle = Magnitude(RelativeAngle(self, other.position));
	const std::int64_t otherAngle = Magnitude(RelativeAngle(other, self.position));
	if (collisionAngle > HalfPi)
		return false;
	const std::int64_t limit = otherMoving ? QuarterPi : QuarterPi * 3 / 4;
	if (collisionAngle > limit)
	{
		if (dot <= Fixed{})
			return false;
		if (!otherMoving || otherAngle <= limit)
			return false;
		// Running into each other: a step on, are they nearer?
		dx += self.direction.x - other.direction.x;
		dy += self.direction.y - other.direction.y;
		if (distanceSquared <= dx * dx + dy * dy)
			return false;
		if (HasHigherPathPriority(self, other))
			return false;
	}
	return !other.dead;
}

// Object::canCrushOrSquish (TEST_CRUSH_OR_SQUISH): never unmanned, never what it counts an ally, never without a crusher
// level; then what squishes (SquishCollide) or has a lower crushable level.
inline bool CanCrushOrSquish(std::uint32_t crusherLevel, bool unmanned, bool allies, bool otherSquishable, std::uint32_t otherCrushableLevel) noexcept
{
	if (unmanned || allies || crusherLevel == 0)
		return false;
	return otherSquishable || crusherLevel > otherCrushableLevel;
}
}
