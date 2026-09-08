/*
** Command & Conquer Generals Zero Hour(tm)
*/

#include <array>
#include <span>
#include <memory>
#include "W3DDevice/GameClient/WaterSkyboxSystem.h"
#include "W3DDevice/GameClient/W3DObjectGraphics.h"

#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "WW3D2/Mesh.h"
#include "WW3D2/RInfo.h"
#include "WW3D2/RendObj.h"
#include "WW3D2/Texture.h"

struct WaterSkyboxSystem::State
{
	RenderObjClass *skybox = nullptr;
    W3DObjectGraphics graphics;
};

WaterSkyboxSystem::WaterSkyboxSystem() :
	m_state(new State)
{
}

WaterSkyboxSystem::~WaterSkyboxSystem()
{
	if (m_state != nullptr)
	{
		REF_PTR_RELEASE(m_state->skybox);
		delete m_state;
		m_state = nullptr;
	}
}

bool WaterSkyboxSystem::Initialize(float scale)
{
	if (m_state == nullptr || m_state->skybox != nullptr)
		return m_state != nullptr;

	W3DAssetManager *asset_manager =
		static_cast<W3DAssetManager *>(W3DAssetManager::Get_Instance());
	m_state->skybox = asset_manager->Create_Render_Obj("new_skybox", scale, 0);
	if (m_state->skybox == nullptr ||
		m_state->skybox->Class_ID() != RenderObjClass::CLASSID_MESH)
	{
		return m_state->skybox != nullptr;
	}

	MeshClass *mesh = static_cast<MeshClass *>(m_state->skybox);
	auto material = mesh->Get_Material_Info();
	if (material == nullptr)
		return true;

	for (Int i = 0; i < static_cast<int>(material->textures.size()); ++i)
	{
		if (material->textures[i].Peek() != nullptr)
		{
			material->textures[i].Peek()->Get_Sampling().address.fill(Graphics::RHISamplerAddress::Clamp);
		}
	}
	material.reset();
	return true;
}

void WaterSkyboxSystem::Render(RenderInfoClass &rinfo, float x, float y,
	float z)
{
	if (m_state == nullptr || m_state->skybox == nullptr)
		return;

	Vector3 position(x, y, z);
	m_state->skybox->Set_Position(position);
	m_state->graphics.Render(*m_state->skybox,rinfo,{},nullptr,true);
}

void WaterSkyboxSystem::Replace_Texture(const char *old_name,
	const char *new_name)
{
	if (m_state == nullptr || m_state->skybox == nullptr)
		return;

	W3DAssetManager *asset_manager =
		static_cast<W3DAssetManager *>(W3DAssetManager::Get_Instance());
	asset_manager->replacePrototypeTexture(m_state->skybox, old_name,
		new_name);
    m_state->graphics.Invalidate();
}
