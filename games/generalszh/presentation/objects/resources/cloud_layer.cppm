export module games.generalszh.presentation.objects.resources.cloud_layer;
import std;

import engine.ecs.system.system;

// The cloud shadows sliding over the terrain (TerrainShader2Stage's cloud pass, ST_TERRAIN_BASE_NOISE1: TSCloudMed.tga
// multiplied over the terrain): whether they show (BaseHeightMapRenderObjClass::useCloudMap: GlobalData's UseCloudMap,
// set by the static game LOD or the player's options, and not at night) and how far they have slid, in texture
// coordinates (updateNoise1's m_xOffset, m_yOffset).
export namespace generalszh::presentation
{
struct CloudLayer
{
	bool enabled{false};
	float x{0.0f};
	float y{0.0f};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::CloudLayer>
{
	static constexpr std::string_view StableName = "generalszh.presentation.cloud_layer";
};
}
