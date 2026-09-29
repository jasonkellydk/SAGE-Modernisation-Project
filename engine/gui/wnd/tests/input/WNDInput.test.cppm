module;

#define BOOST_TEST_MODULE EngineUIWNDInputTests

#include <boost/test/included/unit_test.hpp>

export module Engine.UI.WND.Input.Tests;
import std;

import Engine.UI.WND;
import Engine.UI.WND.Document;
import Engine.UI.WND.Input;
import Engine.UI.WND.Transitions;

using namespace Engine::UI::WND;

namespace
{
std::string Window(std::string_view type, std::string_view name, std::string_view status, int left, int top, int right, int bottom,
	std::string_view children = {})
{
	std::string text = "WINDOW\n  WINDOWTYPE = " + std::string(type) + ";\n  SCREENRECT = UPPERLEFT: " + std::to_string(left) + " " + std::to_string(top) +
		",\n    BOTTOMRIGHT: " + std::to_string(right) + " " + std::to_string(bottom) + ",\n    CREATIONRESOLUTION: 800 600;\n  NAME = \"" +
		std::string(name) + "\";\n  STATUS = " + std::string(status) + ";\n";
	if (!children.empty())
		text += std::string(children) + "  ENDALLCHILDREN\n";
	return text + "END\n";
}

// A horizontal slider 0..100 (the shipped OptionsMenu.wnd music slider's size).
std::string Slider(std::string_view name, int left, int top, int right, int bottom)
{
	std::string text = Window("HORZSLIDER", name, "ENABLED", left, top, right, bottom);
	return text.substr(0, text.size() - 4) + "  SLIDERDATA = MINVALUE: 0,\n    MAXVALUE: 100;\nEND\n";
}

// A combo box in 10-point text (12-pixel rows before its font resolves).
std::string Combo(std::string_view name, int left, int top, int right, int bottom)
{
	std::string text = Window("COMBOBOX", name, "ENABLED", left, top, right, bottom);
	return text.substr(0, text.size() - 4) + "  FONT = NAME: \"Arial\", SIZE: 10, BOLD: 0;\n  COMBOBOXDATA = ISEDITABLE: 0,\n    MAXDISPLAY: 5;\nEND\n";
}

// A digits-only entry of 6 (so 5 characters, as GadgetTextEntry keeps one for the end).
std::string Entry(std::string_view name, int left, int top, int right, int bottom)
{
	std::string text = Window("ENTRYFIELD", name, "ENABLED", left, top, right, bottom);
	return text.substr(0, text.size() - 4) + "  TEXTENTRYDATA = MAXLEN: 6,\n    NUMERICALONLY: 1;\nEND\n";
}

// A list box of 10-point rows (11 pixels a row before its font resolves) that forces a selection.
std::string List(std::string_view name, int left, int top, int right, int bottom)
{
	std::string text = Window("SCROLLLISTBOX", name, "ENABLED", left, top, right, bottom);
	return text.substr(0, text.size() - 4) + "  FONT = NAME: \"Arial\", SIZE: 10, BOLD: 0;\n  LISTBOXDATA = LENGTH: 100,\n    COLUMNS: 2,\n    COLUMNSWIDTH: 70,\n    COLUMNSWIDTH: 30,\n    FORCESELECT: 1;\nEND\n";
}

// A menu: a panel with two buttons, a see-through overlay taking no input
// across the first, a hidden button over the second, and a later panel
// covering a third button.
WNDDocument Menu()
{
	const std::string buttons = "  CHILD\n" + Window("PUSHBUTTON", "Menu.wnd:Play", "ENABLED", 100, 100, 300, 150) + "  CHILD\n" +
		Window("PUSHBUTTON", "Menu.wnd:Quit", "ENABLED", 100, 200, 300, 250) + "  CHILD\n" +
		Window("USER", "Menu.wnd:Glow", "ENABLED+NOINPUT", 0, 0, 800, 180) + "  CHILD\n" +
		Window("PUSHBUTTON", "Menu.wnd:Secret", "ENABLED+HIDDEN", 100, 200, 300, 250) + "  CHILD\n" +
		Window("PUSHBUTTON", "Menu.wnd:Covered", "ENABLED", 500, 100, 700, 150) + "  CHILD\n" +
		Window("PUSHBUTTON", "Menu.wnd:Off", "HIDDEN", 500, 300, 700, 350) + "  CHILD\n" +
		Window("PUSHBUTTON", "Menu.wnd:Dim", "", 500, 400, 700, 450) + "  CHILD\n" +
		Window("CHECKBOX", "Menu.wnd:Shadows", "ENABLED", 100, 300, 130, 330) + "  CHILD\n" +
		Slider("Menu.wnd:Volume", 164, 400, 372, 424) + "  CHILD\n" +
		Combo("Menu.wnd:Resolution", 500, 500, 700, 520) + "  CHILD\n" +
		Entry("Menu.wnd:Port", 100, 500, 300, 520);
	const std::string lists = Window("USER", "Menu.wnd:ListParent", "ENABLED+NOINPUT", 0, 0, 800, 600, "  CHILD\n" + List("Menu.wnd:Replays", 400, 300, 700, 360));
	const std::string text = Window("USER", "Menu.wnd:Parent", "ENABLED", 0, 0, 800, 600, buttons) +
		Window("USER", "Menu.wnd:Cover", "ENABLED", 450, 50, 750, 180) + lists;
	WNDDocument document;
	BOOST_REQUIRE(document.Parse(text));
	return document;
}

std::string NameAt(const WNDDocument &document, float x, float y)
{
	const auto hit = Hit_Test(document, x, y);
	return hit ? document.Windows()[*hit].name : std::string{};
}
}

