export module engine.gameplay.common.scripts.components.timeline_behavior;
import std;
export import engine.gameplay.common.scripts.components.lua_behavior;
export namespace engine::gameplay {
// A prepared observer starts only when its owning timeline fires this cue.
// Script names, parameters and gameplay effects remain in composition data.
struct TimelineBehavior {ecs::Entity clock;std::uint32_t action{},reserved{};};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::TimelineBehavior> {
    static constexpr std::string_view StableName="engine.gameplay.timeline_behavior";
    static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
