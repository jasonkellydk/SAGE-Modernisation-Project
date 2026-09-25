module;

#define BOOST_TEST_MODULE EngineCoreMathTests

#include <boost/test/included/unit_test.hpp>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <type_traits>

export module Engine.Core.Math.Tests;

import Engine.Core.Math.Scalar;
import Engine.Core.Math.Vector2;
import Engine.Core.Math.Vector3;
import Engine.Core.Math.Vector4;
import Engine.Core.Math.AxisAlignedBox3;
import Engine.Core.Math.BoundsQueries3;
import Engine.Core.Math.CollisionResult3;
import Engine.Core.Math.LineGeometry3;
import Engine.Core.Math.LineSegment3;
import Engine.Core.Math.OrientedBox3;
import Engine.Core.Math.Plane3;
import Engine.Core.Math.PolygonClip3;
import Engine.Core.Math.Rectangle2;
import Engine.Core.Math.Sphere3;
import Engine.Core.Math.SpatialGrid3;
import Engine.Core.Math.Triangle3;
import Engine.Core.Math.AffineTransform3;
import Engine.Core.Math.Matrix4;
import Engine.Core.Math.Matrix3;
import Engine.Core.Math.EulerAngles3;
import Engine.Core.Math.Quaternion;
import Engine.Core.Math.QuaternionInterpolator;
import Engine.Core.Math.TurnAngle;
import Engine.Core.Math.RandomStream;
import Engine.Core.Math.RandomVector3Generator;
import Engine.Core.Math.Index3;

BOOST_AUTO_TEST_CASE(vector_operations_have_expected_geometry)
{
	using namespace Engine::Math;
	const float vector2_length = Vector2{3, 4}.Length();
	const Vector2 rotated = Vector2{1, 0}.Rotated(std::numbers::pi_v<float> * 0.5f);
	Vector2 accumulated{1, 2};
	accumulated += 2.0f * Vector2{3, 4};
	accumulated -= Vector2{1, 2};
	Vector2 scaled{2, -4};
	scaled *= 0.5f;
	scaled /= 2.0f;
	accumulated[0] = 5.0f;
	BOOST_CHECK_SMALL(rotated.x, 0.00001f);
	BOOST_CHECK_CLOSE(rotated.y, 1.0f, 0.001);
	BOOST_CHECK(bool(accumulated == Vector2{5, 8}));
	BOOST_CHECK(bool(scaled == Vector2{0.5f, -1.0f}));
	BOOST_CHECK(bool(-scaled == Vector2{-0.5f, 1.0f}));
	BOOST_CHECK_EQUAL((Vector2{3, 4}[1]), 4.0f);
	const auto cross = Vector3{1, 0, 0}.Cross({0, 1, 0});
	Vector3 accumulated3{1, 2, 3};
	accumulated3 += Vector3{2, 3, 4};
	accumulated3 -= Vector3{1, 1, 1};
	accumulated3 *= 2.0f;
	BOOST_CHECK(accumulated3 == (Vector3{4, 8, 12}));
	BOOST_CHECK((2.0f * Vector3{1, 2, 3}) == (Vector3{2, 4, 6}));
	Vector3 indexed3{1, 2, 3};
	indexed3[1] = 7;
	const float dot = Vector4{1, 2, 3, 4}.Dot({2, 3, 4, 5});
	const auto sum = Vector3{1, 2, 3} + Vector3{2, 0, 4};
	BOOST_CHECK_EQUAL(vector2_length, 5.0f);
	BOOST_CHECK_EQUAL((Vector3{3, 4, 0}).Length_Squared(), 25.0f);
	BOOST_CHECK(bool(cross == Vector3{0, 0, 1}));
	BOOST_CHECK_EQUAL(dot, 40.0f);
	BOOST_CHECK(bool(sum == Vector3{3, 2, 7}));
	BOOST_CHECK(bool(-Vector3{3, -2, 7} == Vector3{-3, 2, -7}));
	BOOST_CHECK(bool(indexed3 == Vector3{1, 7, 3}));
	BOOST_CHECK_EQUAL(sizeof(Vector2), 2 * sizeof(float));
	BOOST_CHECK(std::is_standard_layout_v<Vector2>);
	BOOST_CHECK(std::is_trivially_copyable_v<Vector2>);
	BOOST_CHECK_EQUAL(offsetof(Vector2, x), 0u);
	BOOST_CHECK_EQUAL(offsetof(Vector2, y), sizeof(float));
	BOOST_CHECK_EQUAL(sizeof(Vector3), 3 * sizeof(float));
	BOOST_CHECK(std::is_standard_layout_v<Vector3>);
	Vector4 indexed{1, 2, 3, 4};
	indexed[2] = 8;
	BOOST_CHECK_EQUAL(indexed.z, 8.0f);
	BOOST_CHECK_EQUAL(sizeof(Vector4), 4 * sizeof(float));
	BOOST_CHECK(std::is_standard_layout_v<Vector4>);
}

BOOST_AUTO_TEST_CASE(collision_results_have_stable_defaults_and_reset_all_fields)
{
	using namespace Engine::Math;
	CollisionResult3 result;
	BOOST_CHECK(!result.starts_overlapping);
	BOOST_CHECK_EQUAL(result.fraction, 1.0f);
	BOOST_CHECK(result.normal == Vector3{});
	BOOST_CHECK_EQUAL(result.surface_type, 0u);
	BOOST_CHECK(!result.compute_contact_point);
	BOOST_CHECK(result.contact_point == Vector3{});

	result.starts_overlapping = true;
	result.fraction = 0.25f;
	result.normal = {0, 1, 0};
	result.surface_type = 17;
	result.compute_contact_point = true;
	result.contact_point = {2, 3, 4};
	result.Reset();
	BOOST_CHECK(!result.starts_overlapping);
	BOOST_CHECK_EQUAL(result.fraction, 1.0f);
	BOOST_CHECK(result.normal == Vector3{});
	BOOST_CHECK_EQUAL(result.surface_type, 0u);
	BOOST_CHECK(!result.compute_contact_point);
	BOOST_CHECK(result.contact_point == Vector3{});
}

BOOST_AUTO_TEST_CASE(normalization_handles_zero_and_non_finite_lengths)
{
	using namespace Engine::Math;
	const auto zero3 = Vector3{}.Normalized();
	const auto infinite2 = Vector2{std::numeric_limits<float>::infinity(), 1}.Normalized();
	const float largest = (std::numeric_limits<float>::max)();
	const float smallest = (std::numeric_limits<float>::denorm_min)();
	const Vector2 huge2 = Vector2{largest, largest}.Normalized();
	const Vector3 huge3 = Vector3{largest, -largest, largest}.Normalized();
	const Vector4 tiny4 = Vector4{smallest, 0, 0, 0}.Normalized();
	BOOST_CHECK(bool(zero3 == Vector3{}));
	BOOST_CHECK(bool(infinite2 == Vector2{}));
	BOOST_CHECK_CLOSE(huge2.Length(), 1.0f, 0.001);
	BOOST_CHECK_CLOSE(huge3.Length(), 1.0f, 0.001);
	BOOST_CHECK(bool(tiny4 == Vector4{1, 0, 0, 0}));
	const Vector4 unit = Vector4{2, 0, 0, 0}.Normalized();
	BOOST_CHECK(bool(unit == Vector4{1, 0, 0, 0}));
}

BOOST_AUTO_TEST_CASE(angle_wrapping_and_clamping_cover_boundaries)
{
	using namespace Engine::Math;
	BOOST_CHECK_EQUAL(WrapRadians(0), 0.0f);
	BOOST_CHECK_EQUAL(WrapRadians(-Pi), Pi);
	BOOST_CHECK_SMALL(WrapRadians(9.0f * Pi) - Pi, 0.00001f);
	BOOST_CHECK_EQUAL(WrapToRange(370.0f, 0.0f, 360.0f), 10.0f);
	BOOST_CHECK_EQUAL(WrapToRange(-10.0f, 0.0f, 360.0f), 350.0f);
	BOOST_CHECK_EQUAL(WrapToRange(3.0f, 1.0f, 1.0f), 3.0f);
	BOOST_CHECK_EQUAL(DefaultTolerance, 0.0001f);
	BOOST_CHECK_SMALL(DegreesToRadians(180.0f) - Pi, 0.000001f);
	BOOST_CHECK_SMALL(RadiansToDegrees(Pi) - 180.0f, 0.00001f);
	BOOST_CHECK_SMALL(DegreesToRadians(180.0) - std::numbers::pi_v<double>, 0.000000000000001);
	BOOST_CHECK(std::isnan(WrapRadians(std::numeric_limits<float>::quiet_NaN())));
	BOOST_CHECK_EQUAL(ClampFinite(4.0f, -1.0f, 2.0f), 2.0f);
	BOOST_CHECK_EQUAL(ClampFinite(std::numeric_limits<float>::infinity(), -1.0f, 2.0f, 7.0f), 7.0f);
	BOOST_CHECK_EQUAL(Lerp(2.0f, 6.0f, 0.25f), 3.0f);
	BOOST_CHECK_EQUAL(InverseLerp(2.0f, 6.0f, 3.0f), 0.25f);
}

BOOST_AUTO_TEST_CASE(axis_aligned_bounds_include_edges_and_reject_separation)
{
	using namespace Engine::Math;
	const AxisAlignedBox3 first{{-1, -2, -3}, {1, 2, 3}};
	const AxisAlignedBox3 touching{{1, 0, 0}, {2, 1, 1}};
	const AxisAlignedBox3 separate{{1.01f, 0, 0}, {2, 1, 1}};
	const AxisAlignedBox3 invalid{{2, 0, 0}, {1, 1, 1}};
	BOOST_CHECK(first.Is_Valid());
	BOOST_CHECK(first.Contains({-1, 2, 3}));
	BOOST_CHECK(first.Intersects(touching));
	BOOST_CHECK(!first.Intersects(separate));
	BOOST_CHECK(first.Intersects_Segment({-2, 0, 0}, {2, 0, 0}));
	BOOST_CHECK(!first.Intersects_Segment({-2, 4, 0}, {2, 4, 0}));
	BOOST_CHECK(!first.Intersects_Segment({-2, 0, 0}, {std::numeric_limits<float>::infinity(), 0, 0}));
	const AxisAlignedBox3 point_box{{1, 2, 3}, {1, 2, 3}};
	BOOST_CHECK(point_box.Intersects_Segment({0, 2, 3}, {2, 2, 3}));
	BOOST_CHECK(!invalid.Contains({1.5f, 0, 0}));
	AxisAlignedBox3 accumulated{{1, 1, 1}, {0, 0, 0}};
	accumulated.Include({-2, 3, 4});
	BOOST_CHECK(bool(accumulated.minimum == Vector3{-2, 3, 4}));
	BOOST_CHECK(bool(accumulated.maximum == Vector3{-2, 3, 4}));
}

