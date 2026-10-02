export module engine.gameplay.rts.navigation.resources.navigation_grid;
import std;

export import engine.gameplay.rts.navigation.definitions.pathfind_cell;
export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;
import engine.ecs.system.system;
export import engine.ecs.core.entity_codec;
export import engine.gameplay.common.spatial.resources.deck_surfaces;

// The ground pathfinding grid, structure of arrays: per cell its type and
// the obstacle that owns it (the first to cover it), plus clearance planes:
// for a set of locomotor surfaces, how much room each cell has (the
// Chebyshev distance in cells to the nearest cell those surfaces cannot use,
// or the map's edge, capped), so a mover of any size asks one number. Cell
// (x, y) covers world [10x, 10x + 10) x [10y, 10y + 10). The obstacles stamped
// into it are recorded with their pose, so they can be taken out again after
// their objects are gone.
export namespace engine::gameplay
{
enum class ObstacleShape : std::uint8_t
{
	Box,
	Cylinder, // spheres and cylinders stamp alike
	Fence,
};

// An object's footprint as the grid takes it (GeometryInfo, and a fence's FenceWidth / FenceXOffset).
struct ObstacleFootprint
{
	Engine::Math::Fixed majorRadius; // a fence: its FenceWidth
	Engine::Math::Fixed minorRadius; // a fence: its FenceXOffset
	ObstacleShape shape{ObstacleShape::Box};
	bool rubble{false};              // BODY_RUBBLE: its cells become rubble, owned by no one
	bool seeThrough{false};          // KINDOF_CAN_SEE_THROUGH_STRUCTURE
	std::uint8_t reserved[5]{};      // no padding: checkpoints hold its bytes
};

inline constexpr std::uint8_t MaxClearance = 8;

struct ClearancePlane
{
	std::uint8_t surfaces{0};
	std::vector<std::uint8_t> room; // per cell; 0: not usable
	// Each deck's room, per deck cell (as `room`, within the deck; off it, past its ends, counts as open).
	std::vector<std::vector<std::uint8_t>> deckRoom;
	// Connected zones of its usable cells (derived: rebuilt when stale, after its room changed), 0: not usable: the ground's
	// cells first, then each deck's (NavigationGrid::CellCount), joined where a deck meets the ground.
	std::vector<std::uint32_t> zones;
	// The same with the obstacles' cells taken as usable (PathfindZoneManager's terrain zones: connectivity by the
	// terrain alone, buildings aside).
	std::vector<std::uint32_t> terrainZones;
	bool zonesStale{true};
	// Working space of the zones' rebuild (derived, never saved): the usable cells of each zone map.
	std::vector<std::uint8_t> zoneOpen, terrainOpen;
};

// A deck's own cells (PathfindLayer), structure of arrays over the cells its bounds cover (padded by one): Clear on the
// deck, BridgeImpassable along its sides (and everywhere once it is destroyed), Impassable off it (no cell of the deck);
// entry cells (past its ends) link to the ground cell under them, which links back.
struct DeckLayer
{
	ecs::Entity owner;
	DeckGeometry geometry;
	std::int32_t x0{0}, y0{0}, width{0}, height{0};
	std::vector<PathfindCellType> type;
	std::vector<std::uint8_t> toGround;
	bool destroyed{false};
	bool wall{false}; // the wall layer (LAYER_WALL): its cells from the wall pieces, never linked to the ground

	bool Contains(std::int32_t x, std::int32_t y) const noexcept { return x >= x0 && y >= y0 && x < x0 + width && y < y0 + height; }
	std::size_t Index(std::int32_t x, std::int32_t y) const noexcept
	{
		return static_cast<std::size_t>(y - y0) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x - x0);
	}
};

// Decks are layers 1 and up (the original's bridge layers, at most 13 below LAYER_WALL; the ground is GroundLayer); the
// wall, when a map has one, is a deck of its own past those it was made after (DeckLayer::wall).
inline constexpr std::size_t MaxDecks = 13;

struct StampedObstacle
{
	ecs::Entity entity;
	ObstacleFootprint footprint;
	Engine::Math::FixedVector2 position;
	Engine::Math::TurnAngle orientation;
};

class NavigationGrid
{
public:
	NavigationGrid() = default;
	NavigationGrid(std::int32_t width, std::int32_t height) { Resize(width, height); }

	void Resize(std::int32_t width, std::int32_t height)
	{
		m_width = std::max(width, 0);
		m_height = std::max(height, 0);
		const std::size_t cells = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
		m_type.assign(cells, PathfindCellType::Clear);
		m_obstacle.assign(cells, ecs::Entity{});
		m_fence.assign(cells, 0);
		m_seeThrough.assign(cells, 0);
		m_stamped.clear();
		m_decks.clear();
		m_deckLink.assign(cells, 0);
		for (ClearancePlane &plane : m_clearance)
		{
			plane.room.assign(cells, 0);
			plane.deckRoom.clear();
		}
	}

