export module games.generalszh.presentation.camera.resources.view_filter;
import std;

import engine.ecs.system.system;

// The tactical view's screen filter as scripts set it (W3DView m_viewFilter / m_viewFilterMode with the original's
// ScreenMotionBlurFilter state): the motion blur's modes, its count (0 to MAX_COUNT, in COUNT_STEP steps once a logic
// frame), whether it is falling, and the point a zoom in-and-out jumps the camera to at its peak.
export namespace generalszh::presentation
{
// FilterModes' motion blur entries (FM_VIEW_MB_*), and FM_VIEW_DEFAULT.
enum class ViewFilterMode : std::uint8_t
{
	Default,
	InAndOutAlpha,
	InAndOutSaturate,
	InAlpha,
	OutAlpha,
	InSaturate,
	OutSaturate,
	EndPanAlpha,
	PanAlpha, // FM_VIEW_MB_PAN_ALPHA + `panFactor`
};

struct ViewFilter
{
	static constexpr std::int32_t MaxCount = 60;       // ScreenMotionBlurFilter MAX_COUNT
	static constexpr std::int32_t CountStep = 5;       // COUNT_STEP
	static constexpr std::int32_t DefaultPanFactor = 30; // DEFAULT_PAN_FACTOR

	bool motionBlur{false}; // FT_VIEW_MOTION_BLUR_FILTER (else FT_VIEW_DEFAULT)
	ViewFilterMode mode{ViewFilterMode::Default};
	std::int32_t panFactor{DefaultPanFactor};
	std::int32_t maxCount{0};
	bool decrement{false};
	bool additive{false};
	bool doZoomTo{false};
	bool zoomToValid{false};
	std::array<float, 3> zoomTo{};
	std::uint64_t lastTick{~std::uint64_t{0}};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::ViewFilter>
{
	static constexpr std::string_view StableName = "generalszh.presentation.view_filter";
};
}