BOOST_AUTO_TEST_CASE(bounds_queries_cover_sweeps_rotations_and_transforms)
{
	using namespace Engine::Math;
	const auto swept = SweptBounds({2, 3, 4}, {1, 2, 3}, {-7, 8, 0});
	BOOST_CHECK(bool(swept.minimum == Vector3{-6, 1, 1}));
	BOOST_CHECK(bool(swept.maximum == Vector3{3, 13, 7}));
	BOOST_CHECK(!BoundsAreDisjoint(swept, {{3, 1, 1}, {4, 2, 2}}));
	BOOST_CHECK(BoundsAreDisjoint(swept, {{3.01f, 1, 1}, {4, 2, 2}}));
	BOOST_CHECK(!CenterExtentBoxesAreDisjoint({0, 0, 0}, {1, 2, 3}, {2, 0, 0}, {1, 2, 3}));

	const std::array<float, 9> basis{0, -1, 0, 1, 0, 0, 0, 0, -1};
	const auto oriented_extent = OrientedBoxExtent(basis, {1, 2, 3}, .01f);
	BOOST_CHECK_EQUAL(oriented_extent.x, 2.01f);
	BOOST_CHECK_EQUAL(oriented_extent.y, 1.01f);
	BOOST_CHECK_EQUAL(oriented_extent.z, 3.01f);

	const AxisAlignedBox3 box{{-2, 3, -4}, {5, 7, 8}};
	const std::array<float, 12> quarter_turn{0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
	const auto rotated = RotateBoundsZQuarterTurns(box, 1);
	const auto transformed = TransformBoundsCorners(box, quarter_turn);
	BOOST_CHECK(bool(rotated.minimum == transformed.minimum));
	BOOST_CHECK(bool(rotated.maximum == transformed.maximum));
	auto restored = box;
	for (unsigned turn = 0; turn < 4; ++turn)
		restored = RotateBoundsZQuarterTurns(restored, 1);
	BOOST_CHECK(bool(restored.minimum == box.minimum));
	BOOST_CHECK(bool(restored.maximum == box.maximum));

	const auto equal_endpoints = SweptBounds({}, {}, {-0.0f, -0.0f, -0.0f});
	BOOST_CHECK_EQUAL(std::bit_cast<std::uint32_t>(equal_endpoints.minimum.x), std::uint32_t{0});
	BOOST_CHECK_EQUAL(std::bit_cast<std::uint32_t>(equal_endpoints.minimum.y), std::uint32_t{0});
	BOOST_CHECK_EQUAL(std::bit_cast<std::uint32_t>(equal_endpoints.minimum.z), std::uint32_t{0});
	BOOST_CHECK_EQUAL(std::bit_cast<std::uint32_t>(equal_endpoints.maximum.x), std::uint32_t{0});
	BOOST_CHECK_EQUAL(std::bit_cast<std::uint32_t>(equal_endpoints.maximum.y), std::uint32_t{0});
	BOOST_CHECK_EQUAL(std::bit_cast<std::uint32_t>(equal_endpoints.maximum.z), std::uint32_t{0});

	const std::array<float, 12> non_uniform_transform{
		0, -2, 0, 11,
		3, 0, 0, -4,
		0, 0, -1, 6};
	const auto transformed_non_uniform = TransformBoundsCorners(box, non_uniform_transform);
	BOOST_CHECK(bool(transformed_non_uniform.minimum == Vector3{-3, -10, -2}));
	BOOST_CHECK(bool(transformed_non_uniform.maximum == Vector3{5, 11, 10}));

	const OrientedBox3 oriented{{1, 2, 3}, {1, 2, 3}};
	const auto translated_oriented = TransformOrientedBox(oriented,
		AffineTransform3::From_Translation({10, -2, 1}));
	BOOST_REQUIRE(translated_oriented);
	BOOST_CHECK(bool(translated_oriented->center == Vector3{11, 0, 4}));
	BOOST_CHECK(bool(translated_oriented->half_extent == Vector3{1, 2, 3}));
	const AffineTransform3 non_uniform_affine{{
		2, 0, 0, 0,
		0, 3, 0, 0,
		0, 0, 4, 0}};
	const auto scaled_oriented = TransformOrientedBox(oriented, non_uniform_affine);
	BOOST_REQUIRE(scaled_oriented);
	BOOST_CHECK(bool(scaled_oriented->half_extent == Vector3{2, 6, 12}));
	const AffineTransform3 collapsed_axis{{
		0, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0}};
	BOOST_CHECK(!TransformOrientedBox(oriented, collapsed_axis));
}

BOOST_AUTO_TEST_CASE(sphere_geometry_includes_tangent_boundaries_and_rejects_invalid_values)
{
	using namespace Engine::Math;
	const Sphere3 first{{1, 2, 3}, 5};
	BOOST_CHECK(first.Is_Valid());
	BOOST_CHECK(first.Contains({4, 6, 3}));
	BOOST_CHECK(first.Contains({6, 2, 3}));
	BOOST_CHECK(!first.Contains({6.001f, 2, 3}));
	BOOST_CHECK(!first.Contains({std::numeric_limits<float>::infinity(), 2, 3}));
	BOOST_CHECK(first.Intersects({{11, 2, 3}, 5}));
	BOOST_CHECK(!first.Intersects({{11.001f, 2, 3}, 5}));
	BOOST_CHECK(!first.Intersects({{1, 2, 3}, -1}));
	BOOST_CHECK((!Sphere3{{0, 0, 0}, std::numeric_limits<float>::quiet_NaN()}.Is_Valid()));
	const float largest = (std::numeric_limits<float>::max)();
	const Sphere3 large{{largest, largest, largest}, largest};
	BOOST_CHECK(large.Contains({largest, largest, largest}));
	const std::array<Vector3, 2> endpoints{{{-1, 0, 0}, {1, 0, 0}}};
	const auto enclosing = Try_Enclosing_Sphere(endpoints);
	BOOST_REQUIRE(enclosing);
	BOOST_CHECK(bool(enclosing->center == Vector3{0, 0, 0}));
	BOOST_CHECK_EQUAL(enclosing->radius, 1.0f);
	const std::array<Vector3, 1> singleton{{{4, -2, 1}}};
	const auto point_sphere = Try_Enclosing_Sphere(singleton);
	BOOST_REQUIRE(point_sphere);
	BOOST_CHECK(bool(point_sphere->center == singleton.front()));
	BOOST_CHECK_EQUAL(point_sphere->radius, 0.0f);
	BOOST_CHECK(!Try_Enclosing_Sphere(std::span<const Vector3>{}));
	const std::array<Vector3, 1> invalid_points{{{std::numeric_limits<float>::infinity(), 0, 0}}};
	BOOST_CHECK(!Try_Enclosing_Sphere(invalid_points));
}

BOOST_AUTO_TEST_CASE(sphere_inclusion_encloses_inputs_and_handles_containment)
{
	using namespace Engine::Math;
	Sphere3 combined{{0, 0, 0}, 1};
	combined.Include({{4, 0, 0}, 1});
	BOOST_CHECK_CLOSE_FRACTION(combined.center.x, 2.0f, 1.0e-6f);
	BOOST_CHECK_EQUAL(combined.center.y, 0.0f);
	BOOST_CHECK_EQUAL(combined.center.z, 0.0f);
	BOOST_CHECK_CLOSE_FRACTION(combined.radius, 3.0f, 1.0e-6f);
	BOOST_CHECK(combined.Contains({-1, 0, 0}));
	BOOST_CHECK(combined.Contains({5, 0, 0}));

	combined.Include({{2, 0, 0}, 0.25f});
	BOOST_CHECK(bool(combined.center == Vector3{2, 0, 0}));
	BOOST_CHECK_EQUAL(combined.radius, 3.0f);
	combined.Include({{2, 0, 0}, 4.0f});
	BOOST_CHECK(bool(combined.center == Vector3{2, 0, 0}));
	BOOST_CHECK_EQUAL(combined.radius, 4.0f);

	Sphere3 empty{{0, 0, 0}, -1.0f};
	empty.Include({{3, 4, 5}, 2.0f});
	BOOST_CHECK(bool(empty.center == Vector3{3, 4, 5}));
	BOOST_CHECK_EQUAL(empty.radius, 2.0f);
	empty.Include({{}, std::numeric_limits<float>::quiet_NaN()});
	BOOST_CHECK(bool(empty.center == Vector3{3, 4, 5}));
	BOOST_CHECK_EQUAL(empty.radius, 2.0f);

	Sphere3 first_order{{-2, 3, 1}, 0.5f};
	Sphere3 second_order = first_order;
	first_order.Include({{4, -1, 6}, 1.25f});
	second_order.Include({{4, -1, 6}, 1.25f});
	BOOST_CHECK_EQUAL(std::bit_cast<std::uint32_t>(first_order.center.x),
		std::bit_cast<std::uint32_t>(second_order.center.x));
	BOOST_CHECK_EQUAL(std::bit_cast<std::uint32_t>(first_order.center.y),
		std::bit_cast<std::uint32_t>(second_order.center.y));
	BOOST_CHECK_EQUAL(std::bit_cast<std::uint32_t>(first_order.center.z),
		std::bit_cast<std::uint32_t>(second_order.center.z));
	BOOST_CHECK_EQUAL(std::bit_cast<std::uint32_t>(first_order.radius),
		std::bit_cast<std::uint32_t>(second_order.radius));
}

BOOST_AUTO_TEST_CASE(spheres_transform_by_translation_and_signed_scale)
{
	using namespace Engine::Math;
	const Sphere3 source{{1, -2, 3}, 4};
	BOOST_CHECK(source.Translated({5, 6, -7}) == (Sphere3{{6, 4, -4}, 4}));
	BOOST_CHECK(source.Scaled(2) == (Sphere3{{2, -4, 6}, 8}));
	BOOST_CHECK(source.Scaled(-0.5f) == (Sphere3{{-0.5f, 1, -1.5f}, 2}));
	BOOST_CHECK(!source.Scaled(std::numeric_limits<float>::infinity()).Is_Valid());
}

BOOST_AUTO_TEST_CASE(oriented_bounds_cover_containment_aabb_conversion_and_sat_edges)
{
	using namespace Engine::Math;
	const OrientedBox3 axis_box{{0, 0, 0}, {1, 2, 3}};
	const OrientedBox3 quarter_turn{{0, 0, 0}, {1, 2, 3}, {{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}}}};
	const OrientedBox3 touching{{2, 0, 0}, {1, 1, 1}};
	const OrientedBox3 separated{{2.001f, 0, 0}, {1, 1, 1}};
	BOOST_CHECK(axis_box.Is_Valid());
	BOOST_CHECK(axis_box.Contains({1, -2, 3}));
	BOOST_CHECK(!axis_box.Contains({1.001f, 0, 0}));
	BOOST_CHECK(axis_box.Intersects(touching));
	BOOST_CHECK(!axis_box.Intersects(separated));
	BOOST_CHECK(axis_box.Intersects(quarter_turn));
	const AxisAlignedBox3 bounds = quarter_turn.To_Axis_Aligned();
	BOOST_CHECK(bounds.minimum == (Vector3{-2, -1, -3}));
	BOOST_CHECK(bounds.maximum == (Vector3{2, 1, 3}));
	const OrientedBox3 invalid{{0, 0, 0}, {1, -1, 1}};
	BOOST_CHECK(!invalid.Is_Valid());
	BOOST_CHECK(!invalid.Intersects(axis_box));
}

BOOST_AUTO_TEST_CASE(oriented_boxes_support_segment_queries_and_continuous_sweeps)
{
	using namespace Engine::Math;
	const OrientedBox3 target{{0, 0, 0}, {1, 1, 1}};
	const auto segment_hit = target.Intersect_Segment({-3, 0, 0}, {3, 0, 0});
	BOOST_REQUIRE(segment_hit.has_value());
	BOOST_CHECK_CLOSE_FRACTION(segment_hit->fraction, 1.0f / 3.0f, 1.0e-6f);
	BOOST_CHECK(segment_hit->point == (Vector3{-1, 0, 0}));
	BOOST_CHECK(segment_hit->normal == (Vector3{-1, 0, 0}));
	BOOST_CHECK(!target.Intersect_Segment({-3, 2, 0}, {3, 2, 0}).has_value());
	BOOST_CHECK(!target.Intersect_Segment({-3, 0, 0},
		{std::numeric_limits<float>::infinity(), 0, 0}).has_value());

	const OrientedBox3 moving{{-3, 0, 0}, {0.5f, 0.5f, 0.5f}};
	const auto sweep = moving.Sweep(target, {4, 0, 0});
	BOOST_REQUIRE(sweep.has_value());
	BOOST_CHECK_CLOSE_FRACTION(sweep->fraction, 0.375f, 1.0e-6f);
	BOOST_CHECK(sweep->normal == (Vector3{-1, 0, 0}));
	BOOST_CHECK(sweep->point == (Vector3{-1, 0, 0}));
	BOOST_CHECK(!sweep->starts_overlapping);
	BOOST_CHECK(moving.Sweep(target, {1, 0, 0}) == std::nullopt);
	const auto initial_overlap = OrientedBox3{{0, 0, 0}, {0.5f, 0.5f, 0.5f}}.Sweep(target, {});
	BOOST_REQUIRE(initial_overlap.has_value());
	BOOST_CHECK(initial_overlap->starts_overlapping);
}

