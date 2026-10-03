export module games.renegade.content.levels.transition_definitions;
import std;
export import games.renegade.content.levels.persist_records;

export namespace renegade::content {
enum class TransitionStyle : std::uint32_t {
    LadderExitTop, LadderExitBottom, LadderEnterTop, LadderEnterBottom,
    LegacyVehicleEnter0, LegacyVehicleEnter1, LegacyVehicleExit0, LegacyVehicleExit1,
    VehicleEnter, VehicleExit
};
struct TransitionDefinition {
    TransitionStyle style{};
    Engine::Math::FixedOrientedBox3 bounds;
    Engine::Math::FixedAffineTransform3 destination;
    std::string animation;
};
inline std::expected<std::vector<TransitionDefinition>,std::string> ReadTransitionDefinitions(persist::Bytes bytes) {
    using namespace persist;
    // Combat/transitiongameobj.cpp: ordered definition records. Stay inside
    // this schema; unrelated persistence subsystems need different envelopes.
    const auto records=Children(bytes);if(!records) return std::unexpected(records.error());
    std::vector<TransitionDefinition> result;
    for(const auto& record:*records) if(record.id==1111991202) {
        const auto variables=One(record.payload,0x11051106);if(!variables) return std::unexpected(variables.error());
        const auto fields=Micros(variables->payload);if(!fields) return std::unexpected(fields.error());
        TransitionDefinition definition;std::set<unsigned> seen;
        for(const auto& field:*fields) if(field.id>=1 && field.id<=4) {
            if(!seen.insert(field.id).second) return std::unexpected("duplicate transition definition field");
            if(field.id==1) {
                const auto type=U32(field.payload);if(!type || *type>9) return std::unexpected("invalid transition style");
                definition.style=static_cast<TransitionStyle>(*type==4 || *type==5 ? 8 : *type==6 || *type==7 ? 9 : *type);
            } else if(field.id==2) {
                const auto box=OrientedBox(field.payload);if(!box) return std::unexpected(box.error());definition.bounds=*box;
            } else if(field.id==3) {
                const auto name=Text(field.payload);if(!name) return std::unexpected(name.error());definition.animation=*name;
            } else {
                const auto pose=Matrix(field.payload);if(!pose) return std::unexpected(pose.error());definition.destination=*pose;
            }
        }
        if(seen.size()!=4) return std::unexpected("incomplete transition definition");
        result.push_back(std::move(definition));
    }
    return result;
}
}
