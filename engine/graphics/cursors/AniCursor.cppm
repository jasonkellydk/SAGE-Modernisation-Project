export module Graphics.Cursors.Ani;
import std;
export import Graphics.Cursors.Cursor;

// Windows animated cursors (.ANI: a RIFF "ACON" of "anih", optional "rate" and "seq ", and "icon" chunks, each a
// .CUR: an ICONDIR, one CURDIRENTRY with the hotspot, a DIB and its AND mask), as the original's WinCursors (Win32
// LoadCursorFromFile over Data/Cursors/<Texture>.ANI) shows them: frames in "seq " order when there is one, all at the
// size of the first of them (nearest), each shown for its "rate" (1/60 s jiffies) or, without one, 1000 / FPS ms
// (50 ms without an FPS); the hotspot of the first frame in the file (else the given one).
namespace Graphics
{
export struct AniCursorImage final
{
    std::vector<Assets::PreparedImage> frames; // RGBA8, straight alpha
    std::vector<std::uint32_t> durations_ms;
    int hotspot_x = 0, hotspot_y = 0;
};

namespace AniDetail
{
inline std::uint16_t Le16(std::span<const std::byte> data, std::size_t offset)
{
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(data[offset]) | (std::to_integer<unsigned>(data[offset + 1]) << 8));
}

inline std::uint32_t Le32(std::span<const std::byte> data, std::size_t offset)
{
    return std::to_integer<std::uint32_t>(data[offset]) | (std::to_integer<std::uint32_t>(data[offset + 1]) << 8)
        | (std::to_integer<std::uint32_t>(data[offset + 2]) << 16) | (std::to_integer<std::uint32_t>(data[offset + 3]) << 24);
}

inline bool Has(std::size_t offset, std::size_t count, std::size_t size) { return offset <= size && count <= size - offset; }

inline bool Is(std::span<const std::byte> data, std::size_t offset, std::string_view name)
{
    for (std::size_t index = 0; index < 4; ++index)
        if (std::to_integer<char>(data[offset + index]) != name[index]) return false;
    return true;
}

struct Parsed
{
    std::vector<Assets::PreparedImage> frames;
    std::vector<std::uint32_t> rates, sequence;
    int hot_x = -1, hot_y = -1;
};

// One .CUR image as straight RGBA8 (its AND mask clears alpha; 32-bit art keeps its own alpha when it has any).
inline std::optional<Assets::PreparedImage> Icon(std::span<const std::byte> icon, int& hot_x, int& hot_y)
{
    if (!Has(0, 22, icon.size()) || Le16(icon, 0) != 0 || Le16(icon, 2) != 2 || Le16(icon, 4) == 0) return std::nullopt;
    const std::uint32_t image_size = Le32(icon, 6 + 8), image_offset = Le32(icon, 6 + 12);
    if (image_size == 0 || !Has(image_offset, image_size, icon.size())) return std::nullopt;
    const auto image = icon.subspan(image_offset, image_size);
    if (!Has(0, 4, image.size())) return std::nullopt;
    const std::uint32_t header = Le32(image, 0);
    int width = 0, height = 0;
    std::uint16_t bits = 0;
    std::uint32_t compression = 0, colors_used = 0;
    bool top_down = false;
    std::size_t entry_size = 4;
    if (header == 12)
    {
        if (!Has(0, 12, image.size())) return std::nullopt;
        width = Le16(image, 4);
        height = Le16(image, 6) / 2;
        bits = Le16(image, 10);
        entry_size = 3;
    }
    else
    {
        if (header < 40 || !Has(0, header, image.size())) return std::nullopt;
        const auto dib_width = static_cast<std::int32_t>(Le32(image, 4)), dib_height = static_cast<std::int32_t>(Le32(image, 8));
        if (dib_width <= 0 || dib_height == 0) return std::nullopt;
        width = dib_width;
        height = static_cast<int>((dib_height < 0 ? -static_cast<std::int64_t>(dib_height) : dib_height) / 2);
        top_down = dib_height < 0;
        bits = Le16(image, 14);
        compression = Le32(image, 16);
        colors_used = Le32(image, 32);
    }
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024 || (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32)
        || compression != 0) return std::nullopt;
    const std::uint32_t palette_count = bits <= 8 ? (colors_used != 0 ? colors_used : (1u << bits)) : 0;
    if (palette_count > 256) return std::nullopt;
    const std::size_t palette_offset = header;
    const std::size_t pixel_offset = palette_offset + static_cast<std::size_t>(palette_count) * entry_size;
    const std::size_t xor_stride = ((static_cast<std::size_t>(width) * bits + 31) / 32) * 4;
    const std::size_t mask_stride = ((static_cast<std::size_t>(width) + 31) / 32) * 4;
    const std::size_t xor_bytes = xor_stride * height, mask_bytes = mask_stride * height;
    if (!Has(pixel_offset, xor_bytes + mask_bytes, image.size())) return std::nullopt;
    bool source_alpha = false;
    if (bits == 32)
        for (int y = 0; y < height && !source_alpha; ++y)
            for (int x = 0; x < width; ++x)
                if (std::to_integer<unsigned>(image[pixel_offset + static_cast<std::size_t>(y) * xor_stride + x * 4 + 3]) != 0)
                {
                    source_alpha = true;
                    break;
                }
    Assets::PreparedImage out;
    out.width = static_cast<std::uint32_t>(width);
    out.height = static_cast<std::uint32_t>(height);
    out.row_pitch = static_cast<std::size_t>(width) * 4;
    out.encoding = Assets::PixelEncoding::RGBA8;
    out.bytes.resize(out.row_pitch * height);
    for (int y = 0; y < height; ++y)
    {
        const std::size_t source_y = top_down ? static_cast<std::size_t>(y) : static_cast<std::size_t>(height - 1 - y);
        const std::size_t color_row = pixel_offset + source_y * xor_stride;
        const std::size_t mask_row = pixel_offset + xor_bytes + static_cast<std::size_t>(height - 1 - y) * mask_stride;
        for (int x = 0; x < width; ++x)
        {
            unsigned red = 0, green = 0, blue = 0, alpha = 255;
            if (bits <= 8)
            {
                unsigned index = 0;
                if (bits == 1) index = (std::to_integer<unsigned>(image[color_row + x / 8]) >> (7 - (x & 7))) & 1u;
                else if (bits == 4) index = (std::to_integer<unsigned>(image[color_row + x / 2]) >> ((x & 1) ? 0 : 4)) & 0x0fu;
                else index = std::to_integer<unsigned>(image[color_row + x]);
                const std::size_t entry = palette_offset + static_cast<std::size_t>(index) * entry_size;
                if (index >= palette_count || !Has(entry, entry_size, image.size())) return std::nullopt;
                blue = std::to_integer<unsigned>(image[entry]);
                green = std::to_integer<unsigned>(image[entry + 1]);
                red = std::to_integer<unsigned>(image[entry + 2]);
            }
            else
            {
                const std::size_t pixel = color_row + static_cast<std::size_t>(x) * (bits / 8);
                blue = std::to_integer<unsigned>(image[pixel]);
                green = std::to_integer<unsigned>(image[pixel + 1]);
                red = std::to_integer<unsigned>(image[pixel + 2]);
                if (bits == 32) alpha = std::to_integer<unsigned>(image[pixel + 3]);
            }
            const bool masked = (std::to_integer<unsigned>(image[mask_row + x / 8]) & (1u << (7 - (x & 7)))) != 0;
            if (masked) alpha = 0;
            else if (bits != 32 || !source_alpha) alpha = 255;
            const std::size_t at = static_cast<std::size_t>(y) * out.row_pitch + static_cast<std::size_t>(x) * 4;
            out.bytes[at] = std::byte(red);
            out.bytes[at + 1] = std::byte(green);
            out.bytes[at + 2] = std::byte(blue);
            out.bytes[at + 3] = std::byte(alpha);
        }
    }
    hot_x = Le16(icon, 6 + 4);
    hot_y = Le16(icon, 6 + 6);
    return out;
}

