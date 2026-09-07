#define BOOST_TEST_MODULE CollisionBoxRenderObjectQueryTests
#include <boost/test/included/unit_test.hpp>

#include <cmath>

#include "W3DDevice/GameClient/CollisionBoxRenderObject.h"
#include "WW3D2/ColTest.h"
#include "WW3D2/ColType.h"
#include "WW3D2/IntTest.h"
#include "WWMath/aabox.h"
#include "WWMath/castres.h"
#include "WWMath/lineseg.h"
#include "WWMath/matrix3d.h"
#include "WWMath/obbox.h"
#include "WWLib/ref_ptr.h"

namespace
{

constexpr float kTolerance = 0.0001f;

void Check_Vector(const Vector3 &actual, const Vector3 &expected)
{
	BOOST_CHECK_SMALL(actual.X - expected.X, kTolerance);
	BOOST_CHECK_SMALL(actual.Y - expected.Y, kTolerance);
	BOOST_CHECK_SMALL(actual.Z - expected.Z, kTolerance);
}

void Check_Fraction(const CastResultStruct &result, float expected)
{
	BOOST_CHECK_SMALL(result.Fraction - expected, kTolerance);
}

Matrix3D Rotated_Translated_Transform()
{
	Matrix3D transform = Matrix3D::RotateZ90;
	transform.Set_Translation(Vector3(10, 20, 30));
	return transform;
}

Matrix3x3 Rotated_Basis()
{
	return Matrix3x3(Matrix3D::RotateZ90);
}

void Check_Ray_Hit(CollisionBoxRenderObject &object,
	const LineSegClass &line,
	int collision_type,
	float expected_fraction,
	const Vector3 &expected_normal,
	const Vector3 &expected_contact)
{
	CastResultStruct result;
	result.ComputeContactPoint = true;
	RayCollisionTestClass ray(line, &result, collision_type);
	BOOST_REQUIRE(object.Cast_Ray(ray));
	Check_Fraction(result, expected_fraction);
	Check_Vector(result.Normal, expected_normal);
	Check_Vector(result.ContactPoint, expected_contact);
	BOOST_CHECK(ray.CollidedRenderObj == &object);
}

void Check_Swept_Hit(CollisionBoxRenderObject &object,
	AABoxCollisionTestClass &test,
	float expected_fraction,
	const Vector3 &expected_normal)
{
	BOOST_REQUIRE(object.Cast_AABox(test));
	Check_Fraction(*test.Result, expected_fraction);
	Check_Vector(test.Result->Normal, expected_normal);
	BOOST_CHECK(test.CollidedRenderObj == &object);
}

void Check_Swept_Hit(CollisionBoxRenderObject &object,
	OBBoxCollisionTestClass &test,
	float expected_fraction,
	const Vector3 &expected_normal)
{
	BOOST_REQUIRE(object.Cast_OBBox(test));
	Check_Fraction(*test.Result, expected_fraction);
	Check_Vector(test.Result->Normal, expected_normal);
	BOOST_CHECK(test.CollidedRenderObj == &object);
}

}

BOOST_AUTO_TEST_CASE(moved_adapter_preserves_aa_and_ob_bounds_after_rotation_and_translation)
{
	const Vector3 local_center(1, 2, 3);
	const Vector3 local_extent(2, 3, 4);
	const Matrix3D transform = Rotated_Translated_Transform();

	RefCountPtr<CollisionBoxRenderObject> aa_object =
		Create_No_Add_Ref(new CollisionBoxRenderObject);
	aa_object->Set_Local_Center_Extent(local_center, local_extent);
	aa_object->Set_Transform(transform);
	const AABoxClass &aa_box = aa_object->Get_AA_Box();
	Check_Vector(aa_box.Center, Vector3(11, 22, 33));
	Check_Vector(aa_box.Extent, local_extent);

	RefCountPtr<CollisionBoxRenderObject> ob_object =
		Create_No_Add_Ref(new CollisionBoxRenderObject(
			OBBoxClass(Vector3(0, 0, 0), local_extent, Rotated_Basis())));
	BOOST_CHECK_EQUAL(ob_object->Class_ID(), RenderObjClass::CLASSID_OBBOX);
	ob_object->Set_Local_Center_Extent(local_center, local_extent);
	ob_object->Set_Transform(transform);
	const OBBoxClass &ob_box = ob_object->Get_OB_Box();
	Check_Vector(ob_box.Center, Vector3(8, 21, 33));
	Check_Vector(ob_box.Extent, local_extent);
	Check_Vector(Vector3(ob_box.Basis[0][0], ob_box.Basis[1][0], ob_box.Basis[2][0]),
		Vector3(0, 1, 0));
	Check_Vector(Vector3(ob_box.Basis[0][1], ob_box.Basis[1][1], ob_box.Basis[2][1]),
		Vector3(-1, 0, 0));
	Check_Vector(Vector3(ob_box.Basis[0][2], ob_box.Basis[1][2], ob_box.Basis[2][2]),
		Vector3(0, 0, 1));

	// Set_Position keeps the oriented basis while moving the transformed center.
	ob_object->Set_Position(Vector3(-4, 6, 7));
	Check_Vector(ob_object->Get_OB_Box().Center, Vector3(-6, 7, 10));
}

