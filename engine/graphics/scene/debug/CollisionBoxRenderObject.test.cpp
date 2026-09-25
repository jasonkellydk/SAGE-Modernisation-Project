#define BOOST_TEST_MODULE CollisionBoxRenderObjectQueryTests
#include <boost/test/included/unit_test.hpp>

#include <array>
#include <cmath>
#include <initializer_list>

import Engine.Core.Math.Vector3;
import Engine.Core.Math.AffineTransform3;
import Engine.Core.Math.LineSegment3;
import Engine.Core.Math.AxisAlignedBox3;
import Engine.Core.Math.OrientedBox3;

#include "W3DDevice/GameClient/CollisionBoxRenderObject.h"
#include "W3DDevice/GameClient/W3DSegmentedLineRenderObject.h"
#include "W3DDevice/GameClient/W3DCastQuery.h"
#include "W3DDevice/GameClient/W3DSceneQueryMask.h"
#include "W3DDevice/GameClient/W3DIntersectionQuery.h"
#include "WWLib/ref_ptr.h"

namespace
{

constexpr float kTolerance = 0.0001f;

Engine::Math::LineSegment3 Make_Line_Segment(Engine::Math::Vector3 start, Engine::Math::Vector3 end)
{
	return {start, end};
}

void Check_Vector(Engine::Math::Vector3 actual, Engine::Math::Vector3 expected)
{
	BOOST_CHECK_SMALL(actual.x - expected.x, kTolerance);
	BOOST_CHECK_SMALL(actual.y - expected.y, kTolerance);
	BOOST_CHECK_SMALL(actual.z - expected.z, kTolerance);
}

Engine::Math::AxisAlignedBox3 Axis_Aligned_Box(Engine::Math::Vector3 center, Engine::Math::Vector3 extent)
{
	return {center - extent, center + extent};
}

Engine::Math::OrientedBox3 Oriented_Box(Engine::Math::Vector3 center,
	Engine::Math::Vector3 extent, bool rotated = false)
{
	const std::array<Engine::Math::Vector3, 3> axes = rotated
		? std::array<Engine::Math::Vector3, 3>{{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}}}
		: std::array<Engine::Math::Vector3, 3>{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
	return {center, extent, axes};
}

void Check_Fraction(const Engine::Math::CollisionResult3 &result, float expected)
{
	BOOST_CHECK_SMALL(result.fraction - expected, kTolerance);
}

Engine::Math::AffineTransform3 Rotated_Translated_Transform()
{
	Engine::Math::AffineTransform3 transform;
	transform.elements = {0, -1, 0, 10, 1, 0, 0, 20, 0, 0, 1, 30};
	return transform;
}

void Check_Ray_Hit(CollisionBoxRenderObject &object,
	const Engine::Math::LineSegment3 &line,
	int collision_type,
	float expected_fraction,
	Engine::Math::Vector3 expected_normal,
	Engine::Math::Vector3 expected_contact)
{
	Engine::Math::CollisionResult3 result;
	result.compute_contact_point = true;
	W3DRayCastQuery ray(line, &result, collision_type);
	BOOST_REQUIRE(object.Cast_Ray(ray));
	Check_Fraction(result, expected_fraction);
	Check_Vector(result.normal, expected_normal);
	Check_Vector(result.contact_point, expected_contact);
	BOOST_CHECK(ray.CollidedRenderObj == &object);
}

void Check_Swept_Hit(CollisionBoxRenderObject &object,
	W3DBoxCastQuery &test,
	float expected_fraction,
	Engine::Math::Vector3 expected_normal)
{
	BOOST_REQUIRE(object.Cast_AABox(test));
	Check_Fraction(*test.Result, expected_fraction);
	Check_Vector(test.Result->normal, expected_normal);
	BOOST_CHECK(test.CollidedRenderObj == &object);
}

void Check_Swept_Hit(CollisionBoxRenderObject &object,
	W3DOrientedBoxCastQuery &test,
	float expected_fraction,
	Engine::Math::Vector3 expected_normal)
{
	BOOST_REQUIRE(object.Cast_OBBox(test));
	Check_Fraction(*test.Result, expected_fraction);
	Check_Vector(test.Result->normal, expected_normal);
	BOOST_CHECK(test.CollidedRenderObj == &object);
}

}

