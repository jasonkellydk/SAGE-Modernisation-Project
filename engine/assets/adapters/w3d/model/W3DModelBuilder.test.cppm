module;

#define BOOST_TEST_MODULE GeneralsAssetsW3DModelBuilderTests

#include <boost/test/included/unit_test.hpp>

export module Assets.Tests.W3DModelBuilder;
import std;

import Assets.Adapters.W3D.Mesh;
import Assets.Adapters.W3D.Model;
import Assets.Models;
import Assets.Identity;
import Assets.Materials;
import Assets.Adapters.W3D.Materials;
import Assets.Adapters.W3D.PassBindings;

BOOST_AUTO_TEST_CASE(model_builder_keeps_optional_skin_weights)
{
    Assets::W3D::W3DParsedMesh mesh;
    mesh.header.bounds = {{0,0,0},{1,1,0}};
    mesh.positions = {{0,0,0},{1,0,0},{0,1,0}};
    mesh.normals.assign(3,{0,0,1}); mesh.stage_texcoords.assign(3,{}); mesh.colors.assign(3,{});
    mesh.bone_indices = {1,1,1};
    mesh.skin_indices.assign(3,{1,7,0,0});
    mesh.skin_weights.assign(3,{0.4f,0.6f,0,0});
    mesh.triangles = {{0,1,2}};
    Assets::ModelAssetDesc description;
    Assets::W3D::W3DAppend_Mesh(description,mesh);
    BOOST_TEST(description.vertices[0].bone_indices[1] == 7u);
    BOOST_TEST(description.vertices[0].bone_weights[0] == 0.4f);
    BOOST_TEST(description.vertices[0].bone_weights[1] == 0.6f);
    BOOST_TEST(description.skin_bone_count == 8u);
}

BOOST_AUTO_TEST_CASE(selected_vertex_prelighting_disables_scene_lighting_without_affecting_unlit_alternative) {
    Assets::W3D::W3DParsedMesh mesh;mesh.header.attributes=0x03000000; // both alternative wrappers are present
    mesh.positions={{0,0,0},{1,0,0},{0,1,0}};mesh.normals.assign(3,{0,0,1});mesh.stage_texcoords.assign(3,{});mesh.colors.assign(3,{.2f,.3f,.4f,1});mesh.triangles={{0,1,2}};
    mesh.prelit_chunk=0x24;Assets::ModelAssetDesc vertex;Assets::W3D::W3DAppend_Mesh(vertex,mesh);
    BOOST_REQUIRE_EQUAL(vertex.submeshes.size(),1u);BOOST_TEST(!vertex.submeshes[0].lighting_enabled);
    mesh.prelit_chunk=0x23;Assets::ModelAssetDesc unlit;Assets::W3D::W3DAppend_Mesh(unlit,mesh);BOOST_TEST(unlit.submeshes[0].lighting_enabled);
}

BOOST_AUTO_TEST_CASE(hidden_collision_mesh_retains_one_topology_across_material_passes) {
    Assets::W3D::W3DParsedMesh mesh;
    mesh.header.name="physical";mesh.header.attributes=0x1000|0x30;
    mesh.positions={{0,0,0},{1,0,0},{0,1,0}};mesh.normals.assign(3,{0,0,1});mesh.stage_texcoords.assign(3,{});mesh.colors.assign(3,{});
    mesh.triangles={{0,1,2}};mesh.triangle_surfaces={23};
    mesh.materials.vertex_materials.push_back({{"surface",""}});mesh.materials.shaders.push_back({});
    Assets::W3D::W3DMaterialPass first;first.vertex_material_index=0;first.shader_index=0;
    mesh.materials.passes={first,first};
    Assets::ModelAssetDesc description;Assets::W3D::W3DAppend_Mesh(description,mesh);
    BOOST_REQUIRE(description.collision.triangles.size()==1u);BOOST_REQUIRE(description.collision.parts.size()==1u);
    BOOST_TEST(description.collision.parts[0].categories==3u);BOOST_TEST(description.collision.surfaces[0]==23u);
    BOOST_TEST(description.collision.parts[0].name=="physical");BOOST_TEST(description.submeshes.size()==2u);
    Assets::ModelAsset asset({Assets::AssetType::Model,"physical"},std::move(description));
    BOOST_TEST(asset.Collision().triangles[0][2]==2u);BOOST_TEST(asset.Collision().surfaces[0]==23u);
}

