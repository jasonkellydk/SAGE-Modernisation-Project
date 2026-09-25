export module engine.platform.interface;
export import engine.platform.core.types;
export import engine.platform.application;
export import engine.platform.window.interface;
export import engine.platform.window.service;
export import engine.platform.events;
export import engine.platform.input;
export import engine.platform.text_input;
export import engine.platform.text_input.interface;
export import engine.platform.threading;
export import engine.platform.time;
export import engine.platform.display;
export import engine.platform.dialogs;
export import engine.platform.clipboard;
export import engine.platform.system;
export import engine.platform.crash_reporting;
export import engine.platform.libraries;
export import engine.platform.process;
export import engine.platform.network;

export namespace engine::platform
{
class IPlatform
{
public:
	virtual ~IPlatform() = default;
	[[nodiscard]] virtual const char* last_error() const noexcept = 0;
	[[nodiscard]] virtual IApplicationService& application() noexcept = 0;
	[[nodiscard]] virtual IWindowService& windows() noexcept = 0;
	[[nodiscard]] virtual IEventService& events() noexcept = 0;
	[[nodiscard]] virtual IInputService& input() noexcept = 0;
	[[nodiscard]] virtual ITextInputService& text_input() noexcept = 0;
	[[nodiscard]] virtual IThreadingService& threading() noexcept = 0;
	[[nodiscard]] virtual IClockService& clock() noexcept = 0;
	[[nodiscard]] virtual IDisplayService& displays() noexcept = 0;
	[[nodiscard]] virtual IDialogService& dialogs() noexcept = 0;
	[[nodiscard]] virtual IClipboardService& clipboard() noexcept = 0;
	[[nodiscard]] virtual ISystemService& system() noexcept = 0;
	[[nodiscard]] virtual ICrashReportingService& crash_reporting() noexcept = 0;
	[[nodiscard]] virtual ISharedLibraryService& libraries() noexcept = 0;
	[[nodiscard]] virtual IProcessService& processes() noexcept = 0;
	[[nodiscard]] virtual INetworkService& network() noexcept = 0;
};
}
