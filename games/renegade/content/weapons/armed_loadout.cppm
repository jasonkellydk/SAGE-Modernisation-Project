export module games.renegade.content.weapons.armed_loadout;
import std;
export import games.renegade.content.weapons.weapon_catalog;
export namespace renegade::content {
struct ArmedLoadout {std::uint32_t primary{},secondary{};std::int32_t rounds{-1};};
inline std::expected<std::optional<ArmedLoadout>,std::string> ReadArmedLoadout(const LegacyDefinition& definition) {
    using namespace persist;ArmedLoadout result;const auto scopes=Descendants(definition.data,418001830);
    if(!scopes || scopes->size()>1) return std::unexpected("invalid armed definition scope");if(scopes->empty()) return std::nullopt;
    const auto fields=Micros(scopes->front().payload);if(!fields) return std::unexpected(fields.error());std::array<bool,256> seen{};
    for(const auto& field:*fields) {
        if(std::exchange(seen[field.id],true)) return std::unexpected("duplicate armed definition field");
        if(field.id!=11 && field.id!=12 && field.id!=14) continue;
        const auto value=U32(field.payload);if(!value) return std::unexpected(value.error());
        if(field.id==12) result.rounds=std::bit_cast<std::int32_t>(*value);else (field.id==11 ? result.primary : result.secondary)=*value;
    }
    return result;
}
struct SavedWeapon {
    std::uint32_t definition{},phase{},shots{},exists{1},safety{};
    std::int32_t loaded{},reserve{},burst_count{};
    Engine::Math::Fixed remaining{},burst_timer{};
};
struct SavedLoadout {std::vector<SavedWeapon> weapons;std::uint32_t selected{};};
inline std::expected<std::optional<SavedLoadout>,std::string> ReadSavedLoadout(persist::Bytes data) {
    using namespace persist;SavedLoadout result;const auto scopes=Descendants(data,418001843);
    if(!scopes || scopes->size()>1) return std::unexpected("invalid saved weapon bag");if(scopes->empty()) return std::nullopt;
    const auto variables=One(scopes->front().payload,921991503),list=One(scopes->front().payload,921991504);
    if(!variables || !list) return std::unexpected("missing saved weapon bag variables/list");
    const auto fields=Micros(variables->payload);if(!fields) return std::unexpected(fields.error());
    bool selected{};for(const auto& field:*fields) if(field.id==1) {if(std::exchange(selected,true)) return std::unexpected("duplicate selected weapon");const auto index=U32(field.payload);if(!index) return std::unexpected(index.error());result.selected=*index;}
    const auto entries=Children(list->payload);if(!entries) return std::unexpected(entries.error());
    for(const auto& entry:*entries) {
        if(entry.id!=921991505) return std::unexpected("invalid saved weapon entry");const auto values=One(entry.payload,910991544);if(!values) return std::unexpected(values.error());
        const auto micros=Micros(values->payload);if(!micros) return std::unexpected(micros.error());SavedWeapon weapon;std::array<bool,256> seen{};
        for(const auto& field:*micros) {
            if(std::exchange(seen[field.id],true)) return std::unexpected("duplicate saved weapon variable");
            if(field.id==30 || field.id==31) {const auto value=Flag(field.payload);if(!value) return std::unexpected(value.error());(field.id==30 ? weapon.exists : weapon.safety)=*value;}
            else if(field.id==3 || field.id==20) {const auto value=Scalar(field.payload);if(!value) return std::unexpected(value.error());(field.id==3 ? weapon.remaining : weapon.burst_timer)=*value;}
            else if(field.id==2 || field.id==6 || field.id==18 || field.id==19 || field.id==21 || field.id==26) {
                const auto value=U32(field.payload);if(!value) return std::unexpected(value.error());
                switch(field.id) {case 2:weapon.phase=*value;break;case 6:weapon.shots=*value;break;case 18:weapon.loaded=std::bit_cast<std::int32_t>(*value);break;case 19:weapon.reserve=std::bit_cast<std::int32_t>(*value);break;case 21:weapon.burst_count=std::bit_cast<std::int32_t>(*value);break;case 26:weapon.definition=*value;break;}
            }
        }
        if(!weapon.definition || weapon.phase>7 || weapon.remaining<Engine::Math::Fixed{}) return std::unexpected("invalid saved weapon identity/state");
        result.weapons.push_back(weapon);
    }
    if(result.selected>result.weapons.size()) return std::unexpected("invalid saved selected weapon index");return result;
}
}