BOOST_AUTO_TEST_CASE(spatial_grid_tracks_bounds_and_keeps_query_order_deterministic)
{
	using namespace Engine::Math;
	struct Item { int id; } first{1}, second{2}, oversized{3};
	SpatialGrid3<Item> grid(10.0f);
	BOOST_CHECK(grid.Insert(&first, {{-1, -1, -1}, {1, 1, 1}}));
	BOOST_CHECK(grid.Insert(&second, {{19, -1, -1}, {21, 1, 1}}));
	BOOST_CHECK(grid.Insert(&oversized, {{-100, -100, -100}, {100, 100, 100}}));
	const auto candidates = grid.Query_Point({0, 0, 0});
	BOOST_REQUIRE_EQUAL(candidates.size(), 2u);
	BOOST_CHECK(candidates[0] == &first);
	BOOST_CHECK(candidates[1] == &oversized);

	BOOST_CHECK(grid.Update(&first, {{29, -1, -1}, {31, 1, 1}}));
	const auto moved_candidates = grid.Query_Point({0, 0, 0});
	BOOST_REQUIRE_EQUAL(moved_candidates.size(), 1u);
	BOOST_CHECK(moved_candidates.front() == &oversized);
	const auto next_cell_candidates = grid.Query_Point({10, 0, 0});
	BOOST_REQUIRE_EQUAL(next_cell_candidates.size(), 2u);
	BOOST_CHECK(next_cell_candidates[0] == &second);
	BOOST_CHECK(next_cell_candidates[1] == &oversized);

	grid.Set_Cell_Size(5.0f);
	BOOST_REQUIRE_EQUAL(grid.Query_Point({20, 0, 0}).size(), 2u);
	BOOST_CHECK(grid.Remove(&second));
	BOOST_CHECK(!grid.Remove(&second));
	BOOST_CHECK_EQUAL(grid.Size(), 2u);
	BOOST_CHECK(grid.Query_Point({std::numeric_limits<float>::quiet_NaN(), 0, 0}).empty());
}

BOOST_AUTO_TEST_CASE(planes_build_from_non_degenerate_points_and_intersect_infinite_lines)
{
	using namespace Engine::Math;
	const auto plane = Plane3::From_Points({0, 0, 2}, {1, 0, 2}, {0, 1, 2});
	BOOST_REQUIRE(plane.has_value());
	BOOST_CHECK(plane->normal == (Vector3{0, 0, 1}));
	BOOST_CHECK_EQUAL(plane->distance, 2.0f);
	BOOST_CHECK_EQUAL(plane->Signed_Distance({0, 0, 5}), 3.0f);
	const auto parameter = plane->Intersect_Line({0, 0, 0}, {0, 0, 1});
	BOOST_REQUIRE(parameter.has_value());
	BOOST_CHECK_EQUAL(*parameter, 2.0f);
	BOOST_CHECK(!plane->Intersect_Segment({0, 0, 0}, {0, 0, 1}).has_value());
	const auto segment_parameter = plane->Intersect_Segment({0, 0, 0}, {0, 0, 4});
	BOOST_REQUIRE(segment_parameter.has_value());
	BOOST_CHECK_EQUAL(*segment_parameter, 0.5f);
	using Hit = SegmentPlaneHit;
	BOOST_CHECK(Plane3::Classify_Segment_Intersection(std::nullopt) == Hit::Parallel);
	BOOST_CHECK(Plane3::Classify_Segment_Intersection(0.0f) == Hit::Within_Segment);
	BOOST_CHECK(Plane3::Classify_Segment_Intersection(1.0f) == Hit::Within_Segment);
	BOOST_CHECK(Plane3::Classify_Segment_Intersection(-0.01f) == Hit::Outside_Segment);
	BOOST_CHECK(Plane3::Classify_Segment_Intersection(1.01f) == Hit::Outside_Segment);
	const auto scaled_normal_plane = Plane3::From_Normal_Distance({0, 0, 2}, 4);
	BOOST_REQUIRE(scaled_normal_plane.has_value());
	const auto scaled_normal_parameter = scaled_normal_plane->Intersect_Segment({0, 0, 0}, {0, 0, 4});
	BOOST_REQUIRE(scaled_normal_parameter.has_value());
	BOOST_CHECK_EQUAL(*scaled_normal_parameter, 0.5f);
	const Plane3 lower_clip{{0, 0, 1}, 2};
	const Plane3 upper_clip{{0, 0, -1}, -5};
	BOOST_CHECK(lower_clip.Signed_Distance({0, 0, 1}) < 0.0f);
	BOOST_CHECK_EQUAL(lower_clip.Signed_Distance({0, 0, 2}), 0.0f);
	BOOST_CHECK(lower_clip.Signed_Distance({0, 0, 3}) > 0.0f);
	BOOST_CHECK_EQUAL(upper_clip.Signed_Distance({0, 0, 5}), 0.0f);
	BOOST_CHECK(upper_clip.Signed_Distance({0, 0, 6}) < 0.0f);
	BOOST_CHECK(!plane->Intersect_Line({0, 0, 3}, {1, 0, 3}).has_value());
	BOOST_CHECK(!Plane3::From_Points({1, 1, 1}, {2, 2, 2}, {3, 3, 3}).has_value());
}

BOOST_AUTO_TEST_CASE(line_geometry_handles_segments_parallel_lines_and_skew_lines)
{
	using namespace Engine::Math;
	BOOST_CHECK(LineGeometry3::Closest_Point_On_Segment({0, 0, 0}, {4, 0, 0}, {2, 3, 0}) == (Vector3{2, 0, 0}));
	BOOST_CHECK(LineGeometry3::Closest_Point_On_Segment({0, 0, 0}, {4, 0, 0}, {-2, 3, 0}) == (Vector3{0, 0, 0}));
	BOOST_CHECK(LineGeometry3::Closest_Point_On_Segment({0, 0, 0}, {4, 0, 0}, {6, 3, 0}) == (Vector3{4, 0, 0}));
	BOOST_CHECK(LineGeometry3::Closest_Point_On_Segment({1, 2, 3}, {1, 2, 3}, {9, 9, 9}) == (Vector3{1, 2, 3}));

	const auto crossing = LineGeometry3::Closest_Points_On_Lines(
		{0, 0, 0}, {1, 0, 0}, {0.5f, -1, 0}, {0.5f, 1, 0});
	BOOST_REQUIRE(crossing.has_value());
	BOOST_CHECK(crossing->first == (Vector3{0.5f, 0, 0}));
	BOOST_CHECK(crossing->second == (Vector3{0.5f, 0, 0}));

	const auto skew = LineGeometry3::Closest_Points_On_Lines(
		{0, 0, 0}, {1, 0, 0}, {0.5f, -1, 2}, {0.5f, 1, 2});
	BOOST_REQUIRE(skew.has_value());
	BOOST_CHECK(skew->first == (Vector3{0.5f, 0, 0}));
	BOOST_CHECK(skew->second == (Vector3{0.5f, 0, 2}));
	BOOST_CHECK(!LineGeometry3::Closest_Points_On_Lines({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}));
	BOOST_CHECK(!LineGeometry3::Closest_Points_On_Lines({0, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}));
}

BOOST_AUTO_TEST_CASE(line_segments_keep_endpoints_and_clamp_closest_points)
{
	using namespace Engine::Math;
	const LineSegment3 segment{{1, 2, 3}, {5, 2, 3}};
	BOOST_CHECK(segment.Displacement() == (Vector3{4, 0, 0}));
	BOOST_CHECK(segment.Point_At(0.25f) == (Vector3{2, 2, 3}));
	BOOST_CHECK(segment.Direction() == (Vector3{1, 0, 0}));
	BOOST_CHECK_EQUAL(segment.Length(), 4.0f);
	BOOST_CHECK(segment.Closest_Point({3, 7, 3}) == (Vector3{3, 2, 3}));
	BOOST_CHECK(segment.Closest_Point({-5, 2, 3}) == segment.start);
	BOOST_CHECK(segment.Closest_Point({8, 2, 3}) == segment.end);
	const LineSegment3 point_segment{{2, 4, 6}, {2, 4, 6}};
	BOOST_CHECK(point_segment.Closest_Point({10, 10, 10}) == point_segment.start);
}

BOOST_AUTO_TEST_CASE(line_segments_handle_non_finite_and_extreme_coordinates)
{
	using namespace Engine::Math;
	const float maximum = (std::numeric_limits<float>::max)();
	const LineSegment3 segment{{0, 0, 0}, {maximum, 0, 0}};
	BOOST_CHECK_EQUAL(segment.Length(), maximum);
	BOOST_CHECK(segment.Closest_Point({maximum, 4, 0}) == segment.end);

	const float infinity = std::numeric_limits<float>::infinity();
	const LineSegment3 invalid_segment{{infinity, 0, 0}, {0, 0, 0}};
	BOOST_CHECK(invalid_segment.Closest_Point({1, 2, 3}) == invalid_segment.start);
}

BOOST_AUTO_TEST_CASE(line_segments_interpolate_at_height_with_explicit_degenerate_results)
{
	using namespace Engine::Math;
	const LineSegment3 segment{{2, -4, 1}, {10, 8, 5}};
	BOOST_CHECK(Try_Point_At_Z(segment, 1.0f) == (Vector3{2, -4, 1}));
	BOOST_CHECK(Try_Point_At_Z(segment, 3.0f) == (Vector3{6, 2, 3}));
	BOOST_CHECK(Try_Point_At_Z(segment, 7.0f) == (Vector3{14, 14, 7}));
	BOOST_CHECK(!Try_Point_At_Z({{1, 2, 3}, {4, 5, 3}}, 3.0f));
	BOOST_CHECK(!Try_Point_At_Z(segment, std::numeric_limits<float>::infinity()));
	const float maximum = (std::numeric_limits<float>::max)();
	const LineSegment3 overflowing_segment{{0, 0, 0}, {0, maximum, 1}};
	BOOST_CHECK(!Try_Point_At_Z(overflowing_segment, maximum));
}

