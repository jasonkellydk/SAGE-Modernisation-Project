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
import games.generalszh.hud.generals_powers_view;
import games.generalszh.hud.shortcut_bar_view;
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
		m_scheme = m_schemes.ForSide(m_side);
		if (m_scheme != nullptr && m_scheme->width > 0 && m_scheme->height > 0)
			PlaceSchemeWindows(*m_scheme);
		// The bar keeps its aspect, centred at the bottom of the screen (ControlBarScheme::init: Fit_Viewport Center, End).
		m_menu.Fit(width, height, Engine::UI::WND::LayoutAnchor::Center, Engine::UI::WND::LayoutAnchor::End);
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
		m_viewModel = std::make_unique<hud::ControlBarViewModel>([&game](commands::GameCommand command) { game.Submit(command); },
			[this](std::int64_t money) { return Money(money); });
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
			if (const auto *at = now->World().Get<engine::gameplay::Transform>(*next))
				game.UserLookAt(Engine::Math::ToFloat(at->position.x), Engine::Math::ToFloat(at->position.y));
		});
		// ControlBar::processCommandUI: a pressed button stops its CAMEO_FLASH.
		m_viewModel->SetPressed([&game](std::size_t slot, std::string_view button) {
			if (session::SessionView *now = game.View())
				if (auto *flashes = now->World().FindResource<presentation::CameoFlashes>())
					presentation::StopCameoFlash(*flashes, button, slot);
		});
		m_bindings.emplace(m_menu.Document());
		const std::array<int, 4> clock = m_scheme != nullptr ? m_scheme->buildUpClockColor : std::array<int, 4>{0, 0, 0, 100};
		m_view.emplace(*m_bindings, m_menu.Document(), *m_viewModel, [this](std::string_view name) { return m_menu.Image(name); },
			Graphics::Color2D{static_cast<float>(clock[0]) / 255.0f, static_cast<float>(clock[1]) / 255.0f, static_cast<float>(clock[2]) / 255.0f,
				static_cast<float>(clock[3]) / 255.0f});
		// The options button: the OPTIONS meta event (ToggleQuitMenu), as the host answers it.
		m_bindings->BindCommand("ControlBar.wnd:ButtonOptions", m_options);
		LoadGeneralsPowers(files, strings, game, width, height, fontScale);
		m_clockColor = Graphics::Color2D{static_cast<float>(clock[0]) / 255.0f, static_cast<float>(clock[1]) / 255.0f, static_cast<float>(clock[2]) / 255.0f,
			static_cast<float>(clock[3]) / 255.0f};
		LoadShortcutBar(files, strings, game, width, height, fontScale);
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
		const std::string side = player ? view->PlayerSide(*player) : std::string{};
		if (side != m_side || m_scheme == nullptr)
		{
			m_side = side;
			m_scheme = m_schemes.ForSide(side);
		}
		const std::vector<ecs::Entity> selection = game.Selection();
		hud::ControlBarState state = hud::ReadControlBar(*view, player, selection);
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
			if (m_powersBindings->TakeDirty())
				m_powersMenu.Refresh();
		}
		if (m_bindings->TakeDirty())
			m_menu.Refresh();
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
						m_game->Submit(commands::MoveTo{selection, {Engine::Math::Fixed::FromRaw(static_cast<std::int64_t>((*world)[0] * 65536.0f)),
							Engine::Math::Fixed::FromRaw(static_cast<std::int64_t>((*world)[1] * 65536.0f))}});
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
	}

	// The object whose commands show (the builder a placement is for).
	ecs::Entity Selected() const noexcept { return m_loaded ? m_viewModel->Selected() : ecs::Entity{}; }

	// A builder's structure awaiting its place (DOZER_CONSTRUCT), once; empty: none.
	std::string TakePlacement()
	{
		if (!m_loaded || m_viewModel->placing.Get().empty())
			return {};
		std::string placing = m_viewModel->placing.Get();
		m_viewModel->placing.Set({});
		return placing;
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
		struct Placed
		{
			const char *window;
			const char *place;
			const char *enabled;
			const char *hilite;
			const char *disabled;
		};
		static constexpr std::array<Placed, 9> placed{{
			{"ControlBar.wnd:PopupCommunicator", "Chat", "BuddyButtonEnable", "BuddyButtonHightlited", "BuddyButtonDisabled"},
			{"ControlBar.wnd:ButtonIdleWorker", "Worker", "IdleWorkerButtonEnable", "IdleWorkerButtonHightlited", "IdleWorkerButtonDisabled"},
			{"ControlBar.wnd:ButtonOptions", "Options", "OptionsButtonEnable", "OptionsButtonHightlited", "OptionsButtonDisabled"},
			{"ControlBar.wnd:ButtonPlaceBeacon", "Beacon", "BeaconButtonEnable", "BeaconButtonHightlited", "BeaconButtonDisabled"},
			{"ControlBar.wnd:MoneyDisplay", "Money", nullptr, nullptr, nullptr},
			{"ControlBar.wnd:PowerWindow", "PowerBar", nullptr, nullptr, nullptr},
			{"ControlBar.wnd:ButtonGeneral", "General", "GeneralButtonEnable", "GeneralButtonHightlited", "GeneralButtonDisabled"},
			{"ControlBar.wnd:ButtonLarge", "MinMax", nullptr, nullptr, nullptr},
			{"ControlBar.wnd:WinUAttack", "UAttack", "UAttackButtonEnable", nullptr, "UAttackButtonHightlited"},
		}};
		const float fx = static_cast<float>(m_menu.AuthoredWidth()) / static_cast<float>(scheme.width);
		const float fy = static_cast<float>(m_menu.AuthoredHeight()) / static_cast<float>(scheme.height);
		auto &document = m_menu.Document();
		for (const Placed &each : placed)
		{
			const content::SchemeRect *rect = scheme.Place(each.place);
			if (rect == nullptr)
				continue;
			document.Move_Window(each.window, static_cast<int>(static_cast<float>(rect->left) * fx), static_cast<int>(static_cast<float>(rect->top) * fy));
			document.Resize_Window(each.window, static_cast<int>(static_cast<float>(rect->right - rect->left) * fx),
				static_cast<int>(static_cast<float>(rect->bottom - rect->top) * fy));
			if (Engine::UI::WND::WNDWindow *window = document.Find_Window(each.window))
				for (const auto &[state, key] : {std::pair{0, each.enabled}, std::pair{1, each.disabled}, std::pair{2, each.hilite}})
					if (key != nullptr)
						if (const std::string_view image = scheme.Image(key); !image.empty())
						{
							window->draw_states[state].cells[0].image_name = std::string(image);
							window->draw_states[state].cells[0].image = m_menu.Image(image);
						}
		}
		// ControlBarScheme::init: the experience bar's frame (ExpBarForeground) shows the scheme's ExpBarForegroundImage.
		if (Engine::UI::WND::WNDWindow *frame = document.Find_Window("ControlBar.wnd:ExpBarForeground"))
			if (const std::string_view image = scheme.Image("ExpBarForegroundImage"); !image.empty())
			{
				frame->draw_states[0].cells[0].image_name = std::string(image);
				frame->draw_states[0].cells[0].image = m_menu.Image(image);
			}
		// ControlBarScheme::init -> ControlBar::updateUpDownImages / setUpDownImages: the scheme does not dress the
		// minimise button itself (its MinMaxButton images are never set); the bar's stage does. At its default stage the
		// button shows ToggleButtonDownOn, pointed at ToggleButtonDownIn, pressed while pointed at ToggleButtonDownPushed
		// (GadgetButtonSetEnabledImage, SetHiliteImage, SetHiliteSelectedImage).
		if (Engine::UI::WND::WNDWindow *toggle = document.Find_Window("ControlBar.wnd:ButtonLarge"))
		{
			const auto images = scheme.MinimizeButtonImages(false); // the bar's default stage (no minimised stage yet)
			for (const auto &[state, cell, image] : {std::tuple{0, 0, images.enabled}, std::tuple{2, 0, images.hilite}, std::tuple{2, 1, images.hiliteSelected}})
				if (!image.empty() && static_cast<std::size_t>(cell) < toggle->draw_states[state].cells.size())
				{
					toggle->draw_states[state].cells[cell].image_name = std::string(image);
					toggle->draw_states[state].cells[cell].image = m_menu.Image(image);
				}
		}
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

	// The scheme's images, scaled from the resolution it was laid out at: those behind the bar's windows (layers 3..5,
	// the deepest first) or those in front (2..0).
	void DrawScheme(Graphics::Renderer2D &renderer, bool behind)
	{
		if (m_scheme == nullptr || m_scheme->width <= 0 || m_scheme->height <= 0)
			return;
		const auto fit = Engine::UI::WND::Fit_Viewport(m_scheme->width, m_scheme->height, static_cast<int>(m_width), static_cast<int>(m_height),
			Engine::UI::WND::LayoutAnchor::Center, Engine::UI::WND::LayoutAnchor::End);
		const float sx = static_cast<float>(fit.scale), sy = static_cast<float>(fit.scale);
		const float ox = static_cast<float>(fit.x), oy = static_cast<float>(fit.y);
		m_list.Clear();
		for (int layer = behind ? 5 : 2; layer >= (behind ? 3 : 0); --layer)
			for (const content::SchemeImagePart &part : m_scheme->images)
				if (part.layer == layer)
				{
					const float left = ox + static_cast<float>(part.x) * sx, top = oy + static_cast<float>(part.y) * sy;
					m_list.Add_Image(m_menu.Image(part.image), {left, top, left + static_cast<float>(part.width) * sx, top + static_cast<float>(part.height) * sy},
						{1.0f, 1.0f, 1.0f, 1.0f});
				}
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
	Graphics::Color2D m_clockColor{0.0f, 0.0f, 0.0f, 100.0f / 255.0f};
	// The view model outlives the bindings (they unsubscribe from its observables when they go).
	std::unique_ptr<hud::ControlBarViewModel> m_viewModel;
	engine::gui::mvvm::Command m_options; // declared before the bindings (they hold on to it)
	std::optional<Engine::UI::WND::WNDBindings> m_bindings;
	std::optional<hud::ControlBarView> m_view;
	Engine::UI::WND::WNDPointer m_pointer;
	content::ControlBarSchemes m_schemes;
	const content::ControlBarSchemeContent *m_scheme{nullptr};
	std::string m_side;
	std::u16string m_moneyPattern;
	Engine::UI::WND::DrawList m_list;
	Engine::UI::WND::Renderer m_renderer;
	std::uint32_t m_width{1024}, m_height{768};
	presentation::PowerBarSettings m_powerBar;
	bool m_loaded{false};
};
}
