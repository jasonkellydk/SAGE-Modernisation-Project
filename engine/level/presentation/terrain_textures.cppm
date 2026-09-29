export module engine.level.presentation.terrain_textures;
import std;

export import engine.level.presentation.terrain_atlas;
import Engine.Core.Math.FixedPresentation;

// Level terrain surface -> per-cell texture coordinates and blend alphas into
// the atlases of terrain_atlas, ported from the original terrain renderer
// (getUVData, getAlphaUVData, getExtraAlphaUVData, getUVForTileIndex).
//
// Cells are row-major, (width - 1) * (height - 1) of them; cell (x, y) is
// textured from height sample (x, y). Corners run counter-clockwise from
// (x, y): 0 = (x, y), 1 = (x + 1, y), 2 = (x + 1, y + 1), 3 = (x, y + 1).
//
// Kept quirks of the original:
// - A tile index packs a 256-pixel tile and a quadrant (index >> 2 is the
//   tile, bit 0 the right half, bit 1 the top half): one cell shows one
//   128-pixel quarter of a tile, so a tile covers 2x2 cells.
// - A missing tile gives all-zero UVs; a tile whose material was not placed
//   in the atlas maps to the region at the atlas origin.
// - The cell diagonal flip: horizontal/vertical blends flip only when the
//   3-way "flipped" bit is set, uninverted right diagonals and inverted left
//   diagonals always flip, custom-edge blends never flip and have zero alpha
//   (they are drawn by the separate edge pass). A cell whose UVs were
//   adjusted for a cliff instead flips when |h0 - h2| > |h1 - h3|.
// - Cliff adjustment (explicit cliff UVs and the old stretch correction) only
//   runs when options.adjustCliffTextures is set (GameData AdjustCliffTextures,
//   off by default in the original). Explicit cliff UVs are in the level as
//   16.16 fixed point, so they carry up to 1/65536 rounding from the file's
//   32-bit floats. The cliff V scale is the integer quotient
//   AtlasWidth / atlas height, as in the original.
// - The extra (3-way) layer's diagonal: its own flip, or, when its UVs were
//   cliff-adjusted, the same height rule (from the original consumer).
export namespace engine::level::presentation
{
struct TextureCoordinate
{
	float u{0};
	float v{0};
	bool operator==(const TextureCoordinate &) const = default;
};

using CornerCoordinates = std::array<TextureCoordinate, 4>;

// One overlay of a cell: texture coordinates into the tile atlas and the
// per-corner opacity of that layer (0 transparent .. 255 opaque).
struct BlendLayer
{
	CornerCoordinates uv{};
	std::array<std::uint8_t, 4> alpha{};
	// Split the quad along corners 1-3 instead of 0-2.
	bool alternateDiagonal{false};
	// The UVs were adjusted for a cliff (explicit cliff UVs or stretch fix).
	bool cliffAdjusted{false};
	// Blend using a custom edge material (index into edgeMaterials), or -1.
	std::int32_t edgeMaterial{-1};
};

struct CellTexturing
{
	// Base tile (getUVData); cliff-adjusted when enabled.
	CornerCoordinates base{};
	// First blend layer (getAlphaUVData). With no blend it repeats the base
	// UVs with zero alpha. blend.alternateDiagonal is the cell's diagonal.
	BlendLayer blend{};
	// Optional 3-way blend layer drawn on top (getExtraAlphaUVData).
	bool hasExtraBlend{false};
	BlendLayer extraBlend{};
	// The level maps explicit cliff UVs onto this cell.
	bool cliffMapped{false};
	// The cell is flagged as a cliff (passability flag, for debug views).
	bool cliff{false};
};

struct TerrainTexturingOptions
{
	// GameData AdjustCliffTextures (the original default is off).
	bool adjustCliffTextures{false};
	// GameData Use3WayTerrainBlends (the original default is on). When off,
	// extra blends and the forced-flip bit are ignored.
	bool use3WayBlends{true};
};

struct TerrainSurfaceTextures
{
	// Tile atlas, sampled by base and blend layers (RGBA8, top-down).
	AtlasImage atlas;
	// Edge atlas for custom edge blends (RGBA8, top-down).
	AtlasImage edgeAtlas;
	std::vector<MaterialPlacement> materials;
	std::vector<MaterialPlacement> edgeMaterials;
	std::uint32_t cellsWide{0};
	std::uint32_t cellsHigh{0};
	std::vector<CellTexturing> cells; // row-major, cellsWide * cellsHigh
	// Each tile's colour, mipped to one pixel (TileData::getRGBDataForWidth(1)): the average of its pixels as placed
	// in the atlas (a half-size tile: its quarter); black for a tile that was not placed. By tile (index >> 2).
	std::vector<std::array<std::uint8_t, 3>> tileColors;

