export module games.generalszh.presentation.rendering.texture_files;
import std;

import engine.filesystem.core.virtual_file_system;
import Assets.Adapters.DDS;
import Assets.Adapters.TGA.Image;
import Assets.Images.Preparation;

// Textures by file name, as the original finds them: anywhere under
// Art/Textures, the named file or (as shipped data stores most ".tga" names)
// the same stem as ".dds" or ".tga"; decoded to tightly packed RGBA8.
export namespace generalszh::presentation
{
struct TextureImage
{
	std::uint32_t width{0};
	std::uint32_t height{0};
	std::vector<std::byte> pixels; // RGBA8, tightly packed

	bool Valid() const noexcept { return width != 0 && height != 0 && !pixels.empty(); }
};

inline std::string LowerName(std::string_view value)
{
	std::string out(value);
	for (char &c : out)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

inline TextureImage DecodeTextureImage(std::span<const std::byte> bytes)
{
	TextureImage image;
	if (bytes.size() >= 4 && bytes[0] == std::byte{'D'} && bytes[1] == std::byte{'D'} && bytes[2] == std::byte{'S'} &&
		bytes[3] == std::byte{' '})
	{
		Assets::DDSLayout layout;
		std::vector<std::byte> pixels;
		if (!Assets::Read_DDS_Layout(bytes, bytes.size(), layout) || !Assets::Decode_DDS_Surface(bytes, layout, 0, 0, 0, pixels))
			return image;
		image.width = layout.Surface(0)->width;
		image.height = layout.Surface(0)->height;
		image.pixels = std::move(pixels);
		return image;
	}
	Assets::TGAImage tga;
	if (!Assets::Decode_TGA_Image(bytes, tga))
		return image;
	std::vector<Assets::PreparedImage> levels;
	if (!Assets::Prepare_Image_Levels(tga.View(), Assets::PixelEncoding::RGBA8, tga.info.width, tga.info.height, 1, {0, 0, 0}, levels) ||
		levels.empty())
		return image;
	const Assets::PreparedImage &level = levels.front();
	image.width = level.width;
	image.height = level.height;
	const std::size_t row = static_cast<std::size_t>(level.width) * 4;
	image.pixels.resize(row * level.height);
	for (std::uint32_t y = 0; y < level.height; ++y)
		std::copy_n(level.bytes.begin() + static_cast<std::ptrdiff_t>(y * level.row_pitch), row,
			image.pixels.begin() + static_cast<std::ptrdiff_t>(y * row));
	return image;
}

inline TextureImage LoadArtTexture(const engine::filesystem::VirtualFileSystem &files, std::string_view name)
{
	const std::string wanted = LowerName(name);
	const auto dot = wanted.find_last_of('.');
	const std::string stem = wanted.substr(0, dot);
	std::string found;
	for (const std::string &path : files.List("art/textures"))
	{
		const std::string file = LowerName(path.substr(path.find_last_of("/\\") + 1));
		if (file == wanted)
		{
			found = path;
			break;
		}
		if (found.empty() && (file == stem + ".dds" || file == stem + ".tga"))
			found = path;
	}
	if (found.empty())
		return {};
	const auto bytes = files.Read(found);
	return bytes ? DecodeTextureImage(*bytes) : TextureImage{};
}
}
