export module games.generalszh.presentation.rendering.water_rendering;
import std;

import engine.level.model.level;
import games.generalszh.content.loading.content_loader;
import Graphics.RHI;
export import games.generalszh.presentation.rendering.shroud_pixels;

// The level's standing water and rivers on the water renderer: each water
// polygon is a translucent surface with the scrolling StandingWaterTexture,
// lit as drawTrapezoidWater (PickStandingWaterDiffuse: StandingWaterColor or the
// terrain's lighting times the WaterSet DiffuseColor), the sky as its reflected environment, and
// the WaterTransparency depth fade at the shoreline. The interface stays
// light; the water renderer, asset and content imports live in the
// implementation.
export namespace generalszh::presentation
{
class WaterRendering
{
public:
	WaterRendering();
	~WaterRendering();
	WaterRendering(const WaterRendering &) = delete;
	WaterRendering &operator=(const WaterRendering &) = delete;

	// Reads Water.ini (plus the map's map.ini overrides when `mapPath` names
	// the .map file), builds the surfaces and uploads the textures. A level
	// without water loads successfully and draws nothing.
	bool Load(Graphics::Device &device, const engine::filesystem::VirtualFileSystem &files, content::ContentLoader &loader,
		const engine::level::Level &level, std::string &error, std::string_view mapPath = {});

	// Draws after the opaque scene (terrain and objects, flushed): samples the
	// scene depth for the shoreline fade and blends over it. View and
	// projection are the camera's row-major matrices; `timeSeconds` drives
	// the texture scrolling and ripples. `softEdge`: the detail's ShowSoftWaterEdge (off: no shoreline fade, as
	// BaseHeightMapRenderObjClass draws no shoreline blend without it).
	void Draw(Graphics::Device &device, Graphics::CommandList &commands, const std::array<float, 16> &view,
		const std::array<float, 16> &projection, const std::array<float, 3> &eye, float timeSeconds, const ShroudBinding *shroud = nullptr,
		bool softEdge = true);

	std::size_t SurfaceCount() const noexcept;

	// WaterTransparency's RadarWaterColor (the radar's water).
	std::array<std::uint8_t, 3> RadarWaterColor() const noexcept;

private:
	struct State;
	std::unique_ptr<State> m_state;
};
}
