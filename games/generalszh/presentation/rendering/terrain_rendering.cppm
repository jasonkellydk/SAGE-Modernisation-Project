export module games.generalszh.presentation.rendering.terrain_rendering;
import Graphics.Resources.MipChain;
import std;

import engine.level.model.level;
import engine.level.presentation.terrain_mesh;
import engine.level.presentation.terrain_textures;
import engine.filesystem.core.virtual_file_system;
import engine.config.binding.schema;
import games.generalszh.content.loading.content_loader;
import games.generalszh.content.terrain.terrain_type;
export import games.generalszh.presentation.effects.light_pulses;
export import games.generalszh.presentation.effects.scorch_marks;
export import games.generalszh.presentation.objects.components.track_marks;
export import games.generalszh.presentation.rendering.shroud_pixels;
import Graphics.Scene.Tracks.Geometry;
import games.generalszh.presentation.rendering.texture_files;
import Graphics.Scene.Scorches.Geometry;
import Graphics.Scene.Surfaces.Renderer;
import Assets.Math;
import Engine.Core.Math.FixedPresentation;
import Assets.Adapters.TGA.Image;
import Assets.Images.Preparation;
import Graphics.RHI;
import Graphics.Scene.Terrain.Renderer;

namespace generalszh::presentation::detail
{
namespace level_view = engine::level::presentation;

inline level_view::MaterialImage DecodeTexture(const engine::filesystem::VirtualFileSystem &files, const std::string &path)
{
	level_view::MaterialImage image;
	const auto bytes = files.Read(path);
	Assets::TGAImage tga;
	if (!bytes || !Assets::Decode_TGA_Image(*bytes, tga))
		return image;
	std::vector<Assets::PreparedImage> levels;
	if (!Assets::Prepare_Image_Levels(tga.View(), Assets::PixelEncoding::RGBA8, tga.info.width, tga.info.height, 1, {0, 0, 0}, levels) ||
		levels.empty())
		return image;
	const Assets::PreparedImage &level = levels.front();
	image.width = level.width;
	image.height = level.height;
	image.pixels.resize(static_cast<std::size_t>(level.width) * level.height * 4);
	for (std::uint32_t row = 0; row < level.height; ++row)
		for (std::size_t column = 0; column < static_cast<std::size_t>(level.width) * 4; ++column)
			image.pixels[row * static_cast<std::size_t>(level.width) * 4 + column] =
				std::to_integer<std::uint8_t>(level.bytes[row * level.row_pitch + column]);
	return image;
}
}

