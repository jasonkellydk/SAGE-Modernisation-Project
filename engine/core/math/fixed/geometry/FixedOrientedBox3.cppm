export module Engine.Core.Math.FixedOrientedBox3;
import std;
export import Engine.Core.Math.FixedVector;

export namespace Engine::Math
{
// The basis stores orthonormal axes as columns. Extents are half widths.
// Authoritative queries use the same fixed representation as ECS transforms.
struct FixedOrientedBox3
{
	std::array<FixedVector3, 3> axes{{{Fixed::One(), {}, {}}, {{}, Fixed::One(), {}}, {{}, {}, Fixed::One()}}};
	FixedVector3 center, extent;
	constexpr FixedVector3 LocalPoint(FixedVector3 point) const noexcept {
		const auto delta = point - center;
		return {Dot(delta, axes[0]), Dot(delta, axes[1]), Dot(delta, axes[2])};
	}
	constexpr bool Contains(FixedVector3 point, Fixed padding = {}) const noexcept {
		const auto local = LocalPoint(point);
		return Abs(local.x) <= extent.x + padding && Abs(local.y) <= extent.y + padding && Abs(local.z) <= extent.z + padding;
	}
	constexpr FixedVector3 WorldPoint(FixedVector3 point) const noexcept {
		return center + axes[0] * point.x + axes[1] * point.y + axes[2] * point.z;
	}
	bool IsValid(Fixed tolerance = Fixed::FromRatio(1, 1000)) const noexcept {
		if (extent.x < Fixed{} || extent.y < Fixed{} || extent.z < Fixed{}) return false;
		for (std::size_t i = 0; i < 3; ++i) {
			if (Abs(Dot(axes[i], axes[i]) - Fixed::One()) > tolerance) return false;
			for (std::size_t j = i + 1; j < 3; ++j) if (Abs(Dot(axes[i], axes[j])) > tolerance) return false;
		}
		return true;
	}
};
}
