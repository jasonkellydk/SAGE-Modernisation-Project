export module engine.gameplay.common.timing.components.simulation_age;
import std;
export import engine.ecs.system.system;
export import Engine.Core.Math.Fixed;

export namespace engine::gameplay {
// A pausable duration in authoritative ticks. The composing game owns resets
// and enabling; conversion from total ticks avoids accumulated fixed rounding.
struct SimulationAge {
    std::uint64_t ticks{};
    std::uint32_t enabled{1},reserved{};
    Engine::Math::Fixed Seconds(std::uint32_t rate) const {return Engine::Math::Fixed::FromRatio(ticks,rate);}
};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::SimulationAge> {
    static constexpr std::string_view StableName="engine.gameplay.simulation_age";
    static constexpr std::uint32_t Version=1;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
