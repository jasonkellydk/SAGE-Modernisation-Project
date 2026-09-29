export module engine.net.lockstep.command_log;
import std;

// Every released tick bundle, as sent. Whoever restores a checkpoint of tick
// N (a repaired peer, a rejoining player, a late observer) catches up with
// the bundles after N; the whole log is also the match's replay.
export namespace engine::net
{
class CommandLog
{
public:
	void Append(std::uint64_t tick, std::vector<std::byte> bundle) { m_bundles.emplace(tick, std::move(bundle)); }

	// The encoded bundles after `tick`, in tick order.
	template<typename Visit>
	void ForEachAfter(std::uint64_t tick, Visit &&visit) const
	{
		for (auto it = m_bundles.upper_bound(tick); it != m_bundles.end(); ++it)
			visit(it->first, it->second);
	}

	std::uint64_t LastTick() const noexcept { return m_bundles.empty() ? 0 : m_bundles.rbegin()->first; }
	std::size_t Size() const noexcept { return m_bundles.size(); }

private:
	std::map<std::uint64_t, std::vector<std::byte>> m_bundles;
};
}
