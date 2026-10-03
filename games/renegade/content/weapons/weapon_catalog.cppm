export module games.renegade.content.weapons.weapon_catalog;
import std;
export import games.renegade.content.levels.definition_catalog;

export namespace renegade::content {
using Engine::Math::Fixed;
// Immutable game content; entities contain indices and firing state only.
// Combat/weaponmanager.cpp owns these schemas and their original defaults.
struct AmmoPreset {
    std::uint32_t id{},warhead{},fire_sound{},continuous_sound{},explosion{},type{},spray_count{1},bullet_cost{1};
    std::int32_t burst_max{};
    std::string name,model,trail,continuous_emitter;
    Fixed damage{Fixed::One()},range{Fixed::FromInt(10)},velocity{Fixed::One()},gravity{},rate{Fixed::One()},charge{},spray_angle{},burst_delay{};
};
struct WeaponPreset {
    std::uint32_t id{},style{1},primary{},secondary{},reload_sound{},empty_sound{},icon_text{};
    std::int32_t clip_size{},maximum_reserve{100};
    Fixed key_number{},switch_time{},reload_time{},recoil_time{Fixed::FromRatio(1,10)},recoil_scale{Fixed::One()};
    std::string name,model,first_person_model,idle_animation,fire_animation,human_fire_animation,icon_texture;
    Engine::Math::FixedVector3 first_person_offset;
    std::array<Fixed,4> icon_uv{};std::array<Fixed,2> icon_offset{};
};
struct WeaponCatalog {
    std::map<std::uint32_t,WeaponPreset> weapons;
    std::map<std::uint32_t,AmmoPreset> ammunition;
    const WeaponPreset* Find(std::uint32_t id) const noexcept {const auto entry=weapons.find(id);return entry==weapons.end() ? nullptr : &entry->second;}
    const WeaponPreset* Find(std::string_view name) const noexcept {
        for(const auto& [id,weapon]:weapons) if(weapon.name==name) return &weapon;return nullptr;
    }
};
inline std::expected<WeaponCatalog,std::string> ReadWeaponCatalog(const DefinitionCatalog& definitions) {
    using namespace persist;WeaponCatalog result;
    for(const auto& [id,source]:definitions.by_id) {
        const auto weapon_scopes=Descendants(source.data,1205091654),ammo_scopes=Descendants(source.data,1206091429);
        if(!weapon_scopes || !ammo_scopes) return std::unexpected("invalid weapon/ammunition definition envelope");
        if(weapon_scopes->size()>1 || ammo_scopes->size()>1 || !weapon_scopes->empty() && !ammo_scopes->empty()) return std::unexpected("ambiguous weapon/ammunition schema");
        if(weapon_scopes->empty() && ammo_scopes->empty()) continue;
        const bool is_weapon=!weapon_scopes->empty();const auto fields=Micros((is_weapon ? weapon_scopes : ammo_scopes)->front().payload);
        if(!fields) return std::unexpected(fields.error());std::array<bool,256> seen{};
        WeaponPreset weapon;weapon.id=id;weapon.name=source.name;AmmoPreset ammo;ammo.id=id;ammo.name=source.name;
        for(const auto& field:*fields) {
            if(std::exchange(seen[field.id],true)) return std::unexpected("duplicate weapon/ammunition field: "+source.name);
            auto integer=[&](auto& target)->std::expected<void,std::string> {const auto value=U32(field.payload);if(!value) return std::unexpected(value.error());target=static_cast<std::remove_reference_t<decltype(target)>>(*value);return {};};
            auto scalar=[&](Fixed& target)->std::expected<void,std::string> {const auto value=Scalar(field.payload);if(!value) return std::unexpected(value.error());target=*value;return {};};
            auto text=[&](std::string& target)->std::expected<void,std::string> {const auto value=Text(field.payload);if(!value) return std::unexpected(value.error());target=*value;return {};};
            std::expected<void,std::string> status;
            if(is_weapon) {
                switch(field.id) {
                case 1:status=integer(weapon.style);break;case 2:status=text(weapon.model);break;
                case 3:status=text(weapon.idle_animation);break;case 4:status=text(weapon.fire_animation);break;
                case 8:status=scalar(weapon.switch_time);break;case 9:status=scalar(weapon.reload_time);break;
                case 22:status=text(weapon.first_person_model);break;
                case 23:{const auto value=Vector(field.payload);if(!value) status=std::unexpected(value.error());else weapon.first_person_offset=*value;break;}
                case 26:status=integer(weapon.reload_sound);break;case 27:status=integer(weapon.primary);break;case 28:status=integer(weapon.secondary);break;
                case 29:status=integer(weapon.clip_size);break;case 30:status=scalar(weapon.recoil_time);break;case 31:status=scalar(weapon.recoil_scale);break;
                case 35:status=integer(weapon.maximum_reserve);break;case 37:status=scalar(weapon.key_number);break;
                case 38:status=integer(weapon.icon_text);break;case 39:status=text(weapon.icon_texture);break;
                case 40:case 41:{
                    const auto size=field.id==40 ? 4u : 2u;if(field.payload.size()!=size*4) {status=std::unexpected("invalid weapon icon coordinates");break;}
                    for(unsigned i=0;i<size;++i) {const auto value=Scalar(field.payload.subspan(i*4,4));if(!value) {status=std::unexpected(value.error());break;}(field.id==40 ? weapon.icon_uv[i] : weapon.icon_offset[i])=*value;}break;
                }
                case 42:status=text(weapon.human_fire_animation);break;case 43:status=integer(weapon.empty_sound);break;
                default:break;
                }
            } else {
                switch(field.id) {
                case 2:status=text(ammo.model);break;case 3:status=integer(ammo.warhead);break;case 4:status=scalar(ammo.damage);break;
                case 5:status=scalar(ammo.range);break;case 6:status=scalar(ammo.velocity);break;case 7:status=scalar(ammo.gravity);break;
                case 10:status=scalar(ammo.spray_angle);break;case 11:status=integer(ammo.spray_count);break;case 12:status=text(ammo.trail);break;
                case 14:status=scalar(ammo.burst_delay);break;case 15:status=integer(ammo.burst_max);break;case 24:status=integer(ammo.explosion);break;
                case 27:status=scalar(ammo.charge);break;case 28:status=integer(ammo.continuous_sound);break;case 29:status=text(ammo.continuous_emitter);break;
                case 31:status=integer(ammo.bullet_cost);break;case 32:status=integer(ammo.type);break;case 48:status=scalar(ammo.rate);break;case 62:status=integer(ammo.fire_sound);break;
                default:break;
                }
            }
            if(!status) return std::unexpected(source.name+": "+status.error());
        }
        if(is_weapon) {
            // AI weapons use a -1 magazine. Preserve signed original fields;
            // an equipped actor validates its applicable runtime capabilities.
            result.weapons.emplace(id,std::move(weapon));
        } else {
            // Retain editor/test presets too, including zero spray and signed
            // velocities. Runtime availability is a separate game decision.
            if(ammo.type>3) return std::unexpected("invalid ammunition type: "+source.name);
            result.ammunition.emplace(id,std::move(ammo));
        }
    }
    for(const auto& [id,weapon]:result.weapons) for(const auto ammo:{weapon.primary,weapon.secondary}) if(ammo && !result.ammunition.contains(ammo)) return std::unexpected("weapon references missing ammunition: "+weapon.name);
    return result;
}
}
