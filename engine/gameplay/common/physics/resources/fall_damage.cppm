export module engine.gameplay.common.physics.resources.fall_damage;
import std;

export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.common.health.resources.incoming_damage;
import engine.ecs.system.system;

// Falling damage this tick: what hard landings do (per chunk of the physics
// step), for the tick's incoming damage.
export namespace engine::gameplay
{
struct FallDamage : ecs::ChunkOutputs<DamageRecord>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::FallDamage>
{
	static constexpr std::string_view StableName = "engine.gameplay.fall_damage";
};
}
