export module games.generalszh.hosts.game.window_scale;
import std;

import engine.platform.core.types;
import engine.platform.events;

// The window resized freely (a modern convenience: the original's window keeps its display resolution): the game goes on
// drawing at its display resolution, presented stretched over the window (the swap chain's stretch scaling), and the
// pointer is taken back from window pixels to display pixels for everything that reads it.
export namespace generalszh::host
{
struct WindowScale
{
	float displayWidth{800.0f}, displayHeight{600.0f};
	float windowWidth{800.0f}, windowHeight{600.0f};

	// A resized window's new size (none or empty: kept).
	void Resized(const engine::platform::Extent2D &size) noexcept
	{
		if (size.width > 0 && size.height > 0)
		{
			windowWidth = static_cast<float>(size.width);
			windowHeight = static_cast<float>(size.height);
		}
	}

	// A pointer event's position in display pixels.
	void ToDisplay(engine::platform::PlatformEvent &event) const noexcept
	{
		using engine::platform::EventType;
		if (event.type != EventType::mouse_moved && event.type != EventType::mouse_button_down && event.type != EventType::mouse_button_up &&
			event.type != EventType::mouse_wheel)
			return;
		event.position.x = event.position.x * displayWidth / windowWidth;
		event.position.y = event.position.y * displayHeight / windowHeight;
	}
};
}
