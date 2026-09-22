module;
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <limits>
export module engine.platform.adapters.sdl3.threading.condition;
import engine.platform.threading.condition;
import engine.platform.adapters.sdl3.threading.mutex;
export namespace engine::platform::sdl3
{
class SDL3ConditionVariable final : public IConditionVariable
{
public:
	SDL3ConditionVariable() : m_condition(SDL_CreateCondition()) {}
	~SDL3ConditionVariable() override { if (m_condition) SDL_DestroyCondition(m_condition); }
	bool wait(IMutex& mutex, std::uint32_t ms) override
	{
		auto* native = dynamic_cast<SDL3Mutex*>(&mutex); if (!native) return false;
		auto* native_mutex = static_cast<SDL_Mutex*>(native->native_handle());
		if (ms == std::numeric_limits<std::uint32_t>::max()) { SDL_WaitCondition(m_condition, native_mutex); return true; }
		return SDL_WaitConditionTimeout(m_condition, native_mutex, static_cast<Sint32>(std::min(ms, static_cast<std::uint32_t>(INT32_MAX))));
	}
	void notify_one() noexcept override { SDL_SignalCondition(m_condition); }
	void notify_all() noexcept override { SDL_BroadcastCondition(m_condition); }
	[[nodiscard]] bool valid() const noexcept { return m_condition != nullptr; }
private:
	SDL_Condition* m_condition{};
};
}