BOOST_AUTO_TEST_CASE(projected_triangles_include_boundaries_and_handle_degenerate_shapes)
{
	using Engine::Math::Triangle3;
	using Engine::Math::Vector3;
	bool hit_edge = false;
	BOOST_CHECK(Triangle3::Contains_Projected_Point(
		{0, 0, 10}, {4, 0, 10}, {0, 4, 10}, {1, 1, -50}, 0, 1));
	BOOST_CHECK(Triangle3::Contains_Projected_Point(
		{0, 0, 0}, {0, 4, 0}, {4, 0, 0}, {2, 0, 1}, 0, 1, &hit_edge));
	BOOST_CHECK(hit_edge);
	BOOST_CHECK(!Triangle3::Contains_Projected_Point(
		{0, 0, 0}, {4, 0, 0}, {0, 4, 0}, {4, 4, 0}, 0, 1));
	BOOST_CHECK(Triangle3::Contains_Projected_Point(
		{0, 0, 0}, {2, 0, 0}, {4, 0, 0}, {3, 0, 8}, 0, 1));
	BOOST_CHECK(Triangle3::Contains_Projected_Point(
		{2, 2, 0}, {2, 2, 1}, {2, 2, 2}, {2, 2, -4}, 0, 1));
	BOOST_CHECK(!Triangle3::Contains_Projected_Point(
		{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, Vector3{0, 0, 0}, 1, 1));
}

BOOST_AUTO_TEST_CASE(axis_aligned_triangle_rays_report_direction_edges_and_embedded_starts)
{
	using namespace Engine::Math;
	const Vector3 a{0, 0, 0}, b{2, 0, 0}, c{0, 2, 0};
	const Vector4 plane{0, 0, 1, 0};
	const auto entering = Triangle3::Intersect_Semi_Infinite_Axis_Ray(
		a, b, c, plane, {0.5f, 0.5f, 1}, 2, 0, 1, false);
	BOOST_CHECK(entering.intersects);
	BOOST_CHECK(!entering.touches_edge);
	BOOST_CHECK(!entering.starts_inside);
	const auto edge = Triangle3::Intersect_Semi_Infinite_Axis_Ray(
		a, b, c, plane, {1, 0, 1}, 2, 0, 1, false);
	BOOST_CHECK(edge.intersects);
	BOOST_CHECK(edge.touches_edge);
	const auto embedded = Triangle3::Intersect_Semi_Infinite_Axis_Ray(
		a, b, c, plane, {0.5f, 0.5f, 0}, 2, 0, 1, true);
	BOOST_CHECK(embedded.intersects);
	BOOST_CHECK(embedded.starts_inside);
	const auto parallel = Triangle3::Intersect_Semi_Infinite_Axis_Ray(
		a, b, c, plane, {0.5f, 0.5f, 0}, 0, 1, 2, true);
	BOOST_CHECK(!parallel.intersects);
	BOOST_CHECK(parallel.starts_inside);
	// Parallel ray in the plane whose (y, z) projection hits the triangle but
	// whose start lies outside the triangle: legacy case B leaves START_IN_TRI clear.
	const auto parallel_outside = Triangle3::Intersect_Semi_Infinite_Axis_Ray(
		a, b, c, plane, {1.5f, 1.5f, 0}, 0, 1, 2, true);
	BOOST_CHECK(!parallel_outside.intersects);
	BOOST_CHECK(!parallel_outside.starts_inside);
	BOOST_CHECK(!Triangle3::Intersect_Semi_Infinite_Axis_Ray(
		a, b, c, plane, {3, 3, 1}, 2, 0, 1, false).intersects);
}

BOOST_AUTO_TEST_CASE(segment_triangle_intersections_cover_bounds_and_degenerate_cases)
{
	using namespace Engine::Math;
	const Vector3 a{0, 0, 0}, b{2, 0, 0}, c{0, 2, 0};
	const auto hit = Triangle3::Intersect_Segment({0.5f, 0.5f, 1}, {0.5f, 0.5f, -1}, a, b, c);
	BOOST_REQUIRE(hit.has_value());
	BOOST_CHECK(hit->fraction == 0.5f);
	BOOST_CHECK(hit->point == (Vector3{0.5f, 0.5f, 0}));
	BOOST_CHECK(hit->normal == (Vector3{0, 0, 1}));

	const auto reverse_winding = Triangle3::Intersect_Segment(
		{0.5f, 0.5f, -1}, {0.5f, 0.5f, 1}, a, c, b);
	BOOST_REQUIRE(reverse_winding.has_value());
	BOOST_CHECK(reverse_winding->fraction == 0.5f);
	BOOST_CHECK(reverse_winding->normal == (Vector3{0, 0, -1}));

	const auto start_endpoint = Triangle3::Intersect_Segment({0, 0, 0}, {0, 0, 1}, a, b, c);
	BOOST_REQUIRE(start_endpoint.has_value());
	BOOST_CHECK(start_endpoint->fraction == 0.0f);
	const auto end_endpoint = Triangle3::Intersect_Segment({0, 0, 1}, {0, 0, 0}, a, b, c);
	BOOST_REQUIRE(end_endpoint.has_value());
	BOOST_CHECK(end_endpoint->fraction == 1.0f);
	BOOST_CHECK(Triangle3::Intersect_Segment({1, 0, 1}, {1, 0, -1}, a, b, c).has_value());
	BOOST_CHECK(Triangle3::Intersect_Segment({1, 1, 1}, {1, 1, -1}, a, b, c).has_value());
	BOOST_CHECK(!Triangle3::Intersect_Segment({2, 2, 1}, {2, 2, -1}, a, b, c).has_value());
	BOOST_CHECK(!Triangle3::Intersect_Segment({0.5f, 0.5f, 1}, {0.5f, 0.5f, 2}, a, b, c).has_value());
	BOOST_CHECK(!Triangle3::Intersect_Segment({0, 0, 1}, {0, 0, -1}, a, b, b).has_value());
	BOOST_CHECK(!Triangle3::Intersect_Segment(
		{0, 0, 1}, {0, 0, -1}, a, b, Vector3{1, 0, 0}).has_value());
}

BOOST_AUTO_TEST_CASE(triangles_intersect_axis_aligned_and_oriented_boxes_at_boundaries)
{
	using namespace Engine::Math;
	const Triangle3 triangle{{0, 0, 0}, {2, 0, 0}, {0, 2, 0}};
	BOOST_CHECK(triangle.Intersects(AxisAlignedBox3{{0.5f, 0.5f, -1}, {1, 1, 1}}));
	BOOST_CHECK(triangle.Intersects(AxisAlignedBox3{{2, 0, 0}, {3, 1, 1}}));
	BOOST_CHECK(!triangle.Intersects(AxisAlignedBox3{{2.1f, 2.1f, -1}, {3, 3, 1}}));
	BOOST_CHECK(!triangle.Intersects(AxisAlignedBox3{{2, 0, 0}, {1, 1, 1}}));

	const OrientedBox3 rotated_box{{0, 0, 0}, {1, 2, 0.5f},
		{{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}}}};
	BOOST_CHECK(triangle.Intersects(rotated_box));
	const OrientedBox3 separated_box{{4, 0, 0}, {0.25f, 0.25f, 0.25f},
		{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}};
	BOOST_CHECK(!triangle.Intersects(separated_box));
	BOOST_CHECK(!triangle.Intersects(OrientedBox3{{}, {-1, 1, 1},
		{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}}));
}

BOOST_AUTO_TEST_CASE(swept_boxes_report_triangle_hit_fraction_overlap_and_contact)
{
	using namespace Engine::Math;
	const Triangle3 triangle{{0, 0, 0}, {2, 0, 0}, {0, 2, 0}};
	const OrientedBox3 box{{0.5f, 0.5f, 2}, {0.25f, 0.25f, 0.25f},
		{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}};
	const auto hit = triangle.Sweep(box, {0, 0, -3});
	BOOST_REQUIRE(hit.has_value());
	BOOST_CHECK_CLOSE_FRACTION(hit->fraction, 1.75f / 3.0f, 1.0e-6f);
	BOOST_CHECK(hit->normal == (Vector3{0, 0, 1}));
	BOOST_CHECK(!hit->starts_overlapping);
	BOOST_CHECK(hit->point == (Vector3{0.5f, 0.5f, 0}));

	const auto end_touch = triangle.Sweep(
		OrientedBox3{{0.5f, 0.5f, 1}, {0.25f, 0.25f, 0.25f}, box.axes}, {0, 0, -0.75f});
	BOOST_REQUIRE(end_touch.has_value());
	BOOST_CHECK(end_touch->fraction == 1.0f);

	const auto overlap = triangle.Sweep(
		OrientedBox3{{0.5f, 0.5f, 0}, {0.25f, 0.25f, 0.25f}, box.axes}, {0, 0, -1});
	BOOST_REQUIRE(overlap.has_value());
	BOOST_CHECK(overlap->starts_overlapping);
	BOOST_CHECK(overlap->fraction == 0.0f);
	BOOST_CHECK(!triangle.Sweep(box, {1, 0, 0}).has_value());
	BOOST_CHECK(!triangle.Sweep(
		OrientedBox3{{2, 2, 2}, {0.25f, 0.25f, 0.25f}, box.axes}, {0, 0, -3}).has_value());
}

BOOST_AUTO_TEST_CASE(convex_polygon_clipping_keeps_inside_edges_and_interpolates_crossings)
{
	using namespace Engine::Math;
	const std::array<Vector3, 4> crossing{{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}};
	const auto plane = Plane3::From_Normal_Distance({1, 0, 0}, 0);
	BOOST_REQUIRE(plane.has_value());
	const auto clipped = PolygonClip3::Against_Plane(crossing, *plane);
	BOOST_REQUIRE_EQUAL(clipped.size(), 4u);
	BOOST_CHECK(clipped[0] == (Vector3{-1, -1, 0}));
	BOOST_CHECK(clipped[1] == (Vector3{0, -1, 0}));
	BOOST_CHECK(clipped[2] == (Vector3{0, 1, 0}));
	BOOST_CHECK(clipped[3] == (Vector3{-1, 1, 0}));

	const std::array<Vector3, 4> outside{{{1, -1, 0}, {2, -1, 0}, {2, 1, 0}, {1, 1, 0}}};
	BOOST_CHECK(PolygonClip3::Against_Plane(outside, *plane).empty());
	const std::array<Vector3, 4> on_boundary{{{0, -1, 0}, {0, 1, 0}, {0, 1, 1}, {0, -1, 1}}};
	BOOST_CHECK(PolygonClip3::Against_Plane(on_boundary, *plane) ==
		std::vector<Vector3>(on_boundary.begin(), on_boundary.end()));
	const std::array<Vector3, 2> line{{{-1, 0, 0}, {1, 0, 0}}};
	BOOST_CHECK(PolygonClip3::Against_Plane(line, *plane).empty());
}

BOOST_AUTO_TEST_CASE(rectangles_measure_contain_edges_and_form_unions)
{
	using namespace Engine::Math;
	static_assert(std::is_standard_layout_v<Rectangle2>);
	static_assert(std::is_trivially_copyable_v<Rectangle2>);
	static_assert(sizeof(Rectangle2) == 4 * sizeof(float));
	const Rectangle2 first = Rectangle2::From_Corners({-2, -1}, {4, 5});
	BOOST_CHECK_EQUAL(first.Width(), 6.0f);
	BOOST_CHECK_EQUAL(first.Height(), 6.0f);
	BOOST_CHECK(first.Center() == (Vector2{1, 2}));
	BOOST_CHECK(first.Contains({-2, 5}));
	BOOST_CHECK(!first.Contains({4.01f, 0}));
	Rectangle2 united = first;
	united.Include({-4, 0, 1, 9});
	BOOST_CHECK(united == (Rectangle2{-4, -1, 4, 9}));
	Rectangle2 empty{1, 1, 0, 0};
	empty.Include(first);
	BOOST_CHECK(empty == first);
}

BOOST_AUTO_TEST_CASE(affine_transforms_compose_parent_and_local_space)
{
	using namespace Engine::Math;
	AffineTransform3 parent = AffineTransform3::From_Translation({10, 20, 30});
	BOOST_CHECK(parent.Translation() == (Vector3{10, 20, 30}));
	parent.Set_Translation({-2, 4, 8});
	BOOST_CHECK(parent.Translation() == (Vector3{-2, 4, 8}));
	parent.elements[0] = 0;
	parent.elements[1] = -1;
	parent.elements[4] = 1;
	parent.elements[5] = 0;
	BOOST_CHECK(parent.Basis_X() == (Vector3{0, 1, 0}));
	BOOST_CHECK(parent.Basis_Y() == (Vector3{-1, 0, 0}));
	BOOST_CHECK(parent.Basis_Z() == (Vector3{0, 0, 1}));
	AffineTransform3 pre_rotated = parent;
	pre_rotated.Pre_Apply_Rotation(AffineTransform3::Rotation_X(std::numbers::pi_v<float> * 0.5f));
	BOOST_CHECK(pre_rotated.Translation() == (Vector3{-2, 4, 8}));
	BOOST_CHECK_SMALL(pre_rotated.Basis_Z().z, 1.0e-6f);
	BOOST_CHECK_CLOSE_FRACTION(pre_rotated.Basis_Z().y, -1.0f, 1.0e-6f);
	const auto local = AffineTransform3::From_Translation({2, 3, 4});
	const auto world = Compose(parent, local);
	const auto point = world.Transform_Point({1, 1, 1});
	BOOST_CHECK(bool(point == Vector3{-6, 7, 13}));
	BOOST_CHECK(bool(world.Transform_Vector({1, 0, 0}) == Vector3{0, 1, 0}));
}

