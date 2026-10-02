export module games.generalszh.hud.control_bar_stage;
import std;

export import engine.gui.mvvm.observable;

// The control bar's stage and its slide (the original's ControlBar::switchControlBarStage / toggleControlBarStage,
// setDefaultControlBarConfig / setLowControlBarConfig / setUpDownImages, and ShowControlBar's
// WIN_ANIMATION_SLIDE_BOTTOM through AnimateWindowManager and ProcessAnimateWindowSlideFromBottom): the minimise button
// (ButtonLarge) drops the bar to the bottom tenth of the screen and raises it again; shown not immediately (a game's
// start, the end of a letterbox) the bar slides up from below the screen. Headless: positions are ControlBarParent's top
// in display pixels; the host moves the layout.
export namespace generalszh::hud
{
enum class ControlBarStage : std::uint8_t
{
	Default, // CONTROL_BAR_STAGE_DEFAULT
	Low,     // CONTROL_BAR_STAGE_LOW: minimised
	Hidden,  // CONTROL_BAR_STAGE_HIDDEN
};

// ProcessAnimateWindowSlideFromBottom's state for one window (AnimateWindow: its start, end and current y, its velocity).
struct BottomSlide
{
	bool finished{true};
	int startY{0}, endY{0}, currentY{0};
	float velocity{0.0f};
};

inline constexpr float SlideMaxVelocity = -40.0f;   // m_maxVel.y
inline constexpr int SlideSlowDownThreshold = 80;   // m_slowDownThreshold
inline constexpr float SlideSlowDownRatio = 0.67f;  // m_slowDownRatio

// initAnimateWindow: the window's current position is its rest; it starts the display's width below it (travelDistance
// = TheDisplay->getWidth()), moving up at the top speed.
inline BottomSlide StartBottomSlide(int restY, int displayWidth)
{
	BottomSlide slide;
	slide.finished = false;
	slide.endY = restY;
	slide.startY = slide.currentY = restY + displayWidth;
	slide.velocity = SlideMaxVelocity;
	return slide;
}

// updateAnimateWindow, once per AnimateWindowManager::update: y moves by the velocity (truncated to whole pixels); past
// the rest it stops there, finished; within the slow-down threshold of it the velocity shrinks by the ratio, never slower
// than a pixel a frame. True once finished.
inline bool StepBottomSlide(BottomSlide &slide)
{
	if (slide.finished)
		return true;
	slide.currentY += static_cast<int>(slide.velocity);
	if (slide.currentY < slide.endY)
	{
		slide.currentY = slide.endY;
		slide.finished = true;
		return true;
	}
	if (slide.currentY - slide.endY <= SlideSlowDownThreshold)
		slide.velocity *= SlideSlowDownRatio;
	if (slide.velocity >= -1.0f)
		slide.velocity = -1.0f;
	return false;
}

// setLowControlBarConfig: the minimised bar's top, TheDisplay->getHeight() - .1 * TheDisplay->getHeight(), truncated.
inline int LowStageTop(int displayHeight)
{
	return static_cast<int>(static_cast<double>(displayHeight) - 0.1 * static_cast<double>(displayHeight));
}

// setDefaultControlBarConfig / ShowControlBar / ToggleControlBar showing it: the tactical view shrinks above the bar,
// TheTacticalView->setHeight((Int)(TheDisplay->getHeight() * 0.80f)).
inline int DefaultStageViewHeight(int displayHeight)
{
	return static_cast<int>(static_cast<float>(displayHeight) * 0.80f);
}

class ControlBarStageViewModel
{
public:
	ControlBarStageViewModel()
	{
		toggle.SetAction([this] { Toggle(); });
	}

	ControlBarStageViewModel(const ControlBarStageViewModel &) = delete;
	ControlBarStageViewModel &operator=(const ControlBarStageViewModel &) = delete;

	// ControlBar::init: m_defaultControlBarPosition (the bar's top as laid out) and the display's size.
	void SetLayout(int restTop, int displayWidth, int displayHeight)
	{
		m_restTop = restTop;
		m_displayWidth = displayWidth;
		m_displayHeight = displayHeight;
		m_top = m_stage == ControlBarStage::Low ? LowStageTop(displayHeight) : restTop;
		top.Set(Shown());
		// InGameUI::init: the tactical view as tall as the display until a stage or a show says otherwise.
		viewHeight.Set(displayHeight);
	}