BOOST_AUTO_TEST_SUITE(wnd_input)

BOOST_AUTO_TEST_CASE(the_topmost_window_taking_input_is_hit)
{
	const WNDDocument document = Menu();
	BOOST_TEST(NameAt(document, 150, 120) == "Menu.wnd:Play");     // through the NOINPUT glow
	BOOST_TEST(NameAt(document, 150, 220) == "Menu.wnd:Quit");     // the hidden button over it is skipped
	BOOST_TEST(NameAt(document, 600, 120) == "Menu.wnd:Cover");    // a later panel is on top
	BOOST_TEST(NameAt(document, 50, 400) == "Menu.wnd:Parent");
	BOOST_TEST(NameAt(document, 900, 900).empty());
	BOOST_TEST(!Gadget_At(document, 600, 120).has_value());        // the covered button cannot be reached
	BOOST_TEST(!Gadget_At(document, 600, 425).has_value());        // not ENABLED
	BOOST_TEST(!Gadget_At(document, 50, 400).has_value());         // a panel is no gadget
}

BOOST_AUTO_TEST_CASE(buttons_light_under_the_pointer_and_click_on_release_over_the_one_pressed)
{
	WNDDocument document = Menu();
	WNDPointer pointer;
	const auto state = [&](std::string_view name) { return document.Find_Window(name)->visual_state; };
	pointer.Move(document, 150, 120);
	BOOST_TEST(pointer.TakeLook());
	BOOST_TEST((state("Menu.wnd:Play") == VisualState::Highlighted));
	pointer.Move(document, 160, 125); // still over it: nothing changes
	BOOST_TEST(!pointer.TakeLook());
	pointer.Move(document, 150, 220);
	BOOST_TEST((state("Menu.wnd:Play") == VisualState::Normal));
	BOOST_TEST((state("Menu.wnd:Quit") == VisualState::Highlighted));
	// Pressed on Quit (its pushed look), released on Play: no click; pressed and released on Play: a click.
	const auto pressed = pointer.Press(document, 150, 220);
	BOOST_TEST_REQUIRE(pressed.has_value());
	BOOST_TEST((pressed->kind == WNDInputEvent::Kind::Pressed));
	BOOST_TEST(pressed->window == "Menu.wnd:Quit");
	BOOST_TEST((state("Menu.wnd:Quit") == VisualState::Selected));
	BOOST_TEST(!pointer.Release(document, 150, 120).has_value());
	BOOST_TEST(!pointer.Press(document, 50, 400).has_value()); // on no gadget: no click sound either
	BOOST_TEST(!pointer.Release(document, 50, 400).has_value());
	pointer.Press(document, 150, 120);
	const auto clicked = pointer.Release(document, 160, 130);
	BOOST_TEST_REQUIRE(clicked.has_value());
	BOOST_TEST((clicked->kind == WNDInputEvent::Kind::Clicked));
	BOOST_TEST(clicked->window == "Menu.wnd:Play");
	BOOST_TEST((state("Menu.wnd:Play") == VisualState::Highlighted));
	// Leaving while held unselects it: coming back and releasing does not click (GWM_MOUSE_LEAVING).
	pointer.Press(document, 150, 120);
	pointer.Move(document, 150, 220);
	pointer.Move(document, 150, 120);
	BOOST_TEST(!pointer.Release(document, 150, 120).has_value());
	// Released off every gadget: nothing, and nothing stays lit.
	pointer.Press(document, 150, 120);
	BOOST_TEST(!pointer.Release(document, 50, 400).has_value());
	BOOST_TEST((state("Menu.wnd:Play") == VisualState::Normal));
}

