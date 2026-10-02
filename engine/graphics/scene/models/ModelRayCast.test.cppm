module;
#define BOOST_TEST_MODULE ModelRayCastTests
#include <boost/test/included/unit_test.hpp>
export module Graphics.Scene.Models.RayCast.Tests;
import std;
import Graphics.Scene.Models.RayCast;
import Graphics.Scene.Models.CollisionBuild;
import Assets.Identity;
import Assets.Models;

// Legacy parity for picking (GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp
// RTS3DScene::castRay; W3DHierarchyRenderObject.cpp / W3DMeshRenderObject.cpp Cast_Ray; W3DMeshQueries.h Collide).
// Expected fractions follow from the geometry: a ray along +x from x=0 to x=100 meets a plane x=d at d/100.
namespace
{
using Engine::Math::AffineTransform3;
using Engine::Math::Vector3;
using namespace Graphics;

// A square facing +x at x = `at` (two triangles, y and z in [-size, size]), as one mesh appended to `shape`.
void AddWall(ModelCollisionShape &shape, float at, float size, std::uint8_t flags = 0)
{
	ModelCollisionMesh mesh;
	mesh.firstVertex = static_cast<std::uint32_t>(shape.x.size());
	mesh.firstIndex = static_cast<std::uint32_t>(shape.indices.size());
	for (const auto [y, z] : std::array<std::pair<float, float>, 4>{{{-size, -size}, {size, -size}, {size, size}, {-size, size}}})
	{
		shape.x.push_back(at);
		shape.y.push_back(y);
		shape.z.push_back(z);
	}
	for (const std::uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u})
		shape.indices.push_back(index);
	mesh.vertexCount = 4;
	mesh.indexCount = 6;
	mesh.flags = flags;
	shape.meshes.push_back(mesh);
}

// Where a hit ends the ray: the legacy Point_At (start + (end - start) * fraction) in float; a fraction such as 0.3 is not exact
// in float, so the end is checked to 1e-5, the rounding of a float near 100.
bool At(float value, float expected) { return std::abs(value - expected) <= 1e-5f; }

ModelRayCaster AlongX() { return {{0.0f, 0.0f, 0.0f}, {100.0f, 0.0f, 0.0f}}; }
std::vector<AffineTransform3> Identities(std::size_t count) { return std::vector<AffineTransform3>(count, AffineTransform3{}); }
}

// Collide keeps only a strictly nearer hit: of two meshes the nearer wins whatever their order, and the segment ends there.
BOOST_AUTO_TEST_CASE(legacy_parity_the_nearest_triangle_of_a_model_wins)
{
	ModelCollisionShape shape;
	AddWall(shape, 60.0f, 10.0f);
	AddWall(shape, 30.0f, 10.0f);
	ModelRayCaster ray = AlongX();
	const auto worlds = Identities(2);
	BOOST_TEST(CastModelRay(shape, worlds, {}, ray));
	BOOST_TEST(At(ray.end.x, 30.0f));
	BOOST_TEST(ray.end.y == 0.0f);
}

// Cast_Ray with CheckTranslucent: an alpha-blended mesh (headlight beams) is passed through; a hidden one too, and one
// its animation hides.
BOOST_AUTO_TEST_CASE(legacy_parity_alpha_hidden_and_animation_hidden_meshes_are_passed_through)
{
	for (const int skipped : {0, 1, 2})
	{
		ModelCollisionShape shape;
		AddWall(shape, 20.0f, 10.0f, skipped == 0 ? model_collision_flag::Alpha : skipped == 1 ? model_collision_flag::Hidden : 0);
		AddWall(shape, 50.0f, 10.0f);
		const std::array<std::uint8_t, 2> visible{static_cast<std::uint8_t>(skipped == 2 ? 0 : 1), 1};
		ModelRayCaster ray = AlongX();
		BOOST_TEST(CastModelRay(shape, Identities(2), visible, ray));
		BOOST_TEST(At(ray.end.x, 50.0f));
	}
}

// castRay's refinement: once a model is hit the ray ends there, so a model farther along is not hit, and one nearer is.
BOOST_AUTO_TEST_CASE(legacy_parity_models_are_tried_along_a_ray_shortened_by_each_hit)
{
	ModelCollisionShape closer, farther;
	AddWall(closer, 40.0f, 10.0f);
	AddWall(farther, 70.0f, 10.0f);
	ModelRayCaster ray = AlongX();
	BOOST_TEST(CastModelRay(closer, Identities(1), {}, ray));
	BOOST_TEST(!CastModelRay(farther, Identities(1), {}, ray));
	BOOST_TEST(At(ray.end.x, 40.0f));
	ModelRayCaster other = AlongX();
	BOOST_TEST(CastModelRay(farther, Identities(1), {}, other));
	BOOST_TEST(CastModelRay(closer, Identities(1), {}, other));
	BOOST_TEST(At(other.end.x, 40.0f));
}

// A mesh's world transform places it: a wall at x=10 in its own space, moved 25 along x, is met at 35.
BOOST_AUTO_TEST_CASE(legacy_parity_a_mesh_is_tested_in_its_own_space)
{
	ModelCollisionShape shape;
	AddWall(shape, 10.0f, 5.0f);
	const std::array<AffineTransform3, 1> worlds{AffineTransform3::From_Translation({25.0f, 0.0f, 0.0f})};
	ModelRayCaster ray = AlongX();
	BOOST_TEST(CastModelRay(shape, worlds, {}, ray));
	BOOST_TEST(At(ray.end.x, 35.0f));
	// Beside it (y 6 > its half size 5) the ray misses.
	ModelRayCaster beside{{0.0f, 6.0f, 0.0f}, {100.0f, 6.0f, 0.0f}};
	BOOST_TEST(!CastModelRay(shape, worlds, {}, beside));
}

