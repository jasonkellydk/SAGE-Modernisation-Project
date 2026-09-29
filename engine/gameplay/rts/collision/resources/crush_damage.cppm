export module engine.gameplay.rts.collision.resources.crush_damage;
import std;

export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.common.health.resources.incoming_damage;
import engine.ecs.system.system;

// How being run over hurts (the game's damage and death types for it), and
// this tick's crushes, gathered per chunk.
export namespace engine::gameplay
{
struct CrushSettings
{
	std::uint32_t damageType{0};
	std::uint32_t deathType{0};
	Engine::Math::Fixed amount{Engine::Math::Fixed::FromInt(1000000)}; // enough to kill anything
};

struct CrushDamage : ecs::ChunkOutputs<DamageRecord>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::CrushSettings>
{
	static constexpr std::string_view StableName = "engine.gameplay.crush_settings";
};
template<>
struct ResourceTraits<engine::gameplay::CrushDamage>
{
	static constexpr std::string_view StableName = "engine.gameplay.crush_damage";
};
}
