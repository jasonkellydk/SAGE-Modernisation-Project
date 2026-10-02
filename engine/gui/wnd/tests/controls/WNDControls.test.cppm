module;

#define BOOST_TEST_MODULE EngineUIWNDControlTests

#include <boost/test/included/unit_test.hpp>

export module Engine.UI.WND.Controls.Tests;
import std;

import Engine.UI.WND;
import Engine.UI.WND.Controls;
import Engine.UI.WND.Document;
import Engine.UI.WND.Tests.Support.GameData;

using namespace Engine::UI::WND;
using namespace Engine::UI::WND::Tests;

#ifndef ENGINE_UI_WND_GAME_DATA_ROOT
#define ENGINE_UI_WND_GAME_DATA_ROOT "."
#endif

namespace
{

struct ControlCase final
{
	std::string_view file;
	WindowType type;
	ControlKind kind;
};

constexpr std::array cases{
	ControlCase{"MainMenu.wnd", WindowType::User, ControlKind::User},
	ControlCase{"MainMenu.wnd", WindowType::PushButton, ControlKind::PushButton},
	ControlCase{"LanGameOptionsMenu.wnd", WindowType::CheckBox, ControlKind::CheckBox},
	ControlCase{"LanMapSelectMenu.wnd", WindowType::RadioButton, ControlKind::RadioButton},
	ControlCase{"MainMenu.wnd", WindowType::TabControl, ControlKind::TabControl},
	ControlCase{"MOTD.wnd", WindowType::ListBox, ControlKind::ListBox},
	ControlCase{"LanGameOptionsMenu.wnd", WindowType::ComboBox, ControlKind::ComboBox},
	ControlCase{"SkirmishGameOptionsMenu.wnd", WindowType::HorizontalSlider, ControlKind::HorizontalSlider},
	ControlCase{"WOLCustomLobby.wnd", WindowType::VerticalSlider, ControlKind::VerticalSlider},
	ControlCase{"DownloadMenu.wnd", WindowType::ProgressBar, ControlKind::ProgressBar},
	ControlCase{"DownloadMenu.wnd", WindowType::StaticText, ControlKind::StaticText},
	ControlCase{"ReplayControl.wnd", WindowType::TextEntry, ControlKind::TextEntry}};

const WNDWindow *Find_Window(const WNDDocument &document, WindowType type)
{
	for (const WNDWindow &window : document.Windows())
		if (window.type == type)
			return &window;
	return nullptr;
}

bool Render_Real_Control(
	const GameData &data,
	const ControlCase &control_case,
	DrawList &draw_list,
	WNDWindow &selected)
{
	std::string source;
	if (!Load_WND_Source(data.root / "Window" / "Menus" / control_case.file, source))
		return false;
	WNDDocument document;
	if (!document.Parse(source))
		return false;
	ImageCatalog catalog;
	if (!Load_Game_Image_Catalog(data, catalog))
		return false;
	WNDDocumentResolveReport report;
	// Some legacy optional cells deliberately have no mapped-image entry.  The
	// selected control is still rendered from its real cells, so optional
	// misses do not make this fixture test dependent on the complete install.
	document.Resolve_Images(catalog, report);
	const WNDWindow *window = Find_Window(document, control_case.type);
	// GeneralsMD does not ship a TABCONTROL window in the selected shell
	// layouts, but its renderer remains part of the WND contract.  Exercise it
	// with the real shell atlas/state from the root window.
	if (window == nullptr && control_case.type == WindowType::TabControl)
		window = Find_Window(document, WindowType::User);
	if (window == nullptr)
		return false;
	selected = *window;
	ControlVisual visual;
	visual.kind = control_case.kind;
	visual.rectangle = {40.0f, 40.0f, 440.0f, 140.0f};
	visual.state = &selected.draw_states[0];
	visual.thumb_state = &selected.thumb_draw_states[0];
	visual.scale = 1.0f;
	visual.minimum = selected.minimum;
	visual.maximum = selected.maximum;
	visual.position = selected.minimum + (selected.maximum - selected.minimum) / 2;
	visual.progress = 65;
	visual.list_length = selected.list_length;
	visual.list_columns = selected.list_columns;
	visual.checked = true;
	visual.image_style = selected.image_style;
	if (control_case.kind == ControlKind::ComboBox) {
		visual.secondary_state = &selected.combo_entry_draw_states[0];
		visual.thumb_state = &selected.combo_button_draw_states[0];
	}
	return Render_Control(draw_list, visual);
}

}

