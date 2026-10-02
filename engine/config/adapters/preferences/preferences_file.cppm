export module engine.config.adapters.preferences.preferences_file;
import std;

// A preferences file of "key = value" lines (the format of the original's
// UserPreferences: Options.ini, Skirmish.ini, Network.ini): read leniently
// (blank keys or values are dropped, the last of a repeated key wins), kept
// sorted by key and written back one "key = value" line each, in key order.
export namespace engine::config
{
class Preferences
{
public:
	static Preferences Parse(std::string_view text)
	{
		Preferences preferences;
		while (!text.empty())
		{
			const auto end = text.find('\n');
			std::string_view line = text.substr(0, end);
			text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
			const auto equals = line.find('=');
			if (equals == std::string_view::npos)
				continue;
			const std::string_view key = Trim(line.substr(0, equals)), value = Trim(line.substr(equals + 1));
			if (!key.empty() && !value.empty())
				preferences.Set(key, value);
		}
		return preferences;
	}

	std::string Write() const
	{
		std::string text;
		for (const auto &[key, value] : m_entries)
			text += key + " = " + value + "\n";
		return text;
	}

	std::optional<std::string_view> Find(std::string_view key) const noexcept
	{
		const auto found = Lower(key);
		return found != m_entries.end() && found->first == key ? std::optional<std::string_view>(found->second) : std::nullopt;
	}

	void Set(std::string_view key, std::string_view value)
	{
		const auto found = Lower(key);
		if (found != m_entries.end() && found->first == key)
			found->second = std::string(value);
		else
			m_entries.insert(found, {std::string(key), std::string(value)});
	}

	void Set(std::string_view key, std::int64_t value) { Set(key, std::to_string(value)); }
	void SetBool(std::string_view key, bool value) { Set(key, value ? "yes" : "no"); }

	// As UserPreferences::getBool: 1, t, true, y, yes or ok (any case) are true.
	bool Flag(std::string_view key, bool fallback) const
	{
		const auto value = Find(key);
		if (!value)
			return fallback;
		std::string lower(*value);
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
		return lower == "1" || lower == "t" || lower == "true" || lower == "y" || lower == "yes" || lower == "ok";
	}

	// As atoi: the leading integer (0 when there is none).
	std::int64_t Number(std::string_view key, std::int64_t fallback) const
	{
		const auto value = Find(key);
		if (!value)
			return fallback;
		std::int64_t result = 0;
		const char *begin = value->data();
		if (!value->empty() && *begin == '+')
			++begin;
		std::from_chars(begin, value->data() + value->size(), result);
		return result;
	}

	std::size_t Size() const noexcept { return m_entries.size(); }
	// Ordered, read-only enumeration for consumers such as content unlock lists.
	// Mutating the preferences invalidates this view.
	std::span<const std::pair<std::string,std::string>> Entries() const noexcept { return m_entries; }

private:
	static std::string_view Trim(std::string_view text) noexcept
	{
		while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
			text.remove_prefix(1);
		while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
			text.remove_suffix(1);
		return text;
	}

	std::vector<std::pair<std::string, std::string>>::iterator Lower(std::string_view key)
	{
		return std::lower_bound(m_entries.begin(), m_entries.end(), key, [](const auto &entry, std::string_view name) { return entry.first < name; });
	}
	std::vector<std::pair<std::string, std::string>>::const_iterator Lower(std::string_view key) const
	{
		return std::lower_bound(m_entries.begin(), m_entries.end(), key, [](const auto &entry, std::string_view name) { return entry.first < name; });
	}

	std::vector<std::pair<std::string, std::string>> m_entries; // sorted by key
};
}
