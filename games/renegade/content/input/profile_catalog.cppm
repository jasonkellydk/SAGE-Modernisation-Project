export module games.renegade.content.input.profile_catalog;
import std;
import engine.core.serialization.byte_stream;
import engine.config.adapters.ini.section_reader;

export namespace renegade::content {
struct InputProfile {
    std::u16string name;std::string filename;bool default_profile{},custom{};
    bool operator==(const InputProfile&) const=default;
};
struct InputProfileCatalog {
    std::vector<InputProfile> profiles;std::string current;
    std::optional<std::size_t> Find(std::string_view filename) const {
        for(std::size_t i=0;i<profiles.size();++i) if(engine::config::ini::FoldAscii(profiles[i].filename)==engine::config::ini::FoldAscii(filename)) return i;
        return {};
    }
    std::optional<std::size_t> Default() const {
        for(std::size_t i=0;i<profiles.size();++i) if(profiles[i].default_profile) return i;return {};
    }
    void EnsureDefault(std::u16string name) {
        // Source adds the retail default only when the entire dictionary is empty.
        if(profiles.empty()) {profiles.push_back({std::move(name),"DEFAULT_INPUT.CFG",true,false});current=profiles.front().filename;}
        if(!Find(current)) if(const auto index=Default()) current=profiles[*index].filename;
    }
    bool operator==(const InputProfileCatalog&) const=default;
};
bool ValidProfileFilename(std::string_view filename) {
    return !filename.empty() && filename.size()<=255 && filename!="." && filename!=".." &&
        filename.find_first_of("/\\:\0",0,4)==filename.npos;
}
bool ValidProfileName(std::u16string_view name) {
    if(name.empty() || name.size()>65535) return false;
    for(std::size_t i=0;i<name.size();++i) {
        const auto c=name[i];if(c<32 || c==127) return false;
        if(c>=0xd800 && c<=0xdbff) {if(++i==name.size() || name[i]<0xdc00 || name[i]>0xdfff) return false;}
        else if(c>=0xdc00 && c<=0xdfff) return false;
    }
    return true;
}
bool ValidInputProfileCatalog(const InputProfileCatalog& catalog) {
    if(catalog.profiles.size()>4096 || (!catalog.current.empty() && !ValidProfileFilename(catalog.current))) return false;
    std::set<std::string> filenames;
    for(const auto& profile:catalog.profiles) if(!ValidProfileFilename(profile.filename) || !ValidProfileName(profile.name) ||
        !filenames.insert(engine::config::ini::FoldAscii(profile.filename)).second) return false;
    return true;
}
std::expected<std::string,std::string> UniqueInputProfileFilename(const InputProfileCatalog& catalog,
    const std::function<bool(std::string_view)>& occupied) {
    for(unsigned slot=1;slot<=1000000;++slot) {
        const auto filename="input"+(slot<10 ? std::string("0") : std::string{})+std::to_string(slot)+".cfg";
        if(!catalog.Find(filename) && (!occupied || !occupied(filename))) return filename;
    }
    return std::unexpected("no free input profile filename");
}
// Portable catalog version 1 retains the source's names, flags, list order and
// current filename, without the legacy 8-bit microchunk string-length overflow.
std::expected<std::vector<std::byte>,std::string> WriteInputProfileCatalog(const InputProfileCatalog& catalog) {
    if(!ValidInputProfileCatalog(catalog)) return std::unexpected("invalid input profile catalog");
    engine::core::serialization::ByteWriter writer;writer.U32(0x50434952);writer.U32(1);
    writer.U32(static_cast<std::uint32_t>(catalog.profiles.size()));writer.Text(catalog.current);
    for(const auto& profile:catalog.profiles) {
        writer.U32(static_cast<std::uint32_t>(profile.name.size()));
        for(const auto c:profile.name) {writer.U8(static_cast<std::uint8_t>(c));writer.U8(static_cast<std::uint8_t>(c>>8));}
        writer.Text(profile.filename);writer.Flag(profile.default_profile);writer.Flag(profile.custom);
    }
    return writer.Take();
}
std::expected<InputProfileCatalog,std::string> ReadInputProfileCatalog(std::span<const std::byte> bytes) {
    if(bytes.size()>8*1024*1024) return std::unexpected("input profile catalog exceeds eight MiB");
    engine::core::serialization::ByteReader reader(bytes);
    const auto magic=reader.U32(),version=reader.U32(),count=reader.U32();
    if(magic!=0x50434952 || version!=1 || !count || *count>4096) return std::unexpected("invalid input profile catalog header");
    const auto current=reader.Text();if(!current) return std::unexpected("truncated input profile catalog");
    InputProfileCatalog result;result.current=*current;
    for(unsigned i=0;i<*count;++i) {
        const auto size=reader.U32();if(!size || *size>65535) return std::unexpected("invalid input profile name length");
        std::u16string name;name.reserve(*size);
        for(unsigned j=0;j<*size;++j) {const auto lo=reader.U8(),hi=reader.U8();if(!lo || !hi) return std::unexpected("truncated input profile name");name.push_back(char16_t(*lo|(*hi<<8)));}
        const auto filename=reader.Text();const auto default_profile=reader.U8(),custom=reader.U8();
        if(!filename || !default_profile || !custom || *default_profile>1 || *custom>1) return std::unexpected("invalid input profile fields");
        result.profiles.push_back({std::move(name),*filename,*default_profile!=0,*custom!=0});
    }
    if(!reader.AtEnd() || !ValidInputProfileCatalog(result)) return std::unexpected("invalid input profile catalog data");
    return result;
}
namespace profile_detail {
template<class Callback> bool Records(std::span<const std::byte> bytes,bool micro,Callback callback) {
    std::size_t offset{};const std::size_t header=micro ? 2 : 8;
    while(offset<bytes.size()) {
        if(bytes.size()-offset<header) return false;
        engine::core::serialization::ByteReader reader(bytes.subspan(offset,header));
        const std::uint32_t id=micro ? *reader.U8() : *reader.U32();
        const std::uint32_t size=(micro ? std::uint32_t(*reader.U8()) : *reader.U32())&0x7fffffff;
        if(size>bytes.size()-offset-header || !callback(id,bytes.subspan(offset+header,size))) return false;
        offset+=header+size;
    }
    return true;
}
std::optional<std::string> CString(std::span<const std::byte> bytes) {
    if(bytes.empty() || bytes.back()!=std::byte{}) return {};
    const std::string value(reinterpret_cast<const char*>(bytes.data()),bytes.size()-1);
    return value.find('\0')==value.npos ? std::optional(value) : std::nullopt;
}
std::optional<std::u16string> WideString(std::span<const std::byte> bytes) {
    if(bytes.size()<2 || bytes.size()%2 || bytes[bytes.size()-1]!=std::byte{} || bytes[bytes.size()-2]!=std::byte{}) return {};
    std::u16string value;for(std::size_t i=0;i+2<bytes.size();i+=2)
        value.push_back(char16_t(std::to_integer<unsigned>(bytes[i])|(std::to_integer<unsigned>(bytes[i+1])<<8)));
    return ValidProfileName(value) ? std::optional(value) : std::nullopt;
}
}
// Commando/inputconfigmgr.cpp and inputconfig.cpp; CONFIG.DAT remains importable
// from retail/mod files. Repeated VARIABLES ids are interpreted by nesting.
std::expected<InputProfileCatalog,std::string> ReadLegacyInputProfileCatalog(std::span<const std::byte> bytes) {
    if(bytes.size()>8*1024*1024) return std::unexpected("legacy input profile catalog exceeds eight MiB");
    InputProfileCatalog result;
    const bool valid=profile_detail::Records(bytes,false,[&](auto id,auto payload) {
        if(id==0x07181105) return profile_detail::Records(payload,false,[&](auto object_id,auto object) {
            if(object_id!=0x07181107) return true;
            if(result.profiles.size()>=4096) return false;
            InputProfile profile;
            const bool parsed=profile_detail::Records(object,false,[&](auto variable_id,auto variables) {
                if(variable_id!=0x07181106) return true;
                return profile_detail::Records(variables,true,[&](auto field,auto value) {
                    switch(field) {
                    case 1: {const auto text=profile_detail::WideString(value);if(!text) return false;profile.name=*text;break;}
                    case 2: {const auto text=profile_detail::CString(value);if(!text) return false;profile.filename=*text;break;}
                    case 3:case 4:if(value.size()!=1) return false;(field==3 ? profile.default_profile : profile.custom)=value[0]!=std::byte{};break;
                    default:break;
                    }
                    return true;
                });
            });
            if(parsed) result.profiles.push_back(std::move(profile));return parsed;
        });
        if(id==0x07181106) return profile_detail::Records(payload,true,[&](auto field,auto value) {
            if(field==1) {const auto text=profile_detail::CString(value);if(!text) return false;result.current=*text;}return true;
        });
        return true;
    });
    if(!valid || !ValidInputProfileCatalog(result)) return std::unexpected("invalid legacy input profile catalog");
    return result;
}
}
