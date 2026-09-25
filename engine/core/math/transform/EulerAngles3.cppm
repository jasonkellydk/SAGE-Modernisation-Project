module;

#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

export module Engine.Core.Math.EulerAngles3;

export import Engine.Core.Math.AffineTransform3;

export namespace Engine::Math
{
struct EulerAngles3 final
{
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;

	friend constexpr bool operator==(EulerAngles3, EulerAngles3) noexcept = default;

	// Extracts the historical XYZ rotating-frame representation used by the
	// emitter orientation editor. Input is a row-major rotation basis.
	static EulerAngles3 From_Rotation_XYZ_Rotating_Frame(const AffineTransform3 &matrix) noexcept
	{
		const auto at = [&matrix](unsigned row, unsigned column) {
			return static_cast<double>(matrix.elements[row * 4 + column]);
		};
		const double cy = std::sqrt(at(2, 2) * at(2, 2) + at(1, 2) * at(1, 2));
		double first;
		double second;
		double third;
		if (cy > 16.0 * std::numeric_limits<float>::epsilon()) {
			first = std::atan2(at(0, 1), at(0, 0));
			second = std::atan2(-at(0, 2), cy);
			third = std::atan2(at(1, 2), at(2, 2));
		} else {
			first = std::atan2(-at(1, 0), at(1, 1));
			second = std::atan2(-at(0, 2), cy);
			third = 0.0;
		}

		// This convention has odd parity and rotating axes: negate, then swap
		// the first and third rotations.
		first = -first;
		second = -second;
		third = -third;
		std::swap(first, third);

		// Select the equivalent XYZr representation nearest the origin, matching
		// the established editor behavior near the half-turn discontinuity.
		constexpr double pi = std::numbers::pi_v<double>;
		constexpr double tau = 2.0 * pi;
		double alternate_x = pi + first;
		double alternate_y = pi - second;
		double alternate_z = pi + third;
		if (alternate_x > pi) alternate_x -= tau;
		if (alternate_y > pi) alternate_y -= tau;
		if (alternate_z > pi) alternate_z -= tau;
		const double original_magnitude = first * first + second * second + third * third;
		const double alternate_magnitude = alternate_x * alternate_x + alternate_y * alternate_y + alternate_z * alternate_z;
		if (alternate_magnitude < original_magnitude)
			return {alternate_x, alternate_y, alternate_z};
		return {first, second, third};
	}
};
}
