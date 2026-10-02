export module engine.level.presentation.terrain_atlas;
import std;

export import engine.level.model.level;

// Terrain material images -> the tile atlases the terrain surface samples.
// A port of the original terrain texture preparation (tile cutting, atlas
// packing, periodic gutters): every material image is resampled to a square
// of widthInTiles * TilePixelExtent pixels, cut into tiles and packed into a
// fixed-width atlas so that each tile keeps its original pixel position.
//
// Images are RGBA8 with rows top-down (row 0 is the top of the picture), as
// any decoder produces them. The atlas pixels are RGBA8 top-down too: texture
// coordinate v = 0 is atlas row 0.
export namespace engine::level::presentation
{
// Pixel extent of one terrain tile in the atlas (one tile spans 2x2 cells).
inline constexpr std::int32_t TilePixelExtent = 256;
// Gutter between packed materials; half of it is a periodic border around each.
inline constexpr std::int32_t TileGutter = 32;
// Fixed width of both atlases.
inline constexpr std::int32_t AtlasWidth = 8192;
// Materials pack into a square grid of this many tile slots per side.
inline constexpr std::int32_t AtlasSlotsPerRow = AtlasWidth / (TilePixelExtent + TileGutter);
// Largest source image accepted (the original rejects larger ones as corrupt).
inline constexpr std::uint32_t MaxMaterialImageExtent = 8192;

struct MaterialImage
{
	std::uint32_t width{0};
	std::uint32_t height{0};
	std::vector<std::uint8_t> pixels; // RGBA8, rows top-down, width * height * 4 bytes

	bool Valid() const noexcept
	{
		return width > 0 && height > 0 && width <= MaxMaterialImageExtent && height <= MaxMaterialImageExtent &&
			pixels.size() >= static_cast<std::size_t>(width) * height * 4;
	}
};

// Images keyed by TerrainMaterial::name (exact match). A material without an
// image is "missing": its tiles are not drawn and cells using them get the
// missing-texture UVs (all zero).
struct MaterialImages
{
	std::unordered_map<std::string, MaterialImage> materials;
	std::unordered_map<std::string, MaterialImage> edgeMaterials;
};

struct AtlasImage
{
	std::uint32_t width{0};
	std::uint32_t height{0};
	std::vector<std::uint8_t> pixels; // RGBA8, rows top-down

