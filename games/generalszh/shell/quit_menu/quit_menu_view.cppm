export module games.generalszh.shell.quit_menu.quit_menu_view;
import std;

export import games.generalszh.shell.quit_menu.quit_menu_view_model;
export import Engine.UI.WND.Bindings;

// Binds Window/Menus/QuitMenu.wnd (`menu` "QuitMenu.wnd:") or QuitNoSave.wnd ("QuitNoSave.wnd:", without Save/Load) to
// the QuitMenuViewModel: its buttons, Restart's and Exit's texts as the game asks (GadgetButtonSetText) and what is off.
export namespace generalszh::shell
{
inline void BindQuitMenuView(Engine::UI::WND::WNDBindings &bindings, QuitMenuViewModel &viewModel, const std::string &menu)
{
	bindings.BindCommand(menu + "ButtonReturn", viewModel.returnToGame);
	bindings.BindCommand(menu + "ButtonExit", viewModel.exit);
	bindings.BindCommand(menu + "ButtonRestart", viewModel.restart);
	bindings.BindCommand(menu + "ButtonOptions", viewModel.options);
	bindings.BindEnabled(menu + "ButtonOptions", viewModel.optionsEnabled);
	bindings.BindEnabled(menu + "ButtonRestart", viewModel.restartEnabled);
	bindings.BindText(menu + "ButtonRestart", viewModel.restartText);
	bindings.BindText(menu + "ButtonExit", viewModel.exitText);
	if (menu == "QuitMenu.wnd:")
	{
		bindings.BindCommand(menu + "ButtonSaveLoad", viewModel.saveLoad);
		bindings.BindEnabled(menu + "ButtonSaveLoad", viewModel.saveLoadEnabled);
	}
}
}
