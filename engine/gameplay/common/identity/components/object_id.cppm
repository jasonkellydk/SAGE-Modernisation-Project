export module engine.gameplay.common.identity.components.object_id;
import std;

export import engine.ecs.core.component_registry;

// An object's id, handed out in creation order (the original's ObjectID): the newest object has the highest. Where
// the original walks every object (newest first), the port walks these, highest first.
export namespace engine::gameplay
{
struct ObjectId
{
	std::uint32_t value{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::ObjectId>
{
	static constexpr std::string_view StableName = "engine.gameplay.object_id";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
