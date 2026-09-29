export module engine.platform.clipboard;
import std;

export namespace engine::platform
{
class IClipboardService
{
public:
	virtual ~IClipboardService() = default;
	[[nodiscard]] virtual std::optional<std::string> text() const = 0;
	virtual bool set_text(const std::string& text) = 0;
};
}
