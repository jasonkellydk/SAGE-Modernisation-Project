export module games.generalszh.gameplay.ai.components.tunnel_guard;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
import engine.ecs.system.system;

// A unit guarding its player's tunnel network (AIUpdateInterface::aiGuardTunnelNetwork: AITunnelNetworkGuardState with
// its AITNGuardMachine), as data: the machine's state and whether its onEnter has run, its nemesis (the machine's), the
// idle look's next tick, the attack's give-up tick (TunnelNetworkExitConditions), the inner attack's one scan left, the
// last damage it has looked at (the body's clearable last attacker) and when it asked to board.
// TunnelGuardRules: AIData's GuardEnemyScanRate, GuardChaseUnitsDuration (frames), the game's rules.
// TunnelGuardEvents: what the tick's guards did beyond themselves, in order (TunnelGuardSystem; applied after the step).
// Simulation state: checkpointed.
export namespace generalszh::gameplay
{
enum class TunnelGuardState : std::uint8_t
{
	Return,    // AITNGuardReturnState: into the nearest tunnel
	Idle,      // AITNGuardIdleState: looking out for the network
	Inner,     // AITNGuardInnerState: attacking the nemesis
	Outer,     // AITNGuardOuterState: chasing it further
	Aggressor, // AITNGuardAttackAggressorState: striking back
};

struct TunnelGuard
{
	TunnelGuardState state{TunnelGuardState::Return};
	std::uint8_t entered{0};
	std::uint8_t scanForEnemy{0};
	std::uint8_t withoutPursuit{0}; // GUARDMODE_GUARD_WITHOUT_PURSUIT
	std::uint32_t reserved{0};
	ecs::Entity nemesis;
	std::uint64_t nextScan{0};
	std::uint64_t giveUp{0};
	std::uint64_t damageSeen{0};
	std::uint64_t boardAsked{0};
};

struct TunnelGuardRules
{
	std::uint64_t scanTicks{15};
	std::uint64_t chaseTicks{0};
};

struct TunnelGuardEvent
{
	enum class Kind : std::uint8_t
	{
		Board,            // go into `target` (a tunnel)
		Exit,             // out at `target` (a tunnel) at once (exitObjectInAHurry)
		Nemesis,          // TunnelTracker::updateNemesis(target)
		TeamTarget,       // setTeamTargetObject(target)
		ClearTeamTarget,  // setTeamTargetObject(none)
	};
	ecs::Entity unit;
	ecs::Entity target;
	Kind kind{Kind::Board};
};

struct TunnelGuardEvents : ecs::ChunkOutputs<TunnelGuardEvent>
{
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::TunnelGuard>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.tunnel_guard";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const generalszh::gameplay::TunnelGuard &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.state) | (std::uint64_t{value.entered} << 8) | (std::uint64_t{value.scanForEnemy} << 16) |
			(std::uint64_t{value.withoutPursuit} << 24));
		hasher.AppendU64((std::uint64_t{value.nemesis.index} << 32) | value.nemesis.generation);
		hasher.AppendU64(value.nextScan);
		hasher.AppendU64(value.giveUp);
		hasher.AppendU64(value.damageSeen);
		hasher.AppendU64(value.boardAsked);
	}
};

template<>
struct ResourceTraits<generalszh::gameplay::TunnelGuardRules>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.tunnel_guard_rules";
};

template<>
struct ResourceTraits<generalszh::gameplay::TunnelGuardEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.tunnel_guard_events";
};
}
