export module engine.gameplay.rts.slaves.algorithms.enslave;
import std;

export import engine.gameplay.rts.slaves.components.slaved;
import Engine.Core.Math.FixedRandom;
import Engine.Core.Math.FixedAngle;

// Slaves' shared rules: a point `range` away in a random direction (their
// guard and wander offsets), and taking a master (SlavedUpdate::onEnslave).
export namespace engine::gameplay
{
namespace slaved_detail
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;

inline FixedVector2 RandomReach(Engine::Math::RandomStream &random, Fixed range)
{
	const auto direction = Engine::Math::Direction(Engine::Math::TurnAngle{static_cast<std::uint32_t>(Engine::Math::UniformInt(random, 0, 0xFFFFFFFFll))});
	return direction * range;
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
