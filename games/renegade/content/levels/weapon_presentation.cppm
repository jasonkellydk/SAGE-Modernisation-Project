export module games.renegade.content.levels.weapon_presentation;
import std;
export import games.renegade.content.levels.definition_catalog;
export namespace renegade::content {
struct WeaponPresentation {
    std::uint32_t definition{},style{},ammo{},clip_size{},sound{};
    // coltype.h: projectile rays use bit 1; physical hulls use bit 0.
    std::uint32_t collision_categories{2};
    std::string model,fire_animation,projectile,trail;
    Engine::Math::Fixed rate{},charge{},reload{},velocity{},range{},recoil_time{},recoil_scale{};
};
inline std::expected<std::optional<WeaponPresentation>,std::string> ReadEquippedWeapon(const DefinitionCatalog& definitions,const LegacyDefinition& actor) {
    using namespace persist;WeaponPresentation result;
    // ArmedGameObjDef::Load, not a global scan of reused microchunk IDs.
    const auto scopes=Descendants(actor.data,418001830);if(!scopes || scopes->size()>1) return std::unexpected("invalid armed-object definition scope");if(scopes->empty()) return std::nullopt;
    const auto fields=Micros(scopes->front().payload);if(!fields) return std::unexpected(fields.error());
    for(const auto& field:*fields) if(field.id==11) {const auto id=U32(field.payload);if(!id) return std::unexpected(id.error());result.definition=*id;}
    if(!result.definition) return std::nullopt;const auto* weapon=definitions.Find(result.definition);if(!weapon) return std::unexpected("equipped weapon definition is missing");
    const auto variables=Descendants(weapon->data,1205091654);if(!variables || variables->size()!=1) return std::unexpected("invalid weapon definition scope");
    const auto values=Micros(variables->front().payload);if(!values) return std::unexpected(values.error());std::array<bool,256> seen{};
    for(const auto& field:*values) {
        if(std::exchange(seen[field.id],true)) return std::unexpected("duplicate weapon definition field");
        if(field.id==1 || field.id==27 || field.id==29) {const auto value=U32(field.payload);if(!value) return std::unexpected(value.error());(field.id==1 ? result.style : field.id==27 ? result.ammo : result.clip_size)=*value;}
        if(field.id==2 || field.id==4) {const auto value=Text(field.payload);if(!value) return std::unexpected(value.error());(field.id==2 ? result.model : result.fire_animation)=*value;}
        if(field.id==9 || field.id==30 || field.id==31) {const auto value=Scalar(field.payload);if(!value) return std::unexpected(value.error());(field.id==9 ? result.reload : field.id==30 ? result.recoil_time : result.recoil_scale)=*value;}
    }
    if(!result.ammo) return std::unexpected("equipped weapon has no primary ammo");const auto* ammo=definitions.Find(result.ammo);if(!ammo) return std::unexpected("primary ammo definition is missing");
    const auto ammo_scopes=Descendants(ammo->data,1206091429);if(!ammo_scopes || ammo_scopes->size()!=1) return std::unexpected("invalid ammo definition scope");
    const auto ammo_values=Micros(ammo_scopes->front().payload);if(!ammo_values) return std::unexpected(ammo_values.error());seen.fill(false);
    for(const auto& field:*ammo_values) {
        if(std::exchange(seen[field.id],true)) return std::unexpected("duplicate ammo definition field");
        if(field.id==2 || field.id==12) {const auto value=Text(field.payload);if(!value) return std::unexpected(value.error());(field.id==2 ? result.projectile : result.trail)=*value;}
        if(field.id==5 || field.id==6 || field.id==27 || field.id==48) {const auto value=Scalar(field.payload);if(!value) return std::unexpected(value.error());(field.id==5 ? result.range : field.id==6 ? result.velocity : field.id==27 ? result.charge : result.rate)=*value;}
        // FireSoundDefID follows GrenadeSafetyTime at the end of the schema;
        // it is decoded by the audio content adapter when shot playback is bound.
    }
    if(result.style>=10 || result.rate<Engine::Math::Fixed{} || result.charge<Engine::Math::Fixed{} || result.reload<Engine::Math::Fixed{}) return std::unexpected("invalid weapon presentation parameters");
    return result;
}
}
