export module engine.gameplay.rts.combat.components.attack_move;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// A unit moving to a spot while taking on what it comes across (AIAttackMoveToState): where it is going, the retries
// left when its move ends short of there (ATTACK_RETRY_COUNT), the tick it waits until before moving again (a retry's
// three seconds; 0: none), and whether it was fighting at its last update (its attack machine not idle).
export namespace engine::gameplay
{
struct AttackMove
{
	static constexpr std::uint32_t RetryCount = 5;       // ATTACK_RETRY_COUNT
	static constexpr std::int32_t CloseEnoughCells = 8;  // ATTACK_CLOSE_ENOUGH_CELLS
	Engine::Math::FixedVector2 destination;
	std::uint64_t sleepUntil{0};
	std::uint32_t retries{RetryCount};
	std::uint8_t engaged{0};
	std::uint8_t reserved[3]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::AttackMove>
{
	static constexpr std::string_view StableName = "engine.gameplay.attack_move";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
