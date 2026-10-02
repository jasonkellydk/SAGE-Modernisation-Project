export module engine.ecs.core.world;
import std;
import engine.core.contracts;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import engine.ecs.storage.archetype;
export import engine.ecs.core.resource_store;

export namespace ecs
{

extern "C++"
{
class CommandBuffer;
class Scheduler;
struct WorldTestAccess;
}

struct WorldConfig
{
	std::size_t chunkCapacity{ChunkLayout::DefaultCapacity};
};

// World and its cross-module collaborators use ordinary C++ linkage so that
// forward declarations and World::Commit definitions share one entity across
// named module boundaries. The public interface is still imported from here.
extern "C++"
{
class World
{
public:
	explicit World(WorldConfig config = {});
	~World() = default;

	World(const World &) = delete;
	World &operator=(const World &) = delete;

	// World-owned resources (see resource_store): the composition root
	// inserts them; systems reach them through their context.
	template<ResourceType T, typename... Args>
	T &EmplaceResource(Args &&...args)
	{
		return m_resources.Emplace<T>(std::forward<Args>(args)...);
	}
	template<ResourceType T>
	T &Resource() { return m_resources.Get<T>(); }
	template<ResourceType T>
	const T &Resource() const { return m_resources.Get<T>(); }
	template<ResourceType T>
	T *FindResource() noexcept { return m_resources.Find<T>(); }
	template<ResourceType T>
	const T *FindResource() const noexcept { return m_resources.Find<T>(); }

	template<typename... Components>
	Entity Create()
	{
		RequireComponentsFinalized();
		RequireStructuralMutationAllowed();

		Signature signature;
		signature.reserve(sizeof...(Components));
		(signature.push_back(GetDefaultConstructibleComponent<Components>()), ...);
		CanonicalizeSignature(signature);

		const Entity entity = AllocateEntity();
		try
		{
			Archetype &archetype = GetOrCreateArchetype(signature);
			const Archetype::Slot slot = archetype.AddEntity(entity);
			SetLocation(entity, EntityLocation{&archetype, slot.chunk, slot.row});
		}
		catch (...)
		{
			ReleaseUnconstructedEntity(entity);
			throw;
		}
		return entity;
	}

	template<typename T>
	ComponentId RegisterComponent()
	{
		return m_components.Register<T>();
	}

	void FinalizeComponents()
	{
		m_components.Finalize();
		m_sideTables.resize(m_components.Count());
		for (ComponentId id = 0; id < m_components.Count(); ++id)
			if (const ComponentInfo &info = m_components.Get(id); info.storage == ComponentStorage::SideTable)
				m_sideTables[id] = info.createSideTable();
	}

	// A side-table component's table (throws for table components).
	template<typename T>
	SideTable<T> &Side()
	{
		return *static_cast<SideTable<T> *>(SideTableOf(GetRegisteredComponent<T>()));
	}
	template<typename T>
	const SideTable<T> &Side() const
	{
		return *static_cast<const SideTable<T> *>(const_cast<World *>(this)->SideTableOf(GetRegisteredComponent<T>()));
	}

	bool ComponentsFinalized() const noexcept
	{
		return m_components.IsFrozen();
	}

	// Diagnostic/preflight query. The simulation owner still serializes scheduler
	// launch and external lifecycle operations; this is not an exclusion lock.
	bool IsScheduledExecutionActive() const noexcept { return m_scheduledExecutionActive; }

	void Commit(CommandBuffer &commands);
	void Commit(std::span<CommandBuffer *> commandBuffers);

	bool IsAlive(Entity entity) const noexcept;
	bool Destroy(Entity entity);

	template<typename T>
	bool Has(Entity entity) const
	{
		RequireComponentsFinalized();
		if (!IsAlive(entity))
			return false;
		const ComponentId component = GetRegisteredComponent<T>();
		if (SideTableBase *table = SideTableIfAny(component))
			return table->Contains(entity);
		return m_records[entity.index].location.archetype->Has(component);
	}

	template<typename T>
	bool Add(Entity entity)
	{
		RequireComponentsFinalized();
		RequireStructuralMutationAllowed();
		if (!IsAlive(entity))
			return false;
		return AddComponent(entity, GetRegisteredComponent<T>(), nullptr);
	}

	template<typename T>
	bool Remove(Entity entity)
	{
		RequireComponentsFinalized();
		RequireStructuralMutationAllowed();
		if (!IsAlive(entity))
			return false;
		return RemoveComponent(entity, GetRegisteredComponent<T>());
	}

	template<typename T>
	T *Get(Entity entity)
	{
		RequireComponentsFinalized();
		const ComponentId component = GetRegisteredComponent<T>();
		if (!IsAlive(entity))
			return nullptr;
		if (SideTableBase *table = SideTableIfAny(component))
			return static_cast<T *>(table->Find(entity));

		EntityRecord &record = m_records[entity.index];
		const std::size_t column = record.location.archetype->ColumnIndex(component);
		if (column == std::numeric_limits<std::size_t>::max())
			return nullptr;
		return static_cast<T *>(record.location.chunk->ComponentData(column)) + record.location.row;
	}

	template<typename T>
	const T *Get(Entity entity) const
	{
		RequireComponentsFinalized();
		const ComponentId component = GetRegisteredComponent<T>();
		if (!IsAlive(entity))
			return nullptr;
		if (SideTableBase *table = SideTableIfAny(component))
			return static_cast<const T *>(table->Find(entity));

		const EntityRecord &record = m_records[entity.index];
		const std::size_t column = record.location.archetype->ColumnIndex(component);
		if (column == std::numeric_limits<std::size_t>::max())
			return nullptr;
		return static_cast<const T *>(record.location.chunk->ComponentData(column)) + record.location.row;
	}

	std::size_t EntityCount() const noexcept { return m_entityCount; }
	std::size_t ArchetypeCount() const noexcept { return m_archetypes.Count(); }
	std::uint64_t ArchetypeRevision() const noexcept { return m_archetypes.Revision(); }
	// For queries: archetypes by creation index, those in use in signature order, and the chunk layout events
	// (ArchetypeRegistry).
	Archetype *ArchetypeAt(std::uint32_t index) const noexcept { return m_archetypes.At(index); }
	const std::vector<Archetype *> &ArchetypesInUse() const noexcept { return m_archetypes.InUse(); }
	std::uint64_t ArchetypeLayoutEvents() const noexcept { return m_archetypes.LayoutEvents(); }
	std::uint32_t ArchetypeLayoutEvent(std::uint64_t event) const noexcept { return m_archetypes.LayoutEvent(event); }
	std::vector<Archetype *> GetArchetypes() { return m_archetypes.GetArchetypes(); }
	std::vector<const Archetype *> GetArchetypes() const { return m_archetypes.GetArchetypes(); }
	const ComponentRegistry &Components() const noexcept { return m_components; }

	// Deterministic hash of all Serializable component state, entity identities
	// and allocator state, in canonical archetype/chunk/row order. Equal worlds
	// hash equal on every platform. Throws if a Serializable component in use
	// cannot be hashed (see ComponentInfo::hashState).
	StateHashValue StateHash() const;

	// Everything StateHash covers, as bytes: the entity allocator, and every
	// archetype's chunks, rows and Serializable values in their exact layout,
	// so a world loaded from it iterates, allocates and hashes as this one
	// does (Transient components come back default-constructed). Throws if a
	// Serializable component in use cannot be saved (see ComponentInfo).
	void SaveCheckpoint(engine::core::serialization::ByteWriter &writer) const;
	// Into a finalized world with the same components that holds no entities
	// and never has. False (leaving the world empty) for malformed data or a
	// different component schema.
	bool LoadCheckpoint(engine::core::serialization::ByteReader &reader);

private:
	struct EntityLocation
	{
		Archetype *archetype{nullptr};
		Chunk *chunk{nullptr};
		std::size_t row{0};
	};

	struct EntityRecord
	{
		EntityGeneration generation{1};
		bool alive{false};
		bool retired{false};
		EntityLocation location{};
	};

	Entity AllocateEntity();
	SideTableBase *SideTableIfAny(ComponentId component) const noexcept
	{
		return component < m_sideTables.size() ? m_sideTables[component].get() : nullptr;
	}
	SideTableBase *SideTableOf(ComponentId component);
	void ReleaseUnconstructedEntity(Entity entity) noexcept;
	void SetLocation(Entity entity, EntityLocation location) noexcept;
	void RequireComponentsFinalized() const;
	void RequireStructuralMutationAllowed() const;
	void BeginScheduledExecution() noexcept;
	void EndScheduledExecution() noexcept;
	void BeginCommandPlayback() noexcept;
	void EndCommandPlayback() noexcept;

	template<typename T>
	ComponentId GetRegisteredComponent() const
	{
		const ComponentId component = m_components.TryGet<T>();
		if (component == InvalidComponentId)
			throw std::logic_error("ECS component was not registered before finalization");
		return component;
	}

	template<typename T>
	ComponentId GetDefaultConstructibleComponent() const
	{
		const ComponentId component = GetRegisteredComponent<T>();
		if (m_components.Get(component).constructDefault == nullptr)
			throw std::logic_error("ECS component does not support default construction");
		return component;
	}

	bool AddComponent(Entity entity, ComponentId component, void *value);
	// Replaces the component's value (adds it when missing); `value` is moved from.
	bool SetComponent(Entity entity, ComponentId component, void *value);
	bool RemoveComponent(Entity entity, ComponentId component);

	Archetype &GetOrCreateArchetype(const Signature &signature);
	void MoveEntity(Entity entity,
		const Signature &targetSignature,
		ComponentId initializedComponent = InvalidComponentId,
		void *initializedValue = nullptr);
	void MoveEntityTo(Entity entity,
		Archetype &target,
		ComponentId initializedComponent = InvalidComponentId,
		void *initializedValue = nullptr);
	// The archetype with `component` added to (or removed from) `source`'s signature: from the edges already met, else
	// found or made by signature (and remembered).
	Archetype &ArchetypeWith(Archetype &source, ComponentId component);
	Archetype &ArchetypeWithout(Archetype &source, ComponentId component);

	friend class CommandBuffer;
	friend class Scheduler;
	// Test seam for exercising generation exhaustion without billions of cycles.
	friend struct WorldTestAccess;
	void ClearForFailedLoad() noexcept;

	WorldConfig m_config;
	ComponentRegistry m_components;
	ArchetypeRegistry m_archetypes;
	// Per component id: its side table, or null for table components.
	std::vector<std::unique_ptr<SideTableBase>> m_sideTables;
	ResourceStore m_resources;
	std::vector<EntityRecord> m_records;
	std::vector<EntityIndex> m_freeIndices;
	std::size_t m_entityCount{0};
	bool m_scheduledExecutionActive{false};
	bool m_commandPlaybackActive{false};
};
} // extern "C++"

} // namespace ecs