// Legacy parity (GadgetCheckBox check-like press; GadgetHorizontalSlider
// GGM_LEFT_DRAG / GWM_LEFT_UP with its 13-pixel thumb): a 208-pixel slider
// over 0..100 moves 1.95 pixels a step.
BOOST_AUTO_TEST_CASE(legacy_parity_check_boxes_flip_on_press_and_sliders_follow_the_thumb)
{
	WNDDocument document = Menu();
	WNDPointer pointer;
	const auto toggled = pointer.Press(document, 110, 310);
	BOOST_TEST_REQUIRE(toggled.has_value());
	BOOST_TEST((toggled->kind == WNDInputEvent::Kind::Toggled));
	BOOST_TEST(toggled->value == 1);
	BOOST_TEST(!pointer.Release(document, 110, 310).has_value()); // nothing more on release
	BOOST_TEST(pointer.Press(document, 110, 310)->value == 0);
	pointer.Release(document, 110, 310);

	WNDWindow &slider = *document.Find_Window("Menu.wnd:Volume");
	BOOST_TEST(slider.maximum == 100);
	slider.position = 0; // the thumb's centre at 164 + 6.5
	// A click on the track at 350: a fifth of the width (41.6) toward it, to centre 212.1: step 21.
	pointer.Press(document, 350, 410);
	const auto jumped = pointer.Release(document, 350, 410);
	BOOST_TEST_REQUIRE(jumped.has_value());
	BOOST_TEST((jumped->kind == WNDInputEvent::Kind::SliderMoved));
	BOOST_TEST(jumped->value == 21);
	// Grab the thumb (centre 164 + 21 * 1.95 + 6.5 = 211.45) and drag it to 270: (270 - 170.5) / 1.95 = 51.
	pointer.Press(document, 211, 410);
	const auto dragged = pointer.Move(document, 270, 410);
	BOOST_TEST_REQUIRE(dragged.has_value());
	BOOST_TEST(dragged->value == 51);
	BOOST_TEST(pointer.Move(document, 900, 900)->value == 100); // past the end: the maximum
	BOOST_TEST(!pointer.Release(document, 900, 900).has_value());
	BOOST_TEST(slider.position == 100);
}

