export module games.generalszh.hosts.game.control_bar_layer;
import std;
import Engine.UI.WND.Layout;

import Engine.UI.WND;
import Engine.UI.WND.Document;
import Engine.UI.WND.Input;
import Graphics.Renderer2D;
import engine.filesystem.core.virtual_file_system;
import engine.localization.model.string_table;
import games.generalszh.content.loading.content_loader;
import games.generalszh.content.control_bar.control_bar_scheme;
import games.generalszh.hosts.game.shell_menu;
import games.generalszh.hosts.game.game_client;
import games.generalszh.hud.control_bar_view;
import games.generalszh.hud.idle_workers;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.resources.ground_height;
import games.generalszh.hud.generals_powers_view;
import games.generalszh.hud.shortcut_bar_view;
import games.generalszh.hud.build_tooltip;
import games.generalszh.hud.hot_keys;
import games.generalszh.hud.observer_panel_view;
import games.generalszh.hud.control_bar_scheme_look;
import games.generalszh.hud.control_bar_stage;
import engine.gameplay.rts.match.resources.match_outcome;
import games.generalszh.hosts.game.radar_minimap;
import games.generalszh.presentation.hud.algorithms.power_bar;
import games.generalszh.presentation.hud.algorithms.experience_bar;
import games.generalszh.presentation.hud.algorithms.cameo_flash_steps;
import Engine.Core.Math.FixedPresentation;

// The in-game control bar in a match (the original's InGameUI with ControlBar.wnd and its side's ControlBarScheme):
// its scheme's art (layers 3..5 behind the bar's windows, 0..2 in front), the layout bound to its view model, fed each
// frame from the game's view of the local player and their selection, and the mouse over it (clicks on it do not
// reach the world). Over it, the General's Powers screen (GeneralsExpPoints.wnd) its general's button opens; beside it,
// the side's general's powers shortcut bar (its PlayerTemplate's SpecialPowerShortcutWinName).
export namespace generalszh::host
{
class ControlBarLayer
{
public:
	bool Load(const engine::filesystem::VirtualFileSystem &files, const engine::localization::StringTable &strings, content::ContentLoader &loader,
		GameClient &game, std::uint32_t width, std::uint32_t height, float fontScale, std::string &error)
	{
		if (!m_menu.Load(files, "Window/ControlBar.wnd", strings, width, height, error, fontScale))
			return false;
		m_width = width;
		m_height = height;
		m_game = &game;
		m_schemes = content::BindControlBarSchemes(loader.Load({"Data/INI/Default/ControlBarScheme", "Data/INI/ControlBarScheme"}));
		// The local player's side's scheme (setControlBarSchemeByPlayer), and the windows it places (ControlBarScheme::init:
		// the money readout at its MoneyUL..MoneyLR, from its resolution to the layout's).
		session::SessionView *view = game.View();
		const std::optional<std::uint32_t> player = game.LocalPlayer();
		m_side = view != nullptr && player ? view->PlayerSide(*player) : std::string{};
		m_look = std::make_unique<hud::ControlBarSchemeLook>(m_schemes);
		m_scheme = m_look->Select(m_side);
		if (m_scheme != nullptr && m_scheme->width > 0 && m_scheme->height > 0)
			PlaceSchemeWindows(*m_scheme);
		// The bar keeps its aspect, centred at the bottom of the screen (ControlBarScheme::init: Fit_Viewport Center, End);
		// how far that moved the layout, for windows a later scheme places.
		const auto parentAt = [this] {
			const Engine::UI::WND::WNDWindow *parent = m_menu.Document().Find_Window("ControlBar.wnd:ControlBarParent");
			return parent != nullptr ? std::pair{parent->screen_region.left, parent->screen_region.top} : std::pair{0, 0};
		};
		const auto before = parentAt();
		m_menu.Fit(width, height, Engine::UI::WND::LayoutAnchor::Center, Engine::UI::WND::LayoutAnchor::End);
		m_fitShift = {parentAt().first - before.first, parentAt().second - before.second};
		// The bar's stage and slide (ControlBar::init: m_defaultControlBarPosition, the bar where it is laid out).
		m_barAuthoredTop = parentAt().second;
		m_barOffset = 0;
		m_stage = std::make_unique<hud::ControlBarStageViewModel>();
		m_stage->SetLayout(LaidOutTop(), static_cast<int>(width), static_cast<int>(height));
		// The power window draws the power bar (W3DPowerDraw) in place of its own look.
		if (Engine::UI::WND::WNDWindow *power = m_menu.Document().Find_Window("ControlBar.wnd:PowerWindow"))
			for (auto &state : power->draw_states)
				for (auto &cell : state.cells)
				{
					cell.color.alpha = 0.0f;
					cell.border_color.alpha = 0.0f;
					cell.image = {};
				}
		DressRightHud();
		// W3DCommandBarGenExpDraw draws the general's experience bar alone, never the window's own look.
		if (Engine::UI::WND::WNDWindow *experience = m_menu.Document().Find_Window("ControlBar.wnd:GeneralsExp"))
			for (auto &state : experience->draw_states)
				for (auto &cell : state.cells)
				{
					cell.color.alpha = 0.0f;
					cell.border_color.alpha = 0.0f;
					cell.image = {};
				}
		m_powerBar = {game.View() != nullptr ? game.View()->Content().gameData.powerBarBase : 7,
			game.View() != nullptr ? Engine::Math::ToFloat(game.View()->Content().gameData.powerBarIntervals) : 3.0f,
			game.View() != nullptr ? game.View()->Content().gameData.powerBarYellowRange : 5};
		// W3DGadgetPushButtonImageDraw's overlays for the command buttons: pointed at, pressed.
		m_menu.Document().Set_Button_Overlays(m_menu.Image("Cameo_hilited"), m_menu.Image("Cameo_push"));
		// GUI:ControlBarMoneyDisplay: "$%d" in English.
		m_moneyPattern = Localized(strings, "GUI:ControlBarMoneyDisplay");
		m_viewModel = std::make_unique<hud::ControlBarViewModel>([&game](commands::GameCommand command) {
				if (const auto voice = presentation::ControlBarVoice(command))
					game.CueVoice(*voice);
				game.Submit(command);
			},
			[this](std::int64_t money) { return Money(money); });
		// ControlBarScheme::init's updateCommanBarBorderColors and updateSlotExitImage (the scheme was set up before the view
		// model was made).
		if (m_scheme != nullptr)
		{
			m_viewModel->SetBorderColors(m_scheme->borderBuild, m_scheme->borderUpgrade, m_scheme->borderAction, m_scheme->borderSystem);
			m_viewModel->SetSlotExitImage(std::string(m_scheme->Image("CommandMarkerImage")));
		}
		// The panels' texts (CONTROLBAR:UnderConstructionDesc, CONTROLBAR:OCLTimerDesc).
		m_viewModel->SetLabels([&strings](std::string_view label) { return Localized(strings, label); });
		// InGameUI::selectNextIdleWorker: the next idle worker selected alone and the view centred on it (userLookAt).
		m_viewModel->SetSelectNextIdleWorker([&game] {
			session::SessionView *now = game.View();
			const auto player = game.LocalPlayer();
			if (now == nullptr || !player)
				return;
			const std::vector<ecs::Entity> idle = hud::IdleWorkers(*now, *player);
			const std::vector<ecs::Entity> selected = game.Selection();
			const auto next = hud::NextIdleWorker(*now, idle, selected);
			if (!next)
				return;
			game.SelectOnly({*next});
			// selectNextIdleWorker's MSG_CREATE_SELECTED_GROUP: the worker answers.
			game.CueVoice(presentation::UnitVoiceCue{presentation::VoiceOrder::CreateGroup});
			if (const auto *at = now->World().Get<engine::gameplay::Transform>(*next))
				game.UserLookAt(Engine::Math::ToFloat(at->position.x), Engine::Math::ToFloat(at->position.y));
		});
		// HotKeyManager::executeHotKey's click sounds.
		m_viewModel->SetSound([&game](std::string_view sound) { game.PlayInterfaceSound(sound); });
		// ControlBar::processCommandUI: a pressed button stops its CAMEO_FLASH.
		m_viewModel->SetPressed([&game](std::size_t slot, std::string_view button) {
			if (session::SessionView *now = game.View())
				if (auto *flashes = now->World().FindResource<presentation::CameoFlashes>())
					presentation::StopCameoFlash(*flashes, button, slot);
		});
		// ControlBarObserver: the observer's list and info window, over the match's players.
		m_observer = std::make_unique<hud::ObserverPanelViewModel>([this] { return ObserverPlayers(); },
			[this](std::uint32_t player) -> std::optional<hud::ObserverInfo> {
				session::SessionView *now = m_game != nullptr ? m_game->View() : nullptr;
				if (now == nullptr)
					return std::nullopt;
				for (const hud::ObserverCandidate &candidate : ObserverPlayers())
					if (candidate.player == player)
						return hud::ReadObserverInfo(*now, candidate);
				return std::nullopt;
			},
			[&strings](std::string_view label) { return Localized(strings, label); }, m_multiplayer);
		m_bindings.emplace(m_menu.Document());
		const std::array<int, 4> clock = m_scheme != nullptr ? m_scheme->buildUpClockColor : std::array<int, 4>{0, 0, 0, 100};
		m_view.emplace(*m_bindings, m_menu.Document(), *m_viewModel, [this](std::string_view name) { return m_menu.Image(name); },
			Graphics::Color2D{static_cast<float>(clock[0]) / 255.0f, static_cast<float>(clock[1]) / 255.0f, static_cast<float>(clock[2]) / 255.0f,
				static_cast<float>(clock[3]) / 255.0f});
		hud::BindObserverPanelView(*m_bindings, m_menu.Document(), *m_observer, [this](std::string_view name) { return m_menu.Image(name); });
		// The options button: the OPTIONS meta event (ToggleQuitMenu), as the host answers it.
		m_bindings->BindCommand("ControlBar.wnd:ButtonOptions", m_options);
		// ButtonLarge: toggleControlBarStage (ControlBarSystem's GBM_SELECTED).
		m_bindings->BindCommand("ControlBar.wnd:ButtonLarge", m_stage->toggle);
		// PopupCommunicator: ToggleDiplomacy(FALSE), as the host answers it.
		m_bindings->BindCommand("ControlBar.wnd:PopupCommunicator", m_communicator);
		LoadGeneralsPowers(files, strings, game, width, height, fontScale);
		m_clockColor = Graphics::Color2D{static_cast<float>(clock[0]) / 255.0f, static_cast<float>(clock[1]) / 255.0f, static_cast<float>(clock[2]) / 255.0f,
			static_cast<float>(clock[3]) / 255.0f};
		LoadShortcutBar(files, strings, game, width, height, fontScale);
		// ControlBar::init: the build tooltip's layout (ControlBarPopupDescription.wnd), hidden until a command window asks;
		// laid out as the bar (its layouts are authored together). Without it the bar works on without a popup.
		std::string popupError;
		m_popupLoaded = m_popupMenu.Load(files, "Window/ControlBarPopupDescription.wnd", strings, width, height, popupError, fontScale) &&
			m_popupMenu.Fit(width, height, Engine::UI::WND::LayoutAnchor::Center, Engine::UI::WND::LayoutAnchor::End);
		m_labels = [&strings](std::string_view label) { return Localized(strings, label); };
		m_loaded = true;
		return true;
	}

