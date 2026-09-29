export module engine.gameplay.rts.combat.components.missile;
import std;

export import engine.gameplay.rts.combat.resources.shots;
export import engine.ecs.core.component_registry;

// A guided missile in flight (the original's MissileAIUpdate with its THRUST
// locomotor and physics): its state and when it entered it, its velocity and
// nose, where it steers (its victim's position while it tracks one), how far
// it still flies straight before turning, when its fuel runs out, whether it
// is making its final run (braking: it moves straight at the goal), and the
// shot it carries (landing where it detonates).
export namespace engine::gameplay
{
enum class MissileState : std::uint8_t
{
	Launch,
	Ignition,
	AttackNoTurn,
	Attack,
	Kill,
	KillSelf
};

struct MissileFlight
{
	Engine::Math::FixedVector3 velocity;
	Engine::Math::FixedVector3 forward;
	Engine::Math::FixedVector3 previous;     // m_prevPos
	Engine::Math::FixedVector3 goal;
	Engine::Math::FixedVector3 originalGoal; // m_originalTargetPos
	Engine::Math::Fixed noTurnLeft;          // m_noTurnDistLeft
	Engine::Math::Fixed maxSpeed;            // its locomotor's, or its weapon's speed
	Engine::Math::Fixed maxAccel;            // m_maxAccel (Unlimited-like: its locomotor's)
	std::uint64_t stateTick{0};
	std::uint64_t fuelTick{~std::uint64_t{0}};
	MissileState state{MissileState::Launch};
	bool tracking{false};
	bool braking{false};
	bool armed{false};
	bool preciseZ{false};
	bool exhaustLit{false}; // lit at ignition; tossed once its fuel runs out or it is done
	std::uint8_t noDamage{0}; // decoyed: it goes off harmlessly (m_noDamage)
	std::uint8_t jammed{0};   // subdued: its goal scattered for good (m_isJammed)
	std::uint64_t decoyTick{0}; // when it turns for its victim's flare (m_framesTillDecoyed; 0: never)
	Shot shot{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::MissileFlight>
{
	static constexpr std::string_view StableName = "engine.gameplay.missile_flight";
	static constexpr std::uint32_t Version = 3;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::MissileFlight &value, StateHasher &hasher) noexcept
	{
		for (const auto *v : {&value.velocity, &value.forward, &value.previous, &value.goal, &value.originalGoal})
		{
			hasher.AppendU64(static_cast<std::uint64_t>(v->x.Raw()));
			hasher.AppendU64(static_cast<std::uint64_t>(v->y.Raw()));
			hasher.AppendU64(static_cast<std::uint64_t>(v->z.Raw()));
		}
		hasher.AppendU64(static_cast<std::uint64_t>(value.noTurnLeft.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.maxSpeed.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.maxAccel.Raw()));
		hasher.AppendU64(value.stateTick);
		hasher.AppendU64(value.fuelTick);
		hasher.AppendU64(static_cast<std::uint64_t>(value.state) | (value.tracking ? 0x100u : 0u) | (value.braking ? 0x200u : 0u) |
			(value.armed ? 0x400u : 0u) | (value.preciseZ ? 0x800u : 0u) | (value.exhaustLit ? 0x1000u : 0u));
		hasher.AppendU64((static_cast<std::uint64_t>(value.shot.source.index) << 32) | value.shot.source.generation);
		hasher.AppendU64((static_cast<std::uint64_t>(value.shot.target.index) << 32) | value.shot.target.generation);
		hasher.AppendU64((static_cast<std::uint64_t>(value.shot.weapon) << 32) | value.shot.sourcePlayer);
		hasher.AppendU64(value.shot.fireTick);
		hasher.AppendU64(value.decoyTick ^ (std::uint64_t{value.noDamage} << 63) ^ (std::uint64_t{value.jammed} << 62));
	}
};
}
