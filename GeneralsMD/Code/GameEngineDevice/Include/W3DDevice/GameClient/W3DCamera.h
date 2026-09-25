#pragma once

#include <array>
#include "W3DDevice/GameClient/W3DRenderObject.h"
import Engine.Core.Math.Vector2;
import Engine.Core.Math.AffineTransform3;
import Engine.Core.Math.AxisAlignedBox3;
import Engine.Core.Math.OrientedBox3;
import Engine.Core.Math.Sphere3;

import Graphics.Scene.Views.CameraState;

class W3DRenderContext;

struct W3DCameraRenderMatrices final {
	std::array<float, 16> view{};
	std::array<float, 16> projection{};
	std::array<float, 16> view_projection{};
	std::array<float, 16> inverse_projection{};
	std::array<float, 16> inverse_view_projection{};
};

class W3DViewport final
{
public:
	W3DViewport() : Min{0.0f, 0.0f}, Max{1.0f, 1.0f} {}
	W3DViewport(const Engine::Math::Vector2 &minimum, const Engine::Math::Vector2 &maximum) : Min(minimum), Max(maximum) {}

	float Width() const { return Max.x - Min.x; }
	float Height() const { return Max.y - Min.y; }

	Engine::Math::Vector2 Min;
	Engine::Math::Vector2 Max;
};

// Game-facing camera adapter. Projection, frustum construction, and point
// conversion live in Graphics::CameraState; this class retains the native
// value types required by existing render-object callers.
class W3DCamera : public W3DRenderObject
{
public:
	enum ProjectionType
	{
		PERSPECTIVE = 0,
		ORTHO
	};

	enum ProjectionResType
	{
		INSIDE_FRUSTUM,
		OUTSIDE_FRUSTUM,
		OUTSIDE_NEAR_CLIP,
		OUTSIDE_FAR_CLIP
	};

	W3DCamera();
	W3DCamera(const W3DCamera &source);
	W3DCamera &operator=(const W3DCamera &source);
	~W3DCamera() override;

	W3DRenderObject *Clone() const override;
	int Class_ID() const override { return W3DRenderObject::CLASSID_CAMERA; }

	void Render(W3DRenderContext &) override {}

	void Set_Transform(const Engine::Math::AffineTransform3 &transform) override;
	void Set_Position(Engine::Math::Vector3 position) override;

	Engine::Math::Vector3 Get_Right_Dir() const;
	Engine::Math::Vector3 Get_Forward_Dir() const;
	Engine::Math::Vector3 Get_Up_Dir() const;

	void Get_Local_Bounding_Sphere(Engine::Math::Sphere3 &sphere) const override;
	void Get_Local_Bounds(Engine::Math::AxisAlignedBox3 &box) const override;

	float Get_Depth() const;

	void Set_Projection_Type(ProjectionType type);
	ProjectionType Get_Projection_Type() const;

	void Set_Clip_Planes(float near_clip, float far_clip);
	void Get_Clip_Planes(float &near_clip, float &far_clip) const;

	void Set_Zbuffer_Range(float near_depth, float far_depth);
	void Get_Zbuffer_Range(float &near_depth, float &far_depth) const;

	void Set_View_Plane(const Engine::Math::Vector2 &minimum, const Engine::Math::Vector2 &maximum);
	void Set_View_Plane(float horizontal_fov, float vertical_fov = -1.0f);
	void Set_Aspect_Ratio(float width_to_height);

	void Get_View_Plane(Engine::Math::Vector2 &minimum, Engine::Math::Vector2 &maximum) const;
	float Get_Horizontal_FOV() const;
	float Get_Vertical_FOV() const;
	float Get_Aspect_Ratio() const;


	ProjectionResType Project(Engine::Math::Vector3 &destination, const Engine::Math::Vector3 &world_point) const;
	ProjectionResType Project_Camera_Space_Point(Engine::Math::Vector3 &destination,
		const Engine::Math::Vector3 &camera_point) const;
	void Un_Project(Engine::Math::Vector3 &destination, const Engine::Math::Vector2 &view_point) const;
	void Transform_To_View_Space(Engine::Math::Vector3 &destination, const Engine::Math::Vector3 &world_point) const;
	void Rotate_To_View_Space(Engine::Math::Vector3 &destination, const Engine::Math::Vector3 &world_vector) const;

	void Set_Viewport(const Engine::Math::Vector2 &minimum, const Engine::Math::Vector2 &maximum);
	void Get_Viewport(Engine::Math::Vector2 &minimum, Engine::Math::Vector2 &maximum) const;
	const W3DViewport &Get_Viewport() const;

	void Set_Depth_Range(float start = 0.0f, float end = 1.0f);
	void Get_Depth_Range(float *start, float *end) const;

	bool Cull_Sphere(const Engine::Math::Sphere3 &sphere) const;
	bool Cull_Sphere_On_Frustum_Sides(const Engine::Math::Sphere3 &sphere) const;
	bool Cull_Box(const Engine::Math::AxisAlignedBox3 &box) const;

	const Graphics::CameraFrustum &Get_Frustum() const noexcept;
	W3DCameraRenderMatrices Build_Render_Matrices() const;
	std::array<float, 16> Build_Object_View_Projection(const Engine::Math::AffineTransform3 &world) const;
	static std::array<float, 16> Build_World_Matrix(const Engine::Math::AffineTransform3 &world);
	const Graphics::CameraFrustum &Get_View_Space_Frustum() const noexcept;
	const Engine::Math::OrientedBox3 &Get_Near_Clip_Bounding_Box() const;

	void Device_To_View_Space(const Engine::Math::Vector2 &device_coordinate, Engine::Math::Vector3 *view_coordinate);
	void Device_To_World_Space(const Engine::Math::Vector2 &device_coordinate, Engine::Math::Vector3 *world_coordinate);
	float Compute_Projected_Sphere_Radius(float distance, float radius);

	void Apply();

private:
	void Invalidate_Native_Cache() noexcept;
	void Synchronize_State_Transform() const noexcept;
	void Update_Native_Cache() const;

	mutable Graphics::CameraState m_state;
	W3DViewport m_viewport;
	mutable bool m_native_cache_valid = false;
	mutable Engine::Math::OrientedBox3 m_near_clip_box;
};
