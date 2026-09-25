#include "Precompiled/PreRTS.h"
import engine.debug;
import engine.profiling;
import Engine.Core.Math.AffineTransform3;
import Engine.Core.Math.Vector3;
import Graphics.RHI;
#include "Common/GameLOD.h"
#include "Common/GlobalData.h"
#include "GameClient/TerrainVisual.h"
#include "GameLogic/AIPathfind.h"
#include "GameClient/View.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "W3DDevice/GameClient/W3DLight.h"
#include "W3DDevice/GameClient/W3DRenderObject.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DTerrainGraphics.h"
#include "W3DDevice/GameClient/W3DRenderServices.h"

extern bool graphicsRendererAvailable;
extern bool presentMeasuredGraphicsFrame() noexcept;
extern Int64 getPerformanceCounter(engine::platform::IClockService &clock);
extern Int64 getPerformanceCounterFrequency();

extern "C" bool Graphics_Begin_Frame() noexcept;
extern "C" bool Graphics_End_Frame() noexcept;
extern "C" void Graphics_Abort_Frame() noexcept;

RTS3DScene *W3DDisplay::m_3DScene = nullptr;
RTS2DScene *W3DDisplay::m_2DScene = nullptr;
RTS3DInterfaceScene *W3DDisplay::m_3DInterfaceScene = nullptr;

void W3DDisplay::createScenes()
{
	if (TheGlobalData->m_headless)
		return;

	m_3DInterfaceScene = NEW_REF(RTS3DInterfaceScene, ());
	m_3DInterfaceScene->Set_Ambient_Light({1, 1, 1});
	m_2DScene = NEW_REF(RTS2DScene, ());
	m_2DScene->Set_Ambient_Light({1, 1, 1});
	m_3DScene = NEW_REF(RTS3DScene, ());
#if defined(RTS_DEBUG)
	if (TheGlobalData->m_wireframe)
		m_3DScene->Set_Polygon_Mode(W3DScene::LINE);
#endif
	for (Int index = 0; index < TheGlobalData->m_numGlobalLights; ++index)
		m_myLight[index] = NEW_REF(W3DLight, (W3DLight::DIRECTIONAL));

	setTimeOfDay(TheGlobalData->m_timeOfDay);
	for (Int index = 0; index < TheGlobalData->m_numGlobalLights; ++index)
		m_3DScene->setGlobalLight(m_myLight[index], index);
}

void W3DDisplay::destroyScenes()
{
	REF_PTR_RELEASE(m_3DScene);
	REF_PTR_RELEASE(m_2DScene);
	REF_PTR_RELEASE(m_3DInterfaceScene);
	for (Int index = 0; index < Graphics::Material_Light_Count; ++index)
		REF_PTR_RELEASE(m_myLight[index]);
}

void W3DDisplay::removeSceneObjects()
{
	if (m_3DScene == nullptr)
		return;

	W3DSceneIterator *iterator = m_3DScene->Create_Iterator();
	iterator->First();
	while (!iterator->Is_Done()) {
		W3DRenderObject *object = iterator->Current_Item();
		object->Add_Ref();
		m_3DScene->Remove_Render_Object(object);
		object->Release_Ref();
		iterator->Next();
	}
	m_3DScene->Destroy_Iterator(iterator);
}

