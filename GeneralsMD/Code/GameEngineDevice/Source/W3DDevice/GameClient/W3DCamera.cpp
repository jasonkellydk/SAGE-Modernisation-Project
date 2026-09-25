#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "W3DDevice/GameClient/W3DCamera.h"
import Engine.Core.Math.Matrix4;

import Graphics.Frame.AttachmentBindings;

namespace
{

Graphics::Vector3 To_Graphics(const Engine::Math::Vector3 &value) noexcept
{
	return {value.x, value.y, value.z};
}

Graphics::RenderTransform To_Graphics(const Engine::Math::AffineTransform3 &value) noexcept
{
	auto result = Graphics::Affine_Identity();
	for (unsigned row = 0; row < 3; ++row)
		for (unsigned column = 0; column < 4; ++column)
			result.matrix[row * 4 + column] = value.elements[row * 4 + column];
	return result;
}

Engine::Math::Vector3 To_Native(const Graphics::Vector3 &value) noexcept
{
	return {value.x, value.y, value.z};
}

Graphics::CameraProjectionType To_Graphics(W3DCamera::ProjectionType type) noexcept
{
	return type == W3DCamera::ORTHO
		? Graphics::CameraProjectionType::Ortho
		: Graphics::CameraProjectionType::Perspective;
}

W3DCamera::ProjectionType To_Native(Graphics::CameraProjectionType type) noexcept
{
	return type == Graphics::CameraProjectionType::Ortho
		? W3DCamera::ORTHO
		: W3DCamera::PERSPECTIVE;
}

W3DCamera::ProjectionResType To_Native(Graphics::CameraProjectionResult result) noexcept
{
	switch (result) {
	case Graphics::CameraProjectionResult::OutsideFrustum:
		return W3DCamera::OUTSIDE_FRUSTUM;
	case Graphics::CameraProjectionResult::OutsideNearClip:
		return W3DCamera::OUTSIDE_NEAR_CLIP;
	case Graphics::CameraProjectionResult::OutsideFarClip:
		return W3DCamera::OUTSIDE_FAR_CLIP;
	case Graphics::CameraProjectionResult::InsideFrustum:
	default:
		return W3DCamera::INSIDE_FRUSTUM;
	}
}

void Set_Native_Near_Clip_Box(Engine::Math::OrientedBox3 &destination,
	const Graphics::CameraOrientedBox &source) noexcept
{
	destination.center = {source.center.x, source.center.y, source.center.z};
	destination.half_extent = {source.extent.x, source.extent.y, source.extent.z};
	destination.axes = {{
		{source.basis[0], source.basis[3], source.basis[6]},
		{source.basis[1], source.basis[4], source.basis[7]},
		{source.basis[2], source.basis[5], source.basis[8]}}};
}

}

W3DCamera::W3DCamera()
{
	Set_Transform(Engine::Math::AffineTransform3::Identity());
}

W3DCamera::W3DCamera(const W3DCamera &source)
	: W3DRenderObject(source), m_state(source.m_state), m_viewport(source.m_viewport)
{
}

W3DCamera &W3DCamera::operator=(const W3DCamera &source)
{
	if (this != &source) {
		const Graphics::RenderTransform target_transform =
			Graphics::Import_Affine_Transform(Get_Transform());
		const float target_aspect_ratio = m_state.Get_Aspect_Ratio();
		const float target_depth_min = m_state.Get_Depth_Min();
		const float target_depth_max = m_state.Get_Depth_Max();
		W3DRenderObject::operator=(source);
		m_state = source.m_state;
		m_viewport = source.m_viewport;
		m_state.Set_Transform(target_transform);
		m_state.Set_Aspect_Ratio_Value(target_aspect_ratio);
		m_state.Set_Depth_Range(target_depth_min, target_depth_max);
		Invalidate_Native_Cache();
	}
	return *this;
}

W3DCamera::~W3DCamera() = default;

W3DRenderObject *W3DCamera::Clone() const
{
	return NEW_REF(W3DCamera, (*this));
}

void W3DCamera::Set_Transform(const Engine::Math::AffineTransform3 &transform)
{
	W3DRenderObject::Set_Transform(transform);
	m_state.Set_Transform(To_Graphics(transform));
	Invalidate_Native_Cache();
}

