export module games.generalszh.presentation.rendering.standing_water;
import std;

export import games.generalszh.content.water.water_settings;
export import engine.level.model.level;
import Engine.Core.Math.FixedPresentation;

// The look of the map's standing water and rivers (W3DWater WaterRenderObjClass::drawTrapezoidWater and
// drawRiverWater, the polygon water every map draws with GameData's WaterType 0): both are textured with
// WaterTransparency's StandingWaterTexture (m_riverTexture, which updateMapOverrides reloads when a map.ini names
// another) and prelit with one diffuse colour per time of day. That colour is StandingWaterColor when the data sets one;
// left white (Water.ini's R:255 G:255 B:255), it is the terrain's lighting (the first terrain light's ambient plus each
// global terrain light's diffuse times how far it points down) times the time of day's WaterSet DiffuseColor. Black
// means unlit (full white). The alpha is always the time of day's DiffuseColor alpha.
export namespace generalszh::presentation
{
struct StandingWaterDiffuse
{
	std::uint8_t r{255};
	std::uint8_t g{255};
	std::uint8_t b{255};
	std::uint8_t a{255};
	constexpr bool operator==(const StandingWaterDiffuse &) const noexcept = default;
};

// GlobalData's m_numGlobalLights (NumberGlobalLights, 3 unless GameData.ini says otherwise; the shipped data does not).
inline constexpr std::size_t StandingWaterGlobalLights = 3;

// The diffuse drawTrapezoidWater / drawRiverWater give every vertex. `terrain`: the time of day's terrain lights
// (GlobalData m_terrainAmbient / m_terrainDiffuse / m_terrainLightPos, the map's lighting); null or empty means unlit
// (no ambient, no lights).
//
// Computed in single precision in the original's order, then truncated (REAL_TO_INT). Two retail quirks are fixed: the
// original reads the WaterSet colour's channels from the wrong ends of its packed ARGB (its "red" is the blue byte),
// which no shipped DiffuseColor shows since they are all grey; and it packs a channel lit past 255 without clamping, so
// the overflow bleeds into the next channel's bits; both clamp/match channels here.
StandingWaterDiffuse PickStandingWaterDiffuse(const content::WaterTransparencyDefinition &transparency, const content::WaterSetDefinition &look,
	std::span<const engine::level::Light> terrain) noexcept;

// The time of day's terrain lights of the level (empty when the level has none for it).
std::span<const engine::level::Light> CurrentTerrainLights(const engine::level::Lighting &lighting) noexcept;
}

namespace generalszh::presentation
{
namespace
{
std::uint8_t ToByte(float value) noexcept
{
	// REAL_TO_INT truncates; the original never clamps (see the header).
	if (!(value > 0.0f))
		return 0;
	if (value >= 255.0f)
		return 255;
	return static_cast<std::uint8_t>(static_cast<std::int32_t>(value));
}
}

StandingWaterDiffuse PickStandingWaterDiffuse(const content::WaterTransparencyDefinition &transparency, const content::WaterSetDefinition &look,
	std::span<const engine::level::Light> terrain) noexcept
{
	// INI::parseRGBColor stores each channel as value / 255.0f.
	float shadeR = static_cast<float>(transparency.standingWaterColor.r) / 255.0f;
	float shadeG = static_cast<float>(transparency.standingWaterColor.g) / 255.0f;
	float shadeB = static_cast<float>(transparency.standingWaterColor.b) / 255.0f;

	if (shadeR == 1.0f && shadeG == 1.0f && shadeB == 1.0f)
	{
		// Not overridden: the terrain's lighting.
		shadeR = shadeG = shadeB = 0.0f;
		if (!terrain.empty())
		{
			shadeR = Engine::Math::ToFloat(terrain[0].ambient[0]);
			shadeG = Engine::Math::ToFloat(terrain[0].ambient[1]);
			shadeB = Engine::Math::ToFloat(terrain[0].ambient[2]);
		}
		const std::size_t lights = std::min(terrain.size(), StandingWaterGlobalLights);
		for (std::size_t index = 0; index < lights; ++index)
		{
			const float down = -Engine::Math::ToFloat(terrain[index].direction.z);
			if (down > 0.0f)
			{
				shadeR += down * Engine::Math::ToFloat(terrain[index].diffuse[0]);
				shadeG += down * Engine::Math::ToFloat(terrain[index].diffuse[1]);
				shadeB += down * Engine::Math::ToFloat(terrain[index].diffuse[2]);
			}
		}
		const float waterShadeR = static_cast<float>(look.diffuse.r) / 255.0f;
		const float waterShadeG = static_cast<float>(look.diffuse.g) / 255.0f;
		const float waterShadeB = static_cast<float>(look.diffuse.b) / 255.0f;
		shadeR = shadeR * waterShadeR * 255.0f;
		shadeG = shadeG * waterShadeG * 255.0f;
		shadeB = shadeB * waterShadeB * 255.0f;
	}
	else
	{
		shadeR = shadeR * 255.0f;
		shadeG = shadeG * 255.0f;
		shadeB = shadeB * 255.0f;
		if (shadeR == 0.0f && shadeG == 0.0f && shadeB == 0.0f)
			shadeR = shadeG = shadeB = 255.0f; // the special case that disables lighting
	}
	return {ToByte(shadeR), ToByte(shadeG), ToByte(shadeB), look.diffuse.a};
}

std::span<const engine::level::Light> CurrentTerrainLights(const engine::level::Lighting &lighting) noexcept
{
	if (lighting.current >= lighting.sets.size())
		return {};
	return lighting.sets[lighting.current].terrain;
}
}
