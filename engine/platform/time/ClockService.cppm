module;
#include <cstdint>
export module engine.platform.time;

export namespace engine::platform
{
class IClockService
{
public:
	virtual ~IClockService() = default;
	[[nodiscard]] virtual std::uint64_t monotonic_nanoseconds() const noexcept = 0;
};
}
