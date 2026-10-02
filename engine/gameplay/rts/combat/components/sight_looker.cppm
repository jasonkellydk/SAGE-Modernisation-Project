export module engine.gameplay.rts.combat.components.sight_looker;
import std;

export import engine.ecs.core.component_registry;

// A looker whose attacks need a line of sight (KINDOF_ATTACK_NEEDS_LINE_OF_SIGHT, while AIData's AttackUsesLineOfSight is
// on: the game gives it this), and whether it is IMMOBILE (Pathfinder::isAttackViewBlockedByObstacle then leaves the
// terrain out: it cannot move round it).
export namespace engine::gameplay
{
struct SightLooker
{
	std::uint8_t immobile{0};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::SightLooker>
{
	static constexpr std::string_view StableName = "engine.gameplay.sight_looker";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
