module;

#include <cmath>
#include <optional>

export module Engine.Core.Math.LineGeometry3;

export import Engine.Core.Math.Vector3;

export namespace Engine::Math
{
struct ClosestLinePoints3 final
{
	Vector3 first{};
	Vector3 second{};
};

struct LineGeometry3 final
{
	static Vector3 Closest_Point_On_Segment(Vector3 start, Vector3 end, Vector3 point) noexcept
	{
		const Vector3 delta = end - start;
		const float length_squared = delta.Dot(delta);
		if (!(length_squared > 0.0f) || !std::isfinite(length_squared)) return start;
		const float parameter = (point - start).Dot(delta) / length_squared;
		if (!(parameter > 0.0f)) return start;
		if (!(parameter < 1.0f)) return end;
		return start + delta * parameter;
	}

	// Returns the nearest points on two infinite lines. The result is empty for
	// parallel, degenerate, or non-finite line inputs.
	static std::optional<ClosestLinePoints3> Closest_Points_On_Lines(
		Vector3 first_start, Vector3 first_end, Vector3 second_start, Vector3 second_end) noexcept
	{
		const Vector3 first_delta = first_end - first_start;
		const Vector3 second_delta = second_end - second_start;
		const float first_length = first_delta.Length();
		const float second_length = second_delta.Length();
		if (!(first_length > 0.0f) || !(second_length > 0.0f)
			|| !std::isfinite(first_length) || !std::isfinite(second_length)) return std::nullopt;

		const Vector3 first_direction = first_delta / first_length;
		const Vector3 second_direction = second_delta / second_length;
		const Vector3 cross = first_direction.Cross(second_direction);
		const float denominator = cross.Dot(cross);
		if (!(denominator > 0.0f) || !std::isfinite(denominator)) return std::nullopt;

		const Vector3 first_offset = (second_start - first_start).Cross(second_direction);
		const Vector3 second_offset = (first_start - second_start).Cross(first_direction);
		const float first_distance = first_offset.Dot(cross) / denominator;
		const float second_distance = second_offset.Dot(cross * -1.0f) / denominator;
		if (!std::isfinite(first_distance) || !std::isfinite(second_distance)) return std::nullopt;

		const ClosestLinePoints3 result{
			first_start + first_direction * first_distance,
			second_start + second_direction * second_distance};
		if (!std::isfinite(result.first.x) || !std::isfinite(result.first.y) || !std::isfinite(result.first.z)
			|| !std::isfinite(result.second.x) || !std::isfinite(result.second.y) || !std::isfinite(result.second.z))
			return std::nullopt;
		return result;
	}
};
}
