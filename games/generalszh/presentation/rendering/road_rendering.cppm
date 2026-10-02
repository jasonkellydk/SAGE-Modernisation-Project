export module games.generalszh.presentation.rendering.road_rendering;
import std;

export import games.generalszh.presentation.roads.resources.road_geometry;
export import games.generalszh.presentation.rendering.shroud_pixels;
export import engine.level.presentation.terrain_mesh;
export import engine.filesystem.core.virtual_file_system;
import games.generalszh.presentation.rendering.texture_files;
import Graphics.Resources.MipChain;
import Graphics.Scene.Surfaces.Renderer;
import Graphics.Scene.Roads.Renderer;
import Graphics.RHI;

// The map's roads on the surface renderer (W3DRoadBuffer::drawRoads after the terrain, before the props, scorches and
// bridges): each road type's mesh in drawing order with its Roads.ini texture (Art/Textures, three mip levels:
// MIP_LEVELS_3, wrapping, filtered as BilinearTerrainTex / TrilinearTerrainTex say: the shipped Yes), each vertex lit by
// the terrain's static lighting at its height map vertex (RoadSegment::updateSegLighting: getStaticDiffuse), the cloud
// shadows and light map over it while the terrain has them (ST_ROAD_BASE_NOISE1/2/12) and the viewer's shroud. Meshes
// are uploaded once per map.
export namespace generalszh::presentation
{
// RoadSegment::updateSegLighting: a road vertex's colour, the terrain's static diffuse at its height map vertex.
inline std::array<float, 4> RoadVertexColor(const engine::level::presentation::TerrainMesh &terrain, const RoadVertex &vertex)
{
	const auto diffuse = terrain.StaticDiffuse(vertex.lightCell[0], vertex.lightCell[1]);
	return {diffuse[0], diffuse[1], diffuse[2], 1.0f};
}

class RoadRendering
{
public:
	RoadRendering() = default;
	RoadRendering(const RoadRendering &) = delete;
	RoadRendering &operator=(const RoadRendering &) = delete;
	~RoadRendering() { Release(); }

	// The map's terrain (its heights and time of day lighting light the roads).
	void Load(Graphics::Device &device, const engine::filesystem::VirtualFileSystem &files, const engine::level::presentation::TerrainMesh &terrain)
	{
		Release();
		m_device = &device;
		m_files = &files;
		m_terrain.emplace(terrain);
	}

	// `clouds`: the cloud shadows' projection and texture while they show; `lightMap`: the light map while the detail's
	// UseLightMap is on (none: neither).
	bool Draw(Graphics::CommandList &commands, const std::array<float, 16> &viewProjection, const RoadGeometry *roads, const ShroudBinding *shroud = nullptr,
		const std::array<float, 4> *clouds = nullptr, Graphics::RHITextureHandle cloudTexture = {}, Graphics::RHITextureHandle lightMapTexture = {})
	{
		if (roads == nullptr || m_device == nullptr || !m_terrain)
			return true;
		if (roads != m_built || roads->meshes.data() != m_builtMeshes)
			Rebuild(*roads);
		Graphics::SurfaceParameters parameters;
		parameters.view_projection = viewProjection;
		parameters.textured = 1.0f;
		// STRETCH_FACTOR: the noise textures over 31.5 cells.
		constexpr float stretch = 1.0f / (63.0f * 10.0f / 2.0f);
		parameters.lightmap_projection = {stretch, stretch, 0.0f, 0.0f};
		std::array<Graphics::RHITextureHandle, 4> textures{};
		if (clouds != nullptr && cloudTexture.Is_Valid())
		{
			parameters.cloud_projection = *clouds;
			parameters.cloud = 1.0f;
			textures[1] = cloudTexture;
		}
		if (lightMapTexture.Is_Valid())
		{
			parameters.lightmap = 1.0f;
			textures[2] = lightMapTexture;
		}
		if (shroud != nullptr && shroud->Active())
		{
			parameters.shroud = 1.0f;
			parameters.shroud_projection = shroud->projection;
			textures[3] = shroud->texture;
		}
		auto &renderer = Graphics::Get_Surface_Renderer();
		bool drawn = true;
		for (const Drawn &road : m_draws)
		{
			if (!road.texture.Is_Valid() || !road.mesh.Is_Valid())
				continue;
			textures[0] = road.texture;
			drawn = Graphics::Draw_Road(renderer, commands, road.mesh, parameters, textures, true) && drawn;
		}
		return drawn;
	}

private:
	struct Drawn
	{
		Graphics::SurfaceMeshHandle mesh;
		Graphics::RHITextureHandle texture;
	};

	void Rebuild(const RoadGeometry &roads)
	{
		ReleaseMeshes();
		m_built = &roads;
		m_builtMeshes = roads.meshes.data();
		auto &renderer = Graphics::Get_Surface_Renderer();
		for (const RoadMesh &road : roads.meshes)
		{
			std::vector<Graphics::SurfaceVertex> vertices(road.vertices.size());
			for (std::size_t index = 0; index < vertices.size(); ++index)
			{
				vertices[index].position = road.vertices[index].position;
				vertices[index].uv = road.vertices[index].uv;
				vertices[index].color = RoadVertexColor(*m_terrain, road.vertices[index]);
			}
			m_draws.push_back({renderer.Create_Mesh(vertices, road.indices), Texture(road.texture)});
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

	void ReleaseMeshes()
	{
		auto &renderer = Graphics::Get_Surface_Renderer();
		for (const Drawn &road : m_draws)
			if (road.mesh.Is_Valid())
				renderer.Destroy_Mesh(road.mesh);
		m_draws.clear();
		m_built = nullptr;
	}

	void Release()
	{
		ReleaseMeshes();
		if (m_device != nullptr)
			for (const auto &[name, texture] : m_textures)
				if (texture.Is_Valid())
					m_device->Destroy_Texture(texture);
		m_textures.clear();
	}

	Graphics::Device *m_device{nullptr};
	const engine::filesystem::VirtualFileSystem *m_files{nullptr};
	std::optional<engine::level::presentation::TerrainMesh> m_terrain;
	const RoadGeometry *m_built{nullptr};
	const RoadMesh *m_builtMeshes{nullptr};
	std::vector<Drawn> m_draws;
	std::vector<std::pair<std::string, Graphics::RHITextureHandle>> m_textures;
};
}
