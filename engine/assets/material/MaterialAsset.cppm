module;

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

export module Assets.Materials;

import Assets.Handles;
import Assets.Identity;
import Assets.Math;

namespace Assets
{

export enum class MaterialRenderMode : std::uint8_t
{
	Opaque,
	AlphaTest,
	AlphaBlend,
	Additive,
	Multiply
};

export enum class MaterialScope : std::uint8_t
{
	Shared,
	Model
};

export enum class MaterialShadingModel : std::uint8_t
{
	Legacy,
	SpecularGlossiness,
	MetallicRoughness
};

export enum class MaterialTextureRole : std::uint8_t
{
	Normal,
	Specular,
	Emissive,
	Roughness,
	Metallic,
	Occlusion,
	TeamColor,
	Height,
	Count
};

export inline constexpr std::size_t MaterialSurfaceTextureCount = static_cast<std::size_t>(MaterialTextureRole::Count);
export using MaterialSurfaceTextureNames = std::array<std::string, MaterialSurfaceTextureCount>;
export using MaterialSurfaceTextureHandles = std::array<TextureAssetHandle, MaterialSurfaceTextureCount>;

export enum class MaterialTextureChannel : std::uint8_t
{
	Red, Green, Blue, Alpha, RGB
};

export struct MaterialSurfaceParameters final
{
	MaterialShadingModel shading_model = MaterialShadingModel::Legacy;
	float normal_scale = 1.0f;
	float height_scale = 0.1f;
	float specular_scale = 1.0f;
	float emissive_scale = 1.0f;
	float roughness = 0.5f;
	float metallic = 0.0f;
	float occlusion_strength = 1.0f;
	float alpha_cutoff = 0.5f;
	bool normal_flip_green = false;
	MaterialTextureChannel specular_channel = MaterialTextureChannel::RGB;
	MaterialTextureChannel team_color_channel = MaterialTextureChannel::Red;
	float team_color_multiplier = 1.0f;
	bool uv_offset_from_vertex_alpha = false;
	// Legacy W3D has no metalness. Restrict the inferred metal profile to
	// exposed, unsaturated texels; an authored metallic map always wins.
	bool infer_metallic = false;
	// W3D emissive RGB modulates the base texture (for example colored lamp sprites).
	bool emissive_uses_base_color = false;
};

export bool Validate_Material_Surface(const MaterialSurfaceParameters &surface) noexcept
{
	if (surface.shading_model != MaterialShadingModel::Legacy &&
		surface.shading_model != MaterialShadingModel::SpecularGlossiness &&
		surface.shading_model != MaterialShadingModel::MetallicRoughness)
		return false;
	if (surface.specular_channel > MaterialTextureChannel::RGB ||
		surface.team_color_channel >= MaterialTextureChannel::RGB)
		return false;
	for (const float scale : {surface.normal_scale, surface.height_scale, surface.specular_scale, surface.emissive_scale, surface.team_color_multiplier})
		if (!std::isfinite(scale) || scale < 0.0f)
			return false;
	for (const float value : {surface.roughness, surface.metallic, surface.occlusion_strength, surface.alpha_cutoff})
		if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
			return false;
	return true;
}

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
	MaterialRenderMode render_mode = MaterialRenderMode::Opaque;
	bool depth_write = true;
	bool texturing = true;
	Color4f ambient_color{};
	Color4f specular_color{0, 0, 0, 1};
	Color4f emissive_color{0, 0, 0, 1};
	MaterialScope scope = MaterialScope::Shared;
	MaterialSurfaceParameters surface{};
	MaterialSurfaceTextureNames surface_textures{};
};

