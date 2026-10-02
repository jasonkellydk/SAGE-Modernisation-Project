export module games.generalszh.presentation.hud.algorithms.screen_capture;
import std;

// W3DDisplay::takeScreenShot (CommandXlat's MSG_META_TAKE_SCREENSHOT, F12): the screen is written to the user data
// folder as "sshot<NNN>.bmp" (24-bit, CAPTURE_TO_TARGA off in the shipped build), the number from a counter that starts
// at 1 for the run and skips names already there, and InGameUI says GUI:ScreenCapture with the file's name.
export namespace generalszh::presentation
{
// The next free name from `counter` (advanced past it): sshot001.bmp, sshot002.bmp...
inline std::string NextScreenshotName(int &counter, const std::function<bool(const std::string &)> &exists)
{
	for (;;)
	{
		std::string name = std::format("sshot{:03}.bmp", counter++);
		if (!exists || !exists(name))
			return name;
	}
}

// GUI:ScreenCapture ("Screen Captured to '%s'") with the leaf name for its %s.
inline std::u16string ScreenCaptureMessage(std::u16string format, std::string_view leaf)
{
	for (const std::u16string_view marker : {std::u16string_view(u"%ls"), std::u16string_view(u"%s")})
		if (const auto at = format.find(marker); at != std::u16string::npos)
			return format.replace(at, marker.size(), std::u16string(leaf.begin(), leaf.end()));
	return format;
}
}