void W3DDisplay::calculateTerrainLOD()
{
	constexpr Int sample_count = 20;
	constexpr Int discarded_samples = 5;
	const Int64 frequency = getPerformanceCounterFrequency();
	const float maximum_time = TheGlobalData->m_terrainLODTargetTimeMS / 1000.0f;
	float frame_time = 0;
	TerrainLOD best_lod = TERRAIN_LOD_MIN;
	TerrainLOD current_lod = TERRAIN_LOD_AUTOMATIC;
	Int count = 0;
#ifdef RTS_DEBUG
	TheWritableGlobalData->m_terrainLOD = TERRAIN_LOD_NO_WATER;
	m_3DScene->drawTerrainOnly(false);
	TheTerrainRenderObject->adjustTerrainLOD(0);
	return;
#endif
	do {
		Int index = 0;
		float total_sample_time = 0;
		frame_time = 0;
		switch (current_lod) {
		default: current_lod = TERRAIN_LOD_DISABLE; break;
		case TERRAIN_LOD_AUTOMATIC: current_lod = TERRAIN_LOD_MAX; break;
		case TERRAIN_LOD_MAX: current_lod = TERRAIN_LOD_NO_WATER; break;
		case TERRAIN_LOD_NO_WATER: current_lod = TERRAIN_LOD_DISABLE; break;
		}
		if (current_lod == TERRAIN_LOD_DISABLE)
			break;
		TheWritableGlobalData->m_terrainLOD = current_lod;
		m_3DScene->drawTerrainOnly(true);
		TheTerrainRenderObject->adjustTerrainLOD(0);
		for (index = 0; index < sample_count; ++index) {
			const Int64 start = getPerformanceCounter(m_platform.clock());
			updateViews();
			if (!graphicsRendererAvailable || !Graphics_Begin_Frame()) {
				Graphics_Abort_Frame();
				m_3DScene->drawTerrainOnly(false);
				return;
			}
			if (Get_W3D_Render_Services().Begin_Render(true, true, Engine::Math::Vector3{})) {
				drawViews();
				Get_W3D_Render_Services().End_Render();
				if (!Graphics_End_Frame() || !presentMeasuredGraphicsFrame())
					Graphics_Abort_Frame();
			} else {
				Graphics_Abort_Frame();
			}
			const Int64 end = getPerformanceCounter(m_platform.clock());
			const float sample_time = static_cast<float>(static_cast<double>(end - start) / frequency);
			if (index >= discarded_samples) {
				total_sample_time += sample_time;
				if (index > discarded_samples + 1 &&
					sample_time / (index + 1 - discarded_samples) > 2 * maximum_time) {
					++index;
					break;
				}
			}
		}
		frame_time = total_sample_time / (index - discarded_samples);
		++count;
		if (frame_time < maximum_time && best_lod < current_lod)
			best_lod = current_lod;
		if (frame_time < maximum_time)
			break;
	} while (count < 10);

	TheWritableGlobalData->m_terrainLOD = best_lod;
	m_3DScene->drawTerrainOnly(false);
	TheTerrainRenderObject->adjustTerrainLOD(0);
#ifdef RTS_DEBUG
	engine::debug::invariant(count < 10, "count<10", __FILE__, __LINE__, "calculateTerrainLOD");
#endif
}

void W3DDisplay::createLightPulse(const Coord3D *position, const RGBColor *color,
	Real inner_radius, Real attenuation_width, UnsignedInt increase_frame_time,
	UnsignedInt decay_frame_time)
{
	if (m_3DScene == nullptr || inner_radius + attenuation_width < 2.0f * PATHFIND_CELL_SIZE_F + 1.0f)
		return;
	W3DDynamicLight *light = m_3DScene->getADynamicLight();
	light->setEnabled(true);
	light->Set_Ambient({color->red, color->green, color->blue});
	light->Set_Diffuse({color->red, color->green, color->blue});
	light->Set_Position(Engine::Math::Vector3{position->x, position->y, position->z});
	light->Set_Far_Attenuation_Range(inner_radius, inner_radius + attenuation_width);
	light->setFrameFade(increase_frame_time, decay_frame_time);
	light->setDecayRange();
	light->setDecayColor();
	light->Set_Flag(W3DLight::FAR_ATTENUATION, true);
}

void W3DDisplay::setTimeOfDay(TimeOfDay time_of_day)
{
	const GlobalData::TerrainLighting *lighting = &TheGlobalData->m_terrainObjectsLighting[time_of_day][0];
	if (m_3DScene != nullptr)
		m_3DScene->Set_Ambient_Light({lighting->ambient.red, lighting->ambient.green, lighting->ambient.blue});

	for (Int index = 0; index < Graphics::Material_Light_Count; ++index) {
		if (m_myLight[index] == nullptr)
			continue;
		lighting = &TheGlobalData->m_terrainObjectsLighting[time_of_day][index];
		m_myLight[index]->Set_Ambient({0, 0, 0});
		m_myLight[index]->Set_Diffuse({lighting->diffuse.red, lighting->diffuse.green, lighting->diffuse.blue});
		m_myLight[index]->Set_Specular({0, 0, 0});
		const auto transform = Engine::Math::AffineTransform3::From_Basis(
			{1, 0, 0}, {0, 1, 0}, {lighting->lightPos.x, lighting->lightPos.y, lighting->lightPos.z});
		m_myLight[index]->Set_Transform(transform);
	}
	if (TheTerrainRenderObject != nullptr) {
		TheTerrainRenderObject->setTimeOfDay(time_of_day);
		TheTacticalView->forceRedraw();
	}
}
