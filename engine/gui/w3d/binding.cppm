export module engine.gui.w3d.binding;
import std;
export import engine.gui.mvvm.observable;

export namespace engine::gui::w3d
{
// Renderer-independent retained control state. Retail dialog resource
// decoding and W3D rendering consume this state in later migration slices.
struct Control
{
	std::string text;
	bool enabled{true};
};

class TextBinding
{
public:
	TextBinding(mvvm::Observable<std::string> &property, Control &control) : m_property(property)
	{
		m_id = property.Subscribe([&control](const std::string &text) { control.text = text; });
	}
	~TextBinding() { m_property.Unsubscribe(m_id); }
	TextBinding(const TextBinding &) = delete;
	TextBinding &operator=(const TextBinding &) = delete;
private:
	mvvm::Observable<std::string> &m_property;
	mvvm::SubscriptionId m_id;
};
class CommandBinding
{
public:
	CommandBinding(mvvm::Command &command, Control &control) : m_command(command)
	{
		m_id = command.enabled.Subscribe([&control](const bool &enabled) { control.enabled = enabled; });
	}
	~CommandBinding() { m_command.enabled.Unsubscribe(m_id); }
	CommandBinding(const CommandBinding &) = delete;
	CommandBinding &operator=(const CommandBinding &) = delete;
	bool Activate() { return m_command.Execute(); }
private:
	mvvm::Command &m_command;
	mvvm::SubscriptionId m_id;
};
}