export namespace generalszh::presentation
{
// The level's terrain on the renderer: textured base layer plus the 3-way
// blend overlay, lit statically, as the original terrain visual draws it.
class TerrainRendering
{
public:
	bool Load(Graphics::Device &device, const engine::filesystem::VirtualFileSystem &files, content::ContentLoader &loader,
		const engine::level::Level &level, const engine::level::presentation::TerrainMesh &mesh, std::string &error)
	{
		namespace level_view = engine::level::presentation;
		m_files = &files;
		m_device = &device;
		// Material name -> texture file, via Terrain.ini.
		const auto &document = loader.Load({"Data/INI/Default/Terrain", "Data/INI/Terrain"});
		engine::config::BindContext context{loader.DiagnosticsFor(document), engine::time::FixedStep{30}};
		engine::config::DefinitionTable<content::TerrainTypeDefinition> terrainTypes;
		engine::config::BindBlocks(document, "Terrain", content::TerrainTypeSchema(), terrainTypes, context,
			engine::config::Redefinition::Replace);
		level_view::MaterialImages images;
		const auto load = [&](const engine::level::TerrainMaterial &material, auto &target) {
			const content::TerrainTypeDefinition *type = terrainTypes.Find(material.name);
			if (type == nullptr)
			{
				std::println(std::cerr, "terrain: no Terrain.ini entry for material '{}'", material.name);
				return;
			}
			auto image = detail::DecodeTexture(files, content::TerrainTexturePath(*type));
			if (!image.Valid())
				std::println(std::cerr, "terrain: cannot load {}", content::TerrainTexturePath(*type));
			else
				target.emplace(material.name, std::move(image));
		};
		for (const auto &material : level.surface.materials)
			load(material, images.materials);
		for (const auto &material : level.surface.edgeMaterials)
			load(material, images.edgeMaterials);
		m_textures = level_view::BuildTerrainSurfaceTextures(level.terrain, level.surface, images);
		if (m_textures.atlas.width == 0)
		{
			error = "terrain atlas is empty";
			return false;
		}
		// TerrainTextureClass: the tiles' texture with three mip levels (MIP_LEVELS_3), box filtered (D3DXFilterTexture).
		m_atlas = Graphics::Create_Mipped_Texture(device, m_textures.atlas.width, m_textures.atlas.height,
			std::as_bytes(std::span(m_textures.atlas.pixels)), m_textures.atlas.width * 4, 3);
		if (!m_atlas.Is_Valid())
		{
			error = "terrain atlas upload failed";
			return false;
		}

		// Cells: geometry and lighting from the mesh, UVs/alpha from texturing.
		const auto meshCells = mesh.BuildCells();
		std::vector<Graphics::TerrainCell> base;
		std::vector<Graphics::TerrainCell> overlay;
		base.reserve(meshCells.size());
		for (std::size_t index = 0; index < meshCells.size() && index < m_textures.cells.size(); ++index)
		{
			const auto &source = meshCells[index];
			const auto &texturing = m_textures.cells[index];
			Graphics::TerrainCell cell;
			cell.origin = source.origin;
			cell.spacing = source.spacing;
			cell.heights = source.heights;
			cell.normals = source.normals;
			cell.alternate_diagonal = texturing.blend.alternateDiagonal;
			for (std::size_t corner = 0; corner < 4; ++corner)
			{
				cell.colors[corner] = {source.colors[corner][0], source.colors[corner][1], source.colors[corner][2],
					texturing.blend.alpha[corner] / 255.0f};
				cell.base_uv[corner] = {texturing.base[corner].u, texturing.base[corner].v};
				cell.blend_uv[corner] = {texturing.blend.uv[corner].u, texturing.blend.uv[corner].v};
			}
			base.push_back(cell);
			if (texturing.hasExtraBlend)
			{
				Graphics::TerrainCell extra = cell;
				extra.alternate_diagonal = texturing.extraBlend.alternateDiagonal;
				for (std::size_t corner = 0; corner < 4; ++corner)
				{
					extra.base_uv[corner] = {texturing.extraBlend.uv[corner].u, texturing.extraBlend.uv[corner].v};
					extra.colors[corner][3] = texturing.extraBlend.alpha[corner] / 255.0f;
				}
				overlay.push_back(extra);
			}
		}
		if (!Graphics::Get_Terrain_Renderer().Set_Cells(base) || !Graphics::Get_Terrain_Overlay_Renderer().Set_Cells(overlay))
		{
			error = "terrain cell upload failed";
			return false;
		}
		m_overlayCells = overlay.size();
		// For the scorch marks (W3DScorch::updateScorches): the height grid, each cell's diagonal, the scorch texture,
		// and the marks' colour, halfway between the first terrain light's ambient and diffuse.
		m_grid = {static_cast<int>(level.terrain.width), static_cast<int>(level.terrain.height), static_cast<int>(level.terrain.border),
			Engine::Math::ToFloat(level.terrain.cellSize), Engine::Math::ToFloat(level.terrain.cellSize) / 16.0f / 10.0f};
		m_heights.resize(level.terrain.heights.size());
		for (std::size_t index = 0; index < m_heights.size(); ++index)
			m_heights[index] = Engine::Math::ToFloat(level.terrain.heights[index]);
		m_diagonals.resize(m_textures.cells.size());
		for (std::size_t index = 0; index < m_diagonals.size(); ++index)
			m_diagonals[index] = m_textures.cells[index].blend.alternateDiagonal ? 1 : 0;
		for (const auto &placement : level.placements)
			if (const auto type = placement.properties.Get<std::int64_t>("scorchType"))
			{
				const auto radius = placement.properties.Get<Engine::Math::Fixed>("objectRadius").value_or(Engine::Math::Fixed{});
				AddScorch(m_staticMarks,
					{{Engine::Math::ToFloat(placement.position.x), Engine::Math::ToFloat(placement.position.y), Engine::Math::ToFloat(placement.position.z)},
						Engine::Math::ToFloat(radius), static_cast<std::uint32_t>(std::max<std::int64_t>(*type, 0))},
					false);
			}
		// CloudMapTerrainTextureClass: the cloud shadows' texture.
		const TextureImage cloudImage = LoadArtTexture(files, "TSCloudMed.tga");
		if (cloudImage.Valid())
			m_cloudTexture = device.Create_Texture_Initialized(
				{cloudImage.width, cloudImage.height, 1, Graphics::RHITextureFormat::RGBA8_UNorm, static_cast<std::uint32_t>(Graphics::RHITextureUsage::ShaderResource)},
				{std::as_bytes(std::span(cloudImage.pixels)), cloudImage.width * 4});
		// LightMapTerrainTextureClass: the light map (the map's macro texture; none: TSNoiseUrb.tga).
		const TextureImage lightMapImage = LoadArtTexture(files, "TSNoiseUrb.tga");
		if (lightMapImage.Valid())
			m_lightMapTexture = device.Create_Texture_Initialized(
				{lightMapImage.width, lightMapImage.height, 1, Graphics::RHITextureFormat::RGBA8_UNorm, static_cast<std::uint32_t>(Graphics::RHITextureUsage::ShaderResource)},
				{std::as_bytes(std::span(lightMapImage.pixels)), lightMapImage.width * 4});
		const TextureImage scorchImage = LoadArtTexture(files, "EXScorch01.tga");
		if (scorchImage.Valid())
			m_scorchTexture = device.Create_Texture_Initialized(
				{scorchImage.width, scorchImage.height, 1, Graphics::RHITextureFormat::RGBA8_UNorm, static_cast<std::uint32_t>(Graphics::RHITextureUsage::ShaderResource)},
				{std::as_bytes(std::span(scorchImage.pixels)), scorchImage.width * 4});
		const auto &sets = level.lighting.sets;
		if (!sets.empty() && !sets[std::min<std::size_t>(level.lighting.current, sets.size() - 1)].terrain.empty())
		{
			const auto &light = sets[std::min<std::size_t>(level.lighting.current, sets.size() - 1)].terrain[0];
			const auto half = [&](std::size_t channel) {
				return (Engine::Math::ToFloat(light.ambient[channel]) + Engine::Math::ToFloat(light.diffuse[channel])) / 2.0f;
			};
			const std::uint32_t packed = Assets::Color_To_ARGB({half(0), half(1), half(2), 1.0f});
			m_scorchColor = {((packed >> 16) & 255) / 255.0f, ((packed >> 8) & 255) / 255.0f, (packed & 255) / 255.0f, 1.0f};
			// TerrainTracksRenderObjClassSystem::flush: each channel ambient plus half the diffuse, to a byte.
			const auto channel = [&](std::size_t index) {
				return static_cast<std::uint32_t>(static_cast<int>((Engine::Math::ToFloat(light.ambient[index]) + Engine::Math::ToFloat(light.diffuse[index]) / 2.0f) * 255.0f)) & 255u;
			};
			m_trackColor = {channel(0) / 255.0f, channel(1) / 255.0f, channel(2) / 255.0f};
		}
		return true;
	}