// ALIGNED (W3DMeshRenderObject::Cast_Ray): the mesh is turned so its +x axis points back along the ray, whichever way it
// was placed: a card authored edge-on to the ray (in the plane y = 0, met by a ray along +y only edge-on) is met face on.
BOOST_AUTO_TEST_CASE(legacy_parity_an_aligned_mesh_is_turned_to_face_the_ray)
{
	// The card: the square x = 0 (facing +x); the ray comes along +y from y = -50 to 50 through the mesh's origin at
	// (0, 0, 0): unturned the ray lies in the card's plane and misses; turned to face the ray it is met at its origin.
	ModelCollisionShape shape;
	AddWall(shape, 0.0f, 5.0f);
	ModelRayCaster plain{{0.0f, -50.0f, 0.0f}, {0.0f, 50.0f, 0.0f}};
	BOOST_TEST(!CastModelRay(shape, Identities(1), {}, plain));
	shape.meshes[0].flags = model_collision_flag::Aligned;
	ModelRayCaster aligned{{0.0f, -50.0f, 0.0f}, {0.0f, 50.0f, 0.0f}};
	BOOST_TEST(CastModelRay(shape, Identities(1), {}, aligned));
	BOOST_TEST(std::abs(aligned.end.y) < 1e-4f); // half way: fraction 0.5 (float rounding of the turned basis)
}

// castRay's ray-sphere test tests the ray's whole line from its start: a sphere within reach of the line passes, one off it
// does not (even ahead of the start).
BOOST_AUTO_TEST_CASE(legacy_parity_the_sphere_test_is_on_the_rays_line)
{
	const ModelRayCaster ray = AlongX();
	BOOST_TEST(RayNearSphere(ray, {50.0f, 3.0f, 0.0f}, 3.0f));  // touching: beta 0
	BOOST_TEST(!RayNearSphere(ray, {50.0f, 3.1f, 0.0f}, 3.0f));
	BOOST_TEST(RayNearSphere(ray, {-20.0f, 1.0f, 0.0f}, 3.0f)); // behind the start, on the line: still passes
	BOOST_TEST(RayNearSphere(ray, {500.0f, 0.0f, 0.0f}, 1.0f)); // past the end: still passes (the line)
}

// Legacy parity (W3DMeshRenderObject::Load_W3D flags and Cast_Ray per mesh; W3DHierarchyRenderObject's sub-objects on
// their HLOD attachments): a model of two source meshes gives two collision meshes, each on its attachment's bone (the
// lowest level of detail's), made of its pass 0 triangles only; one blended on pass 0 is Alpha, a hidden one Hidden, a
// camera aligned one Aligned.
BOOST_AUTO_TEST_CASE(legacy_parity_a_models_collision_shape_follows_its_meshes)
{
	Assets::ModelAssetDesc description;
	description.name = "tank";
	for (int vertex = 0; vertex < 9; ++vertex)
		description.vertices.push_back({{static_cast<float>(vertex), 0.0f, 0.0f}});
	description.indices = {0, 1, 2, 0, 1, 2, 3, 4, 5, 6, 7, 8};
	description.materials.push_back({"paint", ""});
	// HULL: pass 0 (opaque) and pass 1 (its second pass over the same triangle); GLASS: two pass 0 runs, one blended.
	description.submeshes.push_back({0, 3, 0, "HULL", false, Graphics::W3D_Mesh_Hidden, 0, false});
	description.submeshes.push_back({3, 3, 0, "HULL", false, Graphics::W3D_Mesh_Hidden, 1, true});
	description.submeshes.push_back({6, 3, 0, "GLASS", false, Graphics::W3D_Mesh_Camera_Aligned, 0, false});
	description.submeshes.push_back({9, 3, 0, "GLASS", false, Graphics::W3D_Mesh_Camera_Aligned, 0, true});
	description.rig.skeleton_name = "TANK";
	description.rig.bones = {{"ROOT"}, {"TURRET", 0}};
	description.rig.attachments = {{"TANK.HULL", 0, 1}, {"TANK.HULL", 1, 0}, {"TANK.GLASS", 1, 0}};
	const Assets::ModelAsset model({Assets::AssetType::Model, "tank.w3d"}, std::move(description));

	ModelCollisionShape shape;
	std::vector<std::uint32_t> bones;
	std::string error;
	BOOST_REQUIRE_MESSAGE(Build_Model_Collision_Shape(model, shape, bones, error), error);
	BOOST_REQUIRE_EQUAL(shape.meshes.size(), 2u);
	BOOST_TEST(bones[0] == 1u); // HULL: its level 0 attachment's bone (the level 1 one is a lower level of detail)
	BOOST_TEST(bones[1] == 1u);
	BOOST_TEST(shape.meshes[0].indexCount == 3u); // pass 1's triangle is the same triangle again: not counted
	BOOST_TEST(shape.meshes[1].indexCount == 6u);
	BOOST_TEST(shape.meshes[0].flags == model_collision_flag::Hidden); // pass 1 blending does not make it alpha
	BOOST_TEST(shape.meshes[1].flags == (model_collision_flag::Aligned | model_collision_flag::Alpha));
	BOOST_TEST(shape.x[shape.meshes[1].firstVertex + 3] == 6.0f);
}
