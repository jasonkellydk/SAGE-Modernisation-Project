module;
#include <SDL3/SDL.h>
export module engine.platform.adapters.sdl3.libraries.library;
import engine.platform.libraries.library;
export namespace engine::platform::sdl3
{
class SDL3SharedLibrary final : public ISharedLibrary
{
public:
	explicit SDL3SharedLibrary(SDL_SharedObject* object) noexcept : m_object(object) {}
	~SDL3SharedLibrary() override { if (m_object) SDL_UnloadObject(m_object); }
	[[nodiscard]] void* symbol(const char* name) const noexcept override { return m_object && name ? SDL_LoadFunction(m_object, name) : nullptr; }
private:
	SDL_SharedObject* m_object{};
};
}
