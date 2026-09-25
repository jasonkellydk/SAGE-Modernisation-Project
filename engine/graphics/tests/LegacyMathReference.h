#pragma once

// Test-only, standalone ports of the retired WWMath routines (Matrix3D,
// Quaternion, WWMath fast tables). They exist solely so graphics tests can keep
// comparing the Engine/Graphics implementations bit-for-bit against the
// original algorithms without linking the deleted WWMath library.
//
// Every routine keeps the original operation order and intermediate types
// (float vs. double) of the non-x86-assembly WWMath code paths. Do not
// "simplify" these functions: their value is in being a literal port.
//
// Sources (at the commit that removed WWMath):
//   WWMath/matrix3d.h / matrix3d.cpp, WWMath/quat.h / quat.cpp,
//   WWMath/wwmath.h / wwmath.cpp, WWMath/vector3.h

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace LegacyMathReference
{

constexpr float Pi = 3.141592654f;              // WWMATH_PI
constexpr float Epsilon = 0.0001f;              // WWMATH_EPSILON
constexpr double Slerp_Epsilon = 0.001;         // SLERP_EPSILON (a double literal)
constexpr int Arc_Table_Size = 1024;            // ARC_TABLE_SIZE
constexpr int Sin_Table_Size = 1024;            // SIN_TABLE_SIZE

// ---------------------------------------------------------------------------
// WWMath scalar helpers
// ---------------------------------------------------------------------------

// WWMath::Sqrt (non-x86 path): (float)sqrt(val).
inline float Sqrt(float value) noexcept
{
	return static_cast<float>(std::sqrt(value));
}

// WWMath::Inv_Sqrt (non-x86 path): 1.0f / (float)sqrt(val).
inline float Inv_Sqrt(float value) noexcept
{
	return 1.0f / static_cast<float>(std::sqrt(value));
}

// WWMath::Fabs: clears the sign bit.
inline float Fabs(float value) noexcept
{
	return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7fffffffu);
}

// WWMath::Float_To_Int_Floor: integer bit-trick floor. Shift counts are
// masked to five bits, which is what the x86/x64 shift instructions did with
// the original (formally undefined) out-of-range shifts.
inline int Float_To_Int_Floor(float value) noexcept
{
	int a = std::bit_cast<int>(value);
	const int sign = a >> 31;
	a &= 0x7fffffff;

	const int exponent = (a >> 23) - 127;
	const int expsign = ~(exponent >> 31);
	const unsigned shift = static_cast<unsigned>(31 - exponent) & 31u;
	const int imask = static_cast<int>((1u << shift) - 1u);
	const int mantissa = a & ((1 << 23) - 1);
	int r = static_cast<int>((static_cast<unsigned>(mantissa | (1 << 23)) << 8) >> shift);

	r = ((r & expsign) ^ sign)
		+ ((static_cast<int>(!((mantissa << 8) & imask)) & (expsign ^ ((a - 1) >> 31))) & sign);
	return r;
}

// Lookup tables filled with WWMath::Init's formulas. The table entries are
// computed in double precision, as the retained production tables are.
struct FastTables final
{
	std::array<float, Arc_Table_Size> acos{};
	std::array<float, Sin_Table_Size> sin{};

	FastTables() noexcept
	{
		for (int index = 0; index < Arc_Table_Size; ++index) {
			const float cv = float(index - Arc_Table_Size / 2) * (1.0f / (Arc_Table_Size / 2));
			acos[index] = static_cast<float>(std::acos(static_cast<double>(cv)));
		}
		for (int index = 0; index < Sin_Table_Size; ++index) {
			const float cv = static_cast<float>(index) * 2.0f * Pi / Sin_Table_Size;
			sin[index] = static_cast<float>(std::sin(static_cast<double>(cv)));
		}
	}
};

inline const FastTables &Fast_Tables() noexcept
{
	static const FastTables tables;
	return tables;
}

// WWMath::Fast_Sin
inline float Fast_Sin(float value) noexcept
{
	value *= float(Sin_Table_Size) / (2.0f * Pi);

	int idx0 = Float_To_Int_Floor(value);
	int idx1 = idx0 + 1;
	const float frac = value - static_cast<float>(idx0);

	idx0 = static_cast<int>(static_cast<unsigned>(idx0) & (Sin_Table_Size - 1));
	idx1 = static_cast<int>(static_cast<unsigned>(idx1) & (Sin_Table_Size - 1));

	const auto &table = Fast_Tables().sin;
	return (1.0f - frac) * table[idx0] + frac * table[idx1];
}