	std::array<std::uint8_t, 4> At(std::uint32_t x, std::uint32_t y) const
	{
		const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4;
		return {pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
	}
};

// Where a material landed in its atlas (pixel origin of its top-left corner).
struct MaterialPlacement
{
	bool placed{false};
	std::int32_t x{0};
	std::int32_t y{0};
	std::int32_t widthInTiles{0};
};

// One cut tile: which image, resampled to a (rows x rows)-tile square, and
// which tile of that square (column, row counted from the bottom).
struct TileSource
{
	const MaterialImage *image{nullptr};
	std::int32_t rows{0};
	std::int32_t column{0};
	std::int32_t row{0};
	bool halfTile{false}; // a half-size (128x128) image fills only the lower-left quarter
	bool placed{false};
	std::int32_t x{0}; // top-left pixel of the tile in the atlas
	std::int32_t y{0};
};

struct PackedAtlas
{
	AtlasImage image;
	std::vector<MaterialPlacement> materials; // parallel to the surface's material list
	std::vector<TileSource> tiles;            // indexed by tile number
};

namespace detail
{
// Tiles per side the image holds (the original countTiles on the source file).
inline std::int32_t CountTiles(const MaterialImage &image) noexcept
{
	const std::int32_t across = static_cast<std::int32_t>(image.width) / TilePixelExtent;
	const std::int32_t down = static_cast<std::int32_t>(image.height) / TilePixelExtent;
	if (across > 10 || down > 10)
		return 0;
	const std::int32_t side = std::min(across, down);
	return side * side;
}

// Cuts one material into tiles (the original readTexClass/readTiles).
inline void CutMaterial(const TerrainMaterial &material, const MaterialImage *image, std::vector<TileSource> &tiles)
{
	if (image == nullptr || !image->Valid())
		return;
	const std::int32_t available = CountTiles(*image);
	const std::int64_t tileCount = material.tileCount;
	// The stored grid width wins; only a corrupt one falls back to the image.
	std::int64_t width = material.widthInTiles;
	if (width < 1 || width > 10 || tileCount < width * width)
	{
		width = 0;
		for (std::int64_t candidate = 10; candidate >= 1; --candidate)
			if (available >= candidate * candidate && tileCount >= candidate * candidate)
			{
				width = candidate;
				break;
			}
	}
	if (width <= 0 || tileCount <= 0)
		return;
	const bool half = width == 1 && image->width == TilePixelExtent / 2 && image->height == TilePixelExtent / 2;
	const std::int64_t count = width * width;
	const std::size_t needed = static_cast<std::size_t>(material.firstTile + count);
	if (tiles.size() < needed)
		tiles.resize(needed);
	for (std::int64_t index = 0; index < count; ++index)
	{
		TileSource &tile = tiles[static_cast<std::size_t>(material.firstTile + index)];
		tile.image = image;
		tile.rows = static_cast<std::int32_t>(width);
		tile.column = static_cast<std::int32_t>(index % width);
		tile.row = static_cast<std::int32_t>(index / width);
		tile.halfTile = half;
	}
}

using SlotGrid = std::array<std::array<bool, AtlasSlotsPerRow>, AtlasSlotsPerRow>;

// Finds a free width x width block of slots. Kept as the original: within a
// row only the first free slot is tried, even if the block does not fit there.
inline bool FindSlot(const SlotGrid &grid, std::int32_t width, std::int32_t &row, std::int32_t &column)
{
	for (row = 0; row < AtlasSlotsPerRow - width + 1; ++row)
	{
		for (column = 0; column < AtlasSlotsPerRow - width + 1; ++column)
		{
			if (!grid[row][column])
				continue;
			bool open = true;
			for (std::int32_t i = 0; i < width && open; ++i)
				for (std::int32_t j = 0; j < width && open; ++j)
					if (!grid[row + j][column + i])
						open = false;
			if (open)
				return true;
			break;
		}
	}
	return false;
}

// Places one material and its tiles; returns the pixel height it reaches.
inline std::int32_t PlaceMaterial(SlotGrid &grid, const TerrainMaterial &material, MaterialPlacement &placement,
	std::vector<TileSource> &tiles)
{
	const std::int32_t width = static_cast<std::int32_t>(std::min<std::uint32_t>(material.widthInTiles, AtlasSlotsPerRow + 1));
	placement = {};
	placement.widthInTiles = width;
	std::int32_t row = 0, column = 0;
	if (width > AtlasSlotsPerRow || !FindSlot(grid, width, row, column))
		return 0;
	placement.placed = true;
	placement.x = TileGutter / 2 + column * (TilePixelExtent + TileGutter);
	placement.y = TileGutter / 2 + row * (TilePixelExtent + TileGutter);
	for (std::int32_t i = 0; i < width; ++i)
		for (std::int32_t j = 0; j < width; ++j)
		{
			grid[row + j][column + i] = false;
			const std::size_t index = material.firstTile + static_cast<std::size_t>(i + j * width);
			if (index >= tiles.size() || tiles[index].image == nullptr)
				continue;
			tiles[index].placed = true;
			tiles[index].x = placement.x + i * TilePixelExtent;
			tiles[index].y = placement.y + (width - j - 1) * TilePixelExtent;
		}
	return placement.y + width * TilePixelExtent + TileGutter / 2;
}

inline std::uint32_t PowerOfTwoAtLeast(std::int32_t value) noexcept
{
	std::uint32_t result = 1;
	while (static_cast<std::int64_t>(result) < value)
		result *= 2;
	return result;
}

// Box-filter bins mapping output pixels to source pixels, bottom-up on both
// sides like the original (which decoded to lower-left origin first).
inline void Bins(std::int32_t outputExtent, std::int32_t sourceExtent, std::int32_t first, std::array<std::int32_t, TilePixelExtent> &lo,
	std::array<std::int32_t, TilePixelExtent> &hi)
{
	for (std::int32_t offset = 0; offset < TilePixelExtent; ++offset)
	{
		const std::int64_t output = first + offset;
		std::int32_t start = static_cast<std::int32_t>(output * sourceExtent / outputExtent);
		std::int32_t end = static_cast<std::int32_t>((output + 1) * sourceExtent / outputExtent);
		if (end <= start)
			end = start + 1;
		lo[offset] = std::min(start, sourceExtent - 1);
		hi[offset] = std::min(end, sourceExtent);
	}
}

// Writes one tile's 256x256 pixels. The tile cache is bottom-up and uploaded
// flipped, so tile row cy (from the bottom) lands on atlas row y + 255 - cy.
template <typename Store>
inline void WriteTile(const TileSource &tile, Store &&store)
{
	const MaterialImage &image = *tile.image;
	const std::int32_t sourceWidth = static_cast<std::int32_t>(image.width);
	const std::int32_t sourceHeight = static_cast<std::int32_t>(image.height);
	const std::int32_t extent = tile.halfTile ? TilePixelExtent / 2 : tile.rows * TilePixelExtent;
	std::array<std::int32_t, TilePixelExtent> x0{}, x1{}, y0{}, y1{};
	Bins(extent, sourceWidth, tile.column * TilePixelExtent, x0, x1);
	Bins(extent, sourceHeight, tile.row * TilePixelExtent, y0, y1);
	const std::int32_t covered = tile.halfTile ? TilePixelExtent / 2 : TilePixelExtent;
	for (std::int32_t cy = 0; cy < TilePixelExtent; ++cy)
		for (std::int32_t cx = 0; cx < TilePixelExtent; ++cx)
		{
			std::array<std::uint8_t, 4> pixel{0, 0, 0, 0};
			if (cx < covered && cy < covered)
			{
				std::array<std::uint32_t, 4> sum{};
				for (std::int32_t sy = y0[cy]; sy < y1[cy]; ++sy)
				{
					const std::size_t rowOffset = static_cast<std::size_t>(sourceHeight - 1 - sy) * image.width;
					for (std::int32_t sx = x0[cx]; sx < x1[cx]; ++sx)
					{
						const std::uint8_t *source = &image.pixels[(rowOffset + sx) * 4];
						for (int channel = 0; channel < 4; ++channel)
							sum[channel] += source[channel];
					}
				}
				const std::uint32_t samples = static_cast<std::uint32_t>((x1[cx] - x0[cx]) * (y1[cy] - y0[cy]));
				for (int channel = 0; channel < 4; ++channel)
					pixel[channel] = static_cast<std::uint8_t>((sum[channel] + samples / 2) / samples);
			}
			store(tile.x + cx, tile.y + TilePixelExtent - 1 - cy, pixel);
		}
}

inline PackedAtlas Pack(const std::vector<TerrainMaterial> &materials, const std::unordered_map<std::string, MaterialImage> &images,
	bool largestFirst, std::int32_t &usedHeight)
{
	PackedAtlas atlas;
	for (const TerrainMaterial &material : materials)
	{
		const auto found = images.find(material.name);
		CutMaterial(material, found == images.end() ? nullptr : &found->second, atlas.tiles);
	}
	SlotGrid grid;
	for (auto &row : grid)
		row.fill(true);
	atlas.materials.resize(materials.size());
	usedHeight = 0;
	const auto place = [&](std::size_t index) {
		usedHeight = std::max(usedHeight, PlaceMaterial(grid, materials[index], atlas.materials[index], atlas.tiles));
	};
	if (largestFirst)
	{
		// Terrain materials go widest first, then in declaration order; a
		// width outside 1..AtlasSlotsPerRow is never placed.
		for (std::int32_t width = AtlasSlotsPerRow; width > 0; --width)
			for (std::size_t index = 0; index < materials.size(); ++index)
				if (materials[index].widthInTiles == static_cast<std::uint32_t>(width))
					place(index);
	}
	else
	{
		for (std::size_t index = 0; index < materials.size(); ++index)
			place(index);
	}
	atlas.image.width = AtlasWidth;
	atlas.image.height = PowerOfTwoAtLeast(usedHeight);
	return atlas;
}
}

// The terrain tile atlas (the original terrain texture, which the blend pass
// also samples): opaque black background, tiles copied, then a 16-pixel
// periodic border around each placed material so filtering and mips wrap.
inline PackedAtlas BuildTileAtlas(const TerrainSurface &surface, const MaterialImages &images)
{
	std::int32_t usedHeight = 0;
	PackedAtlas atlas = detail::Pack(surface.materials, images.materials, true, usedHeight);
	AtlasImage &image = atlas.image;
	image.pixels.assign(static_cast<std::size_t>(image.width) * image.height * 4, 0);
	for (std::size_t offset = 3; offset < image.pixels.size(); offset += 4)
		image.pixels[offset] = 255;
	const auto store = [&](std::int32_t x, std::int32_t y, const std::array<std::uint8_t, 4> &pixel) {
		if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(image.width) || y >= static_cast<std::int32_t>(image.height))
			return;
		std::copy(pixel.begin(), pixel.end(), image.pixels.begin() + (static_cast<std::ptrdiff_t>(y) * image.width + x) * 4);
	};
	for (std::size_t index = 0; index < atlas.tiles.size() && index < surface.tileCount; ++index)
		if (atlas.tiles[index].placed && atlas.tiles[index].x > 0)
			detail::WriteTile(atlas.tiles[index], store);

