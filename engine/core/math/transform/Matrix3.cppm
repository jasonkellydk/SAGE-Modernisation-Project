module;

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

export module Engine.Core.Math.Matrix3;

export import Engine.Core.Math.AffineTransform3;

export namespace Engine::Math
{
// Row-major 3x3 matrix using column vectors. Inversion uses fixed-order
// binary64 Gauss-Jordan elimination and rejects singular or non-finite inputs.
struct Matrix3 final
{
	std::array<float, 9> elements{
		1.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 1.0f};

	friend constexpr bool operator==(const Matrix3 &, const Matrix3 &) noexcept = default;
	static constexpr Matrix3 Identity() noexcept { return {}; }
	static constexpr Matrix3 From_Affine_Transform(const AffineTransform3 &transform) noexcept
	{
		Matrix3 result;
		for (std::size_t row = 0; row < 3; ++row)
			for (std::size_t column = 0; column < 3; ++column)
				result(row, column) = transform.elements[row * 4 + column];
		return result;
	}
	AffineTransform3 To_Affine_Transform() const noexcept
	{
		AffineTransform3 result;
		for (std::size_t row = 0; row < 3; ++row)
			for (std::size_t column = 0; column < 3; ++column)
				result.elements[row * 4 + column] = (*this)(row, column);
		return result;
	}
	static Matrix3 Rotation_X(float radians) noexcept
	{
		const float sine = std::sin(radians);
		const float cosine = std::cos(radians);
		return {{1.0f, 0.0f, 0.0f, 0.0f, cosine, -sine, 0.0f, sine, cosine}};
	}
	static Matrix3 Rotation_Y(float radians) noexcept
	{
		const float sine = std::sin(radians);
		const float cosine = std::cos(radians);
		return {{cosine, 0.0f, sine, 0.0f, 1.0f, 0.0f, -sine, 0.0f, cosine}};
	}
	static Matrix3 Rotation_Z(float radians) noexcept
	{
		const float sine = std::sin(radians);
		const float cosine = std::cos(radians);
		return {{cosine, -sine, 0.0f, sine, cosine, 0.0f, 0.0f, 0.0f, 1.0f}};
	}

	constexpr float operator()(std::size_t row, std::size_t column) const noexcept
	{
		return elements[row * 3 + column];
	}
	constexpr float &operator()(std::size_t row, std::size_t column) noexcept
	{
		return elements[row * 3 + column];
	}

	constexpr Vector3 Transform(Vector3 vector) const noexcept
	{
		return {
			(*this)(0, 0) * vector.x + (*this)(0, 1) * vector.y + (*this)(0, 2) * vector.z,
			(*this)(1, 0) * vector.x + (*this)(1, 1) * vector.y + (*this)(1, 2) * vector.z,
			(*this)(2, 0) * vector.x + (*this)(2, 1) * vector.y + (*this)(2, 2) * vector.z};
	}

	constexpr Matrix3 Transposed() const noexcept
	{
		Matrix3 result{};
		for (std::size_t row = 0; row < 3; ++row)
			for (std::size_t column = 0; column < 3; ++column)
				result(row, column) = (*this)(column, row);
		return result;
	}

	constexpr float Determinant() const noexcept
	{
		return (*this)(0, 0) * ((*this)(1, 1) * (*this)(2, 2) - (*this)(1, 2) * (*this)(2, 1))
			- (*this)(0, 1) * ((*this)(1, 0) * (*this)(2, 2) - (*this)(1, 2) * (*this)(2, 0))
			+ (*this)(0, 2) * ((*this)(1, 0) * (*this)(2, 1) - (*this)(1, 1) * (*this)(2, 0));
	}

	bool Is_Orthonormal(float tolerance = 1.0e-4f) const noexcept
	{
		if (!(tolerance >= 0.0f) || !std::isfinite(tolerance)) return false;
		for (std::size_t row = 0; row < 3; ++row)
			for (std::size_t column = 0; column < 3; ++column) {
				double product = 0.0;
				for (std::size_t element = 0; element < 3; ++element)
					product += static_cast<double>((*this)(row, element)) * (*this)(column, element);
				const double expected = row == column ? 1.0 : 0.0;
				if (!std::isfinite(product) || std::abs(product - expected) > tolerance) return false;
			}
		return true;
	}

	std::optional<Matrix3> Inverse() const noexcept
	{
		double augmented[3][6]{};
		for (std::size_t row = 0; row < 3; ++row) {
			for (std::size_t column = 0; column < 3; ++column) {
				const float value = (*this)(row, column);
				if (!std::isfinite(value)) return std::nullopt;
				augmented[row][column] = value;
			}
			augmented[row][3 + row] = 1.0;
		}

		for (std::size_t column = 0; column < 3; ++column) {
			std::size_t pivot_row = column;
			double pivot_magnitude = std::abs(augmented[pivot_row][column]);
			for (std::size_t row = column + 1; row < 3; ++row) {
				const double magnitude = std::abs(augmented[row][column]);
				if (magnitude > pivot_magnitude) {
					pivot_magnitude = magnitude;
					pivot_row = row;
				}
			}
			if (pivot_magnitude == 0.0) return std::nullopt;
			if (pivot_row != column)
				for (std::size_t element = 0; element < 6; ++element)
					std::swap(augmented[pivot_row][element], augmented[column][element]);

			const double pivot = augmented[column][column];
			for (std::size_t element = 0; element < 6; ++element)
				augmented[column][element] /= pivot;
			for (std::size_t row = 0; row < 3; ++row) {
				if (row == column) continue;
				const double factor = augmented[row][column];
				for (std::size_t element = 0; element < 6; ++element)
					augmented[row][element] -= factor * augmented[column][element];
			}
		}

		Matrix3 result{};
		for (std::size_t row = 0; row < 3; ++row)
			for (std::size_t column = 0; column < 3; ++column) {
				const float value = static_cast<float>(augmented[row][3 + column]);
				if (!std::isfinite(value)) return std::nullopt;
				result(row, column) = value;
			}
		return result;
	}

	friend constexpr Matrix3 Compose(const Matrix3 &left, const Matrix3 &right) noexcept
	{
		Matrix3 result{};
		result.elements.fill(0.0f);
		for (std::size_t row = 0; row < 3; ++row)
			for (std::size_t column = 0; column < 3; ++column)
				for (std::size_t element = 0; element < 3; ++element)
					result(row, column) += left(row, element) * right(element, column);
		return result;
	}
};
}
