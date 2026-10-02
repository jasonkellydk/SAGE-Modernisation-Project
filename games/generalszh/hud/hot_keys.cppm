export module games.generalszh.hud.hot_keys;
import std;

// The control bar's hotkeys (the original's HotKeyManager: searchHotKey / addHotKey / executeHotKey, fed by every
// ControlBar::setControlCommand): pure data and free functions, no windows. Each view model keeps the keys of the
// windows it populates; the host asks them in the order the original registers them.
export namespace generalszh::hud
{
// HotKeyManager::searchHotKey: the character after the first '&' in a label's text (the key a button's TextLabel marks),
// lower case as addHotKey keeps it; none: 0. (AsciiString::translate keeps only what fits a char.)
inline char HotKeyOf(std::u16string_view text) noexcept
{
	const std::size_t at = text.find(u'&');
	if (at == std::u16string_view::npos || at + 1 >= text.size() || text[at + 1] > 0x7F)
		return 0;
	return static_cast<char>(std::tolower(static_cast<unsigned char>(text[at + 1])));
}

// HotKeyManager's map: a key to the window registered for it, the first registration kept (addHotKey ignores a key
// already mapped).
struct HotKeys
{
	std::vector<std::pair<char, std::size_t>> keys;

	void Clear() noexcept { keys.clear(); }
	void Add(char key, std::size_t window)
	{
		if (key != 0 && std::ranges::none_of(keys, [key](const auto &entry) { return entry.first == key; }))
			keys.emplace_back(key, window);
	}
	// executeHotKey's lookup: the key lower-cased.
	std::optional<std::size_t> Find(char key) const noexcept
	{
		const char wanted = static_cast<char>(std::tolower(static_cast<unsigned char>(key)));
		for (const auto &[hotKey, window] : keys)
			if (hotKey == wanted)
				return window;
		return std::nullopt;
	}
};

// What HotKeyManager::executeHotKey does with a mapped window: hidden, nothing; enabled, it is clicked with a GUIClick
// and the key is used (true); disabled, a GUIClickDisabled and the key goes on (false).
template <class Click, class Sound>
bool ExecuteHotKey(bool shown, bool enabled, Click &&click, Sound &&sound)
{
	if (!shown)
		return false;
	if (enabled)
	{
		click();
		sound("GUIClick");
		return true;
	}
	sound("GUIClickDisabled");
	return false;
}

// The one map the original keeps, asked through the view models in the order their windows register: the first that
// maps the key acts on it (the later ones' registrations of the same key were ignored).
template <class... Bars>
bool PressFirstHotKey(char key, Bars &...bars)
{
	bool found = false;
	bool used = false;
	(((!found && bars.HasHotKey(key)) ? (found = true, used = bars.PressHotKey(key), 0) : 0), ...);
	return used;
}
}
