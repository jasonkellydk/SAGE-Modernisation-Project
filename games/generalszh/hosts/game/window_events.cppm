export module games.generalszh.hosts.game.window_events;
import std;

export import engine.platform.events;
export import engine.platform.core.types;

// The window's own events in a frame (the host's GameEngine::setQuitting and display resizing): whether it was asked to
// close (its X, Alt+F4, the platform quitting) and the size it was last stretched to. The frame then ends the loop or
// redraws at that size (between frames: the swap chain takes it; the display's GUI layout and camera view keep theirs and
// are drawn scaled onto it).
export namespace generalszh::host
{
struct WindowEvents
{
	bool closeRequested{false};
	std::optional<engine::platform::Extent2D> resizedTo;

	void Note(const engine::platform::PlatformEvent &event) noexcept
	{
		using engine::platform::EventType;
		if (event.type == EventType::quit || event.type == EventType::window_close_requested)
			closeRequested = true;
		if (event.type == EventType::window_resized && event.size.width > 0 && event.size.height > 0)
			resizedTo = event.size;
	}
};
}
