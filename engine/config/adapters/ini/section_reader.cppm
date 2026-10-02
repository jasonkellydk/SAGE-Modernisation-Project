export module engine.config.adapters.ini.section_reader;
import std;
export import engine.config.document.document;

export namespace engine::config::ini
{
std::string FoldAscii(std::string_view text)
{
	std::string folded(text);
	for (char &c : folded) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
	return folded;
}
// Bracketed INI is a separate dialect from the Generals block/End grammar.
// This adapter shares the document model and preserves declaration order.
// Duplicate keys replace their value in place; lookups are ASCII case-insensitive.
std::expected<Document, std::string> ReadSections(std::string name, std::string text)
{
	Document document;
	const auto source = document.AddSource(std::move(name), std::move(text));
	const auto trim = [](std::string_view value) {
		while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) value.remove_prefix(1);
		while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) value.remove_suffix(1);
		return value;
	};
	Node *section = nullptr;
	std::string_view remaining = document.SourceText(source);
	std::uint32_t number = 0;
	while (!remaining.empty())
	{
		++number;
		const auto newline = remaining.find('\n');
		auto line = remaining.substr(0, newline);
		remaining = newline == std::string_view::npos ? std::string_view{} : remaining.substr(newline + 1);
		line = trim(line.substr(0, line.find(';')));
		if (line.empty()) continue;
		if (line.front() == '[' && line.back() == ']')
		{
			const auto key = FoldAscii(trim(line.substr(1, line.size() - 2)));
			if (key.empty()) return std::unexpected("empty INI section at line " + std::to_string(number));
			const auto found = std::ranges::find(document.Roots(), key, &Node::key);
			if (found == document.Roots().end())
			{
				document.Roots().push_back(Node{document.Intern(key), {}, {}, {source, number}, true, {}});
				section = &document.Roots().back();
			}
			else section = &*found;
			continue;
		}
		const auto equal = line.find('=');
		if (!section || equal == std::string_view::npos)
			return std::unexpected("INI entry requires a section and '=' at line " + std::to_string(number));
		const auto key = FoldAscii(trim(line.substr(0, equal)));
		const auto value = trim(line.substr(equal + 1));
		if (key.empty()) return std::unexpected("empty INI key at line " + std::to_string(number));
		Node entry{document.Intern(key), value, {value}, {source, number}, false, {}};
		const auto found = std::ranges::find(section->children, key, &Node::key);
		if (found == section->children.end()) section->children.push_back(std::move(entry));
		else *found = std::move(entry);
	}
	return document;
}
const Node *FindSection(const Document &document, std::string_view name)
{
	const auto key = FoldAscii(name);
	const auto found = std::ranges::find(document.Roots(), key, &Node::key);
	return found == document.Roots().end() ? nullptr : &*found;
}
}