	// The frame's control bar: the local player's scheme, money, selection's commands and queue; `seconds` of real time
	// since the last frame (the general's star blinks on it).
	void Update(GameClient &game, double seconds = 0.0)
	{
		if (!m_loaded)
			return;
		session::SessionView *view = game.View();
		if (view == nullptr)
			return;
		const std::optional<std::uint32_t> player = game.LocalPlayer();
		// setControlBarSchemeByPlayer: a local player not active (Player::isPlayerActive: an observer, or defeated:
		// killPlayer sets FactionObserver's scheme) has the observer bar, whose update fills only the portrait from the
		// selection (ControlBar::update returns before the command panels).
		bool inactive = false;
		if (player)
		{
			const auto *outcome = view->World().FindResource<engine::gameplay::MatchOutcome>();
			inactive = game.LocalPlayerObserver() || (outcome != nullptr && outcome->Eliminated(*player));
		}
		const content::PlayerTemplateInfo *observerFaction = game.PlayerTemplates().Find("FactionObserver");
		const std::string side = inactive && observerFaction != nullptr ? observerFaction->side : player ? view->PlayerSide(*player) : std::string{};
		if (side != m_side || m_scheme == nullptr)
		{
			// setControlBarSchemeByPlayer (killPlayer, a new local player): the side's scheme, set up afresh (init).
			m_side = side;
			m_scheme = m_look->Select(side);
			if (m_scheme != nullptr && m_scheme->width > 0 && m_scheme->height > 0)
			{
				PlaceSchemeWindows(*m_scheme);
				m_menu.Refresh();
			}
		}
		// ControlBarSchemeManager::update: the scheme's animations move on.
		m_look->Update(seconds);
		// doLetterBoxMode: the bars going off show the bar again, sliding in (ShowControlBar(FALSE)); coming on it hides at
		// once (HideControlBar(TRUE): the host leaves it undrawn).
		const bool letterbox = game.Settings().letterbox;
		if (m_letterbox && !letterbox)
			m_stage->Show(false);
		else if (!m_letterbox && letterbox)
			m_stage->Hide();
		m_letterbox = letterbox;
		// The bar's stage and slide (AnimateWindowManager::update with ControlBar::update): where it is, and the minimise
		// button's images for the stage (setUpDownImages).
		m_stage->SetPlayback(m_replay);
		m_stage->Update(seconds);
		MoveBar(m_stage->top.Get());
		if (m_scheme != nullptr && m_stage->minimised.Get() != m_toggleMinimised)
		{
			m_toggleMinimised = m_stage->minimised.Get();
			ApplyToggleImages(*m_scheme, m_toggleMinimised);
			m_menu.Refresh();
		}
		const std::vector<ecs::Entity> selection = game.Selection();
		m_observer->SetObserverBar(inactive);
		m_observer->Update(view->CurrentTick());
		hud::ControlBarState state = hud::ReadControlBar(*view, player, inactive ? std::vector<ecs::Entity>{} : selection);
		// CAMEO_FLASH: ControlBar::update's flash check over the command windows' buttons as they show (once a logic
		// frame); no selection (CB_CONTEXT_NONE) clears every window's flash.
		if (auto *flashes = view->World().FindResource<presentation::CameoFlashes>())
		{
			std::array<std::string_view, hud::CommandButtons> buttons{};
			for (std::size_t slot = 0; slot < hud::CommandButtons; ++slot)
				if (state.slots[slot].button != nullptr && state.slots[slot].state != gameplay::ButtonState::Hidden)
					buttons[slot] = state.slots[slot].button->name;
			if (selection.empty())
				presentation::ClearCameoFlashing(*flashes);
			presentation::StepCameoFlash(*flashes, view->CurrentTick(), buttons);
			for (std::size_t slot = 0; slot < hud::CommandButtons; ++slot)
				state.slots[slot].flashing = flashes->flashing[slot];
		}
		m_viewModel->Apply(std::move(state));
		m_viewModel->ApplyPortrait(hud::ReadPortrait(*view, player, selection));
		ShowRightHudImage(!m_viewModel->portraitShown.Get());
		if (m_shortcut)
		{
			m_shortcut->Apply(hud::ReadShortcutBar(*view, player));
			if (m_shortcutBindings->TakeDirty())
				m_shortcutMenu.Refresh();
		}
		if (m_powers)
		{
			m_powers->Apply(hud::ReadGeneralsPowers(*view, player), static_cast<std::uint64_t>(std::max(seconds, 0.0) * 1'000'000.0));
			if (inactive)
				m_powers->generalEnabled.Set(false); // setControlBarSchemeByPlayer: buttonGeneral->winEnable(FALSE)
			if (m_powersBindings->TakeDirty())
				m_powersMenu.Refresh();
		}
		const bool barChanged = m_bindings->TakeDirty();
		if (barChanged)
			m_menu.Refresh();
		// ControlBar::update: the build tooltip hides unless this frame's mouse kept it; while it shows, a changed bar
		// (m_UIDirty) fills it again (repopulateBuildTooltipLayout).
		if (m_popupLoaded && !hud::UpdateBuildTooltip(m_buildTooltip) && m_buildTooltip.shown && barChanged)
			PopulatePopup();
	}

	// A scripted click on a window of the bar or of the General's Powers screen, by name (captures, checks).
	void Press(const std::string &window)
	{
		if (!m_loaded)
			return;
		Engine::UI::WND::WNDInputEvent click;
		click.kind = Engine::UI::WND::WNDInputEvent::Kind::Clicked;
		click.window = window;
		if (window.starts_with("GeneralsExpPoints.wnd") && m_powersBindings)
			m_powersBindings->Apply(click);
		else if (m_shortcutBindings && !m_shortcutLayout.empty() && window.starts_with(m_shortcutLayout))
		{
			m_shortcutBindings->Apply(click);
			if (m_shortcutBindings->TakeDirty())
				m_shortcutMenu.Refresh();
		}
		else
			m_bindings->Apply(click);
		if (m_powersBindings && m_powersBindings->TakeDirty())
			m_powersMenu.Refresh();
		if (m_bindings->TakeDirty())
			m_menu.Refresh();
	}

