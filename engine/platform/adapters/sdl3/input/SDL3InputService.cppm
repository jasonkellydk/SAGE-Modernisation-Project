module;
#include <SDL3/SDL.h>
#include <array>
export module engine.platform.adapters.sdl3.input;
import engine.platform;

export namespace engine::platform::sdl3
{
class SDL3InputService final : public IInputService
{
public:
	~SDL3InputService() override { for (auto* cursor : m_cursors) if (cursor) SDL_DestroyCursor(cursor); }
	[[nodiscard]] KeyboardState keyboard_state() const noexcept override
	{
		int count = 0; const bool* keys = SDL_GetKeyboardState(&count);
		return {keys, count, (SDL_GetModState() & SDL_KMOD_CAPS) != 0};
	}
	[[nodiscard]] MouseState mouse_state() const noexcept override
	{
		MouseState result{}; result.buttons = SDL_GetMouseState(&result.position.x, &result.position.y); return result;
	}
	bool set_relative_mouse_mode(bool enabled) override
	{
		auto* focus = SDL_GetMouseFocus(); return focus && SDL_SetWindowRelativeMouseMode(focus, enabled);
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