export MaterialSurfaceParameters Upgrade_Legacy_Surface(std::string_view name,
    float shininess = 0, float specular = 0)
{
    MaterialSurfaceParameters surface;
    surface.shading_model = MaterialShadingModel::MetallicRoughness;
    surface.emissive_uses_base_color = true;
    surface.roughness = shininess > 1 && specular > .01f
        ? std::clamp(std::pow(2.0f / (shininess + 2.0f), .25f), .18f, .8f) : .7f;
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return c >= 'A' && c <= 'Z' ? char(c + 32) : char(c);
    });
    const auto slash = lower.find_last_of("/\\");
    const auto base = std::string_view(lower).substr(slash == std::string::npos ? 0 : slash + 1);
    const auto has = [&](std::string_view token) { return base.find(token) != std::string_view::npos; };
    const bool vehicle = base.starts_with("av") || base.starts_with("cv") || base.starts_with("uv")
        || base.starts_with("nv") || base.starts_with("zhcv") || has("tank") || has("aircraft");
    if (vehicle) surface.roughness = .5f;
    if (has("chrome") || has("steel") || has("metal") || has("barrel") || has("tread") || has("track")) {
        surface.metallic = .95f;
        surface.infer_metallic = false;
        surface.roughness = .3f;
    }
    if (has("glass")) { surface.metallic = 0; surface.roughness = .08f; surface.infer_metallic = false; }
    if (has("rubber") || has("tire") || has("tyre") || has("wood") || has("concrete")) {
        surface.metallic = 0; surface.roughness = .85f; surface.infer_metallic = false;
    }
    return surface;
}

// Probe through the caller's asset source so loose files and archives follow
// the same policy. Existing explicit material parameters take precedence.
export template<class Exists>
bool Discover_PBR_Textures(MaterialAssetDesc& material, Exists&& exists)
{
	if (material.primary_texture.empty() || !material.texturing ||
		material.surface.shading_model != MaterialShadingModel::Legacy ||
		material.render_mode == MaterialRenderMode::Additive ||
		material.render_mode == MaterialRenderMode::Multiply) return false;
	std::string stem = material.primary_texture;
	const auto dot = stem.find_last_of('.'), slash = stem.find_last_of("/\\");
	std::string extension;
	if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
		extension = stem.substr(dot); stem.resize(dot);
	}
	const auto find = [&](std::string_view suffix) -> std::string {
		for (const auto& ext : {extension, std::string(".dds"), std::string(".tga")}) {
			if (ext.empty()) continue;
			auto candidate = stem + std::string(suffix) + ext;
			if (exists(candidate)) return candidate;
		}
		return {};
	};
	auto albedo = find("_albedo");
	if (albedo.empty()) return false;
	material.primary_texture = std::move(albedo);
	material.surface = Upgrade_Legacy_Surface(stem, material.shininess,
        std::max({material.specular_color.r, material.specular_color.g, material.specular_color.b}));
	material.surface.roughness = 1.0f; // The texture contains absolute roughness.
	for (const auto& [role, suffix] : {
		std::pair{MaterialTextureRole::Normal, "_normalmap"},
		std::pair{MaterialTextureRole::Roughness, "_roughness"},
		std::pair{MaterialTextureRole::Metallic, "_metallic"},
		std::pair{MaterialTextureRole::Occlusion, "_ao"},
		std::pair{MaterialTextureRole::Emissive, "_emissive"},
		std::pair{MaterialTextureRole::Height, "_height"}})
		material.surface_textures[static_cast<std::size_t>(role)] = find(suffix);
	if (material.surface_textures[static_cast<std::size_t>(MaterialTextureRole::Roughness)].empty())
		material.surface.roughness = Upgrade_Legacy_Surface(stem, material.shininess,
            std::max({material.specular_color.r, material.specular_color.g, material.specular_color.b})).roughness;
	if (!material.surface_textures[static_cast<std::size_t>(MaterialTextureRole::Metallic)].empty()) {
        material.surface.metallic = 1; material.surface.infer_metallic = false;
    }
	return true;
}

export class MaterialAsset final
{
public:
	MaterialAsset(
		AssetIdentity identity,
		MaterialAssetDesc description,
		TextureAssetHandle primary_texture,
		TextureAssetHandle secondary_texture);
	MaterialAsset(
		AssetIdentity identity,
		MaterialAssetDesc description,
		TextureAssetHandle primary_texture,
		TextureAssetHandle secondary_texture,
		MaterialSurfaceTextureHandles surface_textures);