// WWMath::Fast_Acos (table based; falls back to acos near +/-1).
inline float Fast_Acos(float value) noexcept
{
	if (Fabs(value) > 0.975f)
		return static_cast<float>(std::acos(static_cast<double>(value)));

	value *= float(Arc_Table_Size / 2);

	int idx0 = Float_To_Int_Floor(value);
	int idx1 = idx0 + 1;
	const float frac = value - static_cast<float>(idx0);

	idx0 += Arc_Table_Size / 2;
	idx1 += Arc_Table_Size / 2;

	const auto &table = Fast_Tables().acos;
	return (1.0f - frac) * table[idx0] + frac * table[idx1];
}

// ---------------------------------------------------------------------------
// Vector3 (WWMath/vector3.h)
// ---------------------------------------------------------------------------

struct Vector3 final
{
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;

	float Length2() const noexcept { return X * X + Y * Y + Z * Z; }
	float Length() const noexcept { return Sqrt(Length2()); }

	// Vector3::Normalize
	void Normalize() noexcept
	{
		const float len2 = Length2();
		if (len2 != 0.0f) {
			const float oolen = Inv_Sqrt(len2);
			X *= oolen;
			Y *= oolen;
			Z *= oolen;
		}
	}
};

inline Vector3 operator-(const Vector3 &a, const Vector3 &b) noexcept
{
	return {a.X - b.X, a.Y - b.Y, a.Z - b.Z};
}

inline Vector3 operator+(const Vector3 &a, const Vector3 &b) noexcept
{
	return {a.X + b.X, a.Y + b.Y, a.Z + b.Z};
}

inline Vector3 operator*(const Vector3 &a, float k) noexcept
{
	return {a.X * k, a.Y * k, a.Z * k};
}

inline float Dot_Product(const Vector3 &a, const Vector3 &b) noexcept
{
	return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
}

inline Vector3 Cross_Product(const Vector3 &a, const Vector3 &b) noexcept
{
	return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
}

// ---------------------------------------------------------------------------
// Quaternion (WWMath/quat.h / quat.cpp)
// ---------------------------------------------------------------------------

struct Quaternion final
{
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
	float W = 1.0f;

	float operator[](int index) const noexcept
	{
		return index == 0 ? X : index == 1 ? Y : index == 2 ? Z : W;
	}

	// Quaternion::Rotate_Vector
	Vector3 Rotate_Vector(const Vector3 &v) const noexcept
	{
		const float x = W * v.X + (Y * v.Z - v.Y * Z);
		const float y = W * v.Y - (X * v.Z - v.X * Z);
		const float z = W * v.Z + (X * v.Y - v.X * Y);
		const float w = -(X * v.X + Y * v.Y + Z * v.Z);

		return {
			w * (-X) + W * x + (y * (-Z) - (-Y) * z),
			w * (-Y) + W * y - (x * (-Z) - (-X) * z),
			w * (-Z) + W * z + (x * (-Y) - (-X) * y)};
	}
};

// Fast_Slerp (portable C path of quat.cpp; the x86 asm variant was #if 0'd).
inline Quaternion Fast_Slerp(const Quaternion &p, const Quaternion &q, float alpha) noexcept
{
	float beta;
	float cos_t = p.X * q.X + p.Y * q.Y + p.Z * q.Z + p.W * q.W;
	bool qflip;

	if (cos_t < 0.0f) {
		cos_t = -cos_t;
		qflip = true;
	} else {
		qflip = false;
	}

	if (1.0f - cos_t < Epsilon * Epsilon) {
		beta = 1.0f - alpha;
	} else {
		const float theta = Fast_Acos(cos_t);
		const float sin_t = Fast_Sin(theta);
		const float oo_sin_t = 1.0f / sin_t;
		beta = Fast_Sin(theta - alpha * theta) * oo_sin_t;
		alpha = Fast_Sin(alpha * theta) * oo_sin_t;
	}

	if (qflip)
		alpha = -alpha;

	return {
		beta * p.X + alpha * q.X,
		beta * p.Y + alpha * q.Y,
		beta * p.Z + alpha * q.Z,
		beta * p.W + alpha * q.W};
}

