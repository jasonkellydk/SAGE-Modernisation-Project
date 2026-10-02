export module engine.filesystem.adapters.pe.resources;
import std;

export namespace engine::filesystem::pe
{
// Reads resource bytes without loading or executing the PE image. Offsets are
// checked against raw sections, including images with a nonzero resource RVA.
std::expected<std::span<const std::byte>, std::string> Resource(
    std::span<const std::byte> image, std::uint32_t type, std::uint32_t name,
    std::optional<std::uint32_t> language = {})
{
    const auto fits = [&](std::size_t p, std::size_t n) { return p <= image.size() && n <= image.size() - p; };
    const auto u16 = [&](std::size_t p) { return std::to_integer<unsigned>(image[p]) | (std::to_integer<unsigned>(image[p + 1]) << 8); };
    const auto u32 = [&](std::size_t p) { return std::uint32_t(u16(p)) | (std::uint32_t(u16(p + 2)) << 16); };
    if (!fits(0, 64) || u16(0) != 0x5a4d) return std::unexpected("missing DOS header");
    const auto pe = u32(60);
    if (!fits(pe, 24) || u32(pe) != 0x4550) return std::unexpected("missing PE header");
    const auto optional = std::size_t(pe) + 24;
    const auto optional_size = u16(pe + 20);
    if (!fits(optional, optional_size) || optional_size < 2) return std::unexpected("truncated optional header");
    const auto magic = u16(optional);
    const auto directory = optional + (magic == 0x10b ? 96 : 112);
    if ((magic != 0x10b && magic != 0x20b) || directory + 24 > optional + optional_size)
        return std::unexpected("missing resource data directory");
    const auto sections = optional + optional_size;
    const auto count = u16(pe + 6);
    if (!fits(sections, std::size_t(count) * 40)) return std::unexpected("truncated section table");
    const auto raw = [&](std::uint32_t rva, std::uint32_t length) -> std::optional<std::size_t> {
        for (unsigned i = 0; i < count; ++i) {
            const auto section = sections + i * 40;
            const auto base = u32(section + 12), size = u32(section + 16), offset = u32(section + 20);
            if (rva >= base && rva - base <= size && length <= size - (rva - base)) {
                const auto p = std::size_t(offset) + rva - base;
                if (fits(p, length)) return p;
            }
        }
        return {};
    };
    const auto size = u32(directory + 20);
    const auto start = raw(u32(directory + 16), size);
    if (!start || size < 16) return std::unexpected("resource section is absent or truncated");
    const auto in_resource = [&](std::uint32_t p, std::size_t n) { return p <= size && n <= size - p; };
    std::uint32_t table = 0;
    const std::array<std::optional<std::uint32_t>, 3> keys{type, name, language};
    for (unsigned depth = 0; depth < keys.size(); ++depth) {
        if (!in_resource(table, 16)) return std::unexpected("invalid resource directory");
        const auto entries = u16(*start + table + 12) + u16(*start + table + 14);
        if (!in_resource(table + 16, std::size_t(entries) * 8)) return std::unexpected("truncated resource entries");
        std::optional<std::uint32_t> next;
        for (unsigned i = 0; i < entries; ++i) {
            const auto p = *start + table + 16 + i * 8;
            const auto id = u32(p);
            if (!(id & 0x80000000u) && (!keys[depth] || id == *keys[depth])) { next = u32(p + 4); break; }
        }
        if (!next) return std::unexpected("resource identifier not found");
        if (depth < 2) {
            if (!(*next & 0x80000000u)) return std::unexpected("resource directory expected");
            table = *next & 0x7fffffffu;
        } else {
            if ((*next & 0x80000000u) || !in_resource(*next, 16)) return std::unexpected("resource data entry expected");
            const auto length = u32(*start + *next + 4);
            const auto data = raw(u32(*start + *next), length);
            if (!data) return std::unexpected("resource data is outside raw sections");
            return image.subspan(*data, length);
        }
    }
    return std::unexpected("invalid resource tree");
}
}
