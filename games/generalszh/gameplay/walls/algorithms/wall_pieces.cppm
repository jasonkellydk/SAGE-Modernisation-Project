export module games.generalszh.gameplay.walls.algorithms.wall_pieces;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.walls.components.wall_piece;
export import engine.gameplay.common.spatial.resources.deck_surfaces;
import engine.gameplay.common.spatial.components.transform;
import games.generalszh.content.objects.object_definition;

// Pathfinder::addWallPiece (GameLogic::startNewGame's bridge pass: a WALK_ON_TOP_OF_WALL thing once placed): the piece
// joins the wall's pieces while there is room (MAX_WALL_PIECES - 1: 127), where it stands, how it is turned and its
// footprint (GeometryInfo's major and minor radius; a sphere's major both ways).
export namespace generalszh::gameplay
{
inline constexpr std::size_t MaxWallPieces = 127;

inline void AddWallPiece(GameWorld &game, ecs::Entity object, const content::ObjectDefinition &kind)
{
	auto &wall = game.world.Resource<engine::gameplay::DeckSurfaces>().wall;
	const auto *transform = game.world.Get<engine::gameplay::Transform>(object);
	if (transform == nullptr || wall.Pieces() >= MaxWallPieces)
		return;
	const bool sphere = kind.geometry.shape == content::GeometryShape::Sphere;
	wall.AddPiece(object, transform->position.XY(), transform->facing, kind.geometry.majorRadius, sphere ? kind.geometry.majorRadius : kind.geometry.minorRadius);
	game.world.Add<WallPiece>(object);
	game.world.Get<WallPiece>(object)->box = kind.geometry.shape == content::GeometryShape::Box ? 1u : 0u;
}
}
