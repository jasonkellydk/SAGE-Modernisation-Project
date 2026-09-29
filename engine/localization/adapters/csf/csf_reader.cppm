export module engine.localization.adapters.csf.csf_reader;
import std;

export import engine.localization.model.string_table;

// EA "CSF" compiled string tables (little-endian):
//   header: "CSF " (as u32 ' FSC'), version, label count, string count, reserved, language
//   label:  " LBL", string count, name length, name
//   string: " RTS" or "WRTS", length in UTF-16 units, bit-inverted UTF-16LE text,
//           and for WRTS an extra length + speech asset name
// Only the first string of a label is used, and whitespace is normalised the
// way the original does (spaces collapsed, trimmed around line breaks/tabs).
export namespace engine::localization::csf
{
namespace detail
{
constexpr std::uint32_t Tag(char a, char b, char c, char d)
{
	return (std::uint32_t(std::uint8_t(a)) << 24) | (std::uint32_t(std::uint8_t(b)) << 16) | (std::uint32_t(std::uint8_t(c)) << 8) | std::uint8_t(d);
}
constexpr std::uint32_t FileTag = Tag('C', 'S', 'F', ' ');
constexpr std::uint32_t LabelTag = Tag('L', 'B', 'L', ' ');
constexpr std::uint32_t StringTag = Tag('S', 'T', 'R', ' ');
constexpr std::uint32_t StringWithSpeechTag = Tag('S', 'T', 'R', 'W');

inline std::u16string StripSpaces(const std::u16string &text)
{
	std::u16string out;
	char16_t last = 0;
	bool skipAll = true;
	for (const char16_t ch : text)
	{
		if (ch == u' ' && (last == u' ' || skipAll))
			continue;
		if (ch == u'\n' || ch == u'\t')
		{
			if (last == u' ' && !out.empty())
				out.pop_back();
			skipAll = true;
			out += ch;
			last = ch;
			continue;
		}
		out += ch;
		last = ch;
		skipAll = false;
	}
	if (last == u' ' && !out.empty())
		out.pop_back();
	return out;
}

inline std::string ToUtf8(const std::u16string &text)
{
	std::string out;
	for (std::size_t index = 0; index < text.size(); ++index)
	{
		std::uint32_t code = text[index];
		if (code >= 0xD800 && code <= 0xDBFF && index + 1 < text.size() && text[index + 1] >= 0xDC00 && text[index + 1] <= 0xDFFF)
			code = 0x10000 + ((code - 0xD800) << 10) + (text[++index] - 0xDC00);
		if (code < 0x80)
			out += static_cast<char>(code);
		else if (code < 0x800)
		{
			out += static_cast<char>(0xC0 | (code >> 6));
			out += static_cast<char>(0x80 | (code & 0x3F));
		}
		else if (code < 0x10000)
		{
			out += static_cast<char>(0xE0 | (code >> 12));
			out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (code & 0x3F));
		}
		else
		{
			out += static_cast<char>(0xF0 | (code >> 18));
			out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (code & 0x3F));
		}
	}
	return out;
}

class Cursor
{
public:
	explicit Cursor(std::span<const std::byte> data) : m_data(data) {}
	bool AtEnd() const noexcept { return m_at >= m_data.size(); }
	bool U32(std::uint32_t &value)
	{
		if (m_data.size() - m_at < 4)
			return false;
		value = std::to_integer<std::uint32_t>(m_data[m_at]) | (std::to_integer<std::uint32_t>(m_data[m_at + 1]) << 8) |
			(std::to_integer<std::uint32_t>(m_data[m_at + 2]) << 16) | (std::to_integer<std::uint32_t>(m_data[m_at + 3]) << 24);
		m_at += 4;
		return true;
	}
	bool Bytes(std::size_t count, std::string &out)
	{
		if (m_data.size() - m_at < count)
			return false;
		out.assign(reinterpret_cast<const char *>(m_data.data() + m_at), count);
		m_at += count;
		return true;
	}
	bool InvertedUtf16(std::size_t units, std::u16string &out)
	{
		if ((m_data.size() - m_at) / 2 < units)
			return false;
		out.resize(units);
		for (std::size_t unit = 0; unit < units; ++unit, m_at += 2)
			out[unit] = static_cast<char16_t>(~(std::to_integer<unsigned>(m_data[m_at]) | (std::to_integer<unsigned>(m_data[m_at + 1]) << 8)));
		// The original stops at the first NUL after inversion.
		if (const auto nul = out.find(u'\0'); nul != std::u16string::npos)
			out.resize(nul);
		return true;
	}

private:
	std::span<const std::byte> m_data;
	std::size_t m_at{0};
};
}

struct CsfInfo
{
	std::uint32_t version{0};
	std::uint32_t language{0};
	std::size_t labels{0};
};

inline std::expected<CsfInfo, std::string> Read(std::span<const std::byte> data, StringTable &table)
{
	detail::Cursor cursor(data);
	std::uint32_t id = 0, version = 0, labels = 0, strings = 0, reserved = 0, language = 0;
	if (!cursor.U32(id) || !cursor.U32(version) || !cursor.U32(labels) || !cursor.U32(strings) || !cursor.U32(reserved) ||
		!cursor.U32(language))
		return std::unexpected("truncated CSF header");
	if (id != detail::FileTag)
		return std::unexpected("not a CSF string table");
	CsfInfo info{version, language, 0};
	while (!cursor.AtEnd())
	{
		std::uint32_t tag = 0, count = 0, length = 0;
		std::string label;
		if (!cursor.U32(tag) || tag != detail::LabelTag || !cursor.U32(count) || !cursor.U32(length) || !cursor.Bytes(length, label))
			return std::unexpected("corrupt label record after " + std::to_string(info.labels) + " labels");
		LocalizedString value;
		for (std::uint32_t index = 0; index < count; ++index)
		{
			std::u16string text;
			if (!cursor.U32(tag) || (tag != detail::StringTag && tag != detail::StringWithSpeechTag) || !cursor.U32(length) ||
				!cursor.InvertedUtf16(length, text))
				return std::unexpected("corrupt string record in label '" + label + "'");
			std::string speech;
			if (tag == detail::StringWithSpeechTag && (!cursor.U32(length) || !cursor.Bytes(length, speech)))
				return std::unexpected("corrupt speech name in label '" + label + "'");
			if (index == 0)
			{
				value.text = detail::ToUtf8(detail::StripSpaces(text));
				value.speech = std::move(speech);
			}
		}
		table.Set(label, std::move(value));
		++info.labels;
	}
	return info;
}
}
