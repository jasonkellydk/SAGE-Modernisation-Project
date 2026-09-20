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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: W3DPoliceCarDraw.cpp /////////////////////////////////////////////////////////////////////
// Author: Colin Day, May 2001
// Desc:   W3DPoliceCarDraw
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include <stdlib.h>
#include <cmath>
#include <cstdio>
#include <algorithm>

#include "Common/FramePacer.h"
#include "Common/STLTypedefs.h"
#include "Common/Thing.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "W3DDevice/GameClient/Module/W3DPoliceCarDraw.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "Common/RandomValue.h"
#include "W3DDevice/GameClient/W3DScene.h"
import Assets.Cache.Animations;

// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
/** Create a dynamic light for the search light */
//-------------------------------------------------------------------------------------------------
W3DDynamicLight *W3DPoliceCarDraw::createDynamicLight()
{
	W3DDynamicLight *light = nullptr;

	// get me a dynamic light from the scene
	light = W3DDisplay::m_3DScene->getADynamicLight();
	if( light )
	{

		light->setEnabled( TRUE );
		light->Set_Ambient( Vector3( 0.0f, 0.0f, 0.0f ) );
		light->Set_Diffuse( Vector3( 0.0f, 0.0f, 0.0f ) );
		light->Set_Position( Vector3( 0.0f, 0.0f, 0.0f ) );
		light->Set_Far_Attenuation_Range( 5, 15 );

	}

	return light;

}

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DPoliceCarDraw::W3DPoliceCarDraw( Thing *thing, const ModuleData* moduleData ) : W3DTruckDraw( thing, moduleData )
{
	m_light = nullptr;
	m_curFrame = GameClientRandomValueReal(0, 10 );

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DPoliceCarDraw::~W3DPoliceCarDraw()
{

    for(auto* light : {m_light,m_blueLight,m_headlights[0],m_headlights[1]}) {
        if(!light) continue;
        light->setFrameFade(0,5);
        light->setDecayColor();
    }
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DPoliceCarDraw::doDrawModule(const Matrix3D* transformMtx)
{
    // Set the current model, client-physics transform and scale before sampling
    // lamp bones. Logic position alone omits those transforms.
    W3DTruckDraw::doDrawModule(transformMtx);
    W3DRenderObject* model = getRenderObject();
    for (auto* light : {m_light,m_blueLight,m_headlights[0],m_headlights[1]})
        if (light) light->Set_Intensity(0);
    if (!model) return;

    const Real step = 0.25f * TheFramePacer->getActualLogicTimeScaleOverFpsRatio();
    float frameCount = 15;
    const auto animation = model->Peek_Animation();
    if (animation) {
        const auto* asset = Assets::Get_Animation_Cache().Resolve(animation);
        if (asset && asset->frame_count > 0) frameCount = float(asset->frame_count);
    }
    m_curFrame = std::fmod(m_curFrame + step, frameCount);
    if (animation) model->Set_Animation(animation, m_curFrame);

    // The authored animation moves the inactive flare inside the body. Red is
    // exposed during frames 0..6, blue during 7..14. Follow the actual lamp
    // pose and fade across the transition rather than illuminating a guessed
    // position while the corresponding flare is hidden.
    const auto beacon = [&](W3DDynamicLight*& light, const char* boneName,
                            const Vector3& color, bool blue) {
        const int bone = model->Get_Bone_Index(boneName);
        if (!bone) return; // Damaged models can have no light bar.
        if (!light) light = createDynamicLight();
        if (!light) return;
        const float frame = m_curFrame * (15.f / frameCount);
        const float phase = blue ? (frame - 7.f) / 8.f : frame / 7.f;
        const float pulse = phase >= 0 && phase < 1
            ? std::sin(phase * 3.14159265f) : 0.f;
        light->Set_Type(W3DLight::POINT);
        light->Set_Position(model->Get_Bone_Transform(bone).Get_Translation());
        light->Set_Diffuse(color);
        light->Set_Intensity(2.f * pulse * pulse);
        light->Set_Far_Attenuation_Range(8,45);
    };
    beacon(m_light, "CXPOLICECAR001", Vector3(1.f,.01f,.005f), false);
    beacon(m_blueLight, "CXPOLICECAR000", Vector3(.02f,.12f,1.f), true);

    // These bones carried textured light cones in the original W3D. Replace
    // their illumination with real spotlights evaluated by all PBR surfaces.
    for (int i=0; i<2; ++i) {
        int bone = model->Get_Bone_Index(i ? "HEADLIGHT02" : "HEADLIGHT01");
        if (!bone) bone = model->Get_Bone_Index(i ? "CXPOLICECAR005" : "CXPOLICECAR004");
        if (!bone) continue;
        auto*& light = m_headlights[i];
        if (!light) light = createDynamicLight();
        if (!light) continue;
        light->Set_Type(W3DLight::SPOT);
        light->Set_Transform(model->Get_Bone_Transform(bone));
        light->Set_Spot_Direction(Vector3(0,1,0));
        light->Set_Spot_Angle(.24f);
        light->Set_Spot_Exponent(1);
        light->Set_Diffuse(Vector3(1.f,.94f,.82f));
        light->Set_Intensity(1.f);
        light->Set_Far_Attenuation_Range(20,85);
        for (int child=0; child<model->Get_Num_Sub_Objects_On_Bone(bone); ++child) {
            auto* cone = model->Get_Sub_Object_On_Bone(child,bone);
            if (!cone) continue;
            cone->Set_Hidden(true);
            cone->Release_Ref();
        }
    }
    static FILE* trace=std::getenv("GENERALS_LIGHTING_TRACE") ? std::fopen("lighting-police-trace.txt","w") : nullptr;
    static unsigned samples=0;
    if(trace && ++samples%120==0) {
        std::fprintf(trace,"model=%s frame=%.2f\n",model->Get_Name(),m_curFrame);
        for(auto* light : {m_light,m_blueLight,m_headlights[0],m_headlights[1]}) {
            if(!light) continue;
            Graphics::MaterialLightSource source;light->Get_Light_Description(source);
            std::fprintf(trace,"type=%d pos=%.2f,%.2f,%.2f direction=%.3f,%.3f,%.3f intensity=%.3f\n",int(light->Get_Type()),source.position[0],source.position[1],source.position[2],source.direction[0],source.direction[1],source.direction[2],source.intensity);
        }
        std::fflush(trace);
    }

}


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DPoliceCarDraw::crc( Xfer *xfer )
{

	// extend base class
	W3DTruckDraw::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void W3DPoliceCarDraw::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	W3DTruckDraw::xfer( xfer );

	// John A says there is no data for these to save

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DPoliceCarDraw::loadPostProcess()
{

	// extend base class
	W3DTruckDraw::loadPostProcess();

}
