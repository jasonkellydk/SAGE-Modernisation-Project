export module games.generalszh.shell.score.score_screen_view;
import std;

export import games.generalszh.shell.score.score_screen_view_model;
export import Engine.UI.WND.Bindings;

// Binds Window/Menus/ScoreScreen.wnd to the ScoreScreenViewModel: each row's name and values (StaticTextPlayer<n>,
// StaticTextUnitsBuilt<n> ... StaticTextResources<n>) in its colour, shown or hidden with the row (hideWindows); the
// backdrop (ParentScoreScreen's image); OK, and Continue with its text.
export namespace generalszh::shell
{
inline void BindScoreScreenView(Engine::UI::WND::WNDBindings &bindings, ScoreScreenViewModel &viewModel,
	std::function<Engine::UI::WND::ImageRef(std::string_view)> resolve)
{
	static constexpr std::array<std::string_view, ScoreScreenViewModel::Values> columns{"StaticTextUnitsBuilt", "StaticTextUnitsLost", "StaticTextUnitsDestroyed",
		"StaticTextBuildingsBuilt", "StaticTextBuildingsLost", "StaticTextBuildingsDestroyed", "StaticTextResources"};
	const std::string menu = "ScoreScreen.wnd:";
	for (std::size_t index = 0; index < ScoreScreenViewModel::Rows; ++index)
	{
		ScoreScreenViewModel::Row &row = viewModel.rows[index];
		const std::string number = std::to_string(index);
		const std::string player = menu + "StaticTextPlayer" + number;
		bindings.BindVisible(player, row.shown);
		bindings.BindVisible(menu + "StaticTextObserver" + number, row.observer);
		bindings.BindText(player, row.name);
		bindings.BindTextColor(player, row.color);
		for (std::size_t column = 0; column < columns.size(); ++column)
		{
			const std::string window = menu + std::string(columns[column]) + number;
			bindings.BindVisible(window, row.shown);
			bindings.BindText(window, row.values[column]);
			bindings.BindTextColor(window, row.color);
		}
	}
	bindings.BindImage(menu + "ParentScoreScreen", viewModel.backdrop, std::move(resolve));
	for (const char *extra : {"TextEntryChat", "ButtonEmote", "ListboxChatWindowScoreScreen", "ButtonBuddy", "ListboxWarschoolAdvice",
			 "StaticTextWarSchool"})
		bindings.BindVisible(menu + extra, viewModel.never);
	bindings.BindVisible(menu + "StaticTextGameSaveComplete", viewModel.gameSavedShown);
	bindings.BindVisible(menu + "ButtonSaveReplay", viewModel.saveReplayShown);
	bindings.BindEnabled(menu + "ButtonSaveReplay", viewModel.never);
	bindings.BindVisible(menu + "ButtonOk", viewModel.okShown);
	bindings.BindCommand(menu + "ButtonOk", viewModel.ok);
	bindings.BindVisible(menu + "ButtonContinue", viewModel.continueShown);
	bindings.BindText(menu + "ButtonContinue", viewModel.continueText);
	bindings.BindCommand(menu + "ButtonContinue", viewModel.continueGame);
}
}