// Legacy parity (GadgetComboBox GWM_LEFT_UP / HideListBox, winSetLoneWindow): a
// release opens the list (rows the font's height plus 2, 4 more in all), which
// alone takes the pointer until a row is picked or it is dismissed.
BOOST_AUTO_TEST_CASE(legacy_parity_a_combo_box_lists_picks_and_closes)
{
	WNDDocument document = Menu();
	WNDWindow &combo = *document.Find_Window("Menu.wnd:Resolution");
	combo.entries = {u"800 x 600", u"1024 x 768", u"1280 x 1024"};
	combo.selected = 0;
	WNDPointer pointer;
	BOOST_TEST(!pointer.Press(document, 600, 510).has_value());
	const auto opened = pointer.Release(document, 600, 510);
	BOOST_TEST_REQUIRE(opened.has_value());
	BOOST_TEST((opened->kind == WNDInputEvent::Kind::Pressed)); // its click
	BOOST_TEST(combo.list_open);
	BOOST_TEST(combo.screen_region.bottom == 520 + 3 * 12 + 4);
	// Over the list, the row under the pointer lights: row 2 spans 520 + 2 + 24 .. + 36.
	pointer.Move(document, 600, 548);
	BOOST_TEST(combo.hovered_entry == 2);
	// While open, the rest of the layout does not take the pointer: a press elsewhere only closes it.
	BOOST_TEST(!pointer.Press(document, 150, 120).has_value());
	BOOST_TEST(!combo.list_open);
	BOOST_TEST(combo.screen_region.bottom == 520);
	pointer.Release(document, 150, 120);
	// Open again and pick the second row.
	pointer.Press(document, 600, 510);
	pointer.Release(document, 600, 510);
	const auto picked = pointer.Press(document, 600, 538);
	BOOST_TEST_REQUIRE(picked.has_value());
	BOOST_TEST((picked->kind == WNDInputEvent::Kind::ComboSelected));
	BOOST_TEST(picked->value == 1);
	BOOST_TEST(combo.selected == 1);
	BOOST_TEST(!combo.list_open);
}

// Legacy parity (GadgetTextEntry GWM_LEFT_DOWN focus, GWM_IME_CHAR rules and
// length, KEY_BACKSPACE, KEY_ENTER -> GEM_EDIT_DONE).
BOOST_AUTO_TEST_CASE(legacy_parity_a_text_entry_takes_typing_by_its_rules)
{
	WNDDocument document = Menu();
	WNDPointer pointer;
	BOOST_TEST(!pointer.Type(document, u"12").has_value()); // nothing has the keyboard
	pointer.Press(document, 150, 510);
	pointer.Release(document, 150, 510);
	WNDWindow &entry = *document.Find_Window("Menu.wnd:Port");
	BOOST_TEST(entry.focused);
	const auto typed = pointer.Type(document, u"16a0019");
	BOOST_TEST_REQUIRE(typed.has_value());
	BOOST_TEST((typed->kind == WNDInputEvent::Kind::TextChanged));
	BOOST_TEST((entry.text == u"16001")); // the letter refused, stopped at MAXLEN - 1
	BOOST_TEST(!pointer.Type(document, u"9").has_value()); // full
	BOOST_TEST((pointer.Key(document, WNDPointer::EditKey::Backspace)->text == u"1600"));
	const auto done = pointer.Key(document, WNDPointer::EditKey::Enter);
	BOOST_TEST_REQUIRE(done.has_value());
	BOOST_TEST((done->kind == WNDInputEvent::Kind::EditDone));
}

