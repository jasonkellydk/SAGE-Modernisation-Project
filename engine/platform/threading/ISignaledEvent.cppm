module;
#include <cstdint>
export module engine.platform.threading.event;

export namespace engine::platform
{
enum class EventResetMode : std::uint8_t { automatic, manual };
class ISignaledEvent
{
public:
	virtual ~ISignaledEvent() = default;
	virtual bool wait(std::uint32_t timeout_milliseconds = 0xFFFFFFFFu) = 0;
	virtual void signal() noexcept = 0;
	virtual void reset() noexcept = 0;
	[[nodiscard]] virtual bool is_signaled() const noexcept = 0;
};
}
