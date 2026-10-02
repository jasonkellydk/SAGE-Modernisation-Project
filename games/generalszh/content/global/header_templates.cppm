export module games.generalszh.content.global.header_templates;
import std;

export import engine.config.document.document;
export import engine.filesystem.core.virtual_file_system;
import engine.config.binding.values;
import engine.config.adapters.ini.ini_reader;
import games.generalszh.content.ini.zero_hour_grammar;

// Data\<language>\HeaderTemplate.ini (HeaderTemplate.cpp): named fonts a WND window takes in place of its own FONT
// (its HEADERTEMPLATE), so a language can change them in one place. Each is HeaderTemplateManager's Font (quoted name),
// Point and Bold; a name met again is the same template, its fields read once more (the original's duplicate case).
export namespace generalszh::content
{
struct HeaderTemplate
{
	std::string name;
	std::string fontName;
	int point{0};
	bool bold{false};
};

struct HeaderTemplates
{
	std::vector<HeaderTemplate> templates;

	// HeaderTemplateManager::findHeaderTemplate: by exact name; none: nullptr ("[NONE]" names none).
	const HeaderTemplate *Find(std::string_view name) const noexcept
	{
		for (const HeaderTemplate &found : templates)
			if (found.name == name)
				return &found;
		return nullptr;
	}
};

inline void BindHeaderTemplates(const engine::config::Document &document, HeaderTemplates &out)
{
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "HeaderTemplate")
			continue;
		const std::string name(root.Value());
		HeaderTemplate *entry = nullptr;
		for (HeaderTemplate &known : out.templates)
			if (known.name == name)
				entry = &known;
		if (entry == nullptr)
			entry = &out.templates.emplace_back(HeaderTemplate{name});
		for (const engine::config::Node &field : root.children)
		{
			if (field.values.empty())
				continue;
			if (field.key == "Font")
			{
				// INI::parseQuotedAsciiString: the quoted name, its words joined.
				std::string font;
				for (const std::string_view token : field.values)
					font += (font.empty() ? "" : " ") + std::string(token);
				std::erase(font, '"');
				entry->fontName = font;
			}
			else if (field.key == "Point")
				std::from_chars(field.Value().data(), field.Value().data() + field.Value().size(), entry->point);
			else if (field.key == "Bold")
				entry->bold = engine::config::values::ParseBool(field.Value()).value_or(entry->bold);
		}
	}
}

// HeaderTemplateManager::init: Data\<language>\HeaderTemplate.ini (none: no templates).
inline HeaderTemplates ReadHeaderTemplates(const engine::filesystem::VirtualFileSystem &files, std::string_view language = "English")
{
	HeaderTemplates templates;
	const std::string path = "Data/" + std::string(language) + "/HeaderTemplate.ini";
	const auto text = files.ReadText(path);
	if (!text)
		return templates;
	engine::config::Diagnostics diagnostics;
	const auto grammar = ZeroHourIniGrammar(diagnostics);
	engine::config::Document document;
	engine::config::ini::Read(document, document.AddSource(path, *text), grammar, diagnostics);
	BindHeaderTemplates(document, templates);
	return templates;
}

// GameWindowManager::winCreateFromScript's END: a window naming a known template takes its font (name, point, bold) in
// place of its FONT; the point is then sized for the screen like any (HeaderTemplateManager::populateGameFonts:
// GlobalLanguage::adjustFontSize). `Window` is anything with font_name, font_size, font_bold and header_template.
template<typename Window>
void ApplyHeaderTemplates(std::span<Window> windows, const HeaderTemplates &templates)
{
	for (Window &window : windows)
		if (const HeaderTemplate *found = templates.Find(window.header_template))
		{
			window.font_name = found->fontName;
			window.font_size = static_cast<decltype(window.font_size)>(std::max(found->point, 0));
			window.font_bold = found->bold;
		}
}
}