// SlerpInfoStruct / Slerp_Setup / Cached_Slerp. WWMath::Acos and WWMath::Sin
// (non-x86) were float acos/sinf. Note the original divides by Theta, not by
// sin(Theta); that quirk is part of the retained contract.
struct SlerpInfo final
{
	float SinT = 0.0f;
	float Theta = 0.0f;
	bool Flip = false;
	bool Linear = true;
};

inline SlerpInfo Slerp_Setup(const Quaternion &p, const Quaternion &q) noexcept
{
	SlerpInfo info;
	float cos_t = p.X * q.X + p.Y * q.Y + p.Z * q.Z + p.W * q.W;

	if (cos_t < 0.0f) {
		cos_t = -cos_t;
		info.Flip = true;
	} else {
		info.Flip = false;
	}

	if (1.0f - cos_t < Slerp_Epsilon) {
		info.Linear = true;
		info.Theta = 0.0f;
		info.SinT = 0.0f;
	} else {
		info.Linear = false;
		info.Theta = static_cast<float>(std::acos(cos_t));
		info.SinT = std::sin(info.Theta);
	}
	return info;
}

inline Quaternion Cached_Slerp(const Quaternion &p, const Quaternion &q, float alpha, const SlerpInfo &info) noexcept
{
	float beta;
	if (info.Linear) {
		beta = 1.0f - alpha;
	} else {
		const float oo_sin_t = 1.0f / info.Theta;
		beta = std::sin(info.Theta - alpha * info.Theta) * oo_sin_t;
		alpha = std::sin(alpha * info.Theta) * oo_sin_t;
	}

	if (info.Flip)
		alpha = -alpha;

	return {
		beta * p.X + alpha * q.X,
		beta * p.Y + alpha * q.Y,
		beta * p.Z + alpha * q.Z,
		beta * p.W + alpha * q.W};
}

// ---------------------------------------------------------------------------
// Matrix3D (WWMath/matrix3d.h / matrix3d.cpp), row-major 3x4.
// ---------------------------------------------------------------------------

struct Matrix3D final
{
	float Row[3][4]{
		{1.0f, 0.0f, 0.0f, 0.0f},
		{0.0f, 1.0f, 0.0f, 0.0f},
		{0.0f, 0.0f, 1.0f, 0.0f}};

	static Matrix3D Identity() noexcept { return {}; }

	// Matrix3D::RotateZ90
	static Matrix3D Rotate_Z90() noexcept
	{
		Matrix3D result;
		result.Row[0][0] = 0.0f; result.Row[0][1] = -1.0f;
		result.Row[1][0] = 1.0f; result.Row[1][1] = 0.0f;
		return result;
	}

	// Matrix3D(const Vector3 &translation)
	static Matrix3D From_Translation(const Vector3 &t) noexcept
	{
		Matrix3D result;
		result.Set_Translation(t);
		return result;
	}

	float *operator[](std::size_t row) noexcept { return Row[row]; }
	const float *operator[](std::size_t row) const noexcept { return Row[row]; }

	void Make_Identity() noexcept { *this = Matrix3D{}; }

	Vector3 Get_Translation() const noexcept { return {Row[0][3], Row[1][3], Row[2][3]}; }
	void Set_Translation(const Vector3 &t) noexcept { Row[0][3] = t.X; Row[1][3] = t.Y; Row[2][3] = t.Z; }
	void Adjust_Translation(const Vector3 &t) noexcept { Row[0][3] += t.X; Row[1][3] += t.Y; Row[2][3] += t.Z; }

	// Matrix3D::Translate(const Vector3 &)
	void Translate(const Vector3 &t) noexcept
	{
		Row[0][3] += Row[0][0] * t.X + Row[0][1] * t.Y + Row[0][2] * t.Z;
		Row[1][3] += Row[1][0] * t.X + Row[1][1] * t.Y + Row[1][2] * t.Z;
		Row[2][3] += Row[2][0] * t.X + Row[2][1] * t.Y + Row[2][2] * t.Z;
	}

