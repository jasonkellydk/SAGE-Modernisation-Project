export module engine.gameplay.rts.movement.components.descent;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// Coming down under a parachute (or falling): the entity sinks at `rate`
// units per tick and does nothing else until it touches the surface.
export namespace engine::gameplay
{
struct Descent
{
	Engine::Math::Fixed rate;
};

}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Descent>
{
	static constexpr std::string_view StableName = "engine.gameplay.descent";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Descent &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.rate.Raw()));
	}
};
}
