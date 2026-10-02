export module games.generalszh.presentation.hud.algorithms.radar_display;
import std;

export import games.generalszh.presentation.hud.resources.radar_events;

// The radar's drawing (the original's Radar::findDrawPositions / localPixelToRadar and W3DRadar::radarToPixel /
// drawSingleGenericEvent / renderObjectList's stealth blink), in integer pixels as the original computes them: the map
// keeps its aspect inside the radar window (bars filling the rest); radar cells map to pixels with y up; an event's
// triangle shrinks from half the radar's width to 6 pixels over 1.5 s, spinning a full turn, fading after its fade
// frame.
export namespace generalszh::presentation
{
inline constexpr std::int32_t RadarCellCount = 128;

struct RadarPoint
{
	std::int32_t x{0};
	std::int32_t y{0};
	bool operator==(const RadarPoint &) const = default;
};

// Radar::findDrawPositions: the upper left and lower right of the map's picture.
inline std::pair<RadarPoint, RadarPoint> FindDrawPositions(float extentWidth, float extentHeight, std::int32_t startX, std::int32_t startY,
	std::int32_t width, std::int32_t height)
{
	const float ratioWidth = extentWidth / (static_cast<float>(width) * 1.0f);
	const float ratioHeight = extentHeight / (static_cast<float>(height) * 1.0f);
	RadarPoint ul, lr;
	if (ratioWidth >= ratioHeight)
	{
		const float rx = extentWidth / ratioWidth, ry = extentHeight / ratioWidth;
		ul.x = 0;
		ul.y = static_cast<std::int32_t>((static_cast<float>(height) - ry) / 2.0f);
		lr.x = static_cast<std::int32_t>(rx);
		lr.y = height - ul.y;
	}
	else
	{
		const float rx = extentWidth / ratioHeight, ry = extentHeight / ratioHeight;
		ul.x = static_cast<std::int32_t>((static_cast<float>(width) - rx) / 2.0f);
		ul.y = 0;
		lr.x = width - ul.x;
		lr.y = static_cast<std::int32_t>(ry);
	}
	return {{ul.x + startX, ul.y + startY}, {lr.x + startX, lr.y + startY}};
}

// W3DRadar::radarToPixel (y up).
inline RadarPoint RadarToPixel(RadarPoint radar, std::int32_t ulX, std::int32_t ulY, std::int32_t width, std::int32_t height)
{
	return {radar.x * width / RadarCellCount + ulX, (RadarCellCount - 1 - radar.y) * height / RadarCellCount + ulY};
}

// Radar::localPixelToRadar: a pixel within the radar window (its size) to a cell; none outside the map's picture.
inline std::optional<RadarPoint> LocalPixelToRadar(RadarPoint pixel, float extentWidth, float extentHeight, std::int32_t width, std::int32_t height)
{
	const auto [ul, lr] = FindDrawPositions(extentWidth, extentHeight, 0, 0, width, height);
	const std::int32_t scaledWidth = lr.x - ul.x, scaledHeight = lr.y - ul.y;
	if (pixel.x < ul.x || pixel.x > lr.x || pixel.y < ul.y || pixel.y > lr.y || scaledWidth <= 0 || scaledHeight <= 0 || width <= 0 || height <= 0)
		return std::nullopt;
	RadarPoint radar;
	if (scaledWidth >= scaledHeight)
	{
		radar.x = (pixel.x - ul.x) * RadarCellCount / scaledWidth;
		radar.y = static_cast<std::int32_t>((static_cast<float>(pixel.y - ul.y) / static_cast<float>(scaledHeight)) * static_cast<float>(height));
		radar.y = (height - radar.y) * RadarCellCount / height;
	}
	else
	{
		radar.x = static_cast<std::int32_t>((static_cast<float>(pixel.x - ul.x) / static_cast<float>(scaledWidth)) * static_cast<float>(width));
		radar.x = radar.x * RadarCellCount / width;
		radar.y = (height - pixel.y) * RadarCellCount / height;
	}
	return radar;
}

struct RadarTriangle
{
	std::array<RadarPoint, 3> points{}; // screen pixels
	std::array<std::uint8_t, 4> startColor{};
	std::array<std::uint8_t, 4> endColor{};
};

// W3DRadar::drawSingleGenericEvent: `cell` the event's radar cell; `width` the radar picture's width.
inline RadarTriangle EventTriangle(const RadarEvent &event, RadarPoint cell, std::uint64_t frame, std::int32_t ulX, std::int32_t ulY, std::int32_t width,
	std::int32_t height)
{
	constexpr float pi = 3.14159265358979323846f;
	const float maxSize = static_cast<float>(width) / 2.0f;
	constexpr std::int32_t minSize = 6;
	constexpr float fullToSmall = 30.0f * 1.5f;
	const auto frameDiff = static_cast<std::uint32_t>(frame - event.createFrame);
	std::int32_t size = static_cast<std::int32_t>(maxSize * (1.0f - static_cast<float>(frameDiff) / fullToSmall));
	if (size < minSize)
		size = minSize;
	const float addAngle = 2.0f * pi * (static_cast<float>(frameDiff) / fullToSmall);
	RadarTriangle triangle;
	const float angles[3] = {0.0f - addAngle, 2.0f * pi / 3.0f - addAngle, -2.0f * pi / 3.0f - addAngle};
	for (std::size_t corner = 0; corner < 3; ++corner)
	{
		const RadarPoint point{static_cast<std::int32_t>(std::cos(angles[corner]) * static_cast<float>(size) + static_cast<float>(cell.x)),
			static_cast<std::int32_t>(std::sin(angles[corner]) * static_cast<float>(size) + static_cast<float>(cell.y))};
		triangle.points[corner] = RadarToPixel(point, ulX, ulY, width, height);
	}
	const auto fade = [&](std::array<std::uint8_t, 4> color) {
		if (frame > event.fadeFrame && event.dieFrame > event.fadeFrame)
			color[3] = static_cast<std::uint8_t>(static_cast<float>(color[3]) *
				(1.0f - static_cast<float>(frame - event.fadeFrame) / static_cast<float>(event.dieFrame - event.fadeFrame)));
		return color;
	};
	triangle.startColor = fade(event.color1);
	triangle.endColor = fade(event.color2);
	return triangle;
}

// renderObjectList: a stealthed object's blip blinks over a second (the retail formula, wrapping in its first half).
inline std::uint8_t StealthBlinkAlpha(std::uint64_t frame)
{
	constexpr std::uint32_t framesForTransition = 30;
	constexpr float minAlpha = 32.0f;
	const float alphaScale = static_cast<float>(frame % framesForTransition) / (static_cast<float>(framesForTransition) / 2.0f);
	// REAL_TO_UNSIGNEDBYTE: a plain cast (through an int on the original's compiler: the first half's negative
	// values wrap round).
	const float alpha = alphaScale > 0.0f ? ((alphaScale - 1.0f) * (255.0f - minAlpha)) + minAlpha : (alphaScale * (255.0f - minAlpha)) + minAlpha;
	return static_cast<std::uint8_t>(static_cast<std::int32_t>(alpha));
}
}
