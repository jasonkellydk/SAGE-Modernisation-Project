module;

#define BOOST_TEST_MODULE GeneralsAssetsW3DModelBuilderTests

#include <boost/test/included/unit_test.hpp>

#include <array>
#include <cstdint>
#include <string>

export module Assets.Tests.W3DModelBuilder;

import Assets.Adapters.W3D.Mesh;
import Assets.Adapters.W3D.Model;
import Assets.Models;

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
	mesh.materials.vertex_materials.push_back({"body_material", "Body.TGA"});
	mesh.materials.textures.push_back("Body.TGA");
	mesh.materials.passes.push_back({0, 0, {}});

	Assets::ModelAssetDesc description;
	Assets::W3D::W3DAppend_Mesh(description, mesh);

	BOOST_REQUIRE_EQUAL(description.vertices.size(), 3);
	BOOST_REQUIRE_EQUAL(description.indices.size(), 3);
	BOOST_REQUIRE_EQUAL(description.submeshes.size(), 1);
	BOOST_REQUIRE_EQUAL(description.materials.size(), 1);
	BOOST_CHECK(description.submeshes[0].material_index == 0);
	BOOST_CHECK(description.materials[0].primary_texture == "Body.TGA");
	BOOST_CHECK(description.bounds.Is_Valid());
	BOOST_CHECK(description.sort_level == 3);
	BOOST_REQUIRE_EQUAL(description.dependencies.size(), 2);
}
