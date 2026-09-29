export module engine.core.text.quoted_printable;
import std;

// The "quoted-printable" of the original's preference files (QuotedPrintable.cpp):
// every byte that is not a letter or digit is written as '_' and two upper-case
// hex digits. Wide text is taken as its UTF-16 little-endian bytes, so "Jo"
// becomes "J_00o_00". Decoding reads '_' and up to two hex digits for a byte;
// a '_' at the very end stops it.
export namespace engine::core::text
{
namespace detail
{
constexpr char Magic = '_';

constexpr bool Alphanumeric(unsigned char c) noexcept
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

constexpr int HexValue(char c) noexcept
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return 0; // hexDigitToInt
}

inline void Put(std::string &out, unsigned char byte)
{
	if (Alphanumeric(byte))
	{
		out.push_back(static_cast<char>(byte));
		return;
	}
	constexpr char Digits[] = "0123456789ABCDEF";
	out.push_back(Magic);
	out.push_back(Digits[byte >> 4]);
	out.push_back(Digits[byte & 0xF]);
}

inline std::string Bytes(std::string_view encoded)
{
	std::string bytes;
	for (std::size_t at = 0; at < encoded.size(); ++at)
	{
		if (encoded[at] != Magic)
		{
			bytes.push_back(encoded[at]);
			continue;
		}
		if (at + 1 >= encoded.size())
			break; // ends with the magic character
		int value = HexValue(encoded[++at]);
		if (at + 1 < encoded.size())
			value = (value << 4) | HexValue(encoded[++at]);
		bytes.push_back(static_cast<char>(value));
	}
	return bytes;
}
}

// AsciiStringToQuotedPrintable.
inline std::string EncodeQuotedPrintable(std::string_view text)
{
	std::string out;
	for (const char c : text)
		detail::Put(out, static_cast<unsigned char>(c));
	return out;
}

// UnicodeStringToQuotedPrintable.
inline std::string EncodeQuotedPrintable(std::u16string_view text)
{
	std::string out;
	for (const char16_t c : text)
	{
		detail::Put(out, static_cast<unsigned char>(c & 0xFF));
		detail::Put(out, static_cast<unsigned char>(c >> 8));
	}
	return out;
}

// QuotedPrintableToAsciiString.
inline std::string DecodeQuotedPrintable(std::string_view encoded)
{
	std::string bytes = detail::Bytes(encoded);
	if (const auto end = bytes.find('\0'); end != std::string::npos)
		bytes.resize(end);
	return bytes;
}

// QuotedPrintableToUnicodeString (an odd last byte is dropped).
inline std::u16string DecodeQuotedPrintableWide(std::string_view encoded)
{
	const std::string bytes = detail::Bytes(encoded);
	std::u16string text;
	for (std::size_t at = 0; at + 1 < bytes.size(); at += 2)
	{
		const char16_t c = static_cast<char16_t>(static_cast<unsigned char>(bytes[at]) | (static_cast<unsigned char>(bytes[at + 1]) << 8));
		if (c == 0)
			break;
		text.push_back(c);
	}
	return text;
}
}
