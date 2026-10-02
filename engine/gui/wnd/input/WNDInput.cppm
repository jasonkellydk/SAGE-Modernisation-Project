export module Engine.UI.WND.Input;
import std;

export import Engine.UI.WND;
export import Engine.UI.WND.Document;

// The mouse over a WND layout, as the original's GameWindowManager routes it
// to gadgets: the window under the pointer is the topmost one shown there
// (children above their parent, windows defined later above earlier
// siblings), skipping hidden windows with their children, and passing
// through windows that take no input to what lies beneath.
// Gadgets behave as the original's:
// - push buttons (GadgetPushButton) light up under the pointer, show pushed
//   while held, lose it if the pointer leaves, and click on release;
// - check boxes (check-like) flip on the press itself;
// - combo boxes (GadgetComboBox) open and close their list on release (with
//   a click) and, while open, take the pointer alone: a press on a row picks
//   it and closes the list, a press anywhere else just closes it;
// - horizontal sliders (GadgetHorizontalSlider) follow a drag with their
//   13-pixel thumb, snapping to the ends past them, and a click on the track
//   jumps a fifth of the width toward the pointer.
// Coordinates are the layout's (its creation resolution); hosts divide
// screen positions by their scale. Each call reports what happened, for
// the bindings to carry to the view model.
export namespace Engine::UI::WND
{
struct WNDInputEvent
{
	enum class Kind : std::uint8_t
	{
		Pressed,     // a gadget went down (the host plays its click)
		Clicked,     // a push button was clicked
		Toggled,     // a check box flipped (`value`: 1 checked)
		SliderMoved, // a slider moved (`value`: its position)
		ComboSelected, // a combo box's item was picked from its list (`value`: its index)
		TextChanged,   // a text entry's text changed (`text`)
		ListSelected,  // a list box's selection changed (`value`: the row, -1 none)
		ListDoubleClicked, // a list box's row was double-clicked (`value`: the row)
		EditDone,      // Enter in a text entry (GEM_EDIT_DONE)
		RightClicked,  // a RIGHT_CLICK push button was clicked with the right button (GBM_SELECTED_RIGHT)
		Hovered,       // the pointer moved onto `window` (empty: none) off `left` (GBM_MOUSE_ENTERING / LEAVING)
	};
	Kind kind{Kind::Pressed};
	std::string window;
	int value{0};
	std::u16string text;
	std::string left; // Hovered: the gadget the pointer left (empty: none)
};

inline constexpr int HorizontalSliderThumbWidth = 13; // Gadget.h HORIZONTAL_SLIDER_THUMB_WIDTH

namespace wnd_input_detail
{
inline bool Takes_Input(WindowType type) noexcept
{
	return type == WindowType::PushButton || type == WindowType::CheckBox || type == WindowType::RadioButton ||
		type == WindowType::HorizontalSlider || type == WindowType::ComboBox || type == WindowType::TextEntry || type == WindowType::ListBox;
}

inline bool Contains(const Rect &rect, float x, float y) noexcept
{
	return x >= static_cast<float>(rect.left) && x < static_cast<float>(rect.right) && y >= static_cast<float>(rect.top) &&
		y < static_cast<float>(rect.bottom);
}

// The topmost window under (x, y) among `node` and its later siblings (each below the one
// before): a window's children above it; a window taking no input lets the pointer through.
inline NodeIndex Visit(const WNDDocument &document, NodeIndex node, float x, float y)
{
	const auto windows = document.Windows();
	for (; node != Invalid_Node && node < windows.size(); node = windows[node].next_sibling)
	{
		const WNDWindow &window = windows[node];
		// GameWindow::winPointInChild: a disabled window, and all inside it, takes no input.
		if (Has_Flag(window.flags, WindowFlag::Hidden) || Has_Flag(window.flags, WindowFlag::TransitionHidden) || !Has_Flag(window.flags, WindowFlag::Enabled)
			|| !Contains(window.screen_region, x, y))
			continue;
		if (const NodeIndex child = Visit(document, window.first_child, x, y); child != Invalid_Node)
			return child;
		if (!Has_Flag(window.flags, WindowFlag::NoInput))
			return node;
	}
	return Invalid_Node;
}

// Pixels per slider step: (width - thumb) / (max - min), as GadgetHorizontalSlider sizes it.
inline float Pixels_Per_Step(const WNDWindow &slider) noexcept
{
	const int span = std::max(slider.maximum - slider.minimum, 1);
	return static_cast<float>(slider.screen_region.right - slider.screen_region.left - HorizontalSliderThumbWidth) / static_cast<float>(span);
}

// The thumb's centre for the slider's position.
inline float Thumb_Centre(const WNDWindow &slider) noexcept
{
	return static_cast<float>(slider.screen_region.left) + static_cast<float>(slider.position - slider.minimum) * Pixels_Per_Step(slider) +
		HorizontalSliderThumbWidth / 2.0f;
}

// The position the thumb takes centred at `x` (GGM_LEFT_DRAG): the ends past them, else steps from the left.
inline int Position_At(const WNDWindow &slider, float x) noexcept
{
	const float left = static_cast<float>(slider.screen_region.left), right = static_cast<float>(slider.screen_region.right);
	if (x > right - HorizontalSliderThumbWidth / 2.0f)
		return slider.maximum;
	if (x < left + HorizontalSliderThumbWidth / 2.0f)
		return slider.minimum;
	const float perStep = Pixels_Per_Step(slider);
	const int position = perStep > 0.0f ? static_cast<int>((x - left - HorizontalSliderThumbWidth / 2.0f) / perStep) + slider.minimum : slider.minimum;
	return std::clamp(position, slider.minimum, slider.maximum);
}
}

// The topmost window under (x, y) that takes input, if any.
inline std::optional<NodeIndex> Hit_Test(const WNDDocument &document, float x, float y)
{
	if (document.Size() == 0)
		return std::nullopt;
	const NodeIndex hit = wnd_input_detail::Visit(document, document.Root(), x, y);
	return hit == Invalid_Node ? std::nullopt : std::optional(hit);
}

// The gadget under (x, y): the hit window when it is an enabled gadget.
inline std::optional<NodeIndex> Gadget_At(const WNDDocument &document, float x, float y)
{
	const auto hit = Hit_Test(document, x, y);
	if (!hit)
		return std::nullopt;
	const WNDWindow &window = document.Windows()[*hit];
	if (!wnd_input_detail::Takes_Input(window.type) || !Has_Flag(window.flags, WindowFlag::Enabled) || window.visual_state == VisualState::Disabled)
		return std::nullopt;
	return hit;
}

// GameWindowManager::winProcessMouseEvent's tooltip, with no window holding the mouse (no captor, no grabbed window):
// the window whose tooltip shows at (x, y) in layout units, or none (the world under the pointer may give one).
// findWindowUnderMouse, over the top-level windows in order (ABOVE ones, then the rest, then BELOW ones; hidden ones
// skipped; the edges inside): until one is found, the deepest window under the pointer in each one passed over
// (winPointInAnyChild: hidden ones skipped, disabled ones not) that has a tooltip callback or text; the first enabled
// one holds the pointer, at its deepest enabled, shown window (winPointInChild). A window there taking no input gives
// it to its combo box (a combo box's text entry), else to none. With none found before, that window is the one (unless
// hidden). A modal window (`modal`: the first top-level one) holds the pointer alone (no search for tooltips).
// A combo box is its own gadgets here (GadgetComboBox: its drop-down button GadgetComboBox's 21 wide at its right, its
// text entry the rest, taking no input unless editable; all its gadgets with the box's tooltip text, not its callback,
// which the menus set after making them; its open list below it; a menu may give its entry or list a callback of its
// own: `hasCallback` is asked of the box itself, its entry and its list, never of its button).
// `hasCallback(window, part)`: whether the window (or its combo box's list) has a tooltip callback.
enum class WNDTooltipPart : std::uint8_t
{
	Window,      // the window itself
	ComboButton, // a combo box's drop-down button
	ComboEntry,  // a combo box's text entry
	ComboList,   // a combo box's open list
};

struct WNDTooltipTarget
{
	NodeIndex window = Invalid_Node;
	WNDTooltipPart part = WNDTooltipPart::Window;
	bool Found() const noexcept { return window != Invalid_Node; }
};

namespace wnd_tooltip_detail
{
inline bool Within(const Rect &rect, float x, float y) noexcept
{
	return x >= static_cast<float>(rect.left) && x <= static_cast<float>(rect.right) && y >= static_cast<float>(rect.top) &&
		y <= static_cast<float>(rect.bottom);
}

inline bool Hidden(const WNDWindow &window) noexcept
{
	return Has_Flag(window.flags, WindowFlag::Hidden) || Has_Flag(window.flags, WindowFlag::TransitionHidden);
}

inline constexpr int ComboButtonWidth = 21; // GadgetComboBox's buttonWidth

// A combo box's gadgets under (x, y): its open list, then its text entry, then its button (GadgetComboBox makes the
// button, the entry, then the list; each added in front of the one before).
inline std::optional<WNDTooltipPart> Combo_Part(const WNDWindow &combo, float x, float y) noexcept
{
	const Rect box = Combo_Box_Region(combo);
	if (combo.list_open && Within(Combo_List_Region(combo), x, y))
		return WNDTooltipPart::ComboList;
	if (Within({box.left, box.top, box.right - ComboButtonWidth, box.bottom}, x, y))
		return WNDTooltipPart::ComboEntry;
	if (Within({box.right - ComboButtonWidth, box.top, box.right, box.bottom}, x, y))
		return WNDTooltipPart::ComboButton;
	return std::nullopt;
}

// GameWindow::winPointInChild / winPointInAnyChild (`any`: hidden children skipped at the first level, enabled ones
// not asked for anywhere).
inline WNDTooltipTarget Point_In_Child(const WNDDocument &document, NodeIndex node, float x, float y, bool ignoreEnable)
{
	const auto windows = document.Windows();
	const WNDWindow &window = windows[node];
	if (window.type == WindowType::ComboBox)
	{
		if (const auto part = Combo_Part(window, x, y))
			return {node, *part}; // its gadgets are enabled and shown with it
		return {node, WNDTooltipPart::Window};
	}
	for (NodeIndex child = window.first_child; child != Invalid_Node && child < windows.size(); child = windows[child].next_sibling)
	{
		const WNDWindow &each = windows[child];
		if (!Within(each.type == WindowType::ComboBox ? Combo_Box_Region(each) : each.screen_region, x, y)
			&& !(each.type == WindowType::ComboBox && each.list_open && Within(Combo_List_Region(each), x, y)))
			continue;
		if (!Hidden(each) && (ignoreEnable || Has_Flag(each.flags, WindowFlag::Enabled)))
			return Point_In_Child(document, child, x, y, ignoreEnable);
	}
	return {node, WNDTooltipPart::Window};
}
}

template<class HasCallback>
inline WNDTooltipTarget Tooltip_Target(const WNDDocument &document, float x, float y, bool modal, HasCallback &&hasCallback)
{
	using namespace wnd_tooltip_detail;
	const auto windows = document.Windows();
	if (document.Size() == 0)
		return {};
	const auto hasTooltip = [&](const WNDTooltipTarget &target) {
		const WNDWindow &window = windows[target.window];
		// The box's gadgets carry its text; its button no callback, its entry and list one a menu gives them.
		const bool callback = target.part != WNDTooltipPart::ComboButton && hasCallback(target.window, target.part);
		return callback || !window.tooltip.empty();
	};
	WNDTooltipTarget tooltip;
	WNDTooltipTarget input;
	if (modal)
		input = Point_In_Child(document, document.Root(), x, y, false);
	else
	{
		const auto pass = [&](Layer layer) {
			for (NodeIndex node = document.Root(); node != Invalid_Node && node < windows.size(); node = windows[node].next_sibling)
			{
				const WNDWindow &window = windows[node];
				if (window.layer != layer || Hidden(window) || !Within(window.screen_region, x, y))
					continue;
				if (!tooltip.Found())
				{
					// winPointInAnyChild: from its children (itself when in none).
					WNDTooltipTarget child{node, WNDTooltipPart::Window};
					for (NodeIndex each = window.first_child; each != Invalid_Node && each < windows.size(); each = windows[each].next_sibling)
					{
						const WNDWindow &under = windows[each];
						const bool over = Within(under.type == WindowType::ComboBox ? Combo_Box_Region(under) : under.screen_region, x, y)
							|| (under.type == WindowType::ComboBox && under.list_open && Within(Combo_List_Region(under), x, y));
						if (over && !Hidden(under))
						{
							child = Point_In_Child(document, each, x, y, true);
							break;
						}
					}
					if (hasTooltip(child))
						tooltip = child;
				}
				if (Has_Flag(window.flags, WindowFlag::Enabled))
				{
					input = Point_In_Child(document, node, x, y, false);
					return true;
				}
			}
			return false;
		};
		if (!pass(Layer::Above) && !pass(Layer::Normal))
			pass(Layer::Below);
	}
	if (input.Found())
	{
		const WNDWindow &window = windows[input.window];
		if (input.part == WNDTooltipPart::ComboEntry && !window.editable)
			input.part = WNDTooltipPart::Window; // the entry takes no input: its combo box does
		else if (input.part == WNDTooltipPart::Window && Has_Flag(window.flags, WindowFlag::NoInput))
			input = {};
	}
	if (!tooltip.Found() && input.Found())
		tooltip = input;
	return tooltip;
}

class WNDPointer
{
public:
	// The pointer moved: lights follow it; a dragged slider follows it.
	std::optional<WNDInputEvent> Move(WNDDocument &document, float x, float y)
	{
		if (m_lone != Invalid_Node)
		{
			// An open list lights the row under the pointer.
			WNDWindow &combo = document.Mutable_Windows()[m_lone];
			const int row = Row_At(combo, x, y);
			if (row != combo.hovered_entry)
			{
				combo.hovered_entry = row;
				m_looked = true;
			}
			return std::nullopt;
		}
		if (m_dragging != Invalid_Node)
			return Slide(document, m_dragging, wnd_input_detail::Position_At(document.Windows()[m_dragging], x));
		if (m_scrolling != Invalid_Node)
		{
			Scroll(document, y);
			return std::nullopt;
		}
		const NodeIndex over = Gadget_At(document, x, y).value_or(Invalid_Node);
		if (over == m_hover)
			return std::nullopt;
		if (m_pressed != Invalid_Node && m_pressed != over)
		{
			Show(document, m_pressed, VisualState::Normal); // GWM_MOUSE_LEAVING clears the selection
			m_pressed = Invalid_Node;
		}
		Show(document, m_hover, VisualState::Normal);
		const NodeIndex left = std::exchange(m_hover, over);
		Show(document, m_hover, VisualState::Highlighted);
		m_looked = true;
		const auto name = [&](NodeIndex node) { return node != Invalid_Node && node < document.Size() ? document.Windows()[node].name : std::string{}; };
		WNDInputEvent hovered{WNDInputEvent::Kind::Hovered, name(over), 0, {}};
		hovered.left = name(left);
		return hovered;
	}