	// What the options button does (ControlBar's ButtonOptions: ToggleQuitMenu).
	void SetOptionsAction(std::function<void()> action) { m_options.SetAction(std::move(action)); }
	// What the communicator button does (ControlBarSystem's buttonCommunicator: ToggleDiplomacy(FALSE)).
	void SetCommunicatorAction(std::function<void()> action) { m_communicator.SetAction(std::move(action)); }

	// A new match (ControlBar::reset): its game slots' teams and computer players for the observer list; `multiplayer`: a
	// skirmish or network game (TheRecorder->isMultiplayer), not a campaign or challenge mission.
	void SetMatch(const std::array<std::pair<int, bool>, hud::ObserverButtons> &slots, bool multiplayer, bool replay = false)
	{
		m_slots = slots;
		m_multiplayer = multiplayer;
		m_replay = replay;
		m_letterbox = false;
		if (m_observer)
			m_observer->Reset(multiplayer);
		// GameLogic::startNewGame's end: ShowControlBar(FALSE): the bar at its default stage, sliding up from below.
		if (m_stage)
			m_stage->Show(false);
	}

	// The observer bar's view model (none before the bar is loaded).
	hud::ObserverPanelViewModel *ObserverPanel() noexcept { return m_observer.get(); }

	// Esc closes the General's Powers screen (GeneralsExpPointsInput); true when it did.
	bool Escape()
	{
		if (!m_powers || !m_powers->shown.Get())
			return false;
		m_powers->Hide();
		if (m_powersBindings->TakeDirty())
			m_powersMenu.Refresh();
		return true;
	}

	// The mouse this frame (screen pixels; buttons as PointerState): true while it is over the bar.
	bool Point(float x, float y, std::uint8_t pressed, std::uint8_t released, std::uint32_t milliseconds)
	{
		if (!m_loaded)
			return false;
		// GameWindowManager's m_grabWindow: a window used the left button going down; it holds the mouse until it comes up.
		if ((released & 1u) != 0)
			m_grabbed = false;
		if ((pressed & 1u) != 0)
			m_grabbed = Grabs(x, y);
		// commandButtonTooltip: the build tooltip of the window holding the mouse's tooltip.
		BuildTooltipFrame(x, y, milliseconds);
		// The General's Powers screen, while open, is in front of the bar.
		if (m_powers && m_powers->shown.Get())
		{
			auto &screen = m_powersMenu.Document();
			const auto [sx, sy] = m_powersMenu.ToLayout(x, y);
			const bool overScreen = Engine::UI::WND::Hit_Test(screen, sx, sy).has_value();
			if (auto input = m_powersPointer.Move(screen, sx, sy))
				m_powersBindings->Apply(*input);
			if ((pressed & 1u) != 0 && overScreen)
				if (auto press = m_powersPointer.Press(screen, sx, sy))
					m_powersBindings->Apply(*press);
			if ((released & 1u) != 0)
				if (auto release = m_powersPointer.Release(screen, sx, sy, milliseconds))
					m_powersBindings->Apply(*release);
			if (m_powersBindings->TakeDirty() || m_powersPointer.TakeLook())
				m_powersMenu.Refresh();
			if (overScreen)
				return true;
		}
		// The shortcut bar, while it shows beside the bar.
		if (ShortcutShown())
		{
			auto &bar = m_shortcutMenu.Document();
			const auto [bx, by] = m_shortcutMenu.ToLayout(x, y);
			const bool overBar = Engine::UI::WND::Hit_Test(bar, bx, by).has_value();
			if (auto input = m_shortcutPointer.Move(bar, bx, by))
				m_shortcutBindings->Apply(*input);
			if ((pressed & 1u) != 0 && overBar)
				if (auto press = m_shortcutPointer.Press(bar, bx, by))
					m_shortcutBindings->Apply(*press);
			if ((released & 1u) != 0)
				if (auto release = m_shortcutPointer.Release(bar, bx, by, milliseconds))
					m_shortcutBindings->Apply(*release);
			if (m_shortcutBindings->TakeDirty() || m_shortcutPointer.TakeLook())
				m_shortcutMenu.Refresh();
			if (overBar)
				return true;
		}
		// LeftHUDInput: a button down on the radar (while it shows) looks there, or with units selected the left button
		// sends them there.
		if ((pressed & 3u) != 0 && m_game != nullptr && m_radar.Shown(*m_game))
			if (const auto rect = RadarRect(); rect && x >= rect->left && x < rect->right && y >= rect->top && y < rect->bottom)
			{
				if (const auto world = m_radar.WorldAt(x - rect->left, y - rect->top, rect->right - rect->left, rect->bottom - rect->top))
				{
					const std::vector<ecs::Entity> selection = m_game->Selection();
					if (selection.empty() || (pressed & 2u) != 0)
						m_game->UserLookAt((*world)[0], (*world)[1]);
					else
					{
						// Radar::radarToWorld: the spot at the ground's height there.
						const Engine::Math::FixedVector2 spot{Engine::Math::Fixed::FromRaw(static_cast<std::int64_t>((*world)[0] * 65536.0f)),
							Engine::Math::Fixed::FromRaw(static_cast<std::int64_t>((*world)[1] * 65536.0f))};
						const session::SessionView *view = m_game->View();
						const auto *ground = view != nullptr ? view->World().FindResource<engine::gameplay::GroundHeight>() : nullptr;
						m_game->Submit(commands::MoveTo{selection, spot, ground != nullptr ? ground->At(spot) : Engine::Math::Fixed{}});
						// ControlBarCallback's radar move: MSG_DO_MOVETO answered.
						m_game->CueVoice(presentation::UnitVoiceCue{presentation::VoiceOrder::Move});
					}
				}
				return true;
			}
		auto &document = m_menu.Document();
		const auto [lx, ly] = m_menu.ToLayout(x, y);
		const bool over = Engine::UI::WND::Hit_Test(document, lx, ly).has_value();
		std::optional<Engine::UI::WND::WNDInputEvent> input = m_pointer.Move(document, lx, ly);
		if (input)
			m_bindings->Apply(*input);
		if ((pressed & 1u) != 0 && over)
			if (auto press = m_pointer.Press(document, lx, ly))
				m_bindings->Apply(*press);
		if ((released & 1u) != 0)
			if (auto release = m_pointer.Release(document, lx, ly, milliseconds))
				m_bindings->Apply(*release);
		if (m_bindings->TakeDirty() || m_pointer.TakeLook())
			m_menu.Refresh();
		return over;
	}

	// GameWindowManager::winProcessMouseEvent's tooltip over the in-game windows (screen pixels): the window under the
	// mouse with a tooltip (Engine::UI::WND::Tooltip_Target), in front first (the General's Powers screen, the shortcut
	// bar, the bar). Its TOOLTIPTEXT and TOOLTIPDELAY, or nothing: none while a window holds the mouse, none over a
	// window whose tooltip callback is commandButtonTooltip (ControlBar::init / setControlCommand: the command buttons,
	// the communicator, options, idle worker, beacon, general, up-down buttons, the power, money and experience
	// displays: their build tooltip is not the mouse's) or a layout's own TOOLTIPCALLBACK, none off the windows.
	std::optional<std::pair<std::u16string, int>> WindowTooltip(float x, float y) const
	{
		const std::optional<TooltipHit> hit = TooltipHitAt(x, y);
		if (!hit || hit->callback || hit->window->tooltip.empty())
			return std::nullopt;
		return std::pair{hit->window->tooltip, hit->window->tooltip_delay};
	}

	// The mouse over the radar (screen pixels): the window under it on the bar is the radar's (ControlBar.wnd:LeftHUD,
	// LeftHUDInput), the General's Powers screen and the shortcut bar not in front of it; and whether the local player has
	// a radar (rts::localPlayerHasRadar).
	bool OverRadar(float x, float y)
	{
		if (!m_loaded || !m_viewModel->shown.Get())
			return false;
		const auto in = [&](ShellMenu &menu) {
			const auto [lx, ly] = menu.ToLayout(x, y);
			return Engine::UI::WND::Hit_Test(menu.Document(), lx, ly).has_value();
		};
		if ((m_powers && m_powers->shown.Get() && in(m_powersMenu)) || (ShortcutShown() && in(m_shortcutMenu)))
			return false;
		const auto [lx, ly] = m_menu.ToLayout(x, y);
		auto &document = m_menu.Document();
		const auto hit = Engine::UI::WND::Hit_Test(document, lx, ly);
		return hit && document.Windows()[*hit].name == "ControlBar.wnd:LeftHUD";
	}
	bool HasRadar() { return m_loaded && m_game != nullptr && m_radar.Shown(*m_game); }

