export module games.renegade.content.cinematics.startup;
import std;
import engine.scripting.lua.lua_program;
import engine.filesystem.core.virtual_file_system;
import engine.level.model.level;
export namespace renegade::content {
struct CinematicStartup {
    std::uint64_t subject{};std::string program,control_file,music;
    std::uint32_t music_fade_ms{};std::array<std::int64_t,8> state{};bool clear_weapon{};
};
// The original attached Created callbacks are discovered from the level,
// never selected by a map name. Loose Lua overrides use ordinary VFS order.
inline std::expected<std::optional<CinematicStartup>,std::string> LoadCinematicStartup(
    const engine::filesystem::VirtualFileSystem& files,const engine::level::Level& level,std::int64_t star) {
    using namespace engine::scripting::lua;std::optional<CinematicStartup> result;
    for(const auto& binding:level.behaviors) {
        const auto source=files.ReadText(binding.program+".lua");if(!source) continue;
        Program program;const auto loaded=program.Load(*source,binding.program);if(!loaded) return std::unexpected(loaded.error());
        const auto startup_kinds=program.StringList("Startups");if(!startup_kinds) return std::unexpected(startup_kinds.error());
        if(std::ranges::find(*startup_kinds,"Cinematic")==startup_kinds->end()) continue;
        const std::array<Value,2> args{std::int64_t(binding.subject),star};const auto commands=program.Invoke("Created",args);if(!commands) return std::unexpected(commands.error());
        CinematicStartup candidate;candidate.subject=binding.subject;candidate.program=binding.program;
        const auto integer=[](const Command& c,unsigned i)->const std::int64_t* {return i<c.arguments.size() ? std::get_if<std::int64_t>(&c.arguments[i]) : nullptr;};
        const auto string=[](const Command& c,unsigned i)->const std::string* {return i<c.arguments.size() ? std::get_if<std::string>(&c.arguments[i]) : nullptr;};
        for(const auto& command:*commands) {
            if(command.name=="Select_Weapon" && command.arguments.size()==2 && integer(command,0) && *integer(command,0)==star && string(command,1) && string(command,1)->empty()) candidate.clear_weapon=true;
            else if(command.name=="Initialize_State" && command.arguments.size()==9 && integer(command,0) && *integer(command,0)==std::int64_t(binding.subject)) {
                for(unsigned i=0;i<8;++i) {const auto value=integer(command,i+1);if(!value) return std::unexpected("invalid mission initialization state");candidate.state[i]=*value;}
            } else if(command.name=="Fade_Background_Music" && command.arguments.size()==3 && string(command,0) && integer(command,1) && integer(command,2) && *integer(command,1)==0 && *integer(command,2)>=0 && *integer(command,2)<=60000) {
                candidate.music=*string(command,0);candidate.music_fade_ms=std::uint32_t(*integer(command,2));
            } else if(command.name=="Start_Cinematic" && command.arguments.size()==6 && integer(command,0) && *integer(command,0)==std::int64_t(binding.subject) && string(command,1) && string(command,2)) {
                // Created supplies the source origin. Further commands that
                // select a different transform need their typed adapter.
                for(unsigned i=3;i<6;++i) if(!integer(command,i) || *integer(command,i)!=0) return std::unexpected("cinematic startup transform adapter is incomplete");
                candidate.control_file=*string(command,2);
            } else return std::unexpected("unsupported Created effect in "+binding.program+": "+command.name);
        }
        if(candidate.control_file.empty()) continue;
        if(result) return std::unexpected("multiple startup cinematics need explicit composition");result=std::move(candidate);
    }
    return result;
}
}
