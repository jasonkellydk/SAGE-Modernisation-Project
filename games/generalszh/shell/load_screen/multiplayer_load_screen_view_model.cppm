export module games.generalszh.shell.load_screen.multiplayer_load_screen_view_model;
import std;

export import engine.gui.mvvm.observable;
export import games.generalszh.session.setup.game_setup;
export import games.generalszh.shell.game_setup.setup_catalog;
export import games.generalszh.shell.skirmish.skirmish_view_model;

// The skirmish and LAN load screen (LoadScreen.cpp MultiPlayerLoadScreen, Zero Hour's): Menus/MultiplayerLoadScreen.wnd
// with the local player's general (portrait, name, features), one row per player in slot order (name and side in its
// house colour, its team, a progress bar in its colour that a computer player does not have), the map's preview with
// each player's number on its start spot, and the local player's side's load music.
//
// What another player chose at random stays "Random" to a player not on its team (GameSlot::getApparent*,
// Multiplayer.ini ShowRandomPlayerTemplate / ShowRandomColor / ShowRandomStartPos); the local player and its allies
// show as resolved.
export namespace generalszh::shell
{
namespace setup = session::setup;

// A PlayerTemplate as the load screen shows it (its general from ChallengeMode.ini when it has one).
struct LoadScreenFaction
{
	std::string name;           // FactionAmerica
	std::u16string displayName; // DisplayName, localized
	std::string features;       // Features: a label (GUI:BioFeatures_USA)
	std::string loadScreenMusic;
	bool general{false};        // a GeneralPersona plays it
	std::string generalPortrait; // its BioPortraitLarge
	std::string generalName;     // its BioName: a label
};

struct MultiplayerLoadScreenSetup
{
	setup::GameSetup chosen;                    // as set up: random choices not resolved (GameSlot's m_orig*)
	std::array<int, setup::MaxSlots> playerTemplate{}; // as resolved (populateRandomSideAndColor)
	std::array<int, setup::MaxSlots> color{};          // as resolved
	std::array<int, setup::MaxSlots> startPos{};       // as resolved (populateRandomStartPosition)
	std::array<std::u16string, setup::MaxSlots> names; // GameSlot::getName (a computer player's by its level)
	int localSlot{0};
	std::vector<LoadScreenFaction> factions;    // by template index
	int observerTemplate{-1};                   // FactionObserver's index
	std::vector<std::uint32_t> colors;          // MultiplayerColor's, ARGB
	const SetupMap *map{nullptr};               // the MapCache's entry (none: not known)
	std::function<std::u16string(std::string_view)> text;     // Generals.csf
	std::function<bool(std::string_view)> imageKnown;         // a mapped image by name exists
	// Multiplayer.ini's ShowRandomPlayerTemplate / ShowRandomColor / ShowRandomStartPos.
	bool showRandomTemplate{true}, showRandomColor{true}, showRandomStart{true};
};

inline constexpr std::uint32_t LoadScreenNeutralColor = 0xFFFFFFFF; // MultiplayerSettings' random and observer colours

struct LoadScreenRow
{
	engine::gui::mvvm::Observable<bool> shown{false};
	engine::gui::mvvm::Observable<std::u16string> name;
	engine::gui::mvvm::Observable<std::u16string> side;
	engine::gui::mvvm::Observable<std::u16string> team;
	engine::gui::mvvm::Observable<std::uint32_t> color{LoadScreenNeutralColor};
	engine::gui::mvvm::Observable<bool> barShown{false};
	engine::gui::mvvm::Observable<std::string> barImage;
	engine::gui::mvvm::Observable<int> progress{0};
};

class MultiplayerLoadScreenViewModel
{
public:
	// MultiPlayerLoadScreen::init.
	void Init(const MultiplayerLoadScreenSetup &game)
	{
		const auto text = [&](std::string_view label) { return game.text ? game.text(label) : std::u16string(label.begin(), label.end()); };
		const auto faction = [&](int index) -> const LoadScreenFaction * {
			return index >= 0 && static_cast<std::size_t>(index) < game.factions.size() ? &game.factions[static_cast<std::size_t>(index)] : nullptr;
		};
		const int local = game.localSlot;
		const setup::GameSlot &localChosen = game.chosen.slots[static_cast<std::size_t>(local)];
		// The local player's template, else the observer's.
		const LoadScreenFaction *own = faction(game.playerTemplate[static_cast<std::size_t>(local)]);
		if (own == nullptr)
			own = faction(game.observerTemplate);
		std::string portrait;
		std::u16string generalName;
		if (own != nullptr && own->general)
		{
			portrait = own->generalPortrait;
			generalName = text(own->generalName);
		}
		else if (own != nullptr)
		{
			// The original factions have no general.
			if (own->name == "FactionAmerica")
				portrait = "SAFactionLogoLg_US";
			else if (own->name == "FactionGLA")
				portrait = "SUFactionLogoLg_GLA";
			else if (own->name == "FactionChina")
				portrait = "SNFactionLogoLg_China";
			generalName = own->displayName;
		}
		localPortrait.Set(std::move(portrait));
		localName.Set(std::move(generalName));
		localFeatures.Set(text(own == nullptr || own->features.empty() ? "GUI:PlayerObserver" : own->features));
		music.Set(own != nullptr ? own->loadScreenMusic : std::string());

		// isSlotLocalAlly: us, our team, or anyone when we only watch.
		const auto localAlly = [&](int slot) {
			const int team = game.chosen.slots[static_cast<std::size_t>(slot)].team;
			return slot == local || (team >= 0 && team == localChosen.team) || localChosen.playerTemplate == setup::ObserverTemplate;
		};
		m_rowOf.fill(-1);
		int row = 0;
		for (int slot = 0; slot < setup::MaxSlots; ++slot)
		{
			const setup::GameSlot &chosen = game.chosen.slots[static_cast<std::size_t>(slot)];
			if (!chosen.Occupied())
				continue;
			const auto at = static_cast<std::size_t>(slot);
			const bool ally = localAlly(slot);
			// getApparentColor: an observer's is the observer colour's RGB itself, read again as an index (none).
			const int apparentColor = chosen.playerTemplate == setup::ObserverTemplate ? -1 : game.showRandomColor && !ally ? chosen.color : game.color[at];
			const std::uint32_t house =
				apparentColor >= 0 && static_cast<std::size_t>(apparentColor) < game.colors.size() ? game.colors[static_cast<std::size_t>(apparentColor)] : LoadScreenNeutralColor;
			std::string bar = "LoadingBar_ProgressCenter" + std::to_string(apparentColor);
			if (game.imageKnown && !game.imageKnown(bar))
				bar = "LoadingBar_Progress";
			// getApparentPlayerTemplateDisplayName.
			std::u16string side;
			if (game.showRandomTemplate && chosen.playerTemplate == setup::Random && !ally)
				side = text("GUI:Random");
			else if (chosen.playerTemplate == setup::ObserverTemplate)
				side = text("GUI:Observer");
			else if (const LoadScreenFaction *resolved = faction(game.playerTemplate[at]); resolved != nullptr)
				side = resolved->displayName;
			else
				side = text("GUI:Random");
			LoadScreenRow &shown = rows[static_cast<std::size_t>(row)];
			shown.shown.Set(true);
			shown.name.Set(game.names[at]);
			shown.side.Set(std::move(side));
			shown.team.Set(text("Team:" + std::to_string(chosen.team + 1)));
			shown.color.Set(house);
			shown.barImage.Set(std::move(bar));
			shown.barShown.Set(!chosen.AI());
			shown.progress.Set(0);
			m_rowOf[at] = row++;
		}
		for (; row < setup::MaxSlots; ++row)
		{
			LoadScreenRow &unused = rows[static_cast<std::size_t>(row)];
			unused.shown.Set(false);
			unused.barShown.Set(false);
			unused.progress.Set(0);
		}

		// The map's preview (no supply or tech markers: positionAdditionalImages is left out) and updateMapStartSpots on
		// the load screen: each player's number on its apparent start spot.
		MapPreview shownMap;
		if (game.map != nullptr)
		{
			shownMap.known = true;
			shownMap.image = game.map->preview;
			shownMap.extentWidth = game.map->extentWidth;
			shownMap.extentHeight = game.map->extentHeight;
		}
		preview.Set(std::move(shownMap));
		startSpots.Set(game.map != nullptr ? game.map->starts : std::vector<MapPoint>{});
		const int players = game.map != nullptr ? game.map->players : 0;
		for (int spot = 0; spot < setup::MaxSlots; ++spot)
		{
			const auto at = static_cast<std::size_t>(spot);
			startShown[at].Set(game.map != nullptr && spot < players);
			startText[at].Set({});
		}
		if (game.map != nullptr)
			for (int slot = 0; slot < setup::MaxSlots; ++slot)
			{
				const auto at = static_cast<std::size_t>(slot);
				const int spot = game.showRandomStart && !localAlly(slot) ? game.chosen.slots[at].startPos : game.startPos[at];
				if (spot >= 0 && spot < players && game.playerTemplate[at] > setup::ObserverTemplate && game.chosen.slots[at].Occupied())
					startText[static_cast<std::size_t>(spot)].Set(text("NUMBER:" + std::to_string(slot + 1)));
			}
		m_localSlot = local;
	}