	void Rotate_X(float s, float c) noexcept
	{
		for (int row = 0; row < 3; ++row) {
			const float tmp1 = Row[row][1];
			const float tmp2 = Row[row][2];
			Row[row][1] = static_cast<float>(c * tmp1 + s * tmp2);
			Row[row][2] = static_cast<float>(-s * tmp1 + c * tmp2);
		}
	}

	void Rotate_Y(float s, float c) noexcept
	{
		for (int row = 0; row < 3; ++row) {
			const float tmp1 = Row[row][0];
			const float tmp2 = Row[row][2];
			Row[row][0] = static_cast<float>(c * tmp1 - s * tmp2);
			Row[row][2] = static_cast<float>(s * tmp1 + c * tmp2);
		}
	}

	void Rotate_Z(float s, float c) noexcept
	{
		for (int row = 0; row < 3; ++row) {
			const float tmp1 = Row[row][0];
			const float tmp2 = Row[row][1];
			Row[row][0] = static_cast<float>(c * tmp1 + s * tmp2);
			Row[row][1] = static_cast<float>(-s * tmp1 + c * tmp2);
		}
	}

	// Matrix3D::Rotate_Z(float theta): cosf / sinf.
	void Rotate_Z(float theta) noexcept
	{
		const float c = std::cos(theta);
		const float s = std::sin(theta);
		Rotate_Z(s, c);
	}

	// Matrix3D::Look_At_Dir
	void Look_At_Dir(const Vector3 &pos, const Vector3 &dir, float roll) noexcept
	{
		float sinp, cosp;
		float siny, cosy;

		const float dx = dir.X;
		const float dy = dir.Y;
		const float dz = dir.Z;

		const float len2 = Sqrt(dx * dx + dy * dy);

		sinp = dz;
		cosp = len2;

		if (len2 != 0.0f) {
			siny = dy / len2;
			cosy = dx / len2;
		} else {
			siny = 0.0f;
			cosy = 1.0f;
		}

		Row[0][0] = 0.0f;  Row[0][1] = 0.0f; Row[0][2] = -1.0f;
		Row[1][0] = -1.0f; Row[1][1] = 0.0f; Row[1][2] = 0.0f;
		Row[2][0] = 0.0f;  Row[2][1] = 1.0f; Row[2][2] = 0.0f;

		Row[0][3] = pos.X;
		Row[1][3] = pos.Y;
		Row[2][3] = pos.Z;

		Rotate_Y(siny, cosy);
		Rotate_X(sinp, cosp);
		Rotate_Z(-roll);
	}

	// Matrix3D::Look_At
	void Look_At(const Vector3 &p, const Vector3 &t, float roll) noexcept
	{
		Vector3 dir = t - p;
		dir.Normalize();
		Look_At_Dir(p, dir, roll);
	}

	// "this = A * B" (Matrix3D::mul). Safe when this aliases B, as the
	// original was (columns of B are cached before being overwritten).
	void mul(const Matrix3D &A, const Matrix3D &B) noexcept
	{
		const auto submul = [](const float *row, float tmp1, float tmp2, float tmp3) {
			return row[0] * tmp1 + row[1] * tmp2 + row[2] * tmp3;
		};
		for (int column = 0; column < 3; ++column) {
			const float tmp1 = B.Row[0][column];
			const float tmp2 = B.Row[1][column];
			const float tmp3 = B.Row[2][column];
			Row[0][column] = submul(A.Row[0], tmp1, tmp2, tmp3);
			Row[1][column] = submul(A.Row[1], tmp1, tmp2, tmp3);
			Row[2][column] = submul(A.Row[2], tmp1, tmp2, tmp3);
		}
		const float tmp1 = B.Row[0][3];
		const float tmp2 = B.Row[1][3];
		const float tmp3 = B.Row[2][3];
		Row[0][3] = submul(A.Row[0], tmp1, tmp2, tmp3) + A.Row[0][3];
		Row[1][3] = submul(A.Row[1], tmp1, tmp2, tmp3) + A.Row[1][3];
		Row[2][3] = submul(A.Row[2], tmp1, tmp2, tmp3) + A.Row[2][3];
	}

