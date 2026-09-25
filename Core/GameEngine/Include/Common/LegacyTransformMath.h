/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: LegacyTransformMath.h ////////////////////////////////////////////////////////////////////
// In-place transform operations that reproduce the former WWMath Matrix3D / Vector3 member
// functions operation-for-operation. The game simulation depends on these results being
// bit-identical to the original implementation (including signed zeros, which are visible to
// atan2 and to the object transform CRC), so they must not be replaced by generic composition.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cmath>

import Engine.Core.Math.AffineTransform3;
import Engine.Core.Math.Vector3;

// ------------------------------------------------------------------------------------------------
// Former WWMath::Atan2 (float atan2 evaluated as double).
// ------------------------------------------------------------------------------------------------
inline float Legacy_Atan2(float y, float x)
{
	return static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)));
}

// Former Matrix3D::Get_X_Rotation / Get_Y_Rotation.
inline float Legacy_Get_X_Rotation(const Engine::Math::AffineTransform3 &m)
{
	return Legacy_Atan2(m[2][1], m[1][1]);
}

inline float Legacy_Get_Y_Rotation(const Engine::Math::AffineTransform3 &m)
{
	return Legacy_Atan2(m[0][2], m[2][2]);
}

// ------------------------------------------------------------------------------------------------
// Former Matrix3D::Translate (post-multiplied translation).
// ------------------------------------------------------------------------------------------------
inline void Legacy_Translate(Engine::Math::AffineTransform3 &m, float x, float y, float z)
{
	m[0][3] += (float)(m[0][0]*x + m[0][1]*y + m[0][2]*z);
	m[1][3] += (float)(m[1][0]*x + m[1][1]*y + m[1][2]*z);
	m[2][3] += (float)(m[2][0]*x + m[2][1]*y + m[2][2]*z);
}

// Former Matrix3D::Adjust_Translation.
inline void Legacy_Adjust_Translation(Engine::Math::AffineTransform3 &m, float x, float y, float z)
{
	m[0][3] += x;
	m[1][3] += y;
	m[2][3] += z;
}

// ------------------------------------------------------------------------------------------------
// Former Matrix3D::Rotate_X/Y/Z (post-multiplied rotations).
// ------------------------------------------------------------------------------------------------
inline void Legacy_Rotate_X(Engine::Math::AffineTransform3 &m, float s, float c)
{
	for (int r = 0; r < 3; ++r)
	{
		const float tmp1 = m[r][1];
		const float tmp2 = m[r][2];
		m[r][1] = (float)( c*tmp1 + s*tmp2);
		m[r][2] = (float)(-s*tmp1 + c*tmp2);
	}
}

inline void Legacy_Rotate_Y(Engine::Math::AffineTransform3 &m, float s, float c)
{
	for (int r = 0; r < 3; ++r)
	{
		const float tmp1 = m[r][0];
		const float tmp2 = m[r][2];
		m[r][0] = (float)(c*tmp1 - s*tmp2);
		m[r][2] = (float)(s*tmp1 + c*tmp2);
	}
}

inline void Legacy_Rotate_Z(Engine::Math::AffineTransform3 &m, float s, float c)
{
	for (int r = 0; r < 3; ++r)
	{
		const float tmp1 = m[r][0];
		const float tmp2 = m[r][1];
		m[r][0] = (float)( c*tmp1 + s*tmp2);
		m[r][1] = (float)(-s*tmp1 + c*tmp2);
	}
}

inline void Legacy_Rotate_X(Engine::Math::AffineTransform3 &m, float theta) { Legacy_Rotate_X(m, std::sin(theta), std::cos(theta)); }
inline void Legacy_Rotate_Y(Engine::Math::AffineTransform3 &m, float theta) { Legacy_Rotate_Y(m, std::sin(theta), std::cos(theta)); }
inline void Legacy_Rotate_Z(Engine::Math::AffineTransform3 &m, float theta) { Legacy_Rotate_Z(m, std::sin(theta), std::cos(theta)); }

// ------------------------------------------------------------------------------------------------
// Former Matrix3D::In_Place_Pre_Rotate_X/Y/Z (pre-multiplied rotation of the 3x3 part only).
// ------------------------------------------------------------------------------------------------
inline void Legacy_In_Place_Pre_Rotate_X(Engine::Math::AffineTransform3 &m, float theta)
{
	const float c = std::cos(theta);
	const float s = std::sin(theta);
	for (int col = 0; col < 3; ++col)
	{
		const float tmp1 = m[1][col];
		const float tmp2 = m[2][col];
		m[1][col] = (float)(c*tmp1 - s*tmp2);
		m[2][col] = (float)(s*tmp1 + c*tmp2);
	}
}

inline void Legacy_In_Place_Pre_Rotate_Y(Engine::Math::AffineTransform3 &m, float theta)
{
	const float c = std::cos(theta);
	const float s = std::sin(theta);
	for (int col = 0; col < 3; ++col)
	{
		const float tmp1 = m[0][col];
		const float tmp2 = m[2][col];
		m[0][col] = (float)( c*tmp1 + s*tmp2);
		m[2][col] = (float)(-s*tmp1 + c*tmp2);
	}
}

inline void Legacy_In_Place_Pre_Rotate_Z(Engine::Math::AffineTransform3 &m, float theta)
{
	const float c = std::cos(theta);
	const float s = std::sin(theta);
	for (int col = 0; col < 3; ++col)
	{
		const float tmp1 = m[0][col];
		const float tmp2 = m[1][col];
		m[0][col] = (float)(c*tmp1 - s*tmp2);
		m[1][col] = (float)(s*tmp1 + c*tmp2);
	}
}

// ------------------------------------------------------------------------------------------------
// Former Matrix3D::Scale (scales the 3x3 part column-wise).
// ------------------------------------------------------------------------------------------------
inline void Legacy_Scale(Engine::Math::AffineTransform3 &m, float x, float y, float z)
{
	for (int r = 0; r < 3; ++r)
	{
		m[r][0] *= x;
		m[r][1] *= y;
		m[r][2] *= z;
	}
}

inline void Legacy_Scale(Engine::Math::AffineTransform3 &m, float scale)
{
	Legacy_Scale(m, scale, scale, scale);
}

// ------------------------------------------------------------------------------------------------
// Former Vector3::Rotate_X/Y/Z.
// ------------------------------------------------------------------------------------------------
inline Engine::Math::Vector3 Legacy_Vector_Rotate_X(Engine::Math::Vector3 v, float angle)
{
	const float s = std::sin(angle);
	const float c = std::cos(angle);
	const float tmp_y = v.y;
	const float tmp_z = v.z;
	v.y = c * tmp_y - s * tmp_z;
	v.z = s * tmp_y + c * tmp_z;
	return v;
}

inline Engine::Math::Vector3 Legacy_Vector_Rotate_Y(Engine::Math::Vector3 v, float angle)
{
	const float s = std::sin(angle);
	const float c = std::cos(angle);
	const float tmp_x = v.x;
	const float tmp_z = v.z;
	v.x = c * tmp_x + s * tmp_z;
	v.z = -s * tmp_x + c * tmp_z;
	return v;
}

inline Engine::Math::Vector3 Legacy_Vector_Rotate_Z(Engine::Math::Vector3 v, float angle)
{
	const float s = std::sin(angle);
	const float c = std::cos(angle);
	const float tmp_x = v.x;
	const float tmp_y = v.y;
	v.x = c * tmp_x - s * tmp_y;
	v.y = s * tmp_x + c * tmp_y;
	return v;
}
