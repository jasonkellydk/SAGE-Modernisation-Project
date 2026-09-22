module;
#include <SDL3/SDL.h>
export module engine.platform.adapters.sdl3.text_input;
import engine.platform.text_input;
import engine.platform.text_input.interface;
import engine.platform.core.types;

export namespace engine::platform::sdl3
{
class SDL3TextInputService final : public ITextInputService
{
public:
	bool start(WindowId id) override
	{
		auto* window = SDL_GetWindowFromID(id);
		return window != nullptr && SDL_StartTextInput(window);
	}
	void stop(WindowId id) noexcept override
	{
		if (auto* window = SDL_GetWindowFromID(id)) SDL_StopTextInput(window);
	}
	bool set_area(WindowId id, const TextInputArea& area) override
	{
		auto* window = SDL_GetWindowFromID(id);
		if (window == nullptr) return false;
		SDL_Rect rectangle{area.x, area.y, area.width, area.height};
		return SDL_SetTextInputArea(window, &rectangle, area.cursor);
	}
};
}