	// `clouds`: the cloud shadows' projection (scale x, scale y, offset x, offset y) while they show; none: no clouds.
	// `lightMap`: the detail's UseLightMap.
	bool Draw(Graphics::CommandList &commands, const std::array<float, 16> &viewProjection, std::span<const ShownLight> lights = {},
		const ShroudBinding *shroud = nullptr, const std::array<float, 4> *clouds = nullptr, bool lightMap = false) const noexcept
	{
		Graphics::TerrainDrawParameters parameters;
		parameters.view_projection = viewProjection;
		// TerrainShader2Stage's cloud pass (ST_TERRAIN_BASE_NOISE1): the cloud texture multiplied over the terrain.
		const bool clouded = clouds != nullptr && m_cloudTexture.Is_Valid();
		if (clouded)
		{
			parameters.cloud_projection = *clouds;
			parameters.features[0] = 1.0f;
		}
		// TerrainShader2Stage's light map pass (ST_TERRAIN_BASE_NOISE2 / NOISE12: updateNoise2): the light map multiplied
		// over the terrain (D3DBLEND_DESTCOLOR, D3DTA_TEXTURE modulate), mapped by world x and y at STRETCH_FACTOR
		// (1 / (63 * MAP_XY_FACTOR / 2): a texture over 31.5 cells), wrapping, never moving.
		const bool lit = lightMap && m_lightMapTexture.Is_Valid();
		if (lit)
		{
			constexpr float stretch = 1.0f / (63.0f * 10.0f / 2.0f);
			parameters.lightmap_projection = {stretch, stretch, 0.0f, 0.0f};
			parameters.features[1] = 1.0f;
		}
		// W3DShroud: the terrain multiplied by the viewer's shroud (its projected texture).
		const bool shrouded = shroud != nullptr && shroud->Active();
		if (shrouded)
		{
			parameters.shroud_projection = shroud->projection;
			parameters.options[1] = 1.0f;
		}
		const Graphics::RHITextureHandle shroudTexture = shrouded ? shroud->texture : Graphics::RHITextureHandle{};
		// BaseHeightMapRenderObjClass::computeVertexLighting's dynamic lights (point lights: position and far range,
		// diffuse and mid range, ambient) over the terrain's own lighting, as many as the terrain pass takes.
		std::size_t count = 0;
		for (const ShownLight &light : lights)
		{
			if (count == parameters.lights.size())
				break;
			parameters.lights[count++] = {{light.at[0], light.at[1], light.at[2], light.farRange}, {light.diffuse[0], light.diffuse[1], light.diffuse[2], light.nearRange},
				{light.ambient[0], light.ambient[1], light.ambient[2], 0.0f}, {}};
		}
		parameters.light_options[0] = static_cast<float>(count);
		// Base and blend stages both sample the tile atlas.
		const std::array<Graphics::RHITextureHandle, 5> textures{m_atlas, m_atlas, clouded ? m_cloudTexture : Graphics::RHITextureHandle{},
			lit ? m_lightMapTexture : Graphics::RHITextureHandle{}, shroudTexture};
		if (!Graphics::Get_Terrain_Renderer().Render(commands, Graphics::TerrainSurfacePass::Surface, parameters, textures))
			return false;
		if (m_overlayCells == 0)
			return true;
		parameters.features[3] = 3;
		const std::array<Graphics::RHITextureHandle, 5> overlayTextures{m_atlas, {}, {}, {}, shroudTexture};
		return Graphics::Get_Terrain_Overlay_Renderer().Render(commands, Graphics::TerrainSurfacePass::Overlay, parameters, overlayTextures);
	}

