module;

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

export module Engine.Core.Math.AffineTransform3;

export import Engine.Core.Math.Vector3;

export namespace Engine::Math
{
// Row-major 3x4 affine transform. Points are column vectors: p' = M * p.
struct AffineTransform3 final
{
	std::array<float, 12> elements{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f};

	friend constexpr bool operator==(const AffineTransform3 &, const AffineTransform3 &) noexcept = default;
	constexpr float *operator[](std::size_t row) noexcept { return elements.data() + row * 4; }
	constexpr const float *operator[](std::size_t row) const noexcept { return elements.data() + row * 4; }

	static constexpr AffineTransform3 Identity() noexcept { return {}; }
	static constexpr AffineTransform3 From_Translation(Vector3 translation) noexcept
	{
		AffineTransform3 result;
		result.Set_Translation(translation);
		return result;
	}
	static constexpr AffineTransform3 From_Uniform_Scale(float scale) noexcept
	{
		return From_Basis({scale, 0.0f, 0.0f}, {0.0f, scale, 0.0f}, {0.0f, 0.0f, scale});
	}

	static constexpr AffineTransform3 From_Basis(
		Vector3 x_axis, Vector3 y_axis, Vector3 z_axis, Vector3 translation = {}) noexcept
	{
		return {{
			x_axis.x, y_axis.x, z_axis.x, translation.x,
			x_axis.y, y_axis.y, z_axis.y, translation.y,
			x_axis.z, y_axis.z, z_axis.z, translation.z}};
	}

	// Builds an orthonormal transform whose positive X axis follows the direction.
	// The vertical direction uses a stable zero-yaw fallback.
	static AffineTransform3 From_Forward_Direction(Vector3 position, Vector3 forward) noexcept
	{
		forward = forward.Normalized();
		const float horizontal_length = std::sqrt(forward.x * forward.x + forward.y * forward.y);
		if (horizontal_length == 0.0f && forward.z == 0.0f)
			return From_Translation(position);

		const float sine_pitch = forward.z;
		const float cosine_pitch = horizontal_length;
		const float sine_yaw = horizontal_length == 0.0f ? 0.0f : forward.y / horizontal_length;
		const float cosine_yaw = horizontal_length == 0.0f ? 1.0f : forward.x / horizontal_length;
		return From_Basis(
			{cosine_yaw * cosine_pitch, sine_yaw * cosine_pitch, sine_pitch},
			{-sine_yaw, cosine_yaw, 0.0f},
			{-cosine_yaw * sine_pitch, -sine_yaw * sine_pitch, cosine_pitch},
			position);
	}

	// Creates a camera transform with local +X right, +Y up, and +Z backward.
	// A deterministic alternate up axis is selected when the requested up axis
	// is parallel to the view direction.
	static AffineTransform3 Look_At(Vector3 position, Vector3 target,
		Vector3 up = {0.0f, 0.0f, 1.0f}) noexcept
	{
		const Vector3 backward = (position - target).Normalized();
		if (backward.Length_Squared() == 0.0f)
			return From_Translation(position);

		Vector3 right = up.Cross(backward).Normalized();
		if (right.Length_Squared() == 0.0f && backward.x == 0.0f && backward.y == 0.0f) {
			// Exactly vertical view: match the legacy Look_At_Dir zero-yaw basis
			// (sin yaw 0, cos yaw 1), whose right axis is world -Y.
			right = {0.0f, -1.0f, 0.0f};
		} else if (right.Length_Squared() == 0.0f) {
			const Vector3 fallback_up = std::abs(backward.z) > 0.9f
				? Vector3{0.0f, 1.0f, 0.0f} : Vector3{0.0f, 0.0f, 1.0f};
			right = fallback_up.Cross(backward).Normalized();
		}
		const Vector3 corrected_up = backward.Cross(right).Normalized();
		return From_Basis(right, corrected_up, backward, position);
	}

	static AffineTransform3 Look_Along(Vector3 position, Vector3 forward,
		float roll_radians = 0.0f, Vector3 up = {0.0f, 0.0f, 1.0f}) noexcept
	{
		auto result = Look_At(position, position + forward, up);
		// Roll is negated because the camera looks down its local -Z axis
		// (legacy Look_At_Dir applied Rotate_Z(-roll)).
		if (std::isfinite(roll_radians))
			result = Compose(result, Rotation_Z(-roll_radians));
		return result;
	}

	// ---- Legacy bit-compatible variants -------------------------------------
	// These reproduce the retired WWMath Matrix3D routines with the same
	// operation order so simulation results stay bit-identical. Prefer the
	// modern functions above for new code.