void W3DCamera::Set_Position(Engine::Math::Vector3 position)
{
	W3DRenderObject::Set_Position(position);
	m_state.Set_Transform(To_Graphics(Get_Transform()));
	Invalidate_Native_Cache();
}

Engine::Math::Vector3 W3DCamera::Get_Right_Dir() const
{
	const auto direction = Get_Transform().Basis_X();
	return {direction.x, direction.y, direction.z};
}

Engine::Math::Vector3 W3DCamera::Get_Forward_Dir() const
{
	const auto direction = Get_Transform().Basis_Z() * -1.0f;
	return {direction.x, direction.y, direction.z};
}

Engine::Math::Vector3 W3DCamera::Get_Up_Dir() const
{
	const auto direction = Get_Transform().Basis_Y();
	return {direction.x, direction.y, direction.z};
}

void W3DCamera::Get_Local_Bounding_Sphere(Engine::Math::Sphere3 &sphere) const
{
	sphere = {{0.0f, 0.0f, 0.0f}, Get_Depth()};
}

void W3DCamera::Get_Local_Bounds(Engine::Math::AxisAlignedBox3 &box) const
{
	const float depth = Get_Depth();
	box = {{-depth, -depth, -depth}, {depth, depth, depth}};
}

float W3DCamera::Get_Depth() const
{
	return m_state.Get_Depth();
}

void W3DCamera::Set_Projection_Type(ProjectionType type)
{
	m_state.Set_Projection_Type(To_Graphics(type));
	Invalidate_Native_Cache();
}

W3DCamera::ProjectionType W3DCamera::Get_Projection_Type() const
{
	return To_Native(m_state.Get_Projection_Type());
}

void W3DCamera::Set_Clip_Planes(float near_clip, float far_clip)
{
	m_state.Set_Clip_Planes(near_clip, far_clip);
	Invalidate_Native_Cache();
}

void W3DCamera::Get_Clip_Planes(float &near_clip, float &far_clip) const
{
	m_state.Get_Clip_Planes(near_clip, far_clip);
}

void W3DCamera::Set_Zbuffer_Range(float near_depth, float far_depth)
{
	m_state.Set_Zbuffer_Range(near_depth, far_depth);
}

void W3DCamera::Get_Zbuffer_Range(float &near_depth, float &far_depth) const
{
	m_state.Get_Zbuffer_Range(near_depth, far_depth);
}

void W3DCamera::Set_View_Plane(const Engine::Math::Vector2 &minimum, const Engine::Math::Vector2 &maximum)
{
	m_state.Set_View_Plane({minimum.x, minimum.y}, {maximum.x, maximum.y});
	Invalidate_Native_Cache();
}

void W3DCamera::Set_View_Plane(float horizontal_fov, float vertical_fov)
{
	m_state.Set_View_Plane(horizontal_fov, vertical_fov);
	Invalidate_Native_Cache();
}

void W3DCamera::Set_Aspect_Ratio(float width_to_height)
{
	m_state.Set_Aspect_Ratio(width_to_height);
	Invalidate_Native_Cache();
}

void W3DCamera::Get_View_Plane(Engine::Math::Vector2 &minimum, Engine::Math::Vector2 &maximum) const
{
	const auto &view_plane = m_state.Get_View_Plane();
	minimum = {view_plane.min.x, view_plane.min.y};
	maximum = {view_plane.max.x, view_plane.max.y};
}

float W3DCamera::Get_Horizontal_FOV() const
{
	return m_state.Get_Horizontal_FOV();
}

float W3DCamera::Get_Vertical_FOV() const
{
	return m_state.Get_Vertical_FOV();
}

float W3DCamera::Get_Aspect_Ratio() const
{
	return m_state.Get_Aspect_Ratio();
}

W3DCamera::ProjectionResType W3DCamera::Project(Engine::Math::Vector3 &destination,
	const Engine::Math::Vector3 &world_point) const
{
	Update_Native_Cache();
	Graphics::Vector3 projected{};
	const auto result = m_state.Project(projected, To_Graphics(world_point));
	destination = To_Native(projected);
	return To_Native(result);
}

