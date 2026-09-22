module;
#include <SDL3/SDL.h>
#include <memory>
#include <vector>
export module engine.platform.adapters.sdl3.display;
import engine.platform;

export namespace engine::platform::sdl3
{
class SDL3DisplayService final : public IDisplayService
{
public:
	[[nodiscard]] std::vector<DisplayInfo> displays() const override
	{
		std::vector<DisplayInfo> result; int count = 0;
		std::unique_ptr<SDL_DisplayID, decltype(&SDL_free)> ids(SDL_GetDisplays(&count), &SDL_free);
		if (!ids) return result;
		for (int i = 0; i < count; ++i) {
			const auto id = ids.get()[i]; DisplayInfo info{}; info.id = id;
			if (const char* name = SDL_GetDisplayName(id)) info.name = name;
			SDL_Rect bounds{}; if (SDL_GetDisplayBounds(id, &bounds)) { info.origin = {static_cast<float>(bounds.x), static_cast<float>(bounds.y)}; info.bounds = {bounds.w, bounds.h}; }
			SDL_Rect usable{}; if (SDL_GetDisplayUsableBounds(id, &usable)) info.usable_bounds = {usable.w, usable.h};
			info.content_scale = SDL_GetDisplayContentScale(id);
			if (const auto* mode = SDL_GetCurrentDisplayMode(id)) info.current_mode = {{mode->w, mode->h}, mode->refresh_rate};
			result.push_back(std::move(info));
		}
		return result;
	}
	[[nodiscard]] std::vector<DisplayMode> modes(DisplayId display) const override
	{
		std::vector<DisplayMode> result; int count = 0;
		std::unique_ptr<SDL_DisplayMode*, decltype(&SDL_free)> modes(SDL_GetFullscreenDisplayModes(display, &count), &SDL_free);
		if (!modes) return result;
		result.reserve(static_cast<std::size_t>(count));
		for (int i = 0; i < count; ++i) result.push_back({{modes.get()[i]->w, modes.get()[i]->h}, modes.get()[i]->refresh_rate});
		return result;
	}
	[[nodiscard]] DisplayId primary_display() const noexcept override { return SDL_GetPrimaryDisplay(); }
};
}
