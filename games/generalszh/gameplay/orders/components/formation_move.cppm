export module games.generalszh.gameplay.orders.components.formation_move;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// A user formation's member moving in it (AIGroup::friend_moveFormationToPos): its move goes at its group's speed
// (AIMoveToState / AIFollowPathState onEnter: setDesiredSpeed(AIGroup::getSpeed) for a unit with a formation id) while it
// heads for `goal` (its last point) or the points before it; any other order (the next move's
// AIInternalMoveToState::onEnter: FAST_AS_POSSIBLE) or getting there ends it. A side table on the unit.
export namespace generalszh::gameplay
{
struct FormationMove
{
	Engine::Math::FixedVector2 goal;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::FormationMove>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.formation_move";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const generalszh::gameplay::FormationMove &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.goal.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.goal.y.Raw()));
	}
};
}
