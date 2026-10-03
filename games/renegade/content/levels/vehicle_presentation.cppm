export module games.renegade.content.levels.vehicle_presentation;
import std;
export import games.renegade.content.levels.definition_catalog;
export namespace renegade::content {
struct VehiclePresentation {Engine::Math::Fixed spring_length;};
inline std::expected<std::optional<VehiclePresentation>,std::string> ReadVehiclePresentation(const LegacyDefinition& physics) {
    // wwphys/vehiclephys.cpp VehiclePhysDefClass variables: SpringLength=2.
    const auto records=persist::Descendants(physics.data,405001520);
    if(!records || records->size()>1) return std::unexpected("invalid vehicle physics presentation scope");
    if(records->empty()) return std::nullopt;
    const auto fields=persist::Micros(records->front().payload);if(!fields) return std::unexpected(fields.error());
    std::optional<VehiclePresentation> result;
    for(const auto& field:*fields) if(field.id==2) {
        if(result) return std::unexpected("duplicate vehicle spring length");
        const auto value=persist::Scalar(field.payload);if(!value || *value<=Engine::Math::Fixed{}) return std::unexpected("invalid vehicle spring length");
        result=VehiclePresentation{*value};
    }
    if(!result) return std::unexpected("missing vehicle spring length");return result;
}
}