	// The decks, layer d at index d - 1; the ground cell's deck (its layer; 0: none: the ground cell an entry links to).
	const std::vector<DeckLayer> &Decks() const noexcept { return m_decks; }
	std::vector<DeckLayer> &Decks() noexcept { return m_decks; }
	std::uint8_t DeckLink(std::int32_t x, std::int32_t y) const noexcept { return m_deckLink[Index(x, y)]; }
	void SetDeckLink(std::int32_t x, std::int32_t y, std::uint8_t layer) noexcept { m_deckLink[Index(x, y)] = layer; }
	// Every cell the routes know: the ground's, then each deck's in turn (the zones' and the searches' index space).
	std::size_t CellCount() const noexcept
	{
		std::size_t count = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
		for (const DeckLayer &deck : m_decks)
			count += deck.type.size();
		return count;
	}
	// Where the deck of `layer` starts in that index space.
	std::size_t DeckOffset(std::uint8_t layer) const noexcept
	{
		std::size_t offset = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
		for (std::size_t index = 0; index + 1 < layer && index < m_decks.size(); ++index)
			offset += m_decks[index].type.size();
		return offset;
	}

	std::int32_t Width() const noexcept { return m_width; }
	std::int32_t Height() const noexcept { return m_height; }
	// m_extent.hi (inclusive).
	std::int32_t HiX() const noexcept { return m_width - 1; }
	std::int32_t HiY() const noexcept { return m_height - 1; }
	bool Contains(std::int32_t x, std::int32_t y) const noexcept { return x >= 0 && y >= 0 && x < m_width && y < m_height; }

