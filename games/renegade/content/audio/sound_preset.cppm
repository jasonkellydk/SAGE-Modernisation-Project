export module games.renegade.content.audio.sound_preset;
import std;
export import games.renegade.content.levels.definition_catalog;
export namespace renegade::content {
struct SoundPreset {
    std::uint32_t id{},type{1},loops{1},virtual_channel{};
    std::string filename;
    Engine::Math::Fixed volume{Engine::Math::Fixed::One()},pitch{Engine::Math::Fixed::One()},priority{Engine::Math::Fixed::FromRatio(1,2)};
    Engine::Math::Fixed minimum_range{},maximum_range{},start_offset{},pitch_variation{},volume_variation{};
    bool positional{};
};
inline std::expected<SoundPreset,std::string> ReadSoundPreset(const LegacyDefinition& definition) {
    using namespace persist;using Engine::Math::Fixed;
    if(definition.factory!=0x30000) return std::unexpected("sound reference is not an audible-sound definition");
    const auto variables=One(definition.data,0x100);if(!variables) return std::unexpected(variables.error());
    const auto fields=Micros(variables->payload);if(!fields) return std::unexpected(fields.error());
    SoundPreset result;result.id=definition.id;std::set<unsigned> seen;
    for(const auto& field:*fields) {
        if(!seen.insert(field.id).second) return std::unexpected("duplicate sound preset variable");
        if(field.id==11) {const auto value=Text(field.payload);if(!value) return std::unexpected(value.error());result.filename=*value;}
        if(field.id==6 || field.id==9 || field.id==22) {const auto value=U32(field.payload);if(!value) return std::unexpected(value.error());(field.id==6 ? result.loops : field.id==9 ? result.type : result.virtual_channel)=*value;}
        if(field.id==10) {const auto value=Flag(field.payload);if(!value) return std::unexpected(value.error());result.positional=*value;}
        if(field.id==3 || field.id==4 || field.id==7 || field.id==8 || field.id==18 || field.id==19 || field.id==20 || field.id==21) {
            const auto value=Scalar(field.payload);if(!value) return std::unexpected(value.error());
            (field.id==3 ? result.priority : field.id==4 ? result.volume : field.id==7 ? result.maximum_range : field.id==8 ? result.minimum_range :
                field.id==18 ? result.start_offset : field.id==19 ? result.pitch : field.id==20 ? result.pitch_variation : result.volume_variation)=*value;
        }
    }
    // AudibleSoundDefinitionClass::Create_Sound strips relative Windows
    // directories. Resolution thereafter uses the mounted retail filesystem.
    if(result.filename.size()>2 && result.filename[1]!=':') {
        const auto delimiter=result.filename.find_last_of('\\');if(delimiter!=result.filename.npos) result.filename.erase(0,delimiter+1);
    }
    if(result.filename.empty() || result.type>3 || result.volume<Fixed{} || result.pitch<=Fixed{} || result.minimum_range<Fixed{} ||
        result.maximum_range<result.minimum_range || result.pitch_variation<Fixed{} || result.volume_variation<Fixed{} || result.start_offset<Fixed{})
        return std::unexpected("invalid audible-sound definition values");
    return result;
}
}
