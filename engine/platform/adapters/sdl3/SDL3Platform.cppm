module;
#include <SDL3/SDL.h>
export module engine.platform.adapters.sdl3;
import engine.platform;
import engine.platform.adapters.sdl3.runtime;
import engine.platform.adapters.sdl3.application;
import engine.platform.adapters.sdl3.window;
import engine.platform.adapters.sdl3.events;
import engine.platform.adapters.sdl3.input;
import engine.platform.adapters.sdl3.text_input;
import engine.platform.adapters.sdl3.threading;
import engine.platform.adapters.sdl3.clock;
import engine.platform.adapters.sdl3.display;
import engine.platform.adapters.sdl3.dialogs;
import engine.platform.adapters.sdl3.clipboard;
import engine.platform.adapters.sdl3.system;
import engine.platform.adapters.sdl3.crash_reporting;
import engine.platform.adapters.sdl3.libraries;
import engine.platform.adapters.sdl3.process;
import engine.platform.adapters.sdl3.network;

export namespace engine::platform::sdl3
{
// Composition root; each domain adapter is declared and built in its own module.
class SDL3PlatformAdapter final : public IPlatform
{
public:
	[[nodiscard]] const char* last_error() const noexcept override { return SDL_GetError(); }
	[[nodiscard]] IApplicationService& application() noexcept override { return m_application; }
	[[nodiscard]] IWindowService& windows() noexcept override { return m_windows; }
	[[nodiscard]] IEventService& events() noexcept override { return m_events; }
	[[nodiscard]] IInputService& input() noexcept override { return m_input; }
	[[nodiscard]] ITextInputService& text_input() noexcept override { return m_textInput; }
	[[nodiscard]] IThreadingService& threading() noexcept override { return m_threading; }
	[[nodiscard]] IClockService& clock() noexcept override { return m_clock; }
	[[nodiscard]] IDisplayService& displays() noexcept override { return m_displays; }
	[[nodiscard]] IDialogService& dialogs() noexcept override { return m_dialogs; }
	[[nodiscard]] IClipboardService& clipboard() noexcept override { return m_clipboard; }
	[[nodiscard]] ISystemService& system() noexcept override { return m_system; }
	[[nodiscard]] ICrashReportingService& crash_reporting() noexcept override { return m_crashReporting; }
	[[nodiscard]] ISharedLibraryService& libraries() noexcept override { return m_libraries; }
	[[nodiscard]] IProcessService& processes() noexcept override { return m_processes; }
	[[nodiscard]] INetworkService& network() noexcept override { return m_network; }
private:
	SDL3Runtime m_runtime;
	SDL3ApplicationService m_application;
	SDL3WindowService m_windows;
	SDL3EventService m_events;
	SDL3InputService m_input;
	SDL3TextInputService m_textInput;
	SDL3ThreadingService m_threading;
	SDL3ClockService m_clock;
	SDL3DisplayService m_displays;
	SDL3DialogService m_dialogs;
	SDL3ClipboardService m_clipboard;
	SDL3SystemService m_system;
	SDL3CrashReportingService m_crashReporting;
	SDL3SharedLibraryService m_libraries;
	SDL3ProcessService m_processes;
	SDL3NetworkService m_network;
};

}
