export module games.generalszh.gameplay.ai.components.guard;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// A unit guarding a position or an object (AIUpdateInterface::aiGuardPosition / aiGuardObject: AIGuardState with its
// AIGuardMachine), as data: the machine's state and whether its onEnter has run, what it guards (the object, the trigger
// area, else the position), its mode, its nemesis, the idle look's and the return's next scans, the running attack's exit conditions
// (ExitConditions: centre, radius, give-up tick and which apply), where the guarded object stood at the last look
// (AIGuardIdleState::m_guardeePos) and the last damage it has looked at (the body's clearable last attacker).
// GuardRules: AIData's GuardEnemyScanRate, GuardEnemyReturnScanRate and GuardChaseUnitsDuration (frames).
// GuardEvents: what the tick's guards did beyond themselves (the team's victim cleared), applied after the step.
// Simulation state: checkpointed.
export namespace generalszh::gameplay
{
enum class GuardState : std::uint8_t
{
	Inner,     // AIGuardInnerState: attacking the nemesis near what it guards
	Return,    // AIGuardReturnState: back to what it guards, looking out on the way
	Idle,      // AIGuardIdleState: looking out
	Outer,     // AIGuardOuterState: chasing further, for a while
	Aggressor, // AIGuardAttackAggressorState: striking back
};

enum class GuardMode : std::uint8_t
{
	Normal,        // GUARDMODE_NORMAL
	WithoutPursuit, // GUARDMODE_GUARD_WITHOUT_PURSUIT: no chase beyond the inner range
	FlyingOnly,    // GUARDMODE_GUARD_FLYING_UNITS_ONLY: only airborne targets
	Retaliate,     // AI_GUARD_RETALIATE: AIGuardRetaliateMachine, striking back where it stands, then done
};

namespace guard_exit
{
inline constexpr std::uint8_t Expired = 1;   // ATTACK_ExitIfExpiredDuration
inline constexpr std::uint8_t Outside = 2;   // ATTACK_ExitIfOutsideRadius
inline constexpr std::uint8_t NoUnit = 4;    // ATTACK_ExitIfNoUnitFound
}

struct Guard
{
	Engine::Math::FixedVector2 position;
	Engine::Math::FixedVector2 exitCenter;
	Engine::Math::FixedVector2 guardeeAt;
	ecs::Entity target;
	ecs::Entity nemesis;
	Engine::Math::Fixed exitRadius;
	std::uint64_t nextScan{0};
	std::uint64_t nextReturnScan{0};
	std::uint64_t giveUp{0};
	std::uint64_t damageSeen{0};
	GuardState state{GuardState::Inner};
	std::uint8_t entered{0};
	GuardMode mode{GuardMode::Normal};
	std::uint8_t exitFlags{0};
	std::uint32_t area{0xFFFFFFFFu}; // the trigger area it guards (TriggerAreas; none: 0xFFFFFFFF)
};

struct GuardRules
{
	std::uint64_t scanTicks{15};
	std::uint64_t returnScanTicks{30};
	std::uint64_t chaseTicks{0};
};

struct GuardEvent
{
	enum class Kind : std::uint8_t
	{
		ClearTeamTarget, // setTeamTargetObject(none)
		End,             // the machine exited (EXIT_MACHINE_WITH_SUCCESS): its AI goes idle (AI_IDLE)
	};
	ecs::Entity unit;
	Kind kind{Kind::ClearTeamTarget};
};

struct GuardEvents : ecs::ChunkOutputs<GuardEvent>
{
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::Guard>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.guard";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const generalszh::gameplay::Guard &value, StateHasher &hasher) noexcept
	{
		for (const auto &point : {value.position, value.exitCenter, value.guardeeAt})
		{
			hasher.AppendU64(static_cast<std::uint64_t>(point.x.Raw()));
			hasher.AppendU64(static_cast<std::uint64_t>(point.y.Raw()));
		}
		hasher.AppendU64((std::uint64_t{value.target.index} << 32) | value.target.generation);
		hasher.AppendU64((std::uint64_t{value.nemesis.index} << 32) | value.nemesis.generation);
		hasher.AppendU64(static_cast<std::uint64_t>(value.exitRadius.Raw()));
		hasher.AppendU64(value.nextScan);
		hasher.AppendU64(value.nextReturnScan);
		hasher.AppendU64(value.giveUp);
		hasher.AppendU64(value.damageSeen);
		hasher.AppendU64(static_cast<std::uint64_t>(value.state) | (std::uint64_t{value.entered} << 8) | (static_cast<std::uint64_t>(value.mode) << 16) |
			(std::uint64_t{value.exitFlags} << 24) | (std::uint64_t{value.area} << 32));
	}
};

template<>
struct ResourceTraits<generalszh::gameplay::GuardRules>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.guard_rules";
};

template<>
struct ResourceTraits<generalszh::gameplay::GuardEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.guard_events";
};
}