BOOST_AUTO_TEST_CASE(affine_axis_rotations_follow_column_vector_convention)
{
	using namespace Engine::Math;
	const float quarter_turn = std::acos(-1.0f) * 0.5f;
	const auto x = AffineTransform3::Rotation_X(quarter_turn).Transform_Vector({0, 1, 0});
	const auto y = AffineTransform3::Rotation_Y(quarter_turn).Transform_Vector({0, 0, 1});
	const auto z = AffineTransform3::Rotation_Z(quarter_turn).Transform_Vector({1, 0, 0});
	BOOST_CHECK_SMALL(x.x, 1.0e-6f);
	BOOST_CHECK_SMALL(x.y, 1.0e-6f);
	BOOST_CHECK_CLOSE_FRACTION(x.z, 1.0f, 1.0e-6f);
	BOOST_CHECK_CLOSE_FRACTION(y.x, 1.0f, 1.0e-6f);
	BOOST_CHECK_SMALL(y.y, 1.0e-6f);
	BOOST_CHECK_SMALL(y.z, 1.0e-6f);
	BOOST_CHECK_SMALL(z.x, 1.0e-6f);
	BOOST_CHECK_CLOSE_FRACTION(z.y, 1.0f, 1.0e-6f);
	BOOST_CHECK_SMALL(z.z, 1.0e-6f);
	const auto post_rotated_translation = Compose(
		AffineTransform3::From_Translation({1, 2, 3}), AffineTransform3::Rotation_Z(quarter_turn));
	BOOST_CHECK(bool(post_rotated_translation.Transform_Point({}) == Vector3{1, 2, 3}));
	auto pre_applied = AffineTransform3::From_Translation({4, -2, 7});
	pre_applied.Pre_Apply_Rotation(AffineTransform3::Rotation_Z(quarter_turn));
	BOOST_CHECK(pre_applied.Translation() == (Vector3{4, -2, 7}));
	BOOST_CHECK_SMALL(pre_applied.Basis_X().x, 1.0e-6f);
	BOOST_CHECK_CLOSE_FRACTION(pre_applied.Basis_X().y, 1.0f, 1.0e-6f);
}

BOOST_AUTO_TEST_CASE(affine_uniform_scale_and_translation_offsets_are_explicit)
{
	using namespace Engine::Math;
	auto transform = AffineTransform3::From_Uniform_Scale(2.5f);
	transform.Set_Translation({1.0f, -2.0f, 3.0f});
	transform.Adjust_Translation({-4.0f, 5.0f, 0.5f});
	BOOST_CHECK(transform.Basis_X() == (Vector3{2.5f, 0.0f, 0.0f}));
	BOOST_CHECK(transform.Basis_Y() == (Vector3{0.0f, 2.5f, 0.0f}));
	BOOST_CHECK(transform.Basis_Z() == (Vector3{0.0f, 0.0f, 2.5f}));
	BOOST_CHECK(transform.Translation() == (Vector3{-3.0f, 3.0f, 3.5f}));
	BOOST_CHECK(transform.Transform_Point({1.0f, 1.0f, 1.0f}) == (Vector3{-0.5f, 5.5f, 6.0f}));
}

BOOST_AUTO_TEST_CASE(affine_forward_direction_handles_axis_and_zero_boundaries)
{
	using namespace Engine::Math;
	const auto from_basis = AffineTransform3::From_Basis(
		{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}, {4, -2, 7});
	BOOST_CHECK(from_basis.Basis_X() == (Vector3{0, 1, 0}));
	BOOST_CHECK(from_basis.Basis_Y() == (Vector3{-1, 0, 0}));
	BOOST_CHECK(from_basis.Basis_Z() == (Vector3{0, 0, 1}));
	BOOST_CHECK(from_basis.Translation() == (Vector3{4, -2, 7}));
	BOOST_CHECK(from_basis.Transform_Point({1, 0, 0}) == (Vector3{4, -1, 7}));
	const Vector3 position{4.0f, -2.0f, 7.0f};
	const auto east = AffineTransform3::From_Forward_Direction(position, {1.0f, 0.0f, 0.0f});
	BOOST_CHECK(bool(east.Transform_Vector({1.0f, 0.0f, 0.0f}) == (Vector3{1.0f, 0.0f, 0.0f})));
	BOOST_CHECK(east.Translation() == position);
	const auto north = AffineTransform3::From_Forward_Direction({}, {0.0f, 1.0f, 0.0f});
	BOOST_CHECK_SMALL(north.Transform_Vector({1.0f, 0.0f, 0.0f}).x, 1.0e-6f);
	BOOST_CHECK_CLOSE_FRACTION(north.Transform_Vector({1.0f, 0.0f, 0.0f}).y, 1.0f, 1.0e-6f);
	const auto scaled_north = AffineTransform3::From_Forward_Direction({}, {0.0f, 8.0f, 0.0f});
	BOOST_CHECK_CLOSE_FRACTION(scaled_north.Basis_X().Length(), 1.0f, 1.0e-6f);
	BOOST_CHECK_CLOSE_FRACTION(scaled_north.Basis_X().y, 1.0f, 1.0e-6f);
	const auto scaled_diagonal = AffineTransform3::From_Forward_Direction({}, {3.0f, 4.0f, 12.0f});
	BOOST_CHECK_CLOSE_FRACTION(scaled_diagonal.Basis_X().Length(), 1.0f, 1.0e-6f);
	BOOST_CHECK_CLOSE_FRACTION(scaled_diagonal.Basis_X().x, 3.0f / 13.0f, 1.0e-6f);
	BOOST_CHECK_CLOSE_FRACTION(scaled_diagonal.Basis_X().y, 4.0f / 13.0f, 1.0e-6f);
	BOOST_CHECK_CLOSE_FRACTION(scaled_diagonal.Basis_X().z, 12.0f / 13.0f, 1.0e-6f);
	const auto vertical = AffineTransform3::From_Forward_Direction({}, {0.0f, 0.0f, 1.0f});
	BOOST_CHECK(bool(vertical.Transform_Vector({1.0f, 0.0f, 0.0f}) == (Vector3{0.0f, 0.0f, 1.0f})));
	const auto zero = AffineTransform3::From_Forward_Direction(position, {});
	BOOST_CHECK(bool(zero.Transform_Vector({1.0f, 0.0f, 0.0f}) == (Vector3{1.0f, 0.0f, 0.0f})));
	BOOST_CHECK(zero.Translation() == position);
}

BOOST_AUTO_TEST_CASE(affine_look_at_builds_stable_camera_basis)
{
	using namespace Engine::Math;
	const auto toward_north = AffineTransform3::Look_At({0, -5, 0}, {0, 0, 0});
	BOOST_CHECK(toward_north.Translation() == (Vector3{0, -5, 0}));
	BOOST_CHECK_CLOSE_FRACTION(toward_north.Basis_X().x, 1.0f, 1.0e-6f);
	BOOST_CHECK_CLOSE_FRACTION(toward_north.Basis_Y().z, 1.0f, 1.0e-6f);
	BOOST_CHECK_CLOSE_FRACTION(toward_north.Basis_Z().y, -1.0f, 1.0e-6f);

	// Legacy Look_At_Dir zero-yaw basis for a straight-down view.
	const auto vertical = AffineTransform3::Look_At({0, 0, 5}, {});
	BOOST_CHECK(vertical.Basis_X() == (Vector3{0, -1, 0}));
	BOOST_CHECK(vertical.Basis_Y() == (Vector3{1, 0, 0}));
	BOOST_CHECK(vertical.Basis_Z() == (Vector3{0, 0, 1}));
	// Straight-up view: legacy basis is X=(0,-1,0), Y=(-1,0,0), Z=(0,0,-1).
	const auto upward = AffineTransform3::Look_Along({1, 2, 3}, {0, 0, 7});
	BOOST_CHECK(upward.Basis_X() == (Vector3{0, -1, 0}));
	BOOST_CHECK(upward.Basis_Y() == (Vector3{-1, 0, 0}));
	BOOST_CHECK(upward.Basis_Z() == (Vector3{0, 0, -1}));
	BOOST_CHECK(upward.Translation() == (Vector3{1, 2, 3}));
	const auto coincident = AffineTransform3::Look_At({2, 3, 4}, {2, 3, 4});
	BOOST_CHECK(coincident == AffineTransform3::From_Translation({2, 3, 4}));

	// Legacy Look_At_Dir applies Rotate_Z(-roll) because the camera looks down -Z.
	const auto rolled = AffineTransform3::Look_Along({0, -5, 0}, {0, 1, 0}, 1.57079632679f);
	BOOST_CHECK(rolled.Translation() == (Vector3{0, -5, 0}));
	BOOST_CHECK_CLOSE_FRACTION(rolled.Basis_X().z, -1.0f, 1.0e-6f);
	BOOST_CHECK_CLOSE_FRACTION(rolled.Basis_Y().x, 1.0f, 1.0e-6f);
}

BOOST_AUTO_TEST_CASE(affine_transform_import_preserves_row_major_values)
{
	using namespace Engine::Math;
	const std::array<std::array<float, 4>, 3> rows{{
		{{1, 2, 3, 4}}, {{5, 6, 7, 8}}, {{9, 10, 11, 12}}}};
	const auto transform = AffineTransform3::From_Row_Matrix(rows);
	BOOST_CHECK(transform.elements == (std::array<float, 12>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}));

	const std::array<std::array<float, 4>, 3> bit_patterns{{
		{{std::bit_cast<float>(std::uint32_t{0x80000000}), std::bit_cast<float>(std::uint32_t{0x7fc00042}), 0.0f, 1.0f}},
		{{std::bit_cast<float>(std::uint32_t{0x7f800000}), -1.0f, 2.0f, 3.0f}},
		{{4.0f, 5.0f, 6.0f, 7.0f}}}};
	const auto bit_preserving_transform = AffineTransform3::From_Row_Matrix(bit_patterns);
	const std::array<std::uint32_t, 12> expected_bits{
		0x80000000, 0x7fc00042, 0x00000000, 0x3f800000,
		0x7f800000, 0xbf800000, 0x40000000, 0x40400000,
		0x40800000, 0x40a00000, 0x40c00000, 0x40e00000};
	for (std::size_t index = 0; index < expected_bits.size(); ++index)
		BOOST_CHECK_EQUAL(std::bit_cast<std::uint32_t>(bit_preserving_transform.elements[index]), expected_bits[index]);
}

BOOST_AUTO_TEST_CASE(affine_inverse_round_trips_points_and_rejects_singular_matrices)
{
	using namespace Engine::Math;
	AffineTransform3 transform = AffineTransform3::From_Translation({4, -3, 7});
	transform.elements[0] = 2.0f;
	transform.elements[1] = 0.5f;
	transform.elements[5] = 3.0f;
	transform.elements[10] = 0.25f;
	const auto inverse = transform.Inverse();
	BOOST_REQUIRE(inverse.has_value());
	const Vector3 point{2, -5, 11};
	const Vector3 round_trip = inverse->Transform_Point(transform.Transform_Point(point));
	BOOST_CHECK_SMALL(round_trip.x - point.x, 0.00001f);
	BOOST_CHECK_SMALL(round_trip.y - point.y, 0.00001f);
	BOOST_CHECK_SMALL(round_trip.z - point.z, 0.00001f);
	AffineTransform3 singular{};
	singular.elements[0] = 0.0f;
	BOOST_CHECK(!singular.Inverse().has_value());
}

BOOST_AUTO_TEST_CASE(matrix4_composes_transforms_and_inverts_with_stable_pivot_order)
{
	using namespace Engine::Math;
	Matrix4 transform = Matrix4::Identity();
	transform(0, 0) = 2.0f;
	transform(1, 1) = 3.0f;
	transform(2, 2) = 4.0f;
	transform(0, 3) = 5.0f;
	transform(1, 3) = -6.0f;
	transform(2, 3) = 7.0f;
	const Vector3 point{1.0f, 2.0f, 3.0f};
	BOOST_CHECK(transform.Transform_Point(point) == (Vector3{7.0f, 0.0f, 19.0f}));
	const auto inverse = transform.Inverse();
	BOOST_REQUIRE(inverse.has_value());
	const Vector3 restored = inverse->Transform_Point(transform.Transform_Point(point));
	BOOST_CHECK_CLOSE_FRACTION(restored.x, point.x, 1.0e-6f);
	BOOST_CHECK_CLOSE_FRACTION(restored.y, point.y, 1.0e-6f);
	BOOST_CHECK_CLOSE_FRACTION(restored.z, point.z, 1.0e-6f);
	const Matrix4 composed = Compose(transform, *inverse);
	for (unsigned row = 0; row < 4; ++row)
		for (unsigned column = 0; column < 4; ++column)
			BOOST_CHECK_CLOSE_FRACTION(composed(row, column), row == column ? 1.0f : 0.0f, 1.0e-6f);
	Matrix4 singular{};
	singular(2, 2) = 0.0f;
	BOOST_CHECK(!singular.Inverse().has_value());
}

