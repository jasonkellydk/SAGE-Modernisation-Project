module;
#include <cstdint>
#include <optional>
#include <string>
export module engine.platform.system;

export namespace engine::platform
{
enum class PowerState : std::uint8_t { unknown, on_battery, no_battery, charging, charged };
struct PowerStatus { PowerState state{PowerState::unknown}; int seconds_remaining{-1}; int battery_percent{-1}; };
struct SystemInfo { std::string operating_system; std::optional<std::string> cpu; int logical_processors{}; int physical_memory_mb{}; };

class ISystemService
{
public:
	virtual ~ISystemService() = default;
	[[nodiscard]] virtual SystemInfo info() const = 0;
	[[nodiscard]] virtual PowerStatus power_status() const noexcept = 0;
	[[nodiscard]] virtual std::optional<std::string> environment_variable(const std::string& name) const = 0;
	virtual bool set_environment_variable(const std::string& name, const std::string& value) = 0;
	virtual bool open_url(const std::string& url) = 0;
};
}
