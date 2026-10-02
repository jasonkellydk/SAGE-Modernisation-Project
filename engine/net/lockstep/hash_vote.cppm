export module engine.net.lockstep.hash_vote;
import std;

// Desync detection by majority. Voters (the players) report their state
// hash for a tick; once every expected voter has, the largest group of equal
// hashes is the truth (ties go to the group with the lowest seat, the
// authority) and every other voter diverged. Agreed hashes are remembered so
// a checkpoint offered for repair can be verified.
export namespace engine::net
{
struct HashVerdict
{
	std::uint64_t tick{0};
	std::uint64_t agreed{0};
	std::vector<std::uint32_t> majority; // ascending seats
	std::vector<std::uint32_t> diverged; // ascending seats
};

class HashVote
{
public:
	// Records `seat`'s hash for `tick`; the verdict once `voters` have all reported.
	std::optional<HashVerdict> Report(std::uint64_t tick, std::uint32_t seat, std::uint64_t hash, const std::set<std::uint32_t> &voters)
	{
		if (!voters.contains(seat) || tick <= m_decidedThrough)
			return std::nullopt;
		auto &reports = m_pending[tick];
		reports[seat] = hash;
		for (const std::uint32_t voter : voters)
			if (!reports.contains(voter))
				return std::nullopt;
		return Decide(tick, voters);
	}

	// Decides `tick` with whoever has reported (voters that left stop counting).
	std::optional<HashVerdict> Close(std::uint64_t tick, const std::set<std::uint32_t> &voters)
	{
		const auto found = m_pending.find(tick);
		if (found == m_pending.end())
			return std::nullopt;
		return Decide(tick, voters);
	}

	std::optional<std::uint64_t> Agreed(std::uint64_t tick) const
	{
		const auto found = m_agreed.find(tick);
		return found != m_agreed.end() ? std::optional(found->second) : std::nullopt;
	}

	// Ticks reported but not decided, oldest first (for timeouts).
	std::vector<std::uint64_t> PendingTicks() const
	{
		std::vector<std::uint64_t> ticks;
		for (const auto &[tick, reports] : m_pending)
			ticks.push_back(tick);
		return ticks;
	}

private:
	HashVerdict Decide(std::uint64_t tick, const std::set<std::uint32_t> &voters)
	{
		std::map<std::uint32_t, std::uint64_t> reports = std::move(m_pending[tick]);
		m_pending.erase(tick);
		// Older undecided ticks can no longer complete in order: drop them.
		m_pending.erase(m_pending.begin(), m_pending.lower_bound(tick));
		m_decidedThrough = tick;

		std::map<std::uint64_t, std::vector<std::uint32_t>> groups;
		for (const auto &[seat, hash] : reports)
			if (voters.contains(seat))
				groups[hash].push_back(seat); // seats ascend (map order)
		HashVerdict verdict{tick};
		for (const auto &[hash, seats] : groups)
			if (verdict.majority.empty() || seats.size() > verdict.majority.size() ||
				(seats.size() == verdict.majority.size() && seats.front() < verdict.majority.front()))
			{
				verdict.agreed = hash;
				verdict.majority = seats;
			}
		for (const auto &[seat, hash] : reports)
			if (voters.contains(seat) && hash != verdict.agreed)
				verdict.diverged.push_back(seat);
		if (!verdict.majority.empty())
		{
			m_agreed[tick] = verdict.agreed;
			// Keep a bounded window of agreed hashes.
			while (m_agreed.size() > 64)
				m_agreed.erase(m_agreed.begin());
		}
		return verdict;
	}

	std::map<std::uint64_t, std::map<std::uint32_t, std::uint64_t>> m_pending;
	std::map<std::uint64_t, std::uint64_t> m_agreed;
	std::uint64_t m_decidedThrough{0};
};
}
