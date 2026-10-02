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
	space, home, end, page_up, page_down,
    digit_0,digit_1,digit_2,digit_3,digit_4,digit_5,digit_6,digit_7,digit_8,digit_9,
    f1,f2,f3,f4,f5,f6,f7,f8,f9,f10,f11,f12,f13,f14,f15,f16,f17,f18,f19,f20,f21,f22,f23,f24,
    insert,minus,equals,left_bracket,right_bracket,semicolon,apostrophe,grave,backslash,comma,period,slash,
    caps_lock,num_lock,scroll_lock,print_screen,pause,
    keypad_0,keypad_1,keypad_2,keypad_3,keypad_4,keypad_5,keypad_6,keypad_7,keypad_8,keypad_9,
    keypad_minus,keypad_multiply,keypad_plus,keypad_period,keypad_divide,
    left_shift,right_shift,left_control,right_control,left_alt,right_alt,left_command,right_command,application,
    count
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
	std::uint8_t clicks{1};
};

class IEventService
{
public:
	virtual ~IEventService() = default;
	// Polls one event. Returns false when the queue is empty.
	virtual bool poll(PlatformEvent& event) = 0;
	// Enqueues a platform event for deterministic integration playback. Adapters
	// use their native event queue so hosts exercise the same poll/input path.
	// Unsupported events return false; this never changes physical device state.
	virtual bool post(const PlatformEvent&) { return false; }
};
}
