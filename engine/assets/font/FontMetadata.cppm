export module Assets.Fonts.Metadata;
import std;

export namespace Assets {
// Read family aliases from an SFNT name table without installing the font in
// the operating system. Offsets are bounded before decoding supplied bytes.
std::expected<std::vector<std::string>,std::string> Font_Families(std::span<const std::byte> bytes) {
    const auto fits=[&](std::size_t offset,std::size_t size) { return offset<=bytes.size() && size<=bytes.size()-offset; };
    const auto u16=[&](std::size_t at) {return (std::to_integer<unsigned>(bytes[at])<<8)|std::to_integer<unsigned>(bytes[at+1]);};
    const auto u32=[&](std::size_t at) {return (std::uint32_t(u16(at))<<16)|u16(at+2);};
    if(!fits(0,12) || (u32(0)!=0x00010000 && u32(0)!=0x4f54544f)) return std::unexpected("unsupported or truncated SFNT header");
    const unsigned tables=u16(4);
    if(!fits(12,std::size_t(tables)*16)) return std::unexpected("truncated SFNT directory");
    std::size_t name_offset{},name_size{};
    for(unsigned i=0;i<tables;++i) {
        const auto entry=12+std::size_t(i)*16;
        if(u32(entry)==0x6e616d65) {name_offset=u32(entry+8);name_size=u32(entry+12);break;}
    }
    if(name_size<6 || !fits(name_offset,name_size)) return std::unexpected("missing or truncated SFNT name table");
    const auto table=bytes.subspan(name_offset,name_size);
    const auto format=u16(name_offset),count=u16(name_offset+2),storage=u16(name_offset+4);
    if(format>1 || std::size_t(count)*12>table.size()-6 || storage>table.size()) return std::unexpected("invalid SFNT name table records");
    std::vector<std::string> families;
    const auto utf8=[](std::string& out,unsigned cp) {
        if(cp<0x80) out.push_back(static_cast<char>(cp));
        else if(cp<0x800) {out.push_back(static_cast<char>(0xc0|(cp>>6)));out.push_back(static_cast<char>(0x80|(cp&63)));}
        else if(cp<0x10000) {out.push_back(static_cast<char>(0xe0|(cp>>12)));out.push_back(static_cast<char>(0x80|((cp>>6)&63)));out.push_back(static_cast<char>(0x80|(cp&63)));}
        else {out.push_back(static_cast<char>(0xf0|(cp>>18)));out.push_back(static_cast<char>(0x80|((cp>>12)&63)));out.push_back(static_cast<char>(0x80|((cp>>6)&63)));out.push_back(static_cast<char>(0x80|(cp&63)));}
    };
    for(unsigned i=0;i<count;++i) {
        const auto entry=name_offset+6+std::size_t(i)*12;
        const unsigned platform=u16(entry),encoding=u16(entry+2),id=u16(entry+6),length=u16(entry+8),offset=u16(entry+10);
        if(id!=1 && id!=16) continue;
        const bool unicode=platform==0 || (platform==3 && (encoding==1 || encoding==10));
        const bool ascii_mac=platform==1 && encoding==0;
        if(!unicode && !ascii_mac) continue;
        const auto start=std::size_t(storage)+offset;
        if(start>table.size() || length>table.size()-start || (unicode && length%2)) return std::unexpected("truncated SFNT family string");
        std::string family;
        for(unsigned n=0;n<length;) {
            unsigned cp=unicode ? u16(name_offset+start+n) : std::to_integer<unsigned>(table[start+n]);
            n+=unicode ? 2 : 1;
            // Macintosh family names outside ASCII require a platform encoding
            // decoder; Unicode aliases in the same table remain available.
            if(ascii_mac && cp>=128) {family.clear();break;}
            if(cp>=0xd800 && cp<=0xdbff) {
                if(n+2>length) return std::unexpected("truncated SFNT family surrogate");
                const auto low=u16(name_offset+start+n);n+=2;
                if(low<0xdc00 || low>0xdfff) return std::unexpected("invalid SFNT family surrogate");
                cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);
            } else if(cp>=0xdc00 && cp<=0xdfff) return std::unexpected("unpaired SFNT family surrogate");
            if(cp==0) return std::unexpected("embedded null in SFNT family");
            utf8(family,cp);
        }
        if(!family.empty() && std::ranges::find(families,family)==families.end()) families.push_back(std::move(family));
    }
    if(families.empty()) return std::unexpected("font has no supported family names");
    return families;
}
}
