export module engine.platform.crash_reporting;
export namespace engine::platform
{
using CrashHandler = void(*)(void* native_context, void* user_data);
class ICrashReportingService
{
public:
	virtual ~ICrashReportingService() = default;
	virtual bool install_handler(CrashHandler handler, void* user_data) = 0;
	virtual void uninstall_handler() noexcept = 0;
};
}