	const AssetIdentity &Identity() const noexcept;
	const std::string &Name() const noexcept;
	const TextureAssetHandle &Primary_Texture() const noexcept;
	const TextureAssetHandle &Secondary_Texture() const noexcept;
	const Color4f &Base_Color() const noexcept;
	const Color4f &Ambient_Color() const noexcept;
	const Color4f &Specular_Color() const noexcept;
	const Color4f &Emissive_Color() const noexcept;
	float Shininess() const noexcept;
	float Opacity() const noexcept;
	float Translucency() const noexcept;
	std::uint32_t Source_Attributes() const noexcept;
	MaterialRenderMode Render_Mode() const noexcept;
	bool Depth_Write() const noexcept;
	bool Texturing() const noexcept;
	const MaterialSurfaceParameters &Surface() const noexcept;
	TextureAssetHandle Surface_Texture(MaterialTextureRole role) const noexcept;

private:
	AssetIdentity m_identity;
	std::string m_name;
	TextureAssetHandle m_primary_texture;
	TextureAssetHandle m_secondary_texture;
	Color4f m_base_color{};
	Color4f m_ambient_color{};
	Color4f m_specular_color{0, 0, 0, 1};
	Color4f m_emissive_color{0, 0, 0, 1};
	float m_shininess = 1.0f;
	float m_opacity = 1.0f;
	float m_translucency = 0.0f;
	std::uint32_t m_source_attributes = 0;
	MaterialRenderMode m_render_mode = MaterialRenderMode::Opaque;
	bool m_depth_write = true;
	bool m_texturing = true;
	MaterialSurfaceParameters m_surface{};
	MaterialSurfaceTextureHandles m_surface_textures{};
};

}

namespace Assets
{

MaterialAsset::MaterialAsset(
	AssetIdentity identity,
	MaterialAssetDesc description,
	TextureAssetHandle primary_texture,
	TextureAssetHandle secondary_texture)
	: MaterialAsset(std::move(identity), std::move(description), primary_texture, secondary_texture, MaterialSurfaceTextureHandles{})
{
}

MaterialAsset::MaterialAsset(
	AssetIdentity identity,
	MaterialAssetDesc description,
	TextureAssetHandle primary_texture,
	TextureAssetHandle secondary_texture,
	MaterialSurfaceTextureHandles surface_textures)
	: m_identity(std::move(identity)),
	  m_name(std::move(description.name)),
	  m_primary_texture(primary_texture),
	  m_secondary_texture(secondary_texture),
	  m_base_color(description.base_color),
	  m_ambient_color(description.ambient_color),
	  m_specular_color(description.specular_color),
	  m_emissive_color(description.emissive_color),
	  m_shininess(description.shininess),
	m_opacity(description.opacity),
	  m_translucency(description.translucency),
	  m_source_attributes(description.source_attributes),
	  m_render_mode(description.render_mode),
	  m_depth_write(description.depth_write),
	  m_texturing(description.texturing),
	  m_surface(description.surface),
	  m_surface_textures(surface_textures)
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

const Color4f &MaterialAsset::Ambient_Color() const noexcept
{
	return m_ambient_color;
}

const Color4f &MaterialAsset::Specular_Color() const noexcept
{
	return m_specular_color;
}

const Color4f &MaterialAsset::Emissive_Color() const noexcept
{
	return m_emissive_color;
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

MaterialRenderMode MaterialAsset::Render_Mode() const noexcept
{
	return m_render_mode;
}

bool MaterialAsset::Depth_Write() const noexcept
{
	return m_depth_write;
}

bool MaterialAsset::Texturing() const noexcept
{
	return m_texturing;
}

const MaterialSurfaceParameters &MaterialAsset::Surface() const noexcept
{
	return m_surface;
}

TextureAssetHandle MaterialAsset::Surface_Texture(MaterialTextureRole role) const noexcept
{
	const auto index = static_cast<std::size_t>(role);
	return index < m_surface_textures.size() ? m_surface_textures[index] : TextureAssetHandle::Invalid();
}

}
