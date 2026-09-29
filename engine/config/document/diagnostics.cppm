export module engine.config.document.diagnostics;
import std;

import engine.config.document.document;

export namespace engine::config
{
enum class Severity
{
	Warning,
	Error
};

struct Diagnostic
{
	Severity severity{Severity::Error};
	SourceLocation location;
	std::string message;
};

// Collects every problem instead of stopping at the first, so one load
// reports all broken content with file:line.
class Diagnostics
{
public:
	void Error(SourceLocation location, std::string message)
	{
		m_entries.push_back({Severity::Error, location, std::move(message)});
		++m_errors;
	}

	void Warning(SourceLocation location, std::string message)
	{
		m_entries.push_back({Severity::Warning, location, std::move(message)});
	}

	bool HasErrors() const noexcept { return m_errors != 0; }
	std::size_t ErrorCount() const noexcept { return m_errors; }
	const std::vector<Diagnostic> &Entries() const noexcept { return m_entries; }

	std::string Format(const Document &document) const
	{
		std::string out;
		for (const Diagnostic &entry : m_entries)
		{
			out += document.Describe(entry.location);
			out += entry.severity == Severity::Error ? ": error: " : ": warning: ";
			out += entry.message;
			out += '\n';
		}
		return out;
	}

private:
	std::vector<Diagnostic> m_entries;
	std::size_t m_errors{0};
};
}