BOOST_AUTO_TEST_CASE(query_masks_animation_hidden_and_preexisting_startbad_are_rejected)
{
	RefCountPtr<CollisionBoxRenderObject> object =
		Create_No_Add_Ref(new CollisionBoxRenderObject(
			AABoxClass(Vector3(0, 0, 0), Vector3(1, 1, 1))));
	object->Set_Collision_Type(COLL_TYPE_PHYSICAL);
	const LineSegClass line(Vector3(-3, 0, 0), Vector3(3, 0, 0));

	CastResultStruct masked_result;
	masked_result.Fraction = 0.25f;
	masked_result.Normal = Vector3(0, 1, 0);
	RayCollisionTestClass masked(line, &masked_result, COLL_TYPE_PROJECTILE);
	BOOST_CHECK(!object->Cast_Ray(masked));
	Check_Fraction(masked_result, 0.25f);
	Check_Vector(masked_result.Normal, Vector3(0, 1, 0));
	BOOST_CHECK(masked.CollidedRenderObj == nullptr);

	object->Set_Animation_Hidden(true);
	CastResultStruct hidden_result;
	RayCollisionTestClass hidden(line, &hidden_result, COLL_TYPE_PHYSICAL);
	BOOST_CHECK(!object->Cast_Ray(hidden));
	BOOST_CHECK(hidden.CollidedRenderObj == nullptr);
	BOOST_CHECK(!hidden_result.StartBad);

	object->Set_Animation_Hidden(false);
	CastResultStruct startbad_result;
	startbad_result.StartBad = true;
	startbad_result.Fraction = 0.5f;
	startbad_result.Normal = Vector3(0, 0, 1);
	RayCollisionTestClass startbad(line, &startbad_result, COLL_TYPE_PHYSICAL);
	BOOST_CHECK(!object->Cast_Ray(startbad));
	BOOST_CHECK(startbad_result.StartBad);
	Check_Fraction(startbad_result, 0.5f);
	Check_Vector(startbad_result.Normal, Vector3(0, 0, 1));
	BOOST_CHECK(startbad.CollidedRenderObj == nullptr);
}

BOOST_AUTO_TEST_CASE(ray_queries_return_known_fraction_normal_contact_and_object)
{
	const int collision_type = COLL_TYPE_PROJECTILE;

	RefCountPtr<CollisionBoxRenderObject> aa_object =
		Create_No_Add_Ref(new CollisionBoxRenderObject(
			AABoxClass(Vector3(5, 0, 0), Vector3(1, 2, 3))));
	aa_object->Set_Collision_Type(collision_type);
	Check_Ray_Hit(*aa_object,
		LineSegClass(Vector3(-1, 0, 0), Vector3(9, 0, 0)),
		collision_type, 0.5f, Vector3(-1, 0, 0), Vector3(4, 0, 0));

	const OBBoxClass oriented_box(Vector3(5, 0, 0), Vector3(1, 2, 3), Rotated_Basis());
	RefCountPtr<CollisionBoxRenderObject> ob_object =
		Create_No_Add_Ref(new CollisionBoxRenderObject(oriented_box));
	ob_object->Set_Collision_Type(collision_type);
	Check_Ray_Hit(*ob_object,
		LineSegClass(Vector3(5, -5, 0), Vector3(5, 5, 0)),
		collision_type, 0.4f, Vector3(0, -1, 0), Vector3(5, -1, 0));
}