BOOST_AUTO_TEST_CASE(emissive_only_vertex_and_lightmap_solves_use_authored_colors_instead_of_zero_diffuse) {
    // Frozen EA MeshMatDescClass::Post_Load_Process semantics. Retail terrain
    // uses DCG, zero ambient/diffuse and white or tinted emissive materials.
    // PRELIT_VERTEX disables lighting; all-emissive lightmap passes do too.
    for(auto selected:{0x24u,0x25u}) {
        Assets::W3D::W3DParsedMesh mesh;mesh.prelit_chunk=selected;
        mesh.positions={{0,0,0},{1,0,0},{0,1,0}};mesh.normals.assign(3,{0,0,1});mesh.stage_texcoords.assign(3,{});mesh.colors.assign(3,{1,1,1,1});mesh.triangles={{0,1,2}};
        Assets::W3D::W3DVertexMaterialData source;source.material.ambient_color={0,0,0,1};source.material.base_color={0,0,0,1};source.material.emissive_color={146/255.f,146/255.f,146/255.f,1};
        mesh.materials.vertex_materials.push_back(source);
        Assets::W3D::W3DMaterialPass pass;pass.vertex_material_index=0;pass.colors={{66/255.f,59/255.f,57/255.f,1},{.2f,.3f,.4f,1},{.4f,.2f,.1f,1}};
        mesh.materials.passes.push_back(pass);if(selected==0x25) mesh.materials.passes.push_back(pass);
        Assets::ModelAssetDesc converted;Assets::W3D::W3DAppend_Mesh(converted,mesh);
        BOOST_REQUIRE_EQUAL(converted.submeshes.size(),selected==0x25 ? 2u : 1u);
        for(const auto& part:converted.submeshes) {
            BOOST_TEST(!part.lighting_enabled);const auto& material=converted.materials.at(part.material_index);
            const auto& vertex=converted.vertices.at(converted.indices.at(part.first_index));
            BOOST_TEST(material.base_color.r==146/255.f);BOOST_TEST(vertex.color.r==66/255.f);
            const auto shaded=vertex.color.r*material.base_color.r;BOOST_CHECK_SMALL(shaded-66.f*146/(255*255),0.000001f);
        }
        mesh.materials.vertex_materials[0].material.base_color={1,1,1,1};
        Assets::ModelAssetDesc mixed;Assets::W3D::W3DAppend_Mesh(mixed,mesh);
        BOOST_TEST(mixed.submeshes[0].lighting_enabled==(selected!=0x24));
    }
}

