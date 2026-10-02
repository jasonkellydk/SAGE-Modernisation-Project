export module games.generalszh.presentation.camera.algorithms.motion_blur_steps;
import std;

export import games.generalszh.presentation.camera.resources.view_filter;

// The view's motion blur as the scripts drive it (ScriptActions::doCameraMotionBlur, doCameraMotionBlurJump,
// CAMERA_MOTION_BLUR_FOLLOW / END_FOLLOW; W3DView::setViewFilter / setViewFilterMode; ScreenMotionBlurFilter::setup and
// the stepping in its postRender), as free functions over the ViewFilter resource. What the blur looks like is the
// renderer's; this is its timing: the count rises (or falls) by COUNT_STEP once a logic frame, a zoom in-and-out jumps
// the camera to its point at the peak and falls again, and the effect ends when the count is out.
export namespace generalszh::presentation
{
inline bool PanMode(ViewFilterMode mode) noexcept { return mode == ViewFilterMode::PanAlpha || mode == ViewFilterMode::EndPanAlpha; }

// ScreenMotionBlurFilter::setup(mode).
inline void SetUpMotionBlur(ViewFilter &filter, ViewFilterMode mode, std::int32_t panFactor)
{
	filter.additive = mode == ViewFilterMode::InAndOutSaturate || mode == ViewFilterMode::InSaturate || mode == ViewFilterMode::OutSaturate;
	filter.doZoomTo = mode == ViewFilterMode::InAndOutSaturate || mode == ViewFilterMode::InAndOutAlpha;
	if (mode == ViewFilterMode::PanAlpha)
		filter.panFactor = panFactor < 1 ? ViewFilter::DefaultPanFactor : panFactor;
	if (mode != ViewFilterMode::EndPanAlpha)
		filter.maxCount = 0;
	filter.decrement = false;
	if (mode == ViewFilterMode::OutAlpha || mode == ViewFilterMode::OutSaturate)
	{
		filter.maxCount = ViewFilter::MaxCount;
		filter.decrement = true;
	}
}

// W3DView::setViewFilter(FT_VIEW_MOTION_BLUR_FILTER): set up for the mode the view has.
inline void SetMotionBlurFilter(ViewFilter &filter)
{
	filter.motionBlur = true;
	filter.blackWhite = false; // the view has one filter
	SetUpMotionBlur(filter, filter.mode, filter.panFactor);
}

// W3DView::setViewFilterMode(mode) (`panFactor`: a pan's FM_VIEW_MB_PAN_ALPHA offset); the default filter keeps it as is.
inline void SetViewFilterMode(ViewFilter &filter, ViewFilterMode mode, std::int32_t panFactor = 0)
{
	filter.mode = mode;
	if (filter.motionBlur)
		SetUpMotionBlur(filter, mode, panFactor);
}

// doCameraMotionBlur(zoom in, saturate).
inline void StartMotionBlur(ViewFilter &filter, bool zoomIn, bool saturate)
{
	SetMotionBlurFilter(filter);
	SetViewFilterMode(filter, saturate ? (zoomIn ? ViewFilterMode::InSaturate : ViewFilterMode::OutSaturate)
									  : (zoomIn ? ViewFilterMode::InAlpha : ViewFilterMode::OutAlpha));
}

// doCameraMotionBlurJump(waypoint, saturate): in and out, jumping to `at` at the peak (setViewFilterPos).
inline void StartMotionBlurJump(ViewFilter &filter, std::array<float, 3> at, bool saturate)
{
	SetMotionBlurFilter(filter);
	SetViewFilterMode(filter, saturate ? ViewFilterMode::InAndOutSaturate : ViewFilterMode::InAndOutAlpha);
	filter.zoomTo = at;
	filter.zoomToValid = true;
}

// CAMERA_MOTION_BLUR_FOLLOW(amount): setViewFilterMode(FM_VIEW_MB_PAN_ALPHA + amount), then the motion blur filter.
inline void StartMotionBlurFollow(ViewFilter &filter, std::int32_t amount)
{
	SetViewFilterMode(filter, ViewFilterMode::PanAlpha, amount);
	filter.panFactor = amount < 1 ? ViewFilter::DefaultPanFactor : amount;
	SetMotionBlurFilter(filter);
}

// CAMERA_MOTION_BLUR_END_FOLLOW: setViewFilterMode(FM_VIEW_MB_END_PAN_ALPHA), then the motion blur filter.
inline void EndMotionBlurFollow(ViewFilter &filter)
{
	SetViewFilterMode(filter, ViewFilterMode::EndPanAlpha);
	SetMotionBlurFilter(filter);
}

// W3DView::isCameraMovementFinished: a zoom blur counts as a finished camera movement.
inline bool MotionBlurZooming(const ViewFilter &filter) noexcept
{
	return filter.motionBlur && filter.mode != ViewFilterMode::Default && !PanMode(filter.mode);
}

struct MotionBlurStep
{
	std::optional<std::array<float, 3>> jumpTo; // the camera looks at this now (TheTacticalView->lookAt)
	bool ended{false};                          // the view's filter back to the default
};

// ScreenMotionBlurFilter::postRender's count, once a logic frame (`tick`) of a zoom blur: rising by COUNT_STEP to
// MAX_COUNT, where it turns to fall and a zoom in-and-out jumps the camera (else it ends); falling, it ends below 1. A
// pan's blur follows the screen's scroll as it is drawn, which is the renderer's.
inline MotionBlurStep StepMotionBlur(ViewFilter &filter, std::uint64_t tick)
{
	MotionBlurStep step;
	if (filter.lastTick == tick)
		return step;
	filter.skipRender = false;
	if (!MotionBlurZooming(filter))
		return step;
	bool going = true;
	if (filter.decrement)
	{
		filter.maxCount -= ViewFilter::CountStep;
		if (filter.maxCount < 1)
		{
			filter.decrement = false;
			going = false;
		}
		else
			filter.skipRender = true;
	}
	else
	{
		filter.maxCount += ViewFilter::CountStep;
		if (filter.maxCount >= ViewFilter::MaxCount)
		{
			filter.decrement = true;
			if (filter.doZoomTo && filter.zoomToValid)
				step.jumpTo = filter.zoomTo;
			else
				going = false;
		}
		else
			filter.skipRender = true;
	}
	filter.lastTick = tick;
	if (!going)
	{
		filter.zoomToValid = false;
		// W3DView::draw: the effect over, the view's filter and mode are the defaults again.
		filter.motionBlur = false;
		filter.mode = ViewFilterMode::Default;
		step.ended = true;
	}
	return step;
}
}