	// Legacy Matrix3D(axis, angle): rotation about a unit axis, zero translation.
	static AffineTransform3 From_Axis_Angle_Legacy(Vector3 axis, float angle_radians) noexcept
	{
		const float c = std::cos(angle_radians);
		const float s = std::sin(angle_radians);
		const float a0 = axis.x, a1 = axis.y, a2 = axis.z;
		AffineTransform3 result;
		result.elements = {
			a0 * a0 + c * (1.0f - a0 * a0),
			a0 * a1 * (1.0f - c) - a2 * s,
			a2 * a0 * (1.0f - c) + a1 * s,
			0.0f,
			a0 * a1 * (1.0f - c) + a2 * s,
			a1 * a1 + c * (1.0f - a1 * a1),
			a1 * a2 * (1.0f - c) - a0 * s,
			0.0f,
			a2 * a0 * (1.0f - c) - a1 * s,
			a1 * a2 * (1.0f - c) + a0 * s,
			a2 * a2 + c * (1.0f - a2 * a2),
			0.0f};
		return result;
	}

	// Legacy Matrix3D::buildTransformMatrix(pos, dir): +X follows the direction.
	// The direction is NOT normalized; pass a unit vector.
	static AffineTransform3 From_Unit_Forward_Direction(Vector3 position, Vector3 unit_direction) noexcept
	{
		const float len2 = std::sqrt((unit_direction.x * unit_direction.x) + (unit_direction.y * unit_direction.y));
		const float sinp = unit_direction.z;
		const float cosp = len2;
		float siny;
		float cosy;
		if (len2 != 0.0f) {
			siny = unit_direction.y / len2;
			cosy = unit_direction.x / len2;
		} else {
			siny = 0.0f;
			cosy = 1.0f;
		}
		AffineTransform3 result;
		result.Legacy_Translate(position);
		result.Legacy_Rotate_Z(siny, cosy);
		result.Legacy_Rotate_Y(-sinp, cosp);
		return result;
	}

	// Legacy Matrix3D::Get_Orthogonal_Inverse: transpose the rotation and set
	// translation to -(R^T * t). Only valid for orthonormal rotations.
	AffineTransform3 Orthogonal_Inverse() const noexcept
	{
		AffineTransform3 inverse;
		inverse.elements[0] = elements[0];
		inverse.elements[1] = elements[4];
		inverse.elements[2] = elements[8];
		inverse.elements[4] = elements[1];
		inverse.elements[5] = elements[5];
		inverse.elements[6] = elements[9];
		inverse.elements[8] = elements[2];
		inverse.elements[9] = elements[6];
		inverse.elements[10] = elements[10];
		const Vector3 translation = Translation();
		const Vector3 rotated{
			(inverse.elements[0] * translation.x + inverse.elements[1] * translation.y + inverse.elements[2] * translation.z),
			(inverse.elements[4] * translation.x + inverse.elements[5] * translation.y + inverse.elements[6] * translation.z),
			(inverse.elements[8] * translation.x + inverse.elements[9] * translation.y + inverse.elements[10] * translation.z)};
		const Vector3 negated = -rotated;
		inverse.elements[3] = negated.x;
		inverse.elements[7] = negated.y;
		inverse.elements[11] = negated.z;
		return inverse;
	}

	// Legacy Matrix3D::Get_Z_Rotation: WWMath::Atan2(Row[1][0], Row[0][0]).
	float Z_Rotation_Legacy() const noexcept
	{
		return static_cast<float>(std::atan2(static_cast<double>(elements[4]), static_cast<double>(elements[0])));
	}

	constexpr Vector3 Translation() const noexcept
	{
		return {elements[3], elements[7], elements[11]};
	}

	constexpr void Set_Translation(Vector3 translation) noexcept
	{
		elements[3] = translation.x;
		elements[7] = translation.y;
		elements[11] = translation.z;
	}

	constexpr void Adjust_Translation(Vector3 offset) noexcept
	{
		Set_Translation(Translation() + offset);
	}

	constexpr Vector3 Basis_X() const noexcept
	{
		return {elements[0], elements[4], elements[8]};
	}

	constexpr Vector3 Basis_Y() const noexcept
	{
		return {elements[1], elements[5], elements[9]};
	}

	constexpr Vector3 Basis_Z() const noexcept
	{
		return {elements[2], elements[6], elements[10]};
	}

	template<class Matrix>
	static AffineTransform3 From_Row_Matrix(const Matrix &source) noexcept
	{
		AffineTransform3 result;
		for (std::size_t row = 0; row < 3; ++row)
			for (std::size_t column = 0; column < 4; ++column)
				result.elements[row * 4 + column] = source[row][column];
		return result;
	}

	static AffineTransform3 Rotation_X(float radians) noexcept
	{
		const float sine = std::sin(radians);
		const float cosine = std::cos(radians);
		AffineTransform3 result;
		result.elements[5] = cosine;
		result.elements[6] = -sine;
		result.elements[9] = sine;
		result.elements[10] = cosine;
		return result;
	}

	static AffineTransform3 Rotation_Y(float radians) noexcept
	{
		const float sine = std::sin(radians);
		const float cosine = std::cos(radians);
		AffineTransform3 result;
		result.elements[0] = cosine;
		result.elements[2] = sine;
		result.elements[8] = -sine;
		result.elements[10] = cosine;
		return result;
	}

