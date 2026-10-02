export module games.generalszh.hud.observer_panel;
import std;

export import engine.gui.mvvm.observable;
export import games.generalszh.session.session_view;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.ecs.query.query;

// The observer's control bar (the original's ControlBarObserver.cpp and ControlBar::setControlBarSchemeByPlayer): when the
// local player is not active (Player::isPlayerActive: an observer, IsObserver, or a player who was defeated) the bar
// shows the observer list (CB_CONTEXT_OBSERVER_LIST) in place of the command panels: a button per watched player (its
// PlayerTemplate's EnabledImage, its name as tooltip) with a label in its colour; pressing one shows that player's info
// window (units, buildings, units killed and lost, its name in its colour, its side's FlagWaterMark), refreshed every
// half second of logic frames; Cancel goes back to the list. The idle worker and beacon buttons hide, the general's
// button is disabled. Headless: the host feeds it the players and their figures.
export namespace generalszh::hud
{
inline constexpr std::size_t ObserverButtons = 8; // ControlBarObserver MAX_BUTTONS

// A player as the observer list sees it.
struct ObserverCandidate
{
	std::uint32_t player{0};
	std::string name;           // its map name (player<slot> for a game's seat)
	std::u16string displayName; // getPlayerDisplayName
	std::uint32_t color{0};     // getPlayerColor, 0xAARRGGBB
	std::string enabledImage;   // its PlayerTemplate's EnabledImage
	std::string flagImage;      // its PlayerTemplate's FlagWaterMark
	bool observer{false};       // isPlayerObserver
	bool human{false};          // PLAYER_HUMAN
	int team{-1};               // its game slot's team (-1: none)
	bool ai{false};             // its game slot is a computer's
};

struct ObserverListEntry
{
	std::uint32_t player{0};
	std::string image;
	std::u16string tooltip;
	std::u16string label;
	std::uint32_t color{0};
	bool operator==(const ObserverListEntry &) const = default;
};

// UnicodeString::format with string arguments: each %s / %ls in turn takes the next argument.
inline std::u16string FormatObserverLabel(std::u16string format, std::initializer_list<std::u16string_view> arguments)
{
	std::u16string out;
	auto next = arguments.begin();
	for (std::size_t at = 0; at < format.size(); ++at)
	{
		if (format[at] == u'%' && next != arguments.end())
		{
			if (at + 1 < format.size() && format[at + 1] == u's')
			{
				out += *next++;
				++at;
				continue;
			}
			if (at + 2 < format.size() && format[at + 1] == u'l' && format[at + 2] == u's')
			{
				out += *next++;
				at += 2;
				continue;
			}
		}
		out += format[at];
	}
	return out;
}

// ControlBar::populateObserverList. A multiplayer game (TheRecorder->isMultiplayer): player0 .. player7 that exist and
// are not observers, each "CONTROLBAR:ObsPlayerLabel" with its name and its slot's team ("Team:<n + 1>", "Team:AI" for a
// computer on no team). EA's code reads the team from the slot of the button's index, not the player's; the port reads
// the player's own slot (a retail bug, fixed as the SuperHackers fork does). Otherwise: the first human player who is not
// an observer, labelled with its name alone. At most ObserverButtons.
inline std::vector<ObserverListEntry> PopulateObserverList(std::span<const ObserverCandidate> players, bool multiplayer,
	const std::function<std::u16string(std::string_view)> &text)
{
	std::vector<ObserverListEntry> list;
	const auto entry = [](const ObserverCandidate &player) {
		ObserverListEntry shown;
		shown.player = player.player;
		shown.image = player.enabledImage;
		shown.tooltip = player.displayName;
		shown.color = player.color;
		return shown;
	};
	if (multiplayer)
	{
		for (std::size_t slot = 0; slot < ObserverButtons && list.size() < ObserverButtons; ++slot)
		{
			const std::string name = "player" + std::to_string(slot);
			const auto found = std::ranges::find_if(players, [&](const ObserverCandidate &player) { return player.name == name; });
			if (found == players.end() || found->observer)
				continue;
			ObserverListEntry shown = entry(*found);
			const std::string team = found->ai && found->team == -1 ? std::string("Team:AI") : "Team:" + std::to_string(found->team + 1);
			shown.label = FormatObserverLabel(text("CONTROLBAR:ObsPlayerLabel"), {found->displayName, text(team)});
			list.push_back(std::move(shown));
		}
		return list;
	}
	for (const ObserverCandidate &player : players)
		if (!player.observer && player.human)
		{
			ObserverListEntry shown = entry(player);
			shown.label = player.displayName;
			list.push_back(std::move(shown));
			break;
		}
	return list;
}

// What the info window shows of a player.
struct ObserverInfo
{
	std::int64_t units{0}, buildings{0}, unitsKilled{0}, unitsLost{0};
	std::u16string name;
	std::uint32_t color{0};
	std::string flag;
};

// ControlBar::populateObserverInfoWindow's counts (Player::countObjects(mask, clear): the player's objects having every
// kind of `mask` and none of `clear`): units SCORE but not STRUCTURE; buildings SCORE and STRUCTURE, plus SCORE_CREATE
// and STRUCTURE, plus SCORE_DESTROY and STRUCTURE (one with two of those counts twice); killed and lost from its
// ScoreKeeper (getTotalUnitsDestroyed / getTotalUnitsLost).
inline ObserverInfo ReadObserverInfo(session::SessionView &view, const ObserverCandidate &player)
{
	namespace gp = engine::gameplay;
	ObserverInfo info;
	info.name = player.displayName;
	info.color = player.color;
	info.flag = player.flagImage;
	ecs::Query<ecs::Read<gp::Owner>, ecs::Read<gp::DefinitionRef>> query(view.World());
	query.ForEachChunk([&](auto chunk) {
		const auto owners = chunk.template Get<gp::Owner>();
		const auto definitions = chunk.template Get<gp::DefinitionRef>();
		for (std::size_t row = 0; row < owners.size(); ++row)
		{
			if (owners[row].player != player.player)
				continue;
			const content::ObjectDefinition &definition = view.Definition(definitions[row].index);
			const bool structure = definition.Is("STRUCTURE");
			if (definition.Is("SCORE") && !structure)
				++info.units;
			if (structure)
				info.buildings += (definition.Is("SCORE") ? 1 : 0) + (definition.Is("SCORE_CREATE") ? 1 : 0) + (definition.Is("SCORE_DESTROY") ? 1 : 0);
		}
	});
	const std::vector<session::PlayerScore> scores = view.Scores();
	if (player.player < scores.size())
	{
		info.unitsKilled = scores[player.player].unitsDestroyed;
		info.unitsLost = scores[player.player].unitsLost;
	}
	return info;
}

// The panel's state and what its windows show. `players` gives the players as they are now; `info` a player's figures.
class ObserverPanelViewModel
{
public:
	ObserverPanelViewModel(std::function<std::vector<ObserverCandidate>()> players, std::function<std::optional<ObserverInfo>(std::uint32_t)> info,
		std::function<std::u16string(std::string_view)> text, bool multiplayer)
		: m_players(std::move(players)), m_info(std::move(info)), m_text(std::move(text)), m_multiplayer(multiplayer)
	{
		for (std::size_t button = 0; button < ObserverButtons; ++button)
			playerClicked[button].SetAction([this, button] { PressPlayer(button); });
		cancel.SetAction([this] { Cancel(); });
	}