	// W3DScorch::drawScorches: the map's static marks, then the gameplay ones, each as one terrain-hugging mesh (newest
	// first; the gameplay mesh rebuilt when its marks change).
	bool DrawScorches(Graphics::CommandList &commands, const std::array<float, 16> &viewProjection, const ScorchMarks *scorches,
		const ShroudBinding *shroud = nullptr)
	{
		if (!m_scorchTexture.Is_Valid() || m_grid.width < 2)
			return true;
		if (!m_staticBuilt)
		{
			m_staticBuilt = true;
			BuildScorches(m_staticMarks, m_staticMesh, m_staticIndices);
		}
		if (scorches != nullptr && scorches->version != m_scorchVersion)
		{
			m_scorchVersion = scorches->version;
			if (!BuildScorches(*scorches, m_scorchMesh, m_scorchIndices))
				return false;
		}
		// ScorchGeometry winds its triangles counter-clockwise seen from above (as the tracks' strips): those faces front.
		Graphics::SurfaceStyle style;
		style.cull = Graphics::RHICullMode::Back;
		style.front_counter_clockwise = true;
		Graphics::SurfaceParameters parameters;
		parameters.view_projection = viewProjection;
		const Graphics::RHITextureHandle shroudTexture = Shroud(parameters, shroud);
		const std::array<Graphics::RHITextureHandle, 4> textures{m_scorchTexture, {}, {}, shroudTexture};
		auto &renderer = Graphics::Get_Surface_Renderer();
		bool drawn = true;
		if (m_staticMesh.Is_Valid() && m_staticIndices != 0)
			drawn = renderer.Draw(commands, m_staticMesh, style, parameters, textures) && drawn;
		if (m_scorchMesh.Is_Valid() && m_scorchIndices != 0)
			drawn = renderer.Draw(commands, m_scorchMesh, style, parameters, textures) && drawn;
		return drawn;
	}

