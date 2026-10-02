export module games.generalszh.gameplay.walls.components.wall_piece;
import std;

export import engine.ecs.core.component_registry;

// A piece of the wall units walk on top of (KINDOF_WALK_ON_TOP_OF_WALL, in Pathfinder::m_wallPieces): the wall's layer
// and the cells the layer was given when the map began (the same on every piece: PathfindLayer::
// allocateCellsForWallLayer's region), whether its footprint is a box, and whether it still holds the wall up (0 once it
// fell to rubble: the original's INVALID_ID in m_wallPieces). Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct WallPiece
{
	std::int32_t x0{0}, y0{0}, width{0}, height{0};
	std::uint8_t layer{0};
	std::uint8_t valid{1};
	std::uint8_t box{1};
	std::uint8_t reserved[5]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::WallPiece>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.wall_piece";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