BOOST_AUTO_TEST_CASE(euler_extraction_handles_identity_axis_rotations_and_singularities)
{
	using namespace Engine::Math;
	const EulerAngles3 identity = EulerAngles3::From_Rotation_XYZ_Rotating_Frame(AffineTransform3{});
	BOOST_CHECK(identity == (EulerAngles3{}));
	AffineTransform3 z_quarter_turn;
	z_quarter_turn.elements[0] = 0.0f;
	z_quarter_turn.elements[1] = -1.0f;
	z_quarter_turn.elements[4] = 1.0f;
	z_quarter_turn.elements[5] = 0.0f;
	const EulerAngles3 z_angles = EulerAngles3::From_Rotation_XYZ_Rotating_Frame(z_quarter_turn);
	BOOST_CHECK_SMALL(z_angles.x, 0.000001);
	BOOST_CHECK_SMALL(z_angles.y, 0.000001);
	BOOST_CHECK_CLOSE(z_angles.z, std::numbers::pi_v<double> / 2.0, 0.000001);
	AffineTransform3 y_quarter_turn;
	y_quarter_turn.elements[0] = 0.0f;
	y_quarter_turn.elements[2] = 1.0f;
	y_quarter_turn.elements[8] = -1.0f;
	y_quarter_turn.elements[10] = 0.0f;
	const EulerAngles3 y_angles = EulerAngles3::From_Rotation_XYZ_Rotating_Frame(y_quarter_turn);
	BOOST_CHECK_CLOSE(y_angles.y, std::numbers::pi_v<double> / 2.0, 0.000001);
	BOOST_CHECK(std::isfinite(y_angles.x) && std::isfinite(y_angles.z));
	BOOST_CHECK(EulerAngles3::From_Rotation_XYZ_Rotating_Frame(y_quarter_turn) == y_angles);
}

BOOST_AUTO_TEST_CASE(unit_quaternion_rotates_vectors_and_invalid_normalization_is_identity)
{
	using namespace Engine::Math;
	const Quaternion from_axis = Quaternion::From_Axis_Angle({0, 0, 4}, 1.57079632679f);
	const auto from_axis_result = from_axis.Rotate_Vector({1, 0, 0});
	BOOST_CHECK_SMALL(from_axis_result.x, 0.00001f);
	BOOST_CHECK_CLOSE(from_axis_result.y, 1.0f, 0.001);
	BOOST_CHECK_SMALL(from_axis_result.z, 0.00001f);
	BOOST_CHECK(bool(Quaternion::From_Axis_Angle({}, 1.0f) == Quaternion{}));
	const float largest = (std::numeric_limits<float>::max)();
	const Quaternion huge_axis = Quaternion::From_Axis_Angle({0, 0, largest}, 1.57079632679f);
	BOOST_CHECK_CLOSE(huge_axis.Rotate_Vector({1, 0, 0}).y, 1.0f, 0.001);
	BOOST_CHECK(bool(Quaternion::From_Axis_Angle({1, 0, 0}, std::numeric_limits<float>::infinity()) == Quaternion{}));

	const Quaternion quarter_turn{0, 0, 0.70710678118f, 0.70710678118f};
	const auto rotated = quarter_turn.Rotate_Vector({1, 0, 0});
	BOOST_CHECK_SMALL(rotated.x, 0.00001f);
	BOOST_CHECK_CLOSE(rotated.y, 1.0f, 0.001);
	BOOST_CHECK_SMALL(rotated.z, 0.00001f);
	BOOST_CHECK(bool(Quaternion{0, 0, 0, 0}.Normalized() == Quaternion{}));
	const auto combined = quarter_turn * quarter_turn;
	const auto twice_rotated = combined.Rotate_Vector({1, 0, 0});
	BOOST_CHECK_CLOSE(twice_rotated.x, -1.0f, 0.001);
	BOOST_CHECK_SMALL(twice_rotated.y, 0.00001f);
}

BOOST_AUTO_TEST_CASE(quaternion_conversion_handles_trace_and_dominant_axis_branches)
{
	using namespace Engine::Math;
	AffineTransform3 quarter_turn;
	quarter_turn.elements[0] = 0.0f;
	quarter_turn.elements[1] = -1.0f;
	quarter_turn.elements[4] = 1.0f;
	quarter_turn.elements[5] = 0.0f;
	const Quaternion z_rotation = Quaternion::From_Rotation(quarter_turn);
	BOOST_CHECK_CLOSE(z_rotation.z, 0.70710678f, 0.001);
	BOOST_CHECK_CLOSE(z_rotation.w, 0.70710678f, 0.001);
	const auto z_axis_rotation = [](unsigned axis) {
		AffineTransform3 matrix;
		matrix.elements[axis == 0 ? 5 : 0] = -1.0f;
		matrix.elements[axis == 2 ? 5 : 10] = -1.0f;
		if (axis == 1) matrix.elements[0] = -1.0f;
		return Quaternion::From_Rotation(matrix);
	};
	const Quaternion x_half_turn = z_axis_rotation(0);
	const Quaternion y_half_turn = z_axis_rotation(1);
	const Quaternion z_half_turn = z_axis_rotation(2);
	BOOST_CHECK_CLOSE(std::abs(x_half_turn.x), 1.0f, 0.001);
	BOOST_CHECK_CLOSE(std::abs(y_half_turn.y), 1.0f, 0.001);
	BOOST_CHECK_CLOSE(std::abs(z_half_turn.z), 1.0f, 0.001);
	BOOST_CHECK_SMALL(x_half_turn.w, 0.00001f);
	BOOST_CHECK_SMALL(y_half_turn.w, 0.00001f);
	BOOST_CHECK_SMALL(z_half_turn.w, 0.00001f);
}

BOOST_AUTO_TEST_CASE(affine_transform_equality_compares_all_components)
{
	using namespace Engine::Math;
	BOOST_CHECK(AffineTransform3{} == AffineTransform3::Identity());
	BOOST_CHECK(!(AffineTransform3::From_Translation({1, 0, 0}) == AffineTransform3::Identity()));
	BOOST_CHECK(AffineTransform3::From_Translation({1, 2, 3})
		== AffineTransform3::From_Translation({1, 2, 3}));
}

BOOST_AUTO_TEST_CASE(trackball_drag_is_identity_for_no_motion_and_rotates_consistently)
{
	using namespace Engine::Math;
	const Quaternion identity = Quaternion::Trackball_Drag({0.25f, -0.5f}, {0.25f, -0.5f}, 0.8f);
	BOOST_CHECK(identity == Quaternion{});
	BOOST_CHECK(Quaternion::Trackball_Drag({0, 0}, {1, 1}, 0.0f) == Quaternion{});
	BOOST_CHECK(Quaternion::Trackball_Drag({0, 0}, {1, 1}, std::numeric_limits<float>::infinity()) == Quaternion{});
	BOOST_CHECK(Quaternion::Trackball_Drag(
		{std::numeric_limits<float>::quiet_NaN(), 0}, {1, 1}, 0.8f) == Quaternion{});
	BOOST_CHECK(Quaternion::Trackball_Drag(
		{0, 0}, {std::numeric_limits<float>::infinity(), 1}, 0.8f) == Quaternion{});
	const Quaternion wide_range = Quaternion::Trackball_Drag(
		{(std::numeric_limits<float>::max)() / 4.0f, 0},
		{0, (std::numeric_limits<float>::max)() / 4.0f},
		(std::numeric_limits<float>::max)() / 2.0f);
	BOOST_CHECK_CLOSE(wide_range.Length(), 1.0f, 0.001);
	const Quaternion first = Quaternion::Trackball_Drag({-0.2f, 0.1f}, {0.4f, -0.3f}, 0.8f);
	const Quaternion second = Quaternion::Trackball_Drag({-0.2f, 0.1f}, {0.4f, -0.3f}, 0.8f);
	BOOST_CHECK(first == second);
	BOOST_CHECK_CLOSE(first.Length(), 1.0f, 0.001);
	const Vector3 rotated = first.Rotate_Vector({1, 0, 0});
	BOOST_CHECK(std::isfinite(rotated.x) && std::isfinite(rotated.y) && std::isfinite(rotated.z));
	const auto matrix = first.To_Rotation_Transform();
	const Vector3 matrix_rotated = matrix.Transform_Vector({1, 0, 0});
	BOOST_CHECK_CLOSE(rotated.x, matrix_rotated.x, 0.001);
	BOOST_CHECK_CLOSE(rotated.y, matrix_rotated.y, 0.001);
	BOOST_CHECK_CLOSE(rotated.z, matrix_rotated.z, 0.001);
}

BOOST_AUTO_TEST_CASE(quaternion_interpolation_caches_shortest_path_and_preserves_authored_magnitude)
{
	using namespace Engine::Math;
	const Quaternion first{0, 0, 0, 2};
	const Quaternion opposite{0, 0, 0, -3};
	const QuaternionInterpolator linear{first, opposite};
	BOOST_CHECK(linear.Sample(0.25f) == (Quaternion{0, 0, 0, 2.25f}));
	const Quaternion quarter_turn{0, 0, 0.70710678118f, 0.70710678118f};
	const QuaternionInterpolator spherical{Quaternion{}, quarter_turn};
	const Quaternion midpoint = spherical.Sample(0.5f);
	const float weight = std::sin(Pi * 0.125f) / (Pi * 0.25f);
	BOOST_CHECK_CLOSE(midpoint.z, weight * quarter_turn.z, 0.001);
	BOOST_CHECK_CLOSE(midpoint.w, weight + weight * quarter_turn.w, 0.001);
	const float endpoint_scale = std::sin(Pi * 0.25f) / (Pi * 0.25f);
	BOOST_CHECK_CLOSE(spherical.Sample(0.0f).w, endpoint_scale, 0.001);
	BOOST_CHECK_CLOSE(spherical.Sample(1.0f).w, endpoint_scale * quarter_turn.w, 0.001);
}

BOOST_AUTO_TEST_CASE(turn_angles_wrap_exactly_and_fixed_trigonometry_is_repeatable)
{
	using namespace Engine::Math;
	constexpr TurnAngle quarter = TurnAngle::Quarter_Turn();
	BOOST_CHECK((quarter + quarter) == TurnAngle::Half_Turn());
	BOOST_CHECK((TurnAngle{0xffffffffu} + TurnAngle{1}) == TurnAngle{});
	BOOST_CHECK((TurnAngle{} - TurnAngle{1}) == TurnAngle{0xffffffffu});
	BOOST_CHECK(Sin_Cos({0}) == (FixedSinCos{0, 1 << 30}));
	BOOST_CHECK(Sin_Cos(quarter) == (FixedSinCos{1 << 30, 0}));
	BOOST_CHECK(Sin_Cos(TurnAngle::Half_Turn()) == (FixedSinCos{0, -(1 << 30)}));
	const auto first = Sin_Cos({0x12345678u});
	const auto second = Sin_Cos({0x12345678u});
	BOOST_CHECK(first == second);
	BOOST_CHECK((first == FixedSinCos{463948539, 968335303}));
	BOOST_CHECK(first.sine > 0 && first.cosine > 0);
	const auto second_quadrant = Sin_Cos({0x52345678u});
	const auto third_quadrant = Sin_Cos({0x92345678u});
	const auto fourth_quadrant = Sin_Cos({0xd2345678u});
	BOOST_CHECK((second_quadrant == FixedSinCos{968335395, -463949483}));
	BOOST_CHECK((third_quadrant == FixedSinCos{-463948539, -968335303}));
	BOOST_CHECK((fourth_quadrant == FixedSinCos{-968335395, 463949483}));
}

