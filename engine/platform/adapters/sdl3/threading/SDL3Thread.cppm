module;
#include <SDL3/SDL.h>
#include <utility>
export module engine.platform.adapters.sdl3.threading.thread;
import engine.platform.threading.thread;
export namespace engine::platform::sdl3
{
class SDL3Thread final : public IThread
{
public:
	explicit SDL3Thread(SDL_Thread* thread) noexcept : m_thread(thread) {}
	~SDL3Thread() override { if (m_thread) { int ignored{}; SDL_WaitThread(m_thread, &ignored); } }
	int join() override { if (m_thread) SDL_WaitThread(std::exchange(m_thread, nullptr), &m_result); return m_result; }
private:
	SDL_Thread* m_thread{};
	int m_result{};
};
}
