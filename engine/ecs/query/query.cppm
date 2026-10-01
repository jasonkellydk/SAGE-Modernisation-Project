export module engine.ecs.query.query;
import std;

export import engine.ecs.query.access;
export import engine.ecs.query.query_cache;

export namespace ecs
{

namespace detail
{

template<typename Wanted, typename... Terms>
struct ComponentIndex;

template<bool Match, typename Wanted, typename First, typename... Rest>
struct ComponentIndexStep;

template<typename Wanted, typename First, typename... Rest>
struct ComponentIndexStep<true, Wanted, First, Rest...>
{
	static constexpr std::size_t value = 0;
};

template<typename Wanted, typename First, typename... Rest>
struct ComponentIndexStep<false, Wanted, First, Rest...>
{
	static constexpr std::size_t value = 1 + ComponentIndex<Wanted, Rest...>::value;
};

template<typename Wanted, typename First, typename... Rest>
struct ComponentIndex<Wanted, First, Rest...> : ComponentIndexStep<
	std::is_same_v<std::remove_cv_t<Wanted>, typename First::ComponentType>,
	Wanted,
	First,
	Rest...>
{
};

template<typename Wanted>
struct ComponentIndex<Wanted>
{
	static_assert(!std::is_same_v<Wanted, Wanted>, "Requested component is not part of the ECS query");
};

template<typename... Terms>
struct UniqueComponents;

template<>
struct UniqueComponents<> : std::true_type
{
};

template<typename First, typename... Rest>
struct UniqueComponents<First, Rest...> : std::bool_constant<
	((!std::is_same_v<typename First::ComponentType, typename Rest::ComponentType>) && ...) &&
	UniqueComponents<Rest...>::value>
{
};

template<typename Term>
struct QueryView;

template<typename T>
struct QueryView<Read<T>>
{
	using Type = std::span<const typename Read<T>::ComponentType>;
};

template<typename T>
struct QueryView<Write<T>>
{
	using Type = std::span<typename Write<T>::ComponentType>;
};

template<typename T>
struct QueryView<Optional<T>>
{
	using Type = std::span<const typename Optional<T>::ComponentType>;
};

template<typename T>
struct QueryView<OptionalWrite<T>> : QueryView<Write<T>>
{
};

template<typename T>
struct QueryView<Exclude<T>>
{
	using Type = std::span<const typename Exclude<T>::ComponentType>;
};

} // namespace detail

template<typename... Terms>
class QueryChunk
{
public:
	using ViewTuple = std::tuple<typename detail::QueryView<Terms>::Type...>;

	QueryChunk(Chunk &chunk, const std::array<std::size_t, sizeof...(Terms)> &columns) :
		QueryChunk(chunk, columns, 0, chunk.Size())
	{
	}
	// A run of the chunk's rows, [first, first + count): what a prepared piece of a large chunk sees.
	QueryChunk(Chunk &chunk, const std::array<std::size_t, sizeof...(Terms)> &columns, std::size_t first, std::size_t count) :
		m_entities(chunk.Entities() + first, count),
		m_views(MakeViews(chunk, columns, first, count, std::index_sequence_for<Terms...>{}))
	{
	}

	std::size_t Count() const noexcept { return m_entities.size(); }
	std::span<const Entity> Entities() const noexcept { return m_entities; }

	template<typename T>
	auto Get() const
	{
		constexpr std::size_t index = detail::ComponentIndex<T, Terms...>::value;
		return std::get<index>(m_views);
	}

private:
	template<typename Term>
	static typename detail::QueryView<Term>::Type MakeView(Chunk &chunk, std::size_t column, std::size_t first, std::size_t count)
	{
		using Component = typename Term::ComponentType;
		if constexpr (Term::IsExcluded)
		{
			return typename detail::QueryView<Term>::Type{};
		}
		else if (column == std::numeric_limits<std::size_t>::max())
		{
			return typename detail::QueryView<Term>::Type{};
		}
		else if constexpr (Term::Mode == AccessMode::Write)
		{
			return std::span<Component>(static_cast<Component *>(chunk.ComponentData(column)) + first, count);
		}
		else
		{
			return std::span<const Component>(static_cast<const Component *>(chunk.ComponentData(column)) + first, count);
		}
	}

	template<std::size_t... Indices>
	static ViewTuple MakeViews(Chunk &chunk,
		const std::array<std::size_t, sizeof...(Terms)> &columns,
		std::size_t first,
		std::size_t count,
		std::index_sequence<Indices...>)
	{
		return ViewTuple{MakeView<Terms>(chunk, columns[Indices], first, count)...};
	}

	std::span<const Entity> m_entities;
	ViewTuple m_views;
};

template<typename... Terms>
class Query
{
public:
	using Chunk = QueryChunk<Terms...>;

