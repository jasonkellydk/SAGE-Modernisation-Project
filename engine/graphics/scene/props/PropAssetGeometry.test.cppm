module;
#define BOOST_TEST_MODULE PropAssetGeometryTests
#include <boost/test/included/unit_test.hpp>
export module Graphics.Scene.Props.AssetGeometry.Tests;
import std;
import Graphics.Scene.Props.AssetGeometry;
import Assets.Models;
import Assets.Identity;

BOOST_AUTO_TEST_CASE(model_conversion_keeps_large_indices_uvs_normals_and_tangent_sign)
{
    Assets::ModelAssetDesc source;
    source.vertices.resize(70000);
    source.vertices[69999].position={3,4,5};
    source.vertices[69999].normal={0,1,0};
    source.vertices[69999].tangent={1,0,0}; source.vertices[69999].tangent_sign=-1;
    source.vertices[69999].texcoord={.25f,.75f};
    source.indices={69999,1,2,1,69999,3};
    source.materials.push_back({"metal"});
    source.submeshes.push_back({0,6,0,"panels"});
    Assets::ModelAsset model({Assets::AssetType::Model,"test"},source);
    std::vector<Graphics::PropAssetPart> parts; std::string error;
    BOOST_REQUIRE(Graphics::Build_Prop_Asset_Geometry(model,parts,error));
    BOOST_REQUIRE_EQUAL(parts.size(),1u);
    BOOST_TEST(parts[0].indices.size()==6u); BOOST_TEST(parts[0].vertices.size()==4u);
    const auto& vertex=parts[0].vertices[parts[0].indices[0]];
    BOOST_TEST(vertex.position[2]==5); BOOST_TEST(vertex.normal[1]==1);
    BOOST_TEST(vertex.tangent[3]==-1); BOOST_TEST(vertex.uv[1]==.75f);
    BOOST_TEST(parts[0].indices[0]==parts[0].indices[4]);
    source.skin_bone_count=1;
    Assets::ModelAsset skinned({Assets::AssetType::Model,"skin"},source);
    BOOST_TEST(!Graphics::Build_Prop_Asset_Geometry(skinned,parts,error));
    BOOST_TEST(parts.size()==1u); BOOST_TEST(error.find("pose")!=std::string::npos);
    source.skin_bone_count=0; source.indices[0]=70000;
    Assets::ModelAsset invalid({Assets::AssetType::Model,"bad"},source);
    BOOST_TEST(!Graphics::Build_Prop_Asset_Geometry(invalid,parts,error));
}

BOOST_AUTO_TEST_CASE(flattened_geometry_keeps_per_part_static_order_without_inheriting_root)
{
    Assets::ModelAssetDesc source;
    source.sort_level=20;
    source.vertices.resize(3); source.indices={0,1,2};
    source.materials.push_back({"shared"});
    source.submeshes.push_back({0,3,0,"back"});
    source.submeshes.back().sort_level=5;
    source.submeshes.push_back({0,3,0,"front"});
    source.submeshes.back().sort_level=1;
    source.submeshes.push_back({0,3,0,"ordinary"});
    Assets::ModelAsset model({Assets::AssetType::Model,"mixed"},source);
    std::vector<Graphics::PropAssetPart> parts; std::string error;
    BOOST_REQUIRE(Graphics::Build_Prop_Asset_Geometry(model,parts,error));
    BOOST_REQUIRE_EQUAL(parts.size(),3u);
    BOOST_CHECK_EQUAL(parts[0].sort_level,5);
    BOOST_CHECK_EQUAL(parts[1].sort_level,1);
    BOOST_CHECK_EQUAL(parts[2].sort_level,0);
}
