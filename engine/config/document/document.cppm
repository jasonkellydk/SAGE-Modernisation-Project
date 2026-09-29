export module engine.config.document.document;
import std;

export namespace engine::config
{
struct SourceLocation
{
	std::uint32_t source{0};
	std::uint32_t line{0};
};

// One key with its values, or a block of child nodes. Format adapters build
// this tree; bindings read it. It knows nothing about any game.
struct Node
{
	std::string_view key;
	// Everything after the key on its line, trimmed, without comments. Kept
	// for values with their own syntax (quoted text, "X:1 Y:2", "50%").
	std::string_view text;
	// `text` split on whitespace and '='.
	std::vector<std::string_view> values;
	SourceLocation location;
	bool block{false};
	std::vector<Node> children;

	const Node *Find(std::string_view childKey) const noexcept
	{
		for (const Node &child : children)
			if (child.key == childKey)
				return &child;
		return nullptr;
	}

	std::string_view Value(std::size_t index = 0) const noexcept
	{
		return index < values.size() ? values[index] : std::string_view{};
	}
};

// Owns the source text and any strings adapters had to create (e.g. JSON
// unescaping), so every string_view in the tree stays valid while it lives.
class Document
{
public:
	Document() = default;
	Document(const Document &) = delete;
	Document &operator=(const Document &) = delete;
	Document(Document &&) = default;
	Document &operator=(Document &&) = default;

	std::uint32_t AddSource(std::string name, std::string text)
	{
		m_sources.push_back({std::move(name), std::move(text)});
		return static_cast<std::uint32_t>(m_sources.size() - 1);
	}

	std::string_view SourceName(std::uint32_t source) const { return m_sources.at(source).name; }
	std::string_view SourceText(std::uint32_t source) const { return m_sources.at(source).text; }
	std::size_t SourceCount() const noexcept { return m_sources.size(); }

	std::string_view Intern(std::string value)
	{
		m_strings.push_back(std::move(value));
		return m_strings.back();
	}

	std::vector<Node> &Roots() noexcept { return m_roots; }
	const std::vector<Node> &Roots() const noexcept { return m_roots; }

	std::string Describe(SourceLocation location) const
	{
		const std::string_view name = location.source < m_sources.size() ? std::string_view{m_sources[location.source].name} : "<unknown>";
		return std::string(name) + ":" + std::to_string(location.line);
	}

private:
	struct Source
	{
		std::string name;
		std::string text;
	};

	std::deque<Source> m_sources;
	std::deque<std::string> m_strings;
	std::vector<Node> m_roots;
};
}
