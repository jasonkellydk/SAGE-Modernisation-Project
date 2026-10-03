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

struct PreparedMipLevel
{
	std::uint32_t width{}, height{};
	std::vector<std::byte> pixels;
};
struct PreparedMipChain
{
	std::vector<PreparedMipLevel> levels;
};

// CPU preparation is independent of the device and may run on a loader worker.
// Publication and GPU calls remain on the render thread.
inline PreparedMipChain Prepare_Mip_Chain(std::uint32_t width, std::uint32_t height, std::span<const std::byte> pixels,
	std::uint32_t rowPitch, std::uint32_t levels = 0, std::uint32_t reduction = 0, std::uint32_t minimum_dimension = 1)
{
	if (width == 0 || height == 0 || width > (std::numeric_limits<std::uint32_t>::max)()/4 ||
		rowPitch < width * 4 || pixels.size() < static_cast<std::uint64_t>(rowPitch) * (height - 1) + width * 4)
		return {};
	const std::uint32_t full = Full_Mip_Count(width, height);
	levels = levels == 0 ? full : std::min(levels, full);
	// Level 0 packed tightly, then each level from the one above.
	std::vector<std::byte> level(static_cast<std::size_t>(width) * height * 4);
	for (std::uint32_t row = 0; row < height; ++row)
		std::memcpy(level.data() + static_cast<std::size_t>(row) * width * 4, pixels.data() + static_cast<std::size_t>(row) * rowPitch, width * 4);
	std::uint32_t w = width, h = height;
	// Quality is supplied by the resource owner. A one-level image retains its
	// authored resolution; reducing a chain drops its leading levels without
	// resampling already filtered levels or changing other owners' textures.
	for(std::uint32_t skipped=0; skipped<reduction && levels>1 &&
		w>std::max(minimum_dimension,1u) && h>std::max(minimum_dimension,1u);++skipped) {
		std::uint32_t nw{},nh{};
		level=Box_Filter_Level(level,w,h,nw,nh);w=nw;h=nh;--levels;
	}
	PreparedMipChain chain;
	for (std::uint32_t mip = 0; mip < levels; ++mip)
	{
		chain.levels.push_back({w, h, std::move(level)});
		if (mip + 1 < levels)
		{
			std::uint32_t nw = 0, nh = 0;
			level = Box_Filter_Level(chain.levels.back().pixels, w, h, nw, nh);
			w = nw;
			h = nh;
		}
	}
	return chain;
}

inline RHITextureHandle Upload_Mip_Chain(Device &device, const PreparedMipChain &chain)
{
	if (chain.levels.empty() || chain.levels.size() > 15) return {};
	const auto &base = chain.levels.front();
	if (!base.width || !base.height || base.width > (std::numeric_limits<std::uint32_t>::max)() / 4) return {};
	std::uint32_t w = base.width, h = base.height;
	for (const auto &level : chain.levels) {
		if (level.width != w || level.height != h || level.pixels.size() != static_cast<std::uint64_t>(w) * h * 4) return {};
		w = std::max(w / 2, 1u); h = std::max(h / 2, 1u);
	}
	const RHITexture texture_description{base.width, base.height, static_cast<std::uint32_t>(chain.levels.size()),
		RHITextureFormat::RGBA8_UNorm, static_cast<std::uint32_t>(RHITextureUsage::ShaderResource)};
	const auto texture = device.Create_Texture(texture_description);
	if (!texture.Is_Valid()) return {};
	for (std::uint32_t mip = 0; mip < chain.levels.size(); ++mip) {
		const auto &level = chain.levels[mip];
		if (!device.Update_Texture(texture, {level.pixels, level.width * 4, level.width * level.height * 4, mip, 0})) {
			device.Destroy_Texture(texture); return {};
		}
	}
	return texture;
}

inline RHITextureHandle Create_Mipped_Texture(Device &device, std::uint32_t width, std::uint32_t height, std::span<const std::byte> pixels,
	std::uint32_t rowPitch, std::uint32_t levels = 0, std::uint32_t reduction = 0, std::uint32_t minimum_dimension = 1)
{
	return Upload_Mip_Chain(device, Prepare_Mip_Chain(width, height, pixels, rowPitch, levels, reduction, minimum_dimension));
}
}
