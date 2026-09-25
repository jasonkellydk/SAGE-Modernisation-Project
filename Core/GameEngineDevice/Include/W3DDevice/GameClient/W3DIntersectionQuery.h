#pragma once

#include <cstddef>

import Engine.Core.Math.AffineTransform3;
import Engine.Core.Math.AxisAlignedBox3;
import Engine.Core.Math.BoundsQueries3;
import Engine.Core.Math.OrientedBox3;

#include "W3DDevice/GameClient/W3DSceneQueryMask.h"

inline Engine::Math::OrientedBox3 Transform_Intersection_Box(
	const Engine::Math::OrientedBox3 &box, const Engine::Math::AffineTransform3 &transform) noexcept
{
	Engine::Math::OrientedBox3 result;
	result.center = transform.Transform_Point(box.center);
	for (std::size_t axis = 0; axis < result.axes.size(); ++axis) {
		const Engine::Math::Vector3 transformed_axis = transform.Transform_Vector(box.axes[axis]);
		const float scale = transformed_axis.Length();
		result.axes[axis] = scale > 0.0f ? transformed_axis / scale : box.axes[axis];
		result.half_extent[axis] = box.half_extent[axis] * scale;
	}
	return result;
}

struct W3DIntersectionQuery
{
	explicit W3DIntersectionQuery(int mask) : CollisionType(mask) {}
	int CollisionType;
};

class W3DBoxIntersectionQuery : public W3DIntersectionQuery
{
public:
	W3DBoxIntersectionQuery(const Engine::Math::AxisAlignedBox3 &box, int mask)
		: W3DIntersectionQuery(mask), Box(box) {}
	bool Cull(const Engine::Math::AxisAlignedBox3 &bounds) const
	{
		return Engine::Math::BoundsAreDisjoint(Box, bounds);
	}
	Engine::Math::AxisAlignedBox3 Box;
};

class W3DOrientedBoxIntersectionQuery : public W3DIntersectionQuery
{
public:
	W3DOrientedBoxIntersectionQuery(const Engine::Math::OrientedBox3 &box, int mask)
		: W3DIntersectionQuery(mask), Box(box), BoundingBox(box.To_Axis_Aligned()) {}
	W3DOrientedBoxIntersectionQuery(const W3DOrientedBoxIntersectionQuery &source)
		: W3DIntersectionQuery(source), Box(source.Box), BoundingBox(source.BoundingBox) {}
	W3DOrientedBoxIntersectionQuery(const W3DOrientedBoxIntersectionQuery &source,
		const Engine::Math::AffineTransform3 &transform)
		: W3DIntersectionQuery(source), Box(Transform_Intersection_Box(source.Box, transform)),
		  BoundingBox(Box.To_Axis_Aligned()) {}
	W3DOrientedBoxIntersectionQuery(const W3DBoxIntersectionQuery &source,
		const Engine::Math::AffineTransform3 &transform)
		: W3DIntersectionQuery(source),
		  Box(Transform_Intersection_Box({source.Box.Center(), source.Box.Extent()}, transform)),
		  BoundingBox(Box.To_Axis_Aligned()) {}
	bool Cull(const Engine::Math::AxisAlignedBox3 &bounds) const
	{
		return Engine::Math::BoundsAreDisjoint(BoundingBox, bounds);
	}
	Engine::Math::OrientedBox3 Box;
	Engine::Math::AxisAlignedBox3 BoundingBox;
};
