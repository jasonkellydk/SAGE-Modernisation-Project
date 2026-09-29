export module engine.gameplay.common.spatial.resources.deck_surfaces;
import std;

export import Engine.Core.Math.FixedVector;
export import engine.ecs.core.component_registry;
export import engine.gameplay.common.spatial.resources.ground_height;
import engine.ecs.system.system;

// Surfaces above the ground things may stand on (the original's bridge layers, as TerrainLogic knows them): each deck's
// rectangle, its ends `from` and `to` and its four corners at deck height, by layer (layer d at index d - 1; the ground
// is layer 0). What stands on one says so (SurfaceLayer). Where a thing on a deck stands over it, its floor is the
// deck (TerrainLogic::getLayerHeight); elsewhere the ground.
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

struct DeckSurfaces
{
	std::vector<DeckGeometry> decks;
};

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
	const DeckGeometry &deck = surfaces.decks[layer - 1];
	if (!PointOnDeck(deck, at))
		return floor;
	const Engine::Math::Fixed height = DeckHeight(deck, at);
	return height > floor ? height : floor;
}

// TerrainLogic::getLayerForDestination: of the ground and the decks over the point, the one whose height is nearest z
// (the ground on a tie).
inline std::uint8_t LayerForDestination(const DeckSurfaces &surfaces, const GroundHeight &ground, Engine::Math::FixedVector3 at)
{
	std::uint8_t best = GroundLayer;
	Engine::Math::Fixed bestDistance = Engine::Math::Abs(at.z - ground.At(at.XY()));
	for (std::size_t index = 0; index < surfaces.decks.size(); ++index)
	{
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