namespace ecs
{

extern "C++"
{
World::World(WorldConfig config) :
	m_config(config)
{
}

Entity World::AllocateEntity()
{
	if (!m_freeIndices.empty())
	{
		const EntityIndex index = m_freeIndices.back();
		m_freeIndices.pop_back();
		EntityRecord &record = m_records[index];
		engine::core::Assert(!record.retired);
		record.alive = true;
		record.location = EntityLocation{};
		++m_entityCount;
		return Entity{index, record.generation};
	}

	if (m_records.size() >= static_cast<std::size_t>(Entity::InvalidIndex))
		throw std::length_error("ECS entity registry exhausted");

	const EntityIndex index = static_cast<EntityIndex>(m_records.size());
	m_records.push_back(EntityRecord{});
	EntityRecord &record = m_records.back();
	record.alive = true;
	++m_entityCount;
	return Entity{index, record.generation};
}

void World::ReleaseUnconstructedEntity(Entity entity) noexcept
{
	engine::core::Assert(entity.index < m_records.size());
	EntityRecord &record = m_records[entity.index];
	record.alive = false;
	record.location = EntityLocation{};
	engine::core::Assert(m_entityCount > 0);
	--m_entityCount;
	m_freeIndices.push_back(entity.index);
}

void World::SetLocation(Entity entity, EntityLocation location) noexcept
{
	engine::core::Assert(entity.index < m_records.size());
	EntityRecord &record = m_records[entity.index];
	engine::core::Assert(record.alive && record.generation == entity.generation);
	record.location = location;
}

void World::RequireComponentsFinalized() const
{
	if (!m_components.IsFrozen())
		throw std::logic_error("ECS component registry must be finalized before ECS state operations");
}

void World::RequireStructuralMutationAllowed() const
{
	if (m_scheduledExecutionActive && !m_commandPlaybackActive)
		throw std::logic_error("Direct ECS structural mutation is forbidden during scheduled execution; use a command buffer");
}

void World::BeginScheduledExecution() noexcept
{
	engine::core::Assert(!m_scheduledExecutionActive);
	engine::core::Assert(!m_commandPlaybackActive);
	m_scheduledExecutionActive = true;
}

void World::EndScheduledExecution() noexcept
{
	engine::core::Assert(m_scheduledExecutionActive);
	engine::core::Assert(!m_commandPlaybackActive);
	m_scheduledExecutionActive = false;
}

void World::BeginCommandPlayback() noexcept
{
	engine::core::Assert(!m_commandPlaybackActive);
	m_commandPlaybackActive = true;
}

void World::EndCommandPlayback() noexcept
{
	engine::core::Assert(m_commandPlaybackActive);
	m_commandPlaybackActive = false;
}

SideTableBase *World::SideTableOf(ComponentId component)
{
	SideTableBase *table = SideTableIfAny(component);
	if (table == nullptr)
		throw std::logic_error("ECS component '" + std::string(m_components.Get(component).stableName) + "' is not a side-table component");
	return table;
}

Archetype &World::GetOrCreateArchetype(const Signature &signature)
{
	RequireComponentsFinalized();
	for (const ComponentId component : signature)
		if (SideTableIfAny(component) != nullptr)
			throw std::logic_error("ECS side-table component '" + std::string(m_components.Get(component).stableName) +
				"' cannot be part of an archetype: add it to an existing entity");
	return m_archetypes.GetOrCreate(signature, m_components, m_config.chunkCapacity);
}

StateHashValue World::StateHash() const
{
	RequireComponentsFinalized();
	StateHasher hasher;
	// Allocator state decides future entity IDs, so it is part of the state.
	hasher.AppendU64(m_records.size());
	for (const EntityRecord &record : m_records)
		hasher.AppendU64((static_cast<std::uint64_t>(record.generation) << 2) |
			(record.alive ? 1u : 0u) | (record.retired ? 2u : 0u));
	hasher.AppendU64(m_freeIndices.size());
	for (const EntityIndex index : m_freeIndices)
		hasher.AppendU64(index);

	for (const Archetype *archetype : m_archetypes.GetArchetypes())
	{
		if (archetype->EntityCount() == 0)
			continue;
		const auto &columns = archetype->Layout().Columns();
		for (const auto &column : columns)
		{
			const ComponentInfo &info = m_components.Get(column.component);
			if (info.persistence == PersistencePolicy::Serializable && info.hashState == nullptr)
				throw std::logic_error("ECS component '" + std::string(info.stableName) +
					"' is Serializable but has padding or floats; make it padding-free or provide ComponentTraits::HashState");
		}
		hasher.AppendU64(archetype->GetSignature().size());
		for (const ComponentId component : archetype->GetSignature())
			hasher.AppendU64(m_components.Get(component).stableKey);
		for (const auto &chunk : archetype->Chunks())
		{
			if (chunk->Size() == 0)
				continue;
			for (std::size_t row = 0; row < chunk->Size(); ++row)
				hasher.AppendU64((static_cast<std::uint64_t>(chunk->Entities()[row].index) << 32) | chunk->Entities()[row].generation);
			for (std::size_t column = 0; column < columns.size(); ++column)
			{
				const ComponentInfo &info = m_components.Get(columns[column].component);
				if (info.hashState != nullptr)
					info.hashState(chunk->ComponentData(column), chunk->Size(), hasher);
			}
		}
	}
	return hasher.Value();
}

bool World::IsAlive(Entity entity) const noexcept
{
	return entity.IsValid() && entity.index < m_records.size() &&
		m_records[entity.index].alive && m_records[entity.index].generation == entity.generation;
}

bool World::Destroy(Entity entity)
{
	RequireStructuralMutationAllowed();
	if (!IsAlive(entity))
		return false;

	// Its side-table components go with it (their remove hooks run first).
	for (const auto &table : m_sideTables)
		if (table)
			table->Erase(entity);

	EntityRecord &record = m_records[entity.index];
	const EntityLocation location = record.location;
	const Entity moved = location.archetype->RemoveEntity(location.chunk, location.row);
	if (moved.IsValid())
	{
		EntityRecord &movedRecord = m_records[moved.index];
		movedRecord.location = EntityLocation{location.archetype, location.chunk, location.row};
	}

	record.alive = false;
	record.location = EntityLocation{};
	if (record.generation == std::numeric_limits<EntityGeneration>::max())
	{
		// The maximum generation has already been issued. Retiring the index
		// prevents an old handle from becoming valid again after wraparound.
		record.retired = true;
	}
	else
	{
		++record.generation;
		m_freeIndices.push_back(entity.index);
	}
	engine::core::Assert(m_entityCount > 0);
	--m_entityCount;
	return true;
}

bool World::AddComponent(Entity entity, const ComponentId component, void *value)
{
	RequireComponentsFinalized();
	const ComponentInfo *info = m_components.TryGet(component);
	if (component == InvalidComponentId || info == nullptr)
		throw std::logic_error("ECS component ID is not registered");
	if (value == nullptr && info->constructDefault == nullptr)
		throw std::logic_error("ECS component does not support default construction");
	if (!IsAlive(entity))
		return false;
	if (SideTableBase *table = SideTableIfAny(component))
		return table->Insert(entity, value);

	EntityRecord &record = m_records[entity.index];
	if (record.location.archetype->Has(component))
		return false;

	Archetype &target = ArchetypeWith(*record.location.archetype, component);
	if (value == nullptr)
		MoveEntityTo(entity, target);
	else
		MoveEntityTo(entity, target, component, value);
	return true;
}

Archetype &World::ArchetypeWith(Archetype &source, const ComponentId component)
{
	for (const auto &[edge, target] : source.m_addEdges)
		if (edge == component)
			return *target;
	Signature signature = source.GetSignature();
	signature.push_back(component);
	CanonicalizeSignature(signature);
	Archetype &target = GetOrCreateArchetype(signature);
	source.m_addEdges.emplace_back(component, &target);
	return target;
}

Archetype &World::ArchetypeWithout(Archetype &source, const ComponentId component)
{
	for (const auto &[edge, target] : source.m_removeEdges)
		if (edge == component)
			return *target;
	Signature signature = source.GetSignature();
	signature.erase(std::remove(signature.begin(), signature.end(), component), signature.end());
	Archetype &target = GetOrCreateArchetype(signature);
	source.m_removeEdges.emplace_back(component, &target);
	return target;
}

bool World::SetComponent(Entity entity, const ComponentId component, void *value)
{
	RequireComponentsFinalized();
	if (!IsAlive(entity) || value == nullptr)
		return false;
	if (SideTableBase *table = SideTableIfAny(component))
	{
		table->Assign(entity, value);
		return true;
	}
	const EntityRecord &record = m_records[entity.index];
	const std::size_t column = record.location.archetype->ColumnIndex(component);
	if (column == std::numeric_limits<std::size_t>::max())
		return AddComponent(entity, component, value);
	const ComponentInfo &info = m_components.Get(component);
	void *slot = static_cast<std::byte *>(record.location.chunk->ComponentData(column)) + record.location.row * info.size;
	info.destroy(slot);
	info.constructMove(slot, value);
	return true;
}

bool World::RemoveComponent(const Entity entity, const ComponentId component)
{
	RequireComponentsFinalized();
	if (component == InvalidComponentId || m_components.TryGet(component) == nullptr)
		throw std::logic_error("ECS component ID is not registered");
	if (!IsAlive(entity))
		return false;
	if (SideTableBase *table = SideTableIfAny(component))
		return table->Erase(entity);

	EntityRecord &record = m_records[entity.index];
	if (!record.location.archetype->Has(component))
		return false;

	MoveEntityTo(entity, ArchetypeWithout(*record.location.archetype, component));
	return true;
}

void World::MoveEntity(Entity entity,
	const Signature &targetSignature,
	const ComponentId initializedComponent,
	void *initializedValue)
{
	MoveEntityTo(entity, GetOrCreateArchetype(targetSignature), initializedComponent, initializedValue);
}

void World::MoveEntityTo(Entity entity,
	Archetype &target,
	const ComponentId initializedComponent,
	void *initializedValue)
{
	engine::core::Assert(IsAlive(entity));
	engine::core::Assert(initializedValue == nullptr || initializedComponent != InvalidComponentId);
	EntityRecord &record = m_records[entity.index];
	const EntityLocation sourceLocation = record.location;
	Archetype *source = sourceLocation.archetype;
	if (source == &target)
		return;

	// Ensure the source availability list cannot allocate after source values
	// have been moved. This is a cold structural-path preflight.
	source->PrepareForRemoval(sourceLocation.chunk);
	const Archetype::Slot destination = target.ReserveEntity();
	std::size_t constructedNew = 0;
	try
	{
		// New components are constructed before any source component is moved.
		// Their default constructors may throw, while registered component moves
		// are noexcept by contract.
		for (ComponentId component : target.GetSignature())
		{
			if (source->Has(component))
				continue;

			const std::size_t destinationColumn = target.ColumnIndex(component);
			const ComponentInfo &info = m_components.Get(component);
			void *destinationData = destination.chunk->ComponentData(destinationColumn);
			const ChunkLayout::Column &destinationLayout = target.Layout().Columns()[destinationColumn];
			void *destinationElement = static_cast<std::byte *>(destinationData) +
				destination.row * destinationLayout.size;
			if (component == initializedComponent && initializedValue != nullptr)
			{
				info.constructMove(destinationElement, initializedValue);
			}
			else
			{
				if (info.constructDefault == nullptr)
					throw std::logic_error("ECS component does not support default construction");
				info.constructDefault(destinationElement);
			}
			++constructedNew;
		}
	}
	catch (...)
	{
		std::size_t remaining = constructedNew;
		for (ComponentId component : target.GetSignature())
		{
			if (source->Has(component) || remaining == 0)
				continue;

			const std::size_t destinationColumn = target.ColumnIndex(component);
			const ComponentInfo &info = m_components.Get(component);
			const ChunkLayout::Column &destinationLayout = target.Layout().Columns()[destinationColumn];
			void *destinationElement = static_cast<std::byte *>(destination.chunk->ComponentData(destinationColumn)) +
				destination.row * destinationLayout.size;
			info.destroy(destinationElement);
			--remaining;
		}
		target.CancelEntity(destination);
		throw;
	}

	for (ComponentId component : source->GetSignature())
	{
		if (!target.Has(component))
			continue;

		const std::size_t destinationColumn = target.ColumnIndex(component);
		const std::size_t sourceColumn = source->ColumnIndex(component);
		const ComponentInfo &info = m_components.Get(component);
		void *destinationData = destination.chunk->ComponentData(destinationColumn);
		void *sourceData = sourceLocation.chunk->ComponentData(sourceColumn);
		const ChunkLayout::Column &destinationLayout = target.Layout().Columns()[destinationColumn];
		const ChunkLayout::Column &sourceLayout = source->Layout().Columns()[sourceColumn];
		void *destinationElement = static_cast<std::byte *>(destinationData) + destination.row * destinationLayout.size;
		void *sourceElement = static_cast<std::byte *>(sourceData) + sourceLocation.row * sourceLayout.size;
		info.constructMove(destinationElement, sourceElement);
	}

	target.PublishEntity(destination, entity);
	const Entity moved = source->RemoveEntity(sourceLocation.chunk, sourceLocation.row);
	if (moved.IsValid())
	{
		EntityRecord &movedRecord = m_records[moved.index];
		movedRecord.location = EntityLocation{source, sourceLocation.chunk, sourceLocation.row};
	}
	record.location = EntityLocation{&target, destination.chunk, destination.row};
}

} // extern "C++"
} // namespace ecs