	// A mouse button went down over the layout.
	std::optional<WNDInputEvent> Press(WNDDocument &document, float x, float y)
	{
		if (m_lone != Invalid_Node)
		{
			WNDWindow &combo = document.Mutable_Windows()[m_lone];
			const int row = Row_At(combo, x, y);
			const NodeIndex lone = m_lone;
			if (wnd_input_detail::Contains(Combo_Box_Region(combo), x, y))
			{
				m_pressed = lone; // it closes on the release, as it opened
				return std::nullopt;
			}
			Close(document);
			if (row < 0)
				return std::nullopt;
			combo.selected = row;
			return WNDInputEvent{WNDInputEvent::Kind::ComboSelected, combo.name, row};
		}
		Move(document, x, y);
		if (m_hover == Invalid_Node)
			return std::nullopt;
		WNDWindow &window = document.Mutable_Windows()[m_hover];
		m_looked = true;
		switch (window.type)
		{
		case WindowType::CheckBox:
			// Check-like: flips (and reports) on the press.
			window.checked = !window.checked;
			return WNDInputEvent{WNDInputEvent::Kind::Toggled, window.name, window.checked ? 1 : 0};
		case WindowType::ComboBox:
		{
			// An editable one's box (left of its button, as wide as the box is high) takes the keyboard.
			const Rect box = Combo_Box_Region(window);
			if (window.editable && x < static_cast<float>(box.right - (box.bottom - box.top)))
			{
				Focus(document, m_hover);
				return std::nullopt;
			}
			m_pressed = m_hover; // it acts on the release (GWM_LEFT_UP)
			return std::nullopt;
		}
		case WindowType::TextEntry:
			Focus(document, m_hover); // GWM_LEFT_DOWN: winSetFocus
			return std::nullopt;
		case WindowType::ListBox:
		{
			// Its scroll bar: the buttons move a row (GBM_SELECTED), the thumb drags, the slider jumps.
			if (const auto bar = List_Scroll_Bar(window, document.Creation_Width() > 0 ? document.Creation_Width() : 800)) {
				const auto scrollTo = [&](int top) {
					const int clamped = std::clamp(top, 0, List_Last_Top(window));
					m_looked = m_looked || clamped != window.list_top;
					window.list_top = clamped;
				};
				if (wnd_input_detail::Contains(bar->up, x, y)) {
					scrollTo(window.list_top - 1);
					return std::nullopt;
				}
				if (wnd_input_detail::Contains(bar->down, x, y)) {
					scrollTo(window.list_top + 1);
					return std::nullopt;
				}
				if (wnd_input_detail::Contains(bar->track, x, y)) {
					m_scrolling = m_hover;
					m_grab = wnd_input_detail::Contains(bar->thumb, x, y) ? y - static_cast<float>(bar->thumb.top) : static_cast<float>(bar->thumb.bottom - bar->thumb.top) / 2.0f;
					Scroll(document, y);
					return std::nullopt;
				}
			}
			m_pressed = m_hover; // it selects on the release (GWM_LEFT_UP)
			return std::nullopt;
		}
		case WindowType::HorizontalSlider:
		{
			m_pressed = m_hover;
			m_pressX = x;
			// On the thumb it drags; on the track it jumps when released.
			const float centre = wnd_input_detail::Thumb_Centre(window);
			if (x >= centre - HorizontalSliderThumbWidth / 2.0f && x <= centre + HorizontalSliderThumbWidth / 2.0f)
				m_dragging = m_hover;
			return std::nullopt;
		}
		default:
			m_pressed = m_hover;
			Show(document, m_pressed, VisualState::Selected);
			return WNDInputEvent{WNDInputEvent::Kind::Pressed, window.name, 0};
		}
	}

