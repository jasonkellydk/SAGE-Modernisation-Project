export module engine.gameplay.rts.movement.components.locomotion;
import std;

export import engine.ecs.core.component_registry;
export import engine.gameplay.rts.movement.definitions.locomotor;

// A mobile entity's locomotor (copied from its definition at spawn, so the
// movement system reads contiguous data) and its current forward speed.
// Wings at zero speed with no order are parked on the ground.
export namespace engine::gameplay
{
struct Locomotion
{
	LocomotorDefinition locomotor;
	Engine::Math::Fixed speed;
	// Turning at its full rate this tick (PhysicsBehavior::getTurning): +1 to the left (counterclockwise), -1 to
	// the right, 0 not (or turning less than it could).
	std::int8_t turning{0};
	// Going straight up or down where it is (a transport landing or taking off): only its height moves.
	bool vertical{false};
	// Its weave (Locomotor::m_angleOffset, OFFSET_INCREASING, m_offsetIncrement: turn units off its goal, whether
	// that is growing, and how much it grows for each unit it moves).
	std::uint8_t wanderRising{0};
	std::uint8_t reserved0{0}; // no padding: checkpoints hold its bytes
	std::int32_t wanderOffset{0};
	std::int64_t wanderStep{0};
	// Its braking (Locomotor's IS_BRAKING and m_brakingFactor; `brakingStatus`: OBJECT_STATUS_BRAKING as its last move
	// left it, when it slides straight at its goal instead of rolling on), the frame a wheeled one near its goal gives up
	// driving about and brakes (m_donutTimer), and a wheeled one backing up (MOVING_BACKWARDS, DOING_THREE_POINT_TURN).
	Engine::Math::Fixed brakingFactor{Engine::Math::Fixed::One()};
	std::uint64_t donutTimer{0};
	// Its geometry's major radius (a three point turn needs five of them to the goal).
	Engine::Math::Fixed majorRadius;
	std::uint8_t braking{0};
	std::uint8_t brakingStatus{0};
	std::uint8_t backwards{0};
	std::uint8_t threePointTurn{0};
	// The locomotor set it moves on (AIUpdateInterface::m_curLocomotorSet: the game's numbering, 0 its normal one) and
	// whether its normal set is the upgraded one (m_upgradedLocomotors).
	std::uint8_t set{0};
	std::uint8_t upgraded{0};
	std::uint8_t reserved[2]{};
};

// Locomotor::Locomotor: a weaving locomotor starts somewhere within pi/6 of straight, swinging one way or the other at
// pi/40 (x 0.8 to 1.2, over WanderLengthFactor) a unit moved. `uniform(low, high)` rolls the game's random numbers.
template<typename Uniform>
void StartWander(Locomotion &motion, Uniform &&uniform)
{
	constexpr std::int64_t Turn = std::int64_t{1} << 32;
	const std::int64_t sixth = Turn / 12;
	motion.wanderOffset = static_cast<std::int32_t>(uniform(-sixth, sixth));
	const std::int64_t fortieth = Turn / 80; // pi/40
	const std::int64_t scale = uniform(8, 12); // tenths
	const std::int64_t length = std::max<std::int64_t>(motion.locomotor.wanderLength.Raw(), 1);
	motion.wanderStep = fortieth * scale / 10 * 65536 / length;
	motion.wanderRising = uniform(0, 1) != 0 ? 1u : 0u;
}

inline Locomotion MakeLocomotion(const LocomotorDefinition &definition) noexcept
{
	// Everything starts at rest; aircraft with wings start parked.
	Locomotion locomotion;
	locomotion.locomotor = definition;
	return locomotion;
}
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Locomotion>
{
	static constexpr std::string_view StableName = "engine.gameplay.locomotion";
	static constexpr std::uint32_t Version = 4;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Locomotion &value, StateHasher &hasher) noexcept
	{
		const auto &l = value.locomotor;
		for (const auto fixed : {l.maxSpeed, l.minSpeed, l.acceleration, l.braking, l.preferredHeight, l.preferredHeightDamping, l.closeEnough, value.speed})
			hasher.AppendU64(static_cast<std::uint64_t>(fixed.Raw()));
		hasher.AppendU64(l.turnRate.units);
		hasher.AppendU64(static_cast<std::uint64_t>(l.appearance) | static_cast<std::uint64_t>(static_cast<std::uint8_t>(value.turning)) << 8 |
			(value.vertical ? 1ull << 16 : 0ull) | static_cast<std::uint64_t>(value.wanderRising) << 24);
		hasher.AppendU64(static_cast<std::uint32_t>(value.wanderOffset));
		hasher.AppendU64(static_cast<std::uint64_t>(value.wanderStep));
		hasher.AppendU64(static_cast<std::uint64_t>(value.brakingFactor.Raw()));
		hasher.AppendU64(value.donutTimer);
		hasher.AppendU64(static_cast<std::uint64_t>(l.minTurnSpeed.Raw()) ^ (l.canMoveBackward ? 1ull << 63 : 0ull));
		hasher.AppendU64(static_cast<std::uint64_t>(value.braking) | static_cast<std::uint64_t>(value.brakingStatus) << 8 |
			static_cast<std::uint64_t>(value.backwards) << 16 | static_cast<std::uint64_t>(value.threePointTurn) << 24 |
			static_cast<std::uint64_t>(value.set) << 32 | static_cast<std::uint64_t>(value.upgraded) << 40);
		hasher.AppendU64(static_cast<std::uint64_t>(l.wanderAboutPointRadius.Raw()));
	}
};
}
