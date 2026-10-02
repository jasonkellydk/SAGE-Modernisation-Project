export module engine.platform.application;
import std;
export import engine.platform.core.types;

export namespace engine::platform
{
class IApplicationService
{
public:
	virtual ~IApplicationService() = default;
	[[nodiscard]] virtual std::filesystem::path executable_directory() const = 0;
	[[nodiscard]] virtual std::filesystem::path preference_directory(const std::string& organization,const std::string& application) const = 0;
	virtual bool acquire_single_instance(const std::string& identifier) = 0;
	virtual bool send_activation_request(const std::string& identifier) = 0;
	virtual bool poll_activation_request(std::uint32_t timeout_milliseconds) = 0;
	virtual bool activate_window(WindowId window) = 0;
};
}
