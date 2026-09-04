module;

#include <cstdint>
#include <string>
#include <utility>

export module Assets.Materials;

import Assets.Handles;
import Assets.Identity;
import Assets.Math;

namespace Assets
{

export struct MaterialAssetDesc final
{
	std::string name;
	std::string primary_texture;
	std::string secondary_texture;
	Color4f base_color{};
	float shininess = 1.0f;
	float opacity = 1.0f;
	float translucency = 0.0f;
	std::uint32_t source_attributes = 0;
};

export class MaterialAsset final
{
public:
	MaterialAsset(
		AssetIdentity identity,
		MaterialAssetDesc description,
		TextureAssetHandle primary_texture,
		TextureAssetHandle secondary_texture);

	const AssetIdentity &Identity() const noexcept;
	const std::string &Name() const noexcept;
	const TextureAssetHandle &Primary_Texture() const noexcept;
	const TextureAssetHandle &Secondary_Texture() const noexcept;
	const Color4f &Base_Color() const noexcept;
	float Shininess() const noexcept;
	float Opacity() const noexcept;
	float Translucency() const noexcept;
	std::uint32_t Source_Attributes() const noexcept;

private:
	AssetIdentity m_identity;
	std::string m_name;
	TextureAssetHandle m_primary_texture;
	TextureAssetHandle m_secondary_texture;
	Color4f m_base_color{};
	float m_shininess = 1.0f;
	float m_opacity = 1.0f;
	float m_translucency = 0.0f;
	std::uint32_t m_source_attributes = 0;
};

}

namespace Assets
{

MaterialAsset::MaterialAsset(
	AssetIdentity identity,
	MaterialAssetDesc description,
	TextureAssetHandle primary_texture,
	TextureAssetHandle secondary_texture)
	: m_identity(std::move(identity)),
	  m_name(std::move(description.name)),
	  m_primary_texture(primary_texture),
	  m_secondary_texture(secondary_texture),
	  m_base_color(description.base_color),
	  m_shininess(description.shininess),
	  m_opacity(description.opacity),
	  m_translucency(description.translucency),
	  m_source_attributes(description.source_attributes)
{
}

const AssetIdentity &MaterialAsset::Identity() const noexcept
{
	return m_identity;
}

const std::string &MaterialAsset::Name() const noexcept
{
	return m_name;
}

const TextureAssetHandle &MaterialAsset::Primary_Texture() const noexcept
{
	return m_primary_texture;
}

const TextureAssetHandle &MaterialAsset::Secondary_Texture() const noexcept
{
	return m_secondary_texture;
}

const Color4f &MaterialAsset::Base_Color() const noexcept
{
	return m_base_color;
}

float MaterialAsset::Shininess() const noexcept
{
	return m_shininess;
}

float MaterialAsset::Opacity() const noexcept
{
	return m_opacity;
}

float MaterialAsset::Translucency() const noexcept
{
	return m_translucency;
}

std::uint32_t MaterialAsset::Source_Attributes() const noexcept
{
	return m_source_attributes;
}

}