	const CellTexturing &Cell(std::uint32_t x, std::uint32_t y) const { return cells[static_cast<std::size_t>(y) * cellsWide + x]; }

	// UV rectangle of an edge material in the edge atlas (getUVForBlend):
	// {low corner, high corner}.
	std::array<TextureCoordinate, 2> EdgeMaterialRange(std::size_t edgeMaterial) const
	{
		if (edgeMaterial >= edgeMaterials.size() || edgeAtlas.height == 0)
			return {};
		const MaterialPlacement &placement = edgeMaterials[edgeMaterial];
		const float width = static_cast<float>(AtlasWidth);
		const float height = static_cast<float>(edgeAtlas.height);
		const float extent = static_cast<float>(placement.widthInTiles * TilePixelExtent);
		return {TextureCoordinate{static_cast<float>(placement.x) / width, static_cast<float>(placement.y) / height},
			TextureCoordinate{(static_cast<float>(placement.x) + extent) / width, (static_cast<float>(placement.y) + extent) / height}};
	}
};

namespace detail
{
inline constexpr std::uint8_t InvertedBit = 0x1;
inline constexpr std::uint8_t FlippedBit = 0x2;

// Terrain texturing context for one surface (the original height map state).
class SurfaceTexturer
{
public:
	SurfaceTexturer(const Heightfield &terrain, const TerrainSurface &surface, const PackedAtlas &atlas, TerrainTexturingOptions options) :
		m_surface(surface), m_atlas(atlas), m_options(options),
		m_width(static_cast<std::int32_t>(terrain.width)),
		m_samples(static_cast<std::int64_t>(terrain.width) * terrain.height),
		m_atlasHeight(static_cast<std::int32_t>(atlas.image.height))
	{
		m_rawHeights.reserve(terrain.heights.size());
		// Heights are stored bytes * 5/8; texturing works on the bytes.
		for (const auto &height : terrain.heights)
		{
			const std::int64_t raw = (height * Engine::Math::Fixed::FromInt(8) / Engine::Math::Fixed::FromInt(5)).Round();
			m_rawHeights.push_back(static_cast<std::int32_t>(std::clamp<std::int64_t>(raw, 0, 255)));
		}
	}

	std::int32_t RawHeight(std::int32_t x, std::int32_t y) const
	{
		const std::int64_t index = static_cast<std::int64_t>(y) * m_width + x;
		return index >= 0 && index < static_cast<std::int64_t>(m_rawHeights.size()) ? m_rawHeights[static_cast<std::size_t>(index)] : 0;
	}

	// getUVForNdx: the quarter of the tile the tile index selects.
	void TileRange(std::uint32_t tileIndex, float &minU, float &minV, float &maxU, float &maxV) const
	{
		const std::size_t tile = tileIndex >> 2;
		if (tile >= m_atlas.tiles.size() || m_atlas.tiles[tile].image == nullptr)
		{
			minU = minV = maxU = maxV = 0.0f;
			return;
		}
		const TileSource &source = m_atlas.tiles[tile];
		const std::int32_t x = source.placed ? source.x : 0;
		const std::int32_t y = source.placed ? source.y : 0;
		minU = static_cast<float>(x);
		minV = static_cast<float>(y);
		maxU = minU + static_cast<float>(TilePixelExtent);
		maxV = minV + static_cast<float>(TilePixelExtent);
		minU /= static_cast<float>(AtlasWidth);
		minV /= static_cast<float>(m_atlasHeight);
		maxU /= static_cast<float>(AtlasWidth);
		maxV /= static_cast<float>(m_atlasHeight);
		const float midX = (minU + maxU) / 2;
		const float midY = (minV + maxV) / 2;
		if (tileIndex & 2)
			maxV = midY;
		else
			minV = midY;
		if (tileIndex & 1)
			minU = midX;
		else
			maxU = midX;
	}

