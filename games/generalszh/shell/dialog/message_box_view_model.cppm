export module games.generalszh.shell.dialog.message_box_view_model;
import std;

export import engine.gui.mvvm.observable;

// The original's message box (MessageBox.cpp over MessageBox.wnd): a title,
// a message and the buttons asked for (Ok, Cancel, Yes, No), each running
// what the caller gave it and closing the box. It sits over every screen
// and takes the pointer while open.
export namespace generalszh::shell
{
struct MessageBoxRequest
{
	std::u16string title;
	std::u16string message;
	std::function<void()> ok, cancel, yes, no; // a button shows when it has an action (or is asked for)
	bool showOk{false}, showCancel{false}, showYes{false}, showNo{false};
};

// MessageBoxOk / MessageBoxOkCancel / MessageBoxYesNo.
inline MessageBoxRequest MessageBoxOk(std::u16string title, std::u16string message, std::function<void()> ok = {})
{
	return {std::move(title), std::move(message), std::move(ok), {}, {}, {}, true, false, false, false};
}
inline MessageBoxRequest MessageBoxOkCancel(std::u16string title, std::u16string message, std::function<void()> ok, std::function<void()> cancel = {})
{
	return {std::move(title), std::move(message), std::move(ok), std::move(cancel), {}, {}, true, true, false, false};
}
inline MessageBoxRequest MessageBoxYesNo(std::u16string title, std::u16string message, std::function<void()> yes, std::function<void()> no = {})
{
	return {std::move(title), std::move(message), {}, {}, std::move(yes), std::move(no), false, false, true, true};
}

class MessageBoxViewModel
{
public:
	MessageBoxViewModel()
	{
		ok.SetAction([this] { Press(m_request.ok); });
		cancel.SetAction([this] { Press(m_request.cancel); });
		yes.SetAction([this] { Press(m_request.yes); });
		no.SetAction([this] { Press(m_request.no); });
	}
	MessageBoxViewModel(const MessageBoxViewModel &) = delete;
	MessageBoxViewModel &operator=(const MessageBoxViewModel &) = delete;

	void Show(MessageBoxRequest request)
	{
		m_request = std::move(request);
		title.Set(m_request.title);
		message.Set(m_request.message);
		showOk.Set(m_request.showOk);
		showCancel.Set(m_request.showCancel);
		showYes.Set(m_request.showYes);
		showNo.Set(m_request.showNo);
		open.Set(true);
	}

	engine::gui::mvvm::Observable<bool> open{false};
	engine::gui::mvvm::Observable<std::u16string> title, message;
	engine::gui::mvvm::Observable<bool> showOk{false}, showCancel{false}, showYes{false}, showNo{false};
	engine::gui::mvvm::Command ok, cancel, yes, no;

private:
	void Press(const std::function<void()> &action)
	{
		const std::function<void()> run = action; // the box closes first; the action may open another
		open.Set(false);
		if (run)
			run();
	}

	MessageBoxRequest m_request;
};
}