BOOST_AUTO_TEST_CASE(moved_adapter_preserves_aa_and_ob_bounds_after_rotation_and_translation)
{
	const Engine::Math::Vector3 local_center{1, 2, 3};
	const Engine::Math::Vector3 local_extent{2, 3, 4};
	const auto transform = Rotated_Translated_Transform();

	RefCountPtr<CollisionBoxRenderObject> aa_object =
		Create_No_Add_Ref(new CollisionBoxRenderObject);
	aa_object->Set_Local_Center_Extent(local_center, local_extent);
	aa_object->Set_Transform(transform);
	const Engine::Math::AxisAlignedBox3 &aa_box = aa_object->Get_AA_Box();
	Check_Vector((aa_box.minimum + aa_box.maximum) * 0.5f, Engine::Math::Vector3{11, 22, 33});
	Check_Vector((aa_box.maximum - aa_box.minimum) * 0.5f, Engine::Math::Vector3{2, 3, 4});

	RefCountPtr<CollisionBoxRenderObject> ob_object =
		Create_No_Add_Ref(new CollisionBoxRenderObject(Engine::Math::OrientedBox3{
			{}, {2, 3, 4}, {{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}}}}));
	BOOST_CHECK_EQUAL(ob_object->Class_ID(), W3DRenderObject::CLASSID_OBBOX);
	ob_object->Set_Local_Center_Extent(local_center, local_extent);
	ob_object->Set_Transform(transform);
	const Engine::Math::OrientedBox3 &ob_box = ob_object->Get_OB_Box();
	Check_Vector(ob_box.center, Engine::Math::Vector3{8, 21, 33});
	Check_Vector(ob_box.half_extent, Engine::Math::Vector3{2, 3, 4});
	Check_Vector(ob_box.axes[0], Engine::Math::Vector3{0, 1, 0});
	Check_Vector(ob_box.axes[1], Engine::Math::Vector3{-1, 0, 0});
	Check_Vector(ob_box.axes[2], Engine::Math::Vector3{0, 0, 1});

	// Set_Position keeps the oriented basis while moving the transformed center.
	ob_object->Set_Position(Engine::Math::Vector3{-4, 6, 7});
	Check_Vector(ob_object->Get_OB_Box().center, Engine::Math::Vector3{-6, 7, 10});
}

BOOST_AUTO_TEST_CASE(query_masks_animation_hidden_and_preexisting_startbad_are_rejected)
{
	RefCountPtr<CollisionBoxRenderObject> object =
		Create_No_Add_Ref(new CollisionBoxRenderObject(
			Axis_Aligned_Box(Engine::Math::Vector3{0, 0, 0}, Engine::Math::Vector3{1, 1, 1})));
	object->Set_Collision_Type(SCENE_QUERY_PHYSICAL);
	const auto line = Make_Line_Segment(Engine::Math::Vector3{-3, 0, 0}, Engine::Math::Vector3{3, 0, 0});

	Engine::Math::CollisionResult3 masked_result;
	masked_result.fraction = 0.25f;
	masked_result.normal = Engine::Math::Vector3{0, 1, 0};
	W3DRayCastQuery masked(line, &masked_result, SCENE_QUERY_PROJECTILE);
	BOOST_CHECK(!object->Cast_Ray(masked));
	Check_Fraction(masked_result, 0.25f);
	Check_Vector(masked_result.normal, Engine::Math::Vector3{0, 1, 0});
	BOOST_CHECK(masked.CollidedRenderObj == nullptr);

	object->Set_Animation_Hidden(true);
	Engine::Math::CollisionResult3 hidden_result;
	W3DRayCastQuery hidden(line, &hidden_result, SCENE_QUERY_PHYSICAL);
	BOOST_CHECK(!object->Cast_Ray(hidden));
	BOOST_CHECK(hidden.CollidedRenderObj == nullptr);
	BOOST_CHECK(!hidden_result.starts_overlapping);

	object->Set_Animation_Hidden(false);
	Engine::Math::CollisionResult3 startbad_result;
	startbad_result.starts_overlapping = true;
	startbad_result.fraction = 0.5f;
	startbad_result.normal = Engine::Math::Vector3{0, 0, 1};
	W3DRayCastQuery startbad(line, &startbad_result, SCENE_QUERY_PHYSICAL);
	BOOST_CHECK(!object->Cast_Ray(startbad));
	BOOST_CHECK(startbad_result.starts_overlapping);
	Check_Fraction(startbad_result, 0.5f);
	Check_Vector(startbad_result.normal, Engine::Math::Vector3{0, 0, 1});
	BOOST_CHECK(startbad.CollidedRenderObj == nullptr);
}

