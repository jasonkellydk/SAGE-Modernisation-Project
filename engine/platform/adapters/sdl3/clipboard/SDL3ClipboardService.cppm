module;
#include <SDL3/SDL.h>
#include <optional>
#include <string>
export module engine.platform.adapters.sdl3.clipboard;
import engine.platform;

export namespace engine::platform::sdl3
{
class SDL3ClipboardService final : public IClipboardService
{
public:
	[[nodiscard]] std::optional<std::string> text() const override
	{
		char* value = SDL_GetClipboardText(); if (!value) return std::nullopt;
		std::string result(value); SDL_free(value); return result;
	}
	bool set_text(const std::string& value) override { return SDL_SetClipboardText(value.c_str()); }
};
}
