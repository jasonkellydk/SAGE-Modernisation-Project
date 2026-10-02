export module engine.gameplay.rts.combat.resources.garrison_kills;
import std;

export import engine.ecs.system.chunk_outputs;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
export import engine.gameplay.rts.combat.resources.shots;
import engine.ecs.system.system;

// Projectiles that clear garrisons (DumbProjectileBehavior::projectileHandleCollision with GarrisonHitKillCount):
// - GarrisonHits: this tick's such projectiles that ran into a structure (ProjectileFlightSystem, in place of their
//   detonation): the shot, the structure, how many of what kinds it may kill (target classes: GarrisonHitKill
//   Required/ForbiddenKindOf) and the projectile's definition.
// - GarrisonClears (GarrisonClearSystem, for the ImpactSystem): each occupant killed (by the launcher: scoreTheKill,
//   then kill(): unresistable damage of its whole health, a normal death), and the shots that cleared nobody, which
//   detonate as usual.
export namespace engine::gameplay
{
struct GarrisonHit
{
	Shot shot;
	ecs::Entity building;
	std::uint32_t count{0};
	std::uint32_t requiredClasses{0};
	std::uint32_t forbiddenClasses{0};
	std::uint32_t projectileDefinition{0xFFFFFFFFu};
};

struct GarrisonHits : ecs::ChunkOutputs<GarrisonHit>
{
};

// The same from guided missiles (MissileAIUpdate::projectileHandleCollision), written by their flight.
struct MissileGarrisonHits : ecs::ChunkOutputs<GarrisonHit>
{
};

struct GarrisonKill
{
	ecs::Entity building;
	ecs::Entity victim;
	ecs::Entity source;
	std::uint32_t sourcePlayer{0};
	std::uint32_t projectileDefinition{0xFFFFFFFFu};
	Engine::Math::Fixed amount;
};

struct GarrisonClears
{
	std::vector<GarrisonKill> kills;
	std::vector<Shot> detonations;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::GarrisonHits>
{
	static constexpr std::string_view StableName = "engine.gameplay.garrison_hits";
};

template<>
struct ResourceTraits<engine::gameplay::MissileGarrisonHits>
{
	static constexpr std::string_view StableName = "engine.gameplay.missile_garrison_hits";
};

template<>
struct ResourceTraits<engine::gameplay::GarrisonClears>
{
	static constexpr std::string_view StableName = "engine.gameplay.garrison_clears";
};
}
