export module engine.platform.input;
import std;
import engine.platform.core.types;
import engine.platform.events;

export namespace engine::platform
{
enum class CursorShape : std::uint8_t { arrow, ibeam, crosshair, hand, resize_horizontal, resize_vertical, resize_nwse, resize_nesw, resize_all, wait, forbidden };
struct KeyboardState
{
	const bool* keys{};
	int count{};
	bool caps_lock{};
	// Optional adapter metadata maps portable physical keys to the borrowed
	// native state array, preserving existing integer-indexed callers.
	std::span<const int> key_indices;
	[[nodiscard]] bool down(int key) const noexcept
	{
		return keys != nullptr && key >= 0 && key < count && keys[key];
	}
	[[nodiscard]] bool down(KeyCode key) const noexcept
	{
		const auto index=static_cast<unsigned>(key);
		return index>0 && index<key_indices.size() && key_indices[index]>0 && down(key_indices[index]);
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
	// Explicit ownership avoids a transient missing mouse-focus window during
	// scene publication, focus changes, or release after Alt-Tab.
	virtual bool set_relative_mouse_mode(WindowId window, bool enabled) { return set_relative_mouse_mode(enabled); }
	[[nodiscard]] virtual bool relative_mouse_mode(WindowId window) const noexcept { return false; }
	virtual bool show_cursor(bool visible) = 0;
	virtual bool set_cursor_shape(CursorShape shape) = 0;
	virtual bool warp_mouse(WindowId window, Point2D position) = 0;
	virtual bool capture_mouse(bool enabled) = 0;
};
}
