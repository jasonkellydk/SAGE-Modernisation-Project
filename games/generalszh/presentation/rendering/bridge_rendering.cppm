export module games.generalszh.presentation.rendering.bridge_rendering;
import std;

export import games.generalszh.presentation.objects.algorithms.bridge_geometry;
export import games.generalszh.presentation.rendering.shroud_pixels;
export import engine.level.model.level;
export import engine.filesystem.core.virtual_file_system;
import games.generalszh.presentation.rendering.texture_files;
import Graphics.Resources.MipChain;
import Graphics.Scene.Surfaces.Renderer;
import Graphics.Scene.Bridges.Renderer;
import Graphics.RHI;
import Engine.Core.Math.FixedPresentation;

// The map-drawn bridges on the surface renderer (W3DBridgeBuffer::loadBridgesInVertexAndIndexBuffers and drawBridges):
// each bridge's geometry (BuildBridgeGeometry) rebuilt only when the bridges as drawn change (BridgeViews' version) or
// the time of day's lighting does, drawn with its state's Roads.ini texture (Art/Textures, three mip levels:
// MIP_LEVELS_3), cut out below alpha 96, the cloud shadows over it while they show and the viewer's shroud
// (Draw_Bridges), after the terrain's scorch marks and before its tracks (W3DTerrainGraphics::Render).
export namespace generalszh::presentation
{
// The time of day's terrain lights as the bridge lighting takes them (the first one's ambient; up to the three global
// lights' diffuse and direction).
inline BridgeLighting BridgeLightingOf(const engine::level::LightingSet *set) noexcept
{
	BridgeLighting lighting;
	if (set == nullptr || set->terrain.empty())
	{
		lighting.ambient = {1.0f, 1.0f, 1.0f};
		return lighting;
	}
	for (std::size_t channel = 0; channel < 3; ++channel)
		lighting.ambient[channel] = Engine::Math::ToFloat(set->terrain[0].ambient[channel]);
	lighting.count = std::min(set->terrain.size(), lighting.lights.size());
	for (std::size_t index = 0; index < lighting.count; ++index)
	{
		const engine::level::Light &light = set->terrain[index];
		lighting.lights[index].direction = {Engine::Math::ToFloat(light.direction.x), Engine::Math::ToFloat(light.direction.y),
			Engine::Math::ToFloat(light.direction.z)};
		for (std::size_t channel = 0; channel < 3; ++channel)
			lighting.lights[index].diffuse[channel] = Engine::Math::ToFloat(light.diffuse[channel]);
	}
	return lighting;
}

// A bridge geometry's vertices as the surface renderer takes them (the packed colour to 0..1 channels).
inline std::vector<Graphics::SurfaceVertex> BridgeSurfaceVertices(const BridgeGeometry &geometry)
{
	std::vector<Graphics::SurfaceVertex> vertices(geometry.vertices.size());
	for (std::size_t index = 0; index < vertices.size(); ++index)
	{
		const BridgeVertex &source = geometry.vertices[index];
		vertices[index].position = source.position;
		vertices[index].color = {static_cast<float>((source.color >> 16) & 255u) / 255.0f, static_cast<float>((source.color >> 8) & 255u) / 255.0f,
			static_cast<float>(source.color & 255u) / 255.0f, static_cast<float>((source.color >> 24) & 255u) / 255.0f};
		vertices[index].uv = source.uv;
	}
	return vertices;
}

class BridgeRendering
{
public:
	BridgeRendering() = default;
	BridgeRendering(const BridgeRendering &) = delete;
	BridgeRendering &operator=(const BridgeRendering &) = delete;
	~BridgeRendering() { Release(); }

	void Load(Graphics::Device &device, const engine::filesystem::VirtualFileSystem &files)
	{
		Release();
		m_device = &device;
		m_files = &files;
	}