	// What the bar's build tooltip is told this frame (main, from the game): tooltips off while a military caption shows
	// (InGameUI::areTooltipsDisabled: militarySubtitle's disableTooltipsUntil), and a button's popup refused in a replay,
	// under the quit menu (ControlBar::showBuildTooltipLayout).
	void SetBuildTooltipGates(bool tooltipsDisabled, bool blocked) noexcept
	{
		m_tooltipsDisabled = tooltipsDisabled;
		m_buildTooltipBlocked = blocked;
	}

	// Whether a window takes the left button going down at (x, y) (screen pixels): a gadget there, in front first.
	bool Grabs(float x, float y)
	{
		const auto gadget = [&](ShellMenu &menu) {
			const auto [lx, ly] = menu.ToLayout(x, y);
			return Engine::UI::WND::Gadget_At(menu.Document(), lx, ly).has_value();
		};
		return (m_powers && m_powers->shown.Get() && gadget(m_powersMenu)) || (ShortcutShown() && gadget(m_shortcutMenu)) || gadget(m_menu);
	}

	// The level's radar picture for this match.
	void SetRadarTerrain(engine::level::presentation::RadarTerrain terrain) { m_radar.SetTerrain(std::move(terrain)); }

	void Draw(Graphics::Renderer2D &renderer)
	{
		if (!m_loaded || !m_viewModel->shown.Get())
			return;
		DrawScheme(renderer, true);
		m_menu.Draw(renderer);
		DrawPowerBar(renderer);
		DrawExperienceBar(renderer);
		DrawRadar(renderer);
		DrawScheme(renderer, false);
		if (ShortcutShown())
			m_shortcutMenu.Draw(renderer);
		if (m_powers && m_powers->shown.Get())
			m_powersMenu.Draw(renderer);
		// The build tooltip, made after the bar's layouts: in front of them.
		if (m_popupLoaded && m_buildTooltip.shown)
			m_popupMenu.Draw(renderer);
	}

	// The object whose commands show (the builder a placement is for).
	ecs::Entity Selected() const noexcept { return m_loaded ? m_viewModel->Selected() : ecs::Entity{}; }

	// A structure awaiting its place, once: for the selected builder (DOZER_CONSTRUCT), or for a SPECIAL_POWER_CONSTRUCT
	// button (`power`, `options`): from the selected object, or from the shortcut bar's most ready source (`source`).
	struct Placement
	{
		std::string structure;
		std::string power;
		std::uint32_t options{0};
		ecs::Entity source;
	};
	std::optional<Placement> TakePlacement()
	{
		if (m_shortcutPlacement)
			return std::exchange(m_shortcutPlacement, std::nullopt);
		if (!m_loaded || m_viewModel->placing.Get().empty())
			return std::nullopt;
		Placement placement{m_viewModel->placing.Get(), m_viewModel->placingPower, m_viewModel->placingOptions, m_viewModel->Selected()};
		m_viewModel->placing.Set({});
		return placement;
	}

	// A shortcut power that went into waiting for its target, once: its button, the object it fires from and its power's
	// type.
	struct ShortcutTargeting
	{
		std::string button;
		ecs::Entity source;
		std::string type;
	};
	std::optional<ShortcutTargeting> TakeShortcutTargeting() { return std::exchange(m_shortcutTargeting, std::nullopt); }

	// A command button that went into waiting for its target, once; empty: none.
	std::string TakeTargeting()
	{
		if (!m_loaded || m_viewModel->targeting.Get().empty())
			return {};
		std::string button = m_viewModel->targeting.Get();
		m_viewModel->targeting.Set({});
		return button;
	}

private:
	// The window holding the mouse's tooltip in front (the General's Powers screen, the shortcut bar, the bar), with
	// whether its tooltip is a callback (commandButtonTooltip: the build tooltip), as Engine::UI::WND::Tooltip_Target
	// finds it; none while a window holds the mouse or off the windows.
	struct TooltipHit
	{
		const ShellMenu *menu{nullptr};
		const Engine::UI::WND::WNDWindow *window{nullptr};
		bool callback{false};
	};

	// ControlBar::init / setControlCommand's commandButtonTooltip windows: the command buttons, the communicator,
	// options, idle worker, beacon, general and up-down buttons, the power, money and experience displays, the General's
	// Powers screen's science buttons (populatePurchaseScience) and the shortcut bar's buttons (populateSpecialPowerShortcut);
	// a layout's own TOOLTIPCALLBACK too.
	bool CommandTooltipWindow(const Engine::UI::WND::WNDWindow &window) const
	{
		static constexpr std::array<std::string_view, 9> Commanded{"ControlBar.wnd:PopupCommunicator", "ControlBar.wnd:ButtonOptions",
			"ControlBar.wnd:ButtonIdleWorker", "ControlBar.wnd:ButtonPlaceBeacon", "ControlBar.wnd:ButtonGeneral", "ControlBar.wnd:ButtonLarge",
			"ControlBar.wnd:PowerWindow", "ControlBar.wnd:MoneyDisplay", "ControlBar.wnd:GeneralsExp"};
		return !window.tooltip_callback.empty() || window.name.starts_with("ControlBar.wnd:ButtonCommand") ||
			window.name.starts_with("GeneralsExpPoints.wnd:ButtonRank") ||
			(!m_shortcutLayout.empty() && window.name.starts_with(m_shortcutLayout + ":ButtonCommand")) ||
			std::ranges::find(Commanded, std::string_view(window.name)) != Commanded.end();
	}

	std::optional<TooltipHit> TooltipHitAt(float x, float y) const
	{
		if (!m_loaded || m_grabbed)
			return std::nullopt;
		const auto over = [&](const ShellMenu &menu) -> std::optional<TooltipHit> {
			const auto &document = const_cast<ShellMenu &>(menu).Document();
			const auto [lx, ly] = menu.ToLayout(x, y);
			const auto windows = document.Windows();
			const Engine::UI::WND::WNDTooltipTarget target = Engine::UI::WND::Tooltip_Target(document, lx, ly, false,
				[&](Engine::UI::WND::NodeIndex node, Engine::UI::WND::WNDTooltipPart) { return CommandTooltipWindow(windows[node]); });
			if (!target.Found())
				return std::nullopt;
			const Engine::UI::WND::WNDWindow &window = windows[target.window];
			const bool callbackPart =
				target.part == Engine::UI::WND::WNDTooltipPart::Window || target.part == Engine::UI::WND::WNDTooltipPart::ComboList;
			return TooltipHit{&menu, &window, callbackPart && CommandTooltipWindow(window)};
		};
		if (m_powers && m_powers->shown.Get())
			if (const auto found = over(m_powersMenu))
				return found;
		if (ShortcutShown())
			if (const auto found = over(m_shortcutMenu))
				return found;
		return over(m_menu);
	}

	// GadgetButtonGetData: the command button a window carries (setControlCommand), by its name; none: not a button with
	// one.
	const content::CommandButtonContent *ButtonOf(std::string_view name) const
	{
		const session::SessionView *view = m_game != nullptr ? m_game->View() : nullptr;
		if (view == nullptr)
			return nullptr;
		const auto number = [](std::string_view digits) {
			int value = 0;
			std::from_chars(digits.data(), digits.data() + digits.size(), value);
			return value;
		};
		if (name.starts_with("ControlBar.wnd:ButtonCommand"))
			return m_viewModel->CommandButtonAt(static_cast<std::size_t>(number(name.substr(28)) - 1));
		if (name.starts_with("GeneralsExpPoints.wnd:ButtonRank") && m_powers)
		{
			// ButtonRank<1|3|8>Number<n>.
			const std::string_view rest = name.substr(32);
			const std::size_t split = rest.find("Number");
			if (split == std::string_view::npos)
				return nullptr;
			const int rank = number(rest.substr(0, split));
			const std::size_t row = rank == 1 ? 0 : rank == 3 ? 1 : 2;
			return m_powers->SlotButton(row, static_cast<std::size_t>(number(rest.substr(split + 6))));
		}
		if (m_shortcut && !m_shortcutLayout.empty() && name.starts_with(m_shortcutLayout + ":ButtonCommand"))
			return m_shortcut->SlotButton(static_cast<std::size_t>(number(name.substr(m_shortcutLayout.size() + 14)) - 1));
		static constexpr std::array<std::pair<std::string_view, std::string_view>, 6> NonCommands{{{"ControlBar.wnd:PopupCommunicator", "NonCommand_Communicator"},
			{"ControlBar.wnd:ButtonOptions", "NonCommand_Options"}, {"ControlBar.wnd:ButtonIdleWorker", "NonCommand_IdleWorker"},
			{"ControlBar.wnd:ButtonPlaceBeacon", "NonCommand_Beacon"}, {"ControlBar.wnd:ButtonGeneral", "NonCommand_GeneralsExperience"},
			{"ControlBar.wnd:ButtonLarge", "NonCommand_UpDown"}}};
		for (const auto &[window, button] : NonCommands)
			if (name == window)
				return view->Content().commands.Button(button);
		return nullptr;
	}

