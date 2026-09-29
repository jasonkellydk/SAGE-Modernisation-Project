export module engine.config.adapters.ini.ini_reader;
import std;

export import engine.config.document.document;
export import engine.config.document.diagnostics;

export namespace engine::config::ini
{
// Structure of an INI dialect: which first tokens start top-level blocks and
// which keys open a nested block inside a given parent. This is syntax only;
// the game supplies it (as data) and the reader stays game-agnostic.
struct Grammar
{
	// Empty: any first token at top level starts a block.
	std::set<std::string, std::less<>> topLevelBlocks;
	// Top-level keys that are a single line, with no End.
	std::set<std::string, std::less<>> topLevelSingleLine;
	// Parent key -> keys that open a nested block there.
	std::map<std::string, std::set<std::string, std::less<>>, std::less<>> nested;
	// Token that closes a block, compared case-insensitively.
	std::string endToken{"End"};

	bool IsTopLevelBlock(std::string_view key) const
	{
		return topLevelBlocks.empty() || topLevelBlocks.contains(key);
	}

	bool IsTopLevelSingleLine(std::string_view key) const { return topLevelSingleLine.contains(key); }

	bool OpensNestedBlock(std::string_view parent, std::string_view key) const
	{
		const auto found = nested.find(parent);
		return found != nested.end() && found->second.contains(key);
	}
};

// Token rules, matching the classic Generals reader: ';' starts a comment,
// "//" after whitespace starts a comment, control characters are spaces,
// tokens split on space, tab and '='. Quoted text is never cut by comments.
namespace detail
{
constexpr bool IsSeparator(char c) noexcept { return c == ' ' || c == '=' || (c >= 0 && c < 32); }

inline std::string_view StripComment(std::string_view line) noexcept
{
	bool quoted = false;
	for (std::size_t index = 0; index < line.size(); ++index)
	{
		const char c = line[index];
		if (c == '"')
			quoted = !quoted;
		else if (!quoted && c == ';')
			return line.substr(0, index);
		else if (!quoted && c == '/' && index + 1 < line.size() && line[index + 1] == '/' &&
			(index == 0 || line[index - 1] == ' ' || line[index - 1] == '\t'))
			return line.substr(0, index);
	}
	return line;
}

inline std::string_view Trim(std::string_view text) noexcept
{
	while (!text.empty() && IsSeparator(text.front()))
		text.remove_prefix(1);
	while (!text.empty() && (text.back() == ' ' || (text.back() >= 0 && text.back() < 32)))
		text.remove_suffix(1);
	return text;
}

inline std::vector<std::string_view> Tokens(std::string_view text)
{
	std::vector<std::string_view> tokens;
	std::size_t index = 0;
	while (index < text.size())
	{
		while (index < text.size() && IsSeparator(text[index]))
			++index;
		const std::size_t start = index;
		while (index < text.size() && !IsSeparator(text[index]))
			++index;
		if (index > start)
			tokens.push_back(text.substr(start, index - start));
	}
	return tokens;
}

inline bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
	if (a.size() != b.size())
		return false;
	for (std::size_t index = 0; index < a.size(); ++index)
	{
		const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
		if (lower(a[index]) != lower(b[index]))
			return false;
	}
	return true;
}
}

// Builds a grammar from a config document (any format), shaped as:
//   top_level_blocks: [names...]
//   top_level_single_line: [names...]       (optional; no End)
//   legacy_unregistered_blocks: [names...]  (optional; parsed as blocks)
//   nested: { <parent>: [child openers...] }
//   end_token: End            (optional)
inline Grammar GrammarFromDocument(const Document &document, Diagnostics &diagnostics)
{
	Grammar grammar;
	for (const Node &root : document.Roots())
	{
		if (root.key == "top_level_blocks" || root.key == "legacy_unregistered_blocks")
			grammar.topLevelBlocks.insert(root.values.begin(), root.values.end());
		else if (root.key == "top_level_single_line")
			grammar.topLevelSingleLine.insert(root.values.begin(), root.values.end());
		else if (root.key == "nested" && root.block)
			for (const Node &parent : root.children)
				grammar.nested[std::string(parent.key)].insert(parent.values.begin(), parent.values.end());
		else if (root.key == "end_token" && !root.values.empty())
			grammar.endToken = std::string(root.values.front());
		else if (root.key != "comment")
			diagnostics.Warning(root.location, "unknown grammar key '" + std::string(root.key) + "'");
	}
	return grammar;
}

// Parses one source already added to the document and appends its top-level
// blocks to document.Roots(). Problems are reported, parsing continues.
inline void Read(Document &document, std::uint32_t source, const Grammar &grammar, Diagnostics &diagnostics)
{
	const std::string_view text = document.SourceText(source);
	std::vector<Node> stack;
	std::uint32_t lineNumber = 0;
	std::size_t position = 0;
	while (position <= text.size())
	{
		const std::size_t newline = text.find('\n', position);
		const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
		const std::string_view line = detail::StripComment(text.substr(position, end - position));
		position = end + 1;
		++lineNumber;

		const std::string_view content = detail::Trim(line);
		if (content.empty())
		{
			if (newline == std::string_view::npos)
				break;
			continue;
		}
		std::size_t keyEnd = 0;
		while (keyEnd < content.size() && !detail::IsSeparator(content[keyEnd]))
			++keyEnd;
		Node node;
		node.key = content.substr(0, keyEnd);
		node.text = detail::Trim(content.substr(keyEnd));
		node.values = detail::Tokens(node.text);
		node.location = {source, lineNumber};

		if (detail::EqualsIgnoreCase(node.key, grammar.endToken))
		{
			if (stack.empty())
				diagnostics.Error(node.location, "'" + std::string(node.key) + "' without an open block");
			else
			{
				Node closed = std::move(stack.back());
				stack.pop_back();
				(stack.empty() ? document.Roots() : stack.back().children).push_back(std::move(closed));
			}
		}
		else if (stack.empty() && grammar.IsTopLevelSingleLine(node.key))
		{
			document.Roots().push_back(std::move(node));
		}
		else if (stack.empty())
		{
			if (!grammar.IsTopLevelBlock(node.key))
				diagnostics.Error(node.location, "unknown block type '" + std::string(node.key) + "'");
			node.block = true;
			stack.push_back(std::move(node));
		}
		else if (grammar.OpensNestedBlock(stack.back().key, node.key))
		{
			node.block = true;
			stack.push_back(std::move(node));
		}
		else
		{
			stack.back().children.push_back(std::move(node));
		}
		if (newline == std::string_view::npos)
			break;
	}
	while (!stack.empty())
	{
		diagnostics.Error(stack.back().location, "block '" + std::string(stack.back().key) + "' is missing its End");
		Node closed = std::move(stack.back());
		stack.pop_back();
		(stack.empty() ? document.Roots() : stack.back().children).push_back(std::move(closed));
	}
}
}