	static_assert(sizeof...(Terms) > 0, "An ECS query must request at least one term");
	static_assert(detail::UniqueComponents<Terms...>::value,
		"An ECS query cannot contain the same component more than once");

	explicit Query(World &world) :
		m_world(&world),
		m_components{}
	{
		ResolveComponents();
	}

	Query(const Query &) = delete;
	Query &operator=(const Query &) = delete;

	// Prepares a deterministic snapshot of the currently matching non-empty
	// chunks. The snapshot is rebuilt on the calling thread before a scheduler
	// wave dispatches jobs; ExecutePreparedChunk() is read-only with respect to
	// the Query and is therefore safe for concurrent chunk jobs.
	// `pieceRows` (0: whole chunks): a chunk with more rows is prepared as pieces of at most that many, in row order,
	// each its own logical chunk (outputs, commands), so the scheduler can share a heavy system's rows among jobs. Only
	// for systems that ask (SystemTraits PieceRows): their rows must not depend on sharing a chunk.
	std::size_t PrepareChunks(std::size_t pieceRows = 0)
	{
		m_cache.Refresh(*m_world);
		if (pieceRows != m_preparedPieceRows)
		{
			m_preparedPieceRows = pieceRows;
			m_preparedValid = false;
		}

		// The snapshot is still exact while no chunk layout event since names an archetype this query matches (or
		// has not tested yet).
		const std::uint64_t events = m_world->ArchetypeLayoutEvents();
		bool rebuild = !m_preparedValid || events - m_preparedEvents > ArchetypeRegistry::LayoutLogSize;
		for (std::uint64_t event = m_preparedEvents; !rebuild && event < events; ++event)
			rebuild = m_cache.SlotOf(m_world->ArchetypeLayoutEvent(event)) != QueryCache::NoSlot;
		// In pieces, a chunk that grew or shrank needs its pieces cut again.
		if (pieceRows != 0)
			for (std::size_t index = 0; !rebuild && index < m_preparedChunks.size(); ++index)
				rebuild = m_preparedChunks[index].chunk->Size() != m_preparedChunks[index].chunkSize;
		m_preparedEvents = events;
		if (!rebuild)
			return m_preparedChunks.size();
		m_preparedValid = true;

		// The archetypes in use, in signature order (the order of every archetype walk), those this query matches.
		m_preparedChunks.clear();
		for (Archetype *archetype : m_world->ArchetypesInUse())
		{
			bool added = false;
			const std::uint32_t slot = m_cache.Resolve(*archetype, [this](const Archetype &candidate) { return Matches(candidate); }, added);
			if (added)
				m_columnIndices.push_back(MakeColumnIndices(*archetype, std::index_sequence_for<Terms...>{}));
			if (slot == QueryCache::NoSlot)
				continue;
			const std::array<std::size_t, sizeof...(Terms)> &columns = m_columnIndices[slot];
			for (const std::unique_ptr<ecs::Chunk> &chunk : archetype->Chunks())
			{
				if (chunk->Size() == 0)
					continue;
				if (pieceRows == 0)
				{
					m_preparedChunks.push_back(PreparedChunk{chunk.get(), columns, 0, Whole, 0});
					continue;
				}
				// Pieces: their bounds follow the chunk's size when prepared, so a size change prepares again.
				for (std::size_t first = 0; first < chunk->Size(); first += pieceRows)
					m_preparedChunks.push_back(PreparedChunk{chunk.get(), columns, static_cast<std::uint32_t>(first),
						static_cast<std::uint32_t>(std::min<std::size_t>(pieceRows, chunk->Size() - first)), static_cast<std::uint32_t>(chunk->Size())});
			}
		}
		return m_preparedChunks.size();
	}

	std::size_t PreparedChunkCount() const noexcept { return m_preparedChunks.size(); }
	// Rows of one prepared chunk (the scheduler balances its jobs by rows).
	std::size_t PreparedChunkRows(const std::size_t chunkIndex) const noexcept
	{
		const PreparedChunk &prepared = m_preparedChunks[chunkIndex];
		return prepared.count == Whole ? prepared.chunk->Size() : prepared.count;
	}

	template<typename Function>
	void ForEachPreparedChunk(Function &&function) const
	{
		for (const PreparedChunk &prepared : m_preparedChunks)
			std::forward<Function>(function)(View(prepared));
	}

	template<typename Function>
	void ExecutePreparedChunk(const std::size_t chunkIndex, Function &&function) const
	{
		if (chunkIndex >= m_preparedChunks.size())
			throw std::out_of_range("Invalid prepared ECS query chunk index");
		const PreparedChunk &prepared = m_preparedChunks[chunkIndex];
		std::forward<Function>(function)(View(prepared));
	}

