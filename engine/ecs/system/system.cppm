export module engine.ecs.system.system;
import std;

export import engine.ecs.commands.command_buffer;
export import engine.ecs.query.query;
export import engine.time.simulation_time;

export namespace ecs
{

using SystemId = std::uint32_t;
using SystemKey = std::uint64_t;

inline constexpr SystemId InvalidSystemId = (std::numeric_limits<SystemId>::max)();

enum class SystemPhase : std::uint8_t
{
	PreSimulation = 0,
	Simulation = 1,
	PostSimulation = 2
};

inline constexpr std::size_t SystemPhaseCount = 3;

constexpr std::size_t SystemPhaseIndex(const SystemPhase phase) noexcept
{
	return static_cast<std::size_t>(phase);
}

constexpr SystemKey HashSystemKey(const std::string_view stableName) noexcept
{
	SystemKey hash = 14695981039346656037ull;
	for (const char character : stableName)
	{
		hash ^= static_cast<SystemKey>(static_cast<std::uint8_t>(character));
		hash *= 1099511628211ull;
	}
	return hash;
}

template<typename... Systems>
struct SystemTypeList
{
};

template<typename T>
struct SystemTraits;

// System groups: a named umbrella over systems and nested groups (e.g.
// Rendering > Models, Particles, Terrain). A system joins one with
// `using Group = G;` in its SystemTraits; a group may sit in a parent group
// (`using Parent = P;`) and orders against other groups with Before / After
// (SystemTypeList of groups). Group ordering applies to every member of the
// groups (within a phase) and is soft: a group with no registered members
// orders nothing, so a whole group can be left out (or rewritten) without
// touching anything that orders against it.
template<typename G>
struct SystemGroupTraits;

using SystemGroupKey = std::uint64_t;

struct SystemDependency
{
	const std::type_info *type{&typeid(void)};
	SystemKey stableKey{};
	std::string_view stableName{};
};

class SystemContext;

// Shared per-world state that is not per-entity (terrain grid, spatial index,
// player ledgers, settings, seeds) is owned by the world (resource_store).
// Systems hold no data: they DECLARE the access they need, the scheduler
// orders conflicting writers deterministically, and the context hands the
// resources out, checked against the declaration:
//   using Resources = ecs::Resources<ecs::Read<TerrainGrid>, ecs::Write<PowerLedger>>;
//   const TerrainGrid &grid = context.Read<TerrainGrid>();
//   PowerLedger &ledger = context.Write<PowerLedger>();

struct ResourceAccessDescriptor
{
	ResourceKey key{};
	std::string_view stableName{};
	AccessMode mode{AccessMode::None};
};

template<typename... Accesses>
struct Resources
{
	static void Append(std::vector<ResourceAccessDescriptor> &out)
	{
		(AppendOne<Accesses>(out), ...);
	}

private:
	template<typename Access>
	static void AppendOne(std::vector<ResourceAccessDescriptor> &out)
	{
		using T = typename Access::ComponentType;
		static_assert(!Access::IsOptional && !Access::IsExcluded, "Resource access must be Read<T> or Write<T>");
		static_assert(requires { { ResourceTraits<T>::StableName } -> std::convertible_to<std::string_view>; },
			"Resource types must specialize ecs::ResourceTraits with a StableName");
		out.push_back({ResourceKeyOf<T>(), ResourceTraits<T>::StableName, Access::Mode});
	}
};

// Read-only random access to other entities (targets, owners, containers),
// declared up front so the scheduler orders it against writers:
//   using Lookup = ecs::Lookup<ecs::Read<Health>, ecs::Read<Transform>>;
//   const auto lookup = context.Lookup<Lookup>();
//   if (const Health *health = lookup.Get<Health>(target)) ...
// Writes to other entities go through context.Commands(), never a lookup.
template<typename... Accesses>
struct Lookup
{
	static_assert(((Accesses::Mode == AccessMode::Read && !Accesses::IsOptional && !Accesses::IsExcluded) && ...),
		"ecs::Lookup only grants read access: use ecs::Read<T>");

	template<typename T>
	static constexpr bool Allows = (std::is_same_v<T, typename Accesses::ComponentType> || ...);

