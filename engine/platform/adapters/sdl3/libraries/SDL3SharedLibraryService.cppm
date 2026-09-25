module;
#include <SDL3/SDL.h>
#include <memory>
#include <string>
export module engine.platform.adapters.sdl3.libraries;
import engine.platform.libraries;
import engine.platform.adapters.sdl3.libraries.library;
export namespace engine::platform::sdl3
{
class SDL3SharedLibraryService final : public ISharedLibraryService
{
public:
	[[nodiscard]] std::unique_ptr<ISharedLibrary> load(const std::string& path) override
	{
		SDL_SharedObject* object = SDL_LoadObject(path.c_str());
		return object ? std::make_unique<SDL3SharedLibrary>(object) : nullptr;
	}
};
}
