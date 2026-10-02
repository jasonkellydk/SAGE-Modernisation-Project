export module games.generalszh.shell.dialog.message_box_view;
import std;

export import games.generalszh.shell.dialog.message_box_view_model;
export import Engine.UI.WND.Bindings;

// Binds Window/Menus/MessageBox.wnd to the MessageBoxViewModel: shown while
// open, its title and message, and the buttons it asks for.
export namespace generalszh::shell
{
inline void BindMessageBoxView(Engine::UI::WND::WNDBindings &bindings, MessageBoxViewModel &viewModel)
{
	bindings.BindVisible("MessageBox.wnd:MessageBoxParent", viewModel.open);
	bindings.BindText("MessageBox.wnd:StaticTextTitle", viewModel.title);
	bindings.BindText("MessageBox.wnd:StaticTextMessage", viewModel.message);
	bindings.BindVisible("MessageBox.wnd:ButtonOk", viewModel.showOk);
	bindings.BindVisible("MessageBox.wnd:ButtonCancel", viewModel.showCancel);
	bindings.BindVisible("MessageBox.wnd:ButtonYes", viewModel.showYes);
	bindings.BindVisible("MessageBox.wnd:ButtonNo", viewModel.showNo);
	bindings.BindCommand("MessageBox.wnd:ButtonOk", viewModel.ok);
	bindings.BindCommand("MessageBox.wnd:ButtonCancel", viewModel.cancel);
	bindings.BindCommand("MessageBox.wnd:ButtonYes", viewModel.yes);
	bindings.BindCommand("MessageBox.wnd:ButtonNo", viewModel.no);
}
}
