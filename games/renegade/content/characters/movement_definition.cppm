export module games.renegade.content.characters.movement_definition;
import std;
export import games.renegade.content.levels.definition_catalog;
export import Engine.Core.Math.FixedAngle;

export namespace renegade::content {
// Authoritative parameters decoded at the content boundary. Chunk IDs belong
// to the original game schema and never enter the shared movement mechanisms.
struct HumanMovementDefinition {
    Engine::Math::Fixed speed{Engine::Math::Fixed::FromInt(10)};
    Engine::Math::Fixed slide_angle{Engine::Math::Radians(Engine::Math::TurnFromDegrees(45))};
    Engine::Math::Fixed step_height{Engine::Math::Fixed::FromRatio(1,4)};
    Engine::Math::Fixed mass{Engine::Math::Fixed::One()},gravity_scale{Engine::Math::Fixed::One()};
    Engine::Math::Fixed jump_velocity{Engine::Math::Fixed::FromInt(2)};
    Engine::Math::Fixed turn_rate{Engine::Math::Radians(Engine::Math::TurnFromDegrees(180))*Engine::Math::Fixed::FromInt(2)},skeleton_height{},skeleton_width{};
};
inline std::expected<HumanMovementDefinition,std::string> ReadHumanMovementDefinition(
    const LegacyDefinition& soldier,const LegacyDefinition& physics) {
    using namespace persist;using namespace Engine::Math;
    HumanMovementDefinition result;
    const auto read=[&](Bytes bytes,std::uint32_t parent,std::uint32_t variables,auto&& decode)->std::expected<void,std::string> {
        const auto records=ScopedVariables(bytes,parent,variables);
        if(!records || records->size()!=1) return std::unexpected("missing or duplicate human movement definition scope");
        const auto fields=Micros(records->front().payload);if(!fields) return std::unexpected(fields.error());
        std::array<bool,256> seen{};
        for(const auto& field:*fields) {
            if(std::exchange(seen[field.id],true)) return std::unexpected("duplicate human movement definition field");
            const auto status=decode(field);if(!status) return status;
        }
        return {};
    };
    // wwphys/phys3.cpp Phys3DefClass::Load: NormSpeed, SlideAngle (radians), StepHeight.
    const auto motion=read(physics.data,0x04486000,0x04486001,[&](const Micro& field)->std::expected<void,std::string> {
        if(field.id>2) return {};
        const auto value=Scalar(field.payload);if(!value) return std::unexpected(value.error());
        (field.id==0 ? result.speed : field.id==1 ? result.slide_angle : result.step_height)=*value;return {};
    });if(!motion) return std::unexpected(motion.error());
    // wwphys/movephys.cpp uses the SAME variable ID in its own class scope.
    const auto body=read(physics.data,0x04486002,0x04486001,[&](const Micro& field)->std::expected<void,std::string> {
        if(field.id>1) return {};
        const auto value=Scalar(field.payload);if(!value) return std::unexpected(value.error());
        (field.id==0 ? result.mass : result.gravity_scale)=*value;return {};
    });if(!body) return std::unexpected(body.error());
    // Combat/soldier.cpp SoldierGameObjDef::Load. PhysicalGameObjDef reuses
    // 909991657 below this parent; a global descendant scan is ambiguous.
    const auto person=read(soldier.data,909991656,909991657,[&](const Micro& field)->std::expected<void,std::string> {
        if(field.id<1 || field.id>4) return {};
        const auto value=Scalar(field.payload);if(!value) return std::unexpected(value.error());
        (field.id==1 ? result.turn_rate : field.id==2 ? result.jump_velocity : field.id==3 ? result.skeleton_height : result.skeleton_width)=*value;return {};
    });if(!person) return std::unexpected(person.error());
    // SoldierGameObj::Adjust_Skeleton passes signed interpolation weights to
    // the base/tall/wide hierarchies. They are not physical dimensions.
    if(result.speed<Fixed{} || result.mass<=Fixed{} || result.gravity_scale<Fixed{} || result.jump_velocity<Fixed{} ||
        result.step_height<Fixed{} || result.slide_angle<Fixed{} || result.slide_angle>Radians(TurnFromDegrees(90)) ||
        result.turn_rate<Fixed{})
        return std::unexpected("invalid human movement definition parameters: speed="+std::to_string(result.speed.Raw())+", slide="+std::to_string(result.slide_angle.Raw())+
            ", mass="+std::to_string(result.mass.Raw())+", gravity="+std::to_string(result.gravity_scale.Raw())+", jump="+std::to_string(result.jump_velocity.Raw())+
            ", step="+std::to_string(result.step_height.Raw())+", turn="+std::to_string(result.turn_rate.Raw())+", height="+std::to_string(result.skeleton_height.Raw())+", width="+std::to_string(result.skeleton_width.Raw()));
    return result;
}
}
