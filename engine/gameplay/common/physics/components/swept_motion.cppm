export module engine.gameplay.common.physics.components.swept_motion;
import std;
export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

export namespace engine::gameplay {
// A requested displacement for one simulation step, followed by the actual
// displacement. Consumption prevents stale input from moving an idle actor.
struct SweptMotion {Engine::Math::FixedVector3 requested,applied;};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::SweptMotion> {
    static constexpr std::string_view StableName="engine.gameplay.swept_motion";
    static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
