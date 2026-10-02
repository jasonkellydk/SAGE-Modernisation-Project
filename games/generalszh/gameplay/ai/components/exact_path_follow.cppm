export module games.generalszh.gameplay.ai.components.exact_path_follow;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// A unit following a waypoint path exactly as a team (AIFollowWaypointPathExactState with m_moveAsGroup): its offset from
// the group's centre as it set out (setPathFromWaypoint's groupOffset: every waypoint of its path moved by it) and
// whether it goes at the group's speed (setDesiredSpeed(AIGroup::getSpeed): its DesiredSpeed is the follow's). A side
// table on the unit, for as long as it follows the path.
export namespace generalszh::gameplay
{
struct ExactPathFollow
{
	Engine::Math::FixedVector2 offset;
	std::uint8_t groupSpeed{0};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::ExactPathFollow>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.exact_path_follow";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const generalszh::gameplay::ExactPathFollow &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.offset.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.offset.y.Raw()));
		hasher.AppendU64(value.groupSpeed);
	}
};
}
