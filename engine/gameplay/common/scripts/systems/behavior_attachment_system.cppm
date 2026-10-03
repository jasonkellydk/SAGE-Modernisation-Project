export module engine.gameplay.common.scripts.systems.behavior_attachment_system;
import std;
export import engine.gameplay.common.scripts.systems.lua_behavior_system;

export namespace engine::gameplay {
// Parallel producers publish requests into chunk slots. The lifecycle hook
// assigns stable metadata indices and defers structural changes in authored
// order, including when the world has no existing observers.
struct BehaviorAttachmentSystem {
    using Query=ecs::Query<ecs::Read<LuaBehavior>>;
    using Lookup=ecs::Lookup<>;
    using Resources=ecs::Resources<ecs::Read<BehaviorPrograms>,ecs::Write<BehaviorAttachments>,ecs::Write<BehaviorInvocations>>;
    void Execute(Query::Chunk,ecs::SystemContext&) const {}
    void AfterChunks(Query&,ecs::SystemContext& context) const {
        std::vector<BehaviorAttachment> requests;context.Write<BehaviorAttachments>().AppendTo(requests);
        std::stable_sort(requests.begin(),requests.end(),[](const auto& a,const auto& b) {
            return std::tie(a.order,a.subject.index,a.subject.generation,a.definition)<std::tie(b.order,b.subject.index,b.subject.generation,b.definition);
        });
        const auto alive=context.Lookup<Lookup>();const auto& library=context.Read<BehaviorPrograms>();
        // Validate the complete batch before publishing metadata or commands.
        std::size_t count{};
        for(const auto& request:requests) if(alive.IsAlive(request.subject)) {
            if(request.definition>=library.programs.size()) throw std::out_of_range("attachment program definition");
            ++count;
        }
        auto& invocations=context.Write<BehaviorInvocations>();
        if(count>0xffffffffull-invocations.arguments.size() || count>(std::numeric_limits<std::uint64_t>::max)()-invocations.next_order)
            throw std::overflow_error("behavior attachment inventory exhausted");
        for(auto& request:requests) if(alive.IsAlive(request.subject)) {
            const auto invocation=std::uint32_t(invocations.arguments.size());
            invocations.arguments.push_back(std::move(request.arguments));
            const auto observer=context.Commands().Create();
            LuaBehavior binding{request.subject,request.authored_subject,invocations.next_order++,request.definition};binding.invocation=invocation;
            context.Commands().Add<LuaBehavior>(observer,binding);context.Commands().Add<LuaBehaviorState>(observer);
        }
        context.Write<BehaviorAttachments>().Reset(0);
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::BehaviorAttachmentSystem> {
    static constexpr std::string_view StableName="engine.gameplay.behavior_attachment";
    static constexpr std::size_t PieceRows=32;
    static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<engine::gameplay::LuaBehaviorSystem>;
};
}
