module;
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <utility>
export module engine.platform.adapters.sdl3.threading;
import engine.platform.threading;
import engine.platform.adapters.sdl3.threading.mutex;
import engine.platform.adapters.sdl3.threading.semaphore;
import engine.platform.adapters.sdl3.threading.condition;
import engine.platform.adapters.sdl3.threading.thread;
import engine.platform.adapters.sdl3.threading.event;
export namespace engine::platform::sdl3
{
class SDL3ThreadingService final : public IThreadingService
{
public:
	std::size_t logical_processor_count() const noexcept override { return static_cast<std::size_t>(std::max(1, SDL_GetNumLogicalCPUCores())); }
	std::size_t recommended_worker_count() const noexcept override { auto n = logical_processor_count(); return n > 1 ? n / 2 : 1; }
	void sleep_for_microseconds(std::uint64_t duration) override { SDL_DelayNS(duration * 1000); }
	void yield_thread() noexcept override { std::this_thread::yield(); }
	std::unique_ptr<IThread> create_thread(ThreadFunction function, void* data, const char* name) override
	{
		if (!function) return {};
		auto* thread = SDL_CreateThread(function, name ? name : "engine-worker", data);
		return thread ? std::make_unique<SDL3Thread>(thread) : nullptr;
	}
	std::unique_ptr<IMutex> create_mutex() override { auto value = std::make_unique<SDL3Mutex>(); return value->valid() ? std::move(value) : nullptr; }
	std::unique_ptr<ISemaphore> create_semaphore(std::uint32_t initial) override { auto value = std::make_unique<SDL3Semaphore>(initial); return value->valid() ? std::move(value) : nullptr; }
	std::unique_ptr<IConditionVariable> create_condition_variable() override { auto value = std::make_unique<SDL3ConditionVariable>(); return value->valid() ? std::move(value) : nullptr; }
	std::unique_ptr<ISignaledEvent> create_event(EventResetMode mode, bool initial) override { auto value = std::make_unique<SDL3SignaledEvent>(mode, initial); return value->valid() ? std::move(value) : nullptr; }
	int wait_any(std::span<ISignaledEvent* const> events, std::uint32_t timeout) override
	{
		const auto start = SDL_GetTicksNS();
		for (;;) {
			for (std::size_t i = 0; i < events.size(); ++i) if (events[i] && events[i]->is_signaled() && events[i]->wait(0)) return static_cast<int>(i);
			if (timeout == 0 || (timeout != 0xFFFFFFFFu && SDL_GetTicksNS() - start >= static_cast<std::uint64_t>(timeout) * 1000000u)) return -1;
			SDL_DelayNS(1000000u);
		}
	}
};
}
