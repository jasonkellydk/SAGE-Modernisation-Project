export module engine.gameplay.common.spatial.components.tracked_position;
import std;
export import engine.gameplay.common.spatial.components.transform;
export namespace engine::gameplay {
// A live entity position when available, otherwise the configured point.
// Losing the target never replaces the fallback with its last position.
struct TrackedPosition {ecs::Entity target;Engine::Math::FixedVector3 point,resolved;};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::TrackedPosition> {
    static constexpr std::string_view StableName="engine.gameplay.tracked_position";
    static constexpr std::uint32_t Version=1;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
