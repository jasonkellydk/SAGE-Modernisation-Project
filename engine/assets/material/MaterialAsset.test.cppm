module;

#define BOOST_TEST_MODULE GeneralsAssetsMaterialTests

#include <boost/test/included/unit_test.hpp>
#include <limits>

export module Assets.Tests.MaterialAsset;

import Assets.Handles;
import Assets.Identity;
import Assets.Materials;

BOOST_AUTO_TEST_CASE(material_runtime_data_keeps_typed_texture_dependencies)
{
	const Assets::TextureAssetHandle primary(2, 1);
	const Assets::TextureAssetHandle secondary(3, 1);
	const Assets::MaterialAsset material(
		{Assets::AssetType::Material, "materials/paint"},
		{"paint", "textures/paint.tga", "textures/mask.tga"},
		primary,
		secondary);

	BOOST_CHECK(material.Name() == "paint");
	BOOST_CHECK(material.Primary_Texture() == primary);
	BOOST_CHECK(material.Secondary_Texture() == secondary);
	BOOST_CHECK(material.Surface().shading_model == Assets::MaterialShadingModel::Legacy);
	BOOST_CHECK(!material.Surface_Texture(Assets::MaterialTextureRole::Normal).Is_Valid());
}

BOOST_AUTO_TEST_CASE(surface_maps_and_parameters_are_immutable_after_publication)
{
	using namespace Assets;
	MaterialAssetDesc description;
	description.name = "airfield";
	description.surface.shading_model = MaterialShadingModel::SpecularGlossiness;
	description.surface.normal_scale = 2.0f;
	description.surface_textures[static_cast<std::size_t>(MaterialTextureRole::Normal)] = "airfield_nrm.dds";
	MaterialSurfaceTextureHandles handles{};
	handles[static_cast<std::size_t>(MaterialTextureRole::Normal)] = TextureAssetHandle(9, 3);
	handles[static_cast<std::size_t>(MaterialTextureRole::Specular)] = TextureAssetHandle(10, 2);
	const MaterialAsset material({AssetType::Material, "airfield"}, description, {}, {}, handles);
	description.surface.normal_scale = 0.0f;
	handles.fill({});
	BOOST_CHECK(material.Surface().shading_model == MaterialShadingModel::SpecularGlossiness);
	BOOST_TEST(material.Surface().normal_scale == 2.0f);
	BOOST_CHECK(material.Surface_Texture(MaterialTextureRole::Normal) == TextureAssetHandle(9, 3));
	BOOST_CHECK(material.Surface_Texture(MaterialTextureRole::Specular) == TextureAssetHandle(10, 2));
	BOOST_CHECK(!material.Surface_Texture(MaterialTextureRole::Count).Is_Valid());
}

BOOST_AUTO_TEST_CASE(surface_validation_rejects_invalid_values_without_changing_legacy_defaults)
{
	using namespace Assets;
	MaterialSurfaceParameters parameters;
	BOOST_TEST(Validate_Material_Surface(parameters));
	parameters.shading_model = static_cast<MaterialShadingModel>(255);
	BOOST_TEST(!Validate_Material_Surface(parameters));
	parameters = {}; parameters.normal_scale = std::numeric_limits<float>::infinity();
	BOOST_TEST(!Validate_Material_Surface(parameters));
	parameters = {}; parameters.roughness = -0.1f;
	BOOST_TEST(!Validate_Material_Surface(parameters));
	parameters = {}; parameters.metallic = 1.01f;
	BOOST_TEST(!Validate_Material_Surface(parameters));
	parameters = {}; parameters.alpha_cutoff = std::numeric_limits<float>::quiet_NaN();
	BOOST_TEST(!Validate_Material_Surface(parameters));
	parameters = {}; parameters.specular_channel = static_cast<MaterialTextureChannel>(255);
	BOOST_TEST(!Validate_Material_Surface(parameters));
	parameters = {}; parameters.team_color_channel = MaterialTextureChannel::RGB;
	BOOST_TEST(!Validate_Material_Surface(parameters));
	parameters = {}; parameters.team_color_multiplier = -1.0f;
	BOOST_TEST(!Validate_Material_Surface(parameters));
}

