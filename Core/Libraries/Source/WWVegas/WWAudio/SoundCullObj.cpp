#include "SoundCullObj.h"

#include <cmath>

Engine::Math::AffineTransform3 SoundCullObjClass::Get_Transform() const
{
	// Determine the transform to use
	if (m_SoundObj != nullptr)
		m_Transform = m_SoundObj->Get_Transform();
	return m_Transform;
}

void SoundCullObjClass::Set_Transform(const Engine::Math::AffineTransform3 &transform)
{
	m_Transform = transform;

	// Pass the tranform on and re-bucket the object in its culling system
	if (m_SoundObj != nullptr) {
		m_SoundObj->Set_Transform(m_Transform);
		Update_Cull_Box();
	}
}

void SoundCullObjClass::Set_Sound_Obj(SoundSceneObjClass *sound_obj)
{
	// Start using this sound object
	REF_PTR_SET(m_SoundObj, sound_obj);
	if (m_SoundObj != nullptr) {
		m_Transform = m_SoundObj->Get_Transform();
		Update_Cull_Box();
	}
}

Engine::Math::AxisAlignedBox3 SoundCullObjClass::Get_Spatial_Bounds() const
{
	float radius = 0.0f;
	if (m_SoundObj != nullptr) {
		m_Transform = m_SoundObj->Get_Transform();
		radius = m_SoundObj->Get_DropOff_Radius();
	}
	const auto center = m_Transform.Translation();
	const Engine::Math::Vector3 extent{radius, radius, radius};
	return {center - extent, center + extent};
}

void SoundCullObjClass::Update_Cull_Box()
{
	// Same values as the legacy Get_Bounding_Box (): center is the sound's
	// translation, extent is the drop-off radius on every axis.
	float radius = 0.0f;
	if (m_SoundObj != nullptr) {
		m_Transform = m_SoundObj->Get_Transform();
		radius = m_SoundObj->Get_DropOff_Radius();
	}
	m_CullCenter = m_Transform.Translation();
	m_CullExtent = Engine::Math::Vector3{radius, radius, radius};

	if (m_CullingSystem != nullptr) {
		m_CullingSystem->Update(this, {m_CullCenter - m_CullExtent, m_CullCenter + m_CullExtent});
	}
}

bool SoundCullObjClass::Cull_Box_Contains(Engine::Math::Vector3 point) const
{
	// Legacy CollisionMath::Overlap_Test (AABoxClass, Vector3) == INSIDE
	if (std::fabs(point.x - m_CullCenter.x) > m_CullExtent.x) return false;
	if (std::fabs(point.y - m_CullCenter.y) > m_CullExtent.y) return false;
	if (std::fabs(point.z - m_CullCenter.z) > m_CullExtent.z) return false;
	return true;
}