	static std::vector<AccessDescriptor> ResolveAccesses(const ComponentRegistry &components)
	{
		return {AccessDescriptor{components.TryGet<typename Accesses::ComponentType>(), AccessMode::Read, false, false}...};
	}
};

// Side-table components a system reads or writes, declared up front so the
// scheduler orders it against other users of the same tables:
//   using SideTables = ecs::SideTables<ecs::Write<Look>, ecs::Read<Clock>>;
//   auto &looks = context.Side<SideTables, Look>();
// Chunk systems may change the values of the entities in their chunk;
// adding and removing entries goes through context.Commands().
template<typename... Accesses>
struct SideTables
{
	static_assert(((!Accesses::IsOptional && !Accesses::IsExcluded) && ...), "ecs::SideTables takes ecs::Read<T> or ecs::Write<T>");

	template<typename T>
	static constexpr bool Allows = (std::is_same_v<T, typename Accesses::ComponentType> || ...);
	template<typename T>
	static constexpr bool AllowsWrite = ((std::is_same_v<T, typename Accesses::ComponentType> && Accesses::Mode == AccessMode::Write) || ...);

	static std::vector<AccessDescriptor> ResolveAccesses(const ComponentRegistry &components)
	{
		return {AccessDescriptor{components.TryGet<typename Accesses::ComponentType>(), Accesses::Mode, false, false}...};
	}
};

template<typename Spec>
class EntityLookup
{
public:
	explicit EntityLookup(const World &world) noexcept : m_world(&world) {}

	template<typename T>
	const T *Get(Entity entity) const
	{
		static_assert(Spec::template Allows<T>, "Component is not declared in this system's ecs::Lookup");
		return m_world->Get<T>(entity);
	}

	bool IsAlive(Entity entity) const noexcept { return m_world->IsAlive(entity); }

private:
	const World *m_world;
};

struct SystemAccess
{
	const std::vector<ComponentId> &ReadComponents() const noexcept { return readComponents; }
	const std::vector<ComponentId> &WriteComponents() const noexcept { return writeComponents; }
	const std::vector<std::uint64_t> &ReadMask() const noexcept { return readMask; }
	const std::vector<std::uint64_t> &WriteMask() const noexcept { return writeMask; }
	const std::vector<ResourceAccessDescriptor> &Resources() const noexcept { return resources; }

	bool ConflictsWith(const SystemAccess &other) const noexcept
	{
		for (const ResourceAccessDescriptor &mine : resources)
			for (const ResourceAccessDescriptor &theirs : other.resources)
				if (mine.key == theirs.key && (mine.mode == AccessMode::Write || theirs.mode == AccessMode::Write))
					return true;
		const std::size_t words = (std::max)({readMask.size(), writeMask.size(), other.readMask.size(), other.writeMask.size()});
		for (std::size_t index = 0; index < words; ++index)
		{
			const std::uint64_t writes = index < writeMask.size() ? writeMask[index] : 0;
			const std::uint64_t reads = index < readMask.size() ? readMask[index] : 0;
			const std::uint64_t otherReads = index < other.readMask.size() ? other.readMask[index] : 0;
			const std::uint64_t otherWrites = index < other.writeMask.size() ? other.writeMask[index] : 0;
			const std::uint64_t otherWritesAndReads = otherWrites | otherReads;
			if ((writes & otherWritesAndReads) != 0 || (otherWrites & reads) != 0)
				return true;
		}
		return false;
	}

private:
	void Initialize(std::size_t componentCount)
	{
		resources.clear();
		readComponents.clear();
		writeComponents.clear();
		const std::size_t words = (componentCount + 63) / 64;
		readMask.assign(words, 0);
		writeMask.assign(words, 0);
	}

	void Add(const AccessDescriptor access)
	{
		if (access.component == InvalidComponentId || access.excluded)
			return;

		const std::size_t word = static_cast<std::size_t>(access.component) / 64;
		const std::uint64_t bit = std::uint64_t{1} << (access.component % 64);
		if (access.mode == AccessMode::Write)
		{
			writeComponents.push_back(access.component);
			writeMask[word] |= bit;
		}
		else if (access.mode == AccessMode::Read)
		{
			readComponents.push_back(access.component);
			readMask[word] |= bit;
		}
	}