BOOST_AUTO_TEST_CASE(ray_queries_return_known_fraction_normal_contact_and_object)
{
	const int collision_type = SCENE_QUERY_PROJECTILE;

	RefCountPtr<CollisionBoxRenderObject> aa_object =
		Create_No_Add_Ref(new CollisionBoxRenderObject(
			Axis_Aligned_Box(Engine::Math::Vector3{5, 0, 0}, Engine::Math::Vector3{1, 2, 3})));
	aa_object->Set_Collision_Type(collision_type);
	Check_Ray_Hit(*aa_object,
		Make_Line_Segment(Engine::Math::Vector3{-1, 0, 0}, Engine::Math::Vector3{9, 0, 0}),
		collision_type, 0.5f, Engine::Math::Vector3{-1, 0, 0}, Engine::Math::Vector3{4, 0, 0});

	const auto oriented_box = Oriented_Box(Engine::Math::Vector3{5, 0, 0}, Engine::Math::Vector3{1, 2, 3}, true);
	RefCountPtr<CollisionBoxRenderObject> ob_object =
		Create_No_Add_Ref(new CollisionBoxRenderObject(oriented_box));
	ob_object->Set_Collision_Type(collision_type);
	Check_Ray_Hit(*ob_object,
		Make_Line_Segment(Engine::Math::Vector3{5, -5, 0}, Engine::Math::Vector3{5, 5, 0}),
		collision_type, 0.4f, Engine::Math::Vector3{0, -1, 0}, Engine::Math::Vector3{5, -1, 0});
}

BOOST_AUTO_TEST_CASE(ray_starting_inside_box_reports_start_overlap_like_legacy_collide)
{
	const int collision_type = SCENE_QUERY_PHYSICAL;

	RefCountPtr<CollisionBoxRenderObject> aa_object =
		Create_No_Add_Ref(new CollisionBoxRenderObject(
			Axis_Aligned_Box(Engine::Math::Vector3{0, 0, 0}, Engine::Math::Vector3{1, 1, 1})));
	aa_object->Set_Collision_Type(collision_type);

	RefCountPtr<CollisionBoxRenderObject> ob_object =
		Create_No_Add_Ref(new CollisionBoxRenderObject(
			Oriented_Box(Engine::Math::Vector3{0, 0, 0}, Engine::Math::Vector3{1, 2, 1}, true)));
	ob_object->Set_Collision_Type(collision_type);

	const Engine::Math::Vector3 start{0.5f, 0.25f, 0};
	const auto line = Make_Line_Segment(start, Engine::Math::Vector3{5, 0.25f, 0});
	for (CollisionBoxRenderObject *object : {aa_object.Peek(), ob_object.Peek()}) {
		Engine::Math::CollisionResult3 result;
		result.compute_contact_point = true;
		result.fraction = 0.75f;
		result.normal = Engine::Math::Vector3{0, 0, 1};
		W3DRayCastQuery ray(line, &result, collision_type);
		// Legacy StartBad: the query hits, the start is the contact point, and
		// the fraction and normal are left untouched.
		BOOST_REQUIRE(object->Cast_Ray(ray));
		BOOST_CHECK(result.starts_overlapping);
		Check_Fraction(result, 0.75f);
		Check_Vector(result.normal, Engine::Math::Vector3{0, 0, 1});
		Check_Vector(result.contact_point, start);
		BOOST_CHECK(ray.CollidedRenderObj == object);

		// Once StartBad is set, later casts with the same result are rejected.
		W3DRayCastQuery again(line, &result, collision_type);
		BOOST_CHECK(!object->Cast_Ray(again));
		BOOST_CHECK(again.CollidedRenderObj == nullptr);
	}
}

