export module games.generalszh.presentation.hud.algorithms.power_bar;
import std;

// Where the control bar's power bar draws (W3DPowerDraw): a strip of green, yellow or red (PowerPointG/Y/R) from the
// window's left as long as log to PowerBarBase of the power made, PowerBarIntervals of those filling the window; and
// the slider (PowerBarSlider) centred where the power used falls on the same scale, kept inside the window. Yellow
// while the power used is within PowerBarYellowRange under the power made, red over it. Nothing without power made.
// In the window's own pixels, truncated where the original truncates to whole pixels.
export namespace generalszh::presentation
{
enum class PowerBarColor : std::uint8_t
{
	Green,
	Yellow,
	Red
};

struct PowerBarLayout
{
	bool shown{false};
	PowerBarColor color{PowerBarColor::Green};
	int range{0};       // the strip's length from the window's left
	int needleLeft{0};  // the slider's left and right, from the window's left
	int needleRight{0};
};

struct PowerBarSettings
{
	int base{7};
	float intervals{3.0f};
	int yellowRange{5};
};

// logN: log10(value) / log10(base), as the original.
inline float PowerBarLog(float value, float base)
{
	return static_cast<float>(std::log10(value)) / static_cast<float>(std::log10(base));
}

inline PowerBarLayout LayOutPowerBar(int production, int consumption, int width, int sliderWidth, const PowerBarSettings &settings)
{
	PowerBarLayout layout;
	if (consumption > production - settings.yellowRange && consumption <= production)
		layout.color = PowerBarColor::Yellow;
	else if (consumption > production)
		layout.color = PowerBarColor::Red;
	if (production <= 0 || settings.intervals <= 0.0f)
		return layout;
	layout.shown = true;
	const float interval = static_cast<float>(width) / settings.intervals;
	const float base = static_cast<float>(settings.base);
	layout.range = std::min(width, static_cast<int>(PowerBarLog(static_cast<float>(production), base) * interval));
	const float used = consumption == 1 ? 1.5f : static_cast<float>(consumption);
	const int needle = used > 0.0f ? static_cast<int>(PowerBarLog(used, base) * interval) : 0;
	layout.needleLeft = needle - sliderWidth / 2;
	layout.needleRight = layout.needleLeft + sliderWidth;
	if (needle >= width)
	{
		layout.needleLeft = width - sliderWidth;
		layout.needleRight = width;
	}
	if (layout.needleLeft <= 0)
	{
		layout.needleLeft = 0;
		layout.needleRight = sliderWidth;
	}
	return layout;
}
}
