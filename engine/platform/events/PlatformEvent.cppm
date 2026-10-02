export module engine.platform.events;
import std;
import engine.platform.core.types;

export namespace engine::platform
{
enum class EventType : std::uint8_t
{
	quit, window_resized, window_close_requested, key_down, key_up,
	text_input, text_editing, mouse_moved, mouse_button_down, mouse_button_up,
	mouse_wheel, focus_gained, focus_lost
};

enum class KeyCode : std::uint8_t
{
	unknown, enter, keypad_enter, backspace, escape, tab, up, down, left, right, del,
	a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z,
	space, digit0, digit1, digit2, digit3, digit4, digit5, digit6, digit7, digit8, digit9,
	f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
	keypad0, keypad1, keypad2, keypad3, keypad4, keypad5, keypad6, keypad7, keypad8, keypad9,
	keypad_period, keypad_multiply, keypad_minus, keypad_plus, keypad_divide,
	home, end, page_up, page_down, insert,
	minus, equals, left_bracket, right_bracket, semicolon, apostrophe, grave, backslash, comma, period, slash,
	shift, control, alt // either side's
};
enum EventModifier : std::uint32_t { modifier_alt = 1u << 0, modifier_control = 1u << 1, modifier_shift = 1u << 2, modifier_command = 1u << 3 };

struct PlatformEvent
{
	EventType type{};
	WindowId window{};
	std::int32_t code{};
	KeyCode key{KeyCode::unknown};
	std::uint32_t modifiers{};
	bool repeat{};
	bool flipped{};
	Point2D position{};
	Extent2D size{};
	float x{};
	float y{};
	std::int32_t text_start{};
	std::int32_t text_length{};
	char text[32]{};
};

class IEventService
{
public:
	virtual ~IEventService() = default;
	// Polls one event. Returns false when the queue is empty.
	virtual bool poll(PlatformEvent& event) = 0;
};
}