inline bool Chunks(std::span<const std::byte> data, std::size_t begin, std::size_t end, Parsed& parsed)
{
    std::size_t offset = begin;
    while (offset < end)
    {
        if (!Has(offset, 8, end)) return false;
        const std::uint32_t size = Le32(data, offset + 4);
        const std::size_t payload = offset + 8;
        if (!Has(payload, size, end)) return false;
        if (Is(data, offset, "LIST"))
        {
            if (size < 4 || !Chunks(data, payload + 4, payload + size, parsed)) return false;
        }
        else if (Is(data, offset, "icon"))
        {
            int hot_x = -1, hot_y = -1;
            auto frame = Icon(data.subspan(payload, size), hot_x, hot_y);
            if (!frame) return false;
            if (parsed.frames.empty())
            {
                parsed.hot_x = hot_x;
                parsed.hot_y = hot_y;
            }
            parsed.frames.push_back(std::move(*frame));
        }
        else if (Is(data, offset, "rate"))
            for (std::size_t at = 0; at + 4 <= size; at += 4) parsed.rates.push_back(Le32(data, payload + at));
        else if (Is(data, offset, "seq "))
            for (std::size_t at = 0; at + 4 <= size; at += 4) parsed.sequence.push_back(Le32(data, payload + at));
        const std::size_t padded = static_cast<std::size_t>(size) + (size & 1u);
        if (!Has(payload, padded, end)) return false;
        offset = payload + padded;
    }
    return true;
}

