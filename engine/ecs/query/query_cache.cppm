export module engine.ecs.query.query_cache;
import std;

export import engine.ecs.core.world;

export namespace ecs
{

// Which archetypes a query matches, by archetype creation index, worked out lazily: only when an archetype holding
// entities is first met (a long game makes over a thousand archetypes, most soon empty; a query made for one call
// never tests those). Archetypes are only ever added, so an answer never changes.
class QueryCache
{
public:
	static constexpr std::uint32_t NoSlot = 0xFFFFFFFFu;
	static constexpr std::uint32_t Unknown = 0xFFFFFFFEu;

	// Makes room for the archetypes made since the last refresh (not yet tested); true when there were any.
	bool Refresh(World &world)
	{
		const std::uint64_t revision = world.ArchetypeRevision();
		if (m_initialized && m_revision == revision)
			return false;
		const bool grew = world.ArchetypeCount() > m_slotOf.size();
		m_slotOf.resize(world.ArchetypeCount(), Unknown);
		m_revision = revision;
		m_initialized = true;
		return grew;
	}

	// An archetype's slot among the matches (NoSlot: it does not match; Unknown: not tested yet).
	std::uint32_t SlotOf(std::uint32_t archetypeIndex) const noexcept
	{
		return archetypeIndex < m_slotOf.size() ? m_slotOf[archetypeIndex] : Unknown;
	}

	// Tests an archetype not tested yet; `added` is set when it matches (its slot is the next).
	template<typename Predicate>
	std::uint32_t Resolve(Archetype &archetype, Predicate &&predicate, bool &added)
	{
		std::uint32_t &slot = m_slotOf[archetype.Index()];
		added = false;
		if (slot != Unknown)
			return slot;
		if (predicate(archetype))
		{
			slot = static_cast<std::uint32_t>(m_matches.size());
			m_matches.push_back(&archetype);
			added = true;
		}
		else
			slot = NoSlot;
		return slot;
	}

	// The matching archetypes met so far, in the order they were met.
	const std::vector<Archetype *> &Matches() const noexcept { return m_matches; }
	std::uint64_t Revision() const noexcept { return m_revision; }

private:
	std::vector<Archetype *> m_matches;
	std::vector<std::uint32_t> m_slotOf;
	std::uint64_t m_revision{0};
	bool m_initialized{false};
};

} // namespace ecs
