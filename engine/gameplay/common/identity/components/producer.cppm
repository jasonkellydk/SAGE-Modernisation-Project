export module engine.gameplay.common.identity.components.producer;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;

// What made an object (the original's Object::m_producerID, set by setProducer): the factory it came out of, the
// object whose creation list made it, the spawner that spawned it. None: nothing did.
export namespace engine::gameplay
{
struct Producer
{
	ecs::Entity entity;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Producer>
{
	static constexpr std::string_view StableName = "engine.gameplay.producer";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
