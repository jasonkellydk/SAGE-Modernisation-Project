export module games.generalszh.shell.replay.replay_menu_view;
import std;

export import games.generalszh.shell.replay.replay_menu_view_model;
export import Engine.UI.WND.Bindings;

// Binds Window/Menus/ReplayMenu.wnd to the ReplayMenuViewModel: the replay
// list (a double click loads, as the original's GLM_DOUBLE_CLICKED) and its
// Load, Delete, Copy and Back buttons.
export namespace generalszh::shell
{
inline void BindReplayMenuView(Engine::UI::WND::WNDBindings &bindings, ReplayMenuViewModel &viewModel)
{
	bindings.BindList("ReplayMenu.wnd:ListboxReplayFiles", viewModel.items, viewModel.selected, &viewModel.load);
	bindings.BindCommand("ReplayMenu.wnd:ButtonLoadReplay", viewModel.load);
	bindings.BindCommand("ReplayMenu.wnd:ButtonDeleteReplay", viewModel.deleteReplay);
	bindings.BindCommand("ReplayMenu.wnd:ButtonCopyReplay", viewModel.copyReplay);
	bindings.BindCommand("ReplayMenu.wnd:ButtonBack", viewModel.back);
}
}
