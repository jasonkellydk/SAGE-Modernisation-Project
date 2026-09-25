module;

#include <span>
#include <vector>

export module Engine.Core.Math.PolygonClip3;

export import Engine.Core.Math.Plane3;

export namespace Engine::Math
{
struct PolygonClip3 final
{
	// Clips a convex polygon to the half-space with signed plane distance <= 0.
	// Fewer than three input points produce an empty polygon.
	static std::vector<Vector3> Against_Plane(std::span<const Vector3> polygon, const Plane3 &plane)
	{
		std::vector<Vector3> clipped;
		if (polygon.size() < 3) return clipped;
		clipped.reserve(polygon.size() + 1);

		Vector3 previous = polygon.back();
		float previous_distance = plane.Signed_Distance(previous);
		bool previous_inside = previous_distance <= 0.0f;
		for (const Vector3 current : polygon) {
			const float current_distance = plane.Signed_Distance(current);
			const bool current_inside = current_distance <= 0.0f;
			if (previous_inside != current_inside) {
				const float parameter = previous_distance / (previous_distance - current_distance);
				clipped.push_back(previous + (current - previous) * parameter);
			}
			if (current_inside) clipped.push_back(current);
			previous = current;
			previous_distance = current_distance;
			previous_inside = current_inside;
		}
		return clipped;
	}
};
}
