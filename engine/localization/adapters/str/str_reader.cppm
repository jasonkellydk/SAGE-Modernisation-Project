export module engine.localization.adapters.str.str_reader;
import std;

export import engine.localization.model.string_table;
import engine.localization.adapters.csf.csf_reader;

// EA ".str" string files, the text form of a CSF (the original's
// GameTextManager::parseStringFile, also used for a map's map.str):
//   // comment
//   LABEL:Name
//   "the text, which may run over lines" =SpeechName
//   END
// Inside the quotes line breaks and other whitespace become spaces, and
// \n, \t, \\, \', \", \? are escapes (any other escaped character stands for
// itself); characters are 8-bit. A speech name ending in a digit gets an "e"
// appended, as readToEndOfQuote does. The text's whitespace is normalised as
// the CSF reader does. Only the first text of a label is kept.
export namespace engine::localization::str
{
namespace detail
{
inline bool Space(char c) noexcept { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

inline std::string_view Trim(std::string_view line) noexcept
{
	while (!line.empty() && Space(line.front()))
		line.remove_prefix(1);
	while (!line.empty() && Space(line.back()))
		line.remove_suffix(1);
	return line;
}

inline bool CaseBlindEqual(std::string_view a, std::string_view b) noexcept
{
	if (a.size() != b.size())
		return false;
	for (std::size_t index = 0; index < a.size(); ++index)
		if ((a[index] | 0x20) != (b[index] | 0x20))
			return false;
	return true;
}

// translateCopy: the escapes, 8-bit characters widened.
inline std::u16string Translate(std::string_view in)
{
	std::u16string out;
	bool slash = false;
	for (const char c : in)
	{
		const char16_t wide = static_cast<char16_t>(static_cast<unsigned char>(c));
		if (slash)
		{
			slash = false;
			out.push_back(c == 't' ? u'\t' : c == 'n' ? u'\n' : wide);
		}
		else if (c == '\\')
			slash = true;
		else
			out.push_back(wide);
	}
	return out;
}
}

// Adds the file's strings to `table`; the number read, or why the file is broken.
inline std::expected<std::size_t, std::string> Read(std::string_view text, StringTable &table)
{
	std::size_t at = 0, count = 0;
	const auto nextLine = [&](std::string_view &line) {
		if (at >= text.size())
			return false;
		const std::size_t end = text.find('\n', at);
		line = text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
		at = end == std::string_view::npos ? text.size() : end + 1;
		return true;
	};
	std::string_view line;
	while (nextLine(line))
	{
		const std::string_view label = detail::Trim(line);
		if (label.empty() || label.starts_with("//"))
			continue;
		bool read = false;
		for (;;)
		{
			if (!nextLine(line))
				return std::unexpected("unexpected end of string file in " + std::string(label));
			const std::string_view trimmed = detail::Trim(line);
			if (!trimmed.empty() && trimmed.front() == '"')
			{
				// readToEndOfQuote: from after the quote, over lines, to the unescaped closing quote.
				at = static_cast<std::size_t>(line.data() - text.data()) + line.find('"') + 1;
				std::string quoted;
				bool slash = false, closed = false;
				while (at < text.size())
				{
					char c = text[at++];
					if (c == '\\')
						slash = !slash;
					else if (c == '"' && !slash)
					{
						closed = true;
						break;
					}
					else
						slash = false;
					if (c == '\n')
						slash = false;
					if (detail::Space(c))
						c = ' ';
					quoted.push_back(c);
				}
				// The rest of the line: "=Speech" (letters, digits and underscores).
				std::string speech;
				if (closed)
				{
					const std::size_t end = text.find('\n', at);
					std::string_view rest = text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
					at = end == std::string_view::npos ? text.size() : end + 1;
					while (!rest.empty() && (detail::Space(rest.front()) || rest.front() == '='))
						rest.remove_prefix(1);
					for (const char c : rest)
					{
						if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
							break;
						speech.push_back(c);
					}
					if (!speech.empty() && speech.back() >= '0' && speech.back() <= '9')
						speech.push_back('e');
				}
				if (!read)
				{
					LocalizedString value;
					value.text = csf::detail::ToUtf8(csf::detail::StripSpaces(detail::Translate(quoted)));
					value.speech = std::move(speech);
					table.Set(label, std::move(value));
					read = true;
					++count;
				}
				if (!closed)
					return std::unexpected("unexpected end of string file in " + std::string(label));
			}
			else if (detail::CaseBlindEqual(trimmed, "END"))
				break;
		}
	}
	return count;
}
}
