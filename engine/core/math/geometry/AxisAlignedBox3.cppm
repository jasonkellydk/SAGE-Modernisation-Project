export module Engine.Core.Math.AxisAlignedBox3;

export import Engine.Core.Math.Vector3;
export import Engine.Core.Math.CollisionResult3;

export namespace Engine::Math
{
// Result of the legacy WWMath Test_Aligned_Box slab test.
struct LegacyBoxSegmentTest3 final
{
	float fraction = 0.0f;
	bool inside = false;
	unsigned axis = 0;
	bool positive_side = false;
};

// Legacy bit-compatible port of WWMath's Test_Aligned_Box (colmathline.cpp):
// binary32 candidate-plane test of the segment start + t * delta against
// [minimum, maximum]. Returns false on a miss. When the start is inside the box
// the result has inside = true and fraction = 0. The fraction is NOT clamped
// to 1; callers compare it against their current best fraction.
inline bool Test_Aligned_Box_Legacy(Vector3 minimum, Vector3 maximum,
	Vector3 start, Vector3 delta, LegacyBoxSegmentTest3 &test) noexcept
{
	constexpr int side_negative = 0;
	constexpr int side_positive = 1;
	constexpr int side_middle = 2;
	float candidate_plane[3] = {0.0f, 0.0f, 0.0f};
	float max_t[3];
	int quadrant[3];
	bool inside = true;
	for (unsigned i = 0; i < 3; ++i) {
		if (start[i] < minimum[i]) {
			quadrant[i] = side_negative;
			candidate_plane[i] = minimum[i];
			inside = false;
		} else if (start[i] > maximum[i]) {
			quadrant[i] = side_positive;
			candidate_plane[i] = maximum[i];
			inside = false;
		} else {
			quadrant[i] = side_middle;
		}
	}
	if (inside) {
		test.fraction = 0.0f;
		test.inside = true;
		return true;
	}
	for (unsigned i = 0; i < 3; ++i) {
		if (quadrant[i] != side_middle && delta[i] != 0.0f)
			max_t[i] = (candidate_plane[i] - start[i]) / delta[i];
		else
			max_t[i] = -1.0f;
	}
	unsigned intersection_plane = 0;
	for (unsigned i = 1; i < 3; ++i)
		if (max_t[i] > max_t[intersection_plane]) intersection_plane = i;
	if (max_t[intersection_plane] < 0.0f) return false;
	for (unsigned i = 0; i < 3; ++i) {
		if (intersection_plane != i) {
			const float coordinate = start[i] + max_t[intersection_plane] * delta[i];
			if (coordinate < minimum[i] || coordinate > maximum[i]) return false;
		}
	}
	test.fraction = max_t[intersection_plane];
	test.inside = false;
	test.axis = intersection_plane;
	test.positive_side = quadrant[intersection_plane] == side_positive;
	return true;
}

struct AxisAlignedBox3 final
{
	Vector3 minimum{};
	Vector3 maximum{};

	constexpr bool Is_Valid() const noexcept
	{
		return minimum.x <= maximum.x && minimum.y <= maximum.y && minimum.z <= maximum.z;
	}

	constexpr Vector3 Center() const noexcept { return (minimum + maximum) * 0.5f; }
	constexpr Vector3 Extent() const noexcept { return (maximum - minimum) * 0.5f; }

	constexpr bool Contains(Vector3 point) const noexcept
	{
		return Is_Valid() && point.x >= minimum.x && point.x <= maximum.x
			&& point.y >= minimum.y && point.y <= maximum.y
			&& point.z >= minimum.z && point.z <= maximum.z;
	}

	constexpr bool Intersects(const AxisAlignedBox3 &other) const noexcept
	{
		return Is_Valid() && other.Is_Valid()
			&& minimum.x <= other.maximum.x && maximum.x >= other.minimum.x
			&& minimum.y <= other.maximum.y && maximum.y >= other.minimum.y
			&& minimum.z <= other.maximum.z && maximum.z >= other.minimum.z;
	}

	bool Intersects_Segment(Vector3 start, Vector3 end) const noexcept
	{
		constexpr float max_finite = 3.402823466e+38F;
		const auto finite = [max_finite](float value) { return value >= -max_finite && value <= max_finite; };
		if (!Is_Valid() || !finite(start.x) || !finite(start.y) || !finite(start.z)
			|| !finite(end.x) || !finite(end.y) || !finite(end.z)) return false;
		const double origin[3] = {start.x, start.y, start.z};
		const double direction[3] = {static_cast<double>(end.x) - start.x,
			static_cast<double>(end.y) - start.y, static_cast<double>(end.z) - start.z};
		const double low[3] = {minimum.x, minimum.y, minimum.z};
		const double high[3] = {maximum.x, maximum.y, maximum.z};
		double entry = 0.0;
		double exit = 1.0;
		for (unsigned axis = 0; axis < 3; ++axis) {
			if (direction[axis] == 0.0) {
				if (origin[axis] < low[axis] || origin[axis] > high[axis]) return false;
				continue;
			}
			double near_time = (low[axis] - origin[axis]) / direction[axis];
			double far_time = (high[axis] - origin[axis]) / direction[axis];
			if (near_time > far_time) {
				const double temporary = near_time;
				near_time = far_time;
				far_time = temporary;
			}
			if (near_time > entry) entry = near_time;
			if (far_time < exit) exit = far_time;
			if (entry > exit) return false;
		}
		return true;
	}

	// Legacy bit-compatible port of CollisionMath::Collide(LineSeg, AABox).
	// A start inside the box sets result.starts_overlapping (legacy StartBad),
	// stores the start as the contact point when requested, leaves the
	// fraction unchanged and returns true. Otherwise a hit closer than
	// result.fraction replaces fraction, normal, and contact point.
	bool Collide_Segment_Legacy(Vector3 start, Vector3 end, CollisionResult3 &result) const noexcept
	{
		const Vector3 delta = end - start;
		LegacyBoxSegmentTest3 test;
		if (!Test_Aligned_Box_Legacy(minimum, maximum, start, delta, test)) return false;
		if (test.inside) {
			result.starts_overlapping = true;
			if (result.compute_contact_point) result.contact_point = start;
			return true;
		}
		if (test.fraction < result.fraction) {
			result.fraction = test.fraction;
			Vector3 normal{};
			normal[test.axis] = test.positive_side ? 1.0f : -1.0f;
			result.normal = normal;
			if (result.compute_contact_point) result.contact_point = start + result.fraction * delta;
			return true;
		}
		return false;
	}

	constexpr void Include(Vector3 point) noexcept
	{
		if (!Is_Valid()) { minimum = maximum = point; return; }
		if (point.x < minimum.x) minimum.x = point.x;
		if (point.y < minimum.y) minimum.y = point.y;
		if (point.z < minimum.z) minimum.z = point.z;
		if (point.x > maximum.x) maximum.x = point.x;
		if (point.y > maximum.y) maximum.y = point.y;
		if (point.z > maximum.z) maximum.z = point.z;
	}
};
}
