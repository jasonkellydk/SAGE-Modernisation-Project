export module games.generalszh.shell.save_load.save_load_view;
import std;

export import games.generalszh.shell.save_load.save_load_view_model;
export import Engine.UI.WND.Bindings;

// Binds Window/Menus/SaveLoad.wnd to the SaveLoadViewModel: the menu (shown
// at once; the original fades it in), the games list (a double click loads,
// GLM_DOUBLE_CLICKED), its buttons as updateMenuActions enables them, and
// the delete confirm panel.
export namespace generalszh::shell
{
inline void BindSaveLoadView(Engine::UI::WND::WNDBindings &bindings, SaveLoadViewModel &viewModel)
{
	bindings.BindList("SaveLoad.wnd:ListboxGames", viewModel.items, viewModel.selected, &viewModel.load);
	bindings.BindRowColors("SaveLoad.wnd:ListboxGames", viewModel.rowColors);
	bindings.BindEnabled("SaveLoad.wnd:ListboxGames", viewModel.menuEnabled);
	bindings.BindEnabled("SaveLoad.wnd:MenuButtonFrame", viewModel.menuEnabled);
	bindings.BindEnabled("SaveLoad.wnd:ButtonSave", viewModel.canSave);
	bindings.BindEnabled("SaveLoad.wnd:ButtonLoad", viewModel.canLoad);
	bindings.BindEnabled("SaveLoad.wnd:ButtonDelete", viewModel.canLoad);
	bindings.BindVisible("SaveLoad.wnd:DeleteConfirmParent", viewModel.deleteConfirmOpen);
	bindings.BindCommand("SaveLoad.wnd:ButtonLoad", viewModel.load);
	bindings.BindCommand("SaveLoad.wnd:ButtonDelete", viewModel.deleteGame);
	bindings.BindCommand("SaveLoad.wnd:ButtonBack", viewModel.back);
	bindings.BindCommand("SaveLoad.wnd:ButtonDeleteConfirm", viewModel.deleteConfirm);
	bindings.BindCommand("SaveLoad.wnd:ButtonDeleteCancel", viewModel.deleteCancel);
}

// Binds Window/Menus/PopupSaveLoad.wnd (in a game, over the quit menu) likewise, with its save, overwrite, description
// and load panels.
inline void BindPopupSaveLoadView(Engine::UI::WND::WNDBindings &bindings, SaveLoadViewModel &viewModel)
{
	const std::string menu = "PopupSaveLoad.wnd:";
	bindings.BindList(menu + "ListboxGames", viewModel.items, viewModel.selected, &viewModel.load);
	bindings.BindRowColors(menu + "ListboxGames", viewModel.rowColors);
	bindings.BindEnabled(menu + "ListboxGames", viewModel.menuEnabled);
	bindings.BindEnabled(menu + "MenuButtonFrame", viewModel.menuEnabled);
	bindings.BindEnabled(menu + "ButtonSave", viewModel.canSave);
	bindings.BindEnabled(menu + "ButtonLoad", viewModel.canLoad);
	bindings.BindEnabled(menu + "ButtonDelete", viewModel.canLoad);
	bindings.BindVisible(menu + "DeleteConfirmParent", viewModel.deleteConfirmOpen);
	bindings.BindVisible(menu + "LoadConfirmParent", viewModel.loadConfirmOpen);
	bindings.BindVisible(menu + "OverwriteConfirmParent", viewModel.overwriteConfirmOpen);
	bindings.BindVisible(menu + "SaveDescParent", viewModel.saveDescOpen);
	bindings.BindEntry(menu + "EntryDesc", viewModel.description, &viewModel.saveDescConfirm);
	bindings.BindCommand(menu + "ButtonSave", viewModel.save);
	bindings.BindCommand(menu + "ButtonLoad", viewModel.load);
	bindings.BindCommand(menu + "ButtonDelete", viewModel.deleteGame);
	bindings.BindCommand(menu + "ButtonBack", viewModel.back);
	bindings.BindCommand(menu + "ButtonDeleteConfirm", viewModel.deleteConfirm);
	bindings.BindCommand(menu + "ButtonDeleteCancel", viewModel.deleteCancel);
	bindings.BindCommand(menu + "ButtonLoadConfirm", viewModel.loadConfirm);
	bindings.BindCommand(menu + "ButtonLoadCancel", viewModel.loadCancel);
	bindings.BindCommand(menu + "ButtonOverwriteConfirm", viewModel.overwriteConfirm);
	bindings.BindCommand(menu + "ButtonOverwriteCancel", viewModel.overwriteCancel);
	bindings.BindCommand(menu + "ButtonSaveDescConfirm", viewModel.saveDescConfirm);
	bindings.BindCommand(menu + "ButtonSaveDescCancel", viewModel.saveDescCancel);
}
}