	// Material containing a tile (by the level's material ranges), or -1.
	std::int32_t MaterialOf(std::uint32_t tile) const
	{
		for (std::size_t index = 0; index < m_surface.materials.size(); ++index)
		{
			const TerrainMaterial &material = m_surface.materials[index];
			if (tile >= material.firstTile && tile < static_cast<std::uint64_t>(material.firstTile) + material.tileCount)
				return static_cast<std::int32_t>(index);
		}
		return -1;
	}

	float MaterialU(std::int32_t material) const
	{
		const MaterialPlacement &placement = m_atlas.materials[static_cast<std::size_t>(material)];
		return static_cast<float>(placement.placed ? placement.x : 0);
	}
	float MaterialV(std::int32_t material) const
	{
		const MaterialPlacement &placement = m_atlas.materials[static_cast<std::size_t>(material)];
		return static_cast<float>(placement.placed ? placement.y : 0);
	}
	std::int32_t MaterialExtent(std::int32_t material) const
	{
		return static_cast<std::int32_t>(m_surface.materials[static_cast<std::size_t>(material)].widthInTiles) * TilePixelExtent;
	}

	// getUVForTileIndex: UVs of a tile index for sample `index`; returns
	// whether they were adjusted for a cliff (or the cliff mapping's flip).
	bool TileCoordinates(std::int64_t index, std::uint32_t tileIndex, CornerCoordinates &uv) const
	{
		if (index >= m_samples)
			return false;
		float nU = 0, nV = 0, xU = 0, xV = 0;
		TileRange(tileIndex, nU, nV, xU, xV);
		std::array<float, 4> U{nU, xU, xU, nU};
		std::array<float, 4> V{xV, xV, nV, nV};
		const auto store = [&] {
			for (int corner = 0; corner < 4; ++corner)
				uv[corner] = {U[corner], V[corner]};
		};
		store();
		if (!m_options.adjustCliffTextures)
			return false;
		if (nU == 0.0f)
			return false; // missing texture
		const std::size_t sample = static_cast<std::size_t>(index);
		const std::uint16_t cliffIndex = sample < m_surface.cliffs.size() ? m_surface.cliffs[sample] : 0;
		if (cliffIndex != 0 && cliffIndex < m_surface.cliffTable.size())
		{
			const CliffMapping &info = m_surface.cliffTable[cliffIndex];
			const std::int32_t material = MaterialOf(tileIndex >> 2);
			bool tilesMatch = false;
			if (material >= 0)
			{
				const TerrainMaterial &range = m_surface.materials[static_cast<std::size_t>(material)];
				const std::uint32_t other = info.tile >> 2;
				tilesMatch = other >= range.firstTile && other < static_cast<std::uint64_t>(range.firstTile) + range.tileCount;
			}
			if (tilesMatch)
			{
				float minU = MaterialU(material);
				float maxV = MaterialV(material) + static_cast<float>(MaterialExtent(material));
				minU /= static_cast<float>(AtlasWidth);
				maxV /= static_cast<float>(m_atlasHeight);
				// Integer quotient, as the original.
				const float vFactor = static_cast<float>(AtlasWidth / m_atlasHeight);
				for (int corner = 0; corner < 4; ++corner)
				{
					U[corner] = Engine::Math::ToFloat(info.uv[static_cast<std::size_t>(corner) * 2]) + minU;
					V[corner] = Engine::Math::ToFloat(info.uv[static_cast<std::size_t>(corner) * 2 + 1]) * vFactor + maxV;
				}
				store();
				return info.flip;
			}
		}
		const bool adjusted = StretchForSlope(index, tileIndex, nU, nV, xU, xV, U, V);
		store();
		return adjusted;
	}

