export module engine.gameplay.rts.movement.components.face_target;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.core.component_registry;

// The object a unit turns to face (aiFaceObject: AIFaceState's goal object), followed wherever it goes while its order
// is MoveMode::FaceObject. Simulation state: checkpointed.
export namespace engine::gameplay
{
struct FaceTarget
{
	ecs::Entity target;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::FaceTarget>
{
	static constexpr std::string_view StableName = "engine.gameplay.face_target";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
