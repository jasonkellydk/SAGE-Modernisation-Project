export module engine.gameplay.common.physics.resources.shock_waves;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// The tick's shock waves (DamageInfo's m_shockWaveVector, m_shockWaveAmount, m_shockWaveRadius, m_shockWaveTaperOff),
// one per thing a shock-wave weapon's radius damage reached, in the order dealt: from the damage's source to it (none:
// straight up), how strong, how far it reaches and the share left at its edge. Spent by ShockWaveSystem.
export namespace engine::gameplay
{
struct ShockWave
{
	ecs::Entity victim;
	Engine::Math::FixedVector3 vector;
	Engine::Math::Fixed amount;
	Engine::Math::Fixed radius;
	Engine::Math::Fixed taperOff;
};

struct ShockWaves
{
	std::vector<ShockWave> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::ShockWaves>
{
	static constexpr std::string_view StableName = "engine.gameplay.shock_waves";
};
}
