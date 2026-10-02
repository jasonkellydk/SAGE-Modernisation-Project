export module engine.config.adapters.json.json_reader;
import std;

export import engine.config.document.document;
export import engine.config.document.diagnostics;

// JSON -> the same document tree the INI adapter produces:
//   "key": scalar                -> node with one value
//   "key": [scalars]             -> node with several values
//   "key": { ... }               -> block with one child per member
//   "key": [ {..}, {..} ]        -> one block per element, all named "key"
// The root must be an object; its members become top-level nodes.
namespace engine::config::json::detail
{
class Parser
{
public:
	Parser(Document &document, std::uint32_t source, Diagnostics &diagnostics) :
		m_document(document), m_text(document.SourceText(source)), m_source(source), m_diagnostics(diagnostics)
	{
	}

	void ParseRoot()
	{
		SkipSpace();
		if (!Consume('{'))
		{
			Fail("the document root must be an object");
			return;
		}
		ParseMembers(m_document.Roots());
		SkipSpace();
		if (m_position != m_text.size() && !m_failed)
			Fail("unexpected text after the root object");
	}

private:
	void ParseMembers(std::vector<Node> &out)
	{
		SkipSpace();
		if (Consume('}'))
			return;
		while (!m_failed)
		{
			SkipSpace();
			const SourceLocation location = Here();
			std::string_view key;
			if (!ParseString(key))
				return Fail("expected a member name");
			SkipSpace();
			if (!Consume(':'))
				return Fail("expected ':' after member name");
			ParseValueInto(out, key, location);
			SkipSpace();
			if (Consume(','))
				continue;
			if (Consume('}'))
				return;
			return Fail("expected ',' or '}'");
		}
	}

	void ParseValueInto(std::vector<Node> &out, std::string_view key, SourceLocation location)
	{
		SkipSpace();
		Node node;
		node.key = key;
		node.location = location;
		if (Consume('{'))
		{
			node.block = true;
			ParseMembers(node.children);
			out.push_back(std::move(node));
			return;
		}
		if (Consume('['))
		{
			std::vector<Node> blocks;
			SkipSpace();
			if (!Consume(']'))
			{
				while (!m_failed)
				{
					SkipSpace();
					if (Peek() == '{')
					{
						const SourceLocation elementLocation = Here();
						Consume('{');
						Node element;
						element.key = key;
						element.location = elementLocation;
						element.block = true;
						ParseMembers(element.children);
						blocks.push_back(std::move(element));
					}
					else
					{
						std::string_view scalar;
						if (!ParseScalar(scalar))
							return;
						node.values.push_back(scalar);
					}
					SkipSpace();
					if (Consume(','))
						continue;
					if (Consume(']'))
						break;
					return Fail("expected ',' or ']'");
				}
			}
			if (blocks.empty() || !node.values.empty())
			{
				node.text = Join(node.values);
				out.push_back(std::move(node));
			}
			for (Node &block : blocks)
				out.push_back(std::move(block));
			return;
		}
		std::string_view scalar;
		if (!ParseScalar(scalar))
			return;
		node.values.push_back(scalar);
		node.text = scalar;
		out.push_back(std::move(node));
	}

	bool ParseScalar(std::string_view &out)
	{
		SkipSpace();
		if (Peek() == '"')
			return ParseString(out) || (Fail("invalid string"), false);
		const std::size_t start = m_position;
		while (m_position < m_text.size() && m_text[m_position] != ',' && m_text[m_position] != ']' &&
			m_text[m_position] != '}' && !IsSpace(m_text[m_position]))
			++m_position;
		out = m_text.substr(start, m_position - start);
		if (out.empty())
			return Fail("expected a value"), false;
		return true;
	}

	bool ParseString(std::string_view &out)
	{
		if (!Consume('"'))
			return false;
		const std::size_t start = m_position;
		bool escaped = false;
		while (m_position < m_text.size() && m_text[m_position] != '"')
		{
			if (m_text[m_position] == '\n')
				return false;
			if (m_text[m_position] == '\\')
			{
				escaped = true;
				++m_position;
			}
			++m_position;
		}
		if (m_position >= m_text.size())
			return false;
		const std::string_view raw = m_text.substr(start, m_position - start);
		++m_position;
		out = escaped ? m_document.Intern(Unescape(raw)) : raw;
		return true;
	}

	static std::string Unescape(std::string_view raw)
	{
		std::string out;
		for (std::size_t index = 0; index < raw.size(); ++index)
		{
			if (raw[index] != '\\' || index + 1 >= raw.size())
			{
				out += raw[index];
				continue;
			}
			const char c = raw[++index];
			switch (c)
			{
			case 'n': out += '\n'; break;
			case 't': out += '\t'; break;
			case 'r': out += '\r'; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'u':
				// Basic multilingual plane only, encoded as UTF-8.
				if (index + 4 < raw.size())
				{
					const unsigned code = static_cast<unsigned>(std::stoul(std::string(raw.substr(index + 1, 4)), nullptr, 16));
					index += 4;
					if (code < 0x80)
						out += static_cast<char>(code);
					else if (code < 0x800)
					{
						out += static_cast<char>(0xC0 | (code >> 6));
						out += static_cast<char>(0x80 | (code & 0x3F));
					}
					else
					{
						out += static_cast<char>(0xE0 | (code >> 12));
						out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
						out += static_cast<char>(0x80 | (code & 0x3F));
					}
				}
				break;
			default: out += c; break;
			}
		}
		return out;
	}

	std::string_view Join(const std::vector<std::string_view> &values)
	{
		if (values.size() == 1)
			return values.front();
		std::string joined;
		for (const std::string_view value : values)
		{
			if (!joined.empty())
				joined += ' ';
			joined += value;
		}
		return m_document.Intern(std::move(joined));
	}

	static bool IsSpace(char c) noexcept { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

	void SkipSpace()
	{
		while (m_position < m_text.size() && IsSpace(m_text[m_position]))
		{
			if (m_text[m_position] == '\n')
				++m_line;
			++m_position;
		}
	}

	char Peek() const noexcept { return m_position < m_text.size() ? m_text[m_position] : '\0'; }

	bool Consume(char expected)
	{
		if (Peek() != expected)
			return false;
		++m_position;
		return true;
	}

	SourceLocation Here() const noexcept { return {m_source, m_line}; }

	void Fail(std::string message)
	{
		if (!m_failed)
			m_diagnostics.Error(Here(), std::move(message));
		m_failed = true;
	}

	Document &m_document;
	std::string_view m_text;
	std::uint32_t m_source;
	Diagnostics &m_diagnostics;
	std::size_t m_position{0};
	std::uint32_t m_line{1};
	bool m_failed{false};
};
}

export namespace engine::config::json
{
inline void Read(Document &document, std::uint32_t source, Diagnostics &diagnostics)
{
	detail::Parser(document, source, diagnostics).ParseRoot();
}
}
