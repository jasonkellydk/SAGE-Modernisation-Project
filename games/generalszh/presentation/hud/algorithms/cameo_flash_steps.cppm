export module games.generalszh.presentation.hud.algorithms.cameo_flash_steps;
import std;

export import games.generalszh.presentation.hud.resources.cameo_flashes;

// CAMEO_FLASH on the control bar: ScriptActions::doCameoFlash sets a command button's flash count and turns the
// control bar's flash check on; ControlBar::update (once a client frame, 30 a second: the port's presentation ticks)
// flips each command window showing a counted button every tenth frame; ControlBar::processCommandUI stops it when the
// button is pressed; switching to no context clears the windows' flashes.
export namespace generalszh::presentation
{
inline constexpr std::int64_t LogicFramesPerSecond = 30; // LOGICFRAMES_PER_SECOND
inline constexpr std::int64_t DrawableFramesPerFlash = LogicFramesPerSecond / 2; // DRAWABLE_FRAMES_PER_FLASH
inline constexpr std::uint64_t CameoFlashInterval = 10; // ControlBar::update: TheGameClient->getFrame() % 10

// doCameoFlash: LOGICFRAMES_PER_SECOND x seconds / DRAWABLE_FRAMES_PER_FLASH, an odd count made even so the cameo ends
// as it started.
inline std::int32_t CameoFlashCount(std::int64_t seconds) noexcept
{
	const std::int64_t frames = LogicFramesPerSecond * seconds;
	std::int64_t count = frames / DrawableFramesPerFlash;
	if (count % 2 == 1)
		++count;
	return static_cast<std::int32_t>(count);
}

// doCameoFlash(button, seconds): TheControlBar->findCommandButton (`known`: false, and nothing happens, when there is no
// such button), button->setFlashCount, setFlash(TRUE).
inline void StartCameoFlash(CameoFlashes &flashes, std::string_view button, std::int64_t seconds, bool known)
{
	if (!known)
		return;
	const auto found = flashes.counts.find(button);
	if (found != flashes.counts.end())
		found->second = CameoFlashCount(seconds);
	else
		flashes.counts.emplace(std::string(button), CameoFlashCount(seconds));
	flashes.flash = true;
}

// ControlBar::processCommandUI: a pressed button stops flashing (setFlashCount(0), setFlash(FALSE)). The original leaves
// the window's flash status as it was, so a button pressed while lit stayed lit until the bar next showed no context;
// that is a bug-like quirk and the port clears the window it was pressed in (`window`).
inline void StopCameoFlash(CameoFlashes &flashes, std::string_view button, std::size_t window)
{
	if (const auto found = flashes.counts.find(button); found != flashes.counts.end())
		found->second = 0;
	flashes.flash = false;
	if (window < flashes.flashing.size())
		flashes.flashing[window] = false;
}

// ControlBar::switchToContext(CB_CONTEXT_NONE): "Clear any potentially flashing buttons!"
inline void ClearCameoFlashing(CameoFlashes &flashes) noexcept { flashes.flashing.fill(false); }

// ControlBar::update's flash check on `tick` for the command windows' buttons (`buttons`, by window; empty: none):
// every tenth frame each window with a counted button spends one of its count, lighting on an even count and going
// dark on an odd one; the check stops once a count runs out. Once a tick however often it is asked.
inline void StepCameoFlash(CameoFlashes &flashes, std::uint64_t tick, std::span<const std::string_view> buttons)
{
	if (flashes.lastTick == tick)
		return;
	flashes.lastTick = tick;
	if (!flashes.flash)
		return;
	for (std::size_t window = 0; window < buttons.size() && window < flashes.flashing.size(); ++window)
	{
		if (buttons[window].empty())
			continue;
		const auto found = flashes.counts.find(buttons[window]);
		if (found == flashes.counts.end() || found->second <= 0 || tick % CameoFlashInterval != 0)
			continue;
		std::int32_t &count = found->second;
		if (count % 2 == 0)
		{
			--count;
			flashes.flashing[window] = true;
		}
		else
		{
			--count;
			flashes.flashing[window] = false;
			if (count == 0)
				flashes.flash = false;
		}
	}
}
}