BOOST_AUTO_TEST_CASE(authored_screen_blend_and_depth_survive_shader_decode_and_model_building)
{
	// EA w3d_file.h's ONE / ONE_MINUS_SRC_COLOR, depth writes off. This
	// 16-byte source fixture is also used by the retail EVA gizmo meshes.
	const std::array<std::byte,16> wire{std::byte{3},std::byte{0},std::byte{0},std::byte{3},
		std::byte{2},std::byte{1},std::byte{0},std::byte{1},std::byte{1},std::byte{0},
		std::byte{0},std::byte{2},std::byte{0},std::byte{0},std::byte{0},std::byte{2}};
	Assets::W3D::W3DShaderSettings shader;
	BOOST_REQUIRE(Assets::W3D::W3DRead_Shader(wire,shader));
	Assets::W3D::W3DParsedMesh mesh;
	mesh.header.sort_level=1;
	mesh.positions={{0,0,0},{1,0,0},{0,1,0}};
	mesh.normals.assign(3,{0,0,1});mesh.stage_texcoords.assign(3,{});mesh.colors.assign(3,{});
	mesh.triangles={{0,1,2}};
	mesh.materials.vertex_materials.push_back({{"screen",""}});
	mesh.materials.shaders.push_back(shader);
	Assets::W3D::W3DMaterialPass pass;pass.vertex_material_index=0;pass.shader_index=0;
	mesh.materials.passes.push_back(pass);
	Assets::ModelAssetDesc description;
	Assets::W3D::W3DAppend_Mesh(description,mesh);
	BOOST_REQUIRE_EQUAL(description.materials.size(),1u);
	const auto& material=description.materials[0];
	BOOST_REQUIRE(material.draw_state);
	BOOST_CHECK(material.draw_state->source==Assets::MaterialBlendFactor::One);
	BOOST_CHECK(material.draw_state->destination==Assets::MaterialBlendFactor::InverseSourceColor);
	BOOST_CHECK(material.draw_state->depth_comparison==Assets::MaterialDepthComparison::LessEqual);
	BOOST_TEST(!material.depth_write);
	BOOST_CHECK(material.render_mode!=Assets::MaterialRenderMode::AlphaBlend);
	BOOST_TEST(description.submeshes[0].blends);
	BOOST_TEST(description.submeshes[0].sort_level==1);
	mesh.header.sort_level=3;
	Assets::W3D::W3DAppend_Mesh(description,mesh);
	BOOST_REQUIRE_EQUAL(description.submeshes.size(),2u);
	BOOST_TEST(description.submeshes[0].sort_level==1);
	BOOST_TEST(description.submeshes[1].sort_level==3);
	// Depth writes and alpha blending are independent properties.
	mesh.materials.shaders[0].destination_blend=0;
	description={};Assets::W3D::W3DAppend_Mesh(description,mesh);
	BOOST_REQUIRE(description.materials[0].draw_state);
	BOOST_CHECK(description.materials[0].draw_state->destination==Assets::MaterialBlendFactor::Zero);
	BOOST_CHECK(description.materials[0].render_mode==Assets::MaterialRenderMode::Opaque);
	BOOST_TEST(!description.submeshes[0].blends);
}

BOOST_AUTO_TEST_CASE(model_builder_creates_generic_submesh_and_dependencies)
{
	Assets::W3D::W3DParsedMesh mesh;
	mesh.header.name = "body";
	mesh.header.container_name = "unit";
	mesh.header.attributes = 4;
	mesh.header.sort_level = 3;
	mesh.header.bounds = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
	mesh.positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
	mesh.normals.assign(3, {0.0f, 0.0f, 1.0f});
	mesh.stage_texcoords.assign(3, {});
	mesh.colors.assign(3, {});
	mesh.triangles.push_back({0, 1, 2});
	mesh.materials.vertex_materials.push_back({{"body_material", "Body.TGA"}});
	mesh.materials.vertex_materials.back().material.texture_mappings[0]=Assets::TextureEnvironmentMapping{Assets::TextureEnvironmentSource::Reflection};
	mesh.materials.textures.push_back({"Body.TGA"});
	mesh.materials.passes.push_back({0, 0, {}});

	Assets::ModelAssetDesc description;
	Assets::W3D::W3DAppend_Mesh(description, mesh);

	BOOST_REQUIRE_EQUAL(description.vertices.size(), 3);
	BOOST_REQUIRE_EQUAL(description.indices.size(), 3);
	BOOST_REQUIRE_EQUAL(description.submeshes.size(), 1);
	BOOST_REQUIRE_EQUAL(description.materials.size(), 1);
	BOOST_REQUIRE(description.materials[0].texture_mappings[0]);
	BOOST_CHECK(std::get<Assets::TextureEnvironmentMapping>(*description.materials[0].texture_mappings[0]).source==Assets::TextureEnvironmentSource::Reflection);
	BOOST_CHECK(description.submeshes[0].material_index == 0);
	// Each submesh keeps its own mesh's attribute bits (W3DMeshRenderObject::Load_W3D reads hidden, collision and geometry
	// type per mesh), not only the model's union.
	BOOST_CHECK(description.submeshes[0].source_attributes == 4u);
	BOOST_CHECK(description.materials[0].primary_texture == "Body.TGA");
	BOOST_CHECK(description.bounds.Is_Valid());
	BOOST_CHECK(description.sort_level == 3);
	BOOST_REQUIRE_EQUAL(description.dependencies.size(), 2);
}