// Legacy parity (GadgetListBox GWM_LEFT_UP row, FORCESELECT and double click;
// GWM_WHEEL_UP/DOWN a row): rows are the font's height and a pixel apart.
BOOST_AUTO_TEST_CASE(legacy_parity_a_list_box_selects_double_clicks_and_scrolls)
{
	WNDDocument document = Menu();
	WNDWindow &list = *document.Find_Window("Menu.wnd:Replays");
	BOOST_TEST(list.force_select);
	BOOST_TEST_REQUIRE(list.column_widths.size() == 2u);
	BOOST_TEST(list.column_widths[0] == 70);
	for (int index = 0; index < 8; ++index)
		list.entries.push_back(u"Replay " + std::u16string(1, char16_t(u'0' + index)) + u"\t1.04");
	BOOST_TEST(List_Visible_Rows(list) == 5); // 60 pixels of 11-pixel rows
	WNDPointer pointer;
	pointer.Press(document, 500, 325);
	const auto picked = pointer.Release(document, 500, 325, 1000); // 25 / 11: row 2
	BOOST_TEST_REQUIRE(picked.has_value());
	BOOST_TEST((picked->kind == WNDInputEvent::Kind::ListSelected));
	BOOST_TEST(picked->value == 2);
	pointer.Press(document, 500, 325);
	const auto opened = pointer.Release(document, 500, 325, 1300); // the same row within 500 ms
	BOOST_TEST_REQUIRE(opened.has_value());
	BOOST_TEST((opened->kind == WNDInputEvent::Kind::ListDoubleClicked));
	BOOST_TEST(opened->value == 2);
	pointer.Press(document, 500, 325);
	const auto again = pointer.Release(document, 500, 325, 5000); // the selected row again, slowly: kept (FORCESELECT)
	BOOST_TEST(again->value == 2);
	// The wheel scrolls a row at a time, no further than the last page.
	BOOST_TEST(pointer.Wheel(document, 500, 325, 1));
	BOOST_TEST(list.list_top == 1);
	BOOST_TEST(pointer.Wheel(document, 500, 325, 10));
	BOOST_TEST(list.list_top == 3);
	BOOST_TEST(!pointer.Wheel(document, 500, 325, 1));
	pointer.Press(document, 500, 305);
	BOOST_TEST(pointer.Release(document, 500, 305, 9000)->value == 3); // the first shown row is now 3
}

BOOST_AUTO_TEST_SUITE_END()

// Legacy parity (GadgetListboxCreateScrollbar, GadgetListBoxSystem's GBM_SELECTED and GSM_SLIDER_TRACK):
// a list with a scroll bar gets its buttons at the top and bottom of its right edge; they move a row,
// and the thumb dragged down the slider scrolls to the last rows.
BOOST_AUTO_TEST_CASE(legacy_parity_a_list_scrolls_by_its_buttons_and_thumb)
{
	std::string list = Window("SCROLLLISTBOX", "Menu.wnd:Long", "ENABLED", 100, 100, 400, 300);
	list = list.substr(0, list.size() - 4) + "  FONT = NAME: \"Arial\", SIZE: 10, BOLD: 0;\n  LISTBOXDATA = LENGTH: 100,\n    SCROLLBAR: 1,\n    COLUMNS: 1,\n    FORCESELECT: 0;\nEND\n";
	WNDDocument document;
	BOOST_REQUIRE(document.Parse(list));
	WNDWindow &window = *document.Find_Window("Menu.wnd:Long");
	for (int row = 0; row < 40; ++row)
		window.entries.push_back(u"row");
	const auto bar = List_Scroll_Bar(window, 800);
	BOOST_TEST_REQUIRE(bar.has_value());
	BOOST_TEST(bar->up.right == 398);   // 2 pixels in from the right
	BOOST_TEST(bar->up.left == 377);    // 21 wide
	BOOST_TEST(bar->up.top == 102);
	BOOST_TEST(bar->down.bottom == 298);
	BOOST_TEST(bar->track.top == 125);  // under the up button, 3 pixels apart
	BOOST_TEST(bar->thumb.top == bar->track.top);
	const int last = List_Last_Top(window);
	BOOST_TEST(last == 40 - List_Visible_Rows(window));

	WNDPointer pointer;
	const float x = static_cast<float>(bar->up.left + 5);
	pointer.Press(document, x, static_cast<float>(bar->down.top + 5));
	pointer.Release(document, x, static_cast<float>(bar->down.top + 5));
	BOOST_TEST(window.list_top == 1);
	pointer.Press(document, x, static_cast<float>(bar->up.top + 5));
	pointer.Release(document, x, static_cast<float>(bar->up.top + 5));
	BOOST_TEST(window.list_top == 0);
	pointer.Press(document, x, static_cast<float>(bar->up.top + 5)); // already at the top
	BOOST_TEST(window.list_top == 0);
	pointer.Release(document, x, static_cast<float>(bar->up.top + 5));
	// Grab the thumb and pull it past the slider's end: the last rows show.
	pointer.Press(document, x, static_cast<float>(bar->thumb.top + 3));
	pointer.Move(document, x, 1000.0f);
	pointer.Release(document, x, 1000.0f);
	BOOST_TEST(window.list_top == last);
	BOOST_TEST(window.selected == -1); // scrolling picks no row
}

