export module games.renegade.content.levels.sounds;
import std;
export import games.renegade.content.levels.persist_records;
export namespace renegade::content {
struct LevelSound {
    std::uint32_t id{},state{},type{1},loops{};
    std::string filename;Engine::Math::FixedVector3 position;
    Engine::Math::Fixed volume{Engine::Math::Fixed::One()},pitch{Engine::Math::Fixed::One()},minimum_range{},maximum_range{};
    bool positional{};
};
inline std::expected<std::vector<LevelSound>,std::string> ReadLevelSounds(persist::Bytes bytes) {
    using namespace persist;const auto manager=One(bytes,0x30005);if(!manager) return std::unexpected(manager.error());
    const auto scene=One(manager->payload,0x10291220);if(!scene) return std::unexpected(scene.error());
    const auto saved=One(scene->payload,0x101);if(!saved) return std::unexpected(saved.error());
    const auto records=Children(saved->payload);if(!records) return std::unexpected(records.error());
    std::vector<LevelSound> result;std::set<std::uint32_t> ids;
    for(const auto& record:*records) {
        if(record.id!=0x30001 && record.id!=0x30003 && record.id!=0x30004) return std::unexpected("unsupported saved sound factory");
        const auto object=ObjectData(record.payload);if(!object) return std::unexpected(object.error());
        LevelSound sound;sound.positional=record.id!=0x30001;auto audible=object->payload;
        if(sound.positional) {const auto base=One(audible,0x11090956);if(!base) return std::unexpected(base.error());audible=base->payload;}
        const auto identity=Descendants(audible,0x03270459);const auto vars=One(audible,0x100);
        if(!identity || identity->size()!=1 || !vars) return std::unexpected("invalid saved sound state");
        const auto identity_fields=Micros(identity->front().payload),fields=Micros(vars->payload);
        if(!identity_fields || !fields) return std::unexpected("invalid sound variables");
        for(const auto& field:*identity_fields) if(field.id==5) {const auto id=U32(field.payload);if(!id) return std::unexpected(id.error());sound.id=*id;}
        std::set<unsigned> seen;
        for(const auto& field:*fields) {
            if(!seen.insert(field.id).second) return std::unexpected("duplicate sound variable");
            if(field.id==1 || field.id==2 || field.id==6) {const auto value=U32(field.payload);if(!value) return std::unexpected(value.error());(field.id==1 ? sound.state : field.id==2 ? sound.type : sound.loops)=*value;}
            if(field.id==4 || field.id==14 || field.id==19) {const auto value=Scalar(field.payload);if(!value) return std::unexpected(value.error());(field.id==4 ? sound.volume : field.id==14 ? sound.maximum_range : sound.pitch)=*value;}
            if(field.id==10) {const auto pose=Matrix(field.payload);if(!pose) return std::unexpected(pose.error());sound.position={pose->elements[3],pose->elements[7],pose->elements[11]};}
            if(field.id==15) {const auto text=Text(field.payload);if(!text) return std::unexpected(text.error());sound.filename=*text;}
        }
        if(sound.positional) {
            const auto spatial=One(object->payload,0x11090955);if(!spatial) return std::unexpected(spatial.error());const auto spatial_fields=Micros(spatial->payload);if(!spatial_fields) return std::unexpected(spatial_fields.error());
            for(const auto& field:*spatial_fields) if(field.id==5) {const auto value=Scalar(field.payload);if(!value) return std::unexpected(value.error());sound.minimum_range=*value;}
        }
        using Engine::Math::Fixed;
        if(!sound.id || !ids.insert(sound.id).second || sound.state>2 || sound.type>3 || sound.filename.empty() || sound.volume<Fixed{} || sound.pitch<=Fixed{} ||
            sound.minimum_range<Fixed{} || sound.maximum_range<sound.minimum_range) return std::unexpected("invalid saved sound values");
        result.push_back(std::move(sound));
    }
    return result;
}
}
