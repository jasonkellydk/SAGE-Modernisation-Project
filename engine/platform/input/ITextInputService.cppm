export module engine.platform.text_input.interface;
import engine.platform.text_input;
import engine.platform.core.types;

export namespace engine::platform
{
class ITextInputService
{
public:
	virtual ~ITextInputService() = default;
	virtual bool start(WindowId window) = 0;
	virtual void stop(WindowId window) noexcept = 0;
	virtual bool set_area(WindowId window, const TextInputArea& area) = 0;
};
}
