export module engine.gameplay.common.spatial.resources.deck_surfaces;
import std;

export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;
export import engine.ecs.core.entity;
export import engine.ecs.core.component_registry;
export import engine.gameplay.common.spatial.resources.ground_height;
import engine.ecs.system.system;

// Surfaces above the ground things may stand on (the original's bridge layers, as TerrainLogic knows them): each deck's
// rectangle, its ends `from` and `to` and its four corners at deck height, by layer (layer d at index d - 1; the ground
// is layer 0). What stands on one says so (SurfaceLayer). Where a thing on a deck stands over it, its floor is the
// deck (TerrainLogic::getLayerHeight); elsewhere the ground. One layer may be the wall (the original's LAYER_WALL): a
// surface at one height (AIData WallHeight) over its pieces, the cells of the wall layer that are clear (WallSurface).
export namespace engine::gameplay
{
// LAYER_GROUND.
inline constexpr std::uint8_t GroundLayer = 0;

struct DeckGeometry
{
	Engine::Math::FixedVector3 from;
	Engine::Math::FixedVector3 to;
	Engine::Math::FixedVector3 fromLeft;
	Engine::Math::FixedVector3 fromRight;
	Engine::Math::FixedVector3 toLeft;
	Engine::Math::FixedVector3 toRight;
};

// The wall (Pathfinder's m_wallPieces / m_wallHeight and its LAYER_WALL cells): its layer (0: none; its entry in `decks`
// is a placeholder), its height, its pieces structure of arrays (each its object, where it stands and how it is turned,
// its footprint's half sizes: GeometryInfo's major and minor radius, the major both ways for a sphere; `valid` 0 once
// it fell to rubble: the original's INVALID_ID), and the wall layer's cells (pathfinding cells, x0..x0+columns,
// y0..y0+rows) clear for walking on top (Pathfinder::isPointOnWall).
struct WallSurface
{
	std::uint8_t layer{0};
	Engine::Math::Fixed height;
	std::vector<ecs::Entity> owner;
	std::vector<Engine::Math::FixedVector2> position;
	std::vector<Engine::Math::TurnAngle> facing;
	std::vector<Engine::Math::Fixed> major, minor;
	std::vector<std::uint8_t> valid;
	std::int32_t x0{0}, y0{0}, columns{0}, rows{0};
	std::vector<std::uint8_t> clear;

