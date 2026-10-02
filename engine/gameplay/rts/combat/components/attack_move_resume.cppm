export module engine.gameplay.rts.combat.components.attack_move_resume;
import std;

export import engine.ecs.core.component_registry;
export import engine.gameplay.rts.movement.components.move_order;

// An attack-mover that follows a path (AIAttackFollowWaypointPathState) rather than heading for one spot: the move it goes
// back to once a fight is over (computeGoal and computePath from its current waypoint), kept up to date while it is not
// fighting; and whether it follows as a team (the team's path follower ends it; else it ends with its path).
export namespace engine::gameplay
{
struct AttackMoveResume
{
	MoveOrder order;
	std::uint8_t team{0};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::AttackMoveResume>
{
	static constexpr std::string_view StableName = "engine.gameplay.attack_move_resume";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
