export module engine.gameplay.common.scripts.systems.message_timer_system;
import std;
export import engine.gameplay.common.scripts.resources.behavior_programs;

export namespace engine::gameplay {
struct MessageTimerSystem {
    using Query=ecs::Query<ecs::Read<ScheduledBehaviorMessage>>;
    using Lookup=ecs::Lookup<>;
    using Resources=ecs::Resources<ecs::Write<DueBehaviorMessages>>;
    void BeforeChunks(Query& query,ecs::SystemContext& context) const {
        context.Write<DueBehaviorMessages>().Reset(query.PreparedChunkCount());
    }
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto timers=chunk.Get<ScheduledBehaviorMessage>();
        const auto entities=chunk.Entities();const auto alive=context.Lookup<Lookup>();
        auto& output=context.Write<DueBehaviorMessages>().Slot(context);
        for(std::size_t row=0;row<timers.size();++row) {
            const auto& message=timers[row].message;
            if(!alive.IsAlive(message.recipient) || message.observer!=ecs::Entity{} && !alive.IsAlive(message.observer)) {
                context.Commands().Destroy(entities[row]);continue;
            }
            if(message.due_tick>context.Tick()) continue;
            if(message.argument_count>message.arguments.size()) throw std::out_of_range("behavior message argument count");
            output.push_back(message);context.Commands().Destroy(entities[row]);
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::MessageTimerSystem> {
    static constexpr std::string_view StableName="engine.gameplay.message_timer";
    static constexpr std::size_t PieceRows=32;
    static constexpr SystemPhase Phase=SystemPhase::PreSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<>;
};
}
