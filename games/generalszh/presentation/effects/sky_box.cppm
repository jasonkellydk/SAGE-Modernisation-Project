export module games.generalszh.presentation.effects.sky_box;
import std;

// The sky box (W3DWater's WaterSkyboxSystem: the "new_skybox" model, drawn by WaterRenderSystem::render while
// GlobalData m_drawSkyBox is on, which DRAW_SKYBOX_BEGIN / END set): made at SkyBoxScale, its textures clamped, centred
// under the camera at SkyBoxPositionZ; the map's map.ini WaterTransparency faces (SkyboxTextureN, E, S, W, T) swapped in
// for Water.ini's (INIWater's override load: W3DTerrainVisual::replaceSkyboxTextures). Plain data and free functions;
// the renderer draws the model.
export namespace generalszh::presentation
{
inline constexpr std::string_view SkyBoxModel = "new_skybox";

struct SkyBoxSetting
{
	float scale{4.5f};      // GameData SkyBoxScale (GlobalData's default 4.5)
	float positionZ{0.0f};  // GameData SkyBoxPositionZ
	// Water.ini's faces (N, E, S, W, top), and the map's (its map.ini's WaterTransparency, else the same).
	std::array<std::string, 5> textures;
	std::array<std::string, 5> mapTextures;
};

// replaceSkyboxTextures: each face the map names other than Water.ini, as (Water.ini's, the map's).
inline std::vector<std::pair<std::string, std::string>> SkyBoxReplacements(const SkyBoxSetting &setting)
{
	std::vector<std::pair<std::string, std::string>> replacements;
	for (std::size_t face = 0; face < setting.textures.size(); ++face)
		if (setting.mapTextures[face] != setting.textures[face])
			replacements.emplace_back(setting.textures[face], setting.mapTextures[face]);
	return replacements;
}

// WaterRenderSystem::render: the sky box at the camera's x and y and SkyBoxPositionZ, its model at SkyBoxScale (a
// row-major world transform).
inline std::array<float, 16> SkyBoxWorld(const SkyBoxSetting &setting, const std::array<float, 3> &eye) noexcept
{
	const float s = setting.scale;
	return {s, 0.0f, 0.0f, eye[0], 0.0f, s, 0.0f, eye[1], 0.0f, 0.0f, s, setting.positionZ, 0.0f, 0.0f, 0.0f, 1.0f};
}
}
