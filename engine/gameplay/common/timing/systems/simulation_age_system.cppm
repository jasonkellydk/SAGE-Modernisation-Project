export module engine.gameplay.common.timing.systems.simulation_age_system;
import std;
export import engine.gameplay.common.timing.components.simulation_age;
export namespace engine::gameplay {
struct SimulationAgeSystem {
    using Query=ecs::Query<ecs::Write<SimulationAge>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext&) const {
        for(auto& age:chunk.Get<SimulationAge>()) if(age.enabled) {
            if(age.ticks==std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("simulation age exhausted");
            ++age.ticks;
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::SimulationAgeSystem> {
    static constexpr std::string_view StableName="engine.gameplay.simulation_age";
    static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::Simulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<>;
};
}