BOOST_AUTO_TEST_CASE(segmented_line_ray_queries_preserve_mask_miss_transform_and_order)
{
	W3DSegmentedLineRenderObject object;
	object.Set_Collision_Type(SCENE_QUERY_PHYSICAL);
	object.Set_Width(0.5f);
	const Engine::Math::Vector3 points[] = {
		{-2, -1, 0}, {2, -1, 0}, {2, 1, 0}, {-2, 1, 0}};
	object.Set_Points(4, points);

	const auto transform = Rotated_Translated_Transform();
	object.Set_Transform(transform);

	Engine::Math::CollisionResult3 masked_result;
	masked_result.fraction = 0.25f;
	const auto world_ray = Make_Line_Segment(Engine::Math::Vector3{5, 20, 30}, Engine::Math::Vector3{15, 20, 30});
	W3DRayCastQuery masked(world_ray, &masked_result, SCENE_QUERY_PROJECTILE);
	BOOST_CHECK(!object.Cast_Ray(masked));
	BOOST_CHECK_EQUAL(masked_result.fraction, 0.25f);
	BOOST_CHECK(masked.CollidedRenderObj == nullptr);

	Engine::Math::CollisionResult3 miss_result;
	W3DRayCastQuery miss(
		Make_Line_Segment(Engine::Math::Vector3{5, 20, 31}, Engine::Math::Vector3{15, 20, 31}),
		&miss_result, SCENE_QUERY_PHYSICAL);
	BOOST_CHECK(!object.Cast_Ray(miss));
	BOOST_CHECK_EQUAL(miss_result.fraction, 1.0f);
	BOOST_CHECK(miss.CollidedRenderObj == nullptr);

	Engine::Math::CollisionResult3 hit_result;
	W3DRayCastQuery hit(world_ray, &hit_result, SCENE_QUERY_PHYSICAL);
	BOOST_REQUIRE(object.Cast_Ray(hit));
	// The first transformed segment is at x=11 (fraction .6). The later
	// segment is at x=9 (fraction .4), but queries stop at the first accepted hit.
	BOOST_CHECK_CLOSE(hit_result.fraction, 0.6f, 0.001f);
	BOOST_CHECK_EQUAL(hit_result.surface_type, 13u);
	BOOST_CHECK(hit.CollidedRenderObj == &object);

	// Picking uses the full authored width, independently of the drawn radius.
	Engine::Math::CollisionResult3 width_result;
	W3DRayCastQuery within_width(
		Make_Line_Segment(Engine::Math::Vector3{5, 20, 30.4f}, Engine::Math::Vector3{15, 20, 30.4f}),
		&width_result, SCENE_QUERY_PHYSICAL);
	BOOST_REQUIRE(object.Cast_Ray(within_width));
	Check_Fraction(width_result, 0.6f);

	Engine::Math::CollisionResult3 nearer_result;
	nearer_result.fraction = 0.5f;
	W3DRayCastQuery nearer(world_ray, &nearer_result, SCENE_QUERY_PHYSICAL);
	BOOST_REQUIRE(object.Cast_Ray(nearer));
	Check_Fraction(nearer_result, 0.4f);

	Engine::Math::CollisionResult3 limited_result;
	limited_result.fraction = 0.3f;
	W3DRayCastQuery limited(world_ray, &limited_result, SCENE_QUERY_PHYSICAL);
	BOOST_CHECK(!object.Cast_Ray(limited));
	Check_Fraction(limited_result, 0.3f);
	BOOST_CHECK(limited.CollidedRenderObj == nullptr);
}

