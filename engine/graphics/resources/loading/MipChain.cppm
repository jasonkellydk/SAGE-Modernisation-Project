export module Graphics.Resources.MipChain;
import std;

export import Graphics.RHI;

// A texture uploaded with its mipmaps (the original's MIP_LEVELS_ALL / MIP_LEVELS_n textures, D3DXFilterTexture with a
// box filter): each level half the one above (at least 1), every texel the average of the up to four texels it covers;
// `levels` 0 makes the whole chain down to 1x1. RGBA8, rows `rowPitch` bytes apart. Built on the CPU and uploaded level
// by level, the same on every backend. None: the device refused it.
export namespace Graphics
{
inline std::uint32_t Full_Mip_Count(std::uint32_t width, std::uint32_t height) noexcept
{
	std::uint32_t levels = 1;
	while ((width > 1 || height > 1) && levels < 15)
	{
		width = std::max(width / 2, 1u);
		height = std::max(height / 2, 1u);
		++levels;
	}
	return levels;
}

// The next level down of an RGBA8 image (tightly packed rows), box filtered.
inline std::vector<std::byte> Box_Filter_Level(std::span<const std::byte> pixels, std::uint32_t width, std::uint32_t height, std::uint32_t &outWidth,
	std::uint32_t &outHeight)
{
	outWidth = std::max(width / 2, 1u);
	outHeight = std::max(height / 2, 1u);
	std::vector<std::byte> next(static_cast<std::size_t>(outWidth) * outHeight * 4);
	for (std::uint32_t y = 0; y < outHeight; ++y)
		for (std::uint32_t x = 0; x < outWidth; ++x)
		{
			const std::uint32_t x0 = std::min(x * 2, width - 1), x1 = std::min(x * 2 + 1, width - 1);
			const std::uint32_t y0 = std::min(y * 2, height - 1), y1 = std::min(y * 2 + 1, height - 1);
			for (std::uint32_t channel = 0; channel < 4; ++channel)
			{
				const auto at = [&](std::uint32_t px, std::uint32_t py) {
					return static_cast<std::uint32_t>(pixels[(static_cast<std::size_t>(py) * width + px) * 4 + channel]);
				};
				const std::uint32_t sum = at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1);
				next[(static_cast<std::size_t>(y) * outWidth + x) * 4 + channel] = static_cast<std::byte>((sum + 2) / 4);
			}
		}
	return next;
}

inline RHITextureHandle Create_Mipped_Texture(Device &device, std::uint32_t width, std::uint32_t height, std::span<const std::byte> pixels,
	std::uint32_t rowPitch, std::uint32_t levels = 0)
{
	if (width == 0 || height == 0 || rowPitch < width * 4 || pixels.size() < static_cast<std::size_t>(rowPitch) * (height - 1) + width * 4)
		return {};
	const std::uint32_t full = Full_Mip_Count(width, height);
	levels = levels == 0 ? full : std::min(levels, full);
	RHITexture description{width, height, levels, RHITextureFormat::RGBA8_UNorm, static_cast<std::uint32_t>(RHITextureUsage::ShaderResource)};
	const RHITextureHandle texture = device.Create_Texture(description);
	if (!texture.Is_Valid())
		return {};
	// Level 0 packed tightly, then each level from the one above.
	std::vector<std::byte> level(static_cast<std::size_t>(width) * height * 4);
	for (std::uint32_t row = 0; row < height; ++row)
		std::memcpy(level.data() + static_cast<std::size_t>(row) * width * 4, pixels.data() + static_cast<std::size_t>(row) * rowPitch, width * 4);
	std::uint32_t w = width, h = height;
	for (std::uint32_t mip = 0; mip < levels; ++mip)
	{
		if (!device.Update_Texture(texture, {std::span<const std::byte>(level), w * 4, w * h * 4, mip, 0}))
		{
			device.Destroy_Texture(texture);
			return {};
		}
		if (mip + 1 < levels)
		{
			std::uint32_t nw = 0, nh = 0;
			level = Box_Filter_Level(level, w, h, nw, nh);
			w = nw;
			h = nh;
		}
	}
	return texture;
}
}