	const std::int32_t border = TileGutter / 2;
	const std::size_t stride = static_cast<std::size_t>(image.width) * 4;
	const auto pixel = [&](std::int32_t x, std::int32_t y) { return image.pixels.data() + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4; };
	for (const MaterialPlacement &placement : atlas.materials)
	{
		if (!placement.placed || placement.x <= 0 || placement.widthInTiles <= 0)
			continue;
		const std::int32_t extent = placement.widthInTiles * TilePixelExtent;
		const std::int32_t x = placement.x, y = placement.y;
		if (y + extent + border > static_cast<std::int32_t>(image.height))
			continue;
		for (std::int32_t row = 0; row < extent; ++row)
		{
			std::copy_n(pixel(x + extent - border, y + row), border * 4, pixel(x - border, y + row));
			std::copy_n(pixel(x, y + row), border * 4, pixel(x + extent, y + row));
		}
		for (std::int32_t row = 0; row < border; ++row)
		{
			const std::size_t bytes = static_cast<std::size_t>(extent + 2 * border) * 4;
			std::copy_n(pixel(x - border, y + extent - 1 - row), bytes, pixel(x - border, y - 1 - row));
			std::copy_n(pixel(x - border, y + row), bytes, pixel(x - border, y + extent + row));
		}
	}
	return atlas;
}

// The blend-edge atlas (the original alpha edge texture) used by custom edge
// blends: a gradient background (alpha 128, red 255 - y/2, blue x/2), edge
// tiles with alpha rebuilt from colour: black -> 128, white -> 0, else 255.
// Edge materials pack in declaration order; no periodic borders.
inline PackedAtlas BuildEdgeAtlas(const TerrainSurface &surface, const MaterialImages &images)
{
	std::int32_t usedHeight = 0;
	PackedAtlas atlas = detail::Pack(surface.edgeMaterials, images.edgeMaterials, false, usedHeight);
	AtlasImage &image = atlas.image;
	image.pixels.resize(static_cast<std::size_t>(image.width) * image.height * 4);
	for (std::uint32_t y = 0; y < image.height; ++y)
		for (std::uint32_t x = 0; x < image.width; ++x)
		{
			std::uint8_t *target = &image.pixels[(static_cast<std::size_t>(y) * image.width + x) * 4];
			target[0] = static_cast<std::uint8_t>(255 - y / 2);
			target[1] = 0;
			target[2] = static_cast<std::uint8_t>(x / 2);
			target[3] = 128;
		}
	const auto store = [&](std::int32_t x, std::int32_t y, const std::array<std::uint8_t, 4> &pixel) {
		if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(image.width) || y >= static_cast<std::int32_t>(image.height))
			return;
		std::uint8_t *target = &image.pixels[(static_cast<std::size_t>(y) * image.width + x) * 4];
		const bool black = pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0;
		const bool white = pixel[0] == 255 && pixel[1] == 255 && pixel[2] == 255;
		target[0] = pixel[0];
		target[1] = pixel[1];
		target[2] = pixel[2];
		target[3] = black ? 128 : white ? 0 : 255;
	};
	for (std::size_t index = 0; index < atlas.tiles.size() && index < surface.edgeTileCount; ++index)
		if (atlas.tiles[index].placed && atlas.tiles[index].x > 0)
			detail::WriteTile(atlas.tiles[index], store);
	return atlas;
}
}