	// The windows the popup fills (generic ones: GWS_USER_WINDOW or GWS_STATIC_TEXT, the power, money and experience).
	static bool GenericTooltipWindow(std::string_view name)
	{
		return name == "ControlBar.wnd:PowerWindow" || name == "ControlBar.wnd:MoneyDisplay" || name == "ControlBar.wnd:GeneralsExp";
	}

	// commandButtonTooltip -> ControlBar::showBuildTooltipLayout, each frame its window holds the mouse's tooltip.
	void BuildTooltipFrame(float x, float y, std::uint32_t now)
	{
		if (!m_popupLoaded)
			return;
		const std::optional<TooltipHit> hit = TooltipHitAt(x, y);
		if (!hit || !hit->callback)
			return;
		const std::string_view name = hit->window->name;
		const bool generic = GenericTooltipWindow(name);
		const content::CommandButtonContent *button = generic ? nullptr : ButtonOf(name);
		// A push button without a command, or one in a replay or under the quit menu, never fills it.
		const bool blocked = !generic && (button == nullptr || m_buildTooltipBlocked);
		switch (hud::ShowBuildTooltip(m_buildTooltip, name, hit->window->tooltip_delay, now, m_tooltipsDisabled, blocked))
		{
		case hud::BuildTooltipStep::Nothing: break;
		case hud::BuildTooltipStep::Hide: break; // drawn only while shown
		case hud::BuildTooltipStep::Populate:
			m_buildTooltipWindow = std::string(name);
			if (!PopulatePopup())
				m_buildTooltip.shown = false;
			break;
		}
	}

	// ControlBar::populateBuildTooltipLayout for the window it was asked for: the texts (hud::PopulateBuildTooltip), the
	// cost line shown only above 0, and the layout sized to the description wrapped 10 pixels short of its window
	// (its parent grown or shrunk by as much and moved up by it, never under 102 pixels tall, the description window
	// grown with it). False: nothing to fill it with.
	bool PopulatePopup()
	{
		session::SessionView *view = m_game != nullptr ? m_game->View() : nullptr;
		if (view == nullptr || !m_labels)
			return false;
		hud::BuildTooltipText text;
		if (GenericTooltipWindow(m_buildTooltipWindow))
		{
			// getCurrentlyViewedPlayer's energy (the local player's; an observer's none here).
			std::optional<std::pair<std::int64_t, std::int64_t>> energy;
			if (m_game->LocalPlayer())
				energy = std::pair<std::int64_t, std::int64_t>{m_viewModel->powerProduced.Get(), m_viewModel->powerConsumed.Get()};
			const auto filled = hud::PopulateWindowTooltip(m_buildTooltipWindow, energy, m_labels);
			if (!filled)
				return false;
			text = *filled;
		}
		else
		{
			const content::CommandButtonContent *button = ButtonOf(m_buildTooltipWindow);
			const std::optional<std::uint32_t> player = m_game->LocalPlayer();
			if (button == nullptr || !player)
				return false;
			// TheInGameUI->getFirstSelectedDrawable.
			const std::vector<ecs::Entity> selection = m_game->Selection();
			const ecs::Entity selected = selection.empty() ? ecs::Entity{} : selection.front();
			text = hud::PopulateBuildTooltip(*button, view->BuildTooltip(*player, selected, *button), view->Content(), m_labels);
		}
		auto &document = m_popupMenu.Document();
		const float sx = m_popupMenu.ScaleX(), sy = m_popupMenu.ScaleY();
		if (Engine::UI::WND::WNDWindow *name = document.Find_Window("ControlBarPopupDescription.wnd:StaticTextName"))
			name->text = text.name;
		if (Engine::UI::WND::WNDWindow *cost = document.Find_Window("ControlBarPopupDescription.wnd:StaticTextCost"))
		{
			document.Set_Window_Flag(cost->name, Engine::UI::WND::WindowFlag::Hidden, text.costToBuild <= 0);
			if (text.costToBuild > 0)
				cost->text = text.cost;
		}
		Engine::UI::WND::WNDWindow *description = document.Find_Window("ControlBarPopupDescription.wnd:StaticTextDescription");
		const Engine::UI::WND::NodeIndex root = document.Root();
		if (description != nullptr && root != Engine::UI::WND::Invalid_Node)
		{
			// In the screen's pixels, as the original's windows are made at its resolution.
			const int width = static_cast<int>(static_cast<float>(description->screen_region.right - description->screen_region.left) * sx);
			const int height = static_cast<int>(static_cast<float>(description->screen_region.bottom - description->screen_region.top) * sy);
			int textHeight = 0;
			if (description->font != nullptr)
			{
				Engine::UI::WND::TextLayoutOptions wrap;
				wrap.wrapping_width = width - 10;
				std::uint32_t measuredWidth = 0, measuredHeight = 0;
				if (Engine::UI::WND::Get_Text_Renderer().Measure(*description->font, reinterpret_cast<const std::uint16_t *>(text.description.c_str()), wrap,
						measuredWidth, measuredHeight))
					textHeight = static_cast<int>(measuredHeight);
			}
			int difference = textHeight - height;
			const Engine::UI::WND::WNDWindow parent = document.Windows()[root];
			const int parentWidth = parent.screen_region.right - parent.screen_region.left;
			const int parentHeight = static_cast<int>(static_cast<float>(parent.screen_region.bottom - parent.screen_region.top) * sy);
			if (parentHeight + difference < 102)
				difference = 102 - parentHeight;
			const auto layout = [sy](int pixels) { return static_cast<int>(std::lround(static_cast<float>(pixels) / sy)); };
			document.Resize_Window(parent.name, parentWidth, layout(parentHeight + difference));
			document.Move_Window(parent.name, parent.screen_region.left, parent.screen_region.top - layout(difference));
			if (Engine::UI::WND::WNDWindow *grown = document.Find_Window("ControlBarPopupDescription.wnd:StaticTextDescription"))
			{
				document.Resize_Window(grown->name, grown->screen_region.right - grown->screen_region.left, layout(height + difference));
				grown->text = text.description;
			}
		}
		return m_popupMenu.Refresh();
	}

	// ControlBar::init: GeneralsExpPoints.wnd made hidden; the general's button toggles it. Without the layout the bar
	// works on without it.
	void LoadGeneralsPowers(const engine::filesystem::VirtualFileSystem &files, const engine::localization::StringTable &strings, GameClient &game,
		std::uint32_t width, std::uint32_t height, float fontScale)
	{
		std::string error;
		if (!m_powersMenu.Load(files, "Window/GeneralsExpPoints.wnd", strings, width, height, error, fontScale))
			return;
		// SCIENCE:Rank<n>: the screen's title per rank.
		m_rankTitles.clear();
		for (int level = 1; level <= 10; ++level)
			m_rankTitles.push_back(Localized(strings, std::format("SCIENCE:Rank{}", level)));
		const std::string starOff = m_scheme != nullptr ? std::string(m_scheme->Image("GeneralButtonEnable")) : std::string{};
		const std::string starOn = m_scheme != nullptr ? std::string(m_scheme->Image("GeneralButtonHightlited")) : std::string{};
		m_powers = std::make_unique<hud::GeneralsPowersViewModel>([&game](commands::GameCommand command) { game.Submit(command); },
			[this](std::int32_t level) {
				return level >= 1 && static_cast<std::size_t>(level) <= m_rankTitles.size() ? m_rankTitles[static_cast<std::size_t>(level - 1)] : std::u16string{};
			},
			starOff, starOn);
		m_powers->SetLabels([&strings](std::string_view label) { return Localized(strings, label); });
		m_powers->SetSound([&game](std::string_view sound) { game.PlayInterfaceSound(sound); });
		// ControlBarScheme::init: the screen's backdrop is the scheme's PowerPurchaseImage, as big as the image scaled from
		// the scheme's resolution to the screen.
		if (m_scheme != nullptr && m_scheme->width > 0 && m_scheme->height > 0)
			if (const std::string_view backdrop = m_scheme->Image("PowerPurchaseImage"); !backdrop.empty())
				if (Engine::UI::WND::WNDWindow *parent = m_powersMenu.Document().Find_Window("GeneralsExpPoints.wnd:GenExpParent"))
				{
					parent->draw_states[0].cells[0].image_name = std::string(backdrop);
					parent->draw_states[0].cells[0].image = m_powersMenu.Image(backdrop);
					if (const auto size = m_powersMenu.ImageSize(backdrop))
						m_powersMenu.Document().Resize_Window("GeneralsExpPoints.wnd:GenExpParent",
							static_cast<int>(static_cast<float>(size->first) * static_cast<float>(width) / static_cast<float>(m_scheme->width) / m_powersMenu.ScaleX()),
							static_cast<int>(static_cast<float>(size->second) * static_cast<float>(height) / static_cast<float>(m_scheme->height) / m_powersMenu.ScaleY()));
				}
		m_powersBindings.emplace(m_powersMenu.Document());
		m_powersView.emplace(*m_powersBindings, m_powersMenu.Document(), *m_bindings, *m_powers,
			[this](std::string_view name) { return m_powersMenu.Image(name); }, [this](std::string_view name) { return m_menu.Image(name); });
		m_powersMenu.Refresh();
	}

public:
	// HotKeyTranslator: a key released with no modifier, as its printable character: the button it marks (true: used),
	// the one map asked in the order the original registers into it: the command windows, the shortcut bar's buttons
	// (populateSpecialPowerShortcut after evaluateContextUI), then the General's Powers screen's while it shows.
	bool HotKey(char key)
	{
		if (!m_loaded || !m_viewModel)
			return false;
		if (m_shortcut && m_powers)
			return hud::PressFirstHotKey(key, *m_viewModel, *m_shortcut, *m_powers);
		if (m_shortcut)
			return hud::PressFirstHotKey(key, *m_viewModel, *m_shortcut);
		if (m_powers)
			return hud::PressFirstHotKey(key, *m_viewModel, *m_powers);
		return m_viewModel->PressHotKey(key);
	}