BOOST_AUTO_TEST_CASE(swept_aa_and_ob_queries_preserve_known_hits_and_normals)
{
	const int collision_type = SCENE_QUERY_PHYSICAL;

	RefCountPtr<CollisionBoxRenderObject> aa_target =
		Create_No_Add_Ref(new CollisionBoxRenderObject(
			Axis_Aligned_Box(Engine::Math::Vector3{5, 0, 0}, Engine::Math::Vector3{1, 1, 1})));
	aa_target->Set_Collision_Type(collision_type);

	Engine::Math::CollisionResult3 aa_result;
	W3DBoxCastQuery moving_aa(
		Axis_Aligned_Box(Engine::Math::Vector3{-5, 0, 0}, Engine::Math::Vector3{1, 1, 1}),
		Engine::Math::Vector3{10, 0, 0}, &aa_result, collision_type);
	Check_Swept_Hit(*aa_target, moving_aa, 0.8f, Engine::Math::Vector3{-1, 0, 0});

	Engine::Math::CollisionResult3 ob_result;
	const auto moving_ob_box = Oriented_Box(Engine::Math::Vector3{-5, 0, 0}, Engine::Math::Vector3{1, 1, 1});
	W3DOrientedBoxCastQuery moving_ob(moving_ob_box,
		Engine::Math::Vector3{10, 0, 0}, &ob_result, collision_type);
	Check_Swept_Hit(*aa_target, moving_ob, 0.8f, Engine::Math::Vector3{-1, 0, 0});

	const auto oriented_target_box = Oriented_Box(Engine::Math::Vector3{5, 0, 0}, Engine::Math::Vector3{1, 2, 1}, true);
	RefCountPtr<CollisionBoxRenderObject> oriented_target =
		Create_No_Add_Ref(new CollisionBoxRenderObject(oriented_target_box));
	oriented_target->Set_Collision_Type(collision_type);

	Engine::Math::CollisionResult3 oriented_aa_result;
	W3DBoxCastQuery moving_aa_again(
		Axis_Aligned_Box(Engine::Math::Vector3{5, -5, 0}, Engine::Math::Vector3{1, 1, 1}),
		Engine::Math::Vector3{0, 10, 0}, &oriented_aa_result, collision_type);
	Check_Swept_Hit(*oriented_target, moving_aa_again, 0.3f, Engine::Math::Vector3{0, -1, 0});

	Engine::Math::CollisionResult3 oriented_ob_result;
	const auto moving_ob_box_again = Oriented_Box(Engine::Math::Vector3{5, -5, 0}, Engine::Math::Vector3{1, 1, 1});
	W3DOrientedBoxCastQuery moving_ob_again(moving_ob_box_again,
		Engine::Math::Vector3{0, 10, 0}, &oriented_ob_result, collision_type);
	Check_Swept_Hit(*oriented_target, moving_ob_again, 0.3f, Engine::Math::Vector3{0, -1, 0});

	Engine::Math::CollisionResult3 miss_result;
	W3DBoxCastQuery moving_away(
		Axis_Aligned_Box(Engine::Math::Vector3{-5, 0, 0}, Engine::Math::Vector3{1, 1, 1}),
		Engine::Math::Vector3{-10, 0, 0}, &miss_result, collision_type);
	BOOST_CHECK(!aa_target->Cast_AABox(moving_away));
	BOOST_CHECK(moving_away.CollidedRenderObj == nullptr);
	Check_Fraction(miss_result, 1.0f);
}

