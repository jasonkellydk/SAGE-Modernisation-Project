export module engine.gameplay.rts.movement.components.pursuit;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// A mobile attacker's chase of a victim that runs from it (the original's AttackStateMachine CHASE_TARGET state for an
// object, AIAttackPursueTargetState): `active` while it pursues; where it last sent itself (m_prevVictimPos) and the tick
// it last worked its way out (m_approachTimestamp); the speed it asks for (setDesiredSpeed) when `matched` (else as fast as
// it can); and whether its kind lets it set out to crush infantry on its own (not KINDOF_DONT_AUTO_CRUSH_INFANTRY).
export namespace engine::gameplay
{
struct Pursuit
{
	Engine::Math::FixedVector2 prevVictim;
	std::uint64_t approachTick{0};
	Engine::Math::Fixed speed;
	std::uint8_t active{0};
	std::uint8_t matched{0};
	std::uint8_t autoCrush{1};
	std::uint8_t reserved[5]{}; // no padding: checkpoints hold its bytes
};

// TAiData::m_aiCrushesInfantry (AICrushesInfantry): a computer player's vehicles go for infantry they can crush.
struct ChaseRules
{
	bool aiCrushesInfantry{true};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Pursuit>
{
	static constexpr std::string_view StableName = "engine.gameplay.pursuit";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Pursuit &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.prevVictim.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.prevVictim.y.Raw()));
		hasher.AppendU64(value.approachTick);
		hasher.AppendU64(static_cast<std::uint64_t>(value.speed.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.active) | static_cast<std::uint64_t>(value.matched) << 8 |
			static_cast<std::uint64_t>(value.autoCrush) << 16);
	}
};

template<>
struct ResourceTraits<engine::gameplay::ChaseRules>
{
	static constexpr std::string_view StableName = "engine.gameplay.chase_rules";
};
}