	// MSG_META_TOGGLE_CONTROL_BAR: ToggleControlBar (not while a replay plays back).
	void ToggleHidden(bool replay)
	{
		if (m_viewModel)
		{
			const bool wasHidden = m_viewModel->Hidden();
			m_viewModel->ToggleHidden(replay);
			// ToggleControlBar showing it again: switchControlBarStage(CONTROL_BAR_STAGE_DEFAULT), shown at once.
			if (wasHidden && !m_viewModel->Hidden() && m_stage)
				m_stage->Show(true);
			// Hiding it: TheTacticalView->setHeight(TheDisplay->getHeight()).
			else if (!wasHidden && m_viewModel->Hidden() && m_stage)
				m_stage->Hide();
		}
	}

private:

	// TheTacticalView's height in display pixels as the bar's stage and showing set it (none before the bar is loaded).
	std::optional<int> ViewHeight() const { return m_stage ? std::optional(m_stage->viewHeight.Get()) : std::nullopt; }

	bool ShortcutShown() const { return m_shortcut && m_viewModel && m_viewModel->shown.Get() && m_shortcut->shown.Get(); }

	// ControlBar::initSpecialPowershortcutBar: the local player's side's bar layout, made hidden until the bar may show.
	// A side without one (or a layout not found) has none.
	void LoadShortcutBar(const engine::filesystem::VirtualFileSystem &files, const engine::localization::StringTable &strings, GameClient &game,
		std::uint32_t width, std::uint32_t height, float fontScale)
	{
		session::SessionView *view = game.View();
		const std::optional<std::uint32_t> player = game.LocalPlayer();
		if (view == nullptr)
			return;
		const hud::ShortcutBarState state = hud::ReadShortcutBar(*view, player);
		if (state.layout.empty())
			return;
		std::string error;
		if (!m_shortcutMenu.Load(files, "Window/" + state.layout, strings, width, height, error, fontScale))
			return;
		m_shortcutLayout = state.layout;
		m_shortcutMenu.Document().Set_Button_Overlays(m_shortcutMenu.Image("Cameo_hilited"), m_shortcutMenu.Image("Cameo_push"));
		m_shortcut = std::make_unique<hud::ShortcutBarViewModel>([&game](commands::GameCommand command) { game.Submit(command); },
			[this, view](std::string button, ecs::Entity source) {
				const content::CommandButtonContent *pressed = view->Content().commands.Button(button);
				const auto power = pressed != nullptr ? view->Content().powers.Template(pressed->specialPower) : std::nullopt;
				m_shortcutTargeting = ShortcutTargeting{std::move(button), source, power ? view->Content().powers.templates[*power].type : std::string{}};
			},
			[&game](std::vector<ecs::Entity> objects) { game.SelectOnly(objects); },
			[&game](const content::CommandButtonContent &button) {
				hud::ShortcutSources found;
				session::SessionView *now = game.View();
				const std::optional<std::uint32_t> local = game.LocalPlayer();
				if (now == nullptr || !local)
					return found;
				if (button.commandName == "SELECT_ALL_UNITS_OF_TYPE")
					found.objects = now->ObjectsOfType(*local, button.object);
				else if (const auto power = now->Content().powers.Template(button.specialPower))
					found.power = now->ShortcutPowerSource(*local, now->Content().powers.templates[*power].type);
				return found;
			});
		m_shortcut->SetLabels([&strings](std::string_view label) { return Localized(strings, label); });
		m_shortcut->SetSound([&game](std::string_view sound) { game.PlayInterfaceSound(sound); });
		m_shortcut->SetPlace([this](std::string structure, ecs::Entity source, std::string power, std::uint32_t options) {
			m_shortcutPlacement = Placement{std::move(structure), std::move(power), options, source};
		});
		m_shortcutBindings.emplace(m_shortcutMenu.Document());
		m_shortcutView.emplace(*m_shortcutBindings, m_shortcutMenu.Document(), m_shortcutLayout, *m_shortcut,
			[this](std::string_view name) { return m_shortcutMenu.Image(name); }, m_clockColor);
		m_shortcutMenu.Refresh();
	}

	std::u16string Money(std::int64_t money) const
	{
		std::u16string text = m_moneyPattern.empty() ? std::u16string(u"$%d") : m_moneyPattern;
		const std::string digits = std::to_string(money);
		const std::u16string number(digits.begin(), digits.end());
		if (const auto at = text.find(u"%d"); at != std::u16string::npos)
			text.replace(at, 2, number);
		else
			text += number;
		return text;
	}

	// The left HUD's radar (W3DLeftHUDDraw), where ControlBar.wnd:LeftHUD is on screen.
	std::optional<Graphics::Rect2D> RadarRect()
	{
		const Engine::UI::WND::WNDWindow *hud = m_menu.Document().Find_Window("ControlBar.wnd:LeftHUD");
		if (hud == nullptr)
			return std::nullopt;
		const auto &r = hud->screen_region;
		return Graphics::Rect2D{static_cast<float>(r.left) * m_menu.ScaleX(), static_cast<float>(r.top) * m_menu.ScaleY(),
			static_cast<float>(r.right) * m_menu.ScaleX(), static_cast<float>(r.bottom) * m_menu.ScaleY()};
	}

	void DrawRadar(Graphics::Renderer2D &renderer)
	{
		const auto rect = RadarRect();
		if (!rect || m_game == nullptr)
			return;
		m_radarList.Clear();
		const auto hero = m_menu.ImageSize("HeroReticle");
		m_radar.Draw(*m_game, m_radarList, renderer, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, m_menu.Image("HeroReticle"),
			hero ? std::pair{static_cast<float>(hero->first), static_cast<float>(hero->second)} : std::pair{0.0f, 0.0f});
		m_radarRenderer.Render_Draw_List(m_radarList, renderer);
	}

