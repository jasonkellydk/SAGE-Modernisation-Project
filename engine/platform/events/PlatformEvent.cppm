module;
#include <cstdint>
export module engine.platform.events;
import engine.platform.core.types;

export namespace engine::platform
{
enum class EventType : std::uint8_t
{
	quit, window_resized, window_close_requested, key_down, key_up,
	text_input, text_editing, mouse_moved, mouse_button_down, mouse_button_up,
	mouse_wheel, focus_gained, focus_lost
};

enum class KeyCode : std::uint8_t { unknown, enter, keypad_enter };
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