	void SortComponents()
	{
		std::sort(readComponents.begin(), readComponents.end());
		std::sort(writeComponents.begin(), writeComponents.end());
		readComponents.erase(std::unique(readComponents.begin(), readComponents.end()), readComponents.end());
		writeComponents.erase(std::unique(writeComponents.begin(), writeComponents.end()), writeComponents.end());
		// Write access includes read access. Keep diagnostics and masks canonical
		// when a hook's auxiliary declaration overlaps its chunk query.
		std::erase_if(readComponents, [&](ComponentId id) { return std::binary_search(writeComponents.begin(), writeComponents.end(), id); });
		for (ComponentId id : writeComponents)
			readMask[id / 64] &= ~(std::uint64_t{1} << (id % 64));
		// One entry per resource; a write subsumes a read.
		std::sort(resources.begin(), resources.end(), [](const auto &left, const auto &right) {
			if (left.key != right.key)
				return left.key < right.key;
			return left.mode == AccessMode::Write && right.mode != AccessMode::Write;
		});
		resources.erase(std::unique(resources.begin(), resources.end(),
			[](const auto &left, const auto &right) { return left.key == right.key; }), resources.end());
	}

	std::vector<ComponentId> readComponents;
	std::vector<ComponentId> writeComponents;
	std::vector<std::uint64_t> readMask;
	std::vector<std::uint64_t> writeMask;
	std::vector<ResourceAccessDescriptor> resources;

	friend class SystemRegistry;
};

struct SystemInfo
{
	using ResolveAccessFunction = void (*)(SystemAccess &, const ComponentRegistry &);
	using CreateQueryFunction = void *(*)(World &);
	using DestroyQueryFunction = void (*)(void *) noexcept;
	using ExecuteFunction = void (*)(void *, void *, SystemContext &);
	using PrepareQueryFunction = std::size_t (*)(void *);
	using ExecuteChunkFunction = void (*)(void *, void *, std::size_t, SystemContext &);
	using PreparedChunkRowsFunction = std::size_t (*)(void *, std::size_t);

	SystemId id{InvalidSystemId};
	SystemKey stableKey{};
	std::string_view stableName{};
	SystemPhase phase{SystemPhase::Simulation};
	const std::type_info *type{&typeid(void)};
	void *instance{nullptr};
	SystemAccess access{};
	std::vector<SystemDependency> before;
	std::vector<SystemDependency> after;
	// The groups it belongs to, innermost first (empty: none).
	std::vector<SystemGroupKey> groups;
	ResolveAccessFunction resolveAccess{nullptr};
	CreateQueryFunction createQuery{nullptr};
	DestroyQueryFunction destroyQuery{nullptr};
	ExecuteFunction execute{nullptr};
	PrepareQueryFunction prepareQuery{nullptr};
	ExecuteChunkFunction executeChunk{nullptr};
	// Rows of a prepared chunk (1 for a batch system): what the scheduler balances its jobs by.
	PreparedChunkRowsFunction preparedChunkRows{nullptr};
	// Optional lifecycle of ONE cohesive chunk system, not extra graph nodes.
	// Uses Query plus AuxiliaryAccess metadata and the same deferred wave commit.
	ExecuteFunction beforeChunks{nullptr};
	ExecuteFunction afterChunks{nullptr};
	// Explicit joined reduction/preparation node, not ordinary chunk iteration.
	// Runs once, as one job beside its wave's chunk jobs; shares its wave's final commit.
	// Query declares its full access.
	bool batch{false};
	// A batch node that dispatches pool work of its own (SystemTraits BorrowsJobs): it runs alone on the caller, the
	// wave's other work joined first, so the pool is free for it.
	bool borrowsJobs{false};
	// The system's declared ecs::Lookup type, or null when it declares none.
	const std::type_info *lookupType{nullptr};
	// The system's declared ecs::SideTables type, or null when it declares none.
	const std::type_info *sideTablesType{nullptr};

	const SystemAccess &Access() const noexcept { return access; }
	std::span<const SystemDependency> Before() const noexcept { return before; }
	std::span<const SystemDependency> After() const noexcept { return after; }
};

extern "C++" { class Scheduler; }

class SystemContext
{
public:
	World &GetWorld() const noexcept { return *m_world; }
	CommandBuffer &Commands() const noexcept { return *m_commands; }
	std::uint64_t Tick() const noexcept { return m_time.Tick(); }
	const engine::time::SimulationTime &Time() const noexcept { return m_time; }
	SystemId Id() const noexcept { return m_system; }
	SystemPhase Phase() const noexcept { return m_phase; }
	std::uint32_t JobOrder() const noexcept { return m_jobOrder; }
	std::uint32_t ChunkOrder() const noexcept { return m_chunkOrder; }