	// A radius decal (W3DProjectedShadowManager's SHADOW_ALPHA_DECAL / SHADOW_ADDITIVE_DECAL, the texture's square of
	// twice the radius laid on the terrain, clamped at its edges): an alpha decal blends its colour at its opacity; an
	// additive one adds its colour scaled by its opacity (Shadow::setOpacity / setColor).
	struct RadiusDecalDraw
	{
		std::string texture;
		bool additive{false};
		std::array<float, 2> center{};
		float radius{0.0f};
		std::array<float, 3> color{1, 1, 1};
		std::int32_t opacity{255};
	};
	bool DrawRadiusDecal(Graphics::CommandList &commands, const std::array<float, 16> &viewProjection, const RadiusDecalDraw &decal,
		const ShroudBinding *shroud = nullptr)
	{
		if (m_grid.width < 2 || !(decal.radius > 0.0f))
			return true;
		const Graphics::RHITextureHandle texture = TrackTexture(decal.texture);
		if (!texture.Is_Valid())
			return true;
		const float opacity = static_cast<float>(std::clamp(decal.opacity, 0, 255)) / 255.0f;
		const std::array<float, 4> color = decal.additive
			? std::array<float, 4>{decal.color[0] * opacity, decal.color[1] * opacity, decal.color[2] * opacity, 1.0f}
			: std::array<float, 4>{decal.color[0], decal.color[1], decal.color[2], opacity};
		Graphics::ScorchGeometry geometry;
		const int cellsAcross = m_grid.width - 1;
		Graphics::ScorchDescription mark{{decal.center[0], decal.center[1]}, decal.radius, 0};
		mark.whole_texture = true;
		geometry.Append(mark, m_grid, color,
			[&](int x, int y) { return m_heights[static_cast<std::size_t>(std::clamp(y, 0, m_grid.height - 1)) * m_grid.width + std::clamp(x, 0, m_grid.width - 1)]; },
			[&](int x, int y) {
				const std::size_t cell = static_cast<std::size_t>(std::clamp(y, 0, m_grid.height - 2)) * cellsAcross + std::clamp(x, 0, cellsAcross - 1);
				return cell < m_diagonals.size() && m_diagonals[cell] != 0;
			}, std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max());
		if (geometry.indices.empty())
			return true;
		auto &renderer = Graphics::Get_Surface_Renderer();
		if (m_decalMesh.Is_Valid())
		{
			if (!renderer.Update_Mesh(m_decalMesh, geometry.vertices, geometry.indices))
				return false;
		}
		else
			m_decalMesh = renderer.Create_Mesh(geometry.vertices, geometry.indices);
		Graphics::SurfaceStyle style;
		style.cull = Graphics::RHICullMode::Back;
		style.front_counter_clockwise = true; // ScorchGeometry's winding (else every radius decal was culled away)
		style.clamp_texture = true;
		style.blend = decal.additive ? Graphics::RHIBlendMode::Additive : Graphics::RHIBlendMode::Alpha;
		Graphics::SurfaceParameters parameters;
		parameters.view_projection = viewProjection;
		// Under the viewer's shroud, as the terrain's other decals (W3DShroud over the terrain pass).
		const Graphics::RHITextureHandle shroudTexture = Shroud(parameters, shroud);
		const std::array<Graphics::RHITextureHandle, 4> textures{texture, {}, {}, shroudTexture};
		return renderer.Draw(commands, m_decalMesh, style, parameters, textures);
	}