	// ControlBarScheme::init: the windows the scheme places (at its UL..LR, from its resolution to the layout's), with
	// the button art it gives them (enabled, highlighted and disabled draw states).
	void PlaceSchemeWindows(const content::ControlBarSchemeContent &scheme)
	{
		// hud::SchemeWindows on the layout's own resolution (the bar is then fitted to the screen as a whole), moved by
		// however far the fit has already moved the layout.
		auto &document = m_menu.Document();
		const auto looks = hud::SchemeWindows(scheme, m_menu.AuthoredWidth(), m_menu.AuthoredHeight(),
			[&document](std::string_view window) { return document.Find_Window(window) != nullptr; }, {});
		const auto setImage = [this](Engine::UI::WND::WNDWindow &window, std::size_t state, std::size_t cell, const std::string &image) {
			if (cell >= window.draw_states[state].cells.size())
				return;
			window.draw_states[state].cells[cell].image_name = image;
			window.draw_states[state].cells[cell].image = image.empty() ? Engine::UI::WND::ImageRef{} : m_menu.Image(image);
		};
		for (const hud::SchemeWindowLook &look : looks)
		{
			if (look.placed)
			{
				document.Move_Window(look.window, look.screenX + m_fitShift.first, look.screenY + m_fitShift.second + BarOffsetUnits());
				document.Resize_Window(look.window, look.width, look.height);
			}
			Engine::UI::WND::WNDWindow *window = document.Find_Window(look.window);
			if (window == nullptr)
				continue;
			if (look.buttonImages)
			{
				// GadgetButtonSetEnabledImage (cell 0 of enabled, clearing its segment cells 5 and 6), SetHiliteImage,
				// SetHiliteSelectedImage (the hilite state's cell 1), SetDisabledImage.
				setImage(*window, 0, 0, look.enabled);
				setImage(*window, 0, 5, std::string{});
				setImage(*window, 0, 6, std::string{});
				setImage(*window, 2, 0, look.hilite);
				setImage(*window, 2, 1, look.hiliteSelected);
				setImage(*window, 1, 0, look.disabled);
			}
			else if (look.cellImages)
			{
				// winSetEnabledImage(0) / winSetDisabledImage(0).
				setImage(*window, 0, 0, look.enabled);
				if (look.window == "ControlBar.wnd:WinUAttack")
					setImage(*window, 1, 0, look.disabled);
			}
		}
		// updateBuildQueueDisabledImages: each production queue button's disabled image is the scheme's QueueButtonImage
		// (none named: left as it is).
		if (!scheme.queueButtonImage.empty())
			for (std::size_t slot = 0; slot < hud::QueueButtons; ++slot)
				if (Engine::UI::WND::WNDWindow *queue = document.Find_Window(hud::QueueWindowName(slot)))
					setImage(*queue, 1, 0, scheme.queueButtonImage);
		// updateCommanBarBorderColors: the command buttons' borders by kind; updateSlotExitImage: the exit buttons' art.
		if (m_viewModel)
		{
			m_viewModel->SetBorderColors(scheme.borderBuild, scheme.borderUpgrade, scheme.borderAction, scheme.borderSystem);
			m_viewModel->SetSlotExitImage(std::string(scheme.Image("CommandMarkerImage")));
		}
		// ControlBarScheme::init starts with switchControlBarStage(CONTROL_BAR_STAGE_DEFAULT), then updateUpDownImages.
		if (m_stage)
			m_stage->Switch(hud::ControlBarStage::Default);
		ApplyToggleImages(scheme, m_stage && m_stage->minimised.Get());
	}

	// ControlBar::setUpDownImages: the scheme does not dress the minimise button itself (its MinMaxButton images are never
	// set); the bar's stage does: minimised (CONTROL_BAR_STAGE_LOW) ToggleButtonUpOn, pointed at ToggleButtonUpIn,
	// pressed while pointed at ToggleButtonUpPushed; at any other stage the ToggleButtonDown ones (GadgetButtonSetEnabledImage,
	// SetHiliteImage, SetHiliteSelectedImage).
	void ApplyToggleImages(const content::ControlBarSchemeContent &scheme, bool minimised)
	{
		if (Engine::UI::WND::WNDWindow *toggle = m_menu.Document().Find_Window("ControlBar.wnd:ButtonLarge"))
		{
			const auto images = scheme.MinimizeButtonImages(minimised);
			for (const auto &[state, cell, image] : {std::tuple{0, 0, images.enabled}, std::tuple{2, 0, images.hilite}, std::tuple{2, 1, images.hiliteSelected}})
				if (!image.empty() && static_cast<std::size_t>(cell) < toggle->draw_states[state].cells.size())
				{
					toggle->draw_states[state].cells[cell].image_name = std::string(image);
					toggle->draw_states[state].cells[cell].image = m_menu.Image(image);
				}
		}
	}

	// ControlBarParent's top in screen pixels as laid out (m_defaultControlBarPosition), and the bar moved to `top`
	// (winSetPosition): the layout's windows and the scheme's art (the marker windows move with it) follow.
	int LaidOutTop() const
	{
		const Engine::UI::WND::WNDWindow *parent = m_menu.Document().Find_Window("ControlBar.wnd:ControlBarParent");
		return parent != nullptr ? static_cast<int>(static_cast<float>(m_barAuthoredTop) * m_menu.ScaleY()) : 0;
	}
	// The bar's movement in layout units (windows placed while it is moved go with it).
	int BarOffsetUnits() const
	{
		return m_menu.ScaleY() > 0.0f ? static_cast<int>(std::lround(static_cast<float>(m_barOffset) / m_menu.ScaleY())) : 0;
	}
	void MoveBar(int top)
	{
		const int offset = top - LaidOutTop();
		if (offset == m_barOffset)
			return;
		m_barOffset = offset;
		const Engine::UI::WND::WNDWindow *parent = m_menu.Document().Find_Window("ControlBar.wnd:ControlBarParent");
		if (parent == nullptr || m_menu.ScaleY() <= 0.0f)
			return;
		m_menu.Document().Move_Window("ControlBar.wnd:ControlBarParent", parent->screen_region.left,
			m_barAuthoredTop + static_cast<int>(std::lround(static_cast<float>(offset) / m_menu.ScaleY())));
		m_menu.Refresh();
	}

	// W3DCommandBarGenExpDraw: the local player's progress into their rank as a bar rising up the GeneralsExp window
	// (none for a player no longer playing: no powers state then), in screen pixels.
	void DrawExperienceBar(Graphics::Renderer2D &renderer)
	{
		const Engine::UI::WND::WNDWindow *window = m_menu.Document().Find_Window("ControlBar.wnd:GeneralsExp");
		if (window == nullptr || Engine::UI::WND::Has_Flag(window->flags, Engine::UI::WND::WindowFlag::Hidden) || !m_powers || !m_powers->generalEnabled.Get())
			return;
		const auto bottom = m_menu.ImageSize("GenExpBarBottom1"), centre = m_menu.ImageSize("GenExpBar1"), top = m_menu.ImageSize("GenExpBarTop1");
		if (!bottom || !centre || !top)
			return;
		const float left = static_cast<float>(window->screen_region.left) * m_menu.ScaleX();
		const float right = static_cast<float>(window->screen_region.right) * m_menu.ScaleX();
		const float above = static_cast<float>(window->screen_region.top) * m_menu.ScaleY();
		const int height = static_cast<int>(static_cast<float>(window->screen_region.bottom - window->screen_region.top) * m_menu.ScaleY());
		const auto quads = presentation::LayOutExperienceBar(m_powers->progress.Get(), height, bottom->second, centre->second, top->second);
		if (quads.empty())
			return;
		m_list.Clear();
		for (const presentation::ExperienceQuad &quad : quads)
		{
			Engine::UI::WND::ImageRef image = m_menu.Image(quad.piece == presentation::ExperiencePiece::Bottom ? "GenExpBarBottom1"
					: quad.piece == presentation::ExperiencePiece::Top                                        ? "GenExpBarTop1"
																											  : "GenExpBar1");
			const float v0 = image.uv.top, v1 = image.uv.bottom;
			image.uv.top = v0 + (v1 - v0) * quad.vTop;
			image.uv.bottom = v0 + (v1 - v0) * quad.vBottom;
			m_list.Add_Image(image, {left, above + static_cast<float>(quad.top), right, above + static_cast<float>(quad.bottom)}, {1.0f, 1.0f, 1.0f, 1.0f});
		}
		m_renderer.Render_Draw_List(m_list, renderer);
	}

	// The right HUD (ControlBar.wnd:RightHUD, drawn by W3DRightHUDDraw): its image the scheme's RightHUDImage
	// (ControlBarScheme::init -> updateRightHUDImage: winSetEnabledImage), drawn alone while its IMAGE status is on and
	// nothing at all otherwise (W3DRightHUDDraw draws the default look only with WIN_STATUS_IMAGE; never its colour).
	void DressRightHud()
	{
		Engine::UI::WND::WNDWindow *hud = m_menu.Document().Find_Window("ControlBar.wnd:RightHUD");
		if (hud == nullptr)
			return;
		for (auto &state : hud->draw_states)
			for (auto &cell : state.cells)
			{
				cell.color.alpha = 0.0f;
				cell.border_color.alpha = 0.0f;
			}
		if (m_scheme != nullptr)
			if (const std::string_view image = m_scheme->RightHudImage(); !image.empty())
			{
				hud->draw_states[0].cells[0].image_name = std::string(image);
				hud->draw_states[0].cells[0].image = m_menu.Image(image);
			}
	}

	// setPortraitByObject / setPortraitByImage: no portrait (nothing selected, or the build queue in its place) sets the
	// right HUD's IMAGE status, a portrait clears it.
	void ShowRightHudImage(bool shown)
	{
		if (Engine::UI::WND::WNDWindow *hud = m_menu.Document().Find_Window("ControlBar.wnd:RightHUD"))
			hud->image_style = shown;
	}