	std::size_t Index(std::int32_t x, std::int32_t y) const noexcept
	{
		return static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x);
	}

	PathfindCellType Type(std::int32_t x, std::int32_t y) const noexcept { return m_type[Index(x, y)]; }
	// Every ground cell's type in index order (Index).
	std::span<const PathfindCellType> Types() const noexcept { return m_type; }
	ecs::Entity Obstacle(std::int32_t x, std::int32_t y) const noexcept { return m_obstacle[Index(x, y)]; }
	bool ObstacleIsFence(std::int32_t x, std::int32_t y) const noexcept { return m_fence[Index(x, y)] != 0; }
	bool ObstacleIsSeeThrough(std::int32_t x, std::int32_t y) const noexcept { return m_seeThrough[Index(x, y)] != 0; }

	// PathfindCell::setType: a cell an obstacle owns stays an obstacle.
	void SetType(std::int32_t x, std::int32_t y, PathfindCellType type) noexcept
	{
		const std::size_t index = Index(x, y);
		m_type[index] = m_obstacle[index] != ecs::Entity{} ? PathfindCellType::Obstacle : type;
	}

	// PathfindCell::setTypeAsObstacle: only clear or impassable cells take an obstacle; rubble is owned by no one.
	bool SetObstacle(std::int32_t x, std::int32_t y, ecs::Entity entity, const ObstacleFootprint &footprint, bool fence) noexcept
	{
		const std::size_t index = Index(x, y);
		if (m_type[index] != PathfindCellType::Clear && m_type[index] != PathfindCellType::Impassable)
			return false;
		if (footprint.rubble)
		{
			m_type[index] = PathfindCellType::Rubble;
			m_obstacle[index] = {};
			m_fence[index] = m_seeThrough[index] = 0;
			return true;
		}
		m_type[index] = PathfindCellType::Obstacle;
		m_obstacle[index] = entity;
		m_fence[index] = fence ? 1u : 0u;
		m_seeThrough[index] = footprint.seeThrough ? 1u : 0u;
		return true;
	}

	// PathfindCell::removeObstacle: rubble clears whoever made it; the owner's cells clear.
	bool RemoveObstacle(std::int32_t x, std::int32_t y, ecs::Entity entity) noexcept
	{
		const std::size_t index = Index(x, y);
		if (m_type[index] == PathfindCellType::Rubble)
			m_type[index] = PathfindCellType::Clear;
		if (m_obstacle[index] != entity)
			return false;
		m_type[index] = PathfindCellType::Clear;
		m_obstacle[index] = {};
		m_fence[index] = m_seeThrough[index] = 0;
		return true;
	}

	// Whether movers of these surfaces may use the cell.
	bool Usable(std::int32_t x, std::int32_t y, std::uint8_t surfaces) const noexcept { return (SurfacesFor(Type(x, y)) & surfaces) != 0; }

	std::vector<ClearancePlane> &Clearance() noexcept { return m_clearance; }
	const std::vector<ClearancePlane> &Clearance() const noexcept { return m_clearance; }
	const ClearancePlane *ClearanceFor(std::uint8_t surfaces) const noexcept
	{
		for (const ClearancePlane &plane : m_clearance)
			if (plane.surfaces == surfaces)
				return &plane;
		return nullptr;
	}

	std::vector<StampedObstacle> &Stamped() noexcept { return m_stamped; }
	const std::vector<StampedObstacle> &Stamped() const noexcept { return m_stamped; }

	// Checkpoints. Which obstacle owns a cell depends on the order footprints went in and came out, so the obstacle
	// layer is kept as it is rather than stamped again: the cells obstacles or rubble hold (everything else is the
	// terrain's) and what was stamped, in order. Loaded onto a grid classified from the terrain alone.
	void SaveObstacles(engine::core::serialization::ByteWriter &writer) const
	{
		std::uint32_t held = 0;
		for (const PathfindCellType type : m_type)
			held += type == PathfindCellType::Obstacle || type == PathfindCellType::Rubble ? 1u : 0u;
		writer.U32(held);
		for (std::size_t index = 0; index < m_type.size(); ++index)
			if (m_type[index] == PathfindCellType::Obstacle || m_type[index] == PathfindCellType::Rubble)
			{
				writer.U32(static_cast<std::uint32_t>(index));
				writer.U8(static_cast<std::uint8_t>(m_type[index]));
				ecs::WriteEntity(writer, m_obstacle[index]);
				writer.U8(static_cast<std::uint8_t>(m_fence[index] | (m_seeThrough[index] << 1)));
			}
		writer.U32(static_cast<std::uint32_t>(m_stamped.size()));
		for (const StampedObstacle &record : m_stamped)
		{
			ecs::WriteEntity(writer, record.entity);
			writer.U8(static_cast<std::uint8_t>(record.footprint.shape));
			writer.I64(record.footprint.majorRadius.Raw());
			writer.I64(record.footprint.minorRadius.Raw());
			writer.U8(static_cast<std::uint8_t>((record.footprint.rubble ? 1 : 0) | (record.footprint.seeThrough ? 2 : 0)));
			writer.I64(record.position.x.Raw());
			writer.I64(record.position.y.Raw());
			writer.U32(record.orientation.units);
		}
	}

	bool LoadObstacles(engine::core::serialization::ByteReader &reader)
	{
		const auto held = reader.U32();
		if (!held)
			return false;
		for (std::uint32_t cell = 0; cell < *held; ++cell)
		{
			const auto index = reader.U32();
			const auto type = reader.U8();
			const auto owner = ecs::ReadEntity(reader);
			const auto flags = reader.U8();
			if (!index || !type || !owner || !flags || *index >= m_type.size())
				return false;
			m_type[*index] = static_cast<PathfindCellType>(*type);
			m_obstacle[*index] = *owner;
			m_fence[*index] = *flags & 1u;
			m_seeThrough[*index] = (*flags >> 1) & 1u;
		}
		const auto count = reader.U32();
		if (!count)
			return false;
		m_stamped.clear();
		for (std::uint32_t item = 0; item < *count; ++item)
		{
			StampedObstacle record;
			const auto entity = ecs::ReadEntity(reader);
			const auto shape = reader.U8();
			const auto major = reader.I64(), minor = reader.I64();
			const auto marks = reader.U8();
			const auto x = reader.I64(), y = reader.I64();
			const auto orientation = reader.U32();
			if (!entity || !shape || !major || !minor || !marks || !x || !y || !orientation)
				return false;
			record.entity = *entity;
			record.footprint.shape = static_cast<ObstacleShape>(*shape);
			record.footprint.majorRadius = Engine::Math::Fixed::FromRaw(*major);
			record.footprint.minorRadius = Engine::Math::Fixed::FromRaw(*minor);
			record.footprint.rubble = (*marks & 1) != 0;
			record.footprint.seeThrough = (*marks & 2) != 0;
			record.position = {Engine::Math::Fixed::FromRaw(*x), Engine::Math::Fixed::FromRaw(*y)};
			record.orientation = Engine::Math::TurnAngle{*orientation};
			m_stamped.push_back(record);
		}
		return true;
	}

private:
	std::int32_t m_width{0};
	std::int32_t m_height{0};
	std::vector<PathfindCellType> m_type;
	std::vector<ecs::Entity> m_obstacle;
	std::vector<std::uint8_t> m_fence;
	std::vector<std::uint8_t> m_seeThrough;
	std::vector<StampedObstacle> m_stamped;
	std::vector<ClearancePlane> m_clearance;
	std::vector<DeckLayer> m_decks;
	std::vector<std::uint8_t> m_deckLink;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::NavigationGrid>
{
	static constexpr std::string_view StableName = "engine.gameplay.navigation_grid";
};
}
