module;
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <limits>
export module engine.platform.adapters.sdl3.threading.semaphore;
import engine.platform.threading.semaphore;
export namespace engine::platform::sdl3
{
class SDL3Semaphore final : public ISemaphore
{
public:
	explicit SDL3Semaphore(std::uint32_t initial) : m_semaphore(SDL_CreateSemaphore(initial)) {}
	~SDL3Semaphore() override { if (m_semaphore) SDL_DestroySemaphore(m_semaphore); }
	bool wait(std::uint32_t ms) override
	{
		if (ms == std::numeric_limits<std::uint32_t>::max()) { SDL_WaitSemaphore(m_semaphore); return true; }
		return SDL_WaitSemaphoreTimeout(m_semaphore, static_cast<Sint32>(std::min(ms, static_cast<std::uint32_t>(INT32_MAX))));
	}
	bool post(std::uint32_t count) override { while (count--) SDL_SignalSemaphore(m_semaphore); return true; }
	[[nodiscard]] bool valid() const noexcept { return m_semaphore != nullptr; }
private:
	SDL_Semaphore* m_semaphore{};
};
}
