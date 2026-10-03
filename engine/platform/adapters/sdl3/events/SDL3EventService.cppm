module;
#include <SDL3/SDL.h>
export module engine.platform.adapters.sdl3.events;
import std;
import engine.platform;
import engine.platform.adapters.sdl3.keys;

namespace {
// SDL copies the event structure but retains a text pointer. Keep synthetic
// strings stable until polling consumes them; native strings remain SDL-owned.
// The SDL queue is process-global, so any adapter instance can consume a post.
struct PostedText {std::mutex mutex;std::list<std::string> values;};
PostedText& PostedTexts() {static PostedText registry;return registry;}
const char* HoldPostedText(std::string value) {
    auto& registry=PostedTexts();std::lock_guard lock(registry.mutex);
    if(registry.values.size()>=65535) return nullptr;
    return registry.values.emplace_back(std::move(value)).c_str();
}
void ForgetPostedText(const char* pointer) {
    auto& registry=PostedTexts();std::lock_guard lock(registry.mutex);
    const auto found=std::ranges::find_if(registry.values,[&](const auto& value){return value.c_str()==pointer;});
    if(found!=registry.values.end()) registry.values.erase(found);
}
}

export namespace engine::platform::sdl3
{
class SDL3EventService final : public IEventService
{
public:
	bool post(const PlatformEvent& input) override
	{
        SDL_Event event{};
        const char* posted_text{};
		if (input.type != EventType::quit && SDL_GetWindowFromID(input.window) == nullptr) return false;
		switch (input.type) {
		case EventType::quit: event.type = SDL_EVENT_QUIT; break;
		case EventType::focus_gained: case EventType::focus_lost:
			event.type = input.type == EventType::focus_gained ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
			event.window.windowID = input.window; break;
		case EventType::key_down: case EventType::key_up: {
			event.type = input.type == EventType::key_down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
			event.key.windowID = input.window; event.key.down = input.type == EventType::key_down; event.key.repeat = input.repeat;
            event.key.scancode=NativeScanCode(input.key);
            if(event.key.scancode==SDL_SCANCODE_UNKNOWN) return false;
			event.key.mod = static_cast<SDL_Keymod>(((input.modifiers & modifier_alt) ? SDL_KMOD_ALT : 0) |
				((input.modifiers & modifier_control) ? SDL_KMOD_CTRL : 0) |
				((input.modifiers & modifier_shift) ? SDL_KMOD_SHIFT : 0) |
				((input.modifiers & modifier_command) ? SDL_KMOD_GUI : 0));
			break;
		}
        case EventType::text_input:case EventType::text_editing: {
            const auto length=SDL_strnlen(input.text,sizeof(input.text));if(length==sizeof(input.text)) return false;
            posted_text=HoldPostedText(std::string(input.text,length));if(!posted_text) return false;
            if(input.type==EventType::text_input) {
                event.type=SDL_EVENT_TEXT_INPUT;event.text.windowID=input.window;event.text.text=posted_text;
            } else {
                event.type=SDL_EVENT_TEXT_EDITING;event.edit.windowID=input.window;event.edit.text=posted_text;
                event.edit.start=input.text_start;event.edit.length=input.text_length;
            }
            break;
        }
        case EventType::mouse_moved:
			if (!std::isfinite(input.position.x) || !std::isfinite(input.position.y) || !std::isfinite(input.x) || !std::isfinite(input.y)) return false;
			event.type = SDL_EVENT_MOUSE_MOTION; event.motion.windowID = input.window;
			event.motion.x = input.position.x; event.motion.y = input.position.y;
			event.motion.xrel = input.x; event.motion.yrel = input.y; break;
		case EventType::mouse_wheel:
			if (!std::isfinite(input.x) || !std::isfinite(input.y)) return false;
			event.type = SDL_EVENT_MOUSE_WHEEL; event.wheel.windowID = input.window;
			event.wheel.x = input.x; event.wheel.y = input.y;
			event.wheel.direction = input.flipped ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
			break;
		case EventType::mouse_button_down: case EventType::mouse_button_up:
			if (input.code < 1 || input.code > 5) return false;
			event.type = input.type == EventType::mouse_button_down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
			event.button.windowID = input.window; event.button.button = static_cast<Uint8>(input.code);
			event.button.down = input.type == EventType::mouse_button_down;
			event.button.clicks = input.clicks;
			event.button.x = input.position.x; event.button.y = input.position.y; break;
		default: return false;
		}
        const auto posted=SDL_PushEvent(&event);if(!posted && posted_text) ForgetPostedText(posted_text);return posted;
	}
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
                out.key=PortableKey(event.key.scancode);
				out.modifiers = ((event.key.mod & SDL_KMOD_ALT) ? modifier_alt : 0u) |
					((event.key.mod & SDL_KMOD_CTRL) ? modifier_control : 0u) |
					((event.key.mod & SDL_KMOD_SHIFT) ? modifier_shift : 0u) |
					((event.key.mod & SDL_KMOD_GUI) ? modifier_command : 0u);
				out.repeat = event.key.repeat;
				break;
            case SDL_EVENT_TEXT_INPUT:
                out.type=EventType::text_input;out.window=event.text.windowID;SDL_strlcpy(out.text,event.text.text ? event.text.text : "",sizeof(out.text));ForgetPostedText(event.text.text);break;
            case SDL_EVENT_TEXT_EDITING:
                out.type=EventType::text_editing;out.window=event.edit.windowID;out.text_start=event.edit.start;out.text_length=event.edit.length;
                SDL_strlcpy(out.text,event.edit.text ? event.edit.text : "",sizeof(out.text));ForgetPostedText(event.edit.text);break;
			case SDL_EVENT_MOUSE_MOTION: out.type = EventType::mouse_moved; out.window = event.motion.windowID; out.position = {event.motion.x, event.motion.y}; out.x = event.motion.xrel; out.y = event.motion.yrel; break;
			case SDL_EVENT_MOUSE_BUTTON_DOWN: out.type = EventType::mouse_button_down; out.window = event.button.windowID; out.code = event.button.button; out.position = {event.button.x, event.button.y}; out.clicks = event.button.clicks; break;
			case SDL_EVENT_MOUSE_BUTTON_UP: out.type = EventType::mouse_button_up; out.window = event.button.windowID; out.code = event.button.button; out.position = {event.button.x, event.button.y}; out.clicks = event.button.clicks; break;
			case SDL_EVENT_MOUSE_WHEEL: out.type = EventType::mouse_wheel; out.window = event.wheel.windowID; out.x = event.wheel.x; out.y = event.wheel.y; out.flipped = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED; break;
			default: continue;
			}
			return true;
		}
		return false;
	}
};
}
