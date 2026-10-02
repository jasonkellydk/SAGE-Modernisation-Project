export module engine.gui.mvvm.observable;
import std;

// MVVM building blocks: view models expose Observable properties and
// Commands; views bind to them without the view model knowing the view.
export namespace engine::gui::mvvm
{
using SubscriptionId = std::uint32_t;

template<typename T>
class Observable
{
public:
	using Listener = std::function<void(const T &)>;

	Observable() = default;
	explicit Observable(T initial) : m_value(std::move(initial)) {}
	Observable(const Observable &) = delete;
	Observable &operator=(const Observable &) = delete;

	const T &Get() const noexcept { return m_value; }

	// Notifies listeners only when the value changes.
	void Set(T value)
	{
		if (value == m_value)
			return;
		m_value = std::move(value);
		for (const auto &[id, listener] : std::vector(m_listeners))
			listener(m_value);
	}

	// Calls `listener` now with the current value and on every change.
	SubscriptionId Subscribe(Listener listener)
	{
		const SubscriptionId id = ++m_nextId;
		listener(m_value);
		m_listeners.emplace_back(id, std::move(listener));
		return id;
	}

	void Unsubscribe(SubscriptionId id)
	{
		std::erase_if(m_listeners, [id](const auto &entry) { return entry.first == id; });
	}

private:
	T m_value{};
	std::vector<std::pair<SubscriptionId, Listener>> m_listeners;
	SubscriptionId m_nextId{0};
};

// An action a view can trigger (a button press), with an observable
// enabled state the view can reflect.
class Command
{
public:
	Command() = default;
	explicit Command(std::function<void()> execute) : m_execute(std::move(execute)) {}
	Command(const Command &) = delete;
	Command &operator=(const Command &) = delete;

	void SetAction(std::function<void()> execute) { m_execute = std::move(execute); }

	bool Execute()
	{
		if (!enabled.Get() || !m_execute)
			return false;
		m_execute();
		return true;
	}

	Observable<bool> enabled{true};

private:
	std::function<void()> m_execute;
};
}
