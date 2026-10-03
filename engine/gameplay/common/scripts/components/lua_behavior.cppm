export module engine.gameplay.common.scripts.components.lua_behavior;
import std;
export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import engine.scripting.lua.lua_program;

export namespace engine::gameplay {
struct LuaBehavior {
    ecs::Entity subject;
    std::uint64_t authored_subject{}, order{};
    // Optional index into the shared immutable invocation data. No strings,
    // script tables or VMs are stored in an entity's component column.
    std::uint32_t definition{}, initialized{}, enabled{1}, invocation{0xffffffffu};
};
struct LuaBehaviorState { engine::scripting::lua::IntegerState slots{}; };
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::LuaBehavior> {
    static constexpr std::string_view StableName="engine.gameplay.lua_behavior";
    static constexpr std::uint32_t Version=2;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
template<> struct ComponentTraits<engine::gameplay::LuaBehaviorState> {
    static constexpr std::string_view StableName="engine.gameplay.lua_behavior_state";
    static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