BOOST_AUTO_TEST_CASE(real_wnd_fixture_renders_every_control_kind)
{
	const auto data = std::make_shared<const GameData>(
		Load_Game_Data(std::filesystem::path(ENGINE_UI_WND_GAME_DATA_ROOT)));
	BOOST_REQUIRE(!data->textures.empty());
	BOOST_REQUIRE(Initialize_Game_Asset_Runtime(data));

	for (const ControlCase &control_case : cases) {
		DrawList draw_list;
		WNDWindow selected;
		BOOST_REQUIRE_MESSAGE(
			Render_Real_Control(*data, control_case, draw_list, selected),
			"unable to render " << control_case.file << " control type "
				<< static_cast<unsigned>(control_case.type));
		// This is the deterministic command snapshot for controls whose WND
		// definition is intentionally transparent (notably StaticText).
		BOOST_CHECK_MESSAGE(
			draw_list.Size() > 0 || control_case.type == WindowType::StaticText,
			"control emitted no draw commands: " << control_case.file);
		for (const DrawCommand &command : draw_list.Commands())
			BOOST_CHECK(command.rectangle.right >= command.rectangle.left);
	}
}

// W3DGadgetPushButtonImageDraw with USE_OVERLAY_STATES (ControlBar::init sets it on the command buttons): the enabled
// image whatever the state; disabled it draws grey, or dimmed to 144 with ALWAYS_COLOR, or as it is when NOT_READY.
// Without the status a disabled button draws its disabled data (here a plain colour).
BOOST_AUTO_TEST_CASE(overlay_state_buttons_draw_their_art_grey_while_disabled)
{
	std::array<WNDDrawState, 3> states{};
	states[0].cells[0].image.texture = Assets::TextureAssetHandle{7, 1};
	states[1].cells[0].color = {0.5f, 0.5f, 0.5f, 1.0f};
	const auto draw = [&](bool overlay, bool enabled, bool always_color, bool not_ready) {
		ControlVisual visual;
		visual.kind = ControlKind::PushButton;
		visual.rectangle = {0.0f, 0.0f, 60.0f, 48.0f};
		visual.states = states.data();
		visual.state = &states[enabled ? 0 : 1];
		visual.overlay_states = overlay;
		visual.enabled = enabled;
		visual.always_color = always_color;
		visual.not_ready = not_ready;
		DrawList list;
		BOOST_REQUIRE(Render_Control(list, visual));
		BOOST_REQUIRE(list.Size() >= 1u);
		return list.Commands()[0];
	};
	const DrawCommand shown = draw(true, true, false, false);
	BOOST_TEST((shown.kind == DrawCommandKind::Image));
	BOOST_TEST(shown.image.texture.Get_Index() == 7u);
	BOOST_TEST(!shown.grayscale);
	const DrawCommand grey = draw(true, false, false, false);
	BOOST_TEST((grey.kind == DrawCommandKind::Image));
	BOOST_TEST(grey.image.texture.Get_Index() == 7u);
	BOOST_TEST(grey.grayscale);
	const DrawCommand dimmed = draw(true, false, true, false);
	BOOST_TEST(!dimmed.grayscale);
	BOOST_TEST(std::abs(dimmed.color.red - 144.0f / 255.0f) < 1e-6f);
	const DrawCommand notReady = draw(true, false, false, true);
	BOOST_TEST(!notReady.grayscale);
	BOOST_TEST(notReady.color.red == 1.0f);
	const DrawCommand plain = draw(false, false, false, false);
	BOOST_TEST((plain.kind != DrawCommandKind::Image));
}

