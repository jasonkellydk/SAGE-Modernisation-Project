module;
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
export module engine.platform.threading;
export import engine.platform.threading.thread;
export import engine.platform.threading.mutex;
export import engine.platform.threading.semaphore;
export import engine.platform.threading.condition;
export import engine.platform.threading.event;

export namespace engine::platform
{
using ThreadFunction = int (*)(void*);
class IThreadingService
{
public:
	virtual ~IThreadingService() = default;
	[[nodiscard]] virtual std::size_t logical_processor_count() const noexcept = 0;
	[[nodiscard]] virtual std::size_t recommended_worker_count() const noexcept = 0;
	virtual void sleep_for_microseconds(std::uint64_t duration) = 0;
	virtual void yield_thread() noexcept = 0;
	[[nodiscard]] virtual std::unique_ptr<IThread> create_thread(ThreadFunction function, void* data, const char* name) = 0;
	[[nodiscard]] virtual std::unique_ptr<IMutex> create_mutex() = 0;
	[[nodiscard]] virtual std::unique_ptr<ISemaphore> create_semaphore(std::uint32_t initial_count) = 0;
	[[nodiscard]] virtual std::unique_ptr<IConditionVariable> create_condition_variable() = 0;
	[[nodiscard]] virtual std::unique_ptr<ISignaledEvent> create_event(EventResetMode mode, bool initially_signaled = false) = 0;
	virtual int wait_any(std::span<ISignaledEvent* const> events, std::uint32_t timeout_milliseconds = 0xFFFFFFFFu) = 0;
};
}
