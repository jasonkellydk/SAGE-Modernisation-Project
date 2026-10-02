export module engine.gameplay.rts.navigation.algorithms.attack_view;
import std;

export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.rts.navigation.algorithms.view_blocked;

// An attacker's view of its victim as free functions over plain values (the callers gather them), for a looker that needs
// a line of sight (KINDOF_ATTACK_NEEDS_LINE_OF_SIGHT with AttackUsesLineOfSight on: the callers ask only for those):
// PartitionFilterLineOfSight, Pathfinder::isViewBlockedByObstacle and isAttackViewBlockedByObstacle. Here, below combat
// and movement, so both the attack's look and the attack path's search (findAttackPath) read them.
export namespace engine::gameplay
{
// A looker: where it stands (or would stand), the top of its geometry above that (getMaxHeightAbovePosition: its eyes,
// and its weapon's line-of-sight origin, Weapon::getFiringLineOfSightOrigin), whether its weapon's terrain check applies
// (it has a current weapon and is not IMMOBILE), and the cells it sees past first (3 on a bridge or a roof: a layer other
// than the ground).
struct SightEye
{
	Engine::Math::FixedVector3 at;
	Engine::Math::Fixed top;
	bool weaponTerrain{true};
	std::int32_t skipCount{0};
	// The layer its view's walk is on (isAttackViewBlockedByObstacle: the victim's, or its own when the victim is on the
	// ground and it is not); a deck: layer d at index d - 1 of the grid's decks.
	std::uint8_t layer{GroundLayer};
};

// isAttackViewBlockedByObstacle's walk on a deck's own cells (iterateCellsAlongLine on that layer): the walk ends clear off
// the deck (getCell null), and an obstacle cell on it blocks (a deck's cells hold no obstacle owners to pass over).
inline bool DeckViewBlocked(const NavigationGrid &grid, std::uint8_t layer, Engine::Math::FixedVector2 from, Engine::Math::FixedVector2 to,
	std::int32_t skipCount)
{
	if (layer == GroundLayer || layer > grid.Decks().size())
		return false;
	const DeckLayer &deck = grid.Decks()[layer - 1];
	std::int32_t x = WorldToCell(from.x), y = WorldToCell(from.y);
	const std::int32_t endX = WorldToCell(to.x), endY = WorldToCell(to.y);
	const std::int32_t deltaX = std::abs(endX - x), deltaY = std::abs(endY - y);
	std::int32_t xinc1 = endX >= x ? 1 : -1, xinc2 = xinc1, yinc1 = endY >= y ? 1 : -1, yinc2 = yinc1;
	std::int32_t den, num, numadd, numpixels;
	if (deltaX >= deltaY)
	{
		xinc1 = 0;
		yinc2 = 0;
		den = deltaX;
		num = deltaX / 2;
		numadd = deltaY;
		numpixels = deltaX;
	}
	else
	{
		xinc2 = 0;
		yinc1 = 0;
		den = deltaY;
		num = deltaY / 2;
		numadd = deltaX;
		numpixels = deltaY;
	}
	// One cell: off the deck the walk is over (clear); an obstacle cell past the skipped ones blocks.
	const auto visit = [&](std::int32_t cx, std::int32_t cy, bool &stop) {
		if (!deck.Contains(cx, cy))
		{
			stop = true;
			return false;
		}
		if (skipCount > 0)
		{
			--skipCount;
			return false;
		}
		return deck.type[deck.Index(cx, cy)] == PathfindCellType::Obstacle;
	};
	for (std::int32_t pixel = 0; pixel <= numpixels; ++pixel)
	{
		bool stop = false;
		if (visit(x, y, stop))
			return true;
		if (stop)
			return false;
		num += numadd;
		if (num >= den)
		{
			num -= den;
			x += xinc1;
			y += yinc1;
			if (visit(x, y, stop))
				return true;
			if (stop)
				return false;
		}
		x += xinc2;
		y += yinc2;
	}
	return false;
}

// What it looks at: where it stands, its top (getMaxHeightAbovePosition) and the height of its centre
// (GeometryInfo::getCenterPosition).
struct SightTarget
{
	Engine::Math::FixedVector3 at;
	Engine::Math::Fixed top;
	Engine::Math::Fixed centre;
};

// isAttackViewBlockedByObstacle's skipCount: a looker on a layer other than the ground sees 3 cells out of whatever it
// stands on.
inline constexpr std::int32_t RaisedLookerSkipCells = 3;

// Pathfinder::isAttackViewBlockedByObstacle (the caller has settled AttackUsesLineOfSight and
// KINDOF_ATTACK_NEEDS_LINE_OF_SIGHT): with its weapon's terrain check (LOS_TERRAIN: isClearGoalFiringLineOfSightTerrain from
// its top to the victim's centre), then the obstacle cells between them.
inline bool AttackViewBlocked(const GroundHeight &ground, const NavigationGrid &grid, const SightEye &eye, const SightTarget &victim, const ViewIgnores &ignore)
{
	if (eye.weaponTerrain)
	{
		const Engine::Math::FixedVector3 origin{eye.at.x, eye.at.y, eye.at.z + eye.top};
		const Engine::Math::FixedVector3 centre{victim.at.x, victim.at.y, victim.at.z + victim.centre};
		if (!ground.ClearLineOfSight(origin, centre))
			return true;
	}
	if (eye.layer != GroundLayer)
		return DeckViewBlocked(grid, eye.layer, eye.at.XY(), victim.at.XY(), eye.skipCount);
	return AttackViewBlockedByObstacle(grid, eye.at.XY(), victim.at.XY(), ignore, eye.skipCount);
}

// Pathfinder::isViewBlockedByObstacle: never for one significantly above the ground (Thing::isSignificantlyAboveTerrain:
// higher than three frames' fall).
inline bool ViewBlocked(const GroundHeight &ground, const NavigationGrid &grid, Engine::Math::Fixed significantHeight, const SightEye &eye,
	const SightTarget &victim, const ViewIgnores &ignore)
{
	if (victim.at.z - ground.At(victim.at.XY()) > significantHeight)
		return false;
	return AttackViewBlocked(ground, grid, eye, victim, ignore);
}

// PartitionFilterLineOfSight::allow: the terrain clear from top to top (PartitionManager::isClearLineOfSightTerrain), and
// the view not blocked by an obstacle (isViewBlockedByObstacle).
inline bool LineOfSightClear(const GroundHeight &ground, const NavigationGrid &grid, Engine::Math::Fixed significantHeight, const SightEye &eye,
	const SightTarget &victim, const ViewIgnores &ignore)
{
	const Engine::Math::FixedVector3 from{eye.at.x, eye.at.y, eye.at.z + eye.top};
	const Engine::Math::FixedVector3 to{victim.at.x, victim.at.y, victim.at.z + victim.top};
	if (!ground.ClearLineOfSight(from, to))
		return false;
	return !ViewBlocked(ground, grid, significantHeight, eye, victim, ignore);
}
}
