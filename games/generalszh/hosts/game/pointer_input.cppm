export module games.generalszh.hosts.game.pointer_input;
import std;

export import engine.platform.events;
export import games.generalszh.hosts.game.game_client;

// The player's pointer in a match from the platform's events (the original's Win32Mouse / Win32DIKeyboard as the
// world's translators read them), and the checks' scripted drags and clicks.
export namespace generalszh::host
{
// A mouse event: where the pointer is, its buttons (left 1, middle 2, right 3 in the platform's numbering: the pointer's 1,
// 4 and 2)
// held, pressed and let go since the last frame, the wheel's notches. False: not a mouse event.
inline bool PointMouse(PointerState &pointer, const engine::platform::PlatformEvent &event)
{
	using engine::platform::EventType;
	const std::uint8_t button = event.code == 1 ? 1u : event.code == 3 ? 2u : event.code == 2 ? 4u : 0u;
	switch (event.type)
	{
	case EventType::mouse_moved:
		pointer.x = static_cast<float>(event.position.x);
		pointer.y = static_cast<float>(event.position.y);
		return true;
	case EventType::mouse_button_down:
		pointer.x = static_cast<float>(event.position.x);
		pointer.y = static_cast<float>(event.position.y);
		pointer.down |= button;
		pointer.pressed |= button;
		return true;
	case EventType::mouse_button_up:
		pointer.x = static_cast<float>(event.position.x);
		pointer.y = static_cast<float>(event.position.y);
		pointer.down &= static_cast<std::uint8_t>(~button);
		pointer.released |= button;
		return true;
	case EventType::mouse_wheel: pointer.wheel += event.flipped ? -event.y : event.y; return true;
	default: return false;
	}
}

// LookAtTranslator with input off (doDisableInput): the wheel still zooms and the middle button still turns the view;
// the pointer without its left and right buttons (no orders, no selection).
inline PointerState LookOnlyPointer(PointerState pointer) noexcept
{
	pointer.down &= 4u;
	pointer.pressed &= 4u;
	pointer.released &= 4u;
	return pointer;
}

// A key down or up: the arrow keys held (1 up, 2 down, 4 left, 8 right) for the camera's scrolling.
inline void HoldArrows(PointerState &pointer, const engine::platform::PlatformEvent &event)
{
	using engine::platform::EventType;
	using engine::platform::KeyCode;
	const std::uint8_t arrow = event.key == KeyCode::up ? 1u : event.key == KeyCode::down ? 2u : event.key == KeyCode::left ? 4u : event.key == KeyCode::right ? 8u : 0u;
	if (event.type == EventType::key_down)
		pointer.arrows |= arrow;
	else if (event.type == EventType::key_up)
		pointer.arrows &= static_cast<std::uint8_t>(~arrow);
}

// The checks' scripted input, `frame` frames into the match: clicks (--click) down and up at 70 frames, then every 20,
// the pointer arriving 10 frames before each (a ghost being placed follows it there); after them a drag (--drag)
// down 60 frames in, across the next, up the one after.
inline void ScriptPointer(PointerState &pointer, std::span<const std::pair<float, float>> clicks, const std::optional<std::array<float, 4>> &drag, std::uint64_t frame)
{
	for (std::size_t click = 0; click < clicks.size(); ++click)
	{
		const std::uint64_t at = 70 + click * 20;
		if (frame + 10 == at || frame == at || frame == at + 1)
		{
			pointer.x = clicks[click].first;
			pointer.y = clicks[click].second;
		}
		if (frame == at)
		{
			pointer.down |= 1u;
			pointer.pressed |= 1u;
		}
		else if (frame == at + 1)
		{
			pointer.down &= static_cast<std::uint8_t>(~1u);
			pointer.released |= 1u;
		}
	}
	if (drag && frame >= 60 && frame <= 62)
	{
		const auto &box = *drag;
		pointer.x = frame == 60 ? box[0] : box[2];
		pointer.y = frame == 60 ? box[1] : box[3];
		if (frame == 60)
		{
			pointer.down |= 1u;
			pointer.pressed |= 1u;
		}
		else if (frame == 62)
		{
			pointer.down &= static_cast<std::uint8_t>(~1u);
			pointer.released |= 1u;
		}
	}
}
}