// Legacy parity (GadgetComboBox with ISEDITABLE, as NetworkDirectConnect's remote address box):
// its box takes typed text in place of the pick; its button still opens the list.
BOOST_AUTO_TEST_CASE(legacy_parity_an_editable_combo_box_takes_typed_text)
{
	std::string combo = Window("COMBOBOX", "Menu.wnd:Address", "ENABLED", 100, 100, 300, 120);
	combo = combo.substr(0, combo.size() - 4) + "  FONT = NAME: \"Arial\", SIZE: 10, BOLD: 0;\n  COMBOBOXDATA = ISEDITABLE: 1,\n    MAXCHARS: 16,\n    MAXDISPLAY: 5;\nEND\n";
	WNDDocument document;
	BOOST_REQUIRE(document.Parse(combo));
	WNDWindow &box = *document.Find_Window("Menu.wnd:Address");
	BOOST_TEST(box.editable);
	box.entries = {u"10.0.0.1(Home)"};
	box.selected = 0;
	WNDPointer pointer;
	pointer.Press(document, 150.0f, 110.0f); // the box, not its button
	pointer.Release(document, 150.0f, 110.0f);
	BOOST_TEST(!box.list_open);
	const auto typed = pointer.Type(document, u"2");
	BOOST_TEST_REQUIRE(typed.has_value());
	BOOST_TEST((typed->kind == WNDInputEvent::Kind::TextChanged));
	BOOST_TEST(box.selected == -1);
	BOOST_TEST((box.text == u"10.0.0.1(Home)2")); // the pick's text, then what is typed
	pointer.Press(document, 295.0f, 110.0f);        // its button
	pointer.Release(document, 295.0f, 110.0f);
	BOOST_TEST(box.list_open);
}

BOOST_AUTO_TEST_SUITE(wnd_headless)

// Legacy parity (GameWindowManagerScript.cpp parseScreenRect): each window's
// SCREENRECT is scaled by its own CREATIONRESOLUTION, so a layout may mix
// windows authored at different resolutions (e.g. an HD MessageBox.wnd at
// 3840x2160 with a child still at 800x600). They all land in the document's.
BOOST_AUTO_TEST_CASE(legacy_parity_each_window_scales_by_its_own_creation_resolution)
{
	const std::string source =
		"WINDOW\n"
		"  WINDOWTYPE = USER;\n"
		"  SCREENRECT = UPPERLEFT: 0 0, BOTTOMRIGHT: 3840 2160, CREATIONRESOLUTION: 3840 2160;\n"
		"  NAME = \"Box.wnd:\";\n"
		"  CHILD\n"
		"  WINDOW\n"
		"    WINDOWTYPE = USER;\n"
		"    SCREENRECT = UPPERLEFT: 1380 600, BOTTOMRIGHT: 2460 1300, CREATIONRESOLUTION: 3840 2160;\n"
		"    NAME = \"Box.wnd:Parent\";\n"
		"  END\n"
		"  WINDOW\n"
		"    WINDOWTYPE = USER;\n"
		"    SCREENRECT = UPPERLEFT: 400 300, BOTTOMRIGHT: 500 350, CREATIONRESOLUTION: 800 600;\n"
		"    NAME = \"Box.wnd:Line\";\n"
		"  END\n"
		"END\n";
	WNDDocument document;
	BOOST_REQUIRE(document.Parse(source));
	BOOST_CHECK_EQUAL(document.Creation_Width(), 3840);
	BOOST_CHECK_EQUAL(document.Creation_Height(), 2160);
	const WNDWindow &parent = *document.Find_Window("Box.wnd:Parent");
	BOOST_CHECK_EQUAL(parent.authored_region.left, 1380);
	BOOST_CHECK_EQUAL(parent.authored_region.bottom, 1300);
	const WNDWindow &line = *document.Find_Window("Box.wnd:Line");
	BOOST_CHECK_EQUAL(line.authored_region.left, 1920);  // 400 of 800 is half of 3840
	BOOST_CHECK_EQUAL(line.authored_region.top, 1080);   // 300 of 600 is half of 2160
	BOOST_CHECK_EQUAL(line.authored_region.right, 2400);
	BOOST_CHECK_EQUAL(line.authored_region.bottom, 1260);
}

