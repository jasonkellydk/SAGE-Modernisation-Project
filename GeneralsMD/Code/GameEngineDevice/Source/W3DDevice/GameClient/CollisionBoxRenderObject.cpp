#include "W3DDevice/GameClient/W3DRenderServices.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

import Assets.Adapters.W3D.Box;
import Graphics.Scene.Debug.CollisionBox;
import Graphics.Scene.Props.Renderer;
import Graphics.Scene.Props.Submission;
import Graphics.Scene.Views.CameraMatrices;
import Graphics.Scene.Views.View;
import Engine.Core.Math.OrientedBox3;
import Engine.Core.Math.Matrix4;

#include "W3DDevice/GameClient/CollisionBoxRenderObject.h"
#include "W3DDevice/GameClient/W3DCastQuery.h"
#include "W3DDevice/GameClient/W3DIntersectionQuery.h"
#include "W3DDevice/GameClient/W3DRenderContext.h"

#include "WWLib/chunkio.h"
namespace
{

Engine::Math::Matrix4 To_Engine_Matrix(const Engine::Math::AffineTransform3 &transform) noexcept
{
	auto result = Engine::Math::Matrix4::Identity();
	for (unsigned row = 0; row < 3; ++row)
		for (unsigned column = 0; column < 4; ++column)
			result(row, column) = transform.elements[row * 4 + column];
	return result;
}

Engine::Math::OrientedBox3 As_Oriented(const Engine::Math::AxisAlignedBox3 &box) noexcept
{
	return {(box.minimum + box.maximum) * 0.5f,
		(box.maximum - box.minimum) * 0.5f};
}

bool Apply_Sweep(const std::optional<Engine::Math::SweptBoxHit3> &hit,
	Engine::Math::CollisionResult3 *result)
{
	if (!hit) return false;
	// Legacy box sweeps: a starting overlap sets StartBad and a zero fraction
	// and leaves the normal and contact point untouched.
	if (hit->starts_overlapping) {
		result->starts_overlapping = true;
		result->fraction = 0.0f;
		return true;
	}
	if (!(hit->fraction < result->fraction) || hit->fraction >= 1.0f) return false;
	result->fraction = hit->fraction;
	result->normal = hit->normal;
	if (result->compute_contact_point)
		result->contact_point = hit->point;
	return true;
}

Graphics::CollisionBoxDrawData Make_Draw_Data(const CollisionBoxRenderObject &object)
{
	Graphics::CollisionBoxDrawData data;
	data.center = {object.Get_Local_Center().x, object.Get_Local_Center().y, object.Get_Local_Center().z};
	data.extent = {object.Get_Local_Extent().x, object.Get_Local_Extent().y, object.Get_Local_Extent().z};
	data.color = {object.Get_Color().x, object.Get_Color().y, object.Get_Color().z, object.Get_Opacity()};
	data.collision_type = static_cast<std::uint32_t>(object.Get_Collision_Type());
	data.front_counter_clockwise = !Get_W3D_Render_Services().Is_Reflection_Render_Pass();

	const auto &camera = Graphics::Get_Camera_Matrices();
	data.view_projection = Graphics::Compose_Matrices(camera.projection, camera.view).values;

	Engine::Math::AffineTransform3 world_transform;
	if (object.Is_Oriented())
		world_transform = object.Get_Transform();
	else
		world_transform = Engine::Math::AffineTransform3::From_Translation(
			object.Get_Transform().Translation());
	data.world = To_Engine_Matrix(world_transform).elements;
	return data;
}

}

CollisionBoxRenderObject::CollisionBoxRenderObject()
{
	Set_Collision_Type(0);
	Update_Cached_Box();
}

CollisionBoxRenderObject::CollisionBoxRenderObject(const Assets::W3D::W3DBoxDescription &description)
	: m_name(description.name),
	  m_color(description.color[0] / 255.0f, description.color[1] / 255.0f, description.color[2] / 255.0f),
	  m_local_center(description.center.x, description.center.y, description.center.z),
	  m_local_extent(description.extent.x, description.extent.y, description.extent.z),
	  m_oriented(description.Is_Oriented())
{
	Set_Collision_Type(static_cast<int>(description.Collision_Type()));
	Update_Cached_Box();
}

CollisionBoxRenderObject::CollisionBoxRenderObject(const Engine::Math::AxisAlignedBox3 &box)
	: m_local_extent{(box.maximum.x - box.minimum.x) * 0.5f,
		(box.maximum.y - box.minimum.y) * 0.5f, (box.maximum.z - box.minimum.z) * 0.5f}
{
	Set_Collision_Type(0);
	Set_Position(Engine::Math::Vector3{(box.minimum.x + box.maximum.x) * 0.5f,
		(box.minimum.y + box.maximum.y) * 0.5f, (box.minimum.z + box.maximum.z) * 0.5f});
	Update_Cached_Box();
}