	// RECORDERMODETYPE_PLAYBACK: switchControlBarStage does nothing while a replay plays back.
	void SetPlayback(bool playback) noexcept { m_playback = playback; }

	// switchControlBarStage: DEFAULT puts the bar at its laid-out place, LOW at the bottom tenth; either sets the minimise
	// button's images (setUpDownImages). Nothing during playback.
	void Switch(ControlBarStage stage)
	{
		if (m_playback)
			return;
		m_stage = stage;
		minimised.Set(stage == ControlBarStage::Low);
		if (stage == ControlBarStage::Default)
		{
			m_top = m_restTop;
			viewHeight.Set(DefaultStageViewHeight(m_displayHeight));
		}
		else if (stage == ControlBarStage::Low)
		{
			m_top = LowStageTop(m_displayHeight);
			viewHeight.Set(m_displayHeight); // setLowControlBarConfig: the whole display
		}
		top.Set(Shown());
	}

	// toggleControlBarStage (ButtonLarge): DEFAULT to LOW, anything else to DEFAULT.
	void Toggle() { Switch(m_stage == ControlBarStage::Default ? ControlBarStage::Low : ControlBarStage::Default); }

	// ShowControlBar(immediate): the default stage; not immediately, it slides in from below (registerGameWindow
	// WIN_ANIMATION_SLIDE_BOTTOM, the manager reset first).
	void Show(bool immediate)
	{
		Switch(ControlBarStage::Default);
		// ShowControlBar sets the view's height itself as well: so even during playback, where the switch does nothing.
		viewHeight.Set(DefaultStageViewHeight(m_displayHeight));
		m_slide = BottomSlide{};
		if (!immediate)
		{
			// initAnimateWindow reads the window where it is now (the default place just set) as its rest.
			m_slide = StartBottomSlide(m_top, m_displayWidth);
			m_top = m_slide.currentY;
		}
		top.Set(Shown());
	}

	// HideControlBar (a letterbox coming on) / ToggleControlBar hiding it: the tactical view takes the whole display
	// (TheTacticalView->setHeight(TheDisplay->getHeight()); SLIDE_LETTERBOX is not defined). The stage stays.
	void Hide() { viewHeight.Set(m_displayHeight); }

	// AnimateWindowManager::update once per ControlBar::update: here every 1/30 s of real time.
	void Update(double seconds)
	{
		if (m_slide.finished)
		{
			m_pending = 0.0;
			return;
		}
		m_pending += (std::max)(seconds, 0.0);
		while (m_pending >= FrameSeconds && !m_slide.finished)
		{
			m_pending -= FrameSeconds;
			StepBottomSlide(m_slide);
			m_top = m_slide.currentY; // winSetPosition each update
		}
		top.Set(Shown());
	}

	ControlBarStage Stage() const noexcept { return m_stage; }
	const BottomSlide &Slide() const noexcept { return m_slide; }
	int RestTop() const noexcept { return m_restTop; }

	// The bar's top now, in display pixels (ControlBarParent's position).
	engine::gui::mvvm::Observable<int> top{0};
	// CONTROL_BAR_STAGE_LOW: the minimise button shows the scheme's ToggleButtonUp images, else ToggleButtonDown.
	engine::gui::mvvm::Observable<bool> minimised{false};
	// TheTacticalView's height in display pixels (from the display's top): the 3D view the camera fills and picks in.
	engine::gui::mvvm::Observable<int> viewHeight{600};
	// ButtonLarge: toggleControlBarStage.
	engine::gui::mvvm::Command toggle;

private:
	static constexpr double FrameSeconds = 1.0 / 30.0;

	// ControlBarParent's position: whatever last set it (a stage switch's winSetPosition, or the slide's each update).
	int Shown() const noexcept { return m_top; }

	ControlBarStage m_stage{ControlBarStage::Default};
	BottomSlide m_slide;
	int m_restTop{0}, m_displayWidth{800}, m_displayHeight{600};
	int m_top{0};
	bool m_playback{false};
	double m_pending{0.0};
};
}