	// `clouds`: the cloud shadows' projection and texture while they show (none: no clouds).
	bool Draw(Graphics::CommandList &commands, const std::array<float, 16> &viewProjection, const BridgeViews *views, const BridgeArt *art,
		const engine::level::LightingSet *lighting, const ShroudBinding *shroud = nullptr, const std::array<float, 4> *clouds = nullptr,
		Graphics::RHITextureHandle cloudTexture = {})
	{
		if (views == nullptr || art == nullptr || m_device == nullptr)
			return true;
		if (views->version != m_version || lighting != m_lighting || !m_built)
			Rebuild(*views, *art, lighting);
		if (m_draws.empty())
			return true;
		Graphics::SurfaceParameters parameters;
		parameters.view_projection = viewProjection;
		parameters.textured = 1.0f;
		Graphics::RHITextureHandle cloud;
		if (clouds != nullptr && cloudTexture.Is_Valid())
		{
			parameters.cloud_projection = *clouds;
			parameters.cloud = 1.0f;
			cloud = cloudTexture;
		}
		Graphics::RHITextureHandle shroudTexture;
		if (shroud != nullptr && shroud->Active())
		{
			parameters.shroud = 1.0f;
			parameters.shroud_projection = shroud->projection;
			shroudTexture = shroud->texture;
		}
		return Graphics::Draw_Bridges(Graphics::Get_Surface_Renderer(), commands, m_draws, parameters, cloud, shroudTexture);
	}

private:
	void Rebuild(const BridgeViews &views, const BridgeArt &art, const engine::level::LightingSet *lighting)
	{
		auto &renderer = Graphics::Get_Surface_Renderer();
		for (const Graphics::BridgeDraw &draw : m_draws)
			renderer.Destroy_Mesh(draw.mesh);
		m_draws.clear();
		m_version = views.version;
		m_lighting = lighting;
		m_built = true;
		const BridgeLighting light = BridgeLightingOf(lighting);
		for (const BridgeView &view : views.bridges)
		{
			const BridgeKind *kind = art.Kind(view.bridgeTemplate);
			if (kind == nullptr || view.model >= kind->states.size())
				continue;
			const BridgeModel &model = kind->states[view.model];
			if (!model.loaded)
				continue;
			BridgeGeometry geometry;
			BuildBridgeGeometry(model, MeasureBridge(model), view.scale, view.from, view.to, light, geometry);
			if (geometry.indices.empty())
				continue;
			const auto vertices = BridgeSurfaceVertices(geometry);
			const Graphics::SurfaceMeshHandle mesh = renderer.Create_Mesh(vertices, geometry.indices);
			if (!mesh.Is_Valid())
				continue;
			m_draws.push_back({mesh, Texture(model.texture)});
		}
	}

	Graphics::RHITextureHandle Texture(const std::string &name)
	{
		for (const auto &[known, texture] : m_textures)
			if (known == name)
				return texture;
		Graphics::RHITextureHandle texture{};
		if (m_files != nullptr && m_device != nullptr && !name.empty())
		{
			const TextureImage image = LoadArtTexture(*m_files, name);
			if (image.Valid())
				texture = Graphics::Create_Mipped_Texture(*m_device, image.width, image.height, image.pixels, image.width * 4, 3);
		}
		m_textures.emplace_back(name, texture);
		return texture;
	}

	void Release()
	{
		auto &renderer = Graphics::Get_Surface_Renderer();
		for (const Graphics::BridgeDraw &draw : m_draws)
			renderer.Destroy_Mesh(draw.mesh);
		m_draws.clear();
		if (m_device != nullptr)
			for (const auto &[name, texture] : m_textures)
				if (texture.Is_Valid())
					m_device->Destroy_Texture(texture);
		m_textures.clear();
		m_built = false;
	}

	Graphics::Device *m_device{nullptr};
	const engine::filesystem::VirtualFileSystem *m_files{nullptr};
	std::vector<Graphics::BridgeDraw> m_draws;
	std::vector<std::pair<std::string, Graphics::RHITextureHandle>> m_textures;
	std::uint64_t m_version{0};
	const engine::level::LightingSet *m_lighting{nullptr};
	bool m_built{false};
};
}