	static AffineTransform3 Rotation_Z(float radians) noexcept
	{
		const float sine = std::sin(radians);
		const float cosine = std::cos(radians);
		AffineTransform3 result;
		result.elements[0] = cosine;
		result.elements[1] = -sine;
		result.elements[4] = sine;
		result.elements[5] = cosine;
		return result;
	}

	constexpr Vector3 Transform_Point(Vector3 point) const noexcept
	{
		return {
			elements[0] * point.x + elements[1] * point.y + elements[2] * point.z + elements[3],
			elements[4] * point.x + elements[5] * point.y + elements[6] * point.z + elements[7],
			elements[8] * point.x + elements[9] * point.y + elements[10] * point.z + elements[11]};
	}

	constexpr Vector3 Transform_Vector(Vector3 vector) const noexcept
	{
		return {
			elements[0] * vector.x + elements[1] * vector.y + elements[2] * vector.z,
			elements[4] * vector.x + elements[5] * vector.y + elements[6] * vector.z,
			elements[8] * vector.x + elements[9] * vector.y + elements[10] * vector.z};
	}

	std::optional<AffineTransform3> Inverse() const noexcept
	{
		const float a = elements[0], b = elements[1], c = elements[2];
		const float d = elements[4], e = elements[5], f = elements[6];
		const float g = elements[8], h = elements[9], i = elements[10];
		const float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
		if (determinant == 0.0f || !std::isfinite(determinant)) return std::nullopt;

		const float inverse_determinant = 1.0f / determinant;
		AffineTransform3 result;
		result.elements[0] = (e * i - f * h) * inverse_determinant;
		result.elements[1] = (c * h - b * i) * inverse_determinant;
		result.elements[2] = (b * f - c * e) * inverse_determinant;
		result.elements[4] = (f * g - d * i) * inverse_determinant;
		result.elements[5] = (a * i - c * g) * inverse_determinant;
		result.elements[6] = (c * d - a * f) * inverse_determinant;
		result.elements[8] = (d * h - e * g) * inverse_determinant;
		result.elements[9] = (b * g - a * h) * inverse_determinant;
		result.elements[10] = (a * e - b * d) * inverse_determinant;
		const Vector3 translation{elements[3], elements[7], elements[11]};
		const Vector3 inverse_translation = result.Transform_Vector(translation) * -1.0f;
		result.elements[3] = inverse_translation.x;
		result.elements[7] = inverse_translation.y;
		result.elements[11] = inverse_translation.z;
		for (const float element : result.elements) {
			if (!std::isfinite(element)) return std::nullopt;
		}
		return result;
	}

	friend constexpr AffineTransform3 Compose(
		const AffineTransform3 &parent, const AffineTransform3 &local) noexcept
	{
		AffineTransform3 result;
		for (std::size_t row = 0; row < 3; ++row) {
			for (std::size_t column = 0; column < 3; ++column) {
				result.elements[row * 4 + column] =
					parent.elements[row * 4] * local.elements[column]
					+ parent.elements[row * 4 + 1] * local.elements[4 + column]
					+ parent.elements[row * 4 + 2] * local.elements[8 + column];
			}
			result.elements[row * 4 + 3] =
				parent.elements[row * 4] * local.elements[3]
				+ parent.elements[row * 4 + 1] * local.elements[7]
				+ parent.elements[row * 4 + 2] * local.elements[11]
				+ parent.elements[row * 4 + 3];
		}
		return result;
	}

	// Applies a rotation before this transform's basis while leaving its world
	// translation unchanged.
	constexpr void Pre_Apply_Rotation(const AffineTransform3 &rotation) noexcept
	{
		const Vector3 translation = Translation();
		*this = Compose(rotation, *this);
		Set_Translation(translation);
	}

	constexpr void Post_Apply_Rotation(const AffineTransform3 &rotation) noexcept
	{
		*this = Compose(*this, rotation);
	}

private:
	// Legacy Matrix3D post-multiplication helpers, same operation order.
	constexpr void Legacy_Translate(Vector3 t) noexcept
	{
		for (std::size_t row = 0; row < 3; ++row) {
			float *r = elements.data() + row * 4;
			r[3] += r[0] * t.x + r[1] * t.y + r[2] * t.z;
		}
	}

	constexpr void Legacy_Rotate_Y(float s, float c) noexcept
	{
		for (std::size_t row = 0; row < 3; ++row) {
			float *r = elements.data() + row * 4;
			const float tmp1 = r[0];
			const float tmp2 = r[2];
			r[0] = c * tmp1 - s * tmp2;
			r[2] = s * tmp1 + c * tmp2;
		}
	}

	constexpr void Legacy_Rotate_Z(float s, float c) noexcept
	{
		for (std::size_t row = 0; row < 3; ++row) {
			float *r = elements.data() + row * 4;
			const float tmp1 = r[0];
			const float tmp2 = r[1];
			r[0] = c * tmp1 + s * tmp2;
			r[1] = -s * tmp1 + c * tmp2;
		}
	}
};
}
