module;
#include <SDL3/SDL.h>
#include <cstring>
export module engine.platform.adapters.sdl3.events;
import engine.platform;

export namespace engine::platform::sdl3
{
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
					event.key.scancode == SDL_SCANCODE_KP_ENTER ? KeyCode::keypad_enter : KeyCode::unknown;
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
