export module games.generalszh.presentation.effects.snow;
import std;

import engine.ecs.system.system;
export import games.generalszh.presentation.effects.weather_settings;

// The map's snow (SnowManager, Snow.cpp, and W3DSnowManager, W3DSnowManager.cpp of the original): a box of flakes
// around the camera, one emitter a grid step (1 / SnowBoxDensity), each falling from the top of the box at
// SnowVelocity from its own starting height (a 64 x 64 table of random heights tiled over the grid) and swaying by
// SnowAmplitude on a sine of its height. Nothing of it is kept per flake: the field (the resource) holds the settings,
// the table and the clock; each frame's flakes are drawn from it (ExtractSnow), as W3DSnowManager::render computes them.
export namespace generalszh::presentation
{
// SnowManager's SNOW_NOISE_X / SNOW_NOISE_Y.
inline constexpr std::int32_t SnowNoiseX = 64;
inline constexpr std::int32_t SnowNoiseY = 64;
// W3DSnow.cpp: MAXIMUM_CAMERA_DISTANCE (added to a grid coordinate to keep it positive before the table's wrap).
inline constexpr std::int32_t SnowMaximumCameraDistance = 100000;
// W3DSnowManager::render: m_leafDim, the largest box of emitters (on a side) culled as one.
inline constexpr std::int32_t SnowLeafDimension = 45;

// The C runtime's rand() (the original's starting heights draw from it): the MSVC linear congruential generator, its
// state seeded 1 as the runtime's.
struct SnowRandom
{
	std::uint32_t state{1};

