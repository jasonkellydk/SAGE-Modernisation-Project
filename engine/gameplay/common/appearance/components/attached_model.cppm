export module engine.gameplay.common.appearance.components.attached_model;
import std;
export import engine.ecs.core.component_registry;
export namespace engine::gameplay {
// A presentation model attached to this entity's evaluated skeletal socket.
// Game content resolves both IDs before publishing the scene.
struct AttachedModel {std::uint32_t model{},bone{};};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::AttachedModel> {
    static constexpr std::string_view StableName="engine.gameplay.attached_model";static constexpr std::uint32_t Version=1;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