	// Checked read-only access to other entities. Spec must be the system's
	// declared `using Lookup = ecs::Lookup<...>`, so the access is scheduled.
	template<typename Spec>
	EntityLookup<Spec> Lookup() const
	{
		if (m_info != nullptr && (m_info->lookupType == nullptr || *m_info->lookupType != typeid(Spec)))
			throw std::logic_error("ECS system '" + std::string(m_info->stableName) +
				"' used an ecs::Lookup it did not declare as `using Lookup`");
		return EntityLookup<Spec>(*m_world);
	}

	// A side table the system declared in `using SideTables = ecs::SideTables<...>`.
	template<typename Spec, typename T>
	SideTable<T> &Side() const
	{
		static_assert(Spec::template AllowsWrite<T>, "Writing a side table needs ecs::Write<T> in the system's SideTables");
		RequireSideTables(typeid(Spec));
		return m_world->template Side<T>();
	}

	// A world resource the system declared with ecs::Write<T>.
	template<ResourceType T>
	T &Write() const
	{
		RequireResource(ResourceKeyOf<T>(), ResourceTraits<T>::StableName, true);
		return m_world->template Resource<T>();
	}

	// A world resource the system declared with ecs::Read<T> (or Write).
	template<ResourceType T>
	const T &Read() const
	{
		RequireResource(ResourceKeyOf<T>(), ResourceTraits<T>::StableName, false);
		return static_cast<const World &>(*m_world).template Resource<T>();
	}

	// A world resource the system declared with ecs::Read<T> (or Write) that the world may not hold (none: null).
	template<ResourceType T>
	const T *Find() const
	{
		RequireResource(ResourceKeyOf<T>(), ResourceTraits<T>::StableName, false);
		return static_cast<const World &>(*m_world).template FindResource<T>();
	}

	template<typename Spec, typename T>
	const SideTable<T> &SideRead() const
	{
		static_assert(Spec::template Allows<T>, "Reading a side table needs it in the system's SideTables");
		RequireSideTables(typeid(Spec));
		return static_cast<const World &>(*m_world).template Side<T>();
	}

private:
	void RequireResource(ResourceKey key, std::string_view name, bool write) const
	{
		if (m_info == nullptr)
			return;
		for (const ResourceAccessDescriptor &access : m_info->access.Resources())
			if (access.key == key && (!write || access.mode == AccessMode::Write))
				return;
		throw std::logic_error("ECS system '" + std::string(m_info->stableName) + "' used resource '" + std::string(name) +
			(write ? "' for writing" : "'") + " without declaring it in `using Resources`");
	}

	void RequireSideTables(const std::type_info &spec) const
	{
		if (m_info != nullptr && (m_info->sideTablesType == nullptr || *m_info->sideTablesType != spec))
			throw std::logic_error("ECS system '" + std::string(m_info->stableName) +
				"' used ecs::SideTables it did not declare as `using SideTables`");
	}

public:

	// The scheduler supplies the command buffer and deterministic logical
	// ordering fields for each chunk job.
	SystemContext(World &world,
		CommandBuffer &commands,
		engine::time::SimulationTime time,
		SystemId system,
		SystemPhase phase,
		std::uint32_t jobOrder = 0,
		std::uint32_t chunkOrder = 0,
		const SystemInfo *info = nullptr) noexcept :
		m_world(&world),
		m_commands(&commands),
		m_time(time),
		m_system(system),
		m_phase(phase),
		m_jobOrder(jobOrder),
		m_chunkOrder(chunkOrder),
		m_info(info)
	{
	}

private:
	World *m_world;
	CommandBuffer *m_commands;
	engine::time::SimulationTime m_time;
	SystemId m_system;
	SystemPhase m_phase;
	std::uint32_t m_jobOrder;
	std::uint32_t m_chunkOrder;
	const SystemInfo *m_info;

