module;
#include <SDL3/SDL.h>
#include <cstdint>
export module engine.platform.adapters.sdl3.threading.event;
import engine.platform.threading.event;

export namespace engine::platform::sdl3
{
class SDL3SignaledEvent final : public ISignaledEvent
{
public:
	explicit SDL3SignaledEvent(EventResetMode mode, bool signaled)
		: m_mutex(SDL_CreateMutex()), m_condition(SDL_CreateCondition()), m_mode(mode), m_signaled(signaled) {}
	~SDL3SignaledEvent() override { if (m_condition) SDL_DestroyCondition(m_condition); if (m_mutex) SDL_DestroyMutex(m_mutex); }
	[[nodiscard]] bool valid() const noexcept { return m_mutex != nullptr && m_condition != nullptr; }
	bool wait(std::uint32_t timeout) override
	{
		if (!valid()) return false;
		SDL_LockMutex(m_mutex);
		bool result = true;
		while (!m_signaled) {
			if (timeout == 0) { result = false; break; }
			bool woke = true;
			if (timeout == 0xFFFFFFFFu) SDL_WaitCondition(m_condition, m_mutex);
			else woke = SDL_WaitConditionTimeout(m_condition, m_mutex, timeout);
			if (!woke && !m_signaled) { result = false; break; }
		}
		if (result && m_mode == EventResetMode::automatic) m_signaled = false;
		SDL_UnlockMutex(m_mutex);
		return result;
	}
	void signal() noexcept override
	{
		if (!valid()) return;
		SDL_LockMutex(m_mutex);
		m_signaled = true;
		if (m_mode == EventResetMode::manual) SDL_BroadcastCondition(m_condition); else SDL_SignalCondition(m_condition);
		SDL_UnlockMutex(m_mutex);
	}
	void reset() noexcept override { if (valid()) { SDL_LockMutex(m_mutex); m_signaled = false; SDL_UnlockMutex(m_mutex); } }
	[[nodiscard]] bool is_signaled() const noexcept override
	{
		if (!valid()) return false;
		SDL_LockMutex(m_mutex);
		const bool value = m_signaled;
		SDL_UnlockMutex(m_mutex);
		return value;
	}
private:
	SDL_Mutex* m_mutex{};
	SDL_Condition* m_condition{};
	EventResetMode m_mode{};
	bool m_signaled{};
};
}
