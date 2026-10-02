export module engine.level.presentation.radar_terrain;
import std;

export import engine.level.model.level;

// The radar's picture of the terrain (the original's Radar::newMap and W3DRadar::buildTerrainTexture): the map's
// extent (its first playable area, its lowest and highest heights over the whole height map) sampled at 128 x 128
// cells. The middle height is the average ground height (not under water) of every second cell both ways. Each cell is
// the average of it and its neighbours: on land each sample's tile colour (the tile mipped to one pixel) lightened
// toward white above the middle height (up to 95% at the highest) or darkened toward black below it (up to 60% at the
// lowest); under water the radar water colour darkened by the depth below the water (from the water's height down to
// the lowest height), from the neighbours under water; over a standing bridge (its centre on one not broken) the
// bridge's radar colour lightened or darkened for its deck's height, in every sample. Rows run from world y = 0 up (the radar draws them flipped).
export namespace engine::level::presentation
{
inline constexpr std::uint32_t RadarCells = 128; // RADAR_CELL_WIDTH / RADAR_CELL_HEIGHT

struct RadarTerrain
{
	std::uint32_t width{RadarCells};
	std::uint32_t height{RadarCells};
	std::vector<std::uint8_t> pixels; // RGBA8, row 0 at world y = 0
	float extentWidth{0}, extentHeight{0};
	float lowZ{0}, highZ{1};
	float terrainAverageZ{0};

