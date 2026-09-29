module;
#include <array>
#include <cmath>
#include <optional>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <string_view>

module games.generalszh.hosts.game.world_scene;

import Engine.Core.Math.Matrix4;
import Graphics.Frame.Runtime;
import Graphics.Frame.RenderServices;
import games.generalszh.presentation.rendering.terrain_rendering;
import games.generalszh.presentation.rendering.object_rendering;
import games.generalszh.presentation.rendering.water_rendering;
import games.generalszh.presentation.rendering.particle_rendering;
import Graphics.Scene.Lighting.Renderer;
import Graphics.Scene.Shadows.DirectionalRenderer;
import Graphics.Scene.Props.Submission;
import Graphics.Frame.AttachmentBindings;
import Graphics.Scene.Lighting.Environment;
import Engine.Core.Math.FixedPresentation;
import engine.gameplay.common.spatial.resources.ground_height;

namespace generalszh::host
{
namespace
{
// The camera's view-projection, composed as the original: projection * view.
std::array<float, 16> ViewProjection(Graphics::CameraState &camera)
{
	Engine::Math::Matrix4 projection;
	projection.elements = camera.Get_Backend_Projection_Matrix().values;
	Engine::Math::Matrix4 view;
	view.elements = camera.Get_View_Matrix().values;
	return Compose(projection, view).elements;
}
}

struct WorldScene::Renderers
{
	presentation::TerrainRendering terrain;
	presentation::ObjectRendering objects;
	presentation::WaterRendering water;
	presentation::ParticleRendering particles;
	// The renderer's point lights showing presentation's dynamic lights (W3DDynamicLight), reused frame to frame.
	std::vector<Graphics::LightHandle> lights;

	~Renderers()
	{
		for (const Graphics::LightHandle light : lights)
			Graphics::DestroyPointLight(light);
	}

