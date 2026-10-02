export module engine.ecs.storage.archetype;
import std;
import engine.core.contracts;

export import engine.ecs.core.component_registry;
export import engine.ecs.storage.chunk;
export import engine.ecs.storage.signature;

export namespace ecs
{

// This cross-module friend has ordinary C++ ownership. Its definition remains
// exported by core.world; a forward declaration must not attach it here.
extern "C++" { class World; }
class ArchetypeRegistry;

class Archetype
{
public:
	struct Slot
	{
		Chunk *chunk{nullptr};
		std::size_t row{0};
	};

	Archetype(Signature signature,
		std::vector<const ComponentInfo *> components,
		std::size_t chunkCapacity);

	const Signature &GetSignature() const noexcept { return m_signature; }
	const ChunkLayout &Layout() const noexcept { return m_layout; }
	const std::vector<std::unique_ptr<Chunk>> &Chunks() const noexcept { return m_chunks; }

	bool Has(ComponentId component) const noexcept
	{
		return component < m_columnOf.size() && m_columnOf[component] != NoColumn;
	}
	std::size_t ColumnIndex(ComponentId component) const noexcept
	{
		if (component >= m_columnOf.size() || m_columnOf[component] == NoColumn)
			return std::numeric_limits<std::size_t>::max();
		return m_columnOf[component];
	}
	std::size_t EntityCount() const noexcept { return m_entityCount; }
	// Its place in creation order (dense, from 0).
	std::uint32_t Index() const noexcept { return m_index; }

	Slot AddEntity(Entity entity);
	Entity RemoveEntity(Chunk *chunk, std::size_t row);

private:
	Slot ReserveEntity();
	void PublishEntity(Slot slot, Entity entity) noexcept;
	void CancelEntity(Slot slot) noexcept;
	void PrepareForRemoval(Chunk *chunk);

	Signature m_signature;
	// Component id -> column (NoColumn: absent), up to the highest id it has: Has / ColumnIndex in one load.
	static constexpr std::uint16_t NoColumn = 0xFFFFu;
	std::vector<std::uint16_t> m_columnOf;
	std::vector<const ComponentInfo *> m_components;
	ChunkLayout m_layout;
	std::vector<std::unique_ptr<Chunk>> m_chunks;
	std::vector<Chunk *> m_availableChunks;
	std::size_t m_entityCount{0};
	std::uint32_t m_index{0};
	// Told whenever one of its chunks turns non-empty or empty, and when it goes into or out of use.
	ArchetypeRegistry *m_registry{nullptr};
	// The archetype graph's edges met so far (World::AddComponent / RemoveComponent): the archetype one component more
	// or one less leads to, so a repeated move skips building and looking up the signature.
	std::vector<std::pair<ComponentId, Archetype *>> m_addEdges, m_removeEdges;

	friend class World;
	friend class ArchetypeRegistry;
};

class ArchetypeRegistry
{
public:
	Archetype &GetOrCreate(const Signature &signature,
		const ComponentRegistry &components,
		std::size_t chunkCapacity,
		bool *created = nullptr);

	std::vector<Archetype *> GetArchetypes();
	std::vector<const Archetype *> GetArchetypes() const;
	std::size_t Count() const noexcept { return m_archetypes.size(); }
	std::uint64_t Revision() const noexcept { return m_revision; }

	// Archetypes in creation order (Archetype::Index).
	Archetype *At(std::uint32_t index) const noexcept { return m_byIndex[index]; }
	// The archetypes holding entities, in signature order (the order GetArchetypes walks): what queries walk, so the
	// many empty archetypes a long game leaves behind cost them nothing.
	const std::vector<Archetype *> &InUse() const noexcept { return m_inUse; }
	// The chunk layout events: each an archetype whose set of non-empty chunks changed (a chunk's first entity in, its
	// last out), numbered from 0; the last LayoutLogSize are kept. A query's prepared chunks stay exact while no
	// event since names an archetype it matches.
	static constexpr std::uint64_t LayoutLogSize = 4096;
	std::uint64_t LayoutEvents() const noexcept { return m_layoutEvents; }
	std::uint32_t LayoutEvent(std::uint64_t event) const noexcept { return m_layoutLog[event & (LayoutLogSize - 1)]; }
	// After storage was rebuilt behind the archetypes' backs (checkpoint load): the in-use list from the entity
	// counts, and every query's prepared chunks invalidated.
	void RebuildLayoutState();

private:
	friend class Archetype;
	void NoteLayout(const Archetype &archetype) noexcept { m_layoutLog[m_layoutEvents++ & (LayoutLogSize - 1)] = archetype.Index(); }
	void NoteUse(Archetype &archetype, bool inUse) noexcept;

