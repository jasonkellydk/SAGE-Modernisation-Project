export module engine.core.text.utf;
import std;

// UTF-16 <-> UTF-8 (surrogate pairs joined and split; a lone surrogate or a
// broken UTF-8 sequence becomes U+FFFD).
export namespace engine::core::text
{
inline std::string ToUtf8(std::u16string_view text)
{
	std::string out;
	for (std::size_t at = 0; at < text.size(); ++at)
	{
		std::uint32_t code = text[at];
		if (code >= 0xD800 && code <= 0xDBFF && at + 1 < text.size() && text[at + 1] >= 0xDC00 && text[at + 1] <= 0xDFFF)
			code = 0x10000 + ((code - 0xD800) << 10) + (text[++at] - 0xDC00u);
		else if (code >= 0xD800 && code <= 0xDFFF)
			code = 0xFFFD;
		if (code < 0x80)
			out.push_back(static_cast<char>(code));
		else if (code < 0x800)
		{
			out.push_back(static_cast<char>(0xC0 | (code >> 6)));
			out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
		}
		else if (code < 0x10000)
		{
			out.push_back(static_cast<char>(0xE0 | (code >> 12)));
			out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
		}
		else
		{
			out.push_back(static_cast<char>(0xF0 | (code >> 18)));
			out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
		}
	}
	return out;
}

inline std::u16string FromUtf8(std::string_view text)
{
	std::u16string out;
	for (std::size_t at = 0; at < text.size();)
	{
		const auto byte = [&](std::size_t index) { return static_cast<std::uint32_t>(static_cast<unsigned char>(text[index])); };
		const std::uint32_t lead = byte(at);
		const std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
		bool ok = length > 0 && at + length <= text.size();
		std::uint32_t code = length == 1 ? lead : length == 2 ? lead & 0x1F : length == 3 ? lead & 0x0F : lead & 0x07;
		for (std::size_t next = 1; ok && next < length; ++next)
		{
			ok = (byte(at + next) & 0xC0) == 0x80;
			code = (code << 6) | (byte(at + next) & 0x3F);
		}
		if (!ok)
		{
			out.push_back(u'�');
			++at;
			continue;
		}
		at += length;
		if (code >= 0x10000)
		{
			code -= 0x10000;
			out.push_back(static_cast<char16_t>(0xD800 + (code >> 10)));
			out.push_back(static_cast<char16_t>(0xDC00 + (code & 0x3FF)));
		}
		else
			out.push_back(static_cast<char16_t>(code));
	}
	return out;
}
}
