export module engine.gameplay.common.spatial.systems.tracked_position_system;
import std;
export import engine.gameplay.common.spatial.components.tracked_position;
export import engine.ecs.system.system;
export namespace engine::gameplay {
struct TrackedPositionSystem {
    using Query=ecs::Query<ecs::Write<TrackedPosition>>;
    using Lookup=ecs::Lookup<ecs::Read<Transform>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto lookup=context.Lookup<Lookup>();
        for(auto& marker:chunk.Get<TrackedPosition>()) {
            const auto* target=lookup.Get<Transform>(marker.target);
            marker.resolved=target ? target->position : marker.point;
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::TrackedPositionSystem> {
    static constexpr std::string_view StableName="engine.gameplay.tracked_position";
    static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<>;
};
}
