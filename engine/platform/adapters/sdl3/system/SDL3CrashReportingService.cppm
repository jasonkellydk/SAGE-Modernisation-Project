module;
#include <exception>
export module engine.platform.adapters.sdl3.crash_reporting;
import engine.platform.crash_reporting;

export namespace engine::platform::sdl3
{
namespace detail
{
CrashHandler active_crash_handler{};
void* active_crash_data{};
std::terminate_handler previous_terminate_handler{};
void handle_terminate()
{
	if (active_crash_handler) active_crash_handler(nullptr, active_crash_data);
	if (previous_terminate_handler) previous_terminate_handler();
	std::abort();
}
}
class SDL3CrashReportingService final : public ICrashReportingService
{
public:
	~SDL3CrashReportingService() override { uninstall_handler(); }
	bool install_handler(CrashHandler handler, void* data) override
	{
		if (!handler || m_installed) return false;
		detail::active_crash_handler = handler;
		detail::active_crash_data = data;
		detail::previous_terminate_handler = std::set_terminate(detail::handle_terminate);
		m_installed = true;
		return true;
	}
	void uninstall_handler() noexcept override
	{
		if (!m_installed) return;
		std::set_terminate(detail::previous_terminate_handler);
		detail::active_crash_handler = nullptr;
		detail::active_crash_data = nullptr;
		m_installed = false;
	}
private:
	bool m_installed{};
};
}
