module;
#include <SDL3/SDL.h>
export module engine.platform.adapters.sdl3.input;
import std;
import engine.platform;
import engine.platform.adapters.sdl3.keys;

export namespace engine::platform::sdl3
{
class SDL3InputService final : public IInputService
{
public:
	~SDL3InputService() override { for (auto* cursor : m_cursors) if (cursor) SDL_DestroyCursor(cursor); }
	[[nodiscard]] KeyboardState keyboard_state() const noexcept override
	{
		int count = 0; const bool* keys = SDL_GetKeyboardState(&count);
		static constexpr auto indices=[] {
			std::array<int,native_scan_codes.size()> values{};
			for(unsigned i=0;i<values.size();++i) values[i]=static_cast<int>(native_scan_codes[i]);
			return values;
		}();
		return {keys, count, (SDL_GetModState() & SDL_KMOD_CAPS) != 0, indices};
	}
	[[nodiscard]] MouseState mouse_state() const noexcept override
	{
		MouseState result{}; result.buttons = SDL_GetMouseState(&result.position.x, &result.position.y); return result;
	}
	bool set_relative_mouse_mode(bool enabled) override
	{
		auto* focus = SDL_GetMouseFocus();if(!focus) focus=SDL_GetKeyboardFocus();
		return focus && set_relative_mouse_mode(SDL_GetWindowID(focus),enabled);
	}
	bool set_relative_mouse_mode(WindowId window, bool enabled) override
	{
		auto* native=SDL_GetWindowFromID(window);return native && SDL_SetWindowRelativeMouseMode(native,enabled);
	}
	[[nodiscard]] bool relative_mouse_mode(WindowId window) const noexcept override
	{
		auto* native=SDL_GetWindowFromID(window);return native && SDL_GetWindowRelativeMouseMode(native);
	}
	bool show_cursor(bool visible) override { return visible ? SDL_ShowCursor() : SDL_HideCursor(); }
	bool set_cursor_shape(CursorShape shape) override
	{
		static constexpr std::array<SDL_SystemCursor, 11> native{
			SDL_SYSTEM_CURSOR_DEFAULT, SDL_SYSTEM_CURSOR_TEXT, SDL_SYSTEM_CURSOR_CROSSHAIR,
			SDL_SYSTEM_CURSOR_POINTER, SDL_SYSTEM_CURSOR_EW_RESIZE, SDL_SYSTEM_CURSOR_NS_RESIZE,
			SDL_SYSTEM_CURSOR_NWSE_RESIZE, SDL_SYSTEM_CURSOR_NESW_RESIZE, SDL_SYSTEM_CURSOR_MOVE,
			SDL_SYSTEM_CURSOR_WAIT, SDL_SYSTEM_CURSOR_NOT_ALLOWED};
		const auto index = static_cast<std::size_t>(shape);
		if (index >= native.size()) return false;
		if (!m_cursors[index]) m_cursors[index] = SDL_CreateSystemCursor(native[index]);
		return m_cursors[index] && SDL_SetCursor(m_cursors[index]);
	}
	bool warp_mouse(WindowId window, Point2D position) override
	{
		auto* native = SDL_GetWindowFromID(window); if (!native) return false;
		SDL_WarpMouseInWindow(native, position.x, position.y); return true;
	}
	bool capture_mouse(bool enabled) override { return SDL_CaptureMouse(enabled); }
private:
	std::array<SDL_Cursor*, 11> m_cursors{};
};
}