CollisionBoxRenderObject::CollisionBoxRenderObject(const Engine::Math::OrientedBox3 &box)
	: m_local_extent(box.half_extent),
	  m_oriented(true)
{
	Set_Collision_Type(0);
	Engine::Math::AffineTransform3 transform;
	for (unsigned axis = 0; axis < 3; ++axis) {
		transform.elements[axis] = box.axes[axis].x;
		transform.elements[4 + axis] = box.axes[axis].y;
		transform.elements[8 + axis] = box.axes[axis].z;
	}
	transform.elements[3] = box.center.x;
	transform.elements[7] = box.center.y;
	transform.elements[11] = box.center.z;
	Set_Transform(transform);
	Update_Cached_Box();
}

CollisionBoxRenderObject::CollisionBoxRenderObject(const CollisionBoxRenderObject &source)
	: W3DRenderObject(source),
	  m_name(source.m_name),
	  m_color(source.m_color),
	  m_local_center(source.m_local_center),
	  m_local_extent(source.m_local_extent),
	  m_opacity(source.m_opacity),
	  m_oriented(source.m_oriented),
	  m_cached_aa_box(source.m_cached_aa_box),
	  m_cached_ob_box(source.m_cached_ob_box)
{
}

CollisionBoxRenderObject &CollisionBoxRenderObject::operator=(const CollisionBoxRenderObject &source)
{
	if (this != &source) {
		m_graphics.Release(Graphics::Get_Prop_Renderer());
		W3DRenderObject::operator=(source);
		m_name = source.m_name;
		m_color = source.m_color;
		m_local_center = source.m_local_center;
		m_local_extent = source.m_local_extent;
		m_opacity = source.m_opacity;
		m_oriented = source.m_oriented;
		m_cached_aa_box = source.m_cached_aa_box;
		m_cached_ob_box = source.m_cached_ob_box;
	}
	return *this;
}

W3DRenderObject *CollisionBoxRenderObject::Clone() const
{
	return W3DNEW CollisionBoxRenderObject(*this);
}

int CollisionBoxRenderObject::Class_ID() const
{
	return m_oriented ? W3DRenderObject::CLASSID_OBBOX : W3DRenderObject::CLASSID_AABOX;
}

void CollisionBoxRenderObject::Set_Name(const char *name)
{
	if (name != nullptr)
		m_name = name;
}

void CollisionBoxRenderObject::Render(W3DRenderContext &rinfo)
{
	(void)rinfo;
	const auto data = Make_Draw_Data(*this);
	m_graphics.Submit(Graphics::Get_Prop_Renderer(), Graphics::Get_Prop_Submission(), data);
}

void CollisionBoxRenderObject::Set_Transform(const Engine::Math::AffineTransform3 &transform)
{
	W3DRenderObject::Set_Transform(transform);
	Update_Cached_Box();
}

void CollisionBoxRenderObject::Set_Position(Engine::Math::Vector3 position)
{
	W3DRenderObject::Set_Position(position);
	Update_Cached_Box();
}

void CollisionBoxRenderObject::Set_Local_Center_Extent(
	Engine::Math::Vector3 center, Engine::Math::Vector3 extent)
{
	m_local_center = center;
	m_local_extent = extent;
	Update_Cached_Box();
}

void CollisionBoxRenderObject::Set_Local_Min_Max(
	Engine::Math::Vector3 minimum, Engine::Math::Vector3 maximum)
{
	m_local_center = (maximum + minimum) / 2.0f;
	m_local_extent = (maximum - minimum) / 2.0f;
	Update_Cached_Box();
}

void CollisionBoxRenderObject::Update_Cached_Box()
{
	// Must not validate here: Get_Transform() re-enters Validate_Transform(),
	// which lets a container update its sub-objects and call back into us.
	const auto math_transform = Get_Transform_No_Validity_Check();
	const Engine::Math::Vector3 extent = m_local_extent;
	// The axis-aligned box ignores rotation: world center = translation + local center.
	const Engine::Math::Vector3 aa_center = math_transform.Translation() + m_local_center;
	m_cached_aa_box = {aa_center - extent, aa_center + extent};
	const auto ob_center = math_transform.Transform_Point(m_local_center);
	const auto &basis = math_transform.elements;
	m_cached_ob_box = {ob_center, extent, {{
		{basis[0], basis[4], basis[8]},
		{basis[1], basis[5], basis[9]},
		{basis[2], basis[6], basis[10]}}}};
}