W3DCamera::ProjectionResType W3DCamera::Project_Camera_Space_Point(Engine::Math::Vector3 &destination,
	const Engine::Math::Vector3 &camera_point) const
{
	Update_Native_Cache();
	Graphics::Vector3 projected{};
	const auto result = m_state.Project_Camera_Space_Point(projected, To_Graphics(camera_point));
	destination = To_Native(projected);
	return To_Native(result);
}

void W3DCamera::Un_Project(Engine::Math::Vector3 &destination, const Engine::Math::Vector2 &view_point) const
{
	Update_Native_Cache();
	Graphics::Vector3 world{};
	m_state.Un_Project(world, {view_point.x, view_point.y});
	destination = To_Native(world);
}

void W3DCamera::Transform_To_View_Space(Engine::Math::Vector3 &destination, const Engine::Math::Vector3 &world_point) const
{
	Update_Native_Cache();
	Graphics::Vector3 view{};
	m_state.Transform_To_View_Space(view, To_Graphics(world_point));
	destination = To_Native(view);
}

void W3DCamera::Rotate_To_View_Space(Engine::Math::Vector3 &destination, const Engine::Math::Vector3 &world_vector) const
{
	Update_Native_Cache();
	Graphics::Vector3 view{};
	m_state.Rotate_To_View_Space(view, To_Graphics(world_vector));
	destination = To_Native(view);
}

void W3DCamera::Set_Viewport(const Engine::Math::Vector2 &minimum, const Engine::Math::Vector2 &maximum)
{
	m_viewport.Min = minimum;
	m_viewport.Max = maximum;
	m_state.Set_Viewport({minimum.x, minimum.y}, {maximum.x, maximum.y});
	Invalidate_Native_Cache();
}

void W3DCamera::Get_Viewport(Engine::Math::Vector2 &minimum, Engine::Math::Vector2 &maximum) const
{
	minimum = m_viewport.Min;
	maximum = m_viewport.Max;
}

const W3DViewport &W3DCamera::Get_Viewport() const
{
	return m_viewport;
}

void W3DCamera::Set_Depth_Range(float start, float end)
{
	m_state.Set_Depth_Range(start, end);
}

void W3DCamera::Get_Depth_Range(float *start, float *end) const
{
	m_state.Get_Depth_Range(start, end);
}

bool W3DCamera::Cull_Sphere(const Engine::Math::Sphere3 &sphere) const
{
	Update_Native_Cache();
	return m_state.Cull_Sphere({{sphere.center.x, sphere.center.y, sphere.center.z}, sphere.radius});
}

bool W3DCamera::Cull_Sphere_On_Frustum_Sides(const Engine::Math::Sphere3 &sphere) const
{
	Update_Native_Cache();
	return m_state.Cull_Sphere_On_Frustum_Sides({{sphere.center.x, sphere.center.y, sphere.center.z}, sphere.radius});
}

bool W3DCamera::Cull_Box(const Engine::Math::AxisAlignedBox3 &box) const
{
	Update_Native_Cache();
	const Engine::Math::Vector3 extent = (box.maximum - box.minimum) * 0.5f;
	const Engine::Math::Vector3 center = box.minimum + extent;
	return m_state.Cull_Box({{center.x, center.y, center.z}, {extent.x, extent.y, extent.z}});
}

const Graphics::CameraFrustum &W3DCamera::Get_Frustum() const noexcept
{
	return m_state.Get_Frustum();
}

W3DCameraRenderMatrices W3DCamera::Build_Render_Matrices() const
{
	Synchronize_State_Transform();
	Engine::Math::Matrix4 projection;
	projection.elements = m_state.Get_Backend_Projection_Matrix().values;
	Engine::Math::Matrix4 view;
	view.elements = m_state.Get_View_Matrix().values;
	const Engine::Math::Matrix4 view_projection = Compose(projection, view);
	const auto inverse_projection = projection.Inverse().value_or(
		Engine::Math::Matrix4::Identity());
	const auto inverse_view_projection = view_projection.Inverse().value_or(
		Engine::Math::Matrix4::Identity());
	return {view.elements, projection.elements, view_projection.elements,
		inverse_projection.elements, inverse_view_projection.elements};
}

