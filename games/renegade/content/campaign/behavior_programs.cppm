export module games.renegade.content.campaign.behavior_programs;
import std;
export import engine.gameplay.common.scripts.resources.behavior_programs;
export import engine.level.model.level;
import engine.filesystem.core.virtual_file_system;
import Engine.Core.Math.FixedRandom;

export namespace renegade::content {
enum class MissionEvent : std::uint32_t { TimerExpired,Custom,Entered,Exited,ActionComplete };
struct PreparedBehaviorBinding {
    std::uint64_t subject{},order{};std::uint32_t definition{};
    std::optional<std::vector<engine::scripting::lua::Value>> arguments;
};
struct PreparedBehaviors {
    engine::gameplay::BehaviorPrograms library;
    std::vector<PreparedBehaviorBinding> bindings;
    std::vector<std::string> unported;
    std::vector<std::string> conversations;
    std::vector<std::string> optional_conversations;
    std::vector<std::string> animations;
    std::vector<std::string> objective_textures;
};
inline std::expected<PreparedBehaviors,std::string> LoadBehaviorPrograms(const engine::filesystem::VirtualFileSystem& files,
    const engine::level::Level& level,std::uint64_t seed=0,std::string_view start_script={},
    std::span<const engine::level::BehaviorBinding> timeline_scripts={}) {
    PreparedBehaviors result;result.library.event_names={"Timer_Expired","Custom","Entered","Exited","Action_Complete"};
    std::map<std::string,std::optional<std::uint32_t>,std::less<>> definitions;
    std::set<std::string,std::less<>> loading;
    const auto load=[&](auto&& recurse,const std::string& name)->std::expected<std::optional<std::uint32_t>,std::string> {
        if(name.empty() || std::ranges::any_of(name,[](char c) {return !((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_');}))
            return std::unexpected("invalid mission script identity: "+name);
        if(loading.contains(name)) return std::unexpected("cyclic mission script dependency: "+name);
        if(const auto found=definitions.find(name);found!=definitions.end()) return found->second;
        const auto text=files.ReadText(name+".lua");
        if(!text) {definitions.emplace(name,std::nullopt);result.unported.push_back(name);return std::optional<std::uint32_t>{};}
        if(definitions.size()>=4096) return std::unexpected("mission script inventory exceeds preparation limit");
        engine::scripting::lua::Program validation;const auto loaded=validation.Load(*text,name);
        if(!loaded) return std::unexpected(name+": "+loaded.error());
        const auto dependencies=validation.StringList("Scripts");if(!dependencies) return std::unexpected(name+": "+dependencies.error());
        const auto collect=[&](std::string_view field,std::vector<std::string>& output)->std::expected<void,std::string> {
            const auto names=validation.StringList(field);if(!names) return std::unexpected(name+": "+names.error());
            for(const auto& entry:*names) if(std::ranges::find(output,entry)==output.end()) output.push_back(entry);
            return {};
        };
        if(const auto status=collect("Conversations",result.conversations);!status) return std::unexpected(status.error());
        if(const auto status=collect("Animations",result.animations);!status) return std::unexpected(status.error());
        if(const auto status=collect("ObjectiveTextures",result.objective_textures);!status) return std::unexpected(status.error());
        const auto index=std::uint32_t(result.library.programs.size());
        result.library.programs.push_back({name,*text,"Created",{}});definitions.emplace(name,index);loading.insert(name);
        for(const auto& dependency:*dependencies) {
            const auto child=recurse(recurse,dependency);if(!child) return std::unexpected(child.error());
            if(!*child) return std::unexpected(name+": required script is not prepared: "+dependency);
        }
        loading.erase(name);return std::optional<std::uint32_t>{index};
    };
    // Timeline observers are prepared now, then activated by shared ECS cues.
    // Their Created callbacks must never run during level preparation.
    for(const auto& binding:timeline_scripts) {
        const auto definition=load(load,binding.program);if(!definition) return std::unexpected(definition.error());
        if(!*definition) continue;
        engine::scripting::lua::Program program;
        const auto loaded=program.Load(result.library.programs[**definition].source,binding.program);
        if(!loaded) return std::unexpected(loaded.error());
        const auto parameters=program.StringList("ConversationParameters");if(!parameters) return std::unexpected(parameters.error());
        if(!parameters->empty()) {
            if(parameters->size()!=1 || binding.parameters.empty() || binding.parameters.find(',')!=std::string::npos)
                return std::unexpected("invalid single conversation parameter: "+binding.program);
            if(std::ranges::find(result.conversations,binding.parameters)==result.conversations.end()) result.conversations.push_back(binding.parameters);
            if(std::ranges::find(result.optional_conversations,binding.parameters)==result.optional_conversations.end()) result.optional_conversations.push_back(binding.parameters);
        }
    }
    std::uint64_t order{};
    for(const auto& binding:level.behaviors) {
        // These cinematic callbacks already run through the authored timeline
        // adapter. Do not invoke their Created callbacks a second time here.
        if(binding.program=="MX0_MissionStart_DME" || binding.program=="M00_Cinematic_Attack_Command_DLS") {++order;continue;}
        const auto definition=load(load,binding.program);if(!definition) return std::unexpected(definition.error());
        if(!*definition) {++order;continue;}
        auto random=Engine::Math::Stream(seed,{binding.id,binding.subject});
        const auto flyover=10+Engine::Math::UniformInt(random,0,20);
        result.bindings.push_back({binding.subject,order++,**definition,
            std::vector<engine::scripting::lua::Value>{std::int64_t(binding.subject),binding.parameters,std::int64_t(flyover)}});
    }
    // GOD_STATE_SINGLE_INIT attaches the saved CombatManager start script
    // after creating the player. Created may attach additional observers.
    if(!start_script.empty()) {
        const auto definition=load(load,std::string(start_script));if(!definition) return std::unexpected(definition.error());
        if(*definition) result.bindings.push_back({1,order,**definition,
            std::vector<engine::scripting::lua::Value>{std::int64_t{1},std::string{}}});
    }
    return result;
}
}
