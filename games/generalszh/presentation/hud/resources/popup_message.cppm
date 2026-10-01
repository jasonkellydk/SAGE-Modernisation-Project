export module games.generalszh.presentation.hud.resources.popup_message;
import std;

import engine.ecs.system.system;

// The in-game popup message (INGAME_POPUP_MESSAGE: the original's InGameUI PopupMessageData and its
// InGamePopupMessage.wnd): the text, where it sits on the screen (pixels), how wide, in which colour, and whether the
// game waits (paused) until it is dismissed.
export namespace generalszh::presentation
{
struct PopupMessage
{
	bool shown{false};
	std::u16string text;
	std::int32_t x{0};
	std::int32_t y{0};
	std::int32_t width{50};
	std::array<std::uint8_t, 4> color{255, 255, 255, 255}; // InGameUI.ini PopupMessageColor
	bool pause{false};
	std::uint32_t serial{0}; // changes with each new message (the host builds its window again)
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::PopupMessage>
{
	static constexpr std::string_view StableName = "generalszh.presentation.popup_message";
};
}