	// The right button came up over a push button that takes it (GWM_RIGHT_UP on a RIGHT_CLICK button).
	std::optional<WNDInputEvent> RightRelease(WNDDocument &document, float x, float y)
	{
		const auto over = Gadget_At(document, x, y);
		if (!over)
			return std::nullopt;
		const WNDWindow &window = document.Windows()[*over];
		if (window.type != WindowType::PushButton || !Has_Flag(window.flags, WindowFlag::RightClick))
			return std::nullopt;
		return WNDInputEvent{WNDInputEvent::Kind::RightClicked, window.name, 0};
	}

	// A mouse button came up (`milliseconds`: the time, for double clicks).
	std::optional<WNDInputEvent> Release(WNDDocument &document, float x, float y, std::uint64_t milliseconds = 0)
	{
		if (std::exchange(m_dragging, Invalid_Node) != Invalid_Node || std::exchange(m_scrolling, Invalid_Node) != Invalid_Node)
		{
			m_pressed = Invalid_Node;
			return std::nullopt;
		}
		Move(document, x, y);
		const NodeIndex pressed = std::exchange(m_pressed, Invalid_Node);
		if (pressed == Invalid_Node || pressed != m_hover)
			return std::nullopt;
		WNDWindow &window = document.Mutable_Windows()[pressed];
		if (window.type == WindowType::ListBox)
		{
			// GWM_LEFT_UP: the row under the pointer; the same row again quickly is a double click;
			// clicking the selected row unselects it unless the list forces a selection.
			const int row = List_Row_At(window, y);
			const int old = window.selected;
			const bool quick = m_lastListClick != 0 && milliseconds >= m_lastListClick && milliseconds - m_lastListClick < DoubleClickMilliseconds;
			m_lastListClick = milliseconds == 0 ? 1 : milliseconds;
			if (quick && m_lastListRow == row && row >= 0 && m_lastList == pressed)
			{
				m_lastListClick = 0;
				return WNDInputEvent{WNDInputEvent::Kind::ListDoubleClicked, window.name, row};
			}
			m_lastListRow = row;
			m_lastList = pressed;
			int selected = row;
			if (row == old && !window.force_select)
				selected = -1;
			if (selected < 0 && window.force_select)
				selected = old;
			window.selected = selected;
			m_looked = true;
			return WNDInputEvent{WNDInputEvent::Kind::ListSelected, window.name, selected};
		}
		if (window.type == WindowType::ComboBox)
		{
			// GWM_LEFT_UP: the click, and the list shows (the combo taking the pointer alone) or hides.
			if (window.list_open)
				Close(document);
			else
			{
				Set_Combo_Open(window, true);
				m_lone = pressed;
			}
			m_looked = true;
			return WNDInputEvent{WNDInputEvent::Kind::Pressed, window.name, 0};
		}
		if (window.type == WindowType::HorizontalSlider)
		{
			// A click on the track: the thumb jumps toward the pointer, a fifth of the width at most (GWM_LEFT_UP).
			const float page = static_cast<float>(window.screen_region.right - window.screen_region.left) / 5.0f;
			const float centre = wnd_input_detail::Thumb_Centre(window);
			const float target = m_pressX >= centre ? std::min(centre + page, m_pressX) : std::max(centre - page, m_pressX);
			return Slide(document, pressed, wnd_input_detail::Position_At(window, target));
		}
		if (window.type != WindowType::PushButton && window.type != WindowType::RadioButton)
			return std::nullopt;
		Show(document, pressed, VisualState::Highlighted);
		m_looked = true;
		return WNDInputEvent{WNDInputEvent::Kind::Clicked, window.name, 0};
	}

