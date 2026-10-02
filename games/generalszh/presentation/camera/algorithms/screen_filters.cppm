export module games.generalszh.presentation.camera.algorithms.screen_filters;
import std;

export import games.generalszh.presentation.camera.resources.view_filter;

// The tactical view's screen filters as drawn (W3DShaderManager's ScreenBWFilter and ScreenMotionBlurFilter postRender):
// the view's picture drawn back over it as quads, each with its corners' texture coordinates into the picture (the view
// spans 0..1), its colour (alpha a blend weight), and how it blends. What drives them (the script commands, the counts
// a logic frame) is in motion_blur_steps; the renderer draws these quads over a copy of the frame.
export namespace generalszh::presentation
{
struct ScreenFilterQuad
{
	// The original's v[0..3]: bottom right, top right, bottom left, top left.
	std::array<std::array<float, 2>, 4> uv{{{1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f}}};
	std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
	bool blend{false};      // source alpha over the frame (else it replaces it)
	bool additive{false};   // blending, the destination kept whole (D3DBLEND_ONE)
	bool blackWhite{false}; // the grey shader: dot(rgb, (0.3, 0.59, 0.11)), blended in by `fade`
	float fade{0.0f};
};

struct ScreenFilterDraw
{
	std::vector<ScreenFilterQuad> quads;
	// ScreenMotionBlurFilter m_skipRender: drawn from the last frame's picture (the scene not drawn into it again).
	bool keepPicture{false};
};

// CAMERA_BW_MODE_BEGIN (ScriptActions::doBlackWhiteMode: setViewFilterMode(FM_VIEW_BW_BLACK_AND_WHITE),
// setViewFilter(FT_VIEW_BW_FILTER), setFadeParameters(frames, 1)): the view's one filter grey, fading in.
inline void StartBlackWhite(ViewFilter &filter, std::int32_t frames) noexcept
{
	filter.motionBlur = false;
	filter.mode = ViewFilterMode::Default;
	filter.blackWhite = true;
	filter.fadeFrames = frames;
	filter.fadeDirection = 1;
	filter.fadeFrame = 0;
}

// CAMERA_BW_MODE_END: fading out (setFadeParameters(frames, -1)), only while the view's filter is the grey one.
inline void EndBlackWhite(ViewFilter &filter, std::int32_t frames) noexcept
{
	if (!filter.blackWhite)
		return;
	filter.fadeFrames = frames;
	filter.fadeDirection = -1;
	filter.fadeFrame = 0;
}

// ScreenBWFilter::set, a frame: fading in, the fade is frame / frames until the frames are out (then 1, settled); fading
// out, 1 - frame / frames, and once out 0 and the view's filter none again (that frame still drawn, at 0). Stepped once a
// drawn frame of the original's (its postRender calls set): StepScreenFilterFrames. Returns whether it is drawn.
inline bool StepBlackWhite(ViewFilter &filter) noexcept
{
	if (!filter.blackWhite)
		return false;
	if (filter.fadeDirection > 0)
	{
		++filter.fadeFrame;
		if (filter.fadeFrame < filter.fadeFrames)
			filter.fadeValue = static_cast<float>(filter.fadeFrame) / static_cast<float>(filter.fadeFrames);
		else
		{
			filter.fadeFrame = 0;
			filter.fadeValue = 1.0f;
			filter.fadeDirection = 0;
		}
	}
	else if (filter.fadeDirection < 0)
	{
		++filter.fadeFrame;
		if (filter.fadeFrame < filter.fadeFrames)
			filter.fadeValue = 1.0f - static_cast<float>(filter.fadeFrame) / static_cast<float>(filter.fadeFrames);
		else
		{
			filter.fadeValue = 0.0f;
			filter.blackWhite = false;
			filter.fadeFrame = 0;
			filter.fadeDirection = 0;
		}
	}
	return true;
}

// The original drew the view (W3DView::draw) once a frame at 30 frames a second; presentation runs on real time, so
// `deltaSeconds` of it is stepped as that many of the original's drawn frames (the rest carried to the next). Each:
// the grey fade a frame on (ScreenBWFilter::set), and a pan blur's end a count down (ScreenMotionBlurFilter::postRender
// FM_VIEW_MB_END_PAN_ALPHA: --m_maxCount; MotionBlurDraw ends it once below 2). A zoom blur's count goes by logic
// frames instead (its m_lastFrame gate: StepMotionBlur). Returns the frames stepped.
inline constexpr float OriginalFrameSeconds = 1.0f / 30.0f;

inline std::int32_t StepScreenFilterFrames(ViewFilter &filter, float deltaSeconds)
{
	filter.frameSeconds += deltaSeconds;
	std::int32_t frames = 0;
	while (filter.frameSeconds >= OriginalFrameSeconds)
	{
		filter.frameSeconds -= OriginalFrameSeconds;
		++frames;
		StepBlackWhite(filter);
		if (filter.motionBlur && filter.mode == ViewFilterMode::EndPanAlpha && filter.maxCount >= 2)
			--filter.maxCount;
	}
	return frames;
}

// ScreenBWFilter::postRender: the picture drawn back over the view through the grey shader at the fade.
inline void BlackWhiteDraw(const ViewFilter &filter, bool drawn, ScreenFilterDraw &out)
{
	if (!drawn)
		return;
	ScreenFilterQuad quad;
	quad.blackWhite = true;
	quad.fade = filter.fadeValue;
	out.quads.push_back(quad);
}

// ScreenMotionBlurFilter::postRender's quads. A zoom: the picture drawn back opaque, its corners pulled toward the
// centre by sqrt(1 - count / MAX_COUNT x 0.9); then up to 30 copies (as many as the count) blended over it, each pulled
// in by 0.99 more (saturate: 0.98) at alpha 0x15 (saturate: added, at 0x09, more by (count - 30) / 5 past 30, and 60
// more at the peak). A pan (`scroll`: the view's scroll this frame): centred half a view up, its count from the scroll
// (len x 200 x panFactor / 30, between half the pan factor and the pan factor), the copies stretched 0.006 more across;
// its end: centred toward the last scroll, the count falling by one a drawn frame (StepScreenFilterFrames), over (that
// frame still drawn) below 2. `scroll`: the view's scroll this frame in normalized screen units (W3DView::calcDeltaScroll:
// the look-at point projected now less where it last moved from). Returns whether the effect goes on. The first drawing
// after a zoom's count moved keeps the last picture (m_skipRender, then cleared).
inline bool MotionBlurDraw(ViewFilter &filter, std::array<float, 2> scroll, ScreenFilterDraw &out)
{
	if (!filter.motionBlur || filter.mode == ViewFilterMode::Default)
		return true;
	bool going = true;
	std::array<float, 2> center{0.5f, 0.5f};
	bool pan = false;
	if (filter.mode == ViewFilterMode::PanAlpha)
	{
		const float length = std::sqrt(scroll[0] * scroll[0] + scroll[1] * scroll[1]);
		center[1] -= 0.5f;
		filter.decrement = false;
		filter.maxCount = static_cast<std::int32_t>(length * 200.0f * static_cast<float>(filter.panFactor) / static_cast<float>(ViewFilter::DefaultPanFactor));
		if (filter.maxCount < filter.panFactor / 2)
			filter.maxCount = filter.panFactor / 2;
		if (filter.maxCount > filter.panFactor)
			filter.maxCount = filter.panFactor;
		pan = true;
		filter.priorScroll = scroll;
	}
	else if (filter.mode == ViewFilterMode::EndPanAlpha)
	{
		const float length = std::sqrt(filter.priorScroll[0] * filter.priorScroll[0] + filter.priorScroll[1] * filter.priorScroll[1]);
		if (length > 0.0f)
		{
			center[0] += 0.5f * (filter.priorScroll[0] / length);
			center[1] -= 0.5f * (filter.priorScroll[1] / length);
		}
		filter.decrement = false;
		if (filter.maxCount < 2)
			going = false;
		pan = true;
	}
	ScreenFilterQuad base;
	base.color = {1.0f, 1.0f, 1.0f, 1.0f};
	if (!pan)
	{
		const float factor = std::sqrt(1.0f - (static_cast<float>(filter.maxCount) / static_cast<float>(ViewFilter::MaxCount)) * 0.90f);
		for (auto &corner : base.uv)
			for (std::size_t axis = 0; axis < 2; ++axis)
				corner[axis] = (corner[axis] - center[axis]) * factor + center[axis];
	}
	out.quads.push_back(base);
	out.keepPicture = !pan && filter.skipRender;
	filter.skipRender = false;
	ScreenFilterQuad copy = base;
	copy.blend = true;
	copy.additive = filter.additive;
	const std::int32_t limit = std::min<std::int32_t>(filter.maxCount, 30);
	for (std::int32_t index = 0; index < limit; ++index)
	{
		const float factor = filter.additive ? 0.98f : 0.99f;
		std::int32_t alpha = 0x15;
		if (filter.additive)
		{
			alpha = 0x09;
			if (filter.maxCount > limit)
				alpha += (filter.maxCount - limit) / 5;
			if (filter.maxCount == ViewFilter::MaxCount)
				alpha += 60;
		}
		copy.color = {1.0f, 1.0f, 1.0f, static_cast<float>(alpha) / 255.0f};
		for (auto &corner : copy.uv)
		{
			corner[0] = (corner[0] - center[0]) * (pan ? factor + 0.006f : factor) + center[0];
			corner[1] = (corner[1] - center[1]) * factor + center[1];
		}
		out.quads.push_back(copy);
	}
	if (!going)
	{
		filter.zoomToValid = false;
		filter.motionBlur = false;
		filter.mode = ViewFilterMode::Default;
	}
	return going;
}
}