const Engine::Math::AxisAlignedBox3 &CollisionBoxRenderObject::Get_AA_Box() const
{
	Validate_Transform();
	const_cast<CollisionBoxRenderObject *>(this)->Update_Cached_Box();
	return m_cached_aa_box;
}

const Engine::Math::OrientedBox3 &CollisionBoxRenderObject::Get_OB_Box() const
{
	Validate_Transform();
	const_cast<CollisionBoxRenderObject *>(this)->Update_Cached_Box();
	return m_cached_ob_box;
}

bool CollisionBoxRenderObject::Cast_Ray(W3DRayCastQuery &raytest)
{
	if ((Get_Collision_Type() & raytest.CollisionType) == 0)
		return false;
	if (Is_Animation_Hidden() || raytest.Result->starts_overlapping)
		return false;
	// Legacy CollisionMath::Collide(LineSeg, box): a start inside the box sets
	// starts_overlapping, reports the start as the contact point, keeps the
	// fraction and still counts as a hit.
	const bool collided = m_oriented
		? m_cached_ob_box.Collide_Segment_Legacy(raytest.Ray.start, raytest.Ray.end, *raytest.Result)
		: m_cached_aa_box.Collide_Segment_Legacy(raytest.Ray.start, raytest.Ray.end, *raytest.Result);
	if (collided)
		raytest.CollidedRenderObj = this;
	return collided;
}

bool CollisionBoxRenderObject::Cast_AABox(W3DBoxCastQuery &boxtest)
{
	if ((Get_Collision_Type() & boxtest.CollisionType) == 0 || boxtest.Result->starts_overlapping)
		return false;
	const Engine::Math::OrientedBox3 moving{boxtest.Box.Center(), boxtest.Box.Extent()};
	const auto target = m_oriented ? m_cached_ob_box : As_Oriented(m_cached_aa_box);
	const bool collided = Apply_Sweep(moving.Sweep(target, boxtest.Move), boxtest.Result);
	if (collided)
		boxtest.CollidedRenderObj = this;
	return collided;
}

bool CollisionBoxRenderObject::Cast_OBBox(W3DOrientedBoxCastQuery &boxtest)
{
	if ((Get_Collision_Type() & boxtest.CollisionType) == 0 || boxtest.Result->starts_overlapping)
		return false;
	const auto target = m_oriented ? m_cached_ob_box : As_Oriented(m_cached_aa_box);
	const bool collided = Apply_Sweep(boxtest.Box.Sweep(target, boxtest.Move), boxtest.Result);
	if (collided)
		boxtest.CollidedRenderObj = this;
	return collided;
}

bool CollisionBoxRenderObject::Intersect_AABox(W3DBoxIntersectionQuery &boxtest)
{
	if ((Get_Collision_Type() & boxtest.CollisionType) == 0)
		return false;
	const auto target = m_oriented ? m_cached_ob_box : As_Oriented(m_cached_aa_box);
	return Engine::Math::OrientedBox3{boxtest.Box.Center(), boxtest.Box.Extent()}.Intersects(target);
}

bool CollisionBoxRenderObject::Intersect_OBBox(W3DOrientedBoxIntersectionQuery &boxtest)
{
	if ((Get_Collision_Type() & boxtest.CollisionType) == 0)
		return false;
	const auto target = m_oriented ? m_cached_ob_box : As_Oriented(m_cached_aa_box);
	return boxtest.Box.Intersects(target);
}

void CollisionBoxRenderObject::Get_Local_Bounding_Sphere(Engine::Math::Sphere3 &sphere) const
{
	sphere = {m_local_center, m_local_extent.Length()};
}

void CollisionBoxRenderObject::Get_Local_Bounds(Engine::Math::AxisAlignedBox3 &box) const
{
	box = {m_local_center - m_local_extent, m_local_center + m_local_extent};
}

Graphics::ModelFactory<W3DRenderObject> *Load_Collision_Box_Factory(ChunkLoadClass &cload)
{
	std::array<std::byte, Assets::W3D::W3DBoxPayloadSize> bytes{};
	if (cload.Cur_Chunk_Length() != bytes.size()
		|| cload.Read(bytes.data(), static_cast<unsigned>(bytes.size())) != bytes.size())
		return nullptr;

	Assets::W3D::W3DBoxDescription description;
	std::string error;
	if (!Assets::W3D::W3DRead_Box(bytes, description, error))
		return nullptr;
	const int class_id = description.Is_Oriented()
		? W3DRenderObject::CLASSID_OBBOX : W3DRenderObject::CLASSID_AABOX;
	return new Graphics::ModelFactory<W3DRenderObject>(description.name, class_id,
		[description]() -> W3DRenderObject * {
			return NEW_REF(CollisionBoxRenderObject, (description));
		});
}