	friend class Scheduler;
};

namespace detail
{

template<typename T>
concept HasSystemTraits = requires
{
	typename T::Query;
	{ SystemTraits<T>::StableName } -> std::convertible_to<std::string_view>;
	{ SystemTraits<T>::Phase } -> std::convertible_to<SystemPhase>;
	typename SystemTraits<T>::Before;
	typename SystemTraits<T>::After;
};

template<typename T>
inline constexpr bool IsBatchSystem = [] {
	if constexpr (requires { SystemTraits<T>::Batch; })
		return bool(SystemTraits<T>::Batch);
	return false;
}();

template<typename T>
inline constexpr bool BorrowsJobs = [] {
	if constexpr (requires { SystemTraits<T>::BorrowsJobs; })
		return bool(SystemTraits<T>::BorrowsJobs);
	return false;
}();

template<typename T>
concept SystemDefinition = HasSystemTraits<T> && ((!IsBatchSystem<T> && requires(T &system,
	typename T::Query::Chunk chunk,
	SystemContext &context)
{
	system.Execute(chunk, context);
}) || (IsBatchSystem<T> && (requires(T &system, SystemContext &context) {
	system.Execute(context);
} || requires(T &system, typename T::Query &query, SystemContext &context) {
	system.Execute(query, context);
})));

template<typename T>
SystemDependency MakeSystemDependency()
{
	static_assert(HasSystemTraits<T>,
		"System dependency types must provide ecs::SystemTraits and a Query");
	static_assert(SystemTraits<T>::StableName.size() != 0,
		"System dependency types must have a non-empty stable name");
	return SystemDependency{
		&typeid(T),
		HashSystemKey(SystemTraits<T>::StableName),
		SystemTraits<T>::StableName};
}

template<typename... Systems>
void AppendDependencies(std::vector<SystemDependency> &dependencies, SystemTypeList<Systems...>)
{
	(dependencies.push_back(MakeSystemDependency<Systems>()), ...);
}

template<typename G>
concept SystemGroupDefinition = requires { { SystemGroupTraits<G>::StableName } -> std::convertible_to<std::string_view>; };

template<typename G>
SystemGroupKey GroupKeyOf()
{
	static_assert(SystemGroupDefinition<G>, "System groups must specialize ecs::SystemGroupTraits with a StableName");
	return HashSystemKey(SystemGroupTraits<G>::StableName);
}

template<typename... Groups>
void AppendGroupKeys(std::vector<SystemGroupKey> &keys, SystemTypeList<Groups...>)
{
	(keys.push_back(GroupKeyOf<Groups>()), ...);
}

} // namespace detail

class SystemRegistry
{
public:
	template<typename T>
	SystemId Register(T &system, SystemPhase phase = SystemTraits<T>::Phase);

	// Game composition may order reusable systems without making engine types
	// depend on game types. Like registration, this is startup-only metadata.
	template<typename Before, typename After>
	void OrderBefore();

	// Registers a group object: anything with `void Register(SystemRegistry &)`
	// that registers its member systems and nested groups.
	template<typename Group>
	void RegisterGroup(Group &group)
	{
		group.Register(*this);
	}

	// A group's name, if any member of it was registered.
	std::string_view GroupName(SystemGroupKey key) const
	{
		const auto found = m_groupNames.find(key);
		return found == m_groupNames.end() ? std::string_view{} : found->second;
	}
	const std::vector<std::pair<SystemGroupKey, SystemGroupKey>> &GroupOrdering() const noexcept { return m_groupOrdering; }

	template<typename T>
	SystemId TryGet() const noexcept;

	SystemId TryGet(const std::type_info &type) const noexcept;
	const SystemInfo *TryGet(SystemId id) const noexcept;
	const SystemInfo &Get(SystemId id) const;

	void Finalize(const ComponentRegistry &components);

	bool IsFrozen() const noexcept { return m_frozen; }
	std::size_t Count() const noexcept { return m_infos.size(); }
	ComponentSchemaHash ComponentSchema() const noexcept { return m_componentSchemaHash; }
	const std::vector<const SystemInfo *> &OrderedSystems() const noexcept { return m_idToInfo; }

private:
	template<typename T>
	static void ResolveAccess(SystemAccess &access, const ComponentRegistry &components);

	// Records `G` and its ancestors (innermost first) and their ordering.
	template<typename G>
	void NoteGroup(std::vector<SystemGroupKey> &chain)
	{
		const SystemGroupKey key = detail::GroupKeyOf<G>();
		chain.push_back(key);
		if (m_groupNames.emplace(key, SystemGroupTraits<G>::StableName).second)
		{
			std::vector<SystemGroupKey> before, after;
			if constexpr (requires { typename SystemGroupTraits<G>::Before; })
				detail::AppendGroupKeys(before, typename SystemGroupTraits<G>::Before{});
			if constexpr (requires { typename SystemGroupTraits<G>::After; })
				detail::AppendGroupKeys(after, typename SystemGroupTraits<G>::After{});
			for (const SystemGroupKey other : before)
				m_groupOrdering.emplace_back(key, other);
			for (const SystemGroupKey other : after)
				m_groupOrdering.emplace_back(other, key);
		}
		if constexpr (requires { typename SystemGroupTraits<G>::Parent; })
			NoteGroup<typename SystemGroupTraits<G>::Parent>(chain);
	}

