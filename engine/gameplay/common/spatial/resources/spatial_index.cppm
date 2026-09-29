export module engine.gameplay.common.spatial.resources.spatial_index;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// Where every targetable entity is this tick, bucketed on a uniform grid
// and stored flat (entries sorted by cell then entity, cells as compressed
// rows) so parallel systems can run range queries without locks. Rebuilt
// once per tick; query order is deterministic.
export namespace engine::gameplay
{
struct SpatialEntry
{
	ecs::Entity entity;
	Engine::Math::FixedVector3 position;
	Engine::Math::Fixed radius;
	std::uint32_t player{0};
	std::uint32_t classes{0};
	std::uint64_t clearTo{~std::uint64_t{0}}; // the players it is clear to through the shroud (ObjectShroud)
	std::uint32_t team{0xFFFFFFFFu};          // its team (TeamMember; none: Relationships::NoTeam)
};

class SpatialIndex
{
public:
	SpatialIndex() = default;

	// A grid over [0, extent] in x and y, `cellSize` units per cell.
	SpatialIndex(Engine::Math::FixedVector2 extent, Engine::Math::Fixed cellSize) : m_cellSize(cellSize)
	{
		m_columns = std::max<std::int64_t>(1, (extent.x / cellSize).Floor() + 1);
		m_rows = std::max<std::int64_t>(1, (extent.y / cellSize).Floor() + 1);
		m_cellBegin.assign(static_cast<std::size_t>(m_columns * m_rows + 1), 0);
	}

	std::uint32_t CellOf(Engine::Math::FixedVector2 position) const noexcept
	{
		const std::int64_t column = std::clamp<std::int64_t>((position.x / m_cellSize).Floor(), 0, m_columns - 1);
		const std::int64_t row = std::clamp<std::int64_t>((position.y / m_cellSize).Floor(), 0, m_rows - 1);
		return static_cast<std::uint32_t>(row * m_columns + column);
	}

	// Replaces the contents; `entries` may come in any order.
	void Rebuild(std::vector<SpatialEntry> entries)
	{
		m_entries = std::move(entries);
		m_cells.resize(m_entries.size());
		m_largestRadius = {};
		for (const SpatialEntry &entry : m_entries)
			m_largestRadius = std::max(m_largestRadius, entry.radius);
		for (std::size_t index = 0; index < m_entries.size(); ++index)
			m_cells[index] = CellOf(m_entries[index].position.XY());
		// Sorted by cell, entity breaking ties: deterministic whatever the gather order.
		std::vector<std::size_t> order(m_entries.size());
		for (std::size_t index = 0; index < order.size(); ++index)
			order[index] = index;
		std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
			if (m_cells[a] != m_cells[b])
				return m_cells[a] < m_cells[b];
			if (m_entries[a].entity.index != m_entries[b].entity.index)
				return m_entries[a].entity.index < m_entries[b].entity.index;
			return m_entries[a].entity.generation < m_entries[b].entity.generation;
		});
		std::vector<SpatialEntry> sorted;
		sorted.reserve(m_entries.size());
		std::fill(m_cellBegin.begin(), m_cellBegin.end(), 0u);
		for (const std::size_t index : order)
		{
			sorted.push_back(m_entries[index]);
			++m_cellBegin[m_cells[index] + 1];
		}
		for (std::size_t cell = 1; cell < m_cellBegin.size(); ++cell)
			m_cellBegin[cell] += m_cellBegin[cell - 1];
		m_entries = std::move(sorted);
		m_byEntity.resize(m_entries.size());
		for (std::uint32_t index = 0; index < m_entries.size(); ++index)
			m_byEntity[index] = {Key(m_entries[index].entity), index};
		std::sort(m_byEntity.begin(), m_byEntity.end());
	}

	// This tick's entry for an entity, or null when it is not targetable.
	const SpatialEntry *Find(ecs::Entity entity) const
	{
		const std::pair<std::uint64_t, std::uint32_t> key{Key(entity), 0};
		const auto found = std::lower_bound(m_byEntity.begin(), m_byEntity.end(), key);
		return found != m_byEntity.end() && found->first == key.first ? &m_entries[found->second] : nullptr;
	}

	// Calls `visit(entry)` for every entry whose footprint reaches within
	// `range` of `center` (2D), in cell-then-entity order.
	template<typename Visit>
	void ForEachWithin(Engine::Math::FixedVector2 center, Engine::Math::Fixed range, Visit &&visit) const
	{
		if (m_entries.empty())
			return;
		const Engine::Math::Fixed reach = range + m_largestRadius;
		const std::int64_t minColumn = std::clamp<std::int64_t>(((center.x - reach) / m_cellSize).Floor(), 0, m_columns - 1);
		const std::int64_t maxColumn = std::clamp<std::int64_t>(((center.x + reach) / m_cellSize).Floor(), 0, m_columns - 1);
		const std::int64_t minRow = std::clamp<std::int64_t>(((center.y - reach) / m_cellSize).Floor(), 0, m_rows - 1);
		const std::int64_t maxRow = std::clamp<std::int64_t>(((center.y + reach) / m_cellSize).Floor(), 0, m_rows - 1);
		for (std::int64_t row = minRow; row <= maxRow; ++row)
			for (std::int64_t column = minColumn; column <= maxColumn; ++column)
			{
				const auto cell = static_cast<std::size_t>(row * m_columns + column);
				for (std::uint32_t index = m_cellBegin[cell]; index < m_cellBegin[cell + 1]; ++index)
				{
					const SpatialEntry &entry = m_entries[index];
					const Engine::Math::Fixed limit = range + entry.radius;
					if (Engine::Math::DistanceSquared(entry.position.XY(), center) <= limit * limit)
						visit(entry);
				}
			}
	}

	std::span<const SpatialEntry> Entries() const noexcept { return m_entries; }

private:
	static std::uint64_t Key(ecs::Entity entity) noexcept { return (static_cast<std::uint64_t>(entity.index) << 32) | entity.generation; }

	std::vector<std::pair<std::uint64_t, std::uint32_t>> m_byEntity;
	Engine::Math::Fixed m_cellSize{Engine::Math::Fixed::FromInt(100)};
	std::int64_t m_columns{1};
	std::int64_t m_rows{1};
	Engine::Math::Fixed m_largestRadius;
	std::vector<std::uint32_t> m_cellBegin{0, 0};
	std::vector<SpatialEntry> m_entries;
	std::vector<std::uint32_t> m_cells;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::SpatialIndex>
{
	static constexpr std::string_view StableName = "engine.gameplay.spatial_index";
};
}
