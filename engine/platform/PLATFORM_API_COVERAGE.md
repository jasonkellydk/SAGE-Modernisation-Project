# Platform API coverage

This inventory records OS-facing use cases found by scanning the engine runtime
and active game device implementations. `Core/Tools`, `GeneralsMD/Code/Tools`,
and authoring utilities also contain Win32 UI and device code; those tool
applications are separate porting targets and are not silently treated as
covered by the runtime API.

| Existing use case | Platform contract | SDL3 adapter / boundary | Remaining migration work |
| --- | --- | --- | --- |
| Window creation, sizing, fullscreen, focus, minimize/restore and presentation handles | `IWindowService`, `IWindow`, `NativeWindowHandle` | `SDL3WindowService`, `SDL3Window` | Move `SDLPlatformWindow` to the service; graphics backends should request the native surface they support instead of accepting `HWND` directly. |
| Display enumeration, bounds, DPI and display modes | `IDisplayService` | `SDL3DisplayService` | Replace `Win32OSDisplay` and direct SDL display helpers with this service. |
| Keyboard/mouse state, cursor visibility, relative mode, capture, warp and text composition | `IInputService`, `IEventService`, `ITextInputService` | SDL3 input, event and text-input services | Route the legacy input wrappers and `IMEManager` through these services. SDL text composition replaces the old IMM candidate-list integration; game UI still needs to consume the SDL editing event. |
| `QueryPerformanceCounter`, `timeGetTime`, SDL ticks, sleeps and frame timing | `IClockService`, `IThreadingService` | `SDL3ClockService`, `SDL3ThreadingService` | Replace direct timing calls and define the legacy millisecond wraparound conversion at call sites. |
| Mutexes, semaphores, condition waits, signaled events, wait-any, worker creation and thread sleeps | `IThreadingService`, `IMutex`, `ISemaphore`, `IConditionVariable`, `ISignaledEvent`, `IThread` | `SDL3ThreadingService` and per-primitive SDL adapters | Migrate legacy WWLib, audio event and game synchronization wrappers; `engine.jobs` remains on portable C++ threads. |
| Message boxes, file/folder selection, clipboard and open URL | `IDialogService`, `IClipboardService`, `ISystemService` | Per-domain SDL3 adapters | Replace matching Win32 UI calls. SDL file dialogs are asynchronous, so callers must use the callback contract. |
| Environment, OS/memory/power queries and shared-library loading | `ISystemService`, `ISharedLibraryService`, `ISharedLibrary` | SDL3 system and shared-library services | Replace engine runtime calls to environment/system APIs and dynamic-library calls. CPU brand is optional because SDL does not report it portably. |
| TCP/UDP sockets, address resolution, readiness, send/receive, broadcast and reuse-address | `INetworkService`, `ISocket` | SDL3 adapter using SDL_net | Port `GameNetwork` socket call sites. SDL_net does not expose socket buffer sizing, ICMP echo, or the Windows SNMP TCP-table query used by legacy GameSpy code. |
| Child process launch and captured standard I/O | `IProcessService`, `IProcess` | `SDL3ProcessService`, `SDL3Process` | Port `WorkerProcess`. |
| Executable directory, single-instance lock and cross-process activation request | `IApplicationService` | SDL3 base-path lookup plus an SDL_net loopback datagram | Wire the main loop to poll activation requests and raise the game window; remove the old HWND class lookup. |
| Unhandled C++ termination callback | `ICrashReportingService` | `SDL3CrashReportingService` installs a standard C++ terminate handler | This does not write a native minidump or catch OS access violations. `MiniDumper` still needs a portable replacement or removal. |
| COM/ATL startup used by XAudio2 | Audio backend lifecycle | SDL does not provide COM | Keep COM ownership inside the Windows-only audio backend; it is not a cross-platform engine/platform service. |
| DX11/DX12 COM, DXGI swapchains and `HWND` presentation | Graphics-backend API, not a general platform service | `NativeWindowHandle` carries opaque OS surface values | Convert RHI options to request a typed native window descriptor. DX backends remain Windows-specific; other renderers provide their own surface implementation. |

The platform facade is a composition of domain services. The SDL3 adapter is
split by domain and class. Networking uses the SDL_net add-on because core SDL3
does not provide sockets. The current work establishes these contracts and
adapters; it does not yet rewrite every legacy engine call site to use them.