	// "this = this * that" (Matrix3D::postMul, AVOID_TEMP_IN_POSTMUL path).
	void postMul(const Matrix3D &that) noexcept
	{
		for (int row = 0; row < 3; ++row) {
			const float *r = Row[row];
			const float tmpX = r[0] * that.Row[0][0] + r[1] * that.Row[1][0] + r[2] * that.Row[2][0];
			const float tmpY = r[0] * that.Row[0][1] + r[1] * that.Row[1][1] + r[2] * that.Row[2][1];
			const float tmpZ = r[0] * that.Row[0][2] + r[1] * that.Row[1][2] + r[2] * that.Row[2][2];
			const float tmpW = r[0] * that.Row[0][3] + r[1] * that.Row[1][3] + r[2] * that.Row[2][3];
			Row[row][0] = tmpX;
			Row[row][1] = tmpY;
			Row[row][2] = tmpZ;
			Row[row][3] += tmpW;
		}
	}

	// Matrix3D::Transform_Vector (full affine point transform).
	static Vector3 Transform_Vector(const Matrix3D &A, const Vector3 &v) noexcept
	{
		return {
			(A[0][0] * v.X + A[0][1] * v.Y + A[0][2] * v.Z + A[0][3]),
			(A[1][0] * v.X + A[1][1] * v.Y + A[1][2] * v.Z + A[1][3]),
			(A[2][0] * v.X + A[2][1] * v.Y + A[2][2] * v.Z + A[2][3])};
	}

	// Matrix3D::Is_Orthogonal
	int Is_Orthogonal() const noexcept
	{
		const Vector3 x{Row[0][0], Row[0][1], Row[0][2]};
		const Vector3 y{Row[1][0], Row[1][1], Row[1][2]};
		const Vector3 z{Row[2][0], Row[2][1], Row[2][2]};

		if (Dot_Product(x, y) > Epsilon) return 0;
		if (Dot_Product(y, z) > Epsilon) return 0;
		if (Dot_Product(z, x) > Epsilon) return 0;

		if (Fabs(x.Length2() - 1.0f) > Epsilon) return 0;
		if (Fabs(y.Length2() - 1.0f) > Epsilon) return 0;
		if (Fabs(z.Length2() - 1.0f) > Epsilon) return 0;

		return 1;
	}

	// Matrix3D::Re_Orthogonalize
	void Re_Orthogonalize() noexcept
	{
		Vector3 x{Row[0][0], Row[0][1], Row[0][2]};
		Vector3 y{Row[1][0], Row[1][1], Row[1][2]};
		Vector3 z = Cross_Product(x, y);
		y = Cross_Product(z, x);

		float len = x.Length();
		if (len < Epsilon) { Make_Identity(); return; }
		x = x * (1.0f / len);

		len = y.Length();
		if (len < Epsilon) { Make_Identity(); return; }
		y = y * (1.0f / len);

		len = z.Length();
		if (len < Epsilon) { Make_Identity(); return; }
		z = z * (1.0f / len);

		Row[0][0] = x.X; Row[0][1] = x.Y; Row[0][2] = x.Z;
		Row[1][0] = y.X; Row[1][1] = y.Y; Row[1][2] = y.Z;
		Row[2][0] = z.X; Row[2][1] = z.Y; Row[2][2] = z.Z;
	}
};

// Build_Matrix3D(const Quaternion &, Matrix3D &): note the mixed float/double
// intermediates of the original, including the "2.0f" on the [1][1] term.
inline Matrix3D Build_Matrix3D(const Quaternion &q) noexcept
{
	Matrix3D out;
	out[0][0] = (float)(1.0 - 2.0 * (q[1] * q[1] + q[2] * q[2]));
	out[0][1] = (float)(2.0 * (q[0] * q[1] - q[2] * q[3]));
	out[0][2] = (float)(2.0 * (q[2] * q[0] + q[1] * q[3]));

	out[1][0] = (float)(2.0 * (q[0] * q[1] + q[2] * q[3]));
	out[1][1] = (float)(1.0 - 2.0f * (q[2] * q[2] + q[0] * q[0]));
	out[1][2] = (float)(2.0 * (q[1] * q[2] - q[0] * q[3]));

	out[2][0] = (float)(2.0 * (q[2] * q[0] - q[1] * q[3]));
	out[2][1] = (float)(2.0 * (q[1] * q[2] + q[0] * q[3]));
	out[2][2] = (float)(1.0 - 2.0 * (q[1] * q[1] + q[0] * q[0]));

	out[0][3] = out[1][3] = out[2][3] = 0.0f;
	return out;
}

} // namespace LegacyMathReference