	// The objects' decal shadows (W3DProjectedShadowManager::renderShadows' SHADOW_DECAL list, queueDecal / flushDecals):
	// each its texture laid over the terrain cells under its box, its uv turned with the object, 0.01 x MAP_XY_FACTOR
	// over the ground, clamped at its edges, multiplied into what is drawn (_PresetMultiplicativeShader), in white; the
	// ones sharing a texture drawn together.
	struct ShadowDecalDraw
	{
		std::string texture;
		std::array<float, 2> center{};
		std::array<float, 2> uAxis{};
		std::array<float, 2> vAxis{};
		std::array<float, 2> uvOffset{};
		std::array<float, 4> extent{};
	};
	bool DrawShadowDecals(Graphics::CommandList &commands, const std::array<float, 16> &viewProjection, std::span<const ShadowDecalDraw> decals,
		const ShroudBinding *shroud = nullptr)
	{
		if (m_grid.width < 2 || decals.empty())
			return true;
		std::vector<const ShadowDecalDraw *> order;
		order.reserve(decals.size());
		for (const ShadowDecalDraw &decal : decals)
			order.push_back(&decal);
		std::stable_sort(order.begin(), order.end(), [](const ShadowDecalDraw *a, const ShadowDecalDraw *b) { return a->texture < b->texture; });
		Graphics::ScorchGrid grid = m_grid;
		grid.elevation = 0.01f * grid.spacing; // MAP_XY_FACTOR
		const int cellsAcross = m_grid.width - 1;
		auto &renderer = Graphics::Get_Surface_Renderer();
		bool drawn = true;
		for (std::size_t first = 0; first < order.size();)
		{
			std::size_t last = first;
			while (last < order.size() && order[last]->texture == order[first]->texture)
				++last;
			const Graphics::RHITextureHandle texture = TrackTexture(order[first]->texture);
			Graphics::ScorchGeometry geometry;
			if (texture.Is_Valid())
				for (std::size_t index = first; index < last; ++index)
				{
					const ShadowDecalDraw &decal = *order[index];
					Graphics::ScorchDescription mark{decal.center, 1.0f, 0};
					mark.oriented = true;
					mark.u_axis = decal.uAxis;
					mark.v_axis = decal.vAxis;
					mark.uv_offset = decal.uvOffset;
					mark.extent = decal.extent;
					geometry.Append(mark, grid, {1, 1, 1, 1},
						[&](int x, int y) { return m_heights[static_cast<std::size_t>(std::clamp(y, 0, m_grid.height - 1)) * m_grid.width + std::clamp(x, 0, m_grid.width - 1)]; },
						[&](int x, int y) {
							const std::size_t cell = static_cast<std::size_t>(std::clamp(y, 0, m_grid.height - 2)) * cellsAcross + std::clamp(x, 0, cellsAcross - 1);
							return cell < m_diagonals.size() && m_diagonals[cell] != 0;
						}, std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max());
				}
			first = last;
			if (geometry.indices.empty())
				continue;
			if (m_shadowDecalMesh.Is_Valid())
			{
				if (!renderer.Update_Mesh(m_shadowDecalMesh, geometry.vertices, geometry.indices))
					return false;
			}
			else
				m_shadowDecalMesh = renderer.Create_Mesh(geometry.vertices, geometry.indices);
			Graphics::SurfaceStyle style;
			style.cull = Graphics::RHICullMode::Back;
			style.front_counter_clockwise = true; // ScorchGeometry's winding
			style.clamp_texture = true;
			style.blend = Graphics::RHIBlendMode::Multiply;
			Graphics::SurfaceParameters parameters;
			parameters.view_projection = viewProjection;
			// Multiplied in, it darkens what the terrain pass already shrouded: not shrouded again.
			const Graphics::RHITextureHandle shroudTexture = Shroud(parameters, nullptr);
			(void)shroud;
			const std::array<Graphics::RHITextureHandle, 4> textures{texture, {}, {}, shroudTexture};
			drawn = renderer.Draw(commands, m_shadowDecalMesh, style, parameters, textures) && drawn;
		}
		return drawn;
	}

