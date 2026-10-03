export module games.renegade.content.presentation.strings;
import std;
import Assets.Adapters.W3D.Chunks;

export namespace renegade::content
{
struct Translation { std::string descriptor; std::vector<std::u16string> languages; std::uint32_t sound{0xffffffff};std::string animation,english; };
struct StringCatalog {
    std::map<std::uint32_t,Translation> entries;
    std::u16string Lookup(std::uint32_t id,std::size_t language=0) const {
        const auto found=entries.find(id);if(found==entries.end() || found->second.languages.empty()) return {};
        const auto& languages=found->second.languages;return languages[language<languages.size() ? language : 0];
    }
    std::u16string Lookup(std::string_view descriptor,std::size_t language=0) const {
        for(const auto& [id,entry]:entries) if(entry.descriptor==descriptor && !entry.languages.empty())
            return entry.languages[language<entry.languages.size() ? language : 0];
        return {};
    }
};
// wwtranslatedb/translateobj.cpp and translatedbids.h. Persist pointers are
// skipped; identifiers and strings are content, never instantiated C++ objects.
std::expected<StringCatalog,std::string> ReadStrings(std::span<const std::byte> bytes) {
    using namespace Assets::W3D;
    if(!W3DValidate_Chunk_Tree(bytes)) return std::unexpected("malformed translation chunks");
    StringCatalog catalog;
    std::function<bool(std::span<const std::byte>)> visit;
    visit=[&](std::span<const std::byte> chunks) {
        return W3DVisit_Chunks(chunks,[&](const W3DChunkView& chunk) {
            if(chunk.id!=0x90001) return !chunk.contains_children || visit(chunk.payload);
            std::uint32_t id{}; Translation entry;
            std::function<bool(std::span<const std::byte>)> object;
            object=[&](std::span<const std::byte> fields) {
                return W3DVisit_Chunks(fields,[&](const W3DChunkView& field) {
                    if(field.id==0x06141108) {
                        std::size_t p=0;
                        while(p<field.payload.size()) {
                            if(field.payload.size()-p<2) return false;
                            const auto variable=std::to_integer<unsigned>(field.payload[p++]);
                            const auto size=std::to_integer<unsigned>(field.payload[p++]);
                            if(size>field.payload.size()-p) return false;
                            const auto data=field.payload.subspan(p,size);p+=size;
                            if(variable==1 && !W3DRead_U32(data,0,id)) return false;
                            if(variable==5 && !W3DRead_U32(data,0,entry.sound)) return false;
                            if(variable==2) entry.descriptor=W3DRead_String(data);
                            if(variable==6) entry.animation=W3DRead_String(data);
                        }
                    } else if(field.id==0x0614110a) {
                        entry.english=W3DRead_String(field.payload);
                    } else if(field.id==0x0614110b) {
                        if(field.payload.size()%2) return false;
                        std::u16string text;
                        for(std::size_t p=0;p<field.payload.size();p+=2) {
                            const auto c=std::to_integer<unsigned>(field.payload[p])|(std::to_integer<unsigned>(field.payload[p+1])<<8);
                            if(!c) break;
                            text+=static_cast<char16_t>(c);
                        }
                        entry.languages.push_back(std::move(text));
                    } else if(field.contains_children && !object(field.payload)) return false;
                    return true;
                });
            };
            if(!object(chunk.payload) || id==0 || entry.descriptor.empty() || entry.languages.empty() || catalog.entries.contains(id)) return false;
            catalog.entries.emplace(id,std::move(entry));return true;
        });
    };
    if(!visit(bytes) || catalog.entries.empty()) return std::unexpected("incomplete translation database");
    return catalog;
}
}
