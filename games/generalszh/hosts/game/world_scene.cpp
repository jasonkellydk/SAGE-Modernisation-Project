module;
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <random>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <string_view>

module games.generalszh.hosts.game.world_scene;

import games.generalszh.presentation.objects.algorithms.cloud_drift;
import Engine.Core.Math.Matrix4;
import Graphics.Frame.Runtime;
import Graphics.Frame.RenderServices;
import games.generalszh.presentation.rendering.terrain_rendering;
import games.generalszh.presentation.rendering.object_rendering;
import games.generalszh.presentation.camera.algorithms.camera_slave;
import games.generalszh.presentation.rendering.water_rendering;
import games.generalszh.presentation.rendering.bridge_rendering;
import games.generalszh.presentation.rendering.road_rendering;
import games.generalszh.presentation.objects.algorithms.bridge_radar;
import games.generalszh.presentation.rendering.particle_rendering;
import games.generalszh.presentation.rendering.tracer_rendering;
import Graphics.Scene.Lighting.Renderer;
import Graphics.Scene.Shadows.DirectionalRenderer;
import Graphics.Scene.Props.Submission;
import Graphics.Frame.AttachmentBindings;
import Graphics.Scene.Lighting.Environment;
import Engine.Core.Math.FixedPresentation;
import engine.gameplay.common.spatial.resources.ground_height;
import Graphics.Scene.Shroud.Image;
import Graphics.Scene.Screen.Distortion;
import Graphics.Scene.Screen.FilterPass;
import Graphics.Scene.Views.CameraProjection;
import games.generalszh.presentation.effects.heat_haze;
import games.generalszh.presentation.camera.algorithms.screen_filters;
import games.generalszh.presentation.rendering.sky_box_rendering;
import Graphics.Scene.Shadows.StencilVolumes;
import games.generalszh.content.global.game_data;
import engine.config.binding.schema;

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
	presentation::BridgeRendering bridges;
	presentation::RoadRendering roads;
	presentation::ParticleRendering particles;
	presentation::TracerRendering tracers;
	presentation::SkyBoxRendering skyBox;
	float occludedLuminanceScale{0.5f}; // GameData OccludedColorLuminanceScale
	// The renderer's point lights showing presentation's dynamic lights (W3DDynamicLight), reused frame to frame.
	std::vector<Graphics::LightHandle> lights;
	// W3DShroud's image: the viewer's cells a texel each inside a one-texel border, uploaded when they change.
	Graphics::ShroudImage shroudImage;
	Graphics::RHITextureHandle shroudTexture;
	std::array<std::uint32_t, 2> shroudSize{};
	// This frame's heat haze and the client randomness its offsets draw from (GameClientRandomValueReal).
	presentation::HeatHaze haze;
	std::minstd_rand hazeRandom{0x5D0Du};
	// This frame's snow flakes (W3DSnowManager::render's), and the terrain's lowest height the visible box stops at
	// (BaseHeightMapRenderObjClass m_minHeight).
	presentation::SnowFlakes snow;
	float terrainMinHeight{0.0f};
	// The view's screen filter (W3DView's m_viewFilter: grey, motion blur) drawn over the frame, and this frame's quads.
	Graphics::ScreenFilterPass filterPass;
	presentation::ScreenFilterDraw filterDraw;
	std::vector<Graphics::ScreenFilterQuadDraw> filterQuads;

	// W3DView::draw: the filter's postRender over the drawn view, the frame copied to a picture (or the last one kept,
	// m_skipRender) and drawn back as the filter's quads; a pan's blur by the view's scroll (calcDeltaScroll: the look-at
	// point projected now less where it last moved from, both at the look-at's height; none unless both are in view).
	void DrawScreenFilter(Graphics::Device &device, Graphics::CameraState &camera, GameClient &game)
	{
		presentation::ViewFilter *filter = game.ScreenFilter();
		if (filter == nullptr)
			return;
		filterDraw.quads.clear();
		filterDraw.keepPicture = false;
		std::array<float, 2> scroll{};
		if (filter->motionBlur && filter->mode == presentation::ViewFilterMode::PanAlpha)
		{
			const std::array<float, 3> at = game.LookAtPosition();
			const std::array<float, 3> prior = game.PreviousLookAtPosition();
			Graphics::Vector3 priorScreen;
			Graphics::Vector3 screen;
			if (camera.Project(priorScreen, {prior[0], prior[1], at[2]}) == Graphics::CameraProjectionResult::InsideFrustum &&
				camera.Project(screen, {at[0], at[1], at[2]}) == Graphics::CameraProjectionResult::InsideFrustum)
				scroll = {screen.x - priorScreen.x, screen.y - priorScreen.y};
		}
		presentation::BlackWhiteDraw(*filter, filter->blackWhite, filterDraw);
		presentation::MotionBlurDraw(*filter, scroll, filterDraw);
		if (filterDraw.quads.empty() || !filterPass.Initialize(device))
			return;
		filterQuads.clear();
		for (const presentation::ScreenFilterQuad &quad : filterDraw.quads)
		{
			Graphics::ScreenFilterQuadDraw draw;
			draw.vertices = Graphics::Screen_Filter_Corners(quad.uv, quad.color);
			if (quad.blackWhite)
			{
				draw.parameters.operation = 1;
				draw.parameters.fade = quad.fade;
			}
			else
				draw.parameters.vertex_alpha = 1;
			draw.style.blend = quad.blend;
			if (quad.additive)
				draw.style.destination = Graphics::RHIBlendFactor::One;
			filterQuads.push_back(draw);
		}
		auto &swapChain = device.Get_Swap_Chain();
		const auto target = swapChain.Backbuffer();
		const auto depth = swapChain.Depth_Target();
		filterPass.Render(device.Immediate_Command_List(), target.texture, depth.texture, {0, 0, target.width, target.height, 0.0f, 1.0f}, filterQuads,
			filterDraw.keepPicture);
	}

	// The snow as the camera sees it this frame (W3DSnowManager::render: the tactical view's camera position, its
	// frustum, the terrain's visible box).
	const presentation::SnowFlakes *ExtractSnow(Graphics::Device &device, Graphics::CameraState &camera, GameClient &game)
	{
		const presentation::SnowField *field = game.Snow();
		if (field == nullptr || !field->enabled)
			return nullptr;
		const auto &view = camera.Get_View_Matrix().values;
		const auto &projection = camera.Get_Backend_Projection_Matrix().values;
		float nearClip = 1.0f;
		float farClip = 1000.0f;
		camera.Get_Clip_Planes(nearClip, farClip);
		const std::array<float, 3> eye = game.Eye();
		const float height = static_cast<float>(device.Get_Swap_Chain().Backbuffer().height);
		const presentation::SnowView snowView = presentation::MakeSnowView(eye, {view[0], view[1], view[2]}, {view[4], view[5], view[6]},
			{view[8], view[9], view[10]}, projection[0] != 0.0f ? 1.0f / projection[0] : 1.0f, projection[5] != 0.0f ? 1.0f / projection[5] : 1.0f,
			nearClip, farClip, height, terrainMinHeight);
		presentation::ExtractSnow(*field, game.WeatherShown(), snowView, snow);
		return snow.Size() != 0 ? &snow : nullptr;
	}

	~Renderers()
	{
		for (const Graphics::LightHandle light : lights)
			Graphics::DestroyPointLight(light);
		if (shroudTexture.Is_Valid())
			if (auto *device = Graphics::Shared_Frame_Device())
				device->Destroy_Texture(shroudTexture);
	}

	// The shroud to draw with this frame (none: nothing shrouded).
	presentation::ShroudBinding Shroud(Graphics::Device &device, const presentation::ShroudCells *cells)
	{
		if (cells == nullptr || cells->width == 0 || cells->height == 0)
			return {};
		const std::array<std::uint32_t, 2> size{cells->width + 2, cells->height + 2};
		if (!shroudTexture.Is_Valid() || size != shroudSize)
		{
			if (shroudTexture.Is_Valid())
				device.Destroy_Texture(shroudTexture);
			shroudTexture = device.Create_Texture({size[0], size[1], 1, Graphics::RHITextureFormat::BGR565_UNorm});
			shroudSize = size;
			shroudImage.Invalidate_Upload();
		}
		if (!shroudTexture.Is_Valid() || !shroudImage.Set_Cells(cells->pixels, cells->width, cells->height, cells->width, size[0], size[1], cells->border) ||
			!shroudImage.Upload(device, shroudTexture))
			return {};
		return {shroudTexture, presentation::ShroudProjection(cells->cellSize, size[0], size[1])};
	}

	// W3DSmudgeManager::render after the particles: each smudge copies the frame behind it and redraws it there pulled
	// by its offset, its corners clear and its centre at its opacity, tinted 0xFFEEDD; a smudge spans half its size
	// either side of its particle. The original pulls the centre by its across offset both ways (the up one is drawn
	// and never used); here each axis takes its own, as the offsets were meant.
	void DrawHeatHaze(Graphics::Device &device, Graphics::CameraState &camera, GameClient &game,
		const engine::effects::ParticleWorld &particles, bool heatEffects)
	{
		std::minstd_rand &random = hazeRandom;
		presentation::ExtractHeatHaze(particles, game.ParticleAlpha(), heatEffects,
			[&random](float low, float high) { return std::uniform_real_distribution<float>(low, high)(random); }, haze);
		auto &distortion = Graphics::GetScreenDistortionRenderer();
		if (haze.Size() == 0 || !distortion.Is_Initialized())
			return;
		for (float &size : haze.size)
			size *= 0.5f;
		auto &swapChain = device.Get_Swap_Chain();
		const auto target = swapChain.Backbuffer();
		const auto depth = swapChain.Depth_Target();
		const Graphics::Viewport viewport{0, 0, static_cast<float>(target.width), static_cast<float>(target.height), 0, 1};
		const std::array<float, 3> eye = game.Eye();
		distortion.Set_View(Graphics::View(camera.Get_View_Matrix(), camera.Get_Backend_Projection_Matrix(), {eye[0], eye[1], eye[2]}, viewport));
		// SMUDGE_DRAW_SIZE: drawn a batch at a time.
		constexpr std::size_t batch = 512;
		for (std::size_t first = 0; first < haze.Size(); first += batch)
		{
			const std::size_t count = std::min(batch, haze.Size() - first);
			const auto column = [&](const std::vector<float> &values) { return std::span<const float>(values.data() + first, count); };
			Graphics::ScreenDistortionData data{column(haze.x), column(haze.y), column(haze.z), column(haze.offsetX), column(haze.offsetY),
				column(haze.size), column(haze.opacity), {1.0f, 238.0f / 255.0f, 221.0f / 255.0f}};
			distortion.Render(device.Immediate_Command_List(), target.texture, depth.texture, {0, 0, target.width, target.height, 0.0f, 1.0f}, data);
		}
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
	m_renderers->bridges.Load(*Graphics::Shared_Frame_Device(), files);
	m_renderers->roads.Load(*Graphics::Shared_Frame_Device(), files, mesh);
	// The sky box's faces (Water.ini and the map's) and GameData's SkyBoxScale / SkyBoxPositionZ.
	{
		const engine::config::Document &gameDataSet = loader.Load({"Data/INI/Default/GameData", "Data/INI/GameData"});
		engine::config::BindContext context{loader.DiagnosticsFor(gameDataSet), engine::time::FixedStep{30}};
		const content::GameData gameData = content::BindGameData(gameDataSet, context);
		m_renderers->skyBox.Load(loader, mapName, Engine::Math::ToFloat(gameData.skyBoxScale), Engine::Math::ToFloat(gameData.skyBoxPositionZ));
		m_renderers->occludedLuminanceScale = Engine::Math::ToFloat(gameData.occludedLuminanceScale);
	}
	// BaseHeightMapRenderObjClass::initHeightData: m_minHeight, the lowest sample of the height map.
	if (!level.terrain.heights.empty())
		m_renderers->terrainMinHeight = Engine::Math::ToFloat(*std::min_element(level.terrain.heights.begin(), level.terrain.heights.end()));
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
	// The standing bridges in their radar colour (W3DRadar::buildTerrainTexture's workingBridge).
	if (view != nullptr)
		source.bridge = [view](float x, float y) -> std::optional<engine::level::presentation::RadarDeck> {
			const auto bridge = presentation::WorkingBridgeAt(view->World(), view->Content().bridges, x, y);
			if (!bridge)
				return std::nullopt;
			return engine::level::presentation::RadarDeck{bridge->color, bridge->height};
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
	game.WaitForModels();
	if (presentation::ModelLibrary *library = game.Models())
		m_renderers->objects.Preload(*Graphics::Shared_Frame_Device(), game.Objects(), *library);
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
	if (presentation::ModelLibrary *library = game.Models())
		m_renderers->objects.SubmitShadows(device, game.Objects(), *library);
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

void WorldScene::Draw(GameClient &game, Graphics::CameraState &camera, const engine::level::LightingSet *lighting, float waterSeconds,
	float infantryLightScale)
{
	auto &device = *Graphics::Shared_Frame_Device();
	const auto viewProjection = ViewProjection(camera);
	m_renderers->ShowLights(game.Lights());
	RenderShadowMaps(game, camera, lighting);
	const std::vector<presentation::ShownLight> frameLights = game.Lights();
	// The local player's shroud over the terrain, what lies on it, the objects and the water (W3DShroud).
	const presentation::ShroudBinding shroud = m_renderers->Shroud(device, game.Shroud());
	// The cloud shadows sliding over it while they show (BaseHeightMapRenderObjClass::useCloudMap).
	const presentation::CloudLayer *clouds = game.Clouds();
	const std::optional<std::array<float, 4>> cloudProjection =
		clouds != nullptr && clouds->enabled ? std::optional(presentation::CloudProjection(*clouds)) : std::nullopt;
	// The light map over the terrain while the detail's UseLightMap is on (BaseHeightMapRenderObjClass's NOISE2 / NOISE12).
	const presentation::DetailSettings *detail = game.Detail();
	m_renderers->terrain.Draw(device.Immediate_Command_List(), viewProjection, frameLights, &shroud, cloudProjection ? &*cloudProjection : nullptr,
		detail != nullptr && detail->useLightMap);
	// The roads (W3DTerrainGraphics::Render: drawRoads after the terrain, before the props and scorches), under the
	// clouds and the light map as the terrain is.
	m_renderers->roads.Draw(device.Immediate_Command_List(), viewProjection, game.Roads(), &shroud, cloudProjection ? &*cloudProjection : nullptr,
		m_renderers->terrain.CloudTexture(), detail != nullptr && detail->useLightMap ? m_renderers->terrain.LightMapTexture() : Graphics::RHITextureHandle{});
	m_renderers->terrain.DrawScorches(device.Immediate_Command_List(), viewProjection, game.Scorches(), &shroud);
	// The map-drawn bridges (W3DTerrainGraphics::Render: drawBridges after the scorches, before the tracks), lit by the
	// time of day's terrain lights, under the clouds and the shroud.
	m_renderers->bridges.Draw(device.Immediate_Command_List(), viewProjection, game.Bridges(), game.BridgeModels(), lighting, &shroud,
		cloudProjection ? &*cloudProjection : nullptr, m_renderers->terrain.CloudTexture());
	m_renderers->terrain.DrawTracks(device.Immediate_Command_List(), viewProjection, game.Tracks(), &shroud);
	// The objects' radius decals (a superweapon's target), then InGameUI's radius cursor under the pointer.
	if (const presentation::RadiusDecalViews *decals = game.RadiusDecals())
		for (const presentation::RadiusDecalView &decal : decals->decals)
			m_renderers->terrain.DrawRadiusDecal(device.Immediate_Command_List(), viewProjection,
				{decal.texture, decal.additive, {decal.at[0], decal.at[1]}, decal.radius, decal.color, decal.opacity}, &shroud);
	if (const presentation::RadiusCursor *cursor = game.CursorDecal(); cursor != nullptr && cursor->type != 0 && cursor->shown)
		m_renderers->terrain.DrawRadiusDecal(device.Immediate_Command_List(), viewProjection,
			{cursor->texture, cursor->additive, {cursor->at[0], cursor->at[1]}, cursor->radius, cursor->color, cursor->opacity}, &shroud);
	// RTS3DScene's building occlusion while the options' UseBehindBuildingMarker and the scripts' occlusion mode
	// (OPTIONS_SET_OCCLUSION_MODE) are on: the units behind buildings shown in their players' colours.
	const bool occlusionOn = (detail == nullptr || detail->behindBuildingMarkers) && game.Settings().occlusion;
	const presentation::OcclusionPlan occlusion =
		occlusionOn ? presentation::PlanOcclusion(game.Objects(), m_renderers->occludedLuminanceScale) : presentation::OcclusionPlan{};
	if (presentation::ModelLibrary *library = game.Models())
	{
		m_renderers->objects.Draw(device, device.Immediate_Command_List(), viewProjection, game.Eye(), lighting, game.Objects(), *library, frameLights,
			&shroud, infantryLightScale, &occlusion);
		// renderStenciledPlayerColor: each seen player's colour over the tactical view where its occludees lie behind.
		for (const presentation::OcclusionPlan::Quad &quad : occlusion.quads)
			Graphics::Draw_Player_Occlusion(Graphics::Get_Surface_Renderer(), device.Immediate_Command_List(), {-1.0f, 1.0f, 1.0f, -1.0f}, quad.color,
				quad.reference, false, 255);
	}
	Graphics::Get_Render_Services().Flush(nullptr);
	m_renderers->water.Draw(device, device.Immediate_Command_List(), camera.Get_View_Matrix().values, camera.Get_Backend_Projection_Matrix().values,
		game.Eye(), waterSeconds, &shroud, detail == nullptr || detail->showSoftWaterEdge);
	// WaterRenderSystem::render after the water: the sky box around the camera while the scripts draw it (DRAW_SKYBOX_BEGIN).
	m_renderers->skyBox.Draw(device, device.Immediate_Command_List(), viewProjection, game.Eye(), game.Settings().skyBox);
	// Tracers (W3DTracerDraw render objects in the scene).
	if (const presentation::Tracers *tracers = game.TracerEffects(); tracers != nullptr && tracers->Size() != 0)
		m_renderers->tracers.Draw(device.Immediate_Command_List(), viewProjection, camera.Get_View_Matrix().values, game.Eye(), *tracers);
	// Effects over everything in the world, as the original draws particles after water.
	if (const auto *effects = game.Particles())
	{
		const std::vector<presentation::BeamSegment> lasers = game.Lasers();
		m_renderers->particles.Draw(device, *effects, camera.Get_View_Matrix().values, camera.Get_Backend_Projection_Matrix().values, game.Eye(),
			game.ParticleAlpha(), lasers, m_renderers->ExtractSnow(device, camera, game), m_renderers->terrainMinHeight);
		game.SetFieldParticleCount(m_renderers->particles.FieldParticles());
		m_renderers->DrawHeatHaze(device, camera, game, *effects, detail == nullptr || detail->useHeatEffects);
	}
	m_renderers->DrawScreenFilter(device, camera, game);
}

std::optional<std::array<float, 12>> WorldScene::SlaveBone(GameClient &game, const SlavedCamera &slave)
{
	presentation::ModelLibrary *library = game.Models();
	const auto bone = library != nullptr ? presentation::BoneTransformOf(*library, slave.look, slave.animationSeconds, slave.animationStart, slave.bone)
										 : std::nullopt;
	if (!bone)
		return std::nullopt;
	return presentation::BoneInWorld(slave.world, *bone);
}

std::string WorldScene::Summary(GameClient &game) const
{
	const presentation::ModelLibrary *library = game.Models();
	return (library != nullptr ? presentation::ModelSummary(*library) : std::string("0 models")) + "; particles " +
		std::to_string(m_renderers->particles.DrawnParticles()) + ", streak segments " + std::to_string(m_renderers->particles.DrawnStreakSegments());
}
std::string WorldScene::Failures(GameClient &game) const
{
	const presentation::ModelLibrary *library = game.Models();
	return (library != nullptr ? library->failures : std::string{}) + m_renderers->objects.Failures();
}
}
