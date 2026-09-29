export module engine.gameplay.rts.navigation.definitions.pathfind_cell;
import std;

export import Engine.Core.Math.Fixed;

// The pathfinding grid's vocabulary: cells of 10 world units (one heightmap
// cell), what each cell is, and the locomotor surfaces each lets a mover use.
export namespace engine::gameplay
{
inline constexpr std::int64_t PathfindCellSize = 10;

enum class PathfindCellType : std::uint8_t
{
	Clear = 0,
	Water = 1,
	Cliff = 2,
	Rubble = 3,
	Obstacle = 4,
	BridgeImpassable = 5,
	Impassable = 6,
};

// IS_IMPASSABLE.
inline constexpr bool IsImpassable(PathfindCellType type) noexcept
{
	return type == PathfindCellType::Impassable || type == PathfindCellType::Obstacle || type == PathfindCellType::BridgeImpassable;
}

// LocomotorSurfaceType bits.
namespace locomotor_surface
{
inline constexpr std::uint8_t Ground = 1u << 0;
inline constexpr std::uint8_t Water = 1u << 1;
inline constexpr std::uint8_t Cliff = 1u << 2;
inline constexpr std::uint8_t Air = 1u << 3;
inline constexpr std::uint8_t Rubble = 1u << 4;
}

// The surfaces a cell's type allows (Pathfinder::validLocomotorSurfacesForCellType).
inline constexpr std::uint8_t SurfacesFor(PathfindCellType type) noexcept
{
	namespace ls = locomotor_surface;
	switch (type)
	{
	case PathfindCellType::Clear: return ls::Ground | ls::Air;
	case PathfindCellType::Water: return ls::Water | ls::Air;
	case PathfindCellType::Cliff: return ls::Cliff | ls::Air;
	case PathfindCellType::Rubble: return ls::Rubble | ls::Air;
	default: return ls::Air;
	}
}

}
