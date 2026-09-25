module;

#include <array>
#include <cmath>
#include <optional>
#include <utility>

export module Engine.Core.Math.Matrix4;

export import Engine.Core.Math.Vector3;

export namespace Engine::Math
{
// Row-major 4x4 matrix using column vectors. Inversion uses fixed-order
// binary64 Gauss-Jordan elimination and returns empty for singular inputs.
struct Matrix4 final
{
	std::array<float, 16> elements{
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1};

	static constexpr Matrix4 Identity() noexcept { return {}; }
	constexpr float operator()(unsigned row, unsigned column) const noexcept
	{
		return elements[row * 4 + column];
	}
	constexpr float &operator()(unsigned row, unsigned column) noexcept
	{
		return elements[row * 4 + column];
	}

	friend constexpr Matrix4 Compose(const Matrix4 &left, const Matrix4 &right) noexcept
	{
		Matrix4 result{};
		result.elements.fill(0.0f);
		for (unsigned row = 0; row < 4; ++row)
			for (unsigned column = 0; column < 4; ++column)
				for (unsigned element = 0; element < 4; ++element)
					result(row, column) += left(row, element) * right(element, column);
		return result;
	}

	std::optional<Matrix4> Inverse() const noexcept
	{
		double augmented[4][8]{};
		for (unsigned row = 0; row < 4; ++row) {
			for (unsigned column = 0; column < 4; ++column) {
				const float value = (*this)(row, column);
				if (!std::isfinite(value)) return std::nullopt;
				augmented[row][column] = value;
			}
			augmented[row][4 + row] = 1.0;
		}

		for (unsigned column = 0; column < 4; ++column) {
			unsigned pivot_row = column;
			double pivot_magnitude = std::abs(augmented[pivot_row][column]);
			for (unsigned row = column + 1; row < 4; ++row) {
				const double magnitude = std::abs(augmented[row][column]);
				if (magnitude > pivot_magnitude) {
					pivot_magnitude = magnitude;
					pivot_row = row;
				}
			}
			if (pivot_magnitude == 0.0) return std::nullopt;
			if (pivot_row != column)
				for (unsigned element = 0; element < 8; ++element)
					std::swap(augmented[pivot_row][element], augmented[column][element]);

			const double pivot = augmented[column][column];
			for (unsigned element = 0; element < 8; ++element)
				augmented[column][element] /= pivot;
			for (unsigned row = 0; row < 4; ++row) {
				if (row == column) continue;
				const double factor = augmented[row][column];
				for (unsigned element = 0; element < 8; ++element)
					augmented[row][element] -= factor * augmented[column][element];
			}
		}

		Matrix4 result{};
		for (unsigned row = 0; row < 4; ++row)
			for (unsigned column = 0; column < 4; ++column) {
				const float value = static_cast<float>(augmented[row][4 + column]);
				if (!std::isfinite(value)) return std::nullopt;
				result(row, column) = value;
			}
		return result;
	}

	Vector3 Transform_Point(Vector3 point) const noexcept
	{
		const float x = (*this)(0, 0) * point.x + (*this)(0, 1) * point.y
			+ (*this)(0, 2) * point.z + (*this)(0, 3);
		const float y = (*this)(1, 0) * point.x + (*this)(1, 1) * point.y
			+ (*this)(1, 2) * point.z + (*this)(1, 3);
		const float z = (*this)(2, 0) * point.x + (*this)(2, 1) * point.y
			+ (*this)(2, 2) * point.z + (*this)(2, 3);
		const float w = (*this)(3, 0) * point.x + (*this)(3, 1) * point.y
			+ (*this)(3, 2) * point.z + (*this)(3, 3);
		if (w == 0.0f || w == 1.0f) return {x, y, z};
		return {x / w, y / w, z / w};
	}
};
}
