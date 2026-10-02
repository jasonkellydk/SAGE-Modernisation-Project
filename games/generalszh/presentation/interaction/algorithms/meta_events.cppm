export module games.generalszh.presentation.interaction.algorithms.meta_events;
import std;

export import games.generalszh.content.input.command_map;

// MetaEvent.cpp MetaEventTranslator::translateGameMessage for a raw key message: the CommandMap.ini record it raises
// (MSG_META_*), tried in the map's order, the first that fits taking the key (DESTROY_MESSAGE).
export namespace generalszh::presentation
{
// The translator's memory between keys: the last key let down and the modifiers it last saw.
struct MetaKeyState
{
	content::MappableKey lastKeyDown{content::MappableKey::NONE};
	std::uint8_t lastModifiers{content::meta_modifier::None};
};

// MSG_RAW_KEY_DOWN / MSG_RAW_KEY_UP: the key, its transition, whether it repeats (KEY_STATE_AUTOREPEAT) and the
// modifiers held with it (KEY_STATE_CONTROL / SHIFT / ALT, left and right alike).
struct MetaKeyInput
{
	content::MappableKey key{content::MappableKey::Other};
	bool down{true};
	bool autoRepeat{false};
	std::uint8_t modifiers{content::meta_modifier::None};
};

// Where the key comes: the shell active (TheShell->isShellActive), the game client's frame (a GAME-only record waits
// for frame 1), and a replay playing (TOGGLE_FAST_FORWARD_REPLAY's).
struct MetaKeyContext
{
	bool shellActive{false};
	std::uint32_t clientFrame{0};
	bool replay{false};
};

struct MetaTranslation
{
	const content::MetaMapRecord *raised{nullptr}; // the meta-event appended to the stream (none: nothing)
	bool consumed{false};                          // DESTROY_MESSAGE: the raw key goes no further
	bool toggleFastForward{false};                 // TOGGLE_FAST_FORWARD_REPLAY in a replay: GlobalData m_TiVOFastMode flips
};

inline MetaTranslation TranslateMetaKey(const content::CommandMap &map, MetaKeyState &state, const MetaKeyInput &input, const MetaKeyContext &context)
{
	using content::MappableKey;
	using content::MetaTransition;
	namespace usable = content::command_usable;
	MetaTranslation result;
	const std::uint8_t modifiers = input.modifiers;
	for (const content::MetaMapRecord &record : map.records)
	{
		// A GAME-only command waits for the game client's first frame.
		if (record.usableIn == usable::Game && context.clientFrame < 1)
			continue;
		if (context.shellActive && (record.usableIn & usable::Shell) == 0)
			continue;
		if (!context.shellActive && (record.usableIn & usable::Game) == 0)
			continue;
		// The modifiers alone changed: a KEY_NONE record for the new ones (DOWN) or the ones let go (UP).
		if (record.key == MappableKey::NONE && modifiers != state.lastModifiers &&
			((record.transition == MetaTransition::Up && record.modifiers == state.lastModifiers) ||
				(record.transition == MetaTransition::Down && record.modifiers == modifiers)))
		{
			result.raised = &record;
			result.consumed = true;
			break;
		}
		// A key's own transition with exactly these modifiers (DOUBLEDOWN is never tested).
		if (record.key == input.key && record.modifiers == modifiers &&
			((record.transition == MetaTransition::Up && !input.down) || (record.transition == MetaTransition::Down && input.down)))
		{
			if (input.autoRepeat)
			{
				// Eaten, raising nothing.
				result.consumed = true;
				break;
			}
			if (record.meta == "TOGGLE_FAST_FORWARD_REPLAY")
			{
				// Handled here (the translator is off in cinematics), and only in a replay; the key goes on.
				result.toggleFastForward = context.replay;
				break;
			}
			result.raised = &record;
			result.consumed = true;
			break;
		}
	}
	if (input.down)
		state.lastKeyDown = input.key;
	state.lastModifiers = modifiers;
	return result;
}
}
