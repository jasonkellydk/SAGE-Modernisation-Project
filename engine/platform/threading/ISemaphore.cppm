module;
#include <cstdint>
export module engine.platform.threading.semaphore;
export namespace engine::platform
{
class ISemaphore
{
public:
	virtual ~ISemaphore() = default;
	[[nodiscard]] virtual bool wait(std::uint32_t timeout_milliseconds = 0xFFFFFFFFu) = 0;
	virtual bool post(std::uint32_t count = 1) = 0;
};
}