	// Typed text (the platform's text input): each character the focused entry
	// takes by its rules, while under its length (GWM_IME_CHAR / GWM_CHAR).
	std::optional<WNDInputEvent> Type(WNDDocument &document, std::u16string_view typed)
	{
		WNDWindow *entry = Focused(document);
		if (entry == nullptr)
			return std::nullopt;
		bool changed = false;
		for (const char16_t character : typed)
		{
			const bool digit = character >= u'0' && character <= u'9';
			const bool letter = (character >= u'a' && character <= u'z') || (character >= u'A' && character <= u'Z');
			if ((entry->numerical_only && !digit) || (entry->alphanumerical_only && !digit && !letter) || (entry->ascii_only && character > 0x7F) ||
				character < 0x20)
				continue;
			if (static_cast<int>(entry->text.size()) >= entry->max_length - 1)
				break;
			if (entry->type == WindowType::ComboBox && !changed)
			{
				// Typing in an editable combo box replaces its choice with the text.
				if (entry->selected >= 0 && static_cast<std::size_t>(entry->selected) < entry->entries.size())
					entry->text = entry->entries[static_cast<std::size_t>(entry->selected)];
				entry->selected = -1;
			}
			entry->text.push_back(character);
			changed = true;
		}
		if (!changed)
			return std::nullopt;
		m_looked = true;
		return WNDInputEvent{WNDInputEvent::Kind::TextChanged, entry->name, 0, entry->text};
	}