	// The world size of a radar cell (m_xSample, m_ySample).
	float SampleX() const noexcept { return extentWidth / static_cast<float>(width); }
	float SampleY() const noexcept { return extentHeight / static_cast<float>(height); }
};

// A bridge as the radar shows it.
struct RadarDeck
{
	std::array<float, 3> color{}; // its RadarColor (0..1)
	float height{0};              // its deck's height (the average of its four corners)
};

struct RadarTerrainSource
{
	const Heightfield *terrain{nullptr};
	const TerrainSurface *surface{nullptr};
	const std::vector<std::array<std::uint8_t, 3>> *tileColors{nullptr}; // by tile (index >> 2)
	std::array<float, 3> waterColor{140.0f / 255.0f, 140.0f / 255.0f, 1.0f}; // WaterTransparency RadarWaterColor
	std::function<float(float, float)> ground;                            // getGroundHeight
	std::function<std::optional<float>(float, float)> water;              // the water height over a point, if any
	// A standing bridge over a point (none: no bridge there, or it is broken): its radar colour and its deck's height.
	std::function<std::optional<RadarDeck>(float, float)> bridge;
};

namespace radar_detail
{
// W3DRadar::interpolateColorForHeight.
inline std::array<float, 3> ShadeForHeight(std::array<float, 3> color, float height, float hiZ, float midZ, float loZ)
{
	constexpr float howBright = 0.95f, howDark = 0.60f;
	if (hiZ == midZ)
		hiZ = midZ + 0.1f;
	if (midZ == loZ)
		loZ = midZ - 0.1f;
	if (hiZ == loZ)
		hiZ = loZ + 0.2f;
	float t;
	std::array<float, 3> target;
	if (height >= midZ)
	{
		t = (height - midZ) / (hiZ - midZ);
		for (std::size_t c = 0; c < 3; ++c)
			target[c] = color[c] + (1.0f - color[c]) * howBright;
	}
	else
	{
		t = (midZ - height) / (midZ - loZ);
		for (std::size_t c = 0; c < 3; ++c)
			target[c] = color[c] + (0.0f - color[c]) * howDark;
	}
	for (std::size_t c = 0; c < 3; ++c)
		color[c] = std::clamp(color[c] + (target[c] - color[c]) * t, 0.0f, 1.0f);
	return color;
}

// WorldHeightMap::getTerrainColorAt: the sample's tile, mipped to one pixel.
inline std::array<float, 3> TileColorAt(const RadarTerrainSource &source, float x, float y)
{
	const Heightfield &terrain = *source.terrain;
	const float cell = static_cast<float>(terrain.cellSize.Raw()) / 65536.0f;
	std::int64_t ix = static_cast<std::int64_t>(std::floor(x / cell)) + terrain.border;
	std::int64_t iy = static_cast<std::int64_t>(std::floor(y / cell)) + terrain.border;
	ix = std::clamp<std::int64_t>(ix, 0, static_cast<std::int64_t>(terrain.width) - 1);
	iy = std::clamp<std::int64_t>(iy, 0, static_cast<std::int64_t>(terrain.height) - 1);
	const std::size_t index = static_cast<std::size_t>(iy) * terrain.width + static_cast<std::size_t>(ix);
	if (index >= source.surface->tiles.size())
		return {};
	const std::size_t tile = source.surface->tiles[index] >> 2;
	if (source.tileColors == nullptr || tile >= source.tileColors->size())
		return {};
	const auto &rgb = (*source.tileColors)[tile];
	return {rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f};
}
}

inline RadarTerrain BuildRadarTerrain(const RadarTerrainSource &source)
{
	using namespace radar_detail;
	RadarTerrain radar;
	const Heightfield &terrain = *source.terrain;
	const float cell = static_cast<float>(terrain.cellSize.Raw()) / 65536.0f;
	// W3DTerrainLogic::getExtent: the first playable area; the heights over the whole map.
	if (!terrain.playableExtents.empty())
	{
		radar.extentWidth = static_cast<float>(terrain.playableExtents.front()[0]) * cell;
		radar.extentHeight = static_cast<float>(terrain.playableExtents.front()[1]) * cell;
	}
	if (!terrain.heights.empty())
	{
		const auto [low, high] = std::minmax_element(terrain.heights.begin(), terrain.heights.end());
		radar.lowZ = static_cast<float>(low->Raw()) / 65536.0f;
		radar.highZ = static_cast<float>(high->Raw()) / 65536.0f;
	}
	const float sx = radar.SampleX(), sy = radar.SampleY();
	// Radar::newMap: the average ground height, every second cell.
	{
		double sum = 0.0;
		std::int64_t samples = 0;
		float wy = 0.0f;
		for (std::uint32_t y = 0; y < RadarCells; y += 2, wy += 2.0f * sy)
		{
			float wx = 0.0f;
			for (std::uint32_t x = 0; x < RadarCells; x += 2, wx += 2.0f * sx)
			{
				const float z = source.ground(wx, wy);
				const auto water = source.water(wx, wy);
				if (water && z < *water)
					continue;
				sum += z;
				++samples;
			}
		}
		radar.terrainAverageZ = static_cast<float>(sum / static_cast<double>(std::max<std::int64_t>(samples, 1)));
	}
	radar.pixels.assign(static_cast<std::size_t>(RadarCells) * RadarCells * 4, 255);
	const auto worldOf = [&](std::int32_t x, std::int32_t y) {
		x = std::clamp<std::int32_t>(x, 0, RadarCells - 1);
		y = std::clamp<std::int32_t>(y, 0, RadarCells - 1);
		return std::pair{static_cast<float>(x) * sx, static_cast<float>(y) * sy};
	};
	for (std::int32_t y = 0; y < static_cast<std::int32_t>(RadarCells); ++y)
		for (std::int32_t x = 0; x < static_cast<std::int32_t>(RadarCells); ++x)
		{
			const auto [wx, wy] = worldOf(x, y);
			std::array<float, 3> sum{};
			int samples = 0;
			// A standing bridge over the cell's point: the cell is the bridge's colour for the deck's height, all round.
			const std::optional<RadarDeck> deck = source.bridge ? source.bridge(wx, wy) : std::nullopt;
			const auto centreWater = source.water(wx, wy);
			const bool underwater = !deck && centreWater && source.ground(wx, wy) < *centreWater;
			for (std::int32_t j = y - 1; j <= y + 1; ++j)
				for (std::int32_t i = x - 1; i <= x + 1; ++i)
				{
					if (j < 0 || j >= static_cast<std::int32_t>(RadarCells) || i < 0 || i >= static_cast<std::int32_t>(RadarCells))
						continue;
					const auto [px, py] = worldOf(i, j);
					std::array<float, 3> color;
					if (underwater)
					{
						const auto water = source.water(px, py);
						const float ground = source.ground(px, py);
						if (!water || !(ground < *water))
							continue;
						color = ShadeForHeight(source.waterColor, ground, *centreWater, *centreWater, radar.lowZ);
					}
					else if (deck)
						color = ShadeForHeight(deck->color, deck->height, radar.terrainAverageZ, radar.highZ, radar.lowZ);
					else
						color = ShadeForHeight(TileColorAt(source, px, py), source.ground(px, py), radar.terrainAverageZ, radar.highZ, radar.lowZ);
					for (std::size_t c = 0; c < 3; ++c)
						sum[c] += color[c];
					++samples;
				}
			samples = std::max(samples, 1);
			std::uint8_t *pixel = &radar.pixels[(static_cast<std::size_t>(y) * RadarCells + static_cast<std::size_t>(x)) * 4];
			for (std::size_t c = 0; c < 3; ++c)
				pixel[c] = static_cast<std::uint8_t>(std::clamp(sum[c] / static_cast<float>(samples), 0.0f, 1.0f) * 255.0f);
			pixel[3] = 255;
		}
	return radar;
}

// Radar::worldToRadar: the cell a world point falls in (kept on the radar).
inline std::array<std::int32_t, 2> WorldToRadar(const RadarTerrain &radar, float x, float y)
{
	const float sx = radar.SampleX(), sy = radar.SampleY();
	const auto cell = [](float v, float sample) {
		return std::clamp<std::int32_t>(sample > 0.0f ? static_cast<std::int32_t>(v / sample) : 0, 0, RadarCells - 1);
	};
	return {cell(x, sx), cell(y, sy)};
}
}
