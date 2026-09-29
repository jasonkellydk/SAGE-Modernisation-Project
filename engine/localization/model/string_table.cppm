export module engine.localization.model.string_table;
import std;

export namespace engine::localization
{
struct LocalizedString
{
	std::string text;   // UTF-8
	std::string speech; // optional voice asset name
};

// Localized text by label ("GUI:SinglePlayer"). Labels match
// case-insensitively, as in the original lookup. Format adapters fill it.
class StringTable
{
public:
	void Set(std::string_view label, LocalizedString value) { m_entries.insert_or_assign(Key(label), std::move(value)); }

	const LocalizedString *Find(std::string_view label) const
	{
		const auto found = m_entries.find(Key(label));
		return found == m_entries.end() ? nullptr : &found->second;
	}

	// The text, or the label itself when missing (the original shows
	// "MISSING: '<label>'"; showing the label keeps layouts readable).
	std::string_view Text(std::string_view label) const
	{
		const LocalizedString *entry = Find(label);
		return entry != nullptr ? std::string_view(entry->text) : label;
	}

	std::size_t Size() const noexcept { return m_entries.size(); }

private:
	static std::string Key(std::string_view label)
	{
		std::string key(label);
		for (char &c : key)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return key;
	}

	std::map<std::string, LocalizedString, std::less<>> m_entries;
};
}