BOOST_AUTO_TEST_CASE(swept_aa_and_ob_queries_preserve_known_hits_and_normals)
{
	const int collision_type = COLL_TYPE_PHYSICAL;

	RefCountPtr<CollisionBoxRenderObject> aa_target =
		Create_No_Add_Ref(new CollisionBoxRenderObject(
			AABoxClass(Vector3(5, 0, 0), Vector3(1, 1, 1))));
	aa_target->Set_Collision_Type(collision_type);

	CastResultStruct aa_result;
	AABoxCollisionTestClass moving_aa(
		AABoxClass(Vector3(-5, 0, 0), Vector3(1, 1, 1)),
		Vector3(10, 0, 0), &aa_result, collision_type);
	Check_Swept_Hit(*aa_target, moving_aa, 0.8f, Vector3(-1, 0, 0));

	CastResultStruct ob_result;
	const OBBoxClass moving_ob_box(Vector3(-5, 0, 0), Vector3(1, 1, 1));
	OBBoxCollisionTestClass moving_ob(moving_ob_box,
		Vector3(10, 0, 0), &ob_result, collision_type);
	Check_Swept_Hit(*aa_target, moving_ob, 0.8f, Vector3(-1, 0, 0));

	const OBBoxClass oriented_target_box(Vector3(5, 0, 0), Vector3(1, 2, 1), Rotated_Basis());
	RefCountPtr<CollisionBoxRenderObject> oriented_target =
		Create_No_Add_Ref(new CollisionBoxRenderObject(oriented_target_box));
	oriented_target->Set_Collision_Type(collision_type);

	CastResultStruct oriented_aa_result;
	AABoxCollisionTestClass moving_aa_again(
		AABoxClass(Vector3(5, -5, 0), Vector3(1, 1, 1)),
		Vector3(0, 10, 0), &oriented_aa_result, collision_type);
	Check_Swept_Hit(*oriented_target, moving_aa_again, 0.3f, Vector3(0, -1, 0));

	CastResultStruct oriented_ob_result;
	const OBBoxClass moving_ob_box_again(Vector3(5, -5, 0), Vector3(1, 1, 1));
	OBBoxCollisionTestClass moving_ob_again(moving_ob_box_again,
		Vector3(0, 10, 0), &oriented_ob_result, collision_type);
	Check_Swept_Hit(*oriented_target, moving_ob_again, 0.3f, Vector3(0, -1, 0));

	CastResultStruct miss_result;
	AABoxCollisionTestClass moving_away(
		AABoxClass(Vector3(-5, 0, 0), Vector3(1, 1, 1)),
		Vector3(-10, 0, 0), &miss_result, collision_type);
	BOOST_CHECK(!aa_target->Cast_AABox(moving_away));
	BOOST_CHECK(moving_away.CollidedRenderObj == nullptr);
	Check_Fraction(miss_result, 1.0f);
}

BOOST_AUTO_TEST_CASE(intersection_queries_cover_aa_ob_hit_miss_and_masks)
{
	const int collision_type = COLL_TYPE_PHYSICAL;
	RefCountPtr<CollisionBoxRenderObject> aa_target =
		Create_No_Add_Ref(new CollisionBoxRenderObject(
			AABoxClass(Vector3(0, 0, 0), Vector3(1, 1, 1))));
	aa_target->Set_Collision_Type(collision_type);

	AABoxIntersectionTestClass aa_hit(
		AABoxClass(Vector3(1.5f, 0, 0), Vector3(1, 1, 1)), collision_type);
	AABoxIntersectionTestClass aa_miss(
		AABoxClass(Vector3(2.1f, 0, 0), Vector3(1, 1, 1)), collision_type);
	BOOST_CHECK(aa_target->Intersect_AABox(aa_hit));
	BOOST_CHECK(!aa_target->Intersect_AABox(aa_miss));

	OBBoxIntersectionTestClass ob_hit(
		OBBoxClass(Vector3(0, 1.5f, 0), Vector3(1, 1, 1), Rotated_Basis()), collision_type);
	OBBoxIntersectionTestClass ob_miss(
		OBBoxClass(Vector3(0, 2.1f, 0), Vector3(1, 1, 1), Rotated_Basis()), collision_type);
	BOOST_CHECK(aa_target->Intersect_OBBox(ob_hit));
	BOOST_CHECK(!aa_target->Intersect_OBBox(ob_miss));

	const OBBoxClass oriented_target_box(Vector3(0, 0, 0), Vector3(1, 2, 1), Rotated_Basis());
	RefCountPtr<CollisionBoxRenderObject> oriented_target =
		Create_No_Add_Ref(new CollisionBoxRenderObject(oriented_target_box));
	oriented_target->Set_Collision_Type(collision_type);

	AABoxIntersectionTestClass oriented_aa_hit(
		AABoxClass(Vector3(0, 1.5f, 0), Vector3(1, 1, 1)), collision_type);
	AABoxIntersectionTestClass oriented_aa_miss(
		AABoxClass(Vector3(0, 2.1f, 0), Vector3(1, 1, 1)), collision_type);
	BOOST_CHECK(oriented_target->Intersect_AABox(oriented_aa_hit));
	BOOST_CHECK(!oriented_target->Intersect_AABox(oriented_aa_miss));

	OBBoxIntersectionTestClass oriented_ob_hit(
		OBBoxClass(Vector3(1.5f, 0, 0), Vector3(1, 1, 1)), collision_type);
	OBBoxIntersectionTestClass oriented_ob_miss(
		OBBoxClass(Vector3(3.1f, 0, 0), Vector3(1, 1, 1)), collision_type);
	BOOST_CHECK(oriented_target->Intersect_OBBox(oriented_ob_hit));
	BOOST_CHECK(!oriented_target->Intersect_OBBox(oriented_ob_miss));

	AABoxIntersectionTestClass masked(
		AABoxClass(Vector3(0, 0, 0), Vector3(1, 1, 1)), COLL_TYPE_PROJECTILE);
	BOOST_CHECK(!oriented_target->Intersect_AABox(masked));
}
