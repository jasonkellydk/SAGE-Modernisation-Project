module;
#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>
#include <stdexcept>
export module engine.platform.adapters.sdl3.runtime;
export namespace engine::platform::sdl3
{
class SDL3Runtime final
{
public:
	SDL3Runtime()
	{
		m_subsystems = SDL_INIT_VIDEO | SDL_INIT_EVENTS;
		if (!SDL_InitSubSystem(m_subsystems)) throw std::runtime_error(SDL_GetError());
		if (!NET_Init()) { SDL_QuitSubSystem(m_subsystems); throw std::runtime_error(SDL_GetError()); }
	}
	~SDL3Runtime() { NET_Quit(); SDL_QuitSubSystem(m_subsystems); }
	SDL3Runtime(const SDL3Runtime&) = delete;
	SDL3Runtime& operator=(const SDL3Runtime&) = delete;
private:
	SDL_InitFlags m_subsystems{};
};
}
