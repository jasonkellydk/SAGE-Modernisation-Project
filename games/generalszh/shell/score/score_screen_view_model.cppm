export module games.generalszh.shell.score.score_screen_view_model;
import std;

export import engine.gui.mvvm.observable;

// The score screen after a game (the original's ScoreScreen.cpp over ScoreScreen.wnd): eight rows, each a name in its
// colour and its units built, lost and destroyed, buildings built, lost and destroyed and money earned.
// - A campaign's or challenge's mission (initSinglePlayer, grabSinglePlayerInfo): the local player first, named
//   "GUI:Player" (overidePlayerDisplayName); then the other players listed on the score screen, summed by base side and
//   by whether the local player counts them allies or enemies (USA, China, GLA allies, then enemies: "GUI:USAAllies"
//   ...), each sum in the colour of the last player in it; the backdrop is the local player's ScoreScreenImage.
//   Continue: after a victory "GUI:SaveAndContinue" to the next mission, "GUI:EndCampaign" after the last one; after a
//   defeat "GUI:Retry" (the same mission again).
// - A skirmish (initSkirmish, grabMultiPlayerInfo): every seat's player by score, highest first (a score already taken
//   is raised by 1, 2, ... in turn), each by its own name; no Continue; the backdrop "MutiPlayer_ScoreScreen".
// OK goes back to the main menu.
export namespace generalszh::shell
{
struct ScorePlayer
{
	std::u16string name;
	std::string baseSide;         // USA, China, GLA
	std::uint32_t color{0};       // 0xAARRGGBB
	bool local{false}, listed{true}, observer{false};
	std::uint8_t relation{0};     // to the local player: 0 neutral, 1 allies, 2 enemies
	std::int64_t unitsBuilt{0}, unitsLost{0}, unitsDestroyed{0};
	std::int64_t buildingsBuilt{0}, buildingsLost{0}, buildingsDestroyed{0};
	std::int64_t moneyEarned{0};
	std::int64_t score{0};
	std::string playerName;       // its name in the map (player<slot> for a seat)
	std::string scoreScreenImage; // its PlayerTemplate's ScoreScreenImage
};

enum class ScoreScreenKind : std::uint8_t
{
	SinglePlayer,
	Skirmish,
};

struct ScoreScreenSetup
{
	ScoreScreenKind kind{ScoreScreenKind::Skirmish};
	std::vector<ScorePlayer> players; // single player: every player; skirmish: the seats' players in slot order
	bool victorious{false};
	bool campaignOver{false};         // no mission after this one
	bool gameSaved{false};            // the mission save was made (StaticTextGameSaveComplete shows)
	std::string localScoreScreenImage; // the local player's PlayerTemplate ScoreScreenImage
};

enum class ScoreChoice : std::uint8_t
{
	None,
	MainMenu,
	Continue,
};

class ScoreScreenViewModel
{
public:
	static constexpr std::size_t Rows = 8; // MAX_SLOTS
	static constexpr std::size_t Values = 7;

	struct Row
	{
		engine::gui::mvvm::Observable<bool> shown{false};
		engine::gui::mvvm::Observable<bool> observer{false}; // StaticTextObserver<n> (setObserverWindows)
		engine::gui::mvvm::Observable<std::u16string> name;
		engine::gui::mvvm::Observable<std::uint32_t> color{0};
		std::array<engine::gui::mvvm::Observable<std::u16string>, Values> values;
	};

	// `text`: a label's text (TheGameText->fetch).
	explicit ScoreScreenViewModel(std::function<std::u16string(std::string_view)> text) : m_text(std::move(text))
	{
		ok.SetAction([this] { m_choice = ScoreChoice::MainMenu; });
		continueGame.SetAction([this] { m_choice = ScoreChoice::Continue; });
	}

