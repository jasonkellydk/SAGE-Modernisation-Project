module;
#include <SDL3/SDL.h>
#include <cstdint>
#include <optional>
#include <string>
export module engine.platform.adapters.sdl3.window.native;
import engine.platform.window.interface;
import engine.platform.core.types;
export namespace engine::platform::sdl3
{
class SDL3Window final : public IWindow
{
public:
	explicit SDL3Window(SDL_Window* window, WindowMode mode) noexcept : m_window(window), m_mode(mode) {}
	~SDL3Window() override { if (m_window) SDL_DestroyWindow(m_window); }
	SDL3Window(const SDL3Window&) = delete;
	SDL3Window& operator=(const SDL3Window&) = delete;
	[[nodiscard]] WindowId id() const noexcept override { return SDL_GetWindowID(m_window); }
	[[nodiscard]] Extent2D size() const noexcept override { Extent2D s{}; SDL_GetWindowSize(m_window, &s.width, &s.height); return s; }
	[[nodiscard]] Extent2D drawable_size() const noexcept override { Extent2D s{}; SDL_GetWindowSizeInPixels(m_window, &s.width, &s.height); return s; }
	[[nodiscard]] Point2D position() const noexcept override { int x = 0, y = 0; SDL_GetWindowPosition(m_window, &x, &y); return {static_cast<float>(x), static_cast<float>(y)}; }
	[[nodiscard]] std::optional<NativeWindowHandle> native_handle(NativeWindowSystem system) const noexcept override
	{
		const auto props = SDL_GetWindowProperties(m_window);
		if (system == NativeWindowSystem::win32) { auto* handle = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr); if (handle) return NativeWindowHandle{system, nullptr, handle}; }
		if (system == NativeWindowSystem::x11) { auto* display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr); const auto window = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0); if (display && window) return NativeWindowHandle{system, display, reinterpret_cast<void*>(static_cast<std::uintptr_t>(window))}; }
		if (system == NativeWindowSystem::wayland) { auto* display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr); auto* surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr); if (display && surface) return NativeWindowHandle{system, display, surface}; }
		if (system == NativeWindowSystem::cocoa) { auto* window = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr); if (window) return NativeWindowHandle{system, nullptr, window}; }
		return std::nullopt;
	}
	[[nodiscard]] bool is_minimized() const noexcept override { return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) != 0; }
	[[nodiscard]] bool has_focus() const noexcept override { return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_INPUT_FOCUS) != 0; }
	[[nodiscard]] WindowMode mode() const noexcept override { return m_mode; }
	bool set_size(Extent2D s) override { return SDL_SetWindowSize(m_window, s.width, s.height); }
	bool set_minimum_size(Extent2D s) override { return SDL_SetWindowMinimumSize(m_window, s.width, s.height); }
	bool set_maximum_size(Extent2D s) override { return SDL_SetWindowMaximumSize(m_window, s.width, s.height); }
	bool set_position(Point2D p) override { return SDL_SetWindowPosition(m_window, static_cast<int>(p.x), static_cast<int>(p.y)); }
	bool set_title(const std::string& title) override { return SDL_SetWindowTitle(m_window, title.c_str()); }
	bool set_mode(WindowMode mode) override { if (mode == WindowMode::fullscreen) { if (!SDL_SetWindowFullscreen(m_window, true)) return false; } else { if (!SDL_SetWindowFullscreen(m_window, false) || !SDL_SetWindowBordered(m_window, mode == WindowMode::windowed)) return false; } m_mode = mode; return true; }
	void show() override { SDL_ShowWindow(m_window); }
	void hide() override { SDL_HideWindow(m_window); }
	bool raise() override { return SDL_RaiseWindow(m_window); }
	void minimize() override { SDL_MinimizeWindow(m_window); }
	void maximize() override { SDL_MaximizeWindow(m_window); }
	void restore() override { SDL_RestoreWindow(m_window); }
private:
	SDL_Window* m_window{};
	WindowMode m_mode{WindowMode::windowed};
};
}