	// W3DPowerDraw: the power made as a green, yellow or red strip tiled from the window's left, the power used as the
	// slider on the same scale, in screen pixels at the images' own sizes.
	void DrawPowerBar(Graphics::Renderer2D &renderer)
	{
		const Engine::UI::WND::WNDWindow *window = m_menu.Document().Find_Window("ControlBar.wnd:PowerWindow");
		if (window == nullptr || Engine::UI::WND::Has_Flag(window->flags, Engine::UI::WND::WindowFlag::Hidden))
			return;
		const auto slider = m_menu.ImageSize("PowerBarSlider");
		if (!slider)
			return;
		const float left = static_cast<float>(window->screen_region.left) * m_menu.ScaleX();
		const float top = static_cast<float>(window->screen_region.top) * m_menu.ScaleY();
		const int width = static_cast<int>(static_cast<float>(window->screen_region.right - window->screen_region.left) * m_menu.ScaleX());
		const float bottom = static_cast<float>(window->screen_region.bottom) * m_menu.ScaleY();
		const presentation::PowerBarLayout layout =
			presentation::LayOutPowerBar(m_viewModel->powerProduced.Get(), m_viewModel->powerConsumed.Get(), width, slider->first, m_powerBar);
		if (!layout.shown)
			return;
		static constexpr std::array<const char *, 3> strips{"PowerPointG", "PowerPointY", "PowerPointR"};
		const char *strip = strips[static_cast<std::size_t>(layout.color)];
		const auto stripSize = m_menu.ImageSize(strip);
		if (!stripSize)
			return;
		m_list.Clear();
		float cursor = 0.0f;
		if (layout.range > 0)
			Engine::UI::WND::Add_Tiled_Image(m_list, m_menu.Image(strip), {left, top, left + static_cast<float>(layout.range), bottom},
				static_cast<float>(stripSize->first), cursor);
		m_list.Add_Image(m_menu.Image("PowerBarSlider"),
			{left + static_cast<float>(layout.needleLeft), bottom - static_cast<float>(slider->second), left + static_cast<float>(layout.needleRight), bottom},
			{1.0f, 1.0f, 1.0f, 1.0f});
		m_renderer.Render_Draw_List(m_list, renderer);
	}

	// The match's players as the observer list sees them (ThePlayerList): each one's map and shown names, colour, its
	// PlayerTemplate's EnabledImage and FlagWaterMark, whether an observer or human, and its slot's team and computer flag.
	std::vector<hud::ObserverCandidate> ObserverPlayers() const
	{
		std::vector<hud::ObserverCandidate> players;
		if (m_game == nullptr)
			return players;
		session::SessionView *view = m_game->View();
		if (view == nullptr)
			return players;
		const std::vector<session::PlayerScore> scores = view->Scores();
		const std::vector<shell::ScorePlayer> board = m_game->ScoreBoard();
		for (std::size_t index = 0; index < board.size(); ++index)
		{
			const shell::ScorePlayer &row = board[index];
			hud::ObserverCandidate &player = players.emplace_back();
			player.player = static_cast<std::uint32_t>(index);
			player.name = row.playerName;
			player.displayName = row.name;
			player.color = row.color;
			player.observer = row.observer;
			player.human = index < scores.size() && scores[index].human;
			if (const content::PlayerTemplateInfo *faction = m_game->PlayerTemplates().Find(view->PlayerTemplateName(static_cast<std::uint32_t>(index))))
			{
				player.enabledImage = faction->enabledImage;
				player.flagImage = faction->flagWaterMark;
			}
			if (row.playerName.starts_with("player") && row.playerName.size() == 7 && row.playerName[6] >= '0' && row.playerName[6] < '8')
			{
				const auto &[team, ai] = m_slots[static_cast<std::size_t>(row.playerName[6] - '0')];
				player.team = team;
				player.ai = ai;
			}
		}
		return players;
	}

	// The scheme's images, scaled from the resolution it was laid out at: those behind the bar's windows (layers 3..5,
	// the deepest first) or those in front (2..0).
	void DrawScheme(Graphics::Renderer2D &renderer, bool behind)
	{
		if (m_scheme == nullptr || m_scheme->width <= 0 || m_scheme->height <= 0)
			return;
		// The bar keeps its aspect: the scheme's "display" is the fitted bar, placed where the fit puts it. The marker windows
		// (BackgroundMarker / ForegroundMarker) do not move here (no minimised stage), so W3DCommandBar*Draw's offset is 0.
		const auto fit = Engine::UI::WND::Fit_Viewport(m_scheme->width, m_scheme->height, static_cast<int>(m_width), static_cast<int>(m_height),
			Engine::UI::WND::LayoutAnchor::Center, Engine::UI::WND::LayoutAnchor::End);
		const int displayWidth = static_cast<int>(static_cast<double>(m_scheme->width) * fit.scale);
		const int displayHeight = static_cast<int>(static_cast<double>(m_scheme->height) * fit.scale);
		m_list.Clear();
		for (const hud::SchemeArtQuad &quad : m_look->Art(!behind, displayWidth, displayHeight, static_cast<int>(fit.x), static_cast<int>(fit.y) + m_barOffset))
			m_list.Add_Image(m_menu.Image(quad.image),
				{static_cast<float>(quad.left), static_cast<float>(quad.top), static_cast<float>(quad.right), static_cast<float>(quad.bottom)},
				{1.0f, 1.0f, 1.0f, 1.0f});
		m_renderer.Render_Draw_List(m_list, renderer);
	}

	ShellMenu m_menu;
	RadarMinimap m_radar;
	GameClient *m_game{nullptr};
	Engine::UI::WND::DrawList m_radarList;
	Engine::UI::WND::Renderer m_radarRenderer;
	ShellMenu m_powersMenu;
	std::unique_ptr<hud::GeneralsPowersViewModel> m_powers;
	std::optional<Engine::UI::WND::WNDBindings> m_powersBindings;
	std::optional<hud::GeneralsPowersView> m_powersView;
	Engine::UI::WND::WNDPointer m_powersPointer;
	std::vector<std::u16string> m_rankTitles;
	ShellMenu m_shortcutMenu;
	std::string m_shortcutLayout;
	std::unique_ptr<hud::ShortcutBarViewModel> m_shortcut;
	std::optional<Engine::UI::WND::WNDBindings> m_shortcutBindings;
	std::optional<hud::ShortcutBarView> m_shortcutView;
	Engine::UI::WND::WNDPointer m_shortcutPointer;
	std::optional<ShortcutTargeting> m_shortcutTargeting;
	std::optional<Placement> m_shortcutPlacement;
	Graphics::Color2D m_clockColor{0.0f, 0.0f, 0.0f, 100.0f / 255.0f};
	// The view model outlives the bindings (they unsubscribe from its observables when they go).
	std::unique_ptr<hud::ControlBarViewModel> m_viewModel;
	std::unique_ptr<hud::ObserverPanelViewModel> m_observer;
	// The match's game slots as the observer list reads them (team, computer), and whether it is a multiplayer game.
	std::array<std::pair<int, bool>, hud::ObserverButtons> m_slots{};
	bool m_multiplayer{true};
	engine::gui::mvvm::Command m_options; // declared before the bindings (they hold on to it)
	engine::gui::mvvm::Command m_communicator;
	std::optional<Engine::UI::WND::WNDBindings> m_bindings;
	std::optional<hud::ControlBarView> m_view;
	Engine::UI::WND::WNDPointer m_pointer;
	bool m_grabbed{false}; // a window took the left button down (GameWindowManager's m_grabWindow)
	// The build tooltip (ControlBarPopupDescription.wnd), its wait and showing (hud::BuildTooltipState), the game's texts,
	// what refuses it this frame, and its layout's last marker offset (populateBuildTooltipLayout's static lastOffset).
	ShellMenu m_popupMenu;
	bool m_popupLoaded{false};
	hud::BuildTooltipState m_buildTooltip;
	hud::TooltipLabels m_labels;
	bool m_tooltipsDisabled{false};
	bool m_buildTooltipBlocked{false};
	std::string m_buildTooltipWindow; // the window it was filled for (repopulateBuildTooltipLayout's prevWindow)
	content::ControlBarSchemes m_schemes;
	// ControlBarSchemeManager: the scheme in use and its animations (after m_schemes, which it reads).
	std::unique_ptr<hud::ControlBarSchemeLook> m_look;
	const content::ControlBarSchemeContent *m_scheme{nullptr};
	std::pair<int, int> m_fitShift{0, 0}; // how far fitting the bar to the screen moved its layout
	// The bar's stage and slide: ControlBarParent's laid-out top (layout units), how far it is moved now (screen pixels),
	// whether a replay plays back, the letterbox last seen and the minimise button's images last set.
	std::unique_ptr<hud::ControlBarStageViewModel> m_stage;
	int m_barAuthoredTop{0};
	int m_barOffset{0};
	bool m_replay{false};
	bool m_letterbox{false};
	bool m_toggleMinimised{false};
	std::string m_side;
	std::u16string m_moneyPattern;
	Engine::UI::WND::DrawList m_list;
	Engine::UI::WND::Renderer m_renderer;
	std::uint32_t m_width{1024}, m_height{768};
	presentation::PowerBarSettings m_powerBar;
	bool m_loaded{false};
};
}