BOOST_AUTO_TEST_CASE(matrix3_composition_transform_and_inverse_cover_boundaries)
{
	using namespace Engine::Math;
	const Matrix3 axis_rotation = Matrix3::Rotation_Z(std::numbers::pi_v<float> * 0.5f);
	const Vector3 quarter_turned = axis_rotation.Transform({1.0f, 0.0f, 0.0f});
	BOOST_CHECK_SMALL(quarter_turned.x, 0.00001f);
	BOOST_CHECK_CLOSE(quarter_turned.y, 1.0f, 0.001f);
	BOOST_CHECK(axis_rotation.Is_Orthonormal());
	BOOST_CHECK_CLOSE(axis_rotation.Determinant(), 1.0f, 0.001f);
	Matrix3 scaled_rotation = axis_rotation;
	scaled_rotation(0, 1) *= 2.0f;
	BOOST_CHECK(!scaled_rotation.Is_Orthonormal());
	BOOST_CHECK(!axis_rotation.Is_Orthonormal(-1.0f));
	const auto affine = axis_rotation.To_Affine_Transform();
	BOOST_CHECK(Matrix3::From_Affine_Transform(affine) == axis_rotation);

	const Matrix3 scale{{2.0f, 0.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, 4.0f}};
	const Matrix3 quarter_turn{{0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
	const Matrix3 composed = Compose(quarter_turn, scale);
	BOOST_CHECK(composed.Transform({1.0f, 2.0f, 3.0f}) == (Vector3{-6.0f, 2.0f, 12.0f}));
	BOOST_CHECK(quarter_turn.Transposed().Transform({1.0f, 0.0f, 0.0f}) == (Vector3{0.0f, -1.0f, 0.0f}));

	const auto inverse = composed.Inverse();
	BOOST_REQUIRE(inverse.has_value());
	const Vector3 point{7.0f, -5.0f, 11.0f};
	const Vector3 restored = inverse->Transform(composed.Transform(point));
	BOOST_CHECK_CLOSE(restored.x, point.x, 0.0001f);
	BOOST_CHECK_CLOSE(restored.y, point.y, 0.0001f);
	BOOST_CHECK_CLOSE(restored.z, point.z, 0.0001f);

	const Matrix3 singular{{1.0f, 2.0f, 3.0f, 2.0f, 4.0f, 6.0f, 0.0f, 0.0f, 0.0f}};
	BOOST_CHECK(!singular.Inverse().has_value());
	Matrix3 non_finite = Matrix3::Identity();
	non_finite(1, 1) = std::numeric_limits<float>::infinity();
	BOOST_CHECK(!non_finite.Inverse().has_value());
	BOOST_CHECK(Matrix3::Identity().Inverse() == std::optional<Matrix3>{Matrix3::Identity()});
}

BOOST_AUTO_TEST_CASE(random_stream_integer_sequence_is_stable_and_float_range_is_half_open)
{
	using namespace Engine::Math;
	BOOST_CHECK_EQUAL(RandomStream::Derive_Seed(0, 0), 16294208416658607535ull);
	BOOST_CHECK(RandomStream::Derive_Seed(0, 0) != RandomStream::Derive_Seed(0, 1));
	RandomStream first{42};
	RandomStream second{42};
	BOOST_CHECK_EQUAL(first.NextUInt32(), 355248013u);
	BOOST_CHECK_EQUAL(first.NextUInt32(), 1282005598u);
	BOOST_CHECK_EQUAL(second.NextUInt32(), 355248013u);
	const float sample = first.NextUnitFloat();
	BOOST_CHECK_EQUAL(std::bit_cast<std::uint32_t>(sample), 0x3db690f0u);
	BOOST_CHECK(sample >= 0.0f && sample < 1.0f);
	const float scaled = first.NextFloat(-3.0f, 5.0f);
	BOOST_CHECK(scaled >= -3.0f && scaled < 5.0f);
	BOOST_CHECK_EQUAL(first.NextFloat(2.0f, 2.0f), 2.0f);
	BOOST_CHECK_EQUAL(first.NextFloat(3.0f, 2.0f), 3.0f);
	const float float_max = (std::numeric_limits<float>::max)();
	const float wide_range = first.NextFloat(-float_max, float_max);
	BOOST_CHECK(std::isfinite(wide_range));
	BOOST_CHECK(wide_range >= -float_max && wide_range < float_max);
}

BOOST_AUTO_TEST_CASE(random_vector_generators_are_seeded_bounded_and_shape_aware)
{
	using namespace Engine::Math;
	RandomVector3Generator box_a{Vector3Distribution::Box, {2, 3, 4}, 91};
	RandomVector3Generator box_b{Vector3Distribution::Box, {2, 3, 4}, 91};
	for (unsigned sample = 0; sample < 64; ++sample) {
		const Vector3 a = box_a.Next();
		const Vector3 b = box_b.Next();
		BOOST_CHECK(a == b);
		BOOST_CHECK(std::abs(a.x) <= 2 && std::abs(a.y) <= 3 && std::abs(a.z) <= 4);
	}
	RandomVector3Generator sphere{Vector3Distribution::SolidSphere, {5, 0, 0}, 17};
	RandomVector3Generator surface{Vector3Distribution::SphereSurface, {5, 0, 0}, 17};
	RandomVector3Generator cylinder{Vector3Distribution::Cylinder, {6, 2, 0}, 17};
	RandomVector3Generator cylinder_repeat{Vector3Distribution::Cylinder, {6, 2, 0}, 17};
	for (unsigned sample = 0; sample < 32; ++sample) {
		const auto inside = sphere.Next();
		BOOST_CHECK(inside.Dot(inside) <= 25.0001f);
		const auto on_surface = surface.Next();
		BOOST_CHECK_CLOSE(on_surface.Length(), 5.0f, 0.01);
		const auto in_cylinder = cylinder.Next();
		BOOST_CHECK(in_cylinder == cylinder_repeat.Next());
		BOOST_CHECK(std::abs(in_cylinder.x) <= 6);
		BOOST_CHECK(in_cylinder.y * in_cylinder.y + in_cylinder.z * in_cylinder.z <= 4.0001f);
	}
	RandomVector3Generator clamped{Vector3Distribution::Box, {-1, 2, 3}, 1};
	BOOST_CHECK_EQUAL(clamped.Maximum_Extent(), 3.0f);
	BOOST_CHECK(clamped.Dimensions() == (Vector3{0, 2, 3}));
	clamped.Scale(-1);
	BOOST_CHECK(clamped.Next() == (Vector3{}));
	RandomVector3Generator non_finite{Vector3Distribution::Box,
		{std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
		std::numeric_limits<float>::quiet_NaN()}, 4};
	BOOST_CHECK(non_finite.Next() == (Vector3{}));

	const float largest = (std::numeric_limits<float>::max)();
	RandomVector3Generator large_sphere{Vector3Distribution::SolidSphere, {largest, 0, 0}, 37};
	RandomVector3Generator large_sphere_repeat{Vector3Distribution::SolidSphere, {largest, 0, 0}, 37};
	RandomVector3Generator large_cylinder{Vector3Distribution::Cylinder, {largest, largest, 0}, 53};
	const Sphere3 large_sphere_bounds{{}, largest};
	for (unsigned sample_index = 0; sample_index < 64; ++sample_index) {
		const Vector3 sphere_sample = large_sphere.Next();
		BOOST_CHECK(large_sphere_bounds.Contains(sphere_sample));
		BOOST_CHECK(sphere_sample == large_sphere_repeat.Next());

		const Vector3 cylinder_sample = large_cylinder.Next();
		const double radial_y = static_cast<double>(cylinder_sample.y) / largest;
		const double radial_z = static_cast<double>(cylinder_sample.z) / largest;
		BOOST_CHECK(std::abs(cylinder_sample.x) <= largest);
		BOOST_CHECK(radial_y * radial_y + radial_z * radial_z <= 1.000001);
	}

	clamped.Set_Shape(Vector3Distribution::Cylinder, {2, 1, 0});
	BOOST_CHECK(clamped.Distribution() == Vector3Distribution::Cylinder);
	BOOST_CHECK(clamped.Dimensions() == (Vector3{2, 1, 1}));
	clamped.Scale((std::numeric_limits<float>::max)());
	BOOST_CHECK_EQUAL(clamped.Dimensions().x, (std::numeric_limits<float>::max)());
}

BOOST_AUTO_TEST_CASE(index_triplets_keep_contiguous_fixed_width_storage)
{
	using namespace Engine::Math;
	Index3i triangle{{2, 5, 9}};
	Index3u16 compact{{2, 5, 9}};
	BOOST_CHECK_EQUAL(triangle[0], 2);
	BOOST_CHECK_EQUAL(triangle[1], 5);
	BOOST_CHECK_EQUAL(triangle[2], 9);
	BOOST_CHECK(compact == (Index3u16{{2, 5, 9}}));
	triangle[1] = 7;
	BOOST_CHECK_EQUAL(triangle[1], 7);
}

BOOST_AUTO_TEST_CASE(random_vector_generators_return_origin_for_zero_sized_shapes)
{
	using namespace Engine::Math;
	for (const auto distribution : {Vector3Distribution::Box, Vector3Distribution::SolidSphere,
		Vector3Distribution::SphereSurface, Vector3Distribution::Cylinder}) {
		RandomVector3Generator generator{distribution, {}, 0x12345678u};
		for (unsigned sample = 0; sample < 16; ++sample)
			BOOST_CHECK(generator.Next() == (Vector3{}));
	}
}

BOOST_AUTO_TEST_CASE(legacy_normalization_matches_wwmath_formula)
{
	using namespace Engine::Math;
	const Vector3 value{3.0f, 4.0f, 12.0f};
	const float len2 = 3.0f * 3.0f + 4.0f * 4.0f + 12.0f * 12.0f;
	const float inv = 1.0f / std::sqrt(len2);
	const Vector3 normalized = value.Normalized_Legacy();
	BOOST_CHECK(std::bit_cast<std::uint32_t>(normalized.x) == std::bit_cast<std::uint32_t>(3.0f * inv));
	BOOST_CHECK(std::bit_cast<std::uint32_t>(normalized.y) == std::bit_cast<std::uint32_t>(4.0f * inv));
	BOOST_CHECK(std::bit_cast<std::uint32_t>(normalized.z) == std::bit_cast<std::uint32_t>(12.0f * inv));
	BOOST_CHECK(Vector3{}.Normalized_Legacy() == (Vector3{}));
	const Vector3 nan_input{std::numeric_limits<float>::quiet_NaN(), 1.0f, 0.0f};
	BOOST_CHECK(std::isnan(nan_input.Normalized_Legacy().x));

	const Vector2 value2{0.0f, -2.0f};
	BOOST_CHECK(value2.Normalized_Legacy() == (Vector2{0.0f, -1.0f}));
	BOOST_CHECK(Vector2{}.Normalized_Legacy() == (Vector2{}));
	const Vector2 odd{1.0f, 3.0f};
	const float inv2 = 1.0f / std::sqrt(1.0f * 1.0f + 3.0f * 3.0f);
	BOOST_CHECK(odd.Normalized_Legacy() == (Vector2{1.0f * inv2, 3.0f * inv2}));
}

BOOST_AUTO_TEST_CASE(legacy_affine_constructors_match_matrix3d_formulas)
{
	using namespace Engine::Math;
	// Matrix3D(axis, angle) about +Z.
	const float angle = 0.5f;
	const float c = std::cos(angle);
	const float s = std::sin(angle);
	const auto about_z = AffineTransform3::From_Axis_Angle_Legacy({0.0f, 0.0f, 1.0f}, angle);
	BOOST_CHECK(about_z.elements[0] == 0.0f * 0.0f + c * (1.0f - 0.0f * 0.0f));
	BOOST_CHECK(about_z.elements[1] == 0.0f * 0.0f * (1.0f - c) - 1.0f * s);
	BOOST_CHECK(about_z.elements[4] == 0.0f * 0.0f * (1.0f - c) + 1.0f * s);
	BOOST_CHECK(about_z.elements[5] == c);
	BOOST_CHECK(about_z.elements[10] == 1.0f);
	BOOST_CHECK(about_z.Translation() == (Vector3{}));

	// General unit axis, compared term by term against the legacy expressions.
	const Vector3 axis{0.6f, 0.0f, 0.8f};
	const float angle2 = 1.25f;
	const float c2 = std::cos(angle2);
	const float s2 = std::sin(angle2);
	const auto general = AffineTransform3::From_Axis_Angle_Legacy(axis, angle2);
	BOOST_CHECK(general.elements[0] == axis.x * axis.x + c2 * (1.0f - axis.x * axis.x));
	BOOST_CHECK(general.elements[2] == axis.z * axis.x * (1.0f - c2) + axis.y * s2);
	BOOST_CHECK(general.elements[6] == axis.y * axis.z * (1.0f - c2) - axis.x * s2);
	BOOST_CHECK(general.elements[8] == axis.z * axis.x * (1.0f - c2) - axis.y * s2);
	BOOST_CHECK(general.elements[10] == axis.z * axis.z + c2 * (1.0f - axis.z * axis.z));
	const Vector3 rotated_axis = general.Transform_Vector(axis);
	BOOST_CHECK_CLOSE_FRACTION(rotated_axis.x, axis.x, 1.0e-5f);
	BOOST_CHECK_CLOSE_FRACTION(rotated_axis.z, axis.z, 1.0e-5f);

	// buildTransformMatrix: vertical direction uses yaw 0 and the X axis follows +Z.
	const auto up = AffineTransform3::From_Unit_Forward_Direction({1, 2, 3}, {0, 0, 1});
	BOOST_CHECK(up.Basis_X() == (Vector3{0, 0, 1}));
	BOOST_CHECK(up.Basis_Y() == (Vector3{0, 1, 0}));
	BOOST_CHECK(up.Basis_Z() == (Vector3{-1, 0, 0}));
	BOOST_CHECK(up.Translation() == (Vector3{1, 2, 3}));
	// The direction is not normalized: a length-2 +X direction scales the X row by cos(pitch)=2.
	const auto unnormalized = AffineTransform3::From_Unit_Forward_Direction({}, {2, 0, 0});
	BOOST_CHECK(unnormalized.elements[0] == 2.0f);
	// Generic direction, compared with the legacy Rotate_Z then Rotate_Y sequence.
	const Vector3 dir{0.48f, 0.64f, 0.6f};
	const float len2 = std::sqrt((dir.x * dir.x) + (dir.y * dir.y));
	const float siny = dir.y / len2;
	const float cosy = dir.x / len2;
	const auto built = AffineTransform3::From_Unit_Forward_Direction({}, dir);
	// Row 0 after Rotate_Z: (cosy, -siny, 0); Rotate_Y(-sinp, cosp) mixes columns 0 and 2.
	BOOST_CHECK(built.elements[0] == len2 * cosy - (-dir.z) * 0.0f);
	BOOST_CHECK(built.elements[2] == (-dir.z) * cosy + len2 * 0.0f);
	BOOST_CHECK(built.elements[1] == -siny * 1.0f + cosy * 0.0f);
	BOOST_CHECK(built.elements[8] == len2 * 0.0f - (-dir.z) * 1.0f);
	BOOST_CHECK_CLOSE_FRACTION(built.Basis_X().x, dir.x, 1.0e-5f);
	BOOST_CHECK_CLOSE_FRACTION(built.Basis_X().y, dir.y, 1.0e-5f);
	BOOST_CHECK_CLOSE_FRACTION(built.Basis_X().z, dir.z, 1.0e-5f);
}

BOOST_AUTO_TEST_CASE(legacy_orthogonal_inverse_and_z_rotation_match_matrix3d)
{
	using namespace Engine::Math;
	auto transform = AffineTransform3::From_Axis_Angle_Legacy({0.0f, 0.0f, 1.0f}, 0.75f);
	transform.Set_Translation({4.0f, -2.0f, 1.5f});
	const auto inverse = transform.Orthogonal_Inverse();
	BOOST_CHECK(inverse.elements[1] == transform.elements[4]);
	BOOST_CHECK(inverse.elements[4] == transform.elements[1]);
	BOOST_CHECK(inverse.elements[2] == transform.elements[8]);
	const float tx = transform.elements[3], ty = transform.elements[7], tz = transform.elements[11];
	BOOST_CHECK(inverse.elements[3] == -(transform.elements[0] * tx + transform.elements[4] * ty + transform.elements[8] * tz));
	BOOST_CHECK(inverse.elements[7] == -(transform.elements[1] * tx + transform.elements[5] * ty + transform.elements[9] * tz));
	BOOST_CHECK(inverse.elements[11] == -(transform.elements[2] * tx + transform.elements[6] * ty + transform.elements[10] * tz));
	const Vector3 round_trip = inverse.Transform_Point(transform.Transform_Point({1.0f, 2.0f, 3.0f}));
	BOOST_CHECK_CLOSE_FRACTION(round_trip.x, 1.0f, 1.0e-5f);
	BOOST_CHECK_CLOSE_FRACTION(round_trip.y, 2.0f, 1.0e-5f);
	BOOST_CHECK_CLOSE_FRACTION(round_trip.z, 3.0f, 1.0e-5f);

	const float expected = static_cast<float>(std::atan2(
		static_cast<double>(transform.elements[4]), static_cast<double>(transform.elements[0])));
	BOOST_CHECK(transform.Z_Rotation_Legacy() == expected);
	BOOST_CHECK_CLOSE_FRACTION(transform.Z_Rotation_Legacy(), 0.75f, 1.0e-5f);
	BOOST_CHECK(AffineTransform3{}.Z_Rotation_Legacy() == 0.0f);
}

BOOST_AUTO_TEST_CASE(oriented_box_axis_aligned_bounds_accept_any_finite_basis)
{
	using namespace Engine::Math;
	const OrientedBox3 scaled{{1, 2, 3}, {1, 1, 1}, {{{2, 0, 0}, {0, 3, 0}, {0, 0, 1}}}};
	BOOST_CHECK(!scaled.Is_Valid());
	const AxisAlignedBox3 scaled_bounds = scaled.To_Axis_Aligned();
	BOOST_CHECK(scaled_bounds.minimum == (Vector3{-1, -1, 2}));
	BOOST_CHECK(scaled_bounds.maximum == (Vector3{3, 5, 4}));

	const OrientedBox3 skewed{{0, 0, 0}, {1, 2, 1}, {{{1, 1, 0}, {0, -2, 0}, {0, 0, 0.5f}}}};
	const AxisAlignedBox3 skewed_bounds = skewed.To_Axis_Aligned();
	BOOST_CHECK(skewed_bounds.maximum == (Vector3{1, 5, 0.5f}));
	BOOST_CHECK(skewed_bounds.minimum == (Vector3{-1, -5, -0.5f}));

	const OrientedBox3 non_finite{{0, 0, 0}, {1, 1, 1},
		{{{std::numeric_limits<float>::infinity(), 0, 0}, {0, 1, 0}, {0, 0, 1}}}};
	BOOST_CHECK(!non_finite.To_Axis_Aligned().Is_Valid());
}

BOOST_AUTO_TEST_CASE(box_segment_queries_report_starts_inside_like_legacy_collide)
{
	using namespace Engine::Math;
	const OrientedBox3 target{{0, 0, 0}, {1, 1, 1}};
	const auto inside_hit = target.Intersect_Segment({0.5f, 0, 0}, {3, 0, 0});
	BOOST_REQUIRE(inside_hit.has_value());
	BOOST_CHECK(inside_hit->starts_inside);
	BOOST_CHECK(inside_hit->fraction == 0.0f);
	BOOST_CHECK(inside_hit->point == (Vector3{0.5f, 0, 0}));
	const auto outside_hit = target.Intersect_Segment({-3, 0, 0}, {3, 0, 0});
	BOOST_REQUIRE(outside_hit.has_value());
	BOOST_CHECK(!outside_hit->starts_inside);

	// Legacy OBBox collide: start inside sets starts_overlapping, keeps fraction.
	CollisionResult3 result;
	result.fraction = 0.5f;
	result.compute_contact_point = true;
	BOOST_CHECK(target.Collide_Segment_Legacy({0.25f, 0.5f, 0}, {5, 5, 5}, result));
	BOOST_CHECK(result.starts_overlapping);
	BOOST_CHECK(result.fraction == 0.5f);
	BOOST_CHECK(result.contact_point == (Vector3{0.25f, 0.5f, 0}));
	BOOST_CHECK(result.normal == (Vector3{}));

	// Rotated box, hit from outside on the local +Y face (world -X).
	const OrientedBox3 turned{{0, 0, 0}, {1, 2, 3}, {{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}}}};
	CollisionResult3 turned_result;
	turned_result.compute_contact_point = true;
	BOOST_CHECK(turned.Collide_Segment_Legacy({-5, 0, 0}, {5, 0, 0}, turned_result));
	BOOST_CHECK(!turned_result.starts_overlapping);
	BOOST_CHECK(turned_result.fraction == (2.0f - 5.0f) / -10.0f);
	BOOST_CHECK(turned_result.normal == (Vector3{-1, 0, 0}));
	BOOST_CHECK(turned_result.contact_point == (Vector3{-5, 0, 0} + turned_result.fraction * Vector3{10, 0, 0}));
	// A farther hit does not replace the current best.
	CollisionResult3 closer;
	closer.fraction = 0.1f;
	BOOST_CHECK(!turned.Collide_Segment_Legacy({-5, 0, 0}, {5, 0, 0}, closer));
	BOOST_CHECK(closer.fraction == 0.1f);
	// Miss.
	CollisionResult3 missed;
	BOOST_CHECK(!turned.Collide_Segment_Legacy({-5, 5, 0}, {5, 5, 0}, missed));
	BOOST_CHECK(missed.fraction == 1.0f);

	// Legacy AABox collide.
	const AxisAlignedBox3 aabox{{-1, -1, -1}, {1, 1, 1}};
	CollisionResult3 aa_result;
	aa_result.compute_contact_point = true;
	BOOST_CHECK(aabox.Collide_Segment_Legacy({-3, 0, 0}, {3, 0, 0}, aa_result));
	BOOST_CHECK(aa_result.fraction == (-1.0f - -3.0f) / 6.0f);
	BOOST_CHECK(aa_result.normal == (Vector3{-1, 0, 0}));
	BOOST_CHECK(!aa_result.starts_overlapping);
	CollisionResult3 aa_positive;
	BOOST_CHECK(aabox.Collide_Segment_Legacy({0, 0, 4}, {0, 0, -4}, aa_positive));
	BOOST_CHECK(aa_positive.normal == (Vector3{0, 0, 1}));
	BOOST_CHECK(aa_positive.fraction == (1.0f - 4.0f) / -8.0f);
	CollisionResult3 aa_inside;
	aa_inside.compute_contact_point = true;
	BOOST_CHECK(aabox.Collide_Segment_Legacy({0, 0, 0}, {3, 0, 0}, aa_inside));
	BOOST_CHECK(aa_inside.starts_overlapping);
	BOOST_CHECK(aa_inside.fraction == 1.0f);
	BOOST_CHECK(aa_inside.contact_point == (Vector3{}));
	// Legacy test does not clamp to the segment end; the caller's fraction does.
	LegacyBoxSegmentTest3 beyond;
	BOOST_CHECK(Test_Aligned_Box_Legacy({-1, -1, -1}, {1, 1, 1}, {-5, 0, 0}, {2, 0, 0}, beyond));
	BOOST_CHECK(beyond.fraction == 2.0f);
	CollisionResult3 short_segment;
	BOOST_CHECK(!aabox.Collide_Segment_Legacy({-5, 0, 0}, {-3, 0, 0}, short_segment));
}
