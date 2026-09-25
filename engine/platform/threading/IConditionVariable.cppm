module;
#include <cstdint>
export module engine.platform.threading.condition;
export import engine.platform.threading.mutex;
export namespace engine::platform
{
class IConditionVariable
{
public:
	virtual ~IConditionVariable() = default;
	virtual bool wait(IMutex& mutex, std::uint32_t timeout_milliseconds = 0xFFFFFFFFu) = 0;
	virtual void notify_one() noexcept = 0;
	virtual void notify_all() noexcept = 0;
};
}
