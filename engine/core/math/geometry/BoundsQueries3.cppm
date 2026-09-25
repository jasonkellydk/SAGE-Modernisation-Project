module;

#include <array>
#include <cmath>
#include <optional>

export module Engine.Core.Math.BoundsQueries3;

export import Engine.Core.Math.AxisAlignedBox3;
export import Engine.Core.Math.AffineTransform3;
export import Engine.Core.Math.OrientedBox3;

export namespace Engine::Math
{
constexpr AxisAlignedBox3 SweptBounds(Vector3 center, Vector3 extent, Vector3 movement) noexcept
{
	AxisAlignedBox3 result{{center.x - extent.x, center.y - extent.y, center.z - extent.z},
		{center.x + extent.x, center.y + extent.y, center.z + extent.z}};
	const Vector3 end_minimum{center.x + movement.x - extent.x,
		center.y + movement.y - extent.y, center.z + movement.z - extent.z};
	const Vector3 end_maximum{center.x + movement.x + extent.x,
		center.y + movement.y + extent.y, center.z + movement.z + extent.z};
	if (end_maximum.x > result.maximum.x) result.maximum.x = end_maximum.x;
	if (end_maximum.y > result.maximum.y) result.maximum.y = end_maximum.y;
	if (end_maximum.z > result.maximum.z) result.maximum.z = end_maximum.z;
	if (end_minimum.x < result.minimum.x) result.minimum.x = end_minimum.x;
	if (end_minimum.y < result.minimum.y) result.minimum.y = end_minimum.y;
	if (end_minimum.z < result.minimum.z) result.minimum.z = end_minimum.z;
	return result;
}

inline Vector3 OrientedBoxExtent(const std::array<float, 9> &basis,
	Vector3 extent, float padding = 0.0f) noexcept
{
	return {std::fabs(basis[0] * extent.x) + std::fabs(basis[1] * extent.y) + std::fabs(basis[2] * extent.z) + padding,
		std::fabs(basis[3] * extent.x) + std::fabs(basis[4] * extent.y) + std::fabs(basis[5] * extent.z) + padding,
		std::fabs(basis[6] * extent.x) + std::fabs(basis[7] * extent.y) + std::fabs(basis[8] * extent.z) + padding};
}

constexpr bool BoundsAreDisjoint(const AxisAlignedBox3 &first,
	const AxisAlignedBox3 &second) noexcept
{
	return first.minimum.x > second.maximum.x || first.maximum.x < second.minimum.x
		|| first.minimum.y > second.maximum.y || first.maximum.y < second.minimum.y
		|| first.minimum.z > second.maximum.z || first.maximum.z < second.minimum.z;
}

inline bool CenterExtentBoxesAreDisjoint(Vector3 first_center, Vector3 first_extent,
	Vector3 second_center, Vector3 second_extent) noexcept
{
	return std::fabs(second_center.x - first_center.x) > second_extent.x + first_extent.x
		|| std::fabs(second_center.y - first_center.y) > second_extent.y + first_extent.y
		|| std::fabs(second_center.z - first_center.z) > second_extent.z + first_extent.z;
}

constexpr Vector3 RotateZQuarterTurns(Vector3 vector, unsigned quarter_turns) noexcept
{
	switch (quarter_turns & 3u) {
	case 1: return {-vector.y, vector.x, vector.z};
	case 2: return {-vector.x, -vector.y, vector.z};
	case 3: return {vector.y, -vector.x, vector.z};
	default: return vector;
	}
}

constexpr AxisAlignedBox3 RotateBoundsZQuarterTurns(const AxisAlignedBox3 &bounds,
	unsigned quarter_turns) noexcept
{
	const auto &minimum = bounds.minimum;
	const auto &maximum = bounds.maximum;
	switch (quarter_turns & 3u) {
	case 1: return {{-maximum.y, minimum.x, minimum.z}, {-minimum.y, maximum.x, maximum.z}};
	case 2: return {{-maximum.x, -maximum.y, minimum.z}, {-minimum.x, -minimum.y, maximum.z}};
	case 3: return {{minimum.y, -maximum.x, minimum.z}, {maximum.y, -minimum.x, maximum.z}};
	default: return bounds;
	}
}

inline AxisAlignedBox3 TransformBoundsCorners(const AxisAlignedBox3 &bounds,
	const std::array<float, 12> &transform) noexcept
{
	const auto &low = bounds.minimum;
	const auto &high = bounds.maximum;
	const std::array<Vector3, 8> corners{{
		{low.x, low.y, low.z}, {low.x, high.y, low.z},
		{high.x, high.y, low.z}, {high.x, low.y, low.z},
		{low.x, low.y, high.z}, {low.x, high.y, high.z},
		{high.x, high.y, high.z}, {high.x, low.y, high.z}}};
	AxisAlignedBox3 result;
	for (unsigned index = 0; index < corners.size(); ++index) {
		const auto &point = corners[index];
		const Vector3 transformed{
			transform[0] * point.x + transform[1] * point.y + transform[2] * point.z + transform[3],
			transform[4] * point.x + transform[5] * point.y + transform[6] * point.z + transform[7],
			transform[8] * point.x + transform[9] * point.y + transform[10] * point.z + transform[11]};
		if (index == 0) {
			result = {transformed, transformed};
			continue;
		}
		if (result.minimum.x >= transformed.x) result.minimum.x = transformed.x;
		if (result.minimum.y >= transformed.y) result.minimum.y = transformed.y;
		if (result.minimum.z >= transformed.z) result.minimum.z = transformed.z;
		if (result.maximum.x <= transformed.x) result.maximum.x = transformed.x;
		if (result.maximum.y <= transformed.y) result.maximum.y = transformed.y;
		if (result.maximum.z <= transformed.z) result.maximum.z = transformed.z;
	}
	return result;
}

inline std::optional<OrientedBox3> TransformOrientedBox(
	const OrientedBox3 &box, const AffineTransform3 &transform) noexcept
{
	if (!box.Is_Valid()) return std::nullopt;
	OrientedBox3 result;
	result.center = transform.Transform_Point(box.center);
	for (std::size_t index = 0; index < box.axes.size(); ++index) {
		const Vector3 transformed_axis = transform.Transform_Vector(box.axes[index]);
		const float scale = std::sqrt(transformed_axis.Dot(transformed_axis));
		if (!(scale > 0.0f) || !std::isfinite(scale)) return std::nullopt;
		result.axes[index] = transformed_axis / scale;
		if (index == 0) result.half_extent.x = box.half_extent.x * scale;
		else if (index == 1) result.half_extent.y = box.half_extent.y * scale;
		else result.half_extent.z = box.half_extent.z * scale;
	}
	return result.Is_Valid() ? std::optional<OrientedBox3>(result) : std::nullopt;
}
}