	template<typename T>
	static void *CreateQuery(World &world);

	template<typename T>
	static void DestroyQuery(void *query) noexcept;

	template<typename T>
	static void ExecuteSystem(void *instance, void *query, SystemContext &context);

	// A batch system runs once, on the caller; it may take its query to walk
	// the chunks itself (systems driving single-threaded services).
	template<typename T>
	static void ExecuteBatch(T &system, void *query, SystemContext &context)
	{
		if constexpr (requires(typename T::Query &typed) { system.Execute(typed, context); })
			system.Execute(*static_cast<typename T::Query *>(query), context);
		else
			system.Execute(context);
	}

	template<typename T>
	static std::size_t PrepareQuery(void *query);

	template<typename T>
	static std::size_t PreparedChunkRows(void *query, std::size_t chunkIndex) noexcept;

	template<typename T>
	static void ExecuteSystemChunk(void *instance, void *query, std::size_t chunkIndex, SystemContext &context);

	std::deque<SystemInfo> m_infos;
	std::unordered_map<std::type_index, SystemInfo *> m_typeToInfo;
	std::unordered_map<SystemKey, SystemInfo *> m_keyToInfo;
	std::vector<const SystemInfo *> m_idToInfo;
	std::vector<std::pair<SystemDependency, SystemDependency>> m_ordering;
	std::unordered_map<SystemGroupKey, std::string_view> m_groupNames;
	std::vector<std::pair<SystemGroupKey, SystemGroupKey>> m_groupOrdering;
	bool m_frozen{false};
	ComponentSchemaHash m_componentSchemaHash{UnfinalizedSchemaHash};

