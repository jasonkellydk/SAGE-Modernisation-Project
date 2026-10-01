module;
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module games.generalszh.presentation.rendering.water_rendering;

import engine.level.presentation.water_mesh;
import engine.config.binding.schema;
import games.generalszh.content.water.water_settings;
import Engine.Core.Math.FixedPresentation;
import games.generalszh.presentation.rendering.texture_files;
import Graphics.Scene.Water.Renderer;

namespace generalszh::presentation
{
namespace
{
namespace level_view = engine::level::presentation;

using Image = TextureImage;

Image LoadTexture(const engine::filesystem::VirtualFileSystem &files, std::string_view name)
{
	return LoadArtTexture(files, name);
}

// Uploads with a full mip chain (the water tiles far into the distance);
// falls back to the base level when the device cannot generate mips.
Graphics::RHITextureHandle Upload(Graphics::Device &device, const Image &image)
{
	if (image.width == 0 || image.height == 0)
		return {};
	const std::span<const std::byte> bytes(image.pixels);
	Graphics::RHITexture description{image.width, image.height};
	std::uint32_t levels = 1;
	for (std::uint32_t size = (std::max)(image.width, image.height); size > 1; size >>= 1)
		++levels;
	description.mip_count = (std::min)(levels, 15u);
	description.generate_mips = description.mip_count > 1;
	if (description.generate_mips)
	{
		const auto texture = device.Create_Texture_Initialized(description, {bytes, image.width * 4});
		if (texture.Is_Valid() && device.Generate_Texture_Mips(texture))
			return texture;
		if (texture.Is_Valid())
			device.Destroy_Texture(texture);
	}
	description.mip_count = 1;
	description.generate_mips = false;
	return device.Create_Texture_Initialized(description, {bytes, image.width * 4});
}

Image Solid(std::array<std::uint8_t, 4> color)
{
	Image image{1, 1, {}};
	for (const std::uint8_t channel : color)
		image.pixels.push_back(std::byte{channel});
	return image;
}

// A tileable ripple normal map (sum of integer-frequency waves) standing in
// for the original's bump texture: it gives the flat surface its shading
// and environment variation.
Image RippleNormals()
{
	constexpr std::uint32_t size = 64;
	struct Wave
	{
		float kx, ky, amplitude, phase;
	};
	constexpr std::array<Wave, 5> waves{{
		{3, 1, 1.0f, 0.0f},
		{-2, 3, 0.8f, 1.3f},
		{5, -4, 0.45f, 2.1f},
		{1, 6, 0.35f, 4.0f},
		{-7, -2, 0.25f, 0.7f},
	}};
	constexpr float strength = 0.09f;
	Image image{size, size, {}};
	image.pixels.reserve(size * size * 4);
	const float twoPi = 2.0f * std::numbers::pi_v<float>;
	for (std::uint32_t y = 0; y < size; ++y)
		for (std::uint32_t x = 0; x < size; ++x)
		{
			float dx = 0.0f;
			float dy = 0.0f;
			for (const Wave &wave : waves)
			{
				const float angle = twoPi * (wave.kx * x + wave.ky * y) / size + wave.phase;
				const float slope = wave.amplitude * std::cos(angle) * twoPi / size * 10.0f;
				dx += slope * wave.kx;
				dy += slope * wave.ky;
			}
			const float nx = -dx * strength;
			const float ny = -dy * strength;
			const float inverse = 1.0f / std::sqrt(nx * nx + ny * ny + 1.0f);
			const auto encode = [](float value) {
				return std::byte{static_cast<std::uint8_t>(std::clamp((value * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f, 255.0f))};
			};
			image.pixels.push_back(encode(nx * inverse));
			image.pixels.push_back(encode(ny * inverse));
			image.pixels.push_back(encode(inverse));
			image.pixels.push_back(std::byte{255});
		}
	return image;
}

std::array<float, 4> ToColor(const content::WaterColor &color) noexcept
{
	return {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
}

// "Maps/X/X.map" -> "Maps/X/map" (the map's map.ini content set).
std::string MapIniSet(std::string_view mapPath)
{
	const auto slash = mapPath.find_last_of("/\\");
	if (slash == std::string_view::npos)
		return "map";
	return std::string(mapPath.substr(0, slash + 1)) + "map";
}

// How strongly the sky environment adds to the surface (the shader expects
// an HDR ocean environment; the time of day's sky texture is LDR).
constexpr float EnvironmentScale = 0.15f;

constexpr std::array<float, 16> Identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

struct WaterRendering::State
{
	Graphics::Device *device{nullptr};
	std::vector<Graphics::WaterMeshHandle> meshes;
	Graphics::RHITextureHandle waterTexture;
	Graphics::RHITextureHandle skyTexture;
	Graphics::RHITextureHandle flatDisplacement;
	Graphics::RHITextureHandle rippleNormals;
	bool additive{false};
	bool depthUnavailable{false};
	std::array<float, 4> tint{1, 1, 1, 0.5f};
	std::array<std::uint8_t, 3> radarWaterColor{140, 140, 255}; // WaterTransparency RadarWaterColor
	std::array<float, 4> domain{};
	float level{0.0f};
	float transparentDepth{3.0f};
	float minOpacity{1.0f};
	// Texture offset per second (legacy: ms * scroll * sky texels per unit / water texture width).
	std::array<float, 2> scrollPerSecond{};
	std::array<float, 4> sunColor{1, 1, 1, 1};
	std::array<std::array<float, 4>, 3> environmentFrame{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}};

	~State()
	{
		auto &renderer = Graphics::Get_Water_Renderer();
		for (const auto mesh : meshes)
			renderer.Destroy_Mesh(mesh);
		if (device == nullptr)
			return;
		for (const auto texture : {waterTexture, skyTexture, flatDisplacement, rippleNormals})
			if (texture.Is_Valid())
				device->Destroy_Texture(texture);
	}
};

WaterRendering::WaterRendering() : m_state(std::make_unique<State>()) {}
WaterRendering::~WaterRendering() = default;

std::size_t WaterRendering::SurfaceCount() const noexcept { return m_state->meshes.size(); }

bool WaterRendering::Load(Graphics::Device &device, const engine::filesystem::VirtualFileSystem &files, content::ContentLoader &loader,
	const engine::level::Level &level, std::string &error, std::string_view mapPath)
{
	m_state = std::make_unique<State>();
	State &state = *m_state;
	state.device = &device;

	const auto surfaces = level_view::BuildWaterSurfaces(level);
	if (surfaces.empty())
		return true;

	// Water.ini, then the map's overrides.
	content::WaterSettings settings;
	{
		const auto &document = loader.Load({"Data/INI/Default/Water", "Data/INI/Water"});
		engine::config::BindContext context{loader.DiagnosticsFor(document), engine::time::FixedStep{30}};
		content::BindWaterSettings(document, settings, context);
	}
	if (!mapPath.empty())
	{
		const std::string set = MapIniSet(mapPath);
		const auto &document = loader.Load({set});
		engine::config::BindContext context{loader.DiagnosticsFor(document), engine::time::FixedStep{30}};
		content::BindWaterSettings(document, settings, context);
	}
	const content::WaterSetDefinition &look = settings.For(level.lighting.current);
	const content::WaterTransparencyDefinition &transparency = settings.transparency;
	m_state->radarWaterColor = {transparency.radarWaterColor.r, transparency.radarWaterColor.g, transparency.radarWaterColor.b};

	const Image water = LoadTexture(files, look.waterTexture);
	if (water.width == 0)
	{
		error = "cannot load water texture " + look.waterTexture;
		return false;
	}
	state.waterTexture = Upload(device, water);
	const Image sky = LoadTexture(files, look.skyTexture);
	if (sky.width == 0)
		std::fprintf(stderr, "water: cannot load sky texture %s\n", look.skyTexture.c_str());
	else
		state.skyTexture = Upload(device, sky);
	state.flatDisplacement = Upload(device, Solid({0, 0, 0, 0}));
	state.rippleNormals = Upload(device, RippleNormals());
	if (!state.waterTexture.Is_Valid() || !state.flatDisplacement.Is_Valid() || !state.rippleNormals.Is_Valid())
	{
		error = "water texture upload failed";
		return false;
	}

	// The standing-water surface colour: the time of day's diffuse colour at
	// the transparent-water opacity (the original's surface diffuse).
	const auto diffuse = ToColor(look.diffuse);
	const auto transparent = ToColor(look.transparentDiffuse);
	state.tint = {diffuse[0], diffuse[1], diffuse[2], transparent[3]};
	state.additive = transparency.additiveBlending;
	state.transparentDepth = Engine::Math::ToFloat(transparency.transparentWaterDepth);
	state.minOpacity = Engine::Math::ToFloat(transparency.minWaterOpacity);
	const float texelsPerUnit = Engine::Math::ToFloat(look.skyTexelsPerUnit) / static_cast<float>(water.width);
	state.scrollPerSecond = {1000.0f * Engine::Math::ToFloat(look.uScrollPerMs) * texelsPerUnit,
		1000.0f * Engine::Math::ToFloat(look.vScrollPerMs) * texelsPerUnit};

	// Sun colour and direction of the time of day's first terrain light.
	if (level.lighting.current < level.lighting.sets.size() && !level.lighting.sets[level.lighting.current].terrain.empty())
	{
		const auto &light = level.lighting.sets[level.lighting.current].terrain.front();
		state.sunColor = {Engine::Math::ToFloat(light.diffuse[0]), Engine::Math::ToFloat(light.diffuse[1]),
			Engine::Math::ToFloat(light.diffuse[2]), 1.0f};
		const auto direction = Engine::Math::ToFloat(light.direction);
		state.environmentFrame = Graphics::Water_Environment_Frame({-direction.x, -direction.y, -direction.z});
	}

	auto &renderer = Graphics::Get_Water_Renderer();
	std::array<float, 4> bounds{surfaces.front().vertices.front().position[0], surfaces.front().vertices.front().position[1],
		surfaces.front().vertices.front().position[0], surfaces.front().vertices.front().position[1]};
	float highest = surfaces.front().height;
	for (const auto &surface : surfaces)
	{
		std::vector<Graphics::WaterVertex> vertices;
		vertices.reserve(surface.vertices.size());
		for (const auto &source : surface.vertices)
		{
			Graphics::WaterVertex vertex;
			vertex.position = source.position;
			vertex.uv = source.uv;
			vertex.secondary_uv = source.uv;
			vertices.push_back(vertex);
			bounds = {(std::min)(bounds[0], source.position[0]), (std::min)(bounds[1], source.position[1]),
				(std::max)(bounds[2], source.position[0]), (std::max)(bounds[3], source.position[1])};
		}
		highest = (std::max)(highest, surface.height);
		const auto mesh = renderer.Create_Mesh(vertices, surface.indices);
		if (!mesh.Is_Valid())
		{
			error = "water surface " + std::to_string(surface.regionId) + " could not be created";
			return false;
		}
		state.meshes.push_back(mesh);
	}
	state.domain = {bounds[0], bounds[1], bounds[2] - bounds[0], bounds[3] - bounds[1]};
	state.level = highest;
	return true;
}

void WaterRendering::Draw(Graphics::Device &device, Graphics::CommandList &commands, const std::array<float, 16> &view,
	const std::array<float, 16> &projection, const std::array<float, 3> &eye, float timeSeconds, const ShroudBinding *shroud, bool softEdge)
{
	State &state = *m_state;
	if (state.meshes.empty() || state.device != &device)
		return;
	auto &renderer = Graphics::Get_Water_Renderer();

	// The opaque scene's depth, for the shoreline fade (thickness of water
	// in front of the terrain), as the original's transparent water.
	const Graphics::RHITextureHandle depth =
		renderer.Capture_Depth(commands, device.Get_Swap_Chain().Depth_Target(), Graphics::RHITextureFormat::D24_UNorm_S8);
	if (!depth.Is_Valid() && !state.depthUnavailable)
	{
		state.depthUnavailable = true;
		std::fprintf(stderr, "water: scene depth unavailable, drawing without the shoreline fade\n");
	}

	Graphics::WaterParameters parameters;
	parameters.world = Identity;
	parameters.view = view;
	parameters.projection = projection;
	// Wrapped at 10: the shader scales the offset by 0.1, so whole texture repeats.
	parameters.animation = {std::fmod(timeSeconds * state.scrollPerSecond[0], 10.0f), std::fmod(timeSeconds * state.scrollPerSecond[1], 10.0f),
		timeSeconds, state.level};
	parameters.camera_position = {eye[0], eye[1], eye[2], 1.0f};
	parameters.displacement_domain = state.domain;
	parameters.tint = state.tint;
	// No mirror, refraction or underwater; the viewer's shroud over it as over the terrain (W3DWater's shroud stage).
	const bool shrouded = shroud != nullptr && shroud->Active();
	parameters.effects = {0.0f, shrouded ? 1.0f : 0.0f, 0.0f, 0.0f};
	if (shrouded)
		parameters.shroud_projection = shroud->projection;
	parameters.surface_options = {0.0f, state.transparentDepth, state.minOpacity, depth.Is_Valid() && softEdge && state.transparentDepth != 0.0f ? 1.0f : 0.0f};
	// The sky stands in for the reflected environment; the original blends a
	// cloud layer rather than an HDR sky, so it is kept dim (EnvironmentScale).
	parameters.sun_color = {state.sunColor[0] * EnvironmentScale, state.sunColor[1] * EnvironmentScale, state.sunColor[2] * EnvironmentScale, 1.0f};
	parameters.environment_frame = state.environmentFrame;
	parameters.inverse_view_projection = Identity;

	std::array<Graphics::RHITextureHandle, 11> textures{};
	textures[0] = state.waterTexture;
	textures[1] = state.flatDisplacement;
	textures[2] = state.rippleNormals;
	textures[6] = state.skyTexture;
	textures[8] = depth;
	if (shrouded)
		textures[7] = shroud->texture;

	Graphics::WaterStyle style;
	style.pass = Graphics::WaterPass::Ocean;
	style.blend = state.additive ? Graphics::RHIBlendMode::Additive : Graphics::RHIBlendMode::Alpha;
	for (const auto mesh : state.meshes)
		renderer.Draw(commands, mesh, style, parameters, textures);
}
std::array<std::uint8_t, 3> WaterRendering::RadarWaterColor() const noexcept { return m_state->radarWaterColor; }
}