	std::int32_t Next() noexcept
	{
		state = state * 214013u + 2531011u;
		return static_cast<std::int32_t>((state >> 16) & 0x7FFFu);
	}
};

// The snow (SnowManager's members, filled by updateIniSettings, and its clock).
struct SnowField
{
	bool enabled{false};      // WeatherSetting m_snowEnabled
	bool pointSprites{true};  // WeatherSetting m_usePointSprites
	std::string texture{"EXSnowFlake.tga"};
	float velocity{1.0f};
	float frequencyScaleX{0.0f};
	float frequencyScaleY{0.0f};
	float amplitude{0.0f};
	float pointSize{0.0f};
	float maxPointSize{0.0f};
	float minPointSize{0.0f};
	float quadSize{0.0f};
	float boxDimensions{0.0f};
	float emitterSpacing{1.0f};
	float fullTimePeriod{0.0f}; // the time a flake takes from the top of the box to its bottom
	float time{0.0f};           // m_time: seconds into the current fall, kept within fullTimePeriod
	std::array<float, SnowNoiseX * SnowNoiseY> startingHeights{};
	SnowRandom random;
};

// SnowManager::updateIniSettings: a new table of starting heights (rand() % (Int)SnowBoxDimensions, row by row) and
// the setting's values; the full period is the box over the fall speed.
inline void ApplyWeatherSetting(SnowField &snow, const WeatherSetting &setting) noexcept
{
	const std::int32_t boxDimensions = static_cast<std::int32_t>(setting.snowBoxDimensions);
	for (float &height : snow.startingHeights)
		height = boxDimensions > 0 ? static_cast<float>(snow.random.Next() % boxDimensions) : 0.0f;
	snow.enabled = setting.snowEnabled;
	snow.pointSprites = setting.usePointSprites;
	snow.texture = setting.snowTexture;
	snow.velocity = setting.snowVelocity;
	snow.frequencyScaleX = setting.snowFrequencyScaleX;
	snow.frequencyScaleY = setting.snowFrequencyScaleY;
	snow.amplitude = setting.snowAmplitude;
	snow.pointSize = setting.snowPointSize;
	snow.quadSize = setting.snowQuadSize;
	snow.boxDimensions = setting.snowBoxDimensions;
	snow.emitterSpacing = 1.0f / setting.snowBoxDensity;
	snow.maxPointSize = setting.snowMaxPointSize;
	snow.minPointSize = setting.snowMinPointSize;
	snow.fullTimePeriod = snow.boxDimensions / snow.velocity;
}

// W3DSnowManager::update: the clock runs on by the frame's time (WW3D's frame time: a logic frame's length a drawn
// frame, none while time is frozen or the game paused: the frame's game time here), kept within one full period.
inline void AdvanceSnow(SnowField &snow, float seconds) noexcept
{
	snow.time += seconds;
	if (snow.fullTimePeriod != 0.0f)
		snow.time = std::fmod(snow.time, snow.fullTimePeriod);
}

// WWMath::Fast_Sin: a 1024-entry table of sin(i * 2 pi / 1024) (WWMath::Init), read between its two nearest entries.
inline float SnowFastSin(float value) noexcept
{
	constexpr std::int32_t TableSize = 1024;
	constexpr float Pi = 3.141592654f; // WWMATH_PI
	static const std::array<float, TableSize> table = [] {
		std::array<float, TableSize> sines{};
		for (std::int32_t index = 0; index < TableSize; ++index)
		{
			const float angle = static_cast<float>(index) * 2.0f * Pi / static_cast<float>(TableSize);
			sines[static_cast<std::size_t>(index)] = static_cast<float>(std::sin(static_cast<double>(angle)));
		}
		return sines;
	}();
	value *= static_cast<float>(TableSize) / (2.0f * Pi);
	const std::int32_t first = static_cast<std::int32_t>(std::floor(value));
	const float fraction = value - static_cast<float>(first);
	const auto wrap = [](std::int32_t index) { return static_cast<std::size_t>(static_cast<std::uint32_t>(index) & (TableSize - 1)); };
	return (1.0f - fraction) * table[wrap(first)] + fraction * table[wrap(first + 1)];
}

// A plane of the view's frustum, its normal facing in: a point is inside when normal . point + distance >= 0.
struct SnowPlane
{
	std::array<float, 3> normal{};
	float distance{0.0f};
};

// What the snow is drawn for this frame: the camera (where it is, its right and up axes, its frustum's planes and
// corners: 0-3 on the near plane, 4-7 the far plane's matching ones), the viewport's height in pixels (point sprites
// scale by it) and the terrain's lowest height (BaseHeightMapRenderObjClass m_minHeight).
struct SnowView
{
	std::array<float, 3> eye{};
	std::array<float, 3> right{1.0f, 0.0f, 0.0f};
	std::array<float, 3> up{0.0f, 1.0f, 0.0f};
	std::array<SnowPlane, 6> planes{};
	std::array<std::array<float, 3>, 8> corners{};
	float viewportHeight{0.0f};
	float terrainMinHeight{0.0f};
};

// A camera's view for the snow from its frame: eye, right, up and back (view space +X, +Y, +Z: it looks down -Z),
// the tangents of its half fields of view across and up, and its near and far clip distances.
inline SnowView MakeSnowView(const std::array<float, 3> &eye, const std::array<float, 3> &right, const std::array<float, 3> &up,
	const std::array<float, 3> &back, float tanHalfX, float tanHalfY, float nearClip, float farClip, float viewportHeight, float terrainMinHeight) noexcept
{
	SnowView view;
	view.eye = eye;
	view.right = right;
	view.up = up;
	view.viewportHeight = viewportHeight;
	view.terrainMinHeight = terrainMinHeight;
	const auto at = [&](float x, float y, float depth) {
		std::array<float, 3> point{};
		for (std::size_t axis = 0; axis < 3; ++axis)
			point[axis] = eye[axis] + right[axis] * x * depth + up[axis] * y * depth - back[axis] * depth;
		return point;
	};
	constexpr std::array<std::array<float, 2>, 4> signs{{{-1.0f, 1.0f}, {1.0f, 1.0f}, {-1.0f, -1.0f}, {1.0f, -1.0f}}};
	for (std::size_t corner = 0; corner < 4; ++corner)
	{
		view.corners[corner] = at(signs[corner][0] * tanHalfX, signs[corner][1] * tanHalfY, nearClip);
		view.corners[corner + 4] = at(signs[corner][0] * tanHalfX, signs[corner][1] * tanHalfY, farClip);
	}
	const auto cross = [](const std::array<float, 3> &a, const std::array<float, 3> &b) {
		return std::array<float, 3>{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
	};
	const auto plane = [](std::array<float, 3> normal, const std::array<float, 3> &through) {
		const float length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
		if (length > 0.0f)
			for (float &axis : normal)
				axis /= length;
		return SnowPlane{normal, -(normal[0] * through[0] + normal[1] * through[1] + normal[2] * through[2])};
	};
	std::array<float, 3> forward{-back[0], -back[1], -back[2]};
	// The side planes through the eye and two far corners each, facing in.
	const auto direction = [&](std::size_t corner) {
		return std::array<float, 3>{view.corners[corner + 4][0] - eye[0], view.corners[corner + 4][1] - eye[1], view.corners[corner + 4][2] - eye[2]};
	};
	view.planes[0] = plane(cross(direction(2), direction(0)), eye); // left: bottom-left x top-left
	view.planes[1] = plane(cross(direction(1), direction(3)), eye); // right
	view.planes[2] = plane(cross(direction(3), direction(2)), eye); // bottom
	view.planes[3] = plane(cross(direction(0), direction(1)), eye); // top
	view.planes[4] = plane(forward, view.corners[0]);               // near
	view.planes[5] = plane(back, view.corners[4]);                  // far
	return view;
}

// The visible box (BaseHeightMapRenderObjClass::getMaximumVisibleBox with ignoreMaxHeight): each edge from a near
// corner to its far corner cut where it crosses the terrain's lowest height (PlaneClass::Compute_Intersection: only a
// crossing within the edge), the box around the near corners and the cut far ones, as centre and half extent.
struct SnowBox
{
	std::array<float, 3> center{};
	std::array<float, 3> extent{};
};

inline SnowBox MaximumVisibleBox(const SnowView &view) noexcept
{
	std::array<std::array<float, 3>, 8> clipped = view.corners;
	for (std::size_t corner = 0; corner < 4; ++corner)
	{
		const auto &from = view.corners[corner];
		const auto &to = view.corners[corner + 4];
		const float denominator = to[2] - from[2];
		if (denominator == 0.0f)
			continue;
		const float fraction = -(from[2] - view.terrainMinHeight) / denominator;
		if (fraction < 0.0f || fraction > 1.0f)
			continue;
		for (std::size_t axis = 0; axis < 3; ++axis)
			clipped[corner + 4][axis] = from[axis] + (to[axis] - from[axis]) * fraction;
	}
	std::array<float, 3> low = clipped[0];
	std::array<float, 3> high = clipped[0];
	for (const auto &point : clipped)
		for (std::size_t axis = 0; axis < 3; ++axis)
		{
			low[axis] = std::min(low[axis], point[axis]);
			high[axis] = std::max(high[axis], point[axis]);
		}
	SnowBox box;
	for (std::size_t axis = 0; axis < 3; ++axis)
	{
		box.center[axis] = (low[axis] + high[axis]) * 0.5f;
		box.extent[axis] = (high[axis] - low[axis]) * 0.5f;
	}
	return box;
}

// This frame's flakes as drawn, a row each: where (world), and its size (point sprites: pixels across; quads: half
// their world width, a billboard's half extent). Kept as columns.
struct SnowFlakes
{
	std::vector<float> x;
	std::vector<float> y;
	std::vector<float> z;
	std::vector<float> size;
	bool pointSprites{true};
	std::string texture;

	std::size_t Size() const noexcept { return x.size(); }
	void Clear() noexcept
	{
		x.clear();
		y.clear();
		z.clear();
		size.clear();
	}
};

namespace snow_detail
{
// What render() works out for the frame and renderSubBox reads.
struct Frame
{
	float ceiling{0.0f};        // m_snowCeiling
	float heightTraveled{0.0f}; // m_heightTraveled
	float cullOverscan{0.0f};   // m_cullOverscan
};

// CollisionMath::Overlap_Test(frustum, box) != OUTSIDE: the box is not wholly outside any of the frustum's planes.
inline bool Overlaps(const SnowView &view, const std::array<float, 3> &low, const std::array<float, 3> &high) noexcept
{
	for (const SnowPlane &plane : view.planes)
	{
		float reach = plane.distance;
		for (std::size_t axis = 0; axis < 3; ++axis)
			reach += plane.normal[axis] * (plane.normal[axis] >= 0.0f ? high[axis] : low[axis]);
		if (reach < 0.0f)
			return false;
	}
	return true;
}

// A flake's height this frame: down from the ceiling by how far it has fallen from its starting height, within the box.
inline float Height(const SnowField &snow, const Frame &frame, std::int32_t x, std::int32_t y) noexcept
{
	std::int32_t offset = ((x + SnowMaximumCameraDistance) & (SnowNoiseX - 1)) + ((y + SnowMaximumCameraDistance) & (SnowNoiseY - 1)) * SnowNoiseX;
	if (offset > SnowNoiseX * SnowNoiseY)
		offset = 0;
	return frame.ceiling - std::fmod(frame.heightTraveled + snow.startingHeights[static_cast<std::size_t>(offset)], snow.boxDimensions);
}

// D3D's point sprite size with D3DRS_POINTSCALEENABLE (scale A = 0, B = 0, C = 1): the viewport's height times the
// size over the distance from the eye, within D3DRS_POINTSIZE_MIN and _MAX.
inline float PointPixels(const SnowField &snow, const SnowView &view, float x, float y, float z) noexcept
{
	const float dx = x - view.eye[0];
	const float dy = y - view.eye[1];
	const float dz = z - view.eye[2];
	const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
	const float pixels = distance > 0.0f ? view.viewportHeight * snow.pointSize / distance : snow.maxPointSize;
	return std::clamp(pixels, snow.minPointSize, std::max(snow.minPointSize, snow.maxPointSize));
}

// renderSubBox's leaf: every emitter of the box, row by row, at its height, swayed in world x and y.
inline void EmitPoints(const SnowField &snow, const SnowView &view, const Frame &frame, std::int32_t originX, std::int32_t originY, std::int32_t endX,
	std::int32_t endY, SnowFlakes &out)
{
	for (std::int32_t y = originY; y < endY; ++y)
		for (std::int32_t x = originX; x < endX; ++x)
		{
			const float height = Height(snow, frame, x, y);
			const float px = static_cast<float>(x) * snow.emitterSpacing + snow.amplitude * SnowFastSin(height * snow.frequencyScaleX + static_cast<float>(x));
			const float py = static_cast<float>(y) * snow.emitterSpacing + snow.amplitude * SnowFastSin(height * snow.frequencyScaleY + static_cast<float>(y));
			out.x.push_back(px);
			out.y.push_back(py);
			out.z.push_back(height);
			out.size.push_back(PointPixels(snow, view, px, py, height));
		}
}

// W3DSnowManager::renderSubBox: a box larger than a leaf on a side is halved on that side (or both), each half that
// the frustum does not wholly leave (its extent grown by the overscan, from the box's floor to its ceiling) taken in
// turn (upper left, upper right, lower left, lower right; left, right; top, bottom); a leaf is drawn.
inline void SubBox(const SnowField &snow, const SnowView &view, const Frame &frame, std::int32_t originX, std::int32_t originY, std::int32_t endX,
	std::int32_t endY, SnowFlakes &out)
{
	const std::int32_t dimensionX = endX - originX;
	const std::int32_t dimensionY = endY - originY;
	const std::int32_t halfX = static_cast<std::int32_t>(std::ceil(static_cast<float>(dimensionX) * 0.5f));
	const std::int32_t halfY = static_cast<std::int32_t>(std::ceil(static_cast<float>(dimensionY) * 0.5f));
	const float spacing = snow.emitterSpacing;
	const float overscan = frame.cullOverscan;
	const float bottom = frame.ceiling - snow.boxDimensions;
	const auto visit = [&](std::int32_t fromX, std::int32_t fromY, std::int32_t toX, std::int32_t toY) {
		const std::array<float, 3> low{static_cast<float>(fromX) * spacing - overscan, static_cast<float>(fromY) * spacing - overscan, bottom};
		const std::array<float, 3> high{static_cast<float>(toX) * spacing + overscan, static_cast<float>(toY) * spacing + overscan, frame.ceiling};
		if (Overlaps(view, low, high))
			SubBox(snow, view, frame, fromX, fromY, toX, toY, out);
	};
	if (dimensionX > SnowLeafDimension)
	{
		if (dimensionY > SnowLeafDimension)
		{
			visit(originX, originY + halfY, originX + halfX, endY);
			visit(originX + halfX, originY + halfY, endX, endY);
			visit(originX, originY, originX + halfX, originY + halfY);
			visit(originX + halfX, originY, endX, originY + halfY);
			return;
		}
		visit(originX, originY, originX + halfX, endY);
		visit(originX + halfX, originY, endX, endY);
		return;
	}
	if (dimensionY > SnowLeafDimension)
	{
		visit(originX, originY + halfY, endX, endY);
		visit(originX, originY, endX, originY + halfY);
		return;
	}
	if (dimensionX * dimensionY == 0)
		return;
	EmitPoints(snow, view, frame, originX, originY, endX, endY, out);
}

// W3DSnowManager::renderAsQuads (no point sprites): every emitter of the whole box, row by row, its sway along the
// camera's right and up (added in view space), a quad SnowQuadSize across.
inline void EmitQuads(const SnowField &snow, const SnowView &view, const Frame &frame, std::int32_t originX, std::int32_t originY, std::int32_t endX,
	std::int32_t endY, SnowFlakes &out)
{
	for (std::int32_t y = originY; y < endY; ++y)
		for (std::int32_t x = originX; x < endX; ++x)
		{
			const float height = Height(snow, frame, x, y);
			const float across = snow.amplitude * SnowFastSin(height * snow.frequencyScaleX + static_cast<float>(x));
			const float upward = snow.amplitude * SnowFastSin(height * snow.frequencyScaleY + static_cast<float>(y));
			const std::array<float, 3> center{static_cast<float>(x) * snow.emitterSpacing, static_cast<float>(y) * snow.emitterSpacing, height};
			out.x.push_back(center[0] + view.right[0] * across + view.up[0] * upward);
			out.y.push_back(center[1] + view.right[1] * across + view.up[1] * upward);
			out.z.push_back(center[2] + view.right[2] * across + view.up[2] * upward);
			out.size.push_back(0.5f * snow.quadSize);
		}
}
}

// W3DSnowManager::render: nothing unless the snow is on and shown (SHOW_WEATHER); the emitters of the box around the
// camera (half its dimension in emitters either side of the camera's cell), clipped to the visible box grown by the
// sway and the quad size; the ceiling half a box above the camera and the fall so far its clock times its speed plus
// the camera's height within a box; then the flakes (point sprites by frustum-culled sub-boxes, else every one as a quad).
inline void ExtractSnow(const SnowField &snow, bool shown, const SnowView &view, SnowFlakes &out)
{
	out.Clear();
	out.pointSprites = snow.pointSprites;
	out.texture = snow.texture;
	if (!snow.enabled || !shown || snow.boxDimensions <= 0.0f || snow.emitterSpacing <= 0.0f)
		return;
	const float spacing = snow.emitterSpacing;
	const std::int32_t emittersInHalf = static_cast<std::int32_t>(std::floor(snow.boxDimensions / spacing * 0.5f));
	const std::int32_t centerX = static_cast<std::int32_t>(std::floor(view.eye[0] / spacing));
	const std::int32_t centerY = static_cast<std::int32_t>(std::floor(view.eye[1] / spacing));
	std::int32_t originX = centerX - emittersInHalf;
	std::int32_t originY = centerY - emittersInHalf;
	std::int32_t endX = centerX + emittersInHalf;
	std::int32_t endY = centerY + emittersInHalf;

	SnowBox box = MaximumVisibleBox(view);
	box.extent[0] += snow.amplitude + snow.quadSize;
	box.extent[1] += snow.amplitude + snow.quadSize;
	if (static_cast<float>(originX) * spacing < box.center[0] - box.extent[0])
		originX = static_cast<std::int32_t>(std::floor((box.center[0] - box.extent[0]) / spacing));
	if (static_cast<float>(originY) * spacing < box.center[1] - box.extent[1])
		originY = static_cast<std::int32_t>(std::floor((box.center[1] - box.extent[1]) / spacing));
	if (static_cast<float>(endX) * spacing > box.center[0] + box.extent[0])
		endX = static_cast<std::int32_t>(std::floor((box.center[0] + box.extent[0]) / spacing));
	if (static_cast<float>(endY) * spacing > box.center[1] + box.extent[1])
		endY = static_cast<std::int32_t>(std::floor((box.center[1] + box.extent[1]) / spacing));
	if (endY - originY < 0 || endX - originX < 0)
		return;
	if ((endY - originY) * (endX - originX) <= 0)
		return;

	snow_detail::Frame frame;
	frame.ceiling = view.eye[2] + snow.boxDimensions / 2.0f;
	frame.heightTraveled = snow.time * snow.velocity + std::fmod(view.eye[2], snow.boxDimensions);
	frame.cullOverscan = snow.amplitude + snow.quadSize;
	const std::size_t most = static_cast<std::size_t>(endY - originY) * static_cast<std::size_t>(endX - originX);
	for (auto *column : {&out.x, &out.y, &out.z, &out.size})
		column->reserve(most);
	if (snow.pointSprites)
		snow_detail::SubBox(snow, view, frame, originX, originY, endX, endY, out);
	else
		snow_detail::EmitQuads(snow, view, frame, originX, originY, endX, endY, out);
}
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::SnowField>
{
	static constexpr std::string_view StableName = "generalszh.presentation.snow_field";
};
}