	// An editing key for the focused entry: Backspace removes the last character, Enter finishes.
	enum class EditKey : std::uint8_t
	{
		Backspace,
		Enter,
	};
	std::optional<WNDInputEvent> Key(WNDDocument &document, EditKey key)
	{
		WNDWindow *entry = Focused(document);
		if (entry == nullptr)
			return std::nullopt;
		if (key == EditKey::Enter)
			return WNDInputEvent{WNDInputEvent::Kind::EditDone, entry->name, 0, entry->text};
		if (entry->text.empty())
			return std::nullopt;
		entry->text.pop_back();
		m_looked = true;
		return WNDInputEvent{WNDInputEvent::Kind::TextChanged, entry->name, 0, entry->text};
	}

	// The wheel over a list box scrolls it a row (GWM_WHEEL_UP / GWM_WHEEL_DOWN; `steps` below 0: up).
	bool Wheel(WNDDocument &document, float x, float y, int steps)
	{
		const auto hit = Hit_Test(document, x, y);
		if (!hit || document.Windows()[*hit].type != WindowType::ListBox)
			return false;
		WNDWindow &list = document.Mutable_Windows()[*hit];
		const int last = List_Last_Top(list);
		const int top = std::clamp(list.list_top + steps, 0, last);
		if (top == list.list_top)
			return false;
		list.list_top = top;
		m_looked = true;
		return true;
	}