	// TerrainTracksRenderObjClassSystem::flush: each track with two or more edges as one strip (its older edges past the
	// opaque ones fading with distance down the track), its texture from Art/Textures, back faces culled.
	bool DrawTracks(Graphics::CommandList &commands, const std::array<float, 16> &viewProjection, std::span<const TrackView> tracks,
		const ShroudBinding *shroud = nullptr)
	{
		Graphics::SurfaceStyle style;
		style.cull = Graphics::RHICullMode::Back;
		style.front_counter_clockwise = true;
		Graphics::SurfaceParameters parameters;
		parameters.view_projection = viewProjection;
		const Graphics::RHITextureHandle shroudTexture = Shroud(parameters, shroud);
		auto &renderer = Graphics::Get_Surface_Renderer();
		Graphics::TrackGeometry geometry;
		bool drawn = true;
		std::size_t used = 0;
		for (const TrackView &track : tracks)
		{
			if (track.left.size() < 2)
				continue;
			const Graphics::RHITextureHandle texture = TrackTexture(track.texture);
			if (!texture.Is_Valid())
				continue;
			geometry.Build({track.left, track.right, track.v, track.alpha}, static_cast<int>(track.maxEdges), static_cast<int>(track.maxOpaqueEdges), m_trackColor);
			if (used == m_trackMeshes.size())
				m_trackMeshes.emplace_back();
			Graphics::SurfaceMeshHandle &mesh = m_trackMeshes[used++];
			if (mesh.Is_Valid())
			{
				if (!renderer.Update_Mesh(mesh, geometry.vertices, geometry.indices))
					continue;
			}
			else
				mesh = renderer.Create_Mesh(geometry.vertices, geometry.indices);
			const std::array<Graphics::RHITextureHandle, 4> textures{texture, {}, {}, shroudTexture};
			drawn = renderer.Draw(commands, mesh, style, parameters, textures) && drawn;
		}
		return drawn;
	}

	// Surfaces on the terrain (scorches, tracks) take the shroud as the terrain does; the texture for their shroud slot.
	static Graphics::RHITextureHandle Shroud(Graphics::SurfaceParameters &parameters, const ShroudBinding *shroud) noexcept
	{
		if (shroud == nullptr || !shroud->Active())
			return {};
		parameters.shroud = 1.0f;
		parameters.shroud_projection = shroud->projection;
		return shroud->texture;
	}

public:
	// Each tile's colour mipped to one pixel (the radar's terrain colours).
	const std::vector<std::array<std::uint8_t, 3>> &TileColors() const noexcept { return m_textures.tileColors; }
	// The cloud shadows' texture (CloudMapTerrainTextureClass), for what else they fall on (the bridges).
	Graphics::RHITextureHandle CloudTexture() const noexcept { return m_cloudTexture; }
	// The light map (LightMapTerrainTextureClass), for what else it lies on (the roads).
	Graphics::RHITextureHandle LightMapTexture() const noexcept { return m_lightMapTexture; }

private:
	Graphics::RHITextureHandle TrackTexture(const std::string &name)
	{
		for (const auto &[known, texture] : m_trackTextures)
			if (known == name)
				return texture;
		Graphics::RHITextureHandle texture{};
		if (m_files != nullptr && m_device != nullptr)
		{
			const TextureImage image = LoadArtTexture(*m_files, name);
			if (image.Valid())
				texture = m_device->Create_Texture_Initialized(
					{image.width, image.height, 1, Graphics::RHITextureFormat::RGBA8_UNorm, static_cast<std::uint32_t>(Graphics::RHITextureUsage::ShaderResource)},
					{std::as_bytes(std::span(image.pixels)), image.width * 4});
		}
		m_trackTextures.emplace_back(name, texture);
		return texture;
	}

