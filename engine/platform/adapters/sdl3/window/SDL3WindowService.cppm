module;
#include <SDL3/SDL.h>
#include <memory>
export module engine.platform.adapters.sdl3.window;
import engine.platform.window.service;
import engine.platform.core.types;
import engine.platform.adapters.sdl3.window.native;
export namespace engine::platform::sdl3
{
class SDL3WindowService final : public IWindowService
{
public:
	std::unique_ptr<IWindow> create(const WindowConfig& config) override
	{
		SDL_WindowFlags flags = 0;
		if (config.resizable) flags |= SDL_WINDOW_RESIZABLE;
		if (config.highDpi) flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
		if (config.hidden) flags |= SDL_WINDOW_HIDDEN;
		SDL_Window* window = SDL_CreateWindow(config.title.c_str(), config.size.width, config.size.height, flags);
		if (window && config.minimum_size.width > 0 && config.minimum_size.height > 0)
			SDL_SetWindowMinimumSize(window, config.minimum_size.width, config.minimum_size.height);
		if (window && config.mode != WindowMode::windowed &&
			!SDL_SetWindowFullscreen(window, config.mode == WindowMode::fullscreen))
		{
			SDL_DestroyWindow(window);
			return nullptr;
		}
		return window ? std::make_unique<SDL3Window>(window, config.mode) : nullptr;
	}
};
}
