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
	std::uint32_t disguiseTeam{0xFFFFFFFFu};  // disguised (target_class::Disguised): as whose default team
	std::int32_t disguisePlayer{-1};          // and player (Targetable)
};

// An entry's bounding sphere (GeometryInfo::getBoundingSphereRadius, and getZDeltaToCenterPosition: how far its centre is
// above its position), kept as a column beside the entries for FROM_BOUNDINGSPHERE_3D queries.
struct BoundingSphere
{
	Engine::Math::Fixed radius;
	Engine::Math::Fixed lift;
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

	// Replaces the contents; `entries` may come in any order. Without bounding spheres given, each entry's is its
	// footprint's circle about its position.
	void Rebuild(std::vector<SpatialEntry> entries)
	{
		m_gathered = std::move(entries);
		CircleSpheres();
		RebuildGathered();
	}
	// The same, `fill(entries)` appending them to the index's own (reused) buffer: no allocation once warm.
	template<typename Fill>
	void RebuildWith(Fill &&fill)
	{
		m_gathered.clear();
		fill(m_gathered);
		CircleSpheres();
		RebuildGathered();
	}
	// The same with their bounding spheres: `fill(entries, spheres)` appends both, a sphere for each entry, in step.
	template<typename Fill>
	void RebuildWithSpheres(Fill &&fill)
	{
		m_gathered.clear();
		m_gatheredSpheres.clear();
		fill(m_gathered, m_gatheredSpheres);
		if (m_gatheredSpheres.size() != m_gathered.size())
			CircleSpheres();
		RebuildGathered();
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

	// PartitionManager::iterateObjectsInRange(pos, range, FROM_BOUNDINGSPHERE_3D) (distCalcProc_BoundaryAndBoundary_3D
	// from a point): calls `visit(entry, distanceSquared)` for every entry whose bounding sphere comes nearer than `range`
	// to `center`, `distanceSquared` measured from `center` to the sphere (its centre less its radius, never below
	// zero), in cell-then-entity order.
	template<typename Visit>
	void ForEachSphereWithin(Engine::Math::FixedVector3 center, Engine::Math::Fixed range, Visit &&visit) const
	{
		if (m_entries.empty())
			return;
		// Horizontally a sphere comes no nearer than its centre less its radius.
		const Engine::Math::Fixed reach = range + m_largestSphere;
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
					const BoundingSphere &sphere = m_spheres[index];
					const Engine::Math::FixedVector3 offset{entry.position.x - center.x, entry.position.y - center.y,
						entry.position.z + sphere.lift - center.z};
					const Engine::Math::Fixed lengthSquared = Engine::Math::LengthSquared(offset);
					const Engine::Math::Fixed limit = range + sphere.radius;
					if (!(lengthSquared < limit * limit))
						continue;
					Engine::Math::Fixed distanceSquared = lengthSquared;
					if (sphere.radius > Engine::Math::Fixed{})
					{
						const Engine::Math::Fixed shrunk = Engine::Math::Sqrt(lengthSquared) - sphere.radius;
						distanceSquared = shrunk > Engine::Math::Fixed{} ? shrunk * shrunk : Engine::Math::Fixed{};
					}
					// (shrunk below the range, as lengthSquared < limit squared: never visited at range 0.)
					if (!(distanceSquared < range * range))
						continue;
					visit(entry, distanceSquared);
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
	Engine::Math::Fixed m_largestSphere;
	std::vector<std::uint32_t> m_cellBegin{0, 0};
	std::vector<SpatialEntry> m_entries;
	std::vector<BoundingSphere> m_spheres; // m_entries' bounding spheres, in step
	// Rebuild's working buffers, kept for the next tick.
	struct SortKey
	{
		std::uint64_t key; // cell above the entity index (an entity index is unique among the live)
		std::uint32_t at;  // in m_gathered
	};
	std::vector<SpatialEntry> m_gathered;
	std::vector<BoundingSphere> m_gatheredSpheres;
	std::vector<SortKey> m_order;
	std::vector<SortKey> m_orderScratch;
	std::vector<std::uint32_t> m_counts;

	// Stable LSD radix sort of m_order by its key's low `bits` bits, 11 bits a pass (keys are small: a few passes over
	// a thousand entries instead of a comparison sort).
	void RadixSort(unsigned bits)
	{
		constexpr unsigned Digit = 11;
		constexpr std::size_t Buckets = std::size_t{1} << Digit;
		m_orderScratch.resize(m_order.size());
		m_counts.resize(Buckets);
		for (unsigned shift = 0; shift < bits; shift += Digit)
		{
			std::fill(m_counts.begin(), m_counts.end(), 0u);
			for (const SortKey &item : m_order)
				++m_counts[(item.key >> shift) & (Buckets - 1)];
			std::uint32_t sum = 0;
			for (std::uint32_t &count : m_counts)
			{
				const std::uint32_t here = count;
				count = sum;
				sum += here;
			}
			for (const SortKey &item : m_order)
				m_orderScratch[m_counts[(item.key >> shift) & (Buckets - 1)]++] = item;
			m_order.swap(m_orderScratch);
		}
	}

	// Each gathered entry's footprint circle as its sphere, centred at its position.
	void CircleSpheres()
	{
		m_gatheredSpheres.resize(m_gathered.size());
		for (std::size_t index = 0; index < m_gathered.size(); ++index)
			m_gatheredSpheres[index] = {m_gathered[index].radius, {}};
	}

	// Sorted by cell, entity breaking ties: deterministic whatever the gather order (entity indices are unique among
	// the live, so index order is the original's (index, generation) order).
	void RebuildGathered()
	{
		const std::size_t count = m_gathered.size();
		m_largestRadius = {};
		m_largestSphere = {};
		for (const BoundingSphere &sphere : m_gatheredSpheres)
			m_largestSphere = std::max(m_largestSphere, sphere.radius);
		std::uint32_t maxIndex = 0;
		for (const SpatialEntry &entry : m_gathered)
		{
			m_largestRadius = std::max(m_largestRadius, entry.radius);
			maxIndex = std::max(maxIndex, entry.entity.index);
		}
		const unsigned indexBits = static_cast<unsigned>(std::bit_width(maxIndex));
		const unsigned cellBits = static_cast<unsigned>(std::bit_width(static_cast<std::uint64_t>(m_cellBegin.size())));
		m_order.resize(count);
		for (std::size_t index = 0; index < count; ++index)
		{
			const SpatialEntry &entry = m_gathered[index];
			m_order[index] = {(static_cast<std::uint64_t>(CellOf(entry.position.XY())) << indexBits) | entry.entity.index, static_cast<std::uint32_t>(index)};
		}
		RadixSort(indexBits + cellBits);
		std::fill(m_cellBegin.begin(), m_cellBegin.end(), 0u);
		m_entries.resize(count);
		m_spheres.resize(count);
		for (std::size_t index = 0; index < count; ++index)
		{
			const SortKey &key = m_order[index];
			m_entries[index] = m_gathered[key.at];
			m_spheres[index] = m_gatheredSpheres[key.at];
			++m_cellBegin[static_cast<std::size_t>(key.key >> indexBits) + 1];
		}
		for (std::size_t cell = 1; cell < m_cellBegin.size(); ++cell)
			m_cellBegin[cell] += m_cellBegin[cell - 1];
		// By entity: the same radix sort on the index alone.
		for (std::size_t index = 0; index < count; ++index)
			m_order[index] = {m_entries[index].entity.index, static_cast<std::uint32_t>(index)};
		RadixSort(indexBits);
		m_byEntity.resize(count);
		for (std::size_t index = 0; index < count; ++index)
			m_byEntity[index] = {Key(m_entries[m_order[index].at].entity), m_order[index].at};
	}
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