	// The original "old uv adjustment for cliffs": re-spreads a tile over a
	// steep cell so that it does not smear; needed for smooth diagonal slopes.
	bool StretchForSlope(std::int64_t index, std::uint32_t tileIndex, float nU, float nV, float xU, float xV, std::array<float, 4> &U,
		std::array<float, 4> &V) const
	{
		constexpr float StretchLimit = 1.5f;
		constexpr float TileLimit = 4.0f;
		constexpr float TallStretchLimit = 2.0f;
		constexpr float DiamondStretchLimit = 2.4f;
		constexpr float HeightScale = (10.0f / 16.0f) / 10.0f;
		if (index >= m_samples - m_width - 1)
			return true;
		const std::size_t at = static_cast<std::size_t>(index);
		const std::int32_t h0 = m_rawHeights[at];
		const std::int32_t h1 = m_rawHeights[at + 1];
		const std::int32_t h2 = m_rawHeights[at + static_cast<std::size_t>(m_width) + 1];
		const std::int32_t h3 = m_rawHeights[at + static_cast<std::size_t>(m_width)];
		const std::int32_t minH = std::min({h0, h1, h2, h3});
		const std::int32_t maxH = std::max({h0, h1, h2, h3});
		const std::int32_t deltaH = maxH - minH;
		std::int32_t below = 0, above = 0;
		const std::int32_t belowLimit = minH + (2 * deltaH + 1) / 3;
		const std::int32_t aboveLimit = minH + (deltaH + 1) / 3;
		for (const std::int32_t h : {h0, h1, h2, h3})
		{
			if (h < belowLimit)
				++below;
			if (h > aboveLimit)
				++above;
		}
		const float stretch = static_cast<float>(deltaH) * HeightScale;
		if (stretch < StretchLimit)
			return false;
		const std::int32_t material = MaterialOf(tileIndex >> 2);
		if (material < 0)
			return false;
		float nUb = MaterialU(material);
		float nVb = MaterialV(material);
		float xUb = nUb + static_cast<float>(MaterialExtent(material));
		float xVb = nVb + static_cast<float>(MaterialExtent(material));
		nUb /= static_cast<float>(AtlasWidth);
		nVb /= static_cast<float>(m_atlasHeight);
		xUb /= static_cast<float>(AtlasWidth);
		xVb /= static_cast<float>(m_atlasHeight);
		float divisor = TileLimit / stretch;
		if (divisor > TileLimit)
			divisor = TileLimit;
		if (divisor < 1.0f)
			divisor = 1.0f;
		const float deltaV = xVb - nVb;
		if (above != 1 && below != 1 && (above != 2 || below != 2))
		{
			// Diamond shaped: the fix is not that appealing either.
			if (stretch < DiamondStretchLimit)
				return false;
		}
		if (below == 1 || above > below)
		{
			// One low corner.
			if (h0 == minH)
				V[0] = nV + deltaV / divisor;
			else if (h1 == minH)
				V[1] = nV + deltaV / divisor;
			else if (h2 == minH)
				V[2] = xV - deltaV / divisor;
			else if (h3 == minH)
				V[3] = xV - deltaV / divisor;
		}
		else if (above == 1 || below > above)
		{
			// One high corner.
			if (h0 == maxH)
				V[0] = nV + deltaV / divisor;
			else if (h1 == maxH)
				V[1] = nV + deltaV / divisor;
			else if (h2 == maxH)
				V[2] = xV - deltaV / divisor;
			else if (h3 == maxH)
				V[3] = xV - deltaV / divisor;
		}
		else
		{
			// Two up, two down.
			if (stretch < TallStretchLimit)
				return false;
			const auto side = [](std::int32_t delta) {
				float length = static_cast<float>(delta) * HeightScale;
				length = std::sqrt(1 + length * length);
				if (length < StretchLimit)
					length = 1.0f;
				if (length > TileLimit)
					length = TileLimit;
				return length;
			};
			float dx = side(h3 - h2) * (xU - nU);
			float dy = side(h3 - h0) * (xV - nV);
			U = {nU, nU + dx, nU + dx, nU};
			V = {nV + dy, nV + dy, nV, nV};
			dx = side(h1 - h0) * (xU - nU);
			dy = side(h2 - h1) * (xV - nV);
			U[1] = U[0] + dx;
			V[1] = V[3] + dy;
		}
		// Keep within the material's block.
		float adjU = 0, adjV = 0;
		for (int corner = 0; corner < 4; ++corner)
			if (nVb - V[corner] > adjV)
				adjV = nVb - V[corner];
		for (int corner = 0; corner < 4; ++corner)
			V[corner] += adjV;
		adjV = 0;
		for (int corner = 0; corner < 4; ++corner)
		{
			if (U[corner] - xUb > adjU)
				adjU = U[corner] - xUb;
			if (V[corner] - xVb > adjV)
				adjV = V[corner] - xVb;
		}
		for (int corner = 0; corner < 4; ++corner)
		{
			U[corner] -= adjU;
			V[corner] -= adjV;
		}
		return true;
	}

