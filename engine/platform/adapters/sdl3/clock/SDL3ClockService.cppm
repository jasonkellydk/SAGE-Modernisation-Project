module;
#include <SDL3/SDL.h>
#include <cstdint>
export module engine.platform.adapters.sdl3.clock;
import engine.platform;

export namespace engine::platform::sdl3
{
class SDL3ClockService final : public IClockService
{
public:
	[[nodiscard]] std::uint64_t monotonic_nanoseconds() const noexcept override { return SDL_GetTicksNS(); }
};
}