	template<typename Function>
	void ForEachChunk(Function &&function)
	{
		PrepareChunks();
		ForEachPreparedChunk(std::forward<Function>(function));
	}

	std::array<AccessDescriptor, sizeof...(Terms)> Accesses() const noexcept
	{
		return MakeAccesses(std::index_sequence_for<Terms...>{});
	}

	static std::array<AccessDescriptor, sizeof...(Terms)> ResolveAccesses(const ComponentRegistry &components)
	{
		return ResolveAccessesImpl(components, std::index_sequence_for<Terms...>{});
	}

	std::size_t CachedArchetypeCount() const noexcept { return m_cache.Matches().size(); }
	std::uint64_t CachedRevision() const noexcept { return m_cache.Revision(); }

private:
	static constexpr std::uint32_t Whole = 0xFFFFFFFFu; // a whole chunk, at its size when run
	struct PreparedChunk
	{
		ecs::Chunk *chunk{nullptr};
		std::array<std::size_t, sizeof...(Terms)> columns{};
		std::uint32_t first{0}, count{0}; // its rows (Whole: all of them)
		std::uint32_t chunkSize{0};        // a piece's chunk size when cut
	};
	static Chunk View(const PreparedChunk &prepared)
	{
		return prepared.count == Whole ? Chunk(*prepared.chunk, prepared.columns) : Chunk(*prepared.chunk, prepared.columns, prepared.first, prepared.count);
	}

	void ResolveComponents()
	{
		if (!m_world->ComponentsFinalized())
			throw std::logic_error("ECS component registry must be finalized before constructing or executing a query");

		m_components = {m_world->Components().TryGet<typename Terms::ComponentType>()...};
		// Optional and excluded terms of components this world never registered
		// are simply never present; required ones must exist.
		constexpr std::array<bool, sizeof...(Terms)> required{(!Terms::IsOptional && !Terms::IsExcluded)...};
		for (std::size_t index = 0; index < m_components.size(); ++index)
		{
			if (m_components[index] == InvalidComponentId && required[index])
				throw std::logic_error("ECS query component was not registered before finalization");
			if (m_components[index] != InvalidComponentId &&
				m_world->Components().Get(m_components[index]).storage == ComponentStorage::SideTable)
				throw std::logic_error("ECS queries match archetype components; reach side-table components through ecs::SideTables");
		}
	}

	template<typename Term>
	bool MatchesTerm(const Archetype &archetype, ComponentId component) const noexcept
	{
		if constexpr (Term::IsExcluded)
			return !archetype.Has(component);
		else if constexpr (Term::IsOptional)
			return true;
		else
			return archetype.Has(component);
	}

	template<std::size_t... Indices>
	bool MatchesImpl(const Archetype &archetype, std::index_sequence<Indices...>) const noexcept
	{
		return (MatchesTerm<Terms>(archetype, m_components[Indices]) && ...);
	}

	bool Matches(const Archetype &archetype) const noexcept
	{
		return MatchesImpl(archetype, std::index_sequence_for<Terms...>{});
	}

	template<typename Term>
	static AccessDescriptor ResolveAccess(const ComponentRegistry &components)
	{
		const ComponentId component = components.TryGet<typename Term::ComponentType>();
		if (component == InvalidComponentId && !Term::IsOptional && !Term::IsExcluded)
			throw std::logic_error("ECS query component was not registered before finalization");
		return AccessDescriptor{component, Term::Mode, Term::IsOptional, Term::IsExcluded};
	}

	template<std::size_t... Indices>
	static std::array<AccessDescriptor, sizeof...(Terms)> ResolveAccessesImpl(
		const ComponentRegistry &components,
		std::index_sequence<Indices...>)
	{
		return {ResolveAccess<Terms>(components)...};
	}

	template<std::size_t... Indices>
	std::array<std::size_t, sizeof...(Terms)> MakeColumnIndices(const Archetype &archetype,
		std::index_sequence<Indices...>) const noexcept
	{
		return {archetype.ColumnIndex(m_components[Indices])...};
	}

	template<std::size_t... Indices>
	std::array<AccessDescriptor, sizeof...(Terms)> MakeAccesses(std::index_sequence<Indices...>) const noexcept
	{
		return {AccessDescriptor{m_components[Indices], Terms::Mode, Terms::IsOptional, Terms::IsExcluded}...};
	}

	World *m_world;
	std::array<ComponentId, sizeof...(Terms)> m_components;
	QueryCache m_cache;
	std::vector<std::array<std::size_t, sizeof...(Terms)>> m_columnIndices;
	std::vector<PreparedChunk> m_preparedChunks;
	std::uint64_t m_preparedEvents{0};
	std::size_t m_preparedPieceRows{0};
	bool m_preparedValid{false};
};

} // namespace ecs
