module;
#include <memory>
export module engine.platform.window.service;
import engine.platform.core.types;
export import engine.platform.window.interface;

export namespace engine::platform
{
class IWindowService
{
public:
	virtual ~IWindowService() = default;
	[[nodiscard]] virtual std::unique_ptr<IWindow> create(const WindowConfig& config) = 0;
};
}