	// MultiPlayerLoadScreen::update without a network: the local player's progress (GameLogic::processProgress).
	void Update(int percent)
	{
		if (percent <= 100)
			ProcessProgress(m_localSlot, percent);
	}

	// MultiPlayerLoadScreen::processProgress: a player's bar (a LAN peer's as its progress arrives).
	void ProcessProgress(int slot, int percent)
	{
		if (percent < 0 || percent > 100 || slot < 0 || slot >= setup::MaxSlots || m_rowOf[static_cast<std::size_t>(slot)] < 0)
			return;
		rows[static_cast<std::size_t>(m_rowOf[static_cast<std::size_t>(slot)])].progress.Set(percent);
	}

	std::array<LoadScreenRow, setup::MaxSlots> rows;
	engine::gui::mvvm::Observable<std::string> localPortrait;
	engine::gui::mvvm::Observable<std::u16string> localName;
	engine::gui::mvvm::Observable<std::u16string> localFeatures;
	engine::gui::mvvm::Observable<std::string> music; // the local side's LoadScreenMusic (fading in)
	engine::gui::mvvm::Observable<MapPreview> preview;
	engine::gui::mvvm::Observable<std::vector<MapPoint>> startSpots;
	std::array<engine::gui::mvvm::Observable<bool>, setup::MaxSlots> startShown;
	std::array<engine::gui::mvvm::Observable<std::u16string>, setup::MaxSlots> startText;

private:
	std::array<int, setup::MaxSlots> m_rowOf{};
	int m_localSlot{0};
};
}