	friend class Scheduler;
};

template<typename T>
SystemId SystemRegistry::Register(T &system, const SystemPhase phase)
{
	static_assert(std::is_object_v<T>, "ECS systems must be object types");
	static_assert(detail::SystemDefinition<T>,
		"ECS systems must provide Query, Execute, and ecs::SystemTraits");
	// Systems are code, never storage: per-entity data lives in components,
	// shared data in world resources reached through the SystemContext.
	static_assert(std::is_empty_v<T>, "ECS systems hold no data: move it into components or world resources");
	if (m_frozen)
		throw std::logic_error("Cannot register an ECS system after system finalization");

	const std::type_index type = std::type_index(typeid(T));
	if (const auto existing = m_typeToInfo.find(type); existing != m_typeToInfo.end())
	{
		if (existing->second->instance != &system || existing->second->phase != phase)
			throw std::logic_error("ECS system registered with a different instance or phase");
		return existing->second->id;
	}

	const std::string_view stableName = SystemTraits<T>::StableName;
	if (stableName.empty())
		throw std::invalid_argument("ECS system stable name cannot be empty");

	const SystemKey stableKey = HashSystemKey(stableName);
	if (m_keyToInfo.find(stableKey) != m_keyToInfo.end())
	{
		const SystemInfo &existing = *m_keyToInfo.at(stableKey);
		if (existing.stableName == stableName)
			throw std::logic_error("Duplicate ECS system stable key: " + std::string(stableName));
		throw std::logic_error("ECS system stable-key hash collision");
	}

	if (SystemPhaseIndex(phase) >= SystemPhaseCount)
		throw std::invalid_argument("ECS system phase is invalid");

	SystemInfo info;
	info.stableKey = stableKey;
	info.stableName = stableName;
	info.phase = phase;
	info.type = &typeid(T);
	info.instance = &system;
	info.resolveAccess = &ResolveAccess<T>;
	info.createQuery = &CreateQuery<T>;
	info.destroyQuery = &DestroyQuery<T>;
	info.execute = &ExecuteSystem<T>;
	info.prepareQuery = &PrepareQuery<T>;
	info.executeChunk = &ExecuteSystemChunk<T>;
	info.preparedChunkRows = &PreparedChunkRows<T>;
	info.batch = detail::IsBatchSystem<T>;
	info.borrowsJobs = detail::IsBatchSystem<T> && detail::BorrowsJobs<T>;
	if constexpr (requires(T &value, typename T::Query &query, SystemContext &context) { value.BeforeChunks(query, context); })
	{
		static_assert(!detail::IsBatchSystem<T>, "Batch systems already execute once; chunk lifecycle hooks are unnecessary");
		info.beforeChunks = [](void *instance, void *query, SystemContext &context) {
			static_cast<T *>(instance)->BeforeChunks(*static_cast<typename T::Query *>(query), context);
		};
	}
	if constexpr (requires(T &value, typename T::Query &query, SystemContext &context) { value.AfterChunks(query, context); })
	{
		static_assert(!detail::IsBatchSystem<T>, "Batch systems already execute once; chunk lifecycle hooks are unnecessary");
		info.afterChunks = [](void *instance, void *query, SystemContext &context) {
			static_cast<T *>(instance)->AfterChunks(*static_cast<typename T::Query *>(query), context);
		};
	}
	if constexpr (requires { typename T::Lookup; })
		info.lookupType = &typeid(typename T::Lookup);
	if constexpr (requires { typename T::SideTables; })
		info.sideTablesType = &typeid(typename T::SideTables);
	detail::AppendDependencies(info.before, typename SystemTraits<T>::Before{});
	detail::AppendDependencies(info.after, typename SystemTraits<T>::After{});
	if constexpr (requires { typename SystemTraits<T>::Group; })
		NoteGroup<typename SystemTraits<T>::Group>(info.groups);

	m_infos.push_back(std::move(info));
	try
	{
		m_typeToInfo.emplace(type, &m_infos.back());
		m_keyToInfo.emplace(stableKey, &m_infos.back());
	}
	catch (...)
	{
		m_keyToInfo.erase(stableKey);
		m_typeToInfo.erase(type);
		m_infos.pop_back();
		throw;
	}
	return InvalidSystemId;
}

template<typename Before, typename After>
void SystemRegistry::OrderBefore()
{
	if (m_frozen)
		throw std::logic_error("Cannot add ECS system ordering after finalization");
	m_ordering.emplace_back(detail::MakeSystemDependency<Before>(), detail::MakeSystemDependency<After>());
}

template<typename T>
SystemId SystemRegistry::TryGet() const noexcept
{
	return TryGet(typeid(T));
}

template<typename T>
void SystemRegistry::ResolveAccess(SystemAccess &access, const ComponentRegistry &components)
{
	access.Initialize(components.Count());
	for (const AccessDescriptor descriptor : T::Query::ResolveAccesses(components))
		access.Add(descriptor);
	// Additional typed component access for joined reductions/auxiliary queries.
	// Metadata only: this never changes chunk matching or builds another query.
	if constexpr (requires { typename T::AuxiliaryAccess; })
		for (const AccessDescriptor descriptor : T::AuxiliaryAccess::ResolveAccesses(components))
			access.Add(descriptor);
	if constexpr (requires { typename T::SideTables; })
		for (const AccessDescriptor descriptor : T::SideTables::ResolveAccesses(components))
		{
			const ComponentInfo *info = components.TryGet(descriptor.component);
			if (info == nullptr || info->storage != ComponentStorage::SideTable)
				throw std::logic_error("ECS system '" + std::string(SystemTraits<T>::StableName) +
					"' declares a SideTables entry that is not a registered side-table component");
			access.Add(descriptor);
		}
	if constexpr (requires { typename T::Lookup; })
	{
		std::size_t entry = 0;
		for (const AccessDescriptor descriptor : T::Lookup::ResolveAccesses(components))
		{
			if (descriptor.component == InvalidComponentId)
				throw std::logic_error("ECS system '" + std::string(SystemTraits<T>::StableName) +
					"' declares a Lookup of an unregistered component (entry " + std::to_string(entry) + ")");
			access.Add(descriptor);
			++entry;
		}
	}
	if constexpr (requires { typename T::Resources; })
		T::Resources::Append(access.resources);
	access.SortComponents();
}

template<typename T>
void *SystemRegistry::CreateQuery(World &world)
{
	// Batch systems get their query only when they iterate it.
	if constexpr (detail::IsBatchSystem<T> && !requires(T &system, typename T::Query &query, SystemContext &context) { system.Execute(query, context); })
		return nullptr;
	using QueryType = typename T::Query;
	void *memory = ::operator new(sizeof(QueryType), std::align_val_t(alignof(QueryType)));
	try
	{
		std::construct_at(static_cast<QueryType *>(memory), world);
	}
	catch (...)
	{
		::operator delete(memory, std::align_val_t(alignof(QueryType)));
		throw;
	}
	return memory;
}

template<typename T>
void SystemRegistry::DestroyQuery(void *query) noexcept
{
	using QueryType = typename T::Query;
	std::destroy_at(static_cast<QueryType *>(query));
	::operator delete(query, std::align_val_t(alignof(QueryType)));
}

template<typename T>
void SystemRegistry::ExecuteSystem(void *instance, void *query, SystemContext &context)
{
	T &system = *static_cast<T *>(instance);
	if constexpr (detail::IsBatchSystem<T>)
		ExecuteBatch(system, query, context);
	else
	{
	using QueryType = typename T::Query;
	QueryType &typedQuery = *static_cast<QueryType *>(query);
	if constexpr (requires { system.BeforeChunks(typedQuery, context); }) system.BeforeChunks(typedQuery, context);
	typedQuery.ForEachChunk([&](typename QueryType::Chunk chunk) {
		system.Execute(chunk, context);
	});
	if constexpr (requires { system.AfterChunks(typedQuery, context); }) system.AfterChunks(typedQuery, context);
	}
}

template<typename T>
std::size_t SystemRegistry::PrepareQuery(void *query)
{
	if constexpr (detail::IsBatchSystem<T>) return 1;
	else
	{
	using QueryType = typename T::Query;
	if constexpr (requires { SystemTraits<T>::PieceRows; })
		return static_cast<QueryType *>(query)->PrepareChunks(SystemTraits<T>::PieceRows);
	else
		return static_cast<QueryType *>(query)->PrepareChunks();
	}
}

template<typename T>
std::size_t SystemRegistry::PreparedChunkRows(void *query, const std::size_t chunkIndex) noexcept
{
	if constexpr (detail::IsBatchSystem<T>) return 1;
	else return static_cast<typename T::Query *>(query)->PreparedChunkRows(chunkIndex);
}

template<typename T>
void SystemRegistry::ExecuteSystemChunk(void *instance,
	void *query,
	const std::size_t chunkIndex,
	SystemContext &context)
{
	T &system = *static_cast<T *>(instance);
	if constexpr (detail::IsBatchSystem<T>)
		ExecuteBatch(system, query, context);
	else
	{
	using QueryType = typename T::Query;
	QueryType &typedQuery = *static_cast<QueryType *>(query);
	typedQuery.ExecutePreparedChunk(chunkIndex, [&](typename QueryType::Chunk chunk) {
		system.Execute(chunk, context);
	});
	}
}

} // namespace ecs

