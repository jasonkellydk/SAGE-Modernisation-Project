export module games.generalszh.gameplay.combat.components.firestorm;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// A firestorm (the original's FirestormDynamicGeometryInfoUpdate, on a DynamicGeometry): whether its effects have been
// started (m_effectsFired: its particle systems and FXList as it first grows), its scorch left (m_scorchPlaced, once it
// turns to shrink) and the tick of its last damage (m_lastDamageFrame). FirestormConfig: its definition's
// DelayBetweenDamageFrames (ticks, a real), DamageAmount, MaxHeightForDamage (20 unless given), ScorchSize,
// ParticleOffsetZ, FXList and ParticleSystem1-16. Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct Firestorm
{
	std::uint64_t lastDamageTick{0};
	std::uint8_t effectsFired{0};
	std::uint8_t scorchPlaced{0};
	std::uint8_t reserved[6]{};
};

struct FirestormConfig
{
	bool present{false};
	Engine::Math::Fixed damageDelay; // ticks
	Engine::Math::Fixed damage;
	Engine::Math::Fixed maxHeight{Engine::Math::Fixed::FromInt(20)};
	Engine::Math::Fixed scorchSize;
	Engine::Math::Fixed particleOffsetZ;
	std::string fx;
	std::vector<std::string> particles;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::Firestorm>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.firestorm";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
