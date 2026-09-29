export module games.generalszh.shell.challenge.challenge_view;
import std;

export import games.generalszh.shell.challenge.challenge_view_model;
export import Engine.UI.WND.Bindings;
import Engine.UI.WND;
import Engine.UI.WND.Document;

// Binds Window/Menus/ChallengeMenu.wnd to the ChallengeViewModel. As
// ChallengeMenuInit / setEnabledButtons: each GeneralPosition button shows
// its general's medallions (normal, hilite, and selected when down or
// disabled), sized to the normal one, hidden and disabled for a general
// that starts locked. (The GadgetParent shows as ChallengeMenuFade plays.)
export namespace generalszh::shell
{
namespace wnd = Engine::UI::WND;

// `resolve`: an image by name; `size`: its width in the layout's units (the button is made that square).
inline void BindChallengeView(wnd::WNDBindings &bindings, wnd::WNDDocument &document, ChallengeViewModel &viewModel,
	std::function<wnd::ImageRef(std::string_view)> resolve, std::function<int(std::string_view)> size)
{
	const std::string menu = "ChallengeMenu.wnd:";
	for (std::size_t index = 0; index < ChallengeViewModel::Count; ++index)
	{
		const std::string name = menu + "GeneralPosition" + std::to_string(index);
		const ChallengeGeneral &general = viewModel.Generals()[index];
		if (wnd::WNDWindow *button = document.Find_Window(name))
		{
			const auto image = [&](std::size_t state, std::size_t cell, const std::string &image) {
				wnd::WNDDrawCell &drawn = button->draw_states[state].cells[cell];
				drawn.image_name = image;
				drawn.image = image.empty() || !resolve ? wnd::ImageRef{} : resolve(image);
			};
			image(0, 0, general.normal);   // GadgetCheckBoxSetEnabledImage
			image(2, 0, general.hilite);   // GadgetCheckBoxSetHiliteImage
			image(2, 1, general.selected); // GadgetCheckBoxSetHiliteUncheckedBoxImage
			image(1, 1, general.selected); // GadgetCheckBoxSetDisabledUncheckedBoxImage
			if (const int width = size && !general.normal.empty() ? size(general.normal) : 0; width > 0)
				document.Resize_Window(name, width, width);
		}
		document.Set_Window_Flag(name, wnd::WindowFlag::Enabled, general.enabled);
		document.Set_Window_Flag(name, wnd::WindowFlag::Hidden, !general.enabled);
		bindings.BindChecked(name, viewModel.checked[index]);
		bindings.BindCommand(name, viewModel.select[index]);
		bindings.BindHover(name, viewModel.enter[index], &viewModel.leave[index]);
	}
	bindings.BindVisible(menu + "GeneralsBioParent", viewModel.bioShown);
	bindings.BindVisible(menu + "ButtonPlay", viewModel.playShown);
	bindings.BindImage(menu + "BioPortrait", viewModel.portrait, resolve);
	// The four entries, repurposed: name, rank, branch, strategy.
	bindings.BindText(menu + "BioNameEntry", viewModel.lines[0]);
	bindings.BindText(menu + "BioDOBEntry", viewModel.lines[1]);
	bindings.BindText(menu + "BioBirthplaceEntry", viewModel.lines[2]);
	bindings.BindText(menu + "BioStrategyEntry", viewModel.lines[3]);
	bindings.BindCommand(menu + "ButtonPlay", viewModel.play);
	bindings.BindCommand(menu + "ButtonBack", viewModel.back);
}
}