	void ShowLights(const std::vector<presentation::ShownLight> &shown)
	{
		while (lights.size() < shown.size())
			lights.push_back(Graphics::CreatePointLight({}));
		for (std::size_t index = 0; index < lights.size(); ++index)
		{
			Graphics::RenderLight light;
			if (index < shown.size())
			{
				const presentation::ShownLight &on = shown[index];
				light.position = {on.at[0], on.at[1], on.at[2]};
				light.color = {on.diffuse[0], on.diffuse[1], on.diffuse[2]};
				light.range = on.farRange;
			}
			else
				light.flags = Graphics::RenderLightFlags::None;
			Graphics::UpdatePointLight(lights[index], light);
		}
	}
};

WorldScene::WorldScene() : m_renderers(std::make_unique<Renderers>()) {}
WorldScene::~WorldScene() = default;

bool WorldScene::Load(const engine::filesystem::VirtualFileSystem &files, content::ContentLoader &loader, const engine::level::Level &level,
	const engine::level::presentation::TerrainMesh &mesh, std::string_view mapName, std::string &error, std::string &waterError)
{
	if (!m_renderers->terrain.Load(*Graphics::Shared_Frame_Device(), files, loader, level, mesh, error))
		return false;
	m_renderers->water.Load(*Graphics::Shared_Frame_Device(), files, loader, level, waterError, std::string(mapName));
	return true;
}

engine::level::presentation::RadarTerrain WorldScene::BuildRadarTerrain(const engine::level::Level &level, GameClient &game) const
{
	engine::level::presentation::RadarTerrainSource source;
	source.terrain = &level.terrain;
	source.surface = &level.surface;
	source.tileColors = &m_renderers->terrain.TileColors();
	const auto water = m_renderers->water.RadarWaterColor();
	source.waterColor = {water[0] / 255.0f, water[1] / 255.0f, water[2] / 255.0f};
	session::SessionView *view = game.View();
	const auto *ground = view != nullptr ? view->World().FindResource<engine::gameplay::GroundHeight>() : nullptr;
	source.ground = [ground](float x, float y) {
		return ground != nullptr ? Engine::Math::ToFloat(ground->At({Engine::Math::Fixed::FromRaw(static_cast<std::int64_t>(std::llround(x * 65536.0f))), Engine::Math::Fixed::FromRaw(static_cast<std::int64_t>(std::llround(y * 65536.0f)))})) : 0.0f;
	};
	source.water = [ground](float x, float y) -> std::optional<float> {
		Engine::Math::Fixed height;
		if (ground != nullptr && ground->Water({Engine::Math::Fixed::FromRaw(static_cast<std::int64_t>(std::llround(x * 65536.0f))), Engine::Math::Fixed::FromRaw(static_cast<std::int64_t>(std::llround(y * 65536.0f)))}, height))
			return Engine::Math::ToFloat(height);
		return std::nullopt;
	};
	return engine::level::presentation::BuildRadarTerrain(source);
}

void WorldScene::Preload(GameClient &game)
{
	m_renderers->objects.Preload(*Graphics::Shared_Frame_Device(), game.Objects(), [&game](std::uint32_t look) { return game.ModelFor(look); });
}

// W3DDirectionalShadows (Collect_Directional_Shadow_Casters, Render_Directional_Shadow_Maps): the objects that cast
// shadows as casters, then the shadow maps from the map's first terrain light, for the camera's view (cached maps, its
// clip planes, 400 of depth padding, maps sized for the viewport); the terrain then darkens where they fall.
void WorldScene::RenderShadowMaps(GameClient &game, Graphics::CameraState &camera, const engine::level::LightingSet *lighting)
{
	auto &device = *Graphics::Shared_Frame_Device();
	auto &submission = Graphics::Get_Prop_Submission();
	submission.Clear_Shadows();
	Graphics::Get_Environment_Lighting().parameters.shadow_options[0] = 0;
	if (lighting == nullptr || lighting->terrain.empty())
		return;
	m_renderers->objects.SubmitShadows(game.Objects(), [&game](std::uint32_t look) { return game.ModelFor(look); });
	const auto viewport = Graphics::Get_Attachment_Bindings().Current().viewport;
	Graphics::View view;
	view.view_matrix = camera.Get_View_Matrix();
	view.projection_matrix = camera.Get_Backend_Projection_Matrix();
	Graphics::ShadowSettings settings;
	settings.cache_maps = true;
	camera.Get_Clip_Planes(settings.near_clip, settings.far_clip);
	settings.depth_padding = 400;
	settings.map_size = Graphics::Shadow_Map_Size_For_Viewport(viewport.width, viewport.height);
	Graphics::RenderLight light;
	light.type = Graphics::RenderLightType::Directional;
	light.flags = Graphics::RenderLightFlags::Enabled;
	const auto &sun = lighting->terrain[0].direction;
	light.direction = {Engine::Math::ToFloat(sun.x), Engine::Math::ToFloat(sun.y), Engine::Math::ToFloat(sun.z)};
	const auto saved = Graphics::Get_Attachment_Bindings().Capture();
	const auto color = device.Get_Swap_Chain().Backbuffer();
	const auto depth = device.Get_Swap_Chain().Depth_Target();
	Graphics::Get_Directional_Shadow_Renderer().Render(device.Immediate_Command_List(), view, light, settings, color.texture, depth.texture, viewport);
	Graphics::Get_Attachment_Bindings().Restore(saved);
	submission.Clear_Shadows();
}

void WorldScene::Draw(GameClient &game, Graphics::CameraState &camera, const engine::level::LightingSet *lighting, float waterSeconds)
{
	auto &device = *Graphics::Shared_Frame_Device();
	const auto viewProjection = ViewProjection(camera);
	m_renderers->ShowLights(game.Lights());
	RenderShadowMaps(game, camera, lighting);
	const std::vector<presentation::ShownLight> frameLights = game.Lights();
	m_renderers->terrain.Draw(device.Immediate_Command_List(), viewProjection, frameLights);
	m_renderers->terrain.DrawScorches(device.Immediate_Command_List(), viewProjection, game.Scorches());
	m_renderers->terrain.DrawTracks(device.Immediate_Command_List(), viewProjection, game.Tracks());
	m_renderers->objects.Draw(device, device.Immediate_Command_List(), viewProjection, game.Eye(), lighting, game.Objects(),
		[&game](std::uint32_t look) { return game.ModelFor(look); }, frameLights);
	game.KnowClips([this](std::uint32_t look) { return m_renderers->objects.ClipOf(look); });
	Graphics::Get_Render_Services().Flush(nullptr);
	m_renderers->water.Draw(device, device.Immediate_Command_List(), camera.Get_View_Matrix().values, camera.Get_Backend_Projection_Matrix().values,
		game.Eye(), waterSeconds);
	// Effects over everything in the world, as the original draws particles after water.
	if (const auto *effects = game.Particles())
	{
		const std::vector<presentation::BeamSegment> lasers = game.Lasers();
		m_renderers->particles.Draw(device, *effects, camera.Get_View_Matrix().values, camera.Get_Backend_Projection_Matrix().values, game.Eye(),
			game.ParticleAlpha(), lasers);
	}
}

std::string WorldScene::Summary() const
{
	return m_renderers->objects.Summary() + "; particles " + std::to_string(m_renderers->particles.DrawnParticles()) + ", streak segments " +
		std::to_string(m_renderers->particles.DrawnStreakSegments());
}
std::string WorldScene::Failures() const { return m_renderers->objects.Failures(); }
}
