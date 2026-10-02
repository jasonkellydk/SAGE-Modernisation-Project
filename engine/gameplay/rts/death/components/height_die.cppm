export module engine.gameplay.rts.death.components.height_die;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;
export import engine.ecs.core.entity;
import engine.ecs.system.system;

// HeightDieUpdate: dies once below TargetHeight above the ground (optionally only while falling), snapping to the ground
// when asked (and never staying below it), after InitialDelay ticks from its first look; nothing while carried. With
// TargetHeightIncludesStructures its target is instead above the tallest structure within its reach, when that stands
// taller than TargetHeight. Below DestroyAttachedParticlesAtHeight (a height, not above the ground; negative: never) its
// attached particle systems go, once (`particlesGone`).
// ParticleClears: this tick's objects whose attached particle systems go (ParticleSystemManager::destroyAttachedSystems);
// cleared as each tick starts.
export namespace engine::gameplay
{
struct HeightDie
{
	Engine::Math::Fixed targetHeight;  // TargetHeight
	Engine::Math::Fixed lastZ{Engine::Math::Fixed::FromInt(-1)}; // m_lastPosition.z
	std::uint64_t initialDelay{0};     // InitialDelay, ticks
	std::uint64_t earliestDeath{std::numeric_limits<std::uint64_t>::max()}; // not yet looked
	std::uint8_t onlyWhenMovingDown{0}; // OnlyWhenMovingDown
	std::uint8_t snapToGround{0};       // SnapToGroundOnDeath
	std::uint8_t died{0};
	std::uint8_t includeStructures{0}; // TargetHeightIncludesStructures
	std::uint8_t particlesGone{0};
	std::uint8_t reserved[3]{};
	Engine::Math::Fixed particlesAt{Engine::Math::Fixed::FromInt(-1)}; // DestroyAttachedParticlesAtHeight
	// Its own geometry (whatever its body): its bounding circle's radius (how far it looks), its bounding sphere's
	// radius and that sphere's centre above its position.
	Engine::Math::Fixed circleRadius;
	Engine::Math::Fixed sphereRadius;
	Engine::Math::Fixed centerZ;
};

struct ParticleClears
{
	std::vector<ecs::Entity> entities;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::HeightDie>
{
	static constexpr std::string_view StableName = "engine.gameplay.height_die";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<engine::gameplay::ParticleClears>
{
	static constexpr std::string_view StableName = "engine.gameplay.particle_clears";
};
}
