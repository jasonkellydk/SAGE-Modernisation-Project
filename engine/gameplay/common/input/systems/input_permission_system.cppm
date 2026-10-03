export module engine.gameplay.common.input.systems.input_permission_system;
import std;
export import engine.gameplay.common.input.components.input_permission;
export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;

export namespace engine::gameplay {
struct InputPermissionRequest {ecs::Entity subject;std::uint64_t order{};std::uint32_t enabled{};};
using InputPermissionRequests=ecs::ChunkOutputs<InputPermissionRequest>;
}
export namespace ecs {
template<> struct ResourceTraits<engine::gameplay::InputPermissionRequests> {
    static constexpr std::string_view StableName="engine.gameplay.input_permission_requests";
};
}
export namespace engine::gameplay {
struct InputPermissionSystem {
    using Query=ecs::Query<ecs::Write<InputPermission>>;
    using Resources=ecs::Resources<ecs::Read<InputPermissionRequests>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto entities=chunk.Entities();const auto permissions=chunk.Get<InputPermission>();
        for(std::size_t row=0;row<permissions.size();++row) {
            const InputPermissionRequest* latest{};
            context.Read<InputPermissionRequests>().ForEach([&](const auto& request) {
                if(request.subject==entities[row] && (!latest || request.order>=latest->order)) latest=&request;
            });
            if(latest) permissions[row].enabled=bool(latest->enabled);
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::InputPermissionSystem> {
    static constexpr std::string_view StableName="engine.gameplay.input_permission";
    static constexpr std::size_t PieceRows=32;
    static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<>;
};
}
