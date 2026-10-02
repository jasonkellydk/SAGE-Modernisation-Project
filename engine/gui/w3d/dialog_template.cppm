export module engine.gui.w3d.dialog_template;
import std;

export namespace engine::gui::w3d
{
struct ResourceField { std::optional<std::uint16_t> ordinal; std::u16string text; };
struct DialogRect { std::int16_t x{}, y{}, width{}, height{}; };
struct DialogControlDefinition {
    std::uint32_t id{}, style{}, extended_style{};
    DialogRect rect;
    ResourceField kind, title;
    std::vector<std::byte> creation_data;
};
struct DialogDefinition {
    std::uint32_t style{}, extended_style{};
    DialogRect rect;
    ResourceField menu, kind, title;
    std::u16string font;
    std::uint16_t font_size{}, font_weight{};
    std::uint8_t font_italic{}, font_charset{};
    std::vector<DialogControlDefinition> controls;
};

// Portable decoding of the resource format consumed by wwui/dialogparser.cpp.
// Extended templates are supported for mod tools in addition to retail's
// standard templates. Returned strings retain the original IDS_ descriptors.
std::expected<DialogDefinition, std::string> ReadDialogTemplate(std::span<const std::byte> bytes)
{
    struct Reader {
        std::span<const std::byte> bytes;
        std::size_t p{};
        bool good{true};
        std::uint32_t Read(unsigned n) {
            if (p > bytes.size() || n > bytes.size() - p) { good = false; return 0; }
            std::uint32_t value{};
            for (unsigned i = 0; i < n; ++i) value |= std::to_integer<std::uint32_t>(bytes[p++]) << (i * 8);
            return value;
        }
        ResourceField Field() {
            ResourceField out;
            auto c = Read(2);
            if (c == 0xffff) out.ordinal = static_cast<std::uint16_t>(Read(2));
            else while (good && c) { out.text += static_cast<char16_t>(c); c = Read(2); }
            return out;
        }
        DialogRect Rect() { return {static_cast<std::int16_t>(Read(2)), static_cast<std::int16_t>(Read(2)),
            static_cast<std::int16_t>(Read(2)), static_cast<std::int16_t>(Read(2))}; }
        void Align() { p = (p + 3) & ~std::size_t(3); if (p > bytes.size()) good = false; }
    } r{bytes};
    DialogDefinition out;
    const auto first = r.Read(2), second = r.Read(2);
    const bool extended = first == 1 && second == 0xffff;
    if (extended) { r.Read(4); out.extended_style = r.Read(4); out.style = r.Read(4); }
    else { out.style = first | (second << 16); out.extended_style = r.Read(4); }
    const auto count = r.Read(2);
    out.rect = r.Rect();
    out.menu = r.Field(); out.kind = r.Field(); out.title = r.Field();
    if (out.style & 0x40) {
        out.font_size = static_cast<std::uint16_t>(r.Read(2));
        if (extended) { out.font_weight = static_cast<std::uint16_t>(r.Read(2));
            out.font_italic = static_cast<std::uint8_t>(r.Read(1)); out.font_charset = static_cast<std::uint8_t>(r.Read(1)); }
        out.font = r.Field().text;
    }
    // Each item occupies at least 24 bytes. Bound allocation by input size.
    if (!r.good || count > bytes.size() / 24) return std::unexpected("invalid dialog header or control count");
    for (unsigned i = 0; i < count && r.good; ++i) {
        r.Align();
        DialogControlDefinition control;
        if (extended) { r.Read(4); control.extended_style = r.Read(4); control.style = r.Read(4); }
        else { control.style = r.Read(4); control.extended_style = r.Read(4); }
        control.rect = r.Rect(); control.id = r.Read(extended ? 4 : 2);
        control.kind = r.Field(); control.title = r.Field();
        const auto size = r.Read(2);
        // The creation-data count includes its own WORD, when nonzero.
        if (size == 1 || !r.good || (size && size - 2 > bytes.size() - r.p)) { r.good = false; break; }
        if (size) { control.creation_data.assign(bytes.begin() + r.p, bytes.begin() + r.p + size - 2); r.p += size - 2; }
        out.controls.push_back(std::move(control));
    }
    if (!r.good) return std::unexpected("truncated dialog template");
    return out;
}
}
