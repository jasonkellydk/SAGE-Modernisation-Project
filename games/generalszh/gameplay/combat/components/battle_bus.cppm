export module games.generalszh.gameplay.combat.components.battle_bus;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// A battle bus's first death (BattleBusSlowDeathBehavior while not really dead): thrown up (m_isInFirstDeath) until it
// lands the first update after its ground check tick; then a hulk, checked every EMPTY_HULK_CHECK_DELAY (15) ticks,
// killed (PENALTY, EXTRA_4) once empty for EmptyHulkDestructionDelay (m_penaltyDeathFrame). Simulation state:
// checkpointed.
export namespace generalszh::gameplay
{
enum class BusPhase : std::uint8_t
{
	Driving,
	Thrown,
	Hulk,
};

struct BattleBus
{
	std::uint64_t groundCheckTick{0};
	std::uint64_t penaltyTick{0}; // 0: not counting
	std::uint64_t nextCheck{0};   // the hulk's next update (0: never: no delay)
	BusPhase phase{BusPhase::Driving};
	std::uint8_t reserved[7]{};
};

// Each definition's BattleBusSlowDeathBehavior (present or not): which of its slow deaths it is (DeathDefinition::slow),
// FXStartUndeath, OCLStartUndeath, FXHitGround, OCLHitGround, ThrowForce, PercentDamageToPassengers and
// EmptyHulkDestructionDelay (ticks).
struct BattleBusConfig
{
	bool present{false};
	std::uint32_t slowIndex{0};
	std::string fxStart;
	std::string oclStart;
	std::string fxHitGround;
	std::string oclHitGround;
	Engine::Math::Fixed throwForce{Engine::Math::Fixed::One()};
	Engine::Math::Fixed passengerDamage;
	std::uint32_t hulkDelayTicks{0};
};

inline constexpr std::uint64_t BusGroundCheckDelay = 10; // GROUND_CHECK_DELAY
inline constexpr std::uint64_t BusHulkCheckDelay = 15;   // EMPTY_HULK_CHECK_DELAY
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::BattleBus>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.battle_bus";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
