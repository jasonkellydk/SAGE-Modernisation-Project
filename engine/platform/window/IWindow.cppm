module;
#include <optional>
#include <string>
export module engine.platform.window.interface;
import engine.platform.core.types;

export namespace engine::platform
{
class IWindow
{
public:
	virtual ~IWindow() = default;
	[[nodiscard]] virtual WindowId id() const noexcept = 0;
	[[nodiscard]] virtual Extent2D size() const noexcept = 0;
	[[nodiscard]] virtual Extent2D drawable_size() const noexcept = 0;
	[[nodiscard]] virtual Point2D position() const noexcept = 0;
	[[nodiscard]] virtual std::optional<NativeWindowHandle> native_handle(NativeWindowSystem system) const noexcept = 0;
	[[nodiscard]] virtual bool is_minimized() const noexcept = 0;
	[[nodiscard]] virtual bool has_focus() const noexcept = 0;
	[[nodiscard]] virtual WindowMode mode() const noexcept = 0;
	virtual bool set_size(Extent2D size) = 0;
	virtual bool set_minimum_size(Extent2D size) = 0;
	virtual bool set_maximum_size(Extent2D size) = 0;
	virtual bool set_position(Point2D position) = 0;
	virtual bool set_title(const std::string& title) = 0;
	virtual bool set_mode(WindowMode mode) = 0;
	virtual void show() = 0;
	virtual void hide() = 0;
	virtual bool raise() = 0;
	virtual void minimize() = 0;
	virtual void maximize() = 0;
	virtual void restore() = 0;
};
}
