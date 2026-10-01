export module games.generalszh.shell.load_screen.load_screen_view;
import std;

export import games.generalszh.shell.load_screen.shell_game_load_screen_view_model;
export import games.generalszh.shell.load_screen.mission_load_screen_view_model;
export import games.generalszh.shell.load_screen.multiplayer_load_screen_view_model;
export import games.generalszh.shell.skirmish.skirmish_view;
export import Engine.UI.WND.Bindings;
import Engine.UI.WND;
import Engine.UI.WND.Document;

// Binds the load screens' layouts (Window/Menus/ShellGameLoadScreen.wnd, SinglePlayerLoadScreen.wnd,
// ChallengeLoadScreen.wnd, MultiplayerLoadScreen.wnd) to their view models. `resolve` and `size`: a mapped image by name.
export namespace generalszh::shell
{
namespace wnd = Engine::UI::WND;

using LoadScreenImageResolve = std::function<wnd::ImageRef(std::string_view)>;
using LoadScreenImageSize = std::function<std::optional<std::pair<int, int>>(std::string_view)>;

inline void BindShellGameLoadScreenView(wnd::WNDBindings &bindings, ShellGameLoadScreenViewModel &viewModel, LoadScreenImageResolve resolve,
	LoadScreenImageSize size)
{
	const std::string screen = "ShellGameLoadScreen.wnd:";
	bindings.BindCellImage(screen + "ParentShellGameLoadScreen", 0, viewModel.titleImage, std::move(resolve), std::move(size));
	bindings.BindVisible(screen + "StaticTextLegal", viewModel.legalShown);
	bindings.BindProgress(screen + "ProgressLoad", viewModel.progress);
	bindings.BindVisible(screen + "ProgressLoad", viewModel.progressShown);
}

// SinglePlayerLoadScreen.wnd ("SinglePlayerLoadScreen.wnd:") or ChallengeLoadScreen.wnd ("ChallengeLoadScreen.wnd:"):
// the bar, and a campaign's side look on its background and bar. The movie in the background window is the host's.
class MissionLoadScreenView
{
public:
	MissionLoadScreenView(wnd::WNDBindings &bindings, MissionLoadScreenViewModel &viewModel, bool challenge, LoadScreenImageResolve resolve,
		LoadScreenImageSize size)
		: m_viewModel(viewModel)
	{
		const std::string screen = challenge ? "ChallengeLoadScreen.wnd:" : "SinglePlayerLoadScreen.wnd:";
		bindings.BindProgress(screen + "ProgressLoad", viewModel.progress);
		if (challenge)
			return;
		bindings.BindText(screen + "Percent", viewModel.percentText);
		bindings.BindVisible(screen + "Percent", viewModel.percentShown);
		bindings.BindCellImage(screen + "ParentSinglePlayerLoadScreen", 0, m_background, resolve, size);
		bindings.BindCellImage(screen + "ProgressLoad", 6, m_bar, resolve, size);
		const auto id = viewModel.look.Subscribe([this](const MissionLoadLook &look) {
			m_background.Set(look.background);
			m_bar.Set(look.bar);
		});
		m_release = [this, id] { m_viewModel.look.Unsubscribe(id); };
		// init hides the objectives, the location and the units' captions (moveWindows is Generals' alone).
		for (const char *hidden : {"ObjectivesWin", "StaticTextCameoText0", "StaticTextCameoText1", "StaticTextCameoText2", "StaticTextCameoText3"})
			bindings.BindVisible(screen + hidden, m_never);
	}
	~MissionLoadScreenView()
	{
		if (m_release)
			m_release();
	}
	MissionLoadScreenView(const MissionLoadScreenView &) = delete;
	MissionLoadScreenView &operator=(const MissionLoadScreenView &) = delete;

private:
	MissionLoadScreenViewModel &m_viewModel;
	engine::gui::mvvm::Observable<std::string> m_background, m_bar;
	engine::gui::mvvm::Observable<bool> m_never{false};
	std::function<void()> m_release;
};

class MultiplayerLoadScreenView
{
public:
	MultiplayerLoadScreenView(wnd::WNDBindings &bindings, wnd::WNDDocument &document, MultiplayerLoadScreenViewModel &viewModel, LoadScreenImageResolve resolve,
		LoadScreenImageSize size)
		: m_viewModel(viewModel)
	{
		const std::string screen = "MultiplayerLoadScreen.wnd:";
		bindings.BindCellImage(screen + "LocalGeneralPortrait", 0, viewModel.localPortrait, resolve, size);
		bindings.BindText(screen + "LocalGeneralName", viewModel.localName);
		bindings.BindText(screen + "LocalGeneralFeatures", viewModel.localFeatures);
		for (int index = 0; index < setup::MaxSlots; ++index)
		{
			const auto at = static_cast<std::size_t>(index);
			const std::string slot = std::to_string(index);
			LoadScreenRow &row = viewModel.rows[at];
			bindings.BindProgress(screen + "ProgressLoad" + slot, row.progress);
			bindings.BindVisible(screen + "ProgressLoad" + slot, row.barShown);
			bindings.BindCellImage(screen + "ProgressLoad" + slot, 6, row.barImage, resolve, size);
			for (const char *text : {"StaticTextPlayer", "StaticTextSide", "StaticTextTeam"})
			{
				bindings.BindVisible(screen + text + slot, row.shown);
				bindings.BindTextColor(screen + text + slot, row.color);
			}
			bindings.BindText(screen + "StaticTextPlayer" + slot, row.name);
			bindings.BindText(screen + "StaticTextSide" + slot, row.side);
			bindings.BindText(screen + "StaticTextTeam" + slot, row.team);
			bindings.BindText(screen + "ButtonMapStartPosition" + slot, viewModel.startText[at]);
			bindings.BindVisible(screen + "ButtonMapStartPosition" + slot, viewModel.startShown[at]);
		}
		bindings.BindPicture(screen + "WinMapPreview", m_picture, resolve);
		const auto place = [this, &document, screen] {
			m_picture.Set(detail::Picture(m_viewModel.preview.Get()));
			detail::PlaceStartButtons(document, screen + "WinMapPreview", screen + "ButtonMapStartPosition", m_viewModel.preview.Get(), m_viewModel.startSpots.Get());
		};
		const auto previewId = viewModel.preview.Subscribe([place](const MapPreview &) { place(); });
		const auto spotsId = viewModel.startSpots.Subscribe([place](const std::vector<MapPoint> &) { place(); });
		m_release = [this, previewId, spotsId] {
			m_viewModel.preview.Unsubscribe(previewId);
			m_viewModel.startSpots.Unsubscribe(spotsId);
		};
	}
	~MultiplayerLoadScreenView()
	{
		if (m_release)
			m_release();
	}
	MultiplayerLoadScreenView(const MultiplayerLoadScreenView &) = delete;
	MultiplayerLoadScreenView &operator=(const MultiplayerLoadScreenView &) = delete;

private:
	MultiplayerLoadScreenViewModel &m_viewModel;
	engine::gui::mvvm::Observable<wnd::PictureSource> m_picture;
	std::function<void()> m_release;
};
}
