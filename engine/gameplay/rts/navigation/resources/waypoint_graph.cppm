export module engine.gameplay.rts.navigation.resources.waypoint_graph;
import std;

export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// Named waypoints with directed links and path labels, stored flat for
// systems to read: positions in one array, links in compressed rows
// (linkBegin[i] .. linkBegin[i + 1]). The game builds it from its level.
export namespace engine::gameplay
{
struct WaypointDescription
{
	std::uint32_t id{0};
	std::string name;
	Engine::Math::FixedVector3 position;
	std::vector<std::uint32_t> links; // waypoint ids
	std::vector<std::string> labels;  // path labels
};

class WaypointGraph
{
public:
	static constexpr std::uint32_t None = 0xFFFFFFFFu;

	WaypointGraph() = default;

	explicit WaypointGraph(std::span<const WaypointDescription> waypoints)
	{
		std::map<std::uint32_t, std::uint32_t> byId;
		for (std::uint32_t index = 0; index < waypoints.size(); ++index)
			byId.emplace(waypoints[index].id, index);
		for (std::uint32_t index = 0; index < waypoints.size(); ++index)
		{
			const auto &waypoint = waypoints[index];
			m_positions.push_back(waypoint.position);
			m_names.push_back(waypoint.name);
			m_byName.emplace(waypoint.name, index);
			m_linkBegin.push_back(static_cast<std::uint32_t>(m_links.size()));
			for (const std::uint32_t id : waypoint.links)
				if (const auto found = byId.find(id); found != byId.end())
					m_links.push_back(found->second);
			for (const auto &label : waypoint.labels)
				if (!label.empty())
					m_labeled[Lower(label)].push_back(index);
		}
		m_linkBegin.push_back(static_cast<std::uint32_t>(m_links.size()));
	}

	std::size_t Size() const noexcept { return m_positions.size(); }

	// Whether the waypoint carries the path label (any of its labels; labels match regardless of case).
	bool HasLabel(std::uint32_t index, std::string_view label) const
	{
		const auto found = m_labeled.find(Lower(label));
		return found != m_labeled.end() && std::ranges::find(found->second, index) != found->second.end();
	}
	Engine::Math::FixedVector3 Position(std::uint32_t index) const { return m_positions.at(index); }
	const std::string &Name(std::uint32_t index) const { return m_names.at(index); }

	std::span<const std::uint32_t> Links(std::uint32_t index) const
	{
		return std::span<const std::uint32_t>(m_links).subspan(m_linkBegin.at(index), m_linkBegin.at(index + 1) - m_linkBegin.at(index));
	}

	// First waypoint of that name in level order.
	std::uint32_t Find(std::string_view name) const
	{
		const auto found = m_byName.lower_bound(name);
		return found == m_byName.end() || found->first != name ? None : found->second;
	}

	// The labeled waypoint closest to `from` in 2D (ties: level order).
	std::uint32_t ClosestOnPath(Engine::Math::FixedVector2 from, std::string_view label) const
	{
		const auto found = m_labeled.find(Lower(label));
		if (found == m_labeled.end())
			return None;
		std::uint32_t best = None;
		Engine::Math::Fixed bestDistance;
		for (const std::uint32_t index : found->second)
		{
			const Engine::Math::Fixed distance = Engine::Math::DistanceSquared(m_positions[index].XY(), from);
			if (best == None || distance < bestDistance)
			{
				best = index;
				bestDistance = distance;
			}
		}
		return best;
	}

private:
	static std::string Lower(std::string_view text)
	{
		std::string out(text);
		for (char &c : out)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return out;
	}

	std::vector<Engine::Math::FixedVector3> m_positions;
	std::vector<std::string> m_names;
	std::vector<std::uint32_t> m_linkBegin;
	std::vector<std::uint32_t> m_links;
	std::multimap<std::string, std::uint32_t, std::less<>> m_byName;
	std::map<std::string, std::vector<std::uint32_t>, std::less<>> m_labeled;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::WaypointGraph>
{
	static constexpr std::string_view StableName = "engine.gameplay.waypoint_graph";
};
}
