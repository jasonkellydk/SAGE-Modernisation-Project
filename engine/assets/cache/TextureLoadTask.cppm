module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Assets.Cache.TextureLoadTask;

import Assets.Identity;
import Assets.Importers.Models;
import Assets.Textures;

namespace Assets
{

export struct TextureLoadResult final
{
	std::shared_ptr<const TextureAsset> asset;
	std::string error;

	bool Succeeded() const noexcept
	{
		return asset != nullptr && error.empty();
	}
};

export TextureLoadResult Load_Texture_Asset(const AssetIdentity &identity, const AssetSource &source);

namespace TextureLoadDetail
{

std::string Source_Format(std::string_view name)
{
	const std::size_t extension = name.find_last_of('.');
	return extension == std::string_view::npos ? "unknown" : std::string(name.substr(extension + 1));
}

std::uint16_t Read_U16(const std::uint8_t *data) noexcept
{
	return static_cast<std::uint16_t>(data[0] | (static_cast<std::uint16_t>(data[1]) << 8));
}

bool Decode_TGA(
	std::span<const std::byte> source,
	std::uint32_t &width,
	std::uint32_t &height,
	std::vector<std::byte> &pixels)
{
	if (source.size() < 18)
		return false;

	const auto *header = reinterpret_cast<const std::uint8_t *>(source.data());
	const std::uint8_t color_map_type = header[1];
	const std::uint8_t image_type = header[2];
	const std::uint16_t image_width = Read_U16(header + 12);
	const std::uint16_t image_height = Read_U16(header + 14);
	const std::uint8_t pixel_depth = header[16];
	if (color_map_type != 0 || (image_type != 2 && image_type != 10)
		|| image_width == 0 || image_height == 0 || (pixel_depth != 24 && pixel_depth != 32))
		return false;

	const std::size_t bytes_per_pixel = pixel_depth / 8;
	std::size_t offset = 18 + header[0];
	if (offset > source.size())
		return false;
	width = image_width;
	height = image_height;
	pixels.resize(static_cast<std::size_t>(width) * height * 4);

	const bool top_origin = (header[17] & 0x20) != 0;
	std::size_t pixel_index = 0;
	while (pixel_index < static_cast<std::size_t>(width) * height) {
		std::size_t run_count = 1;
		bool run_length_packet = false;
		if (image_type == 10) {
			if (offset >= source.size())
				return false;
			const std::uint8_t packet = std::to_integer<std::uint8_t>(source[offset++]);
			run_length_packet = (packet & 0x80) != 0;
			run_count = (packet & 0x7f) + 1;
		}
		if (run_count > static_cast<std::size_t>(width) * height - pixel_index)
			return false;

		std::array<std::uint8_t, 4> color{};
		for (std::size_t run = 0; run < run_count; ++run) {
			if (!run_length_packet || run == 0) {
				if (offset + bytes_per_pixel > source.size())
					return false;
				const auto *encoded = reinterpret_cast<const std::uint8_t *>(source.data()) + offset;
				color[0] = encoded[2];
				color[1] = encoded[1];
				color[2] = encoded[0];
				color[3] = bytes_per_pixel == 4 ? encoded[3] : 255;
				offset += bytes_per_pixel;
			}

			const std::size_t source_y = pixel_index / width;
			const std::size_t destination_y = top_origin ? source_y : height - 1 - source_y;
			std::byte *destination = pixels.data() + (destination_y * width + pixel_index % width) * 4;
			for (std::size_t component = 0; component < 4; ++component)
				destination[component] = static_cast<std::byte>(color[component]);
			++pixel_index;
		}
	}
	return true;
}

std::array<std::uint8_t, 4> Decode_RGB565(std::uint16_t value) noexcept
{
	const std::uint8_t red = static_cast<std::uint8_t>(((value >> 11) & 0x1f) * 255 / 31);
	const std::uint8_t green = static_cast<std::uint8_t>(((value >> 5) & 0x3f) * 255 / 63);
	const std::uint8_t blue = static_cast<std::uint8_t>((value & 0x1f) * 255 / 31);
	return {red, green, blue, 255};
}

bool Decode_DDS(
	std::span<const std::byte> source,
	std::uint32_t &width,
	std::uint32_t &height,
	std::vector<std::byte> &pixels)
{
	if (source.size() < 128)
		return false;

	const auto *bytes = reinterpret_cast<const std::uint8_t *>(source.data());
	if (bytes[0] != 'D' || bytes[1] != 'D' || bytes[2] != 'S' || bytes[3] != ' ')
		return false;

	auto read_u32 = [bytes](std::size_t offset) noexcept {
		return static_cast<std::uint32_t>(bytes[offset])
			| (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
			| (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
			| (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
	};
	if (read_u32(4) != 124 || read_u32(76) != 32)
		return false;

	height = read_u32(12);
	width = read_u32(16);
	const std::uint32_t pixel_format_flags = read_u32(80);
	const std::uint32_t four_cc = read_u32(84);
	constexpr std::uint32_t dxt1 = 0x31545844;
	constexpr std::uint32_t dxt3 = 0x33545844;
	constexpr std::uint32_t dxt5 = 0x35545844;
	if (width == 0 || height == 0 || (pixel_format_flags & 0x4) == 0
		|| (four_cc != dxt1 && four_cc != dxt3 && four_cc != dxt5)
		|| width > (std::numeric_limits<std::size_t>::max() / 4) / height)
		return false;

	const std::size_t block_width = (static_cast<std::size_t>(width) + 3) / 4;
	const std::size_t block_height = (static_cast<std::size_t>(height) + 3) / 4;
	const std::size_t block_size = four_cc == dxt1 ? 8 : 16;
	if (block_width > std::numeric_limits<std::size_t>::max() / block_height
		|| block_width * block_height > (source.size() - 128) / block_size)
		return false;

	pixels.resize(static_cast<std::size_t>(width) * height * 4);
	for (std::size_t block_y = 0; block_y < block_height; ++block_y) {
		for (std::size_t block_x = 0; block_x < block_width; ++block_x) {
			const std::size_t block_offset = 128 + (block_y * block_width + block_x) * block_size;
            const std::size_t color_offset=block_offset+(four_cc==dxt1 ? 0 : 8);
			const std::uint16_t color0 = static_cast<std::uint16_t>(bytes[color_offset])
				| static_cast<std::uint16_t>(bytes[color_offset + 1] << 8);
			const std::uint16_t color1 = static_cast<std::uint16_t>(bytes[color_offset + 2])
				| static_cast<std::uint16_t>(bytes[color_offset + 3] << 8);
			const std::array<std::uint8_t, 4> first = Decode_RGB565(color0);
			const std::array<std::uint8_t, 4> second = Decode_RGB565(color1);
			std::array<std::array<std::uint8_t, 4>, 4> colors = {first, second, {}, {}};
			if (four_cc == dxt1 && color0 <= color1) {
				for (std::size_t channel = 0; channel < 3; ++channel) {
					colors[2][channel] = static_cast<std::uint8_t>((first[channel] + second[channel]) / 2);
					colors[3][channel] = 0;
				}
				colors[2][3] = 255;
			}
			else {
				for (std::size_t channel = 0; channel < 3; ++channel) {
					colors[2][channel] = static_cast<std::uint8_t>((2 * first[channel] + second[channel]) / 3);
					colors[3][channel] = static_cast<std::uint8_t>((first[channel] + 2 * second[channel]) / 3);
				}
				colors[2][3] = colors[3][3] = 255;
			}

			const std::uint32_t color_indices = read_u32(color_offset + 4);
			std::array<std::uint8_t, 16> alpha_indices{};
			if (four_cc == dxt3) {
				for (std::size_t pixel = 0; pixel < 16; ++pixel) {
					const std::uint8_t value = bytes[block_offset + (pixel / 2)];
					alpha_indices[pixel] = static_cast<std::uint8_t>((pixel & 1) == 0 ? value & 0x0f : value >> 4);
				}
			}
			else if (four_cc == dxt5) {
				const std::uint8_t alpha0 = bytes[block_offset + 0];
				const std::uint8_t alpha1 = bytes[block_offset + 1];
				std::array<std::uint8_t, 8> alpha_values = {alpha0, alpha1, 0, 0, 0, 0, 0, 0};
				if (alpha0 > alpha1) {
					for (std::size_t index = 2; index < 8; ++index)
						alpha_values[index] = static_cast<std::uint8_t>(((8 - index) * alpha0 + (index - 1) * alpha1) / 7);
				}
				else {
					for (std::size_t index = 2; index < 6; ++index)
						alpha_values[index] = static_cast<std::uint8_t>(((6 - index) * alpha0 + (index - 1) * alpha1) / 5);
					alpha_values[6] = 0;
					alpha_values[7] = 255;
				}
				std::uint64_t alpha_bits = 0;
				for (std::size_t index = 0; index < 6; ++index)
					alpha_bits |= static_cast<std::uint64_t>(bytes[block_offset + 2 + index]) << (index * 8);
				for (std::size_t pixel = 0; pixel < 16; ++pixel)
					alpha_indices[pixel] = alpha_values[(alpha_bits >> (pixel * 3)) & 0x7];
			}

			for (std::size_t local_y = 0; local_y < 4; ++local_y) {
				for (std::size_t local_x = 0; local_x < 4; ++local_x) {
					const std::size_t x = block_x * 4 + local_x;
					const std::size_t y = block_y * 4 + local_y;
					if (x >= width || y >= height)
						continue;
					const std::size_t pixel = local_y * 4 + local_x;
					const std::uint32_t color_index = (color_indices >> (pixel * 2)) & 0x3;
					std::byte *destination = pixels.data() + (y * width + x) * 4;
					for (std::size_t channel = 0; channel < 4; ++channel)
						destination[channel] = static_cast<std::byte>(colors[color_index][channel]);
					if (four_cc == dxt3)
						destination[3] = static_cast<std::byte>(alpha_indices[pixel] * 17);
					else if (four_cc == dxt5)
						destination[3] = static_cast<std::byte>(alpha_indices[pixel]);
				}
			}
		}
	}
	return true;
}

}

TextureLoadResult Load_Texture_Asset(const AssetIdentity &identity, const AssetSource &source)
{
	try {
		if (!source)
			return {nullptr, "no asset source is configured"};
		const std::vector<std::byte> bytes = source(identity);
		if (bytes.empty())
			return {nullptr, "texture source is empty"};

		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::vector<std::byte> pixels;
		if (!TextureLoadDetail::Decode_TGA(bytes, width, height, pixels))
			TextureLoadDetail::Decode_DDS(bytes, width, height, pixels);
		return {
			std::make_shared<const TextureAsset>(
				identity,
				TextureLoadDetail::Source_Format(identity.canonical_name),
				bytes.size(),
				width,
				height,
				width * 4,
				std::move(pixels)),
			{}};
	} catch (const std::exception &exception) {
		return {nullptr, exception.what()};
	} catch (...) {
		return {nullptr, "unknown exception while loading texture"};
	}
}

}
