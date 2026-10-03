export module games.renegade.content.characters.actor_animation;
import std;
export import games.renegade.content.levels.dynamic_level;
export namespace renegade::content {
struct ActorAnimation {
    std::string clip;
    Engine::Math::Fixed frame{},target{};
    std::uint32_t mode{1},hold_style{7};
};
inline std::expected<std::optional<ActorAnimation>,std::string> ReadActorAnimation(const ActorPlacement& actor) {
    using namespace persist;
    if(actor.factory!=0x4010e) return std::nullopt;
    ActorAnimation result;
    const auto controls=Descendants(actor.data,910991153);
    if(!controls || controls->size()>1) return std::unexpected("invalid actor animation control");
    if(!controls->empty()) {
        const auto channel=One(controls->front().payload,910991519);
        if(!channel) return std::unexpected(channel.error());
        const auto current=One(channel->payload,910991517);
        if(!current) return std::unexpected(current.error());
        const auto variables=One(current->payload,910991515);
        if(!variables) return std::unexpected(variables.error());
        const auto fields=Micros(variables->payload);if(!fields) return std::unexpected(fields.error());
        std::array<bool,256> seen{};
        for(const auto& field:*fields) {
            if(std::exchange(seen[field.id],true)) return std::unexpected("duplicate actor animation field");
            if(field.id==6) {auto value=Text(field.payload);if(!value) return std::unexpected(value.error());result.clip=std::move(*value);}
            if(field.id==3 || field.id==9) {const auto value=Scalar(field.payload);if(!value) return std::unexpected(value.error());(field.id==3 ? result.frame : result.target)=*value;}
            if(field.id==5) {const auto value=U32(field.payload);if(!value || *value>3) return std::unexpected("invalid animation mode");result.mode=*value;}
        }
    }
    const auto states=Descendants(actor.data,915991207);
    if(!states || states->size()>1) return std::unexpected("invalid human animation state");
    if(!states->empty()) {
        const auto fields=Micros(states->front().payload);if(!fields) return std::unexpected(fields.error());
        for(const auto& field:*fields) if(field.id==4) {const auto value=U32(field.payload);if(!value || *value>=10) return std::unexpected("invalid weapon hold style");result.hold_style=*value;}
    }
    if(result.clip.empty()) {result.frame={};result.mode=1;}
    return result;
}
// humanstate.cpp UPRIGHT standing leg and weapon-style animation names;
// animcontrol.cpp selects the hierarchy's skeleton variant when available.
inline std::string StandingAnimation(char skeleton,std::uint32_t hold_style) {
    constexpr std::array styles{"A0","A0","C2","D2","E2","F2","A0","A0","B0","A0"};
    if(hold_style>=styles.size()) throw std::invalid_argument("invalid weapon hold style");
    return std::string("S_")+skeleton+"_HUMAN.H_"+skeleton+"_"+styles[hold_style]+"A0";
}
inline std::array<std::string,21> LocomotionAnimations(char skeleton,std::uint32_t hold_style) {
    constexpr std::array legs{"A0","A1","A2","A3","A4","B1","B2","B3","B4","J0","J1","J2","J3","J4","L0","L1","L2","L3","L4","A5","A6"};
    constexpr std::array torso{"A0","A0","C2","D2","E2","F2","A0","A0","B0","A0"};
    if(hold_style>=torso.size()) throw std::invalid_argument("invalid weapon hold style");
    std::array<std::string,21> result;
    for(std::size_t i=0;i<result.size();++i) result[i]=std::string("S_")+skeleton+"_HUMAN.H_"+skeleton+"_"+(i>=14 && i<=18 ? "A0" : torso[hold_style])+legs[i];
    return result;
}
inline std::array<std::string,28> HumanAnimations(char skeleton,std::uint32_t hold_style) {
    const auto locomotion=LocomotionAnimations(skeleton,hold_style);std::array<std::string,28> result;
    std::copy(locomotion.begin(),locomotion.end(),result.begin());
    constexpr std::array ladder{"412A","422A","432A"};
    for(unsigned i=0;i<ladder.size();++i) result[21+i]=std::string("S_")+skeleton+"_HUMAN.H_"+skeleton+"_"+ladder[i];
    // TransitionDataClass::StyleType order: exit top/bottom, enter top/bottom.
    constexpr std::array transitions{"4262","4243","4263","4242"};
    for(unsigned i=0;i<transitions.size();++i) result[24+i]=std::string("S_")+skeleton+"_HUMAN.H_"+skeleton+"_"+transitions[i];
    return result;
}
}