	ObserverPanelViewModel(const ObserverPanelViewModel &) = delete;
	ObserverPanelViewModel &operator=(const ObserverPanelViewModel &) = delete;

	// ControlBar::reset, for a new game: no one looked at, the bar to be set afresh; `multiplayer`: TheRecorder->isMultiplayer.
	void Reset(bool multiplayer)
	{
		m_multiplayer = multiplayer;
		m_set = false;
		m_lookAt.reset();
		m_list.clear();
	}

	// ControlBar::setControlBarSchemeByPlayer: the local player inactive (an observer or defeated) switches the bar to the
	// observer list (switchToContext(CB_CONTEXT_OBSERVER_LIST): the list shown and filled, the info window hidden); active,
	// to no context (both hidden).
	void SetObserverBar(bool on)
	{
		if (on == observerBar.Get() && m_set)
			return;
		m_set = true;
		observerBar.Set(on);
		idleWorkerShown.Set(!on);
		generalEnabled.Set(!on);
		infoShown.Set(false);
		listShown.Set(on);
		if (on)
			PopulateList();
	}

	// ControlBar::update in observer mode: every LOGICFRAMES_PER_SECOND / 2 frames the info window is filled again.
	void Update(std::uint64_t frame)
	{
		if (observerBar.Get() && frame % 15 == 0)
			PopulateInfo();
	}

