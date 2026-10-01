export module games.generalszh.presentation.hud.algorithms.popup_message_layout;
import std;

export import games.generalszh.presentation.hud.resources.popup_message;

// INGAME_POPUP_MESSAGE: InGameUI::popupMessage (x and y as percents of the screen, clamped to 0..100; the width at least
// 50 pixels) and InGamePopupMessageInit's layout of InGamePopupMessage.wnd around the text word-wrapped to the width
// less 14: the text 2 pixels in, the OK button at the right under it, the window just tall enough.
export namespace generalszh::presentation
{
// InGameUI::popupMessage: the earlier message (if any) gone, the new one shown; the game waits when it asks to.
inline void ShowPopupMessage(PopupMessage &popup, std::u16string text, std::int64_t xPercent, std::int64_t yPercent, std::int64_t width, bool pause,
	std::int32_t screenWidth, std::int32_t screenHeight)
{
	popup.shown = true;
	popup.text = std::move(text);
	const std::int64_t x = std::clamp<std::int64_t>(xPercent, 0, 100);
	const std::int64_t y = std::clamp<std::int64_t>(yPercent, 0, 100);
	popup.x = static_cast<std::int32_t>(static_cast<float>(screenWidth) * (static_cast<float>(x) / 100.0f));
	popup.y = static_cast<std::int32_t>(static_cast<float>(screenHeight) * (static_cast<float>(y) / 100.0f));
	popup.width = static_cast<std::int32_t>(std::max<std::int64_t>(width, 50));
	popup.pause = pause;
	++popup.serial;
}

// InGameUI::clearPopupMessageData (its OK button, Enter or Esc): gone; true when it had paused the game.
inline bool ClosePopupMessage(PopupMessage &popup)
{
	const bool paused = popup.shown && popup.pause;
	popup.shown = false;
	popup.pause = false;
	return paused;
}

struct PopupRect
{
	std::int32_t x{0}, y{0}, width{0}, height{0};
};

struct PopupLayout
{
	PopupRect window; // InGamePopupMessageParent (screen pixels)
	PopupRect text;   // StaticTextMessage (in the window)
	PopupRect ok;     // ButtonOk (in the window)
	std::int32_t wrapWidth{0}; // the text's word wrap
};

// InGamePopupMessageInit for text `textHeight` tall once wrapped at wrapWidth, and an OK button okWidth x okHeight.
inline PopupLayout LayOutPopupMessage(const PopupMessage &popup, std::int32_t textHeight, std::int32_t okWidth, std::int32_t okHeight) noexcept
{
	PopupLayout layout;
	layout.wrapWidth = popup.width - 14;
	layout.window = {popup.x, popup.y, popup.width, textHeight + 7 + 2 + 2 + okHeight + 2};
	layout.text = {2, 2, popup.width - 4, textHeight + 7};
	layout.ok = {popup.width - okWidth - 2, textHeight + 7 + 2 + 2, okWidth, okHeight};
	return layout;
}
}
