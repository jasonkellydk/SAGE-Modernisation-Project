export module engine.ecs.storage.side_table;
import std;

export import engine.ecs.core.entity;

// Side-table storage: components kept outside the archetypes, one sparse set
// per component type (as EnTT's pools, and flecs' DontFragment components).
// Adding or removing one never moves the entity, never changes chunk layout,
// the state hash or checkpoints, so presentation state (render objects,
// animation clocks, sound handles) can live on simulation entities without
// touching determinism.
//
// Layout (structure of arrays): a paged sparse array maps entity index to a
// dense slot; the dense entity column and the dense value column are packed,
// values in fixed pages so pointers stay stable while the table grows.
// Removal swaps the last slot into the hole.
export namespace ecs
{
class SideTableBase
{
public:
	virtual ~SideTableBase() = default;
	virtual bool Contains(Entity entity) const noexcept = 0;
	// Default-constructs, or move-constructs from `value` when given. False if already there.
	virtual bool Insert(Entity entity, void *value) = 0;
	// Replaces the value (inserting when absent); `value` is moved from.
	virtual void Assign(Entity entity, void *value) = 0;
	virtual bool Erase(Entity entity) = 0;
	virtual void *Find(Entity entity) noexcept = 0;
	virtual std::size_t Size() const noexcept = 0;
	virtual void Clear() noexcept = 0;
};

template<typename T>
class SideTable final : public SideTableBase
{
public:
	static constexpr std::size_t SparsePage = 4096;
	static constexpr std::size_t ValuePage = 1024;
	static constexpr std::uint32_t Empty = std::numeric_limits<std::uint32_t>::max();

	// Called with each value about to go (erased, or its entity destroyed):
	// the owner releases what it holds (sound handles, emitters).
	using RemoveHook = std::function<void(Entity, T &)>;

	SideTable() = default;
	SideTable(const SideTable &) = delete;
	SideTable &operator=(const SideTable &) = delete;
	~SideTable() override { DestroyValues(); }

	void OnRemove(RemoveHook hook) { m_onRemove = std::move(hook); }

	bool Contains(Entity entity) const noexcept override
	{
		const std::uint32_t slot = SlotOf(entity.index);
		return slot != Empty && m_entities[slot] == entity;
	}

	T *Get(Entity entity) noexcept
	{
		const std::uint32_t slot = SlotOf(entity.index);
		return slot != Empty && m_entities[slot] == entity ? &ValueAt(slot) : nullptr;
	}
	const T *Get(Entity entity) const noexcept { return const_cast<SideTable *>(this)->Get(entity); }

	template<typename... Args>
	T *Emplace(Entity entity, Args &&...args)
	{
		if (const std::uint32_t slot = SlotOf(entity.index); slot != Empty)
		{
			if (m_entities[slot] == entity)
				return nullptr;
			// A stale entry of a destroyed generation still holds the index.
			EraseSlot(slot, true);
		}
		const auto slot = static_cast<std::uint32_t>(m_entities.size());
		EnsureValuePage(slot);
		new (&ValueAt(slot)) T(std::forward<Args>(args)...);
		m_entities.push_back(entity);
		SparseSlot(entity.index) = slot;
		return &ValueAt(slot);
	}

	bool Insert(Entity entity, void *value) override
	{
		return value == nullptr ? Emplace(entity) != nullptr : Emplace(entity, std::move(*static_cast<T *>(value))) != nullptr;
	}

	void Assign(Entity entity, void *value) override
	{
		if (T *existing = Get(entity))
			*existing = std::move(*static_cast<T *>(value));
		else
			Emplace(entity, std::move(*static_cast<T *>(value)));
	}

	bool Erase(Entity entity) override
	{
		const std::uint32_t slot = SlotOf(entity.index);
		if (slot == Empty || m_entities[slot] != entity)
			return false;
		EraseSlot(slot, true);
		return true;
	}

	void *Find(Entity entity) noexcept override { return Get(entity); }
	std::size_t Size() const noexcept override { return m_entities.size(); }

	void Clear() noexcept override
	{
		while (!m_entities.empty())
			EraseSlot(static_cast<std::uint32_t>(m_entities.size() - 1), true);
	}

	// Dense iteration (structure of arrays): Entities()[i] owns Value(i).
	std::span<const Entity> Entities() const noexcept { return m_entities; }
	T &Value(std::size_t dense) noexcept { return ValueAt(static_cast<std::uint32_t>(dense)); }
	const T &Value(std::size_t dense) const noexcept { return const_cast<SideTable *>(this)->ValueAt(static_cast<std::uint32_t>(dense)); }

	template<typename Visit>
	void ForEach(Visit &&visit)
	{
		for (std::size_t dense = 0; dense < m_entities.size(); ++dense)
			visit(m_entities[dense], Value(dense));
	}

private:
	std::uint32_t SlotOf(EntityIndex index) const noexcept
	{
		const std::size_t page = index / SparsePage;
		if (page >= m_sparse.size() || m_sparse[page] == nullptr)
			return Empty;
		return m_sparse[page][index % SparsePage];
	}

	std::uint32_t &SparseSlot(EntityIndex index)
	{
		const std::size_t page = index / SparsePage;
		if (page >= m_sparse.size())
			m_sparse.resize(page + 1);
		if (m_sparse[page] == nullptr)
		{
			m_sparse[page] = std::make_unique<std::uint32_t[]>(SparsePage);
			std::fill_n(m_sparse[page].get(), SparsePage, Empty);
		}
		return m_sparse[page][index % SparsePage];
	}

	T &ValueAt(std::uint32_t slot) noexcept
	{
		return *std::launder(reinterpret_cast<T *>(m_values[slot / ValuePage].get() + (slot % ValuePage) * sizeof(T)));
	}

	void EnsureValuePage(std::uint32_t slot)
	{
		const std::size_t page = slot / ValuePage;
		while (m_values.size() <= page)
			m_values.emplace_back(static_cast<std::byte *>(::operator new[](ValuePage * sizeof(T), std::align_val_t(alignof(T)))));
	}

	void EraseSlot(std::uint32_t slot, bool notify)
	{
		const Entity entity = m_entities[slot];
		if (notify && m_onRemove)
			m_onRemove(entity, ValueAt(slot));
		const auto last = static_cast<std::uint32_t>(m_entities.size() - 1);
		if (slot != last)
		{
			ValueAt(slot) = std::move(ValueAt(last));
			m_entities[slot] = m_entities[last];
			SparseSlot(m_entities[slot].index) = slot;
		}
		std::destroy_at(&ValueAt(last));
		m_entities.pop_back();
		SparseSlot(entity.index) = Empty;
	}

	void DestroyValues() noexcept
	{
		for (std::size_t dense = 0; dense < m_entities.size(); ++dense)
			std::destroy_at(&ValueAt(static_cast<std::uint32_t>(dense)));
		m_entities.clear();
	}

	struct AlignedDelete
	{
		void operator()(std::byte *bytes) const noexcept { ::operator delete[](bytes, std::align_val_t(alignof(T))); }
	};

	std::vector<std::unique_ptr<std::uint32_t[]>> m_sparse;
	std::vector<Entity> m_entities;
	std::vector<std::unique_ptr<std::byte[], AlignedDelete>> m_values;
	RemoveHook m_onRemove;
};
}
