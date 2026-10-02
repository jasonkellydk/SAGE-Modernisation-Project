export module games.generalszh.content.input.command_map;
import std;

export import engine.config.binding.schema;

// CommandMap.ini (MetaEvent.cpp: MetaMap::parseMetaMap with TheMetaMapFieldParseTable): which key, transition and
// modifiers raise each meta-event, where it is usable, and the keyboard menu's category, description and name. The
// original loads Data\<language>\CommandMap.ini, then Data\INI\CommandMap.ini (GameEngine::init: TheMetaMap); a
// meta-event named again updates its record in place, a new one goes to the front of the list (getMetaMapRec), so the
// list holds the meta-events last-defined-first, which is the order MetaEventTranslator tries them in.
export namespace generalszh::content
{
// MetaEvent.h MappableKeyType: the keys that may be mapped, by their CommandMap.ini names (KeyNames, less "KEY_").
// `Other` is any key outside the set (a modifier key, say): it raises nothing but its modifiers' change.
enum class MappableKey : std::uint8_t
{
	KP0, KP1, KP2, KP3, KP4, KP5, KP6, KP7, KP8, KP9, KPDEL, KPSTAR, KPMINUS, KPPLUS, KPENTER, KPSLASH,
	ESC, BACKSPACE, ENTER, SPACE, TAB,
	F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
	A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
	K1, K2, K3, K4, K5, K6, K7, K8, K9, K0,
	MINUS, EQUAL, LBRACKET, RBRACKET, SEMICOLON, APOSTROPHE, TICK, BACKSLASH, COMMA, PERIOD, SLASH,
	UP, DOWN, LEFT, RIGHT, HOME, END, PGUP, PGDN, INS, DEL,
	NONE,  // KEY_NONE: a record raised by its modifiers alone
	Other, // not mappable
};

// MappableKeyTransition.
enum class MetaTransition : std::uint8_t
{
	Down,
	Up,
	DoubleDown, // parsed; the translator's DOUBLEDOWN test is commented out in the original
};

// MappableKeyModState, as bits.
namespace meta_modifier
{
inline constexpr std::uint8_t None = 0;
inline constexpr std::uint8_t Ctrl = 1u << 0;
inline constexpr std::uint8_t Alt = 1u << 1;
inline constexpr std::uint8_t Shift = 1u << 2;
}

// CommandUsableInType (TheCommandUsableInNames: SHELL, GAME).
namespace command_usable
{
inline constexpr std::uint32_t None = 0;
inline constexpr std::uint32_t Shell = 1u << 0;
inline constexpr std::uint32_t Game = 1u << 1;
}

// MappableKeyCategories.
enum class MetaCategory : std::uint8_t
{
	Control,
	Information,
	Interface,
	Selection,
	Taunt,
	Team,
	Misc,
	Debug,
};

struct MetaMapRecord
{
	std::string meta; // the meta-event (GameMessageMetaTypeNames' name)
	MappableKey key{MappableKey::NONE};
	MetaTransition transition{MetaTransition::Down};
	std::uint8_t modifiers{meta_modifier::None};
	std::uint32_t usableIn{command_usable::None};
	MetaCategory category{MetaCategory::Misc};
	std::string description; // labels (parseAndTranslateLabel)
	std::string displayName;
};

struct CommandMap
{
	std::vector<MetaMapRecord> records; // MetaMap::m_metaMaps, in its order

