module;
#include <SDL3/SDL.h>
#include <optional>
#include <string>
export module engine.platform.adapters.sdl3.system;
import engine.platform;

export namespace engine::platform::sdl3
{
class SDL3SystemService final : public ISystemService
{
public:
	[[nodiscard]] SystemInfo info() const override
	{
		const char* platform = SDL_GetPlatform();
		return {platform ? platform : "unknown", {}, SDL_GetNumLogicalCPUCores(), SDL_GetSystemRAM()};
	}
	[[nodiscard]] PowerStatus power_status() const noexcept override
	{
		int seconds = -1, percent = -1; const auto state = SDL_GetPowerInfo(&seconds, &percent);
		PowerState mapped = PowerState::unknown;
		switch (state) {
		case SDL_POWERSTATE_ON_BATTERY: mapped = PowerState::on_battery; break;
		case SDL_POWERSTATE_NO_BATTERY: mapped = PowerState::no_battery; break;
		case SDL_POWERSTATE_CHARGING: mapped = PowerState::charging; break;
		case SDL_POWERSTATE_CHARGED: mapped = PowerState::charged; break;
		default: break;
		}
		return {mapped, seconds, percent};
	}
	[[nodiscard]] std::optional<std::string> environment_variable(const std::string& name) const override
	{
		const char* value = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), name.c_str());
		return value ? std::optional<std::string>(value) : std::nullopt;
	}
	bool set_environment_variable(const std::string& name, const std::string& value) override
	{
		return SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name.c_str(), value.c_str(), true);
	}
	bool open_url(const std::string& url) override { return SDL_OpenURL(url.c_str()); }
};
}
