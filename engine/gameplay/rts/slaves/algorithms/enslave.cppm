export module engine.gameplay.rts.slaves.algorithms.enslave;
import std;

export import engine.gameplay.rts.slaves.components.slaved;
import Engine.Core.Math.FixedRandom;
import Engine.Core.Math.FixedAngle;

// Slaves' shared rules: a point `range` away in a random direction (their
// guard and wander offsets), and taking a master (SlavedUpdate::onEnslave).
// The direction is GameLogicRandomValue(0, 2*PI): an integer, so a whole number
// of radians from 0 to 6 (2*PI truncated), one of seven directions.
export namespace engine::gameplay
{
namespace slaved_detail
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;

inline FixedVector2 RandomReach(Engine::Math::RandomStream &random, Fixed range)
{
	const auto radians = Fixed::FromInt(Engine::Math::UniformInt(random, 0, 6));
	return Engine::Math::Direction(Engine::Math::TurnFromRadians(radians)) * range;
}
}

// startSlavedEffects: a slave's master, and its first guard point (GuardMaxRange in a random direction).
inline void Enslave(Slaved &slave, ecs::Entity master, Engine::Math::Fixed masterRadius, std::uint64_t seed, std::uint64_t tick, ecs::Entity self)
{
	slave.masterRadius = masterRadius;
	auto random = Engine::Math::Stream(seed ^ 0x51A8u, {tick, self.index, self.generation});
	slave.master = master;
	slave.enslaved = 1;
	slave.guardOffset = slaved_detail::RandomReach(random, slave.definition.guardMaxRange);
}
}
