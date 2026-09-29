export module games.generalszh.content.loading.content_loader;
import std;

export import engine.config.adapters.ini.ini_reader;
export import engine.filesystem.core.virtual_file_system;

export namespace generalszh::content
{
// Loads INI content sets the way the original game names them: a set path
// such as "Data\INI\Object" means the file "Data\INI\Object.ini" (if any)
// followed by every "*.ini" under "Data\INI\Object\" in sorted order. A
// "Default" set is loaded before the main one so its definitions come first.
class ContentLoader
{
public:
	ContentLoader(const engine::filesystem::VirtualFileSystem &files, engine::config::ini::Grammar grammar) :
		m_files(files), m_grammar(std::move(grammar))
	{
	}

	// Parses the sets into one new document (kept alive by the loader, so
	// definitions may refer to its nodes) and returns it.
	// The mounted install the content comes from (models, for their bones).
	const engine::filesystem::VirtualFileSystem &Files() const noexcept { return m_files; }

	const engine::config::Document &Load(std::vector<std::string_view> sets)
	{
		auto loaded = std::make_unique<Loaded>();
		for (const std::string_view set : sets)
		{
			const std::string file = std::string(set) + ".ini";
			if (m_files.Exists(file))
				Parse(*loaded, file);
			for (const std::string &path : m_files.List(set, ".ini"))
				Parse(*loaded, path);
		}
		m_loaded.push_back(std::move(loaded));
		return m_loaded.back()->document;
	}

	// Diagnostics of the document returned by Load (binding may add more).
	engine::config::Diagnostics &DiagnosticsFor(const engine::config::Document &document)
	{
		for (auto &loaded : m_loaded)
			if (&loaded->document == &document)
				return loaded->diagnostics;
		throw std::logic_error("document was not loaded by this ContentLoader");
	}

	std::size_t ErrorCount() const noexcept
	{
		std::size_t errors = 0;
		for (const auto &loaded : m_loaded)
			errors += loaded->diagnostics.ErrorCount();
		return errors;
	}

	std::string Report() const
	{
		std::string out;
		for (const auto &loaded : m_loaded)
			out += loaded->diagnostics.Format(loaded->document);
		return out;
	}

private:
	struct Loaded
	{
		engine::config::Document document;
		engine::config::Diagnostics diagnostics;
	};

	void Parse(Loaded &loaded, const std::string &path)
	{
		auto text = m_files.ReadText(path);
		if (!text)
			return;
		engine::config::ini::Read(loaded.document, loaded.document.AddSource(path, std::move(*text)), m_grammar, loaded.diagnostics);
	}

	const engine::filesystem::VirtualFileSystem &m_files;
	engine::config::ini::Grammar m_grammar;
	std::vector<std::unique_ptr<Loaded>> m_loaded;
};
}