namespace ecs
{

SystemId SystemRegistry::TryGet(const std::type_info &type) const noexcept
{
	const auto existing = m_typeToInfo.find(std::type_index(type));
	return existing == m_typeToInfo.end() ? InvalidSystemId : existing->second->id;
}

const SystemInfo *SystemRegistry::TryGet(const SystemId id) const noexcept
{
	return static_cast<std::size_t>(id) < m_idToInfo.size() ? m_idToInfo[id] : nullptr;
}

const SystemInfo &SystemRegistry::Get(const SystemId id) const
{
	const SystemInfo *info = TryGet(id);
	if (info == nullptr)
		throw std::out_of_range("Invalid ECS system ID");
	return *info;
}

void SystemRegistry::Finalize(const ComponentRegistry &components)
{
	if (m_frozen)
		return;
	if (!components.IsFrozen())
		throw std::logic_error("ECS component registry must be finalized before system finalization");

	std::vector<SystemInfo *> ordered;
	ordered.reserve(m_infos.size());
	for (SystemInfo &info : m_infos)
		ordered.push_back(&info);

	std::sort(ordered.begin(), ordered.end(), [](const SystemInfo *left, const SystemInfo *right) {
		if (left->stableKey != right->stableKey)
			return left->stableKey < right->stableKey;
		return left->stableName < right->stableName;
	});

	for (std::size_t index = 1; index < ordered.size(); ++index)
	{
		if (ordered[index - 1]->stableKey == ordered[index]->stableKey)
			throw std::logic_error("Duplicate or colliding ECS system stable key");
	}
	if (ordered.size() >= static_cast<std::size_t>(InvalidSystemId))
		throw std::length_error("Too many ECS systems for dense SystemId");

	std::vector<SystemAccess> access(ordered.size());
	for (std::size_t index = 0; index < ordered.size(); ++index)
		ordered[index]->resolveAccess(access[index], components);

	for (std::size_t index = 0; index < ordered.size(); ++index)
	{
		ordered[index]->id = static_cast<SystemId>(index);
		ordered[index]->access = std::move(access[index]);
	}
	m_idToInfo = std::vector<const SystemInfo *>(ordered.begin(), ordered.end());
	m_componentSchemaHash = components.SchemaHash();
	m_frozen = true;
}

} // namespace ecs