	std::size_t Pieces() const noexcept { return owner.size(); }
	void AddPiece(ecs::Entity entity, Engine::Math::FixedVector2 at, Engine::Math::TurnAngle turn, Engine::Math::Fixed halfX, Engine::Math::Fixed halfY,
		bool isValid = true)
	{
		owner.push_back(entity);
		position.push_back(at);
		facing.push_back(turn);
		major.push_back(halfX);
		minor.push_back(halfY);
		valid.push_back(isValid ? 1u : 0u);
	}
	void ClearPieces() noexcept
	{
		owner.clear();
		position.clear();
		facing.clear();
		major.clear();
		minor.clear();
		valid.clear();
	}
};

struct DeckSurfaces
{
	std::vector<DeckGeometry> decks;
	WallSurface wall;
};

// PathfindLayer::isPointOnWall for one piece: the point, turned into the piece's frame, within its half sizes.
inline bool PointOnWallPiece(const WallSurface &wall, std::size_t piece, Engine::Math::FixedVector2 at)
{
	// cos(-angle), sin(-angle).
	const Engine::Math::Fixed c = Engine::Math::Cos(wall.facing[piece]);
	const Engine::Math::Fixed s = -Engine::Math::Sin(wall.facing[piece]);
	const Engine::Math::Fixed dx = at.x - wall.position[piece].x, dy = at.y - wall.position[piece].y;
	const Engine::Math::Fixed x = Engine::Math::Abs(dx * c - dy * s);
	const Engine::Math::Fixed y = Engine::Math::Abs(dx * s + dy * c);
	return x <= wall.major[piece] && y <= wall.minor[piece];
}

// PathfindLayer::isPointOnWall: on any piece still standing.
inline bool PointOnWallPieces(const WallSurface &wall, Engine::Math::FixedVector2 at)
{
	for (std::size_t piece = 0; piece < wall.Pieces(); ++piece)
		if (wall.valid[piece] != 0 && PointOnWallPiece(wall, piece, at))
			return true;
	return false;
}

// Pathfinder::isPointOnWall: the wall layer's cell under the point is clear.
inline bool PointOnWall(const DeckSurfaces &surfaces, Engine::Math::FixedVector2 at)
{
	const WallSurface &wall = surfaces.wall;
	if (wall.layer == 0 || wall.Pieces() == 0 || wall.columns <= 0)
		return false;
	const Engine::Math::Fixed cell = Engine::Math::Fixed::FromInt(10);
	const auto x = static_cast<std::int32_t>((at.x / cell).Floor()) - wall.x0;
	const auto y = static_cast<std::int32_t>((at.y / cell).Floor()) - wall.y0;
	if (x < 0 || y < 0 || x >= wall.columns || y >= wall.rows)
		return false;
	return wall.clear[static_cast<std::size_t>(y) * static_cast<std::size_t>(wall.columns) + static_cast<std::size_t>(x)] != 0;
}

inline bool IsWallLayer(const DeckSurfaces &surfaces, std::uint8_t layer) noexcept { return layer != GroundLayer && layer == surfaces.wall.layer; }

// AIFollowWaypointPathState::computeGoal's wall case: a waypoint on the wall is a goal on the wall (LAYER_WALL, at the
// wall's height), and a member's offset goal that is off the wall falls back to the waypoint itself. Off the wall, the
// offset goal as it is.
// computeGoal's m_goalLayer: waypoints lie on the ground (TerrainLogic::addWaypoint snaps them to it), so a waypoint's
// goal is on the ground, or on the wall for a waypoint on it.
inline std::uint8_t WaypointGoalLayer(const DeckSurfaces &surfaces, Engine::Math::FixedVector2 waypoint)
{
	return surfaces.wall.layer != 0 && PointOnWall(surfaces, waypoint) ? surfaces.wall.layer : GroundLayer;
}

inline Engine::Math::FixedVector2 WaypointGoalOnWall(const DeckSurfaces &surfaces, Engine::Math::FixedVector2 waypoint, Engine::Math::FixedVector2 goal)
{
	if (PointOnWall(surfaces, waypoint) && !PointOnWall(surfaces, goal))
		return waypoint;
	return goal;
}

namespace deck_surface_detail
{
inline bool InTriangle(Engine::Math::FixedVector2 a, Engine::Math::FixedVector2 b, Engine::Math::FixedVector2 c, Engine::Math::FixedVector2 p)
{
	using Engine::Math::Fixed;
	const auto side = [](Engine::Math::FixedVector2 from, Engine::Math::FixedVector2 to, Engine::Math::FixedVector2 at) {
		return (to.x - from.x) * (at.y - from.y) - (to.y - from.y) * (at.x - from.x);
	};
	const Fixed d1 = side(a, b, p), d2 = side(b, c, p), d3 = side(c, a, p);
	const bool negative = d1 < Fixed{} || d2 < Fixed{} || d3 < Fixed{};
	const bool positive = d1 > Fixed{} || d2 > Fixed{} || d3 > Fixed{};
	return !(negative && positive);
}
}

// Bridge::isPointOnBridge: within its bounds, in one of the two triangles its corners make (2D).
inline bool PointOnDeck(const DeckGeometry &deck, Engine::Math::FixedVector2 at)
{
	using namespace deck_surface_detail;
	const auto left1 = deck.fromLeft.XY(), right1 = deck.fromRight.XY(), left2 = deck.toLeft.XY(), right2 = deck.toRight.XY();
	if (at.x < std::min({left1.x, right1.x, left2.x, right2.x}) || at.x > std::max({left1.x, right1.x, left2.x, right2.x}) ||
		at.y < std::min({left1.y, right1.y, left2.y, right2.y}) || at.y > std::max({left1.y, right1.y, left2.y, right2.y}))
		return false;
	return InTriangle(left1, right1, left2, at) || InTriangle(right1, left2, right2, at);
}

// Bridge::getBridgeHeight: the deck's plane (through fromLeft, fromRight and toLeft) straight above or below `at`.
inline Engine::Math::Fixed DeckHeight(const DeckGeometry &deck, Engine::Math::FixedVector2 at)
{
	using Engine::Math::Fixed;
	const Engine::Math::FixedVector3 u = deck.fromRight - deck.fromLeft, v = deck.toLeft - deck.fromLeft;
	const Engine::Math::FixedVector3 n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
	if (n.z == Fixed{})
		return deck.fromLeft.z;
	return deck.fromLeft.z - (n.x * (at.x - deck.fromLeft.x) + n.y * (at.y - deck.fromLeft.y)) / n.z;
}

// TerrainLogic::getLayerHeight: on a deck's layer over its surface, the deck (where it is above the ground); else the
// ground.
inline Engine::Math::Fixed LayerHeight(const DeckSurfaces &surfaces, const GroundHeight &ground, Engine::Math::FixedVector2 at, std::uint8_t layer)
{
	const Engine::Math::Fixed floor = ground.At(at);
	if (layer == GroundLayer || layer > surfaces.decks.size())
		return floor;
	// W3DTerrainLogic::getLayerHeight's LAYER_WALL: on the wall, the wall's height (whatever the ground there).
	if (IsWallLayer(surfaces, layer))
		return PointOnWall(surfaces, at) ? surfaces.wall.height : floor;
	const DeckGeometry &deck = surfaces.decks[layer - 1];
	if (!PointOnDeck(deck, at))
		return floor;
	const Engine::Math::Fixed height = DeckHeight(deck, at);
	return height > floor ? height : floor;
}

// TerrainLogic::getHighestLayerForDestination (all bridges, rubble too: onlyHealthyBridges false): of the ground and the
// decks over the point at or below z, the one z is nearest above (the ground's distance not made absolute: a point below
// the ground takes a deck only when its distance is smaller still); the ground on a tie. The wall is looked at first, when
// the point is more than half the wall's height above the ground: on the wall and at or above its height.
inline std::uint8_t HighestLayerForDestination(const DeckSurfaces &surfaces, const GroundHeight &ground, Engine::Math::FixedVector3 at)
{
	std::uint8_t best = GroundLayer;
	Engine::Math::Fixed bestDistance = at.z - ground.At(at.XY());
	if (bestDistance > surfaces.wall.height / Engine::Math::Fixed::FromInt(2) && PointOnWall(surfaces, at.XY()))
	{
		const Engine::Math::Fixed delta = at.z - surfaces.wall.height;
		if (delta >= Engine::Math::Fixed{} && Engine::Math::Abs(delta) < Engine::Math::Abs(bestDistance))
		{
			best = surfaces.wall.layer;
			bestDistance = delta;
		}
	}
	for (std::size_t index = 0; index < surfaces.decks.size(); ++index)
	{
		if (index + 1 == surfaces.wall.layer)
			continue;
		const DeckGeometry &deck = surfaces.decks[index];
		if (!PointOnDeck(deck, at.XY()))
			continue;
		const Engine::Math::Fixed delta = at.z - DeckHeight(deck, at.XY());
		if (delta >= Engine::Math::Fixed{} && Engine::Math::Abs(delta) < Engine::Math::Abs(bestDistance))
		{
			best = static_cast<std::uint8_t>(index + 1);
			bestDistance = delta;
		}
	}
	return best;
}

// TerrainLogic::getLayerForDestination: of the ground, the wall (looked at only when the point is more than half the
// wall's height from the ground) and the decks over the point, the one whose height is nearest z (the earlier on a tie).
inline std::uint8_t LayerForDestination(const DeckSurfaces &surfaces, const GroundHeight &ground, Engine::Math::FixedVector3 at)
{
	std::uint8_t best = GroundLayer;
	Engine::Math::Fixed bestDistance = Engine::Math::Abs(at.z - ground.At(at.XY()));
	if (bestDistance > surfaces.wall.height / Engine::Math::Fixed::FromInt(2) && PointOnWall(surfaces, at.XY()))
	{
		const Engine::Math::Fixed delta = Engine::Math::Abs(at.z - surfaces.wall.height);
		if (delta < bestDistance)
		{
			best = surfaces.wall.layer;
			bestDistance = delta;
		}
	}
	for (std::size_t index = 0; index < surfaces.decks.size(); ++index)
	{
		if (index + 1 == surfaces.wall.layer)
			continue;
		const DeckGeometry &deck = surfaces.decks[index];
		if (!PointOnDeck(deck, at.XY()))
			continue;
		const Engine::Math::Fixed distance = Engine::Math::Abs(at.z - DeckHeight(deck, at.XY()));
		if (distance < bestDistance)
		{
			best = static_cast<std::uint8_t>(index + 1);
			bestDistance = distance;
		}
	}
	return best;
}
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::DeckSurfaces>
{
	static constexpr std::string_view StableName = "engine.gameplay.deck_surfaces";
};
}
