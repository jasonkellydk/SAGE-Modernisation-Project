export module games.generalszh.presentation.rendering.sky_box_rendering;
import std;

import games.generalszh.content.loading.content_loader;
import Graphics.RHI;
export import games.generalszh.presentation.effects.sky_box;

// The sky box drawn (W3DWater's WaterSkyboxSystem): its model through the asset cache, bound once ready with the map's
// faces swapped in and its textures clamped, drawn under the camera while the scripts have it on (DRAW_SKYBOX_BEGIN).
// The interface stays light; the asset and prop renderer imports live in the implementation.
export namespace generalszh::presentation
{
class SkyBoxRendering
{
public:
	SkyBoxRendering();
	~SkyBoxRendering();
	SkyBoxRendering(const SkyBoxRendering &) = delete;
	SkyBoxRendering &operator=(const SkyBoxRendering &) = delete;

	// Water.ini's faces and the map's (its map.ini's WaterTransparency, when `mapPath` names the .map file), and
	// GameData's SkyBoxScale and SkyBoxPositionZ.
	void Load(content::ContentLoader &loader, std::string_view mapPath, float scale, float positionZ);

	// Draws the sky box under the camera while `shown` (once its model is ready).
	void Draw(Graphics::Device &device, Graphics::CommandList &commands, const std::array<float, 16> &viewProjection, const std::array<float, 3> &eye,
		bool shown);

	const SkyBoxSetting &Setting() const noexcept;

private:
	struct State;
	std::unique_ptr<State> m_state;
};
}
