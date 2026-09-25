module;
#include <cstdint>
#include <string>
#include <vector>
export module engine.platform.display;
import engine.platform.core.types;

export namespace engine::platform
{
using DisplayId = std::uint32_t;
struct DisplayMode
{
	Extent2D resolution{};
	float refresh_rate{};
};
struct DisplayInfo
{
	DisplayId id{};
	std::string name;
	Point2D origin{};
	Extent2D bounds{};
	Extent2D usable_bounds{};
	float content_scale{1.0f};
	DisplayMode current_mode{};
};

class IDisplayService
{
public:
	virtual ~IDisplayService() = default;
	[[nodiscard]] virtual std::vector<DisplayInfo> displays() const = 0;
	[[nodiscard]] virtual std::vector<DisplayMode> modes(DisplayId display) const = 0;
	[[nodiscard]] virtual DisplayId primary_display() const noexcept = 0;
};
}
