export module games.renegade.content.presentation.hud_settings;
import std;
export import games.renegade.content.levels.definition_catalog;

export namespace renegade::content {
using Engine::Math::FixedVector3;using Engine::Math::Fixed;
struct HudSettings {
    FixedVector3 health_high{{},Fixed::One(),{}},health_medium{Fixed::One(),Fixed::One(),{}},health_low{Fixed::One(),{},{}},
        enemy{Fixed::One(),{},{}},friendly{{},Fixed::One(),{}},neutral{Fixed::One(),Fixed::One(),Fixed::One()},primary_objective{{},Fixed::One(),{}};
};
// GlobalSettings.cpp reuses the human-loiter variable chunk number for HUD.
// Its owning factory, rather than a global microchunk scan, identifies colors.
inline std::expected<HudSettings,std::string> ReadHudSettings(const DefinitionCatalog& definitions) {
    using namespace persist;HudSettings result;bool found{};
    for(const auto& [id,definition]:definitions.by_id) {
        if(definition.factory!=0x40603) continue;
        if(std::exchange(found,true)) return std::unexpected("duplicate global HUD settings");
        const auto scopes=ScopedVariables(definition.data,803001812,803001813);
        if(!scopes || scopes->size()!=1) return std::unexpected("missing HUD settings variables");
        const auto fields=Micros(scopes->front().payload);if(!fields) return std::unexpected(fields.error());std::array<bool,256> seen{};
        for(const auto& field:*fields) {
            if(std::exchange(seen[field.id],true)) return std::unexpected("duplicate HUD settings variable");
            FixedVector3* target{};
            switch(field.id) {
            case 4:target=&result.primary_objective;break;
            case 89:target=&result.health_high;break;case 90:target=&result.health_medium;break;case 91:target=&result.health_low;break;
            case 92:target=&result.enemy;break;case 93:target=&result.friendly;break;case 94:target=&result.neutral;break;
            default:break;
            }
            if(!target) continue;const auto value=Vector(field.payload);if(!value) return std::unexpected(value.error());*target=*value;
        }
    }
    if(!found) return std::unexpected("global HUD settings are missing");return result;
}
}