BOOST_AUTO_TEST_CASE(intersection_queries_cover_aa_ob_hit_miss_and_masks)
{
	const int collision_type = SCENE_QUERY_PHYSICAL;
	RefCountPtr<CollisionBoxRenderObject> aa_target =
		Create_No_Add_Ref(new CollisionBoxRenderObject(
			Axis_Aligned_Box(Engine::Math::Vector3{0, 0, 0}, Engine::Math::Vector3{1, 1, 1})));
	aa_target->Set_Collision_Type(collision_type);

	W3DBoxIntersectionQuery aa_hit(
		Axis_Aligned_Box(Engine::Math::Vector3{1.5f, 0, 0}, Engine::Math::Vector3{1, 1, 1}), collision_type);
	W3DBoxIntersectionQuery aa_miss(
		Axis_Aligned_Box(Engine::Math::Vector3{2.1f, 0, 0}, Engine::Math::Vector3{1, 1, 1}), collision_type);
	BOOST_CHECK(aa_target->Intersect_AABox(aa_hit));
	BOOST_CHECK(!aa_target->Intersect_AABox(aa_miss));

	W3DOrientedBoxIntersectionQuery ob_hit(
		Oriented_Box(Engine::Math::Vector3{0, 1.5f, 0}, Engine::Math::Vector3{1, 1, 1}, true), collision_type);
	W3DOrientedBoxIntersectionQuery ob_miss(
		Oriented_Box(Engine::Math::Vector3{0, 2.1f, 0}, Engine::Math::Vector3{1, 1, 1}, true), collision_type);
	BOOST_CHECK(aa_target->Intersect_OBBox(ob_hit));
	BOOST_CHECK(!aa_target->Intersect_OBBox(ob_miss));

	const auto oriented_target_box = Oriented_Box(Engine::Math::Vector3{0, 0, 0}, Engine::Math::Vector3{1, 2, 1}, true);
	RefCountPtr<CollisionBoxRenderObject> oriented_target =
		Create_No_Add_Ref(new CollisionBoxRenderObject(oriented_target_box));
	oriented_target->Set_Collision_Type(collision_type);

	W3DBoxIntersectionQuery oriented_aa_hit(
		Axis_Aligned_Box(Engine::Math::Vector3{0, 1.5f, 0}, Engine::Math::Vector3{1, 1, 1}), collision_type);
	W3DBoxIntersectionQuery oriented_aa_miss(
		Axis_Aligned_Box(Engine::Math::Vector3{0, 2.1f, 0}, Engine::Math::Vector3{1, 1, 1}), collision_type);
	BOOST_CHECK(oriented_target->Intersect_AABox(oriented_aa_hit));
	BOOST_CHECK(!oriented_target->Intersect_AABox(oriented_aa_miss));

	W3DOrientedBoxIntersectionQuery oriented_ob_hit(
		Oriented_Box(Engine::Math::Vector3{1.5f, 0, 0}, Engine::Math::Vector3{1, 1, 1}), collision_type);
	W3DOrientedBoxIntersectionQuery oriented_ob_miss(
		Oriented_Box(Engine::Math::Vector3{3.1f, 0, 0}, Engine::Math::Vector3{1, 1, 1}), collision_type);
	BOOST_CHECK(oriented_target->Intersect_OBBox(oriented_ob_hit));
	BOOST_CHECK(!oriented_target->Intersect_OBBox(oriented_ob_miss));

	W3DBoxIntersectionQuery masked(
		Axis_Aligned_Box(Engine::Math::Vector3{0, 0, 0}, Engine::Math::Vector3{1, 1, 1}), SCENE_QUERY_PROJECTILE);
	BOOST_CHECK(!oriented_target->Intersect_AABox(masked));
}
