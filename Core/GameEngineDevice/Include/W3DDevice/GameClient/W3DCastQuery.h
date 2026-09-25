#pragma once

#include <cstddef>

import Engine.Core.Math.AffineTransform3;
import Engine.Core.Math.AxisAlignedBox3;
import Engine.Core.Math.BoundsQueries3;
import Engine.Core.Math.CollisionResult3;
import Engine.Core.Math.LineSegment3;
import Engine.Core.Math.OrientedBox3;

#include "W3DDevice/GameClient/W3DSceneQueryMask.h"

class W3DRenderObject;

struct W3DCastQuery
{
	W3DCastQuery(Engine::Math::CollisionResult3 *result, int mask) : Result(result), CollisionType(mask) {}
	W3DCastQuery(const W3DCastQuery &) = default;
	Engine::Math::CollisionResult3 *Result;
	int CollisionType;
	W3DRenderObject *CollidedRenderObj = nullptr;
};

inline Engine::Math::OrientedBox3 Transform_Query_Box(
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

inline Engine::Math::AxisAlignedBox3 Swept_Query_Bounds(
	const Engine::Math::OrientedBox3 &box, Engine::Math::Vector3 movement) noexcept
{
	const auto bounds = box.To_Axis_Aligned();
	const Engine::Math::Vector3 padding{0.01f, 0.01f, 0.01f};
	return Engine::Math::SweptBounds(bounds.Center(), bounds.Extent() + padding, movement);
}

class W3DRayCastQuery : public W3DCastQuery
{
public:
	W3DRayCastQuery(const Engine::Math::LineSegment3 &ray, Engine::Math::CollisionResult3 *result,
		int mask = SCENE_QUERY_0, bool translucent = false, bool hidden = false)
		: W3DCastQuery(result, mask), Ray(ray), CheckTranslucent(translucent), CheckHidden(hidden) {}
	W3DRayCastQuery(const W3DRayCastQuery &source, const Engine::Math::AffineTransform3 &transform)
		: W3DCastQuery(source),
		  Ray{transform.Transform_Point(source.Ray.start), transform.Transform_Point(source.Ray.end)},
		  CheckTranslucent(source.CheckTranslucent), CheckHidden(source.CheckHidden) {}
	W3DRayCastQuery(const W3DRayCastQuery &) = delete;
	W3DRayCastQuery &operator=(const W3DRayCastQuery &) = delete;
	bool Cull(const Engine::Math::AxisAlignedBox3 &bounds) const
	{
		return !bounds.Intersects_Segment(Ray.start, Ray.end);
	}
	Engine::Math::LineSegment3 Ray;
	bool CheckTranslucent;
	bool CheckHidden;
};

class W3DBoxCastQuery : public W3DCastQuery
{
public:
	enum ROTATION_TYPE { ROTATE_NONE, ROTATE_Z90, ROTATE_Z180, ROTATE_Z270 };
	W3DBoxCastQuery(const Engine::Math::AxisAlignedBox3 &box, Engine::Math::Vector3 movement,
		Engine::Math::CollisionResult3 *result, int mask = SCENE_QUERY_0)
		: W3DCastQuery(result, mask), Box(box), Move(movement), Sweep_Bounds(Engine::Math::SweptBounds(
			box.Center(), box.Extent(), movement)) {}
	W3DBoxCastQuery(const W3DBoxCastQuery &) = default;
	W3DBoxCastQuery &operator=(const W3DBoxCastQuery &) = delete;
	bool Cull(const Engine::Math::AxisAlignedBox3 &bounds) const
	{
		return Engine::Math::BoundsAreDisjoint(Sweep_Bounds, bounds);
	}
	void Translate(Engine::Math::Vector3 translation)
	{
		Box.minimum = Box.minimum + translation;
		Box.maximum = Box.maximum + translation;
		Sweep_Bounds.minimum = Sweep_Bounds.minimum + translation;
		Sweep_Bounds.maximum = Sweep_Bounds.maximum + translation;
	}
	void Rotate(ROTATION_TYPE rotation)
	{
		const auto turns = static_cast<unsigned>(rotation);
		Box = Engine::Math::RotateBoundsZQuarterTurns(Box, turns);
		Move = Engine::Math::RotateZQuarterTurns(Move, turns);
		Sweep_Bounds = Engine::Math::RotateBoundsZQuarterTurns(Sweep_Bounds, turns);
	}
	void Transform(const Engine::Math::AffineTransform3 &transform)
	{
		Box = Engine::Math::TransformBoundsCorners(Box, transform.elements);
		Move = transform.Transform_Vector(Move);
		Sweep_Bounds = Engine::Math::TransformBoundsCorners(Sweep_Bounds, transform.elements);
	}
	Engine::Math::AxisAlignedBox3 Box;
	Engine::Math::Vector3 Move;
	Engine::Math::AxisAlignedBox3 Sweep_Bounds;
};

class W3DOrientedBoxCastQuery : public W3DCastQuery
{
public:
	W3DOrientedBoxCastQuery(const Engine::Math::OrientedBox3 &box, Engine::Math::Vector3 movement,
		Engine::Math::CollisionResult3 *result, int mask = SCENE_QUERY_0)
		: W3DCastQuery(result, mask), Box(box), Move(movement), Sweep_Bounds(Swept_Query_Bounds(box, movement)) {}
	W3DOrientedBoxCastQuery(const W3DOrientedBoxCastQuery &) = default;
	W3DOrientedBoxCastQuery &operator=(const W3DOrientedBoxCastQuery &) = delete;
	W3DOrientedBoxCastQuery(const W3DOrientedBoxCastQuery &source,
		const Engine::Math::AffineTransform3 &transform)
		: W3DCastQuery(source), Box(Transform_Query_Box(source.Box, transform)),
		  Move(transform.Transform_Vector(source.Move)),
		  Sweep_Bounds(Engine::Math::TransformBoundsCorners(source.Sweep_Bounds, transform.elements)) {}
	W3DOrientedBoxCastQuery(const W3DBoxCastQuery &source,
		const Engine::Math::AffineTransform3 &transform)
		: W3DCastQuery(source),
		  Box(Transform_Query_Box({source.Box.Center(), source.Box.Extent()}, transform)),
		  Move(transform.Transform_Vector(source.Move)),
		  Sweep_Bounds(Engine::Math::TransformBoundsCorners(source.Sweep_Bounds, transform.elements)) {}
	bool Cull(const Engine::Math::AxisAlignedBox3 &bounds) const
	{
		return Engine::Math::BoundsAreDisjoint(Sweep_Bounds, bounds);
	}
	Engine::Math::OrientedBox3 Box;
	Engine::Math::Vector3 Move;
	Engine::Math::AxisAlignedBox3 Sweep_Bounds;
};
