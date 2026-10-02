export module games.generalszh.presentation.rendering.shroud_pixels;
import std;

export import engine.gameplay.rts.vision.resources.shroud_map;
import Engine.Core.Math.FixedPresentation;
export import Graphics.RHI;

// The shroud as W3DShroud draws it, for the viewing player: each partition cell one texel of a 16-bit (R5G6B5) image
// the world's surfaces are multiplied by. A cell's level is W3DDisplay::setShroudLevel's (shrouded: ShroudAlpha,
// fogged: FogAlpha, clear: ClearAlpha), never darker than ShroudAlpha; its colour ShroudColor scaled by level/255
// (setShroudLevel / fillShroudData), a level of 255 fully lit. The texels around the map are the border's
// (fillBorderShroudData: shrouded, or clear once a script disabled the border shroud). The projection takes a world
// point to the image as W3DShroud's (one texel in: the border).
export namespace generalszh::presentation
{
struct ShroudLook
{
	std::array<std::uint8_t, 3> color{255, 255, 255}; // ShroudColor
	std::uint8_t clearAlpha{255};
	std::uint8_t fogAlpha{127};
	std::uint8_t shroudAlpha{0};
};

// setShroudLevel's texel for `level`.
constexpr std::uint16_t ShroudPixel(std::uint8_t level, const ShroudLook &look) noexcept
{
	if (level < look.shroudAlpha)
		level = look.shroudAlpha;
	auto blue = static_cast<std::uint32_t>(static_cast<float>(level) * (static_cast<float>(look.color[2]) / 255.0f));
	auto green = static_cast<std::uint32_t>(static_cast<float>(level) * (static_cast<float>(look.color[1]) / 255.0f));
	auto red = static_cast<std::uint32_t>(static_cast<float>(level) * (static_cast<float>(look.color[0]) / 255.0f));
	if (level == 255)
		red = green = blue = 255; // unshrouded pixels should be fully lit
	return static_cast<std::uint16_t>(((blue & 0xF8u) >> 3) | ((green & 0xFCu) << 3) | ((red & 0xF8u) << 8));
}

// W3DDisplay::setShroudLevel: a cell's level by what the viewer knows of it.
constexpr std::uint8_t ShroudLevel(engine::gameplay::CellShroud status, const ShroudLook &look) noexcept
{
	return status == engine::gameplay::CellShroud::Shrouded ? look.shroudAlpha
		: status == engine::gameplay::CellShroud::Fogged    ? look.fogAlpha
															: look.clearAlpha;
}

struct ShroudCells
{
	std::vector<std::uint16_t> pixels; // row by row, `width` a row
	std::uint32_t width{0};
	std::uint32_t height{0};
	std::uint16_t border{0};
	float cellSize{0.0f};
};

// The viewer's cells and the border (`borderClear`: DISABLE_BORDER_SHROUD, the border at ClearAlpha).
inline void FillShroudCells(const engine::gameplay::ShroudMap &shroud, std::uint32_t player, const ShroudLook &look, bool borderClear, ShroudCells &out)
{
	out.width = static_cast<std::uint32_t>(std::max(shroud.CellsX(), 0));
	out.height = static_cast<std::uint32_t>(std::max(shroud.CellsY(), 0));
	out.cellSize = Engine::Math::ToFloat(shroud.CellSize());
	out.border = ShroudPixel(borderClear ? look.clearAlpha : look.shroudAlpha, look);
	out.pixels.resize(static_cast<std::size_t>(out.width) * out.height);
	const std::array<std::uint16_t, 3> byStatus{ShroudPixel(ShroudLevel(engine::gameplay::CellShroud::Clear, look), look),
		ShroudPixel(ShroudLevel(engine::gameplay::CellShroud::Fogged, look), look), ShroudPixel(ShroudLevel(engine::gameplay::CellShroud::Shrouded, look), look)};
	for (std::uint32_t y = 0; y < out.height; ++y)
		for (std::uint32_t x = 0; x < out.width; ++x)
			out.pixels[static_cast<std::size_t>(y) * out.width + x] =
				byStatus[static_cast<std::size_t>(shroud.Status(player, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)))];
}

// The shroud image as the renderers take it this frame (none: nothing shrouded).
struct ShroudBinding
{
	Graphics::RHITextureHandle texture;
	std::array<float, 4> projection{};

	bool Active() const noexcept { return texture.Is_Valid(); }
};

// The shaders' uv = world.xy * P.xy + P.zw for an image `textureWidth` x `textureHeight` whose cells start one texel in
// (W3DShroud's projection with the map's origin at 0).
constexpr std::array<float, 4> ShroudProjection(float cellSize, std::uint32_t textureWidth, std::uint32_t textureHeight) noexcept
{
	const float sx = 1.0f / (cellSize * static_cast<float>(textureWidth));
	const float sy = 1.0f / (cellSize * static_cast<float>(textureHeight));
	return {sx, sy, cellSize * sx, cellSize * sy};
}
}
