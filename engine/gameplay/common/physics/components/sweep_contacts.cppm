export module engine.gameplay.common.physics.components.sweep_contacts;
import std;
export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

export namespace engine::gameplay {
struct SweepContacts {
    static constexpr std::uint32_t Capacity=8;
    std::array<Engine::Math::FixedVector3,Capacity> normals{};
    std::array<std::uint64_t,Capacity> subjects{};
    std::array<std::uint32_t,Capacity> surfaces{};
    std::uint32_t count{},overlapping{};
};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::SweepContacts> {
    static constexpr std::string_view StableName="engine.gameplay.sweep_contacts";
    static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
