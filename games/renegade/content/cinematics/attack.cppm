export module games.renegade.content.cinematics.attack;
import std;
export import games.renegade.content.cinematics.cinematic;
import engine.scripting.lua.lua_program;
import Engine.Core.Math.FixedVector;
export namespace renegade::content {
struct CinematicAttack {Engine::Math::FixedVector3 offset;Engine::Math::Fixed duration;std::uint32_t priority{},reset_priority{},action{},force{};};
inline std::expected<CinematicAttack,std::string> ReadCinematicAttack(std::string_view lua,std::string_view parameters) {
    using namespace engine::scripting::lua;using Engine::Math::Fixed;
    const auto duration=parameters.empty() ? std::expected<Fixed,std::string>{Fixed::One()} : CinematicDetail::Number(parameters);
    if(!duration || *duration<Fixed{}) return std::unexpected("invalid cinematic attack duration");
    Program program;const auto loaded=program.Load(lua,"cinematic attack");if(!loaded) return std::unexpected(loaded.error());
    const std::array<Value,7> arguments{std::int64_t{1},std::int64_t{0},std::int64_t{0},std::int64_t{0},std::int64_t{Fixed::OneRaw},std::int64_t{0},duration->Raw()};
    const auto commands=program.Invoke("Created",arguments);if(!commands || commands->size()!=2) return std::unexpected("unsupported cinematic attack Created commands");
    const auto& attack=commands->front();const auto& custom=commands->back();
    if(attack.name!="Action_Attack" || attack.arguments.size()!=10 || custom.name!="Send_Custom_Event" || custom.arguments.size()!=5) return std::unexpected("invalid cinematic attack command schema");
    for(const auto& command:*commands) for(const auto& argument:command.arguments) if(!std::holds_alternative<std::int64_t>(argument)) return std::unexpected("cinematic attack command needs fixed integer values");
    const auto value=[&](unsigned index) {return std::get<std::int64_t>(attack.arguments[index]);};
    const auto custom_duration=std::get<std::int64_t>(custom.arguments[4]);if(custom_duration<0) return std::unexpected("negative cinematic custom delay");
    const std::array<Value,4> event{std::int64_t{1},custom.arguments[2],custom.arguments[3],std::int64_t{1}};const auto reset=program.Invoke("Custom",event);
    if(!reset || reset->size()!=1 || reset->front().name!="Action_Reset" || reset->front().arguments.size()!=2 || !std::holds_alternative<std::int64_t>(reset->front().arguments[1])) return std::unexpected("unsupported cinematic attack Custom commands");
    return CinematicAttack{{Fixed::FromRaw(value(3)),Fixed::FromRaw(value(4)),Fixed::FromRaw(value(5))},Fixed::FromRaw(custom_duration),std::uint32_t(value(1)),std::uint32_t(std::get<std::int64_t>(reset->front().arguments[1])),std::uint32_t(value(2)),std::uint32_t(value(9))};
}
}