	std::map<Signature, std::unique_ptr<Archetype>, SignatureLess> m_archetypes;
	std::vector<Archetype *> m_byIndex;
	std::vector<Archetype *> m_inUse;
	std::array<std::uint32_t, LayoutLogSize> m_layoutLog{};
	std::uint64_t m_layoutEvents{0};
	std::uint64_t m_revision{0};
};

} // namespace ecs

namespace ecs
{

Archetype::Archetype(Signature signature,
	std::vector<const ComponentInfo *> components,
	std::size_t chunkCapacity) :
	m_signature(std::move(signature)),
	m_components(std::move(components)),
	m_layout(ChunkLayout::Build(m_components, chunkCapacity))
{
	if (m_signature.size() >= NoColumn)
		throw std::length_error("ECS archetype has too many components");
	if (!m_signature.empty())
		m_columnOf.assign(static_cast<std::size_t>(*std::max_element(m_signature.begin(), m_signature.end())) + 1, NoColumn);
	for (std::size_t column = 0; column < m_signature.size(); ++column)
		m_columnOf[m_signature[column]] = static_cast<std::uint16_t>(column);
}

Archetype::Slot Archetype::ReserveEntity()
{
	Chunk *target = nullptr;
	if (!m_availableChunks.empty())
	{
		target = m_availableChunks.back();
	}
	else
	{
		m_chunks.push_back(std::make_unique<Chunk>(ChunkLayout(m_layout), m_components));
		target = m_chunks.back().get();
		try
		{
			m_availableChunks.push_back(target);
		}
		catch (...)
		{
			m_chunks.pop_back();
			throw;
		}
	}

	engine::core::Assert(target != nullptr && !target->IsFull());
	return Slot{target, target->ReserveRow()};
}

void Archetype::PublishEntity(const Slot slot, const Entity entity) noexcept
{
	engine::core::Assert(slot.chunk != nullptr);
	slot.chunk->PublishRow(entity, slot.row);
	++m_entityCount;
	if (m_registry != nullptr)
	{
		if (slot.chunk->Size() == 1)
			m_registry->NoteLayout(*this);
		if (m_entityCount == 1)
			m_registry->NoteUse(*this, true);
	}
	if (slot.chunk->IsFull())
	{
		engine::core::Assert(!m_availableChunks.empty() && m_availableChunks.back() == slot.chunk);
		m_availableChunks.pop_back();
	}
}

void Archetype::CancelEntity(const Slot slot) noexcept
{
	engine::core::Assert(slot.chunk != nullptr);
	slot.chunk->CancelRow(slot.row);
}

void Archetype::PrepareForRemoval(Chunk *chunk)
{
	engine::core::Assert(chunk != nullptr);
	engine::core::Assert(!chunk->IsFull() || std::find(m_availableChunks.begin(), m_availableChunks.end(), chunk) == m_availableChunks.end());
	engine::core::Assert(std::find_if(m_chunks.begin(), m_chunks.end(), [chunk](const std::unique_ptr<Chunk> &candidate) {
		return candidate.get() == chunk;
	}) != m_chunks.end());

	if (chunk->IsFull())
		m_availableChunks.reserve(m_availableChunks.size() + 1);
}

Archetype::Slot Archetype::AddEntity(const Entity entity)
{
	const Slot slot = ReserveEntity();
	std::size_t constructed = 0;
	try
	{
		for (const ComponentInfo *info : m_components)
		{
			if (info->constructDefault == nullptr)
				throw std::logic_error("ECS component does not support default construction");
			const std::size_t columnIndex = ColumnIndex(info->id);
			const ChunkLayout::Column &column = m_layout.Columns()[columnIndex];
			info->constructDefault(static_cast<std::byte *>(slot.chunk->ComponentData(columnIndex)) +
				slot.row * column.size);
			++constructed;
		}
	}
	catch (...)
	{
		for (std::size_t index = 0; index < constructed; ++index)
		{
			const ComponentInfo *info = m_components[index];
			const std::size_t columnIndex = ColumnIndex(info->id);
			const ChunkLayout::Column &column = m_layout.Columns()[columnIndex];
			info->destroy(static_cast<std::byte *>(slot.chunk->ComponentData(columnIndex)) +
				slot.row * column.size);
		}
		CancelEntity(slot);
		throw;
	}

	PublishEntity(slot, entity);
	return slot;
}

Entity Archetype::RemoveEntity(Chunk *chunk, std::size_t row)
{
	engine::core::Assert(chunk != nullptr);
	engine::core::Assert(!chunk->IsFull() || std::find(m_availableChunks.begin(), m_availableChunks.end(), chunk) == m_availableChunks.end());
	engine::core::Assert(std::find_if(m_chunks.begin(), m_chunks.end(), [chunk](const std::unique_ptr<Chunk> &candidate) {
		return candidate.get() == chunk;
	}) != m_chunks.end());

	const bool wasFull = chunk->IsFull();
	PrepareForRemoval(chunk);
	const Entity moved = chunk->RemoveSwap(row);
	engine::core::Assert(m_entityCount > 0);
	--m_entityCount;
	if (m_registry != nullptr)
	{
		if (chunk->Size() == 0)
			m_registry->NoteLayout(*this);
		if (m_entityCount == 0)
			m_registry->NoteUse(*this, false);
	}
	if (wasFull)
		m_availableChunks.push_back(chunk);
	return moved;
}

Archetype &ArchetypeRegistry::GetOrCreate(const Signature &signature,
	const ComponentRegistry &components,
	std::size_t chunkCapacity,
	bool *created)
{
	if (!components.IsFrozen())
		throw std::logic_error("ECS component registry must be finalized before creating archetypes");

	Signature canonicalSignature = signature;
	CanonicalizeSignature(canonicalSignature);

	const auto found = m_archetypes.find(canonicalSignature);
	if (found != m_archetypes.end())
	{
		if (created != nullptr)
			*created = false;
		return *found->second;
	}

	std::vector<const ComponentInfo *> infos;
	infos.reserve(canonicalSignature.size());
	for (ComponentId component : canonicalSignature)
		infos.push_back(&components.Get(component));

	auto archetype = std::make_unique<Archetype>(std::move(canonicalSignature), std::move(infos), chunkCapacity);
	Archetype *result = archetype.get();
	// Room first, so going into use never allocates (it happens in noexcept publishing).
	m_byIndex.reserve(m_byIndex.size() + 1);
	m_inUse.reserve(m_byIndex.size() + 1);
	result->m_index = static_cast<std::uint32_t>(m_byIndex.size());
	result->m_registry = this;
	m_archetypes.emplace(result->GetSignature(), std::move(archetype));
	m_byIndex.push_back(result);
	++m_revision;
	if (created != nullptr)
		*created = true;
	return *result;
}

void ArchetypeRegistry::NoteUse(Archetype &archetype, const bool inUse) noexcept
{
	const auto at = std::lower_bound(m_inUse.begin(), m_inUse.end(), &archetype, [](const Archetype *left, const Archetype *right) {
		return SignatureLess{}(left->GetSignature(), right->GetSignature());
	});
	if (inUse)
	{
		if (at == m_inUse.end() || *at != &archetype)
			m_inUse.insert(at, &archetype); // capacity reserved at creation: no allocation
	}
	else if (at != m_inUse.end() && *at == &archetype)
		m_inUse.erase(at);
}

void ArchetypeRegistry::RebuildLayoutState()
{
	m_inUse.clear();
	for (const auto &entry : m_archetypes)
		if (entry.second->EntityCount() != 0)
			m_inUse.push_back(entry.second.get());
	m_layoutEvents += LayoutLogSize + 1;
}

std::vector<Archetype *> ArchetypeRegistry::GetArchetypes()
{
	std::vector<Archetype *> result;
	result.reserve(m_archetypes.size());
	for (const auto &entry : m_archetypes)
		result.push_back(entry.second.get());
	return result;
}

std::vector<const Archetype *> ArchetypeRegistry::GetArchetypes() const
{
	std::vector<const Archetype *> result;
	result.reserve(m_archetypes.size());
	for (const auto &entry : m_archetypes)
		result.push_back(entry.second.get());
	return result;
}

} // namespace ecs
