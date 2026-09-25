add_library(engine_platform STATIC)

target_sources(engine_platform PUBLIC
	FILE_SET CXX_MODULES
	BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
	FILES
		Platform.cppm
		Interface.cppm
		Runtime.cppm
		application/ApplicationService.cppm
		core/PlatformTypes.cppm
		window/WindowService.cppm
		window/IWindow.cppm
		window/IWindowService.cppm
		events/PlatformEvent.cppm
		input/InputService.cppm
		input/TextInputService.cppm
		input/ITextInputService.cppm
		threading/IThread.cppm
		threading/IMutex.cppm
		threading/ISemaphore.cppm
		threading/IConditionVariable.cppm
		threading/ISignaledEvent.cppm
		threading/ThreadingService.cppm
		time/ClockService.cppm
		display/DisplayService.cppm
		dialogs/DialogService.cppm
		clipboard/ClipboardService.cppm
		system/SystemService.cppm
		system/CrashReportingService.cppm
		libraries/SharedLibraryService.cppm
		libraries/ISharedLibrary.cppm
		process/IProcess.cppm
		process/ProcessService.cppm
		network/ISocket.cppm
		network/NetworkService.cppm
)

target_compile_features(engine_platform PUBLIC cxx_std_23)
set_property(TARGET engine_platform PROPERTY CXX_SCAN_FOR_MODULES ON)

find_package(SDL3 CONFIG REQUIRED)
include(FetchContent)
set(SDLNET_SAMPLES OFF)
set(SDLNET_INSTALL OFF)
FetchContent_Declare(SDL3_net
	GIT_REPOSITORY https://github.com/libsdl-org/SDL_net.git
	GIT_TAG release-3.2.0
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3_net)

add_library(engine_platform_sdl3 STATIC)
target_sources(engine_platform_sdl3 PUBLIC
	FILE_SET CXX_MODULES
	BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
	FILES
		adapters/sdl3/SDL3Platform.cppm
		adapters/sdl3/SDL3Runtime.cppm
		adapters/sdl3/application/SDL3ApplicationService.cppm
		adapters/sdl3/window/SDL3WindowService.cppm
		adapters/sdl3/window/SDL3Window.cppm
		adapters/sdl3/events/SDL3EventService.cppm
		adapters/sdl3/input/SDL3InputService.cppm
		adapters/sdl3/input/SDL3TextInputService.cppm
		adapters/sdl3/threading/SDL3ThreadingService.cppm
		adapters/sdl3/threading/SDL3Mutex.cppm
		adapters/sdl3/threading/SDL3Semaphore.cppm
		adapters/sdl3/threading/SDL3ConditionVariable.cppm
		adapters/sdl3/threading/SDL3SignaledEvent.cppm
		adapters/sdl3/threading/SDL3Thread.cppm
		adapters/sdl3/clock/SDL3ClockService.cppm
		adapters/sdl3/display/SDL3DisplayService.cppm
		adapters/sdl3/dialogs/SDL3DialogService.cppm
		adapters/sdl3/dialogs/SDL3FileDialogContext.cppm
		adapters/sdl3/clipboard/SDL3ClipboardService.cppm
		adapters/sdl3/system/SDL3SystemService.cppm
		adapters/sdl3/system/SDL3CrashReportingService.cppm
		adapters/sdl3/libraries/SDL3SharedLibraryService.cppm
		adapters/sdl3/libraries/SDL3SharedLibrary.cppm
		adapters/sdl3/process/SDL3ProcessService.cppm
		adapters/sdl3/process/SDL3Process.cppm
		adapters/sdl3/network/SDL3NetworkService.cppm
		adapters/sdl3/network/SDL3Socket.cppm
)
target_compile_features(engine_platform_sdl3 PUBLIC cxx_std_23)
target_link_libraries(engine_platform_sdl3 PUBLIC engine_platform SDL3::SDL3 SDL3_net::SDL3_net)
set_property(TARGET engine_platform_sdl3 PROPERTY CXX_SCAN_FOR_MODULES ON)