	// W3DScorch::updateScorches: the marks (newest first) as terrain-hugging geometry in `mesh`.
	bool BuildScorches(const ScorchMarks &scorches, Graphics::SurfaceMeshHandle &mesh, std::size_t &indices)
	{
		auto &renderer = Graphics::Get_Surface_Renderer();
		Graphics::ScorchGeometry geometry;
		const int cellsAcross = m_grid.width - 1;
		for (auto it = scorches.marks.rbegin(); it != scorches.marks.rend(); ++it)
			if (!geometry.Append({{it->at[0], it->at[1]}, it->radius, it->type}, m_grid, m_scorchColor,
					[&](int x, int y) { return m_heights[static_cast<std::size_t>(std::clamp(y, 0, m_grid.height - 1)) * m_grid.width + std::clamp(x, 0, m_grid.width - 1)]; },
					[&](int x, int y) {
						const std::size_t cell = static_cast<std::size_t>(std::clamp(y, 0, m_grid.height - 2)) * cellsAcross + std::clamp(x, 0, cellsAcross - 1);
						return cell < m_diagonals.size() && m_diagonals[cell] != 0;
					}))
				break;
		indices = geometry.indices.size();
		if (mesh.Is_Valid())
			return renderer.Update_Mesh(mesh, geometry.vertices, geometry.indices);
		if (!geometry.indices.empty())
			mesh = renderer.Create_Mesh(geometry.vertices, geometry.indices);
		return true;
	}

	engine::level::presentation::TerrainSurfaceTextures m_textures;
	Graphics::RHITextureHandle m_atlas{};
	std::size_t m_overlayCells{0};
	Graphics::SurfaceMeshHandle m_shadowDecalMesh{};
	Graphics::ScorchGrid m_grid{};
	std::vector<float> m_heights;
	std::vector<std::uint8_t> m_diagonals;
	Graphics::RHITextureHandle m_scorchTexture{};
	Graphics::RHITextureHandle m_cloudTexture{};
	Graphics::RHITextureHandle m_lightMapTexture{};
	std::array<float, 4> m_scorchColor{1, 1, 1, 1};
	Graphics::SurfaceMeshHandle m_scorchMesh{};
	std::size_t m_scorchIndices{0};
	std::uint64_t m_scorchVersion{0};
	Graphics::SurfaceMeshHandle m_decalMesh{}; // the radius decal's, rebuilt each frame it is drawn
	// The map's own scorch marks (placed objects with a scorchType: W3DTerrainVisual's addStaticScorch).
	ScorchMarks m_staticMarks;
	Graphics::SurfaceMeshHandle m_staticMesh{};
	std::size_t m_staticIndices{0};
	bool m_staticBuilt{false};
	// Terrain tracks: their colour, their textures by name, and the meshes drawn each frame.
	const engine::filesystem::VirtualFileSystem *m_files{nullptr};
	Graphics::Device *m_device{nullptr};
	std::array<float, 3> m_trackColor{1, 1, 1};
	std::vector<std::pair<std::string, Graphics::RHITextureHandle>> m_trackTextures;
	std::vector<Graphics::SurfaceMeshHandle> m_trackMeshes;
};
}
