export module engine.gameplay.rts.movement.components.attack_approach;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;

// An attack's request for a route to a spot it may fire from (AIUpdateInterface::requestAttackPath ->
// Pathfinder::findAttackPath), for a ground attacker: its order goes to the victim's position, and the
// route searched toward it stops at the first cell (in the search's order) from which its weapon reaches the victim
// (isGoalPosWithinAttackRange: bounding circles, its range less a quarter cell), half a cell or more from where it stands,
// with its view clear (isAttackViewBlockedByObstacle, its eye at the cell's centre). `active` while its attack approaches
// (AIAttackApproachTargetState), `search` while that approach's route is such a search.
// What the view check reads is kept here as the attack saw it: the victim's place, top, centre and slaver, its own top,
// container and slaver, whether its weapon's terrain check applies, and the cells it sees past first.
export namespace engine::gameplay
{
struct AttackApproach
{
	Engine::Math::FixedVector3 victimAt;
	Engine::Math::Fixed victimTop;
	Engine::Math::Fixed victimCentre;
	Engine::Math::Fixed victimRadius;
	Engine::Math::Fixed ownRadius;
	Engine::Math::Fixed goalRange;
	Engine::Math::Fixed minimumRange; // the weapon's MinimumAttackRange (isGoalPosWithinAttackRange: a quarter cell more)
	Engine::Math::Fixed eyeTop;
	// AIAttackApproachTargetState's own: where it last sent itself for (m_prevVictimPos) and the tick it may work its way
	// out again (m_approachTimestamp + MIN_RECOMPUTE_TIME).
	Engine::Math::FixedVector2 prevVictim;
	std::uint64_t approachTick{0};
	ecs::Entity victim;
	ecs::Entity victimSlaver;
	ecs::Entity container;
	ecs::Entity slaver;
	std::int32_t skipCount{0};
	std::uint8_t active{0};
	std::uint8_t weaponTerrain{1};
	std::uint8_t sight{0}; // it needs a line of sight (isAttackViewBlockedByObstacle answers for it only)
	std::uint8_t search{0}; // its route searches for the spot (on the ground, no contact weapon): findAttackPath
	// The layer its view walks (isAttackViewBlockedByObstacle as the search calls it: the victim's, or its own as it stands
	// when the victim is on the ground).
	std::uint8_t viewLayer{0};
	// A contact weapon's approach: it paths and pushes into its victim (AIAttackApproachTargetState::computePath:
	// ai->ignoreObstacle(victim), until the state's onExit).
	std::uint8_t ignoreVictim{0};
	std::uint8_t reserved[6]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::AttackApproach>
{
	static constexpr std::string_view StableName = "engine.gameplay.attack_approach";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
