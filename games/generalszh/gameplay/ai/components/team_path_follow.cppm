export module games.generalszh.gameplay.ai.components.team_path_follow;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import engine.core.serialization.byte_stream;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// A unit following a waypoint path as a team (AIUpdateInterface::aiFollowWaypointPathAsTeam: AIFollowWaypointPathState
// with m_moveAsGroup): its offset from the group's centre when the order came (m_groupOffset, kept for every waypoint),
// the goal it was last sent to (the waypoint plus that offset, kept on the map: m_goalPosition), the waypoint it is
// heading for and the one before (m_currentWaypoint, m_priorWaypoint), and whether its player is a skirmish computer
// player (it may count as there once its team's centre is close: SkirmishGroupFudgeDistance). Its speed is the group's
// (DesiredSpeed, set with it). Simulation state: checkpointed.
//
// TeamWaypoints: each team's current waypoint (Team::m_currentWaypoint), by team index: a member arriving moves the team
// on, the others follow. TeamPathEvents: what the tick's followers saw (arrived, the team moved on, another order took
// over), applied in order after the systems (ApplyTeamPathFollows).
export namespace generalszh::gameplay
{
struct TeamPathFollow
{
	Engine::Math::FixedVector2 offset;
	Engine::Math::FixedVector2 goal;
	std::uint32_t waypoint{0xFFFFFFFFu};
	std::uint32_t prior{0xFFFFFFFFu};
	std::uint8_t skirmish{0};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};

class TeamWaypoints
{
public:
	static constexpr std::uint32_t None = 0xFFFFFFFFu;

	std::uint32_t Of(std::uint32_t team) const noexcept { return team < m_waypoints.size() ? m_waypoints[team] : None; }
	void Set(std::uint32_t team, std::uint32_t waypoint)
	{
		if (team >= m_waypoints.size())
			m_waypoints.resize(team + 1, None);
		m_waypoints[team] = waypoint;
	}

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(m_waypoints.size()));
		for (const std::uint32_t waypoint : m_waypoints)
			writer.U32(waypoint);
	}
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto count = reader.U32();
		if (!count)
			return false;
		std::vector<std::uint32_t> waypoints;
		waypoints.reserve(*count);
		for (std::uint32_t index = 0; index < *count; ++index)
		{
			const auto waypoint = reader.U32();
			if (!waypoint)
				return false;
			waypoints.push_back(*waypoint);
		}
		m_waypoints = std::move(waypoints);
		return true;
	}
private:
	std::vector<std::uint32_t> m_waypoints;
};

enum class TeamPathEventKind : std::uint8_t
{
	Arrived,  // its move to its goal ended (or, a skirmish AI's, its team's centre is close enough)
	MovedOn,  // its team's waypoint is no longer its own
	Replaced, // another order took over its movement
};

struct TeamPathEvent
{
	ecs::Entity unit;
	TeamPathEventKind kind{TeamPathEventKind::Arrived};
};

struct TeamPathEvents : ecs::ChunkOutputs<TeamPathEvent>
{
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::TeamPathFollow>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.team_path_follow";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const generalszh::gameplay::TeamPathFollow &value, StateHasher &hasher) noexcept
	{
		for (const auto fixed : {value.offset.x, value.offset.y, value.goal.x, value.goal.y})
			hasher.AppendU64(static_cast<std::uint64_t>(fixed.Raw()));
		hasher.AppendU64((std::uint64_t{value.prior} << 32) | value.waypoint);
		hasher.AppendU64(value.skirmish);
	}
};

template<>
struct ResourceTraits<generalszh::gameplay::TeamWaypoints>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.team_waypoints";
};

template<>
struct ResourceTraits<generalszh::gameplay::TeamPathEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.team_path_events";
};
}
