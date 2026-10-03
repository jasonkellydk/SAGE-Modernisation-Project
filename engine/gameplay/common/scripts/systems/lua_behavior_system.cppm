export module engine.gameplay.common.scripts.systems.lua_behavior_system;
import std;
export import engine.gameplay.common.scripts.components.lua_behavior;
export import engine.gameplay.common.scripts.systems.message_timer_system;

export namespace engine::gameplay {
// No per-entity VM registry: immutable source is shared, persistent integers
// are ECS columns, and a callback uses a temporary private VM on its worker.
struct LuaBehaviorSystem {
    using Query=ecs::Query<ecs::Write<LuaBehavior>,ecs::Write<LuaBehaviorState>>;
    using Lookup=ecs::Lookup<>;
    using Resources=ecs::Resources<ecs::Read<BehaviorPrograms>,ecs::Read<BehaviorInvocations>,ecs::Read<BehaviorInbox>,ecs::Read<DueBehaviorMessages>,ecs::Write<BehaviorEmissions>>;
    void BeforeChunks(Query& query,ecs::SystemContext& context) const {
        context.Write<BehaviorEmissions>().Reset(query.PreparedChunkCount());
    }
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto bindings=chunk.Get<LuaBehavior>();const auto states=chunk.Get<LuaBehaviorState>();
        const auto entities=chunk.Entities();const auto alive=context.Lookup<Lookup>();
        const auto& library=context.Read<BehaviorPrograms>();auto& output=context.Write<BehaviorEmissions>().Slot(context);
        for(std::size_t row=0;row<bindings.size();++row) {
            auto& binding=bindings[row];if(!alive.IsAlive(binding.subject)) {context.Commands().Destroy(entities[row]);continue;}
            if(!binding.enabled) continue;
            if(binding.definition>=library.programs.size()) throw std::out_of_range("behavior program definition");
            std::vector<BehaviorMessage> messages;
            const auto accept=[&](const BehaviorMessage& message) {
                if(message.recipient==binding.subject && (message.observer==ecs::Entity{} || message.observer==entities[row])) messages.push_back(message);
            };
            context.Read<DueBehaviorMessages>().ForEach(accept);
            for(const auto& message:context.Read<BehaviorInbox>().messages) accept(message);
            if(binding.initialized && messages.empty()) continue;
            std::stable_sort(messages.begin(),messages.end(),[](const auto& a,const auto& b) {return std::tie(a.due_tick,a.order)<std::tie(b.due_tick,b.order);});
            const auto& definition=library.programs[binding.definition];
            engine::scripting::lua::Program program;
            const auto loaded=program.Load(definition.source,definition.name);
            if(!loaded) throw std::runtime_error(definition.name+": "+loaded.error());
            const auto restored=program.RestoreState(states[row].slots);
            if(!restored) throw std::runtime_error(restored.error());
            std::vector<BehaviorEmission> emissions;
            const auto invoke=[&](std::string_view name,std::span<const engine::scripting::lua::Value> arguments) {
                auto commands=program.Invoke(name,arguments);
                if(!commands) throw std::runtime_error(definition.name+"."+std::string(name)+": "+commands.error());
                for(auto& command:*commands) emissions.push_back({entities[row],binding.subject,binding.order,context.Tick(),std::move(command)});
            };
            if(!binding.initialized && !definition.startup_event.empty()) {
                const auto& invocations=context.Read<BehaviorInvocations>().arguments;
                if(binding.invocation!=0xffffffffu && binding.invocation>=invocations.size()) throw std::out_of_range("behavior invocation definition");
                invoke(definition.startup_event,binding.invocation==0xffffffffu ? definition.startup_arguments : invocations[binding.invocation]);
            }
            for(const auto& message:messages) {
                if(message.event>=library.event_names.size() || message.argument_count>message.arguments.size()) throw std::out_of_range("behavior event definition");
                std::vector<engine::scripting::lua::Value> arguments;
                for(std::uint32_t i=0;i<message.argument_count;++i) arguments.emplace_back(message.arguments[i]);
                invoke(library.event_names[message.event],arguments);
            }
            const auto saved=program.CaptureState();if(!saved) throw std::runtime_error(definition.name+": "+saved.error());
            states[row].slots=*saved;binding.initialized=1;
            output.insert(output.end(),std::make_move_iterator(emissions.begin()),std::make_move_iterator(emissions.end()));
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::LuaBehaviorSystem> {
    static constexpr std::string_view StableName="engine.gameplay.lua_behavior";
    static constexpr std::size_t PieceRows=32;
    static constexpr SystemPhase Phase=SystemPhase::PreSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<engine::gameplay::MessageTimerSystem>;
};
}
