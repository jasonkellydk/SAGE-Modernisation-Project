export module games.generalszh.presentation.hud.algorithms.message_list;
import std;

export import games.generalszh.presentation.hud.resources.in_game_messages;

// InGameUI::addMessageText and the message fading in InGameUI::update.
export namespace generalszh::presentation
{
// A new message on top: the oldest drops off, the rest move down a line. Its colour is the given one (opaque), else
// the first colour unless the one under it is shown in the first colour, then the second (the lines alternate).
inline void AddMessage(InGameMessages &messages, std::u16string text, std::uint64_t tick, std::optional<std::array<std::uint8_t, 3>> rgb = std::nullopt)
{
	auto color1 = messages.color1, color2 = messages.color2;
	if (rgb)
		color1 = color2 = {(*rgb)[0], (*rgb)[1], (*rgb)[2], 255};
	auto &slots = messages.slots;
	for (std::size_t index = slots.size() - 1; index >= 1; --index)
		slots[index] = slots[index - 1];
	slots[0] = InGameMessage{std::move(text), tick, {}, true};
	slots[0].color = !slots[1].shown || slots[1].color == color2 ? color1 : color2;
}

// Once a message is older than the timeout, each tick its alpha drops by a hundredth of its age in ticks (whole
// numbers: nothing for its first hundred ticks); at none it goes.
inline void StepMessages(InGameMessages &messages, std::uint64_t tick)
{
	for (std::size_t index = messages.slots.size(); index-- > 0;)
	{
		InGameMessage &message = messages.slots[index];
		if (tick - message.tick <= messages.timeoutTicks)
			continue;
		const auto amount = static_cast<std::int64_t>(static_cast<float>(tick - message.tick) * 0.01f); // REAL_TO_INT
		std::uint8_t &alpha = message.color[3];
		alpha = alpha - amount < 0 ? std::uint8_t{0} : static_cast<std::uint8_t>(alpha - amount);
		if (alpha == 0)
			message = InGameMessage{};
	}
}

// "%ls" (or "%s") in the text replaced by `name` (the original's swprintf of the label's format).
inline std::u16string FormatWithName(std::u16string_view format, std::u16string_view name)
{
	std::u16string text(format);
	for (const std::u16string_view marker : {std::u16string_view(u"%ls"), std::u16string_view(u"%s")})
		if (const auto at = text.find(marker); at != std::u16string::npos)
		{
			text.replace(at, marker.size(), name);
			break;
		}
	return text;
}
}