// A list row's columns are cut from its text while drawing; the draw list keeps
// its own copy so the text is still there when the list is rendered.
BOOST_AUTO_TEST_CASE(wnd_draw_list_keeps_text_built_while_drawing)
{
	DrawList draw_list;
	const std::uint16_t *kept = nullptr;
	{
		const std::u16string row = u"name\tmap";
		kept = draw_list.Keep_Text(std::u16string_view(row).substr(5));
	}
	BOOST_TEST_REQUIRE(kept != nullptr);
	for (int filler = 0; filler < 64; ++filler)
		draw_list.Keep_Text(u"another row's column");
	BOOST_TEST((std::u16string(reinterpret_cast<const char16_t *>(kept)) == u"map"));
	draw_list.Clear();
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(wnd_transitions)

namespace
{
// Three buttons and a static in one layout, found by the transitions by name.
struct Stage
{
	WNDDocument document;
	std::vector<std::string> sounds;
	Stage()
	{
		const std::string text = Window("USER", "T.wnd:Parent", "ENABLED", 0, 0, 800, 600,
			"  CHILD\n" + Window("PUSHBUTTON", "T.wnd:A", "ENABLED", 100, 100, 300, 150) + "  CHILD\n" +
				Window("PUSHBUTTON", "T.wnd:B", "ENABLED", 100, 200, 300, 250) + "  CHILD\n" + Window("STATICTEXT", "T.wnd:Title", "ENABLED", 100, 300, 300, 330));
		BOOST_REQUIRE(document.Parse(text));
	}
	TransitionHost Host()
	{
		TransitionHost host;
		host.find = [this](std::string_view name) { return TransitionTarget{document.Find_Window(name), 1.0f, 1.0f}; };
		host.play = [this](std::string_view sound) { sounds.emplace_back(sound); };
		return host;
	}
	bool Hidden(std::string_view name) { return Has_Flag(document.Find_Window(name)->flags, WindowFlag::TransitionHidden); }
};
}

// Legacy parity (FlashTransition): hidden for frames 1-3 under a white flash, shown from 4, done at 8;
// a FrameDelay holds a window back; the group ends when every window has.
BOOST_AUTO_TEST_CASE(legacy_parity_flash_frames_and_frame_delays)
{
	Stage stage;
	std::vector<TransitionGroupDefinition> groups{{"Menu", true, {{"T.wnd:A", TransitionStyle::Flash, 0}, {"T.wnd:B", TransitionStyle::Flash, 5}}}};
	WNDTransitions transitions(groups, stage.Host());
	transitions.SetGroup("menu"); // names without case
	BOOST_TEST(stage.Hidden("T.wnd:A")); // hidden as the group starts
	BOOST_TEST(stage.Hidden("T.wnd:B"));
	transitions.Update(1.0); // frame 1: A flashes in, its sound
	BOOST_TEST((stage.sounds == std::vector<std::string>{"GUIBoarderFadeIn"}));
	transitions.Update(3.0); // frame 4: A shows
	BOOST_TEST(!stage.Hidden("T.wnd:A"));
	BOOST_TEST(stage.Hidden("T.wnd:B"));
	BOOST_TEST(!transitions.IsFinished());
	transitions.Update(4.0); // frame 8: A done; B at its frame 3
	BOOST_TEST(stage.Hidden("T.wnd:B"));
	transitions.Update(0.5); // no whole frame: nothing moves
	BOOST_TEST(stage.Hidden("T.wnd:B"));
	transitions.Update(0.5); // frame 9: B's 4, shown
	BOOST_TEST(!stage.Hidden("T.wnd:B"));
	transitions.Update(4.0); // frame 13: B's 8
	BOOST_TEST(transitions.IsFinished());
	transitions.Update(1.0); // it plays once: dropped
	BOOST_TEST((!transitions.Active() || transitions.IsFinished()));
}

// Legacy parity (GameWindowTransitionsHandler::setGroup/reverse/remove): a new group waits (its windows
// hidden now) while the one playing goes back; reverse plays a group out from fully shown; remove ends it.
BOOST_AUTO_TEST_CASE(legacy_parity_groups_wait_reverse_and_remove_as_the_originals)
{
	Stage stage;
	std::vector<TransitionGroupDefinition> groups{
		{"First", false, {{"T.wnd:A", TransitionStyle::Flash, 0}, {"T.wnd:A", TransitionStyle::ReverseSound, 0}}},
		{"Second", false, {{"T.wnd:B", TransitionStyle::ButtonFlash, 0}}}};
	WNDTransitions transitions(groups, stage.Host());
	transitions.SetGroup("First");
	transitions.Update(8.0);
	BOOST_TEST(transitions.IsFinished()); // done, and kept (not played once)
	BOOST_TEST(!stage.Hidden("T.wnd:A"));
	// Another group: the first goes back (its fade sound plays), the second waits with B hidden.
	transitions.SetGroup("Second");
	BOOST_TEST(stage.Hidden("T.wnd:B"));
	stage.sounds.clear();
	transitions.Update(8.0);
	BOOST_TEST((std::find(stage.sounds.begin(), stage.sounds.end(), "GUITransitionFade") != stage.sounds.end()));
	BOOST_TEST(stage.Hidden("T.wnd:A")); // played back out
	transitions.Update(1.0);  // the second takes over
	transitions.Update(17.0); // its button flash: hidden through frame 10, shown from 11, done at 17
	BOOST_TEST(!stage.Hidden("T.wnd:B"));
	BOOST_TEST(transitions.IsFinished());
	// reverse on another group: from fully shown back out; remove ends it at once.
	transitions.Reverse("First");
	BOOST_TEST(!stage.Hidden("T.wnd:A")); // skipped to shown first
	transitions.Remove("First");
	BOOST_TEST(transitions.IsFinished());
}

// Legacy parity (FullFadeTransition, TypeTextTransition, TextOnFrameTransition).
BOOST_AUTO_TEST_CASE(legacy_parity_full_fade_type_text_and_text_on_frame)
{
	Stage stage;
	stage.document.Find_Window("T.wnd:Title")->text = u"Skirmish";
	std::vector<TransitionGroupDefinition> groups{{"Screen", true,
		{{"T.wnd:A", TransitionStyle::FullFade, 0}, {"T.wnd:Title", TransitionStyle::TypeText, 0}, {"T.wnd:B", TransitionStyle::TextOnFrame, 2}}}};
	WNDTransitions transitions(groups, stage.Host());
	transitions.SetGroup("Screen");
	BOOST_TEST(stage.Hidden("T.wnd:A"));
	BOOST_TEST(stage.Hidden("T.wnd:Title"));
	BOOST_TEST(stage.Hidden("T.wnd:B"));
	transitions.Update(3.0);
	BOOST_TEST(stage.Hidden("T.wnd:A")); // the box still fades in
	BOOST_TEST(!stage.Hidden("T.wnd:B")); // shown on its first frame (delay 2)
	transitions.Update(2.0);              // frame 5: A shows under the box
	BOOST_TEST(!stage.Hidden("T.wnd:A"));
	BOOST_TEST(stage.Hidden("T.wnd:Title")); // still typing (8 characters)
	transitions.Update(3.0);                 // frame 8: typed
	BOOST_TEST(!stage.Hidden("T.wnd:Title"));
	BOOST_TEST(std::count(stage.sounds.begin(), stage.sounds.end(), "GUITypeText") == 7);
}

BOOST_AUTO_TEST_SUITE_END()
