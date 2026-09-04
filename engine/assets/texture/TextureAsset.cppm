module;

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

export module Assets.Textures;

import Assets.Identity;

namespace Assets
{

export class TextureAsset final
{
public:
	TextureAsset(AssetIdentity identity, std::string source_format, std::size_t source_size);

	const AssetIdentity &Identity() const noexcept;
	const std::string &Source_Format() const noexcept;
	std::size_t Source_Size() const noexcept;

private:
	AssetIdentity m_identity;
	std::string m_source_format;
	std::size_t m_source_size = 0;
};

}

namespace Assets
{

TextureAsset::TextureAsset(AssetIdentity identity, std::string source_format, std::size_t source_size)
	: m_identity(std::move(identity)),
	  m_source_format(std::move(source_format)),
	  m_source_size(source_size)
{
}

const AssetIdentity &TextureAsset::Identity() const noexcept
{
	return m_identity;
}

const std::string &TextureAsset::Source_Format() const noexcept
{
	return m_source_format;
}

std::size_t TextureAsset::Source_Size() const noexcept
{
	return m_source_size;
}

}
