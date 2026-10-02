export module engine.platform.core.types;
import std;

export namespace engine::platform
{

using WindowId = std::uint32_t;

struct Extent2D
{
	int width{1280};
	int height{720};
};

struct Point2D
{
	float x{};
	float y{};
};

enum class WindowMode : std::uint8_t { windowed, borderless, fullscreen };

struct WindowConfig
{
	std::string title{"SAGE"};
	Extent2D size{};
	Extent2D minimum_size{0, 0};
	bool resizable{true};
	bool highDpi{true};
	bool hidden{};
	WindowMode mode{WindowMode::windowed};
};

enum class NativeWindowSystem : std::uint8_t { win32, x11, wayland, cocoa, uikit, android, unknown, automatic };

// Native values are intentionally opaque. A renderer asks for the system it
// supports and interprets the values only inside its platform backend.
struct NativeWindowHandle
{
	NativeWindowSystem system{NativeWindowSystem::unknown};
	void* display{};
	void* window{};
};

} // namespace engine::platform
