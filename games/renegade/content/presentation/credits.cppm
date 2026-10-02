export module games.renegade.content.presentation.credits;
import std;
export namespace renegade::content {
// CreditsMenuClass::On_Init_Dialog reads bytes as unsigned ANSI characters,
// stops at the first NUL and removes every carriage return, including CRCRLF.
std::u16string ReadCreditsText(std::span<const std::byte> bytes) {
    std::u16string text;text.reserve(bytes.size());
    for(const auto byte:bytes) {
        const auto ch=std::to_integer<unsigned char>(byte);if(!ch) break;
        if(ch!='\r') text.push_back(static_cast<char16_t>(ch));
    }
    return text;
}
}