	const MetaMapRecord *Find(std::string_view meta) const noexcept
	{
		for (const MetaMapRecord &record : records)
			if (record.meta == meta)
				return &record;
		return nullptr;
	}
};

// GameMessageMetaTypeNames in a release build (the cheats and _DEBUG / _INTERNAL demo names left out).
inline constexpr std::array<std::string_view, 104> MetaEventNames{
	"SAVE_VIEW1", "SAVE_VIEW2", "SAVE_VIEW3", "SAVE_VIEW4", "SAVE_VIEW5", "SAVE_VIEW6", "SAVE_VIEW7", "SAVE_VIEW8",
	"VIEW_VIEW1", "VIEW_VIEW2", "VIEW_VIEW3", "VIEW_VIEW4", "VIEW_VIEW5", "VIEW_VIEW6", "VIEW_VIEW7", "VIEW_VIEW8",
	"CREATE_TEAM0", "CREATE_TEAM1", "CREATE_TEAM2", "CREATE_TEAM3", "CREATE_TEAM4", "CREATE_TEAM5", "CREATE_TEAM6", "CREATE_TEAM7",
	"CREATE_TEAM8", "CREATE_TEAM9",
	"SELECT_TEAM0", "SELECT_TEAM1", "SELECT_TEAM2", "SELECT_TEAM3", "SELECT_TEAM4", "SELECT_TEAM5", "SELECT_TEAM6", "SELECT_TEAM7",
	"SELECT_TEAM8", "SELECT_TEAM9",
	"ADD_TEAM0", "ADD_TEAM1", "ADD_TEAM2", "ADD_TEAM3", "ADD_TEAM4", "ADD_TEAM5", "ADD_TEAM6", "ADD_TEAM7", "ADD_TEAM8", "ADD_TEAM9",
	"VIEW_TEAM0", "VIEW_TEAM1", "VIEW_TEAM2", "VIEW_TEAM3", "VIEW_TEAM4", "VIEW_TEAM5", "VIEW_TEAM6", "VIEW_TEAM7", "VIEW_TEAM8",
	"VIEW_TEAM9",
	"SELECT_MATCHING_UNITS", "SELECT_NEXT_UNIT", "SELECT_PREV_UNIT", "SELECT_NEXT_WORKER", "SELECT_PREV_WORKER", "SELECT_HERO",
	"SELECT_ALL", "SELECT_ALL_AIRCRAFT", "VIEW_COMMAND_CENTER", "VIEW_LAST_RADAR_EVENT", "SCATTER", "STOP", "DEPLOY",
	"CREATE_FORMATION", "FOLLOW", "CHAT_PLAYERS", "CHAT_ALLIES", "CHAT_EVERYONE", "DIPLOMACY", "PLACE_BEACON", "DELETE_BEACON",
	"OPTIONS", "TOGGLE_LOWER_DETAILS", "TOGGLE_CONTROL_BAR", "BEGIN_PATH_BUILD", "END_PATH_BUILD", "BEGIN_FORCEATTACK",
	"END_FORCEATTACK", "BEGIN_FORCEMOVE", "END_FORCEMOVE", "BEGIN_WAYPOINTS", "END_WAYPOINTS", "BEGIN_PREFER_SELECTION",
	"END_PREFER_SELECTION", "TAKE_SCREENSHOT", "ALL_CHEER", "BEGIN_CAMERA_ROTATE_LEFT", "END_CAMERA_ROTATE_LEFT",
	"BEGIN_CAMERA_ROTATE_RIGHT", "END_CAMERA_ROTATE_RIGHT", "BEGIN_CAMERA_ZOOM_IN", "END_CAMERA_ZOOM_IN", "BEGIN_CAMERA_ZOOM_OUT",
	"END_CAMERA_ZOOM_OUT", "CAMERA_RESET", "TOGGLE_CAMERA_TRACKING_DRAWABLE", "TOGGLE_FAST_FORWARD_REPLAY", "DEMO_INSTANT_QUIT"};

// INI lookup names (stricmp, as scanLookupList).
namespace detail
{
inline bool SameName(std::string_view a, std::string_view b) noexcept
{
	const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
	return a.size() == b.size() && std::ranges::equal(a, b, [&](char x, char y) { return lower(x) == lower(y); });
}

inline constexpr std::array<std::string_view, 91> KeyNames{"KEY_KP0", "KEY_KP1", "KEY_KP2", "KEY_KP3", "KEY_KP4", "KEY_KP5", "KEY_KP6",
	"KEY_KP7", "KEY_KP8", "KEY_KP9", "KEY_KPDEL", "KEY_KPSTAR", "KEY_KPMINUS", "KEY_KPPLUS", "KEY_KPENTER", "KEY_KPSLASH", "KEY_ESC",
	"KEY_BACKSPACE", "KEY_ENTER", "KEY_SPACE", "KEY_TAB", "KEY_F1", "KEY_F2", "KEY_F3", "KEY_F4", "KEY_F5", "KEY_F6", "KEY_F7", "KEY_F8",
	"KEY_F9", "KEY_F10", "KEY_F11", "KEY_F12", "KEY_A", "KEY_B", "KEY_C", "KEY_D", "KEY_E", "KEY_F", "KEY_G", "KEY_H", "KEY_I", "KEY_J",
	"KEY_K", "KEY_L", "KEY_M", "KEY_N", "KEY_O", "KEY_P", "KEY_Q", "KEY_R", "KEY_S", "KEY_T", "KEY_U", "KEY_V", "KEY_W", "KEY_X", "KEY_Y",
	"KEY_Z", "KEY_1", "KEY_2", "KEY_3", "KEY_4", "KEY_5", "KEY_6", "KEY_7", "KEY_8", "KEY_9", "KEY_0", "KEY_MINUS", "KEY_EQUAL",
	"KEY_LBRACKET", "KEY_RBRACKET", "KEY_SEMICOLON", "KEY_APOSTROPHE", "KEY_TICK", "KEY_BACKSLASH", "KEY_COMMA", "KEY_PERIOD", "KEY_SLASH",
	"KEY_UP", "KEY_DOWN", "KEY_LEFT", "KEY_RIGHT", "KEY_HOME", "KEY_END", "KEY_PGUP", "KEY_PGDN", "KEY_INS", "KEY_DEL", "KEY_NONE"};
static_assert(KeyNames.size() == static_cast<std::size_t>(MappableKey::NONE) + 1);

inline constexpr std::array<std::pair<std::string_view, std::uint8_t>, 8> ModifierNames{{{"NONE", meta_modifier::None},
	{"CTRL", meta_modifier::Ctrl}, {"ALT", meta_modifier::Alt}, {"SHIFT", meta_modifier::Shift},
	{"CTRL_ALT", meta_modifier::Ctrl | meta_modifier::Alt}, {"SHIFT_CTRL", meta_modifier::Shift | meta_modifier::Ctrl},
	{"SHIFT_ALT", meta_modifier::Shift | meta_modifier::Alt},
	{"SHIFT_ALT_CTRL", meta_modifier::Shift | meta_modifier::Alt | meta_modifier::Ctrl}}};

inline constexpr std::array<std::string_view, 8> CategoryNames{"CONTROL", "INFORMATION", "INTERFACE", "SELECTION", "TAUNT", "TEAM",
	"MISC", "DEBUG"};

template <std::size_t N>
std::optional<std::size_t> IndexOf(const std::array<std::string_view, N> &names, std::string_view token) noexcept
{
	for (std::size_t index = 0; index < N; ++index)
		if (SameName(names[index], token))
			return index;
	return std::nullopt;
}
}

// The CommandMap.ini name of a key ("KEY_F1").
inline std::string_view MappableKeyName(MappableKey key) noexcept
{
	const auto index = static_cast<std::size_t>(key);
	return index < detail::KeyNames.size() ? detail::KeyNames[index] : std::string_view("KEY_???");
}

// INI::parseMetaMapDefinition for each CommandMap block of `document`, onto `map`.
inline void BindCommandMap(const engine::config::Document &document, CommandMap &map, engine::config::BindContext &context)
{
	using namespace engine::config;
	for (const Node &root : document.Roots())
	{
		if (root.key != "CommandMap")
			continue;
		const std::string_view name = root.Value();
		// findGameMessageMetaType: an unknown name is INI_INVALID_DATA (the block is not read).
		const auto known = detail::IndexOf(MetaEventNames, name);
		if (!known)
		{
			context.diagnostics.Error(root.location, "CommandMap '" + std::string(name) + "' is not a meta-event");
			continue;
		}
		const std::string meta(MetaEventNames[*known]);
		// getMetaMapRec: the record for the meta-event, else a new one at the front with its defaults.
		auto found = std::ranges::find(map.records, meta, &MetaMapRecord::meta);
		if (found == map.records.end())
		{
			map.records.insert(map.records.begin(), MetaMapRecord{meta});
			found = map.records.begin();
		}
		MetaMapRecord &record = *found;
		for (const Node &field : root.children)
		{
			const std::string_view key = field.key;
			const std::string_view token = field.Value();
			const auto invalid = [&] { context.diagnostics.Error(field.location, "'" + std::string(key) + "': unknown value '" + std::string(token) + "'"); };
			if (key == "Key")
			{
				if (const auto index = detail::IndexOf(detail::KeyNames, token))
					record.key = static_cast<MappableKey>(*index);
				else
					invalid();
			}
			else if (key == "Transition")
			{
				if (detail::SameName(token, "DOWN"))
					record.transition = MetaTransition::Down;
				else if (detail::SameName(token, "UP"))
					record.transition = MetaTransition::Up;
				else if (detail::SameName(token, "DOUBLEDOWN"))
					record.transition = MetaTransition::DoubleDown;
				else
					invalid();
			}
			else if (key == "Modifiers")
			{
				const auto modifier = std::ranges::find_if(detail::ModifierNames, [&](const auto &entry) { return detail::SameName(entry.first, token); });
				if (modifier != detail::ModifierNames.end())
					record.modifiers = modifier->second;
				else
					invalid();
			}
			else if (key == "UseableIn")
			{
				// INI::parseBitString32 over SHELL, GAME (NONE clears; +X / -X adjust).
				std::uint32_t bits = record.usableIn;
				bool normal = false;
				for (const std::string_view value : field.values)
				{
					if (detail::SameName(value, "NONE"))
					{
						bits = 0;
						break;
					}
					const bool add = value.starts_with('+');
					const bool remove = value.starts_with('-');
					const std::string_view flag = add || remove ? value.substr(1) : value;
					const std::uint32_t bit = detail::SameName(flag, "SHELL") ? command_usable::Shell
						: detail::SameName(flag, "GAME")                     ? command_usable::Game
																			 : 0u;
					if (bit == 0)
					{
						context.diagnostics.Error(field.location, "'UseableIn': unknown '" + std::string(value) + "'");
						continue;
					}
					if (add)
						bits |= bit;
					else if (remove)
						bits &= ~bit;
					else
					{
						if (!normal)
							bits = 0;
						bits |= bit;
						normal = true;
					}
				}
				record.usableIn = bits;
			}
			else if (key == "Category")
			{
				if (const auto index = detail::IndexOf(detail::CategoryNames, token))
					record.category = static_cast<MetaCategory>(*index);
				else
					invalid();
			}
			else if (key == "Description")
				record.description = std::string(token);
			else if (key == "DisplayName")
				record.displayName = std::string(token);
		}
	}
}
}