inline Assets::PreparedImage Scaled(const Assets::PreparedImage& image, std::uint32_t width, std::uint32_t height)
{
    Assets::PreparedImage out;
    out.width = width;
    out.height = height;
    out.row_pitch = static_cast<std::size_t>(width) * 4;
    out.encoding = Assets::PixelEncoding::RGBA8;
    out.bytes.resize(out.row_pitch * height);
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const std::uint32_t sx = x * image.width / width, sy = y * image.height / height;
            for (unsigned channel = 0; channel < 4; ++channel)
                out.bytes[y * out.row_pitch + x * 4 + channel] = image.bytes[sy * image.row_pitch + sx * 4 + channel];
        }
    return out;
}
}

// The frames, their durations and the hotspot of an .ANI (std::nullopt: not a readable one); `fallback_fps` is the
// INI's FPS truncated to whole frames per second, as the original's static_cast<Int>.
export std::optional<AniCursorImage> Read_Ani_Cursor(std::span<const std::byte> data, int fallback_fps, int fallback_hot_x, int fallback_hot_y)
{
    using namespace AniDetail;
    if (data.size() < 12 || !Is(data, 0, "RIFF") || !Is(data, 8, "ACON")) return std::nullopt;
    const std::size_t end = std::min(data.size(), static_cast<std::size_t>(8) + Le32(data, 4));
    Parsed parsed;
    if (end < 12 || !Chunks(data, 12, end, parsed) || parsed.frames.empty()) return std::nullopt;
    AniCursorImage out;
    if (parsed.sequence.empty())
        out.frames = std::move(parsed.frames);
    else
    {
        for (const std::uint32_t index : parsed.sequence)
            if (index < parsed.frames.size()) out.frames.push_back(parsed.frames[index]);
        if (out.frames.empty()) return std::nullopt;
    }
    const std::uint32_t width = out.frames.front().width, height = out.frames.front().height;
    for (auto& frame : out.frames)
        if (frame.width != width || frame.height != height) frame = Scaled(frame, width, height);
    for (std::size_t frame = 0; frame < out.frames.size(); ++frame)
    {
        std::uint32_t duration = 0;
        if (frame < parsed.rates.size() && parsed.rates[frame] != 0)
            duration = static_cast<std::uint32_t>(std::clamp<std::uint64_t>((static_cast<std::uint64_t>(parsed.rates[frame]) * 1000 + 30) / 60, 1, 0xffffffffu));
        else if (out.frames.size() > 1)
            duration = fallback_fps > 0 ? static_cast<std::uint32_t>(std::max(1, 1000 / fallback_fps)) : 50u;
        out.durations_ms.push_back(duration);
    }
    out.hotspot_x = std::clamp(parsed.hot_x >= 0 ? parsed.hot_x : fallback_hot_x, 0, static_cast<int>(width) - 1);
    out.hotspot_y = std::clamp(parsed.hot_y >= 0 ? parsed.hot_y : fallback_hot_y, 0, static_cast<int>(height) - 1);
    return out;
}

// Makes `cursor` show an .ANI.
export bool Load_Ani_Cursor(Cursor& cursor, std::span<const std::byte> data, int fallback_fps, int fallback_hot_x, int fallback_hot_y)
{
    const auto image = Read_Ani_Cursor(data, fallback_fps, fallback_hot_x, fallback_hot_y);
    if (!image) return false;
    std::vector<CursorFrame> frames;
    for (std::size_t frame = 0; frame < image->frames.size(); ++frame)
        frames.push_back({image->frames[frame].View(), image->durations_ms[frame]});
    return cursor.Initialize(frames, image->hotspot_x, image->hotspot_y);
}
}
