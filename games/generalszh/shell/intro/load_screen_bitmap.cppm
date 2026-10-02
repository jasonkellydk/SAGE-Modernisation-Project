export module games.generalszh.shell.intro.load_screen_bitmap;
import std;

// The splash shown while the game loads (WinMain: LoadImage of Install_Final.bmp from the install's root, then the
// window's WM_PAINT BitBlts it at the client area's (0, 0), DEFAULT_XRESOLUTION x DEFAULT_YRESOLUTION (800 x 600)
// unscaled, as the window is made; the bitmap goes once the window is up, its pixels staying on screen until the engine
// draws). Its pixels as the renderer takes them: RGBA, top row first; what the blit covers is the bitmap's top-left
// 800 x 600 (less where the bitmap is smaller).
export namespace generalszh::shell
{
inline constexpr std::uint32_t LoadScreenWidth = 800;  // DEFAULT_XRESOLUTION
inline constexpr std::uint32_t LoadScreenHeight = 600; // DEFAULT_YRESOLUTION

struct LoadScreenBitmap
{
	std::uint32_t width{0}, height{0};
	std::vector<std::byte> rgba; // width * height * 4, top row first
};

// A Windows bitmap (BITMAPFILEHEADER + BITMAPINFOHEADER; uncompressed 24 or 32 bits, rows bottom-up unless the height is
// negative, each row padded to 4 bytes) as RGBA; none when it is not one LoadImage takes this way.
inline std::optional<LoadScreenBitmap> DecodeLoadScreenBitmap(std::span<const std::byte> bytes)
{
	const auto u16 = [&](std::size_t at) -> std::uint32_t {
		return static_cast<std::uint32_t>(bytes[at]) | static_cast<std::uint32_t>(bytes[at + 1]) << 8;
	};
	const auto u32 = [&](std::size_t at) -> std::uint32_t { return u16(at) | u16(at + 2) << 16; };
	if (bytes.size() < 54 || bytes[0] != std::byte{'B'} || bytes[1] != std::byte{'M'})
		return std::nullopt;
	const std::uint32_t offset = u32(10);
	const std::int32_t width = static_cast<std::int32_t>(u32(18));
	const std::int32_t rawHeight = static_cast<std::int32_t>(u32(22));
	const std::uint32_t bits = u16(28);
	const std::uint32_t compression = u32(30);
	if (width <= 0 || rawHeight == 0 || (bits != 24 && bits != 32) || (compression != 0 && !(compression == 3 && bits == 32)))
		return std::nullopt;
	const bool topDown = rawHeight < 0;
	const std::uint32_t height = static_cast<std::uint32_t>(topDown ? -static_cast<std::int64_t>(rawHeight) : rawHeight);
	const std::size_t stride = (static_cast<std::size_t>(width) * bits / 8 + 3) & ~std::size_t{3};
	if (offset > bytes.size() || bytes.size() - offset < stride * height)
		return std::nullopt;
	LoadScreenBitmap bitmap;
	bitmap.width = static_cast<std::uint32_t>(width);
	bitmap.height = height;
	bitmap.rgba.resize(static_cast<std::size_t>(width) * height * 4);
	const std::size_t step = bits / 8;
	for (std::uint32_t row = 0; row < height; ++row)
	{
		const std::size_t source = offset + stride * (topDown ? row : height - 1 - row);
		for (std::int32_t x = 0; x < width; ++x)
		{
			const std::size_t from = source + static_cast<std::size_t>(x) * step;
			const std::size_t to = (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
			bitmap.rgba[to + 0] = bytes[from + 2]; // stored blue, green, red
			bitmap.rgba[to + 1] = bytes[from + 1];
			bitmap.rgba[to + 2] = bytes[from + 0];
			bitmap.rgba[to + 3] = std::byte{0xFF}; // BitBlt SRCCOPY: opaque
		}
	}
	return bitmap;
}

// What the blit covers of the client area: from (0, 0), the bitmap's size up to 800 x 600.
inline std::array<std::uint32_t, 2> LoadScreenBlitSize(const LoadScreenBitmap &bitmap) noexcept
{
	return {std::min(bitmap.width, LoadScreenWidth), std::min(bitmap.height, LoadScreenHeight)};
}
}
