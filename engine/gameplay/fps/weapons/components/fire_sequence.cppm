export module engine.gameplay.fps.weapons.components.fire_sequence;
import std;
export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;

export namespace engine::gameplay {
struct FireTrigger {Engine::Math::FixedVector3 target;std::uint32_t pressed{},force{};};
enum class FirePhase:std::uint32_t {Ready,Charge,Cooldown,Reload};
struct FireSequence {
    Engine::Math::Fixed period{},charge{},reload{},remaining{};
    std::uint32_t capacity{},rounds{},shots{},fired{};
    FirePhase phase{};std::uint32_t reserved{};
};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::FireTrigger> {
    static constexpr std::string_view StableName="engine.gameplay.fire_trigger";static constexpr std::uint32_t Version=1;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
template<> struct ComponentTraits<engine::gameplay::FireSequence> {
    static constexpr std::string_view StableName="engine.gameplay.fire_sequence";static constexpr std::uint32_t Version=1;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