	// The blend alpha pattern and forced flip shared by both blend layers.
	void BlendPattern(const TerrainBlend &blend, BlendLayer &layer) const
	{
		const std::uint8_t inverted = m_options.use3WayBlends ? blend.inverted : static_cast<std::uint8_t>(blend.inverted & ~FlippedBit);
		const bool isInverted = (inverted & InvertedBit) != 0;
		auto &alpha = layer.alpha;
		bool flip = false;
		alpha = {0, 0, 0, 0};
		if (blend.horizontal)
		{
			flip = (inverted & FlippedBit) != 0;
			if (isInverted)
				alpha[0] = alpha[3] = 255;
			else
				alpha[1] = alpha[2] = 255;
		}
		if (blend.vertical)
		{
			flip = (inverted & FlippedBit) != 0;
			if (isInverted)
				alpha[0] = alpha[1] = 255;
			else
				alpha[2] = alpha[3] = 255;
		}
		if (blend.rightDiagonal)
		{
			if (isInverted)
			{
				alpha[1] = 255;
				if (blend.longDiagonal)
					alpha[0] = alpha[2] = 255;
			}
			else
			{
				flip = true;
				alpha[2] = 255;
				if (blend.longDiagonal)
					alpha[1] = alpha[3] = 255;
			}
		}
		if (blend.leftDiagonal)
		{
			if (isInverted)
			{
				flip = true;
				alpha[0] = 255;
				if (blend.longDiagonal)
					alpha[1] = alpha[3] = 255;
			}
			else
			{
				alpha[3] = 255;
				if (blend.longDiagonal)
					alpha[0] = alpha[2] = 255;
			}
		}
		layer.edgeMaterial = blend.edgeMaterial >= 0 ? blend.edgeMaterial : -1;
		if (blend.edgeMaterial >= 0)
		{
			alpha = {0, 0, 0, 0};
			flip = false;
		}
		layer.alternateDiagonal = flip;
	}

	const TerrainBlend *Blend(std::uint16_t index) const
	{
		return index != 0 && index < m_surface.blendTable.size() ? &m_surface.blendTable[index] : nullptr;
	}

	bool HeightFlip(std::int32_t x, std::int32_t y) const
	{
		const std::int32_t p0 = RawHeight(x, y), p1 = RawHeight(x + 1, y);
		const std::int32_t p2 = RawHeight(x + 1, y + 1), p3 = RawHeight(x, y + 1);
		return std::abs(p0 - p2) > std::abs(p1 - p3);
	}

