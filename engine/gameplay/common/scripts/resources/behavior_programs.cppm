export module engine.gameplay.common.scripts.resources.behavior_programs;
import std;
export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.common.scripts.components.scheduled_message;
export import engine.scripting.lua.lua_program;

export namespace engine::gameplay {
struct BehaviorProgram {
    std::string name, source;
    std::string startup_event;
    std::vector<engine::scripting::lua::Value> startup_arguments;
};
struct BehaviorPrograms {
    std::vector<BehaviorProgram> programs;
    std::vector<std::string> event_names;
};
struct BehaviorInbox { std::vector<BehaviorMessage> messages; };
// Program sources are immutable after preparation. Attachments reference
// them and place invocation arguments in a stable append-only pool. A saved
// world must be resumed with its corresponding program and invocation data.
struct BehaviorInvocations {
    std::vector<std::vector<engine::scripting::lua::Value>> arguments;
    std::uint64_t next_order{};
};
struct BehaviorAttachment {
    ecs::Entity subject;
    std::uint64_t authored_subject{}, order{};
    std::uint32_t definition{};
    std::vector<engine::scripting::lua::Value> arguments;
};
using BehaviorAttachments=ecs::ChunkOutputs<BehaviorAttachment>;
using DueBehaviorMessages=ecs::ChunkOutputs<BehaviorMessage>;
struct BehaviorEmission {
    ecs::Entity observer, subject;
    std::uint64_t order{}, tick{};
    engine::scripting::lua::Command command;
};
using BehaviorEmissions=ecs::ChunkOutputs<BehaviorEmission>;
}
export namespace ecs {
template<> struct ResourceTraits<engine::gameplay::BehaviorPrograms> {
    static constexpr std::string_view StableName="engine.gameplay.behavior_programs";
};
template<> struct ResourceTraits<engine::gameplay::BehaviorInbox> {
    static constexpr std::string_view StableName="engine.gameplay.behavior_inbox";
};
template<> struct ResourceTraits<engine::gameplay::BehaviorInvocations> {
    static constexpr std::string_view StableName="engine.gameplay.behavior_invocations";
};
template<> struct ResourceTraits<engine::gameplay::BehaviorAttachments> {
    static constexpr std::string_view StableName="engine.gameplay.behavior_attachments";
};
template<> struct ResourceTraits<engine::gameplay::DueBehaviorMessages> {
    static constexpr std::string_view StableName="engine.gameplay.due_behavior_messages";
};
template<> struct ResourceTraits<engine::gameplay::BehaviorEmissions> {
    static constexpr std::string_view StableName="engine.gameplay.behavior_emissions";
};
}