	// The layout changed under the pointer (a panel shown or hidden): forget what it was over.
	void Reset(WNDDocument &document)
	{
		Close(document);
		Focus(document, Invalid_Node);
		Show(document, m_hover, VisualState::Normal);
		m_hover = m_pressed = m_dragging = m_scrolling = Invalid_Node;
		m_looked = true;
	}

	// True once after the pointer changed how the layout looks (rebuild its render list).
	bool TakeLook() noexcept { return std::exchange(m_looked, false); }

	NodeIndex Hovered() const noexcept { return m_hover; }

	// The held thumb follows the pointer: the first row shown as far down the rows as the thumb is down its slider.
	void Scroll(WNDDocument &document, float y)
	{
		WNDWindow &list = document.Mutable_Windows()[m_scrolling];
		const auto bar = List_Scroll_Bar(list, document.Creation_Width() > 0 ? document.Creation_Width() : 800);
		if (!bar)
			return;
		const float travel = static_cast<float>((bar->track.bottom - bar->track.top) - (bar->thumb.bottom - bar->thumb.top));
		const int last = List_Last_Top(list);
		const float fraction = travel > 0.0f ? std::clamp((y - m_grab - static_cast<float>(bar->track.top)) / travel, 0.0f, 1.0f) : 0.0f;
		const int top = static_cast<int>(fraction * static_cast<float>(last) + 0.5f);
		if (top != list.list_top)
		{
			list.list_top = top;
			m_looked = true;
		}
	}

private:
	WNDWindow *Focused(WNDDocument &document) noexcept
	{
		return m_focus != Invalid_Node && m_focus < document.Size() ? &document.Mutable_Windows()[m_focus] : nullptr;
	}