	CellTexturing Cell(std::int32_t x, std::int32_t y) const
	{
		CellTexturing cell;
		const std::int64_t index = static_cast<std::int64_t>(y) * m_width + x;
		const std::size_t sample = static_cast<std::size_t>(index);
		const std::uint32_t tile = sample < m_surface.tiles.size() ? m_surface.tiles[sample] : 0;
		TileCoordinates(index, tile, cell.base);

		// getAlphaUVData
		const TerrainBlend *blend = Blend(sample < m_surface.blends.size() ? m_surface.blends[sample] : 0);
		if (blend == nullptr)
			cell.blend.cliffAdjusted = TileCoordinates(index, tile, cell.blend.uv);
		else
		{
			cell.blend.cliffAdjusted = TileCoordinates(index, blend->tile, cell.blend.uv);
			BlendPattern(*blend, cell.blend);
		}
		if (cell.blend.cliffAdjusted)
			cell.blend.alternateDiagonal = HeightFlip(x, y);

		// getExtraAlphaUVData
		const TerrainBlend *extra =
			m_options.use3WayBlends ? Blend(sample < m_surface.extraBlends.size() ? m_surface.extraBlends[sample] : 0) : nullptr;
		if (extra != nullptr)
		{
			cell.hasExtraBlend = true;
			cell.extraBlend.cliffAdjusted = TileCoordinates(index, extra->tile, cell.extraBlend.uv);
			BlendPattern(*extra, cell.extraBlend);
			cell.extraBlend.alternateDiagonal = cell.extraBlend.alternateDiagonal || (cell.extraBlend.cliffAdjusted && HeightFlip(x, y));
		}

		cell.cliffMapped = sample < m_surface.cliffs.size() && m_surface.cliffs[sample] != 0;
		const std::size_t flagByte = static_cast<std::size_t>(y) * m_surface.cliffFlagBytesPerRow + static_cast<std::size_t>(x >> 3);
		cell.cliff = flagByte < m_surface.cliffFlags.size() && ((m_surface.cliffFlags[flagByte] >> (x & 7)) & 1) != 0;
		return cell;
	}

private:
	const TerrainSurface &m_surface;
	const PackedAtlas &m_atlas;
	TerrainTexturingOptions m_options;
	std::int32_t m_width;
	std::int64_t m_samples;
	std::int32_t m_atlasHeight;
	std::vector<std::int32_t> m_rawHeights;
};
}

// Builds both atlases and the texturing of every cell. `images` supplies the
// decoded picture of each material by name; materials without one render as
// missing (zero UVs).
inline TerrainSurfaceTextures BuildTerrainSurfaceTextures(const Heightfield &terrain, const TerrainSurface &surface,
	const MaterialImages &images, TerrainTexturingOptions options = {})
{
	TerrainSurfaceTextures result;
	PackedAtlas tiles = BuildTileAtlas(surface, images);
	PackedAtlas edges = BuildEdgeAtlas(surface, images);
	if (terrain.width >= 2 && terrain.height >= 2)
	{
		result.cellsWide = terrain.width - 1;
		result.cellsHigh = terrain.height - 1;
		const detail::SurfaceTexturer texturer(terrain, surface, tiles, options);
		result.cells.reserve(static_cast<std::size_t>(result.cellsWide) * result.cellsHigh);
		for (std::uint32_t y = 0; y < result.cellsHigh; ++y)
			for (std::uint32_t x = 0; x < result.cellsWide; ++x)
				result.cells.push_back(texturer.Cell(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)));
	}
	result.tileColors.assign(tiles.tiles.size(), {0, 0, 0});
	for (std::size_t index = 0; index < tiles.tiles.size(); ++index)
	{
		const TileSource &tile = tiles.tiles[index];
		if (!tile.placed || tile.x <= 0 || tile.image == nullptr)
			continue;
		const std::int32_t covered = tile.halfTile ? TilePixelExtent / 2 : TilePixelExtent;
		std::array<std::uint64_t, 3> sum{};
		std::uint64_t samples = 0;
		for (std::int32_t cy = 0; cy < covered; ++cy)
			for (std::int32_t cx = 0; cx < covered; ++cx)
			{
				const std::int32_t x = tile.x + cx, y = tile.y + TilePixelExtent - 1 - cy;
				if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(tiles.image.width) || y >= static_cast<std::int32_t>(tiles.image.height))
					continue;
				const auto pixel = tiles.image.At(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
				for (std::size_t c = 0; c < 3; ++c)
					sum[c] += pixel[c];
				++samples;
			}
		if (samples != 0)
			for (std::size_t c = 0; c < 3; ++c)
				result.tileColors[index][c] = static_cast<std::uint8_t>(sum[c] / samples);
	}
	result.atlas = std::move(tiles.image);
	result.materials = std::move(tiles.materials);
	result.edgeAtlas = std::move(edges.image);
	result.edgeMaterials = std::move(edges.materials);
	return result;
}
}