	void Show(const ScoreScreenSetup &setup)
	{
		m_choice = ScoreChoice::None;
		std::size_t count = 0;
		const auto fill = [&](const std::u16string &name, std::uint32_t color, std::array<std::int64_t, Values> numbers, bool observer) {
			if (count >= Rows)
				return;
			Row &row = rows[count++];
			row.observer.Set(observer);
			row.name.Set(name);
			row.color.Set(color);
			for (std::size_t index = 0; index < Values; ++index)
			{
				const std::string number = observer ? std::string{} : std::to_string(numbers[index]);
				row.values[index].Set(std::u16string(number.begin(), number.end()));
			}
			row.shown.Set(true);
		};
		const auto numbersOf = [](const ScorePlayer &player) {
			return std::array<std::int64_t, Values>{player.unitsBuilt, player.unitsLost, player.unitsDestroyed, player.buildingsBuilt, player.buildingsLost,
				player.buildingsDestroyed, player.moneyEarned};
		};
		if (setup.kind == ScoreScreenKind::SinglePlayer)
		{
			backdrop.Set(setup.localScoreScreenImage);
			const auto local = std::ranges::find_if(setup.players, [](const ScorePlayer &player) { return player.local; });
			if (local != setup.players.end())
			{
				fill(Text("GUI:Player"), local->color, numbersOf(*local), false);
				for (const bool friends : {true, false})
					for (const std::string_view side : {"USA", "China", "GLA"})
					{
						std::array<std::int64_t, Values> sum{};
						std::uint32_t color = 0;
						bool populate = false;
						for (const ScorePlayer &player : setup.players)
						{
							if (player.local || player.baseSide != side || !player.listed || player.relation != (friends ? 1 : 2))
								continue;
							const auto numbers = numbersOf(player);
							for (std::size_t index = 0; index < Values; ++index)
								sum[index] += numbers[index];
							color = player.color;
							populate = true;
						}
						if (populate)
							fill(Text("GUI:" + std::string(side) + (friends ? "Allies" : "Enemies")), color, sum, false);
					}
			}
			okShown.Set(true);
			continueShown.Set(true);
			// finishSinglePlayerInit: after the automatic mission save (a mission won with one to follow).
			gameSavedShown.Set(setup.gameSaved);
			continueText.Set(Text(!setup.victorious ? "GUI:Retry" : setup.campaignOver ? "GUI:EndCampaign" : "GUI:SaveAndContinue"));
		}
		else
		{
			backdrop.Set("MutiPlayer_ScoreScreen");
			// grabMultiPlayerInfo: by score in a map, a score already there raised by the next adder.
			std::map<std::int64_t, const ScorePlayer *> byScore;
			std::int64_t adder = 1;
			for (const ScorePlayer &player : setup.players)
			{
				std::int64_t score = player.score;
				if (byScore.contains(score))
					score += adder++;
				byScore[score] = &player;
			}
			for (auto it = byScore.rbegin(); it != byScore.rend(); ++it)
				fill(it->second->name, it->second->color, numbersOf(*it->second), it->second->observer);
			okShown.Set(true);
			continueShown.Set(false);
			gameSavedShown.Set(false);
		}
		// hideWindows: the rows not used.
		for (std::size_t index = count; index < Rows; ++index)
		{
			rows[index].shown.Set(false);
			rows[index].observer.Set(false);
		}
		// initSinglePlayer / initSkirmish: no chat, buddies or (not ported) war school advice; a single-player game saves no
		// replay (its button hidden), a skirmish's shows it off (nothing recorded).
		saveReplayShown.Set(setup.kind != ScoreScreenKind::SinglePlayer);
	}

	// What the player chose, once.
	ScoreChoice TakeChoice() { return std::exchange(m_choice, ScoreChoice::None); }

	std::array<Row, Rows> rows;
	engine::gui::mvvm::Observable<bool> okShown{true}, continueShown{false};
	engine::gui::mvvm::Observable<bool> gameSavedShown{false}; // StaticTextGameSaveComplete
	engine::gui::mvvm::Observable<bool> saveReplayShown{false};
	engine::gui::mvvm::Observable<bool> never{false}; // what never shows here (chat, buddies, war school)
	engine::gui::mvvm::Observable<std::u16string> continueText;
	engine::gui::mvvm::Observable<std::string> backdrop;
	engine::gui::mvvm::Command ok, continueGame;

private:
	std::u16string Text(const std::string &label) const
	{
		return m_text ? m_text(label) : std::u16string(label.begin(), label.end());
	}

	std::function<std::u16string(std::string_view)> m_text;
	ScoreChoice m_choice{ScoreChoice::None};
};
}
