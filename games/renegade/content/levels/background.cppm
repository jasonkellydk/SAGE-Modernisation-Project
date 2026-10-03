export module games.renegade.content.levels.background;
import std;
export import games.renegade.content.levels.persist_records;
import Engine.Core.Math.FixedPresentation;
export namespace renegade::content {
struct BackgroundDefinition {
    std::uint32_t hours{15},minutes{},light_source{},moon{};
    Engine::Math::Fixed cloud_cover{},gloominess{},tint{};
};
inline std::expected<BackgroundDefinition,std::string> ReadBackground(persist::Bytes bytes) {
    using namespace persist;const auto manager=One(bytes,0x40126);if(!manager) return std::unexpected(manager.error());
    const auto saved=One(manager->payload,0x11080732),weather=One(manager->payload,0x11020216);
    if(!saved || !weather) return std::unexpected("missing background state");
    BackgroundDefinition result;
    const auto read=[&](const Chunk& record,bool dynamic)->std::expected<void,std::string> {
        const auto fields=Micros(record.payload);if(!fields) return std::unexpected(fields.error());std::set<unsigned> seen;
        for(const auto& field:*fields) {
            if(!seen.insert(field.id).second) return std::unexpected("duplicate background variable");
            if(!dynamic && field.id>=0x14 && field.id<=0x17) {
                const auto value=U32(field.payload);if(!value) return std::unexpected(value.error());
                switch(field.id) {case 0x14:result.hours=*value;break;case 0x15:result.minutes=*value;break;case 0x16:result.light_source=*value;break;case 0x17:result.moon=*value;break;}
            } else if(dynamic && (field.id==0x18 || field.id==0x1e || field.id==0x24)) {
                const auto value=Scalar(field.payload);if(!value) return std::unexpected(value.error());
                (field.id==0x18 ? result.cloud_cover : field.id==0x1e ? result.gloominess : result.tint)=*value;
            }
        }
        return {};
    };
    if(const auto status=read(*saved,false);!status) return std::unexpected(status.error());
    if(const auto status=read(*weather,true);!status) return std::unexpected(status.error());
    using Engine::Math::Fixed;
    if(result.hours>23 || result.minutes>59 || result.light_source>1 || result.moon>1 || result.cloud_cover<Fixed{} || result.cloud_cover>Fixed::FromInt(1) ||
        result.gloominess<Fixed{} || result.gloominess>Fixed::FromInt(1) || result.tint<Fixed{} || result.tint>Fixed::FromInt(1)) return std::unexpected("invalid background value");
    return result;
}
}
