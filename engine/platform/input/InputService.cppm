module;
#include <cstdint>
export module engine.platform.input;
import engine.platform.core.types;

export namespace engine::platform
{
enum class CursorShape : std::uint8_t { arrow, ibeam, crosshair, hand, resize_horizontal, resize_vertical, resize_nwse, resize_nesw, resize_all, wait, forbidden };
struct KeyboardState
{
	const bool* keys{};
	int count{};
	bool caps_lock{};
	[[nodiscard]] bool down(int key) const noexcept
	{
		return keys != nullptr && key >= 0 && key < count && keys[key];
	}
};

struct MouseState
{
	Point2D position{};
	std::uint32_t buttons{};
};

class IInputService
{
public:
	virtual ~IInputService() = default;
	[[nodiscard]] virtual KeyboardState keyboard_state() const noexcept = 0;
	[[nodiscard]] virtual MouseState mouse_state() const noexcept = 0;
	virtual bool set_relative_mouse_mode(bool enabled) = 0;
	virtual bool show_cursor(bool visible) = 0;
	virtual bool set_cursor_shape(CursorShape shape) = 0;
	virtual bool warp_mouse(WindowId window, Point2D position) = 0;
	virtual bool capture_mouse(bool enabled) = 0;
};
}