BOOST_AUTO_TEST_CASE(surface_pass_preserves_face_material_order_uv_seams_and_skin_attachment)
{
	using namespace Assets;
	using namespace Assets::W3D;
	W3DParsedMesh mesh;
	mesh.header.name = "panels";
	mesh.header.bounds = {{0, 0, 0}, {1, 1, 0}};
	mesh.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	mesh.normals.assign(3, {0, 0, 1});
	mesh.stage_texcoords.assign(3, {});
	mesh.colors.assign(3, {});
	mesh.bone_indices = {2, 3, 4};
	mesh.triangles = {{0, 1, 2}, {2, 1, 0}, {0, 2, 1}};
	W3DMaterialPass pass; pass.uses_shader_material = true;
	mesh.materials.passes.push_back(pass);
	mesh.surface_materials = {{"red", "red.dds"}, {"blue", "blue.dds"}};
	mesh.surface_materials[0].surface.shading_model = MaterialShadingModel::SpecularGlossiness;
	mesh.surface_materials[0].surface_textures[static_cast<std::size_t>(MaterialTextureRole::Normal)] = "nrm.dds";
	W3DPassBindings bindings;
	bindings.shader_material_ids = {0, 1, 0};
	bindings.stages.resize(1);
	bindings.stages[0].texcoords = {{0, 0}, {1, 0}, {0, 1}, {.5f, .25f}};
	bindings.stages[0].face_texcoord_ids = {{3, 1, 2}, {2, 1, 0}, {0, 2, 1}};
	mesh.shader_pass_bindings.push_back(bindings);
	ModelAssetDesc description;
	W3DAppend_Mesh(description, mesh);
	BOOST_REQUIRE_EQUAL(description.submeshes.size(), 3u);
	BOOST_REQUIRE_EQUAL(description.materials.size(), 2u);
	BOOST_REQUIRE_EQUAL(description.indices.size(), 9u);
	for (std::size_t face = 0; face < 3; ++face) {
		BOOST_TEST(description.submeshes[face].material_index == (face == 1 ? 1u : 0u));
		BOOST_TEST(description.submeshes[face].first_index == face * 3);
		BOOST_TEST(description.submeshes[face].index_count == 3u);
	}
	const auto &corner = description.vertices[description.indices[0]];
	BOOST_TEST(corner.texcoord.x == .5f);
	BOOST_TEST(corner.texcoord.y == .75f);
	BOOST_TEST(corner.bone_indices[0] == 2u);
	BOOST_TEST(corner.bone_weights[0] == 1.0f);
	BOOST_TEST(std::isfinite(corner.tangent.x));
	BOOST_TEST(corner.tangent.x * corner.tangent.x + corner.tangent.y * corner.tangent.y
		+ corner.tangent.z * corner.tangent.z == 1.0f, boost::test_tools::tolerance(.0001f));
	BOOST_TEST(description.vertices[description.indices[6]].texcoord.x == 0.0f);
	BOOST_TEST(description.skin_bone_count == 5u);
	bool normal_dependency = false;
	for (const auto &dependency : description.dependencies)
		if (dependency.name == "nrm.dds") normal_dependency = true;
	BOOST_TEST(normal_dependency);
}

BOOST_AUTO_TEST_CASE(authored_tangent_handedness_follows_the_w3d_v_axis_conversion)
{
	Assets::W3D::W3DParsedMesh mesh;
	mesh.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	mesh.normals.assign(3, {0, 0, 1});
	mesh.tangents.assign(3, {2, 0, 0});
	mesh.bitangents.assign(3, {0, 3, 0});
	mesh.stage_texcoords.assign(3, {});
	mesh.colors.assign(3, {});
	mesh.triangles = {{0, 1, 2}};
	Assets::ModelAssetDesc description;
	Assets::W3D::W3DAppend_Mesh(description, mesh);
	BOOST_TEST(description.vertices[0].tangent.x == 1.0f);
	BOOST_TEST(description.vertices[0].tangent_sign == -1.0f);
	mesh.bitangents.assign(3, {0, -3, 0});
	Assets::ModelAssetDesc mirrored;
	Assets::W3D::W3DAppend_Mesh(mirrored, mesh);
	BOOST_TEST(mirrored.vertices[0].tangent_sign == 1.0f);
}