BOOST_AUTO_TEST_CASE(material_runtime_retains_independent_lighting_colors)
{
	Assets::MaterialAssetDesc description;
	description.name = "paint";
	description.base_color = {0.1f, 0.2f, 0.3f, 1};
	description.ambient_color = {0.2f, 0.3f, 0.4f, 1};
	description.specular_color = {0.3f, 0.4f, 0.5f, 1};
	description.emissive_color = {0.4f, 0.5f, 0.6f, 1};
	const Assets::MaterialAsset material({Assets::AssetType::Material, "paint"}, description, {}, {});
	BOOST_CHECK_EQUAL(material.Base_Color().r, 0.1f);
	BOOST_CHECK_EQUAL(material.Ambient_Color().g, 0.3f);
	BOOST_CHECK_EQUAL(material.Specular_Color().b, 0.5f);
	BOOST_CHECK_EQUAL(material.Emissive_Color().r, 0.4f);
}

BOOST_AUTO_TEST_CASE(pbr_companions_resolve_dds_for_tga_names_and_keep_missing_maps_optional)
{
    using namespace Assets;
    MaterialAssetDesc material;
    material.primary_texture = "Art/Textures/tank.tga";
    material.render_mode = MaterialRenderMode::AlphaTest;
    const auto exists = [](const std::string& name) {
        return name == "Art/Textures/tank_albedo.dds" || name == "Art/Textures/tank_normalmap.dds"
            || name == "Art/Textures/tank_roughness.dds" || name == "Art/Textures/tank_height.dds";
    };
    BOOST_REQUIRE(Discover_PBR_Textures(material, exists));
    BOOST_TEST(material.primary_texture == "Art/Textures/tank_albedo.dds");
    BOOST_CHECK(material.surface.shading_model == MaterialShadingModel::MetallicRoughness);
    BOOST_CHECK(material.render_mode == MaterialRenderMode::AlphaTest);
    BOOST_TEST(material.surface.roughness == 1.0f);
    BOOST_TEST(material.surface.metallic == 0.0f);
    BOOST_TEST(!material.surface.infer_metallic);
    BOOST_TEST(material.surface_textures[static_cast<std::size_t>(MaterialTextureRole::Height)] == "Art/Textures/tank_height.dds");
    BOOST_CHECK(!Discover_PBR_Textures(material, exists));
    material = {}; material.primary_texture = "dotted.directory/paint";
    BOOST_REQUIRE(Discover_PBR_Textures(material, [](const std::string& name) { return name == "dotted.directory/paint_albedo.tga"; }));
    BOOST_TEST(material.surface.roughness == 0.7f);
    for (const auto& map : material.surface_textures) BOOST_TEST(map.empty());
}
BOOST_AUTO_TEST_CASE(pbr_discovery_does_not_reinterpret_effects_or_explicit_materials)
{
    using namespace Assets;
    for (const auto mode : {MaterialRenderMode::Additive, MaterialRenderMode::Multiply}) {
        MaterialAssetDesc material; material.primary_texture = "fire.dds"; material.render_mode = mode;
        BOOST_CHECK(!Discover_PBR_Textures(material, [](const std::string&) { return true; }));
        BOOST_TEST(material.primary_texture == "fire.dds");
    }
    MaterialAssetDesc material; material.primary_texture = "paint.dds";
    BOOST_CHECK(!Discover_PBR_Textures(material, [](const std::string&) { return false; }));
    BOOST_CHECK(material.surface.shading_model == MaterialShadingModel::Legacy);
    material.surface.shading_model = MaterialShadingModel::SpecularGlossiness;
    BOOST_CHECK(!Discover_PBR_Textures(material, [](const std::string&) { return true; }));
    material.surface.height_scale = -1;
    BOOST_CHECK(!Validate_Material_Surface(material.surface));
}
BOOST_AUTO_TEST_CASE(legacy_conversion_separates_exposed_metal_paint_glass_and_rubber)
{
    using namespace Assets;
    BOOST_TEST(Upgrade_Legacy_Surface("Art/Textures/AVTank.tga").metallic == 0.f);
    BOOST_TEST(Upgrade_Legacy_Surface("steel_panel").metallic == .95f);
    BOOST_TEST(Upgrade_Legacy_Surface("tank_tire").metallic == 0);
    BOOST_TEST(Upgrade_Legacy_Surface("concrete").roughness == .85f);
    BOOST_TEST(Upgrade_Legacy_Surface("glass").roughness == .08f);
    BOOST_TEST(Upgrade_Legacy_Surface("paint",64,.2f).roughness < Upgrade_Legacy_Surface("paint",2,.2f).roughness);
    MaterialAssetDesc material; material.primary_texture="tank.tga";
    BOOST_REQUIRE(Discover_PBR_Textures(material,[](const std::string& path) {
        return path=="tank_albedo.tga" || path=="tank_metallic.tga";
    }));
    BOOST_TEST(material.surface.metallic == 1);
    BOOST_TEST(!material.surface.infer_metallic);
}