// W3DGameWinDefaultDraw (EA): a window with a video buffer (WinInstanceData::setVideoBuffer, the mission load screens'
// movie) draws its own look first, then the video stretched over the whole window; without one, only its look.
BOOST_AUTO_TEST_CASE(a_window_with_a_video_draws_it_over_its_own_look)
{
	std::array<WNDDrawState, 3> states{};
	states[0].cells[0].image.texture = Assets::TextureAssetHandle{7, 1};
	ImageRef video;
	video.texture = Assets::TextureAssetHandle{9, 1};
	ControlVisual visual;
	visual.kind = ControlKind::User;
	visual.rectangle = {0.0f, 0.0f, 800.0f, 600.0f};
	visual.states = states.data();
	visual.state = &states[0];
	visual.image_style = true;
	DrawList plain;
	BOOST_REQUIRE(Render_Control(plain, visual));
	visual.video = &video;
	DrawList withVideo;
	BOOST_REQUIRE(Render_Control(withVideo, visual));
	BOOST_REQUIRE(withVideo.Size() == plain.Size() + 1u);
	const DrawCommand last = withVideo.Commands()[withVideo.Size() - 1];
	BOOST_TEST((last.kind == DrawCommandKind::Image));
	BOOST_TEST(last.image.texture.Get_Index() == 9u);
	BOOST_TEST(last.rectangle.right == 800.0f);
	BOOST_TEST(last.rectangle.bottom == 600.0f);
}

// W3DGadgetPushButtonImageDraw: an enabled USE_OVERLAY_STATES button pointed at draws Cameo_hilited over its art,
// pressed it draws Cameo_push; disabled, neither.
BOOST_AUTO_TEST_CASE(overlay_state_buttons_show_hilite_and_push_overlays)
{
	std::array<WNDDrawState, 3> states{};
	states[0].cells[0].image.texture = Assets::TextureAssetHandle{7, 1};
	ImageRef hilite, push;
	hilite.texture = Assets::TextureAssetHandle{8, 1};
	push.texture = Assets::TextureAssetHandle{9, 1};
	const auto draw = [&](bool enabled, bool highlighted, bool pressed) {
		ControlVisual visual;
		visual.kind = ControlKind::PushButton;
		visual.rectangle = {0.0f, 0.0f, 60.0f, 48.0f};
		visual.states = states.data();
		visual.state = &states[enabled ? 0 : 1];
		visual.overlay_states = true;
		visual.enabled = enabled;
		visual.highlighted = highlighted;
		visual.checked = pressed;
		visual.highlighted_overlay = &hilite;
		visual.pushed_overlay = &push;
		DrawList list;
		BOOST_REQUIRE(Render_Control(list, visual));
		std::vector<std::uint32_t> images;
		for (const DrawCommand &command : list.Commands())
			if (command.kind == DrawCommandKind::Image)
				images.push_back(command.image.texture.Get_Index());
		return images;
	};
	BOOST_TEST((draw(true, false, false) == std::vector<std::uint32_t>{7}));
	BOOST_TEST((draw(true, true, false) == std::vector<std::uint32_t>{7, 8}));
	BOOST_TEST((draw(true, true, true) == std::vector<std::uint32_t>{7, 9}));
	BOOST_TEST((draw(false, true, false) == std::vector<std::uint32_t>{7}));
}

// GadgetButtonDrawInverseClock (the production queue's front button): the clock draws over the button's art, this far
// round, in its colour, as the part still to go.
BOOST_AUTO_TEST_CASE(push_buttons_draw_their_clock_over_their_art)
{
	std::array<WNDDrawState, 3> states{};
	states[0].cells[0].image.texture = Assets::TextureAssetHandle{7, 1};
	ControlVisual visual;
	visual.kind = ControlKind::PushButton;
	visual.rectangle = {0.0f, 0.0f, 60.0f, 48.0f};
	visual.states = states.data();
	visual.state = &states[0];
	visual.clock = true;
	visual.clock_percent = 25;
	visual.clock_remaining = true;
	visual.clock_color = {0.0f, 0.0f, 0.0f, 160.0f / 255.0f};
	DrawList list;
	BOOST_REQUIRE(Render_Control(list, visual));
	BOOST_REQUIRE(list.Size() == 2u);
	BOOST_TEST((list.Commands()[0].kind == DrawCommandKind::Image));
	const DrawCommand &clock = list.Commands()[1];
	BOOST_TEST((clock.kind == DrawCommandKind::Clock));
	BOOST_TEST(clock.percent == 25);
	BOOST_TEST(clock.remaining);
	BOOST_TEST(std::abs(clock.color.alpha - 160.0f / 255.0f) < 1e-6f);
}
