export module games.renegade.content.cinematics.cinematic;
import std;
export import engine.level.model.timeline;
export namespace renegade::content {
enum class CinematicOpcode {CreateModel,CreatePreset,Destroy,Animation,Camera,Attach,Shadow,Shake,Letterbox,FadeColor,FadeOpacity,Custom,Script,Explosion,Audio,Primary};
struct CinematicAction {
    CinematicOpcode opcode{};
    std::int32_t slot{-1},host{-1};
    std::string asset,bone,parameters;
    std::array<Engine::Math::Fixed,4> values{};
    bool looping{},blended{};
    std::uint32_t source_line{};
};
struct CinematicDefinition {engine::level::Timeline timeline;std::vector<CinematicAction> actions;};
namespace CinematicDetail {
inline std::string_view Trim(std::string_view text) {while(!text.empty() && static_cast<unsigned char>(text.front())<=32) text.remove_prefix(1);while(!text.empty() && static_cast<unsigned char>(text.back())<=32) text.remove_suffix(1);return text;}
inline std::expected<std::vector<std::string>,std::string> Parameters(std::string_view text) {
    std::vector<std::string> result;std::string value;bool quoted{};
    for(char c:text) {
        if(c=='"') quoted=!quoted;
        else if(c==',' && !quoted) {result.emplace_back(Trim(value));value.clear();}
        else value+=c;
    }
    if(quoted) return std::unexpected("unterminated cinematic quote");
    result.emplace_back(Trim(value));return result;
}
inline std::expected<Engine::Math::Fixed,std::string> Number(std::string_view text) {
    const auto number=Engine::Math::Fixed::ParseDecimal(text);
    if(!number) return std::unexpected("invalid or out-of-range cinematic number");
    return *number;
}
}
// Test_Cinematic::Load_Control_File uses negative 30fps frame numbers,
// positive seconds, stable ordering at equal times, and quoted comma fields.
// Parsing is confined to the content boundary; gameplay receives typed data.
inline std::expected<CinematicDefinition,std::string> ReadCinematic(std::string_view text) {
    using Engine::Math::Fixed;using namespace CinematicDetail;
    CinematicDefinition result;unsigned line_number{};
    constexpr std::array names{"create_object","create_real_object","destroy_object","play_animation","control_camera","attach_to_bone","enable_shadow","shake_camera","enable_letterbox","set_screen_fade_color","set_screen_fade_opacity","send_custom","attach_script","create_explosion","play_audio","set_primary"};
    while(!text.empty()) {
        ++line_number;const auto newline=text.find('\n');auto line=Trim(text.substr(0,newline));text=newline==text.npos ? std::string_view{} : text.substr(newline+1);
        if(line.empty() || line.front()==';') continue;
        const auto whitespace=line.find_first_of(" \t\r");if(whitespace==line.npos) return std::unexpected("cinematic line lacks command");
        auto time=Number(line.substr(0,whitespace));if(!time) return std::unexpected(time.error());if(*time<Fixed{}) *time=-*time/Fixed::FromInt(30);
        auto fields=Parameters(Trim(line.substr(whitespace)));if(!fields) return std::unexpected(fields.error());
        auto command=fields->front();for(char& c:command) if(c>='A' && c<='Z') c+=32;
        const auto found=std::ranges::find(names,command);if(found==names.end()) return std::unexpected("unsupported cinematic command: "+command);
        CinematicAction action;action.opcode=static_cast<CinematicOpcode>(found-names.begin());action.source_line=line_number;
        const auto arg=[&](unsigned i)->std::string_view {return i<fields->size() ? (*fields)[i] : std::string_view{};};
        const auto integer=[&](unsigned i,std::int32_t& output) {const auto s=arg(i);const auto p=std::from_chars(s.data(),s.data()+s.size(),output);return !s.empty() && p.ec==std::errc{} && p.ptr==s.data()+s.size();};
        const auto number=[&](unsigned i,Fixed& output) {const auto value=Number(arg(i));if(!value) return false;output=*value;return true;};
        bool valid=true;
        switch(action.opcode) {
        case CinematicOpcode::CreateModel:valid=integer(1,action.slot) && !arg(2).empty();action.asset=arg(2);break;
        case CinematicOpcode::CreatePreset:valid=integer(1,action.slot) && !arg(2).empty();action.asset=arg(2);if(!arg(3).empty()) valid&=integer(3,action.host);action.bone=arg(4);break;
        case CinematicOpcode::Destroy:case CinematicOpcode::Camera:case CinematicOpcode::Primary:valid=integer(1,action.slot);break;
        case CinematicOpcode::Animation: {
            std::int32_t loop{},blend{};valid=integer(1,action.slot) && !arg(2).empty() && integer(3,loop);action.asset=arg(2);action.bone=arg(4);action.looping=loop!=0;
            if(!arg(5).empty()) valid&=integer(5,blend);action.blended=blend==1;break;
        }
        case CinematicOpcode::Attach:valid=integer(1,action.slot) && integer(2,action.host);action.bone=arg(3);break;
        case CinematicOpcode::Script:valid=integer(1,action.slot) && !arg(2).empty();action.asset=arg(2);action.parameters=arg(3);break;
        case CinematicOpcode::Shadow:valid=integer(1,action.slot) && number(2,action.values[0]);break;
        case CinematicOpcode::Shake:valid=integer(1,action.slot) && number(2,action.values[0]) && number(3,action.values[1]);break;
        case CinematicOpcode::Letterbox:case CinematicOpcode::FadeOpacity:valid=number(1,action.values[0]) && number(2,action.values[1]);break;
        case CinematicOpcode::FadeColor:for(unsigned c=0;c<4;++c) valid&=number(c+1,action.values[c]);break;
        case CinematicOpcode::Custom:valid=integer(1,action.slot) && number(2,action.values[0]) && number(3,action.values[1]);break;
        case CinematicOpcode::Explosion:case CinematicOpcode::Audio:action.asset=arg(1);valid=!action.asset.empty();if(!arg(2).empty()) valid&=integer(2,action.host);action.bone=arg(3);break;
        }
        if(!valid) return std::unexpected("invalid cinematic arguments at line "+std::to_string(line_number));
        result.timeline.cues.push_back({*time,static_cast<std::uint32_t>(result.actions.size())});result.actions.push_back(std::move(action));
    }
    std::stable_sort(result.timeline.cues.begin(),result.timeline.cues.end(),[](const auto& a,const auto& b) {return a.time<b.time;});
    return result;
}
}