	void Focus(WNDDocument &document, NodeIndex node)
	{
		if (WNDWindow *old = Focused(document))
			old->focused = false;
		m_focus = node;
		if (WNDWindow *now = Focused(document))
			now->focused = true;
		m_looked = true;
	}

	// The row of the open list under (x, y), or -1.
	static int Row_At(const WNDWindow &combo, float x, float y) noexcept
	{
		const Rect list = Combo_List_Region(combo);
		if (!wnd_input_detail::Contains(list, x, y))
			return -1;
		const int row = static_cast<int>((y - static_cast<float>(list.top) - 2.0f) / static_cast<float>(Combo_Row_Height(combo))) + combo.list_top;
		return row >= 0 && row < static_cast<int>(combo.entries.size()) && row - combo.list_top < Combo_Rows(combo) ? row : -1;
	}

	void Close(WNDDocument &document)
	{
		if (m_lone == Invalid_Node || m_lone >= document.Size())
		{
			m_lone = Invalid_Node;
			return;
		}
		Set_Combo_Open(document.Mutable_Windows()[m_lone], false);
		m_lone = Invalid_Node;
		m_looked = true;
	}

	std::optional<WNDInputEvent> Slide(WNDDocument &document, NodeIndex slider, int position)
	{
		WNDWindow &window = document.Mutable_Windows()[slider];
		if (window.position == position)
			return std::nullopt;
		window.position = position;
		m_looked = true;
		return WNDInputEvent{WNDInputEvent::Kind::SliderMoved, window.name, position};
	}

	static void Show(WNDDocument &document, NodeIndex node, VisualState state)
	{
		if (node == Invalid_Node || node >= document.Size())
			return;
		WNDWindow &window = document.Mutable_Windows()[node];
		if (window.visual_state != VisualState::Disabled)
			window.visual_state = state;
	}

	NodeIndex m_hover{Invalid_Node};
	NodeIndex m_pressed{Invalid_Node};
	NodeIndex m_dragging{Invalid_Node};
	NodeIndex m_scrolling = Invalid_Node; // a list whose scroll thumb is held
	float m_grab = 0.0f;                  // where on the thumb it is held
	NodeIndex m_lone{Invalid_Node}; // an open combo box: it alone takes the pointer
	NodeIndex m_focus{Invalid_Node}; // the text entry with the keyboard
	// The last list click, for double clicks (the system's double-click time, 500 ms by default).
	static constexpr std::uint64_t DoubleClickMilliseconds = 500;
	std::uint64_t m_lastListClick{0};
	int m_lastListRow{-1};
	NodeIndex m_lastList{Invalid_Node};
	float m_pressX{0.0f};
	bool m_looked{false};
};
}
