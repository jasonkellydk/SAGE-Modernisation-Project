module;
#include <SDL3/SDL.h>
export module engine.platform.adapters.sdl3.events;
import std;
import engine.platform;

export namespace engine::platform::sdl3
{
// The keys past the letters (digits, function keys, the keypad, navigation, punctuation and the modifiers).
inline KeyCode OtherKey(SDL_Scancode code) noexcept
{
	if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9)
		return static_cast<KeyCode>(static_cast<int>(KeyCode::digit1) + (code - SDL_SCANCODE_1));
	if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12)
		return static_cast<KeyCode>(static_cast<int>(KeyCode::f1) + (code - SDL_SCANCODE_F1));
	if (code >= SDL_SCANCODE_KP_1 && code <= SDL_SCANCODE_KP_9)
		return static_cast<KeyCode>(static_cast<int>(KeyCode::keypad1) + (code - SDL_SCANCODE_KP_1));
	switch (code)
	{
	case SDL_SCANCODE_0: return KeyCode::digit0;
	case SDL_SCANCODE_SPACE: return KeyCode::space;
	case SDL_SCANCODE_KP_0: return KeyCode::keypad0;
	case SDL_SCANCODE_KP_PERIOD: return KeyCode::keypad_period;
	case SDL_SCANCODE_KP_MULTIPLY: return KeyCode::keypad_multiply;
	case SDL_SCANCODE_KP_MINUS: return KeyCode::keypad_minus;
	case SDL_SCANCODE_KP_PLUS: return KeyCode::keypad_plus;
	case SDL_SCANCODE_KP_DIVIDE: return KeyCode::keypad_divide;
	case SDL_SCANCODE_HOME: return KeyCode::home;
	case SDL_SCANCODE_END: return KeyCode::end;
	case SDL_SCANCODE_PAGEUP: return KeyCode::page_up;
	case SDL_SCANCODE_PAGEDOWN: return KeyCode::page_down;
	case SDL_SCANCODE_INSERT: return KeyCode::insert;
	case SDL_SCANCODE_MINUS: return KeyCode::minus;
	case SDL_SCANCODE_EQUALS: return KeyCode::equals;
	case SDL_SCANCODE_LEFTBRACKET: return KeyCode::left_bracket;
	case SDL_SCANCODE_RIGHTBRACKET: return KeyCode::right_bracket;
	case SDL_SCANCODE_SEMICOLON: return KeyCode::semicolon;
	case SDL_SCANCODE_APOSTROPHE: return KeyCode::apostrophe;
	case SDL_SCANCODE_GRAVE: return KeyCode::grave;
	case SDL_SCANCODE_BACKSLASH: return KeyCode::backslash;
	case SDL_SCANCODE_COMMA: return KeyCode::comma;
	case SDL_SCANCODE_PERIOD: return KeyCode::period;
	case SDL_SCANCODE_SLASH: return KeyCode::slash;
	case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return KeyCode::shift;
	case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return KeyCode::control;
	case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return KeyCode::alt;
	default: return KeyCode::unknown;
	}
}

class SDL3EventService final : public IEventService
{
public:
	bool poll(PlatformEvent& out) override
	{
		SDL_Event event{};
		while (SDL_PollEvent(&event)) {
			out = {};
			switch (event.type) {
			case SDL_EVENT_QUIT: out.type = EventType::quit; break;
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED: out.type = EventType::window_close_requested; out.window = event.window.windowID; break;
			case SDL_EVENT_WINDOW_RESIZED: case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			case SDL_EVENT_WINDOW_DISPLAY_CHANGED: case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
			case SDL_EVENT_WINDOW_ENTER_FULLSCREEN: case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: {
				out.type = EventType::window_resized; out.window = event.window.windowID;
				if (auto* window = SDL_GetWindowFromID(out.window)) SDL_GetWindowSizeInPixels(window, &out.size.width, &out.size.height);
				break;
			}
			case SDL_EVENT_WINDOW_FOCUS_GAINED: out.type = EventType::focus_gained; out.window = event.window.windowID; break;
			case SDL_EVENT_WINDOW_FOCUS_LOST: out.type = EventType::focus_lost; out.window = event.window.windowID; break;
			case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:
				out.type = event.type == SDL_EVENT_KEY_DOWN ? EventType::key_down : EventType::key_up;
				out.window = event.key.windowID;
				out.key = event.key.scancode == SDL_SCANCODE_RETURN ? KeyCode::enter :
					event.key.scancode == SDL_SCANCODE_KP_ENTER ? KeyCode::keypad_enter :
					event.key.scancode == SDL_SCANCODE_BACKSPACE ? KeyCode::backspace :
					event.key.scancode == SDL_SCANCODE_ESCAPE ? KeyCode::escape :
					event.key.scancode == SDL_SCANCODE_TAB ? KeyCode::tab :
					event.key.scancode == SDL_SCANCODE_UP ? KeyCode::up :
					event.key.scancode == SDL_SCANCODE_DOWN ? KeyCode::down :
					event.key.scancode == SDL_SCANCODE_LEFT ? KeyCode::left :
					event.key.scancode == SDL_SCANCODE_RIGHT ? KeyCode::right :
					event.key.scancode == SDL_SCANCODE_DELETE ? KeyCode::del :
					event.key.scancode >= SDL_SCANCODE_A && event.key.scancode <= SDL_SCANCODE_Z
						? static_cast<KeyCode>(static_cast<int>(KeyCode::a) + (event.key.scancode - SDL_SCANCODE_A)) : OtherKey(event.key.scancode);
				out.modifiers = ((event.key.mod & SDL_KMOD_ALT) ? modifier_alt : 0u) |
					((event.key.mod & SDL_KMOD_CTRL) ? modifier_control : 0u) |
					((event.key.mod & SDL_KMOD_SHIFT) ? modifier_shift : 0u) |
					((event.key.mod & SDL_KMOD_GUI) ? modifier_command : 0u);
				out.repeat = event.key.repeat;
				break;
			case SDL_EVENT_TEXT_INPUT: out.type = EventType::text_input; out.window = event.text.windowID; SDL_strlcpy(out.text, event.text.text, sizeof(out.text)); break;
			case SDL_EVENT_TEXT_EDITING: out.type = EventType::text_editing; out.window = event.edit.windowID; out.text_start = event.edit.start; out.text_length = event.edit.length; SDL_strlcpy(out.text, event.edit.text, sizeof(out.text)); break;
			case SDL_EVENT_MOUSE_MOTION: out.type = EventType::mouse_moved; out.window = event.motion.windowID; out.position = {event.motion.x, event.motion.y}; break;
			case SDL_EVENT_MOUSE_BUTTON_DOWN: out.type = EventType::mouse_button_down; out.window = event.button.windowID; out.code = event.button.button; out.position = {event.button.x, event.button.y}; break;
			case SDL_EVENT_MOUSE_BUTTON_UP: out.type = EventType::mouse_button_up; out.window = event.button.windowID; out.code = event.button.button; out.position = {event.button.x, event.button.y}; break;
			case SDL_EVENT_MOUSE_WHEEL: out.type = EventType::mouse_wheel; out.window = event.wheel.windowID; out.x = event.wheel.x; out.y = event.wheel.y; out.flipped = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED; break;
			default: continue;
			}
			return true;
		}
		return false;
	}
};
}
