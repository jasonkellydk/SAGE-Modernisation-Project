export module engine.gameplay.rts.docking.algorithms.dock_points;
import std;

export import engine.gameplay.rts.docking.components.dock;
export import engine.gameplay.common.spatial.algorithms.find_position;

// Where a docking mover goes (DockUpdate's positions), as pure functions of
// the dock, where it stands and where the mover is:
//   an approach point: the dock's own (a bone), or, with none, just outside
//   it toward the mover (half its major radius out from its centre), then
//   the first legal spot around that (findPositionAround out to 100: the
//   caller's legality, e.g. room on the mover's pathfinding plane), else the
//   point itself;
//   the entry, action and exit points: the dock's, or for a boneless dock
//   where the mover already is (entry only: a mover further than twice its
//   own radius from a dock that draws movers in is pulled in to touch it).
export namespace engine::gameplay
{
inline constexpr std::int64_t DockApproachSearchRadius = 100;

template<typename Legal>
Engine::Math::FixedVector2 ApproachPoint(const Dock &dock, Engine::Math::FixedVector3 dockPosition, std::uint32_t slot, Engine::Math::FixedVector3 moverPosition,
	Engine::Math::TurnAngle startAngle, Legal &&legal)
{
	using Engine::Math::Fixed;
	Engine::Math::FixedVector2 working = slot < Dock::MaxApproaches && slot < dock.approachPoints ? dock.approach[slot] : dockPosition.XY();
	if (dock.approachPoints == 0)
	{
		// The offset is taken in three dimensions (Vector3::Normalized_Legacy), then laid on the ground.
		const auto toward = Engine::Math::Normalize(moverPosition - dockPosition) * (dock.majorRadius / Fixed::FromInt(2));
		working += toward.XY();
	}
	if (const auto spot = FindPositionAround(working, Fixed{}, Fixed::FromInt(DockApproachSearchRadius), startAngle, legal))
		return *spot;
	return working;
}

// ThePartitionManager->getDistanceSquared(FROM_BOUNDINGSPHERE_2D): between the two bounding circles, never below zero.
inline Engine::Math::Fixed CircleGapSquared(Engine::Math::FixedVector2 a, Engine::Math::Fixed radiusA, Engine::Math::FixedVector2 b, Engine::Math::Fixed radiusB) noexcept
{
	using Engine::Math::Fixed;
	const Fixed gap = Engine::Math::Distance(a, b) - radiusA - radiusB;
	return gap > Fixed{} ? gap * gap : Fixed{};
}

inline Engine::Math::FixedVector2 EntryPoint(const Dock &dock, Engine::Math::FixedVector2 dockPosition, Engine::Math::Fixed dockRadius,
	Engine::Math::FixedVector2 moverPosition, Engine::Math::Fixed moverRadius) noexcept
{
	using Engine::Math::Fixed;
	if (!dock.boneless)
		return dock.enter;
	if (!dock.drawsIn || CircleGapSquared(moverPosition, moverRadius, dockPosition, dockRadius) <= (moverRadius * 2) * (moverRadius * 2))
		return moverPosition;
	const auto away = moverPosition - dockPosition;
	const Fixed distance = Engine::Math::Length(away);
	const Fixed target = dockRadius + moverRadius;
	if (distance > target && distance > Fixed{})
		return dockPosition + away * (target / distance);
	return moverPosition;
}

inline Engine::Math::FixedVector2 ActionPoint(const Dock &dock, Engine::Math::FixedVector2 moverPosition) noexcept
{
	return dock.boneless ? moverPosition : dock.action;
}

inline Engine::Math::FixedVector2 ExitPoint(const Dock &dock, Engine::Math::FixedVector2 moverPosition) noexcept
{
	return dock.boneless ? moverPosition : dock.exit;
}
}