// Legacy parity (engine/graphics/scene/models/ModelMeshDrawing.cppm Draw_Base, the reference renderer's mesh drawing,
// fed by W3DMeshResource's per-polygon shader and texture arrays and per-vertex material array): a pass's polygons draw
// in authored order in batches that share a shader (per polygon), a stage 0 texture (per polygon) and the vertex
// material of their first vertex. Faces: 0 (vm0, sh0, tx0), 1 (vm0, sh0, tx1), 2 (vm1, sh1, tx1) by its first vertex,
// 3 (vm0, sh0, tx0) again: four batches, three distinct materials (the last batch reuses the first's).
BOOST_AUTO_TEST_CASE(legacy_parity_a_pass_splits_where_its_polygons_shader_texture_or_first_vertex_material_changes)
{
	Assets::W3D::W3DParsedMesh mesh;
	mesh.header.name = "body";
	mesh.header.container_name = "unit";
	for (int vertex = 0; vertex < 8; ++vertex)
		mesh.positions.push_back({static_cast<float>(vertex), 0.0f, 0.0f});
	mesh.normals.assign(8, {0.0f, 0.0f, 1.0f});
	mesh.stage_texcoords.assign(8, {});
	mesh.colors.assign(8, {});
	mesh.triangles = {{0, 1, 2}, {1, 2, 3}, {4, 5, 6}, {2, 3, 7}};
	mesh.materials.vertex_materials.push_back({{"plain", ""}});
	mesh.materials.vertex_materials.push_back({{"glass", ""}});
	mesh.materials.textures.push_back({"Hull.TGA"});
	mesh.materials.textures.push_back({"Window.TGA"});
	Assets::W3D::W3DShaderSettings opaque;
	Assets::W3D::W3DShaderSettings blended;
	blended.source_blend = 2;
	blended.destination_blend = 5;
	mesh.materials.shaders = {opaque, blended};
	Assets::W3D::W3DMaterialPass pass;
	pass.vertex_material_ids = {0, 0, 0, 0, 1, 1, 1, 0};
	pass.vertex_material_index = 0;
	pass.shader_ids = {0, 0, 1, 0};
	pass.shader_index = 0;
	pass.texture_ids = {0, 1, 1, 0};
	pass.texture_index = 0;
	mesh.materials.passes.push_back(pass);

	Assets::ModelAssetDesc description;
	Assets::W3D::W3DAppend_Mesh(description, mesh);

	BOOST_REQUIRE_EQUAL(description.submeshes.size(), 4u);
	BOOST_REQUIRE_EQUAL(description.materials.size(), 3u);
	const std::array<std::uint32_t, 4> firsts{0, 3, 6, 9};
	const std::array<std::uint32_t, 4> materials{0, 1, 2, 0};
	for (std::size_t batch = 0; batch < 4; ++batch)
	{
		BOOST_TEST(description.submeshes[batch].first_index == firsts[batch]);
		BOOST_TEST(description.submeshes[batch].index_count == 3u);
		BOOST_TEST(description.submeshes[batch].material_index == materials[batch]);
		// W3DMeshRenderObject::Load_W3D's is_alpha per polygon: only the blended shader's batch counts as alpha.
		BOOST_TEST(description.submeshes[batch].pass == 0u);
		BOOST_TEST(description.submeshes[batch].blends == (batch == 2));
	}
	BOOST_TEST(description.materials[0].primary_texture == "Hull.TGA");
	BOOST_TEST(description.materials[1].primary_texture == "Window.TGA");
	BOOST_TEST(description.materials[2].primary_texture == "Window.TGA");
	BOOST_TEST(description.materials[2].name == "glass");
	BOOST_TEST((description.materials[2].render_mode == Assets::MaterialRenderMode::AlphaBlend));
	BOOST_TEST((description.materials[0].render_mode != Assets::MaterialRenderMode::AlphaBlend));
}