std::array<float, 16> W3DCamera::Build_World_Matrix(const Engine::Math::AffineTransform3 &world)
{
	Engine::Math::Matrix4 matrix = Engine::Math::Matrix4::Identity();
	for (unsigned row = 0; row < 3; ++row)
		for (unsigned column = 0; column < 4; ++column)
			matrix(row, column) = world.elements[row * 4 + column];
	return matrix.elements;
}

std::array<float, 16> W3DCamera::Build_Object_View_Projection(
	const Engine::Math::AffineTransform3 &world) const
{
	const auto camera = Build_Render_Matrices();
	Engine::Math::Matrix4 view_projection;
	view_projection.elements = camera.view_projection;
	Engine::Math::Matrix4 object;
	object.elements = Build_World_Matrix(world);
	return Compose(view_projection, object).elements;
}

const Graphics::CameraFrustum &W3DCamera::Get_View_Space_Frustum() const noexcept
{
	return m_state.Get_View_Space_Frustum();
}

const Engine::Math::OrientedBox3 &W3DCamera::Get_Near_Clip_Bounding_Box() const
{
	Update_Native_Cache();
	return m_near_clip_box;
}

void W3DCamera::Device_To_View_Space(const Engine::Math::Vector2 &device_coordinate, Engine::Math::Vector3 *view_coordinate)
{
	if (view_coordinate == nullptr)
		return;
	Update_Native_Cache();
	const auto &viewport = Graphics::Get_Attachment_Bindings().Default().viewport;
	Graphics::Vector3 view{};
	m_state.Device_To_View_Space({device_coordinate.x, device_coordinate.y},
		static_cast<float>(viewport.width), static_cast<float>(viewport.height), view);
	*view_coordinate = To_Native(view);
}

void W3DCamera::Device_To_World_Space(const Engine::Math::Vector2 &device_coordinate, Engine::Math::Vector3 *world_coordinate)
{
	if (world_coordinate == nullptr)
		return;
	Update_Native_Cache();
	const auto &viewport = Graphics::Get_Attachment_Bindings().Default().viewport;
	Graphics::Vector3 world{};
	m_state.Device_To_World_Space({device_coordinate.x, device_coordinate.y},
		static_cast<float>(viewport.width), static_cast<float>(viewport.height), world);
	*world_coordinate = To_Native(world);
}

float W3DCamera::Compute_Projected_Sphere_Radius(float distance, float radius)
{
	return m_state.Compute_Projected_Sphere_Radius(distance, radius);
}

void W3DCamera::Apply()
{
	Update_Native_Cache();
	const auto &screen = Graphics::Get_Attachment_Bindings().Default().viewport;
	const float width = static_cast<float>(screen.width);
	const float height = static_cast<float>(screen.height);
	Graphics::RHIViewport viewport{};
	viewport.x = static_cast<std::uint32_t>(m_viewport.Min.x * width);
	viewport.y = static_cast<std::uint32_t>(m_viewport.Min.y * height);
	viewport.width = static_cast<std::uint32_t>(m_viewport.Width() * width);
	viewport.height = static_cast<std::uint32_t>(m_viewport.Height() * height);
	m_state.Get_Depth_Range(&viewport.min_depth, &viewport.max_depth);
	Graphics::Get_Attachment_Bindings().Set_Viewport(viewport);
	m_state.Publish_Camera_Matrices();
}

void W3DCamera::Invalidate_Native_Cache() noexcept
{
	m_native_cache_valid = false;
}

void W3DCamera::Update_Native_Cache() const
{
	if (m_native_cache_valid)
		return;
	Synchronize_State_Transform();

	Set_Native_Near_Clip_Box(m_near_clip_box, m_state.Get_Near_Clip_Bounding_Box());
	m_native_cache_valid = true;
}

void W3DCamera::Synchronize_State_Transform() const noexcept
{
	const Graphics::RenderTransform transform = To_Graphics(Get_Transform());
	if (transform.matrix != m_state.Get_Transform().matrix) {
		m_state.Set_Transform(transform);
		m_native_cache_valid = false;
	}
}
