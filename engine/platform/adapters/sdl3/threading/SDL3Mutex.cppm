module;
#include <SDL3/SDL.h>
export module engine.platform.adapters.sdl3.threading.mutex;
import engine.platform.threading.mutex;
export namespace engine::platform::sdl3
{
class SDL3Mutex final : public IMutex
{
public:
	SDL3Mutex() : m_mutex(SDL_CreateMutex()) {}
	~SDL3Mutex() override { if (m_mutex) SDL_DestroyMutex(m_mutex); }
	void lock() override { SDL_LockMutex(m_mutex); }
	bool try_lock() override { return SDL_TryLockMutex(m_mutex); }
	void unlock() noexcept override { SDL_UnlockMutex(m_mutex); }
	[[nodiscard]] void* native_handle() const noexcept { return m_mutex; }
	[[nodiscard]] bool valid() const noexcept { return m_mutex != nullptr; }
private:
	SDL_Mutex* m_mutex{};
};
}
