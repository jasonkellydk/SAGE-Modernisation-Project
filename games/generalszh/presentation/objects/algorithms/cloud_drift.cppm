export module games.generalszh.presentation.objects.algorithms.cloud_drift;
import std;

export import games.generalszh.presentation.objects.resources.cloud_layer;

// TerrainShader2Stage::updateNoise1 (W3DShaderManager.cpp): the clouds slide m_xSlidePerSecond (-0.02) and
// m_ySlidePerSecond (1.5 times that) texture widths a second, each kept within one width either way; the terrain samples
// them at its map position over STRETCH_FACTOR (one texture across 63 / 2 tiles of MAP_XY_FACTOR 10: 315 units) plus
// that slide (the camera-space position through the inverse view, scaled, then offset).
export namespace generalszh::presentation
{
inline constexpr float CloudSlideX = -0.02f;
inline constexpr float CloudSlideY = 1.50f * CloudSlideX;
inline constexpr float CloudStretch = 1.0f / (63.0f * 10.0f / 2.0f);

inline void DriftClouds(CloudLayer &clouds, float seconds) noexcept
{
	clouds.x += CloudSlideX * seconds;
	clouds.y += CloudSlideY * seconds;
	while (clouds.x > 1.0f)
		clouds.x -= 1.0f;
	while (clouds.y > 1.0f)
		clouds.y -= 1.0f;
	while (clouds.x < -1.0f)
		clouds.x += 1.0f;
	while (clouds.y < -1.0f)
		clouds.y += 1.0f;
}

// The terrain's cloud coordinates as (scale x, scale y, offset x, offset y): u = x * scale + offset.
inline std::array<float, 4> CloudProjection(const CloudLayer &clouds) noexcept { return {CloudStretch, CloudStretch, clouds.x, clouds.y}; }
}