	// The player whose info shows (m_observerLookAtPlayer); none: none.
	std::optional<std::uint32_t> LookAt() const noexcept { return m_lookAt; }

	engine::gui::mvvm::Observable<bool> observerBar{false};
	engine::gui::mvvm::Observable<bool> listShown{false}, infoShown{false};
	engine::gui::mvvm::Observable<bool> idleWorkerShown{true}; // ButtonIdleWorker hidden for an observer
	engine::gui::mvvm::Observable<bool> generalEnabled{true};  // ButtonGeneral disabled for an observer
	std::array<engine::gui::mvvm::Observable<bool>, ObserverButtons> buttonShown;
	std::array<engine::gui::mvvm::Observable<std::string>, ObserverButtons> buttonImage;
	std::array<engine::gui::mvvm::Observable<std::u16string>, ObserverButtons> buttonTooltip;
	std::array<engine::gui::mvvm::Observable<std::u16string>, ObserverButtons> label;
	std::array<engine::gui::mvvm::Observable<std::uint32_t>, ObserverButtons> labelColor;
	std::array<engine::gui::mvvm::Command, ObserverButtons> playerClicked;
	engine::gui::mvvm::Command cancel;
	engine::gui::mvvm::Observable<std::u16string> units, buildings, unitsKilled, unitsLost, name;
	engine::gui::mvvm::Observable<std::uint32_t> nameColor{0};
	engine::gui::mvvm::Observable<std::string> flag;
	engine::gui::mvvm::Observable<bool> portraitShown{false}; // WinGeneralPortrait

private:
	static std::u16string Number(std::int64_t value)
	{
		const std::string text = std::to_string(value);
		return std::u16string(text.begin(), text.end());
	}

	void PopulateList()
	{
		const std::vector<ObserverCandidate> players = m_players ? m_players() : std::vector<ObserverCandidate>{};
		m_list = PopulateObserverList(players, m_multiplayer, m_text);
		for (std::size_t button = 0; button < ObserverButtons; ++button)
		{
			const bool shown = button < m_list.size();
			buttonShown[button].Set(shown);
			if (!shown)
				continue;
			const ObserverListEntry &entry = m_list[button];
			buttonImage[button].Set(entry.image);
			buttonTooltip[button].Set(entry.tooltip);
			label[button].Set(entry.label);
			labelColor[button].Set(entry.color);
		}
	}

	// ControlBarObserverSystem, a player's button: the info window shown, the list hidden, that player looked at.
	void PressPlayer(std::size_t button)
	{
		infoShown.Set(true);
		listShown.Set(false);
		m_lookAt = button < m_list.size() ? std::optional(m_list[button].player) : std::nullopt;
		if (m_lookAt)
			PopulateInfo();
	}

	// ButtonCancel: no one looked at, the info window hidden, the list shown and filled again.
	void Cancel()
	{
		m_lookAt.reset();
		infoShown.Set(false);
		listShown.Set(true);
		PopulateList();
	}

	void PopulateInfo()
	{
		if (!infoShown.Get())
			return;
		const std::optional<ObserverInfo> shown = m_lookAt && m_info ? m_info(*m_lookAt) : std::nullopt;
		if (!shown)
		{
			infoShown.Set(false);
			listShown.Set(true);
			PopulateList();
			return;
		}
		units.Set(Number(shown->units));
		buildings.Set(Number(shown->buildings));
		unitsKilled.Set(Number(shown->unitsKilled));
		unitsLost.Set(Number(shown->unitsLost));
		name.Set(shown->name);
		nameColor.Set(shown->color);
		flag.Set(shown->flag);
		portraitShown.Set(true);
	}

	std::function<std::vector<ObserverCandidate>()> m_players;
	std::function<std::optional<ObserverInfo>(std::uint32_t)> m_info;
	std::function<std::u16string(std::string_view)> m_text;
	bool m_multiplayer{true};
	bool m_set{false};
	std::vector<ObserverListEntry> m_list;
	std::optional<std::uint32_t> m_lookAt;
};
}
