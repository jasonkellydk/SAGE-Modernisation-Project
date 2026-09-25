import Graphics.Frame.Runtime;
/*
**	Command & Conquer Renegade(tm)
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

// GraphicView.cpp : implementation file
//

#include "StdAfx.h"
#include "W3DView.h"
#include "GraphicView.h"
#include "WW3D2/WW3D.h"
import Engine.Core.Math.Quaternion;
import Engine.Core.Math.AffineTransform3;
#ifdef RTS_ZEROHOUR
import Graphics.Frame.ToolFrame;
#endif
#include "Globals.h"
#include "W3DViewDoc.h"
#include <process.h>
#include "MainFrm.h"
#include "Utils.h"
#include "mmsystem.h"
#include "WW3D2/Light.h"
#include "ViewerAssetMgr.h"
#include "WWLib/rcfile.h"
#include "WW3D2/PartEmt.h"
#include "WW3D2/PartBuf.h"
#include "WW3D2/HLOD.h"
#include "ViewerScene.h"
#include "ScreenCursor.h"
#include "W3DDevice/GameClient/W3DMeshRenderObject.h"
#include "WW3D2/ColTest.h"
#include "WWLib/MPU.h"
#include "W3DDevice/GameClient/W3DDazzleRenderObject.h"
#include "WWAudio/SoundScene.h"
#include "WWAudio/WWAudio.h"
#include "WW3D2/MetalMap.h"
import engine.debug;

#include <algorithm>
#include <cmath>

#ifdef RTS_DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


/////////////////////////////////////////////////////////////////////////
//  Local Prototypes
/////////////////////////////////////////////////////////////////////////
void CALLBACK fnTimerCallback (UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);

namespace
{
Engine::Math::Vector3 To_Core_Math(const Vector3 &value) noexcept
{
	return {value.X, value.Y, value.Z};
}

Engine::Math::AffineTransform3 To_Core_Math(const Matrix3D &value) noexcept
{
	Engine::Math::AffineTransform3 result;
	for (unsigned row = 0; row < 3; ++row)
		for (unsigned column = 0; column < 4; ++column)
			result.elements[row * 4 + column] = value[row][column];
	return result;
}

Engine::Math::Quaternion Quaternion_From_W3D(const Matrix3D &value) noexcept
{
	return Engine::Math::Quaternion::From_Rotation(To_Core_Math(value));
}

Matrix3D To_W3D(const Engine::Math::AffineTransform3 &value) noexcept
{
	Matrix3D result(1);
	for (unsigned row = 0; row < 3; ++row)
		for (unsigned column = 0; column < 4; ++column)
			result[row][column] = value.elements[row * 4 + column];
	return result;
}

// Row post-rotations of the legacy Matrix3D (Rotate_X/Y/Z(sin, cos)).
void Legacy_Rotate_X(Engine::Math::AffineTransform3 &m, float s, float c) noexcept
{
	for (unsigned row = 0; row < 3; ++row) {
		float *r = m[row];
		const float tmp1 = r[1];
		const float tmp2 = r[2];
		r[1] = c * tmp1 + s * tmp2;
		r[2] = -s * tmp1 + c * tmp2;
	}
}

void Legacy_Rotate_Y(Engine::Math::AffineTransform3 &m, float s, float c) noexcept
{
	for (unsigned row = 0; row < 3; ++row) {
		float *r = m[row];
		const float tmp1 = r[0];
		const float tmp2 = r[2];
		r[0] = c * tmp1 - s * tmp2;
		r[2] = s * tmp1 + c * tmp2;
	}
}

void Legacy_Rotate_Z(Engine::Math::AffineTransform3 &m, float s, float c) noexcept
{
	for (unsigned row = 0; row < 3; ++row) {
		float *r = m[row];
		const float tmp1 = r[0];
		const float tmp2 = r[1];
		r[0] = c * tmp1 + s * tmp2;
		r[1] = -s * tmp1 + c * tmp2;
	}
}

// Legacy Matrix3D::Look_At: origin at 'position', -Z towards 'target', rolled
// about the local Z axis by 'roll_radians'.
Engine::Math::AffineTransform3 Camera_Look_At(Engine::Math::Vector3 position,
	Engine::Math::Vector3 target, float roll_radians = 0.0F) noexcept
{
	const Engine::Math::Vector3 dir = (target - position).Normalized_Legacy();

	const float len2 = std::sqrt(dir.x * dir.x + dir.y * dir.y);
	const float sinp = dir.z;
	const float cosp = len2;
	float siny;
	float cosy;
	if (len2 != 0.0F) {
		siny = dir.y / len2;
		cosy = dir.x / len2;
	} else {
		siny = 0.0F;
		cosy = 1.0F;
	}

	Engine::Math::AffineTransform3 transform;
	transform.elements = {
		0.0F, 0.0F, -1.0F, position.x,
		-1.0F, 0.0F, 0.0F, position.y,
		0.0F, 1.0F, 0.0F, position.z};

	Legacy_Rotate_Y(transform, siny, cosy);
	Legacy_Rotate_X(transform, sinp, cosp);
	Legacy_Rotate_Z(transform, std::sin(-roll_radians), std::cos(-roll_radians));
	return transform;
}

// Legacy axis lock: Build_Matrix3D(rotation), Get_X/Y/Z_Rotation (atan2 of the
// matrix terms), Rotate_X/Y/Z of an identity matrix, Build_Quaternion.
Engine::Math::Quaternion Lock_Rotation_To_Axis(const Engine::Math::Quaternion &rotation, int axis) noexcept
{
	const Engine::Math::AffineTransform3 matrix = rotation.To_Rotation_Transform();
	Engine::Math::AffineTransform3 locked;
	if (axis == 0) {
		const float angle = static_cast<float>(std::atan2(matrix[2][1], matrix[1][1]));
		Legacy_Rotate_X(locked, std::sin(angle), std::cos(angle));
	} else if (axis == 1) {
		const float angle = static_cast<float>(std::atan2(matrix[0][2], matrix[2][2]));
		Legacy_Rotate_Y(locked, std::sin(angle), std::cos(angle));
	} else {
		const float angle = static_cast<float>(std::atan2(matrix[1][0], matrix[0][0]));
		Legacy_Rotate_Z(locked, std::sin(angle), std::cos(angle));
	}
	return Engine::Math::Quaternion::From_Rotation(locked);
}

// Legacy Matrix3D::Is_Orthogonal.
bool Is_Orthogonal(const Engine::Math::AffineTransform3 &m) noexcept
{
	constexpr float epsilon = 0.0001F;
	const Engine::Math::Vector3 x{m[0][0], m[0][1], m[0][2]};
	const Engine::Math::Vector3 y{m[1][0], m[1][1], m[1][2]};
	const Engine::Math::Vector3 z{m[2][0], m[2][1], m[2][2]};

	if (x.Dot(y) > epsilon) return false;
	if (y.Dot(z) > epsilon) return false;
	if (z.Dot(x) > epsilon) return false;

	if (std::fabs(x.Length_Squared() - 1.0F) > epsilon) return false;
	if (std::fabs(y.Length_Squared() - 1.0F) > epsilon) return false;
	if (std::fabs(z.Length_Squared() - 1.0F) > epsilon) return false;

	return true;
}

// Legacy Matrix3D::Re_Orthogonalize (the translation is kept unless the basis
// degenerates, in which case the whole matrix becomes identity).
void Re_Orthogonalize(Engine::Math::AffineTransform3 &m) noexcept
{
	constexpr float epsilon = 0.0001F;
	Engine::Math::Vector3 x{m[0][0], m[0][1], m[0][2]};
	Engine::Math::Vector3 y{m[1][0], m[1][1], m[1][2]};
	Engine::Math::Vector3 z = x.Cross(y);
	y = z.Cross(x);

	float len = x.Length();
	if (len < epsilon) { m = Engine::Math::AffineTransform3::Identity(); return; }
	x *= 1.0F / len;

	len = y.Length();
	if (len < epsilon) { m = Engine::Math::AffineTransform3::Identity(); return; }
	y *= 1.0F / len;

	len = z.Length();
	if (len < epsilon) { m = Engine::Math::AffineTransform3::Identity(); return; }
	z *= 1.0F / len;

	m[0][0] = x.x; m[0][1] = x.y; m[0][2] = x.z;
	m[1][0] = y.x; m[1][1] = y.y; m[1][2] = y.z;
	m[2][0] = z.x; m[2][1] = z.y; m[2][2] = z.z;
}
}


IMPLEMENT_DYNCREATE(CGraphicView, CView)


////////////////////////////////////////////////////////////////////////////
//
//  CGraphicView
//
////////////////////////////////////////////////////////////////////////////
CGraphicView::CGraphicView ()
    : m_bInitialized (FALSE),
      m_TimerID (0),
      m_bMouseDown (FALSE),
      m_bRMouseDown (FALSE),
      m_bActive (TRUE),
      m_animationSpeed (1.0F),
      m_dwLastFrameUpdate (0),
		m_iWindowed (1),
      m_animationState (AnimInvalid),
      m_objectRotation (NoRotation),
		m_LightRotation (NoRotation),
		m_bLightMeshInScene (false),
		m_ParticleCountUpdate (0),
		m_CameraBonePosX (false),
		m_UpdateCounter (0),
      m_allowedCameraRotation (FreeRotation),
		m_ObjectCenter {0.0f, 0.0f, 0.0f}
{
    // Get the windowed mode from the registry
    CString string_windowed = theApp.GetProfileString ("Config", "Windowed", "1");
	 m_iWindowed = ::atoi ((LPCTSTR)string_windowed);
}


////////////////////////////////////////////////////////////////////////////
//
//  ~CGraphicView
//
////////////////////////////////////////////////////////////////////////////
CGraphicView::~CGraphicView ()
{
}


BEGIN_MESSAGE_MAP(CGraphicView, CView)
	//{{AFX_MSG_MAP(CGraphicView)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_RBUTTONUP()
	ON_WM_RBUTTONDOWN()
	ON_WM_GETMINMAXINFO()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()



////////////////////////////////////////////////////////////////////////////
//
//  OnDraw
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnDraw (CDC* pDC)
{
	// Get the document to display
    CW3DViewDoc* doc = (CW3DViewDoc *)GetDocument();

    // Are we in a valid state?
    if (!pDC->IsPrinting ())
    {
    }
}


#ifdef RTS_DEBUG
void CGraphicView::AssertValid() const
{
	CView::AssertValid();
}

void CGraphicView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}
#endif //RTS_DEBUG



////////////////////////////////////////////////////////////////////////////
//
//  PreCreateWindow
//
////////////////////////////////////////////////////////////////////////////
int
CGraphicView::OnCreate (LPCREATESTRUCT lpCreateStruct)
{
	// Allow the base class to process this message
    if (CView::OnCreate(lpCreateStruct) == -1)
		return -1;

    m_dwLastFrameUpdate = timeGetTime ();//::GetTickCount ();
	return 0;
}


////////////////////////////////////////////////////////////////////////////
//
//  InitializeGraphicView
//
////////////////////////////////////////////////////////////////////////////
BOOL
CGraphicView::InitializeGraphicView ()
{
	// Assume failure
	BOOL bReturn = FALSE;
	if (g_iDeviceIndex < 0) {
		return FALSE;
	}

	m_bInitialized = FALSE;

	// Initialize the rendering engine with the information from
	// this window.
	RECT rect;
	GetClientRect (&rect);

	int cx = rect.right-rect.left;
	int cy = rect.bottom-rect.top;
	if (m_iWindowed == 0) {
		cx = g_iWidth;
		cy = g_iHeight;
		((CW3DViewDoc *)GetDocument())->Show_Cursor (true);
	} else {
		((CW3DViewDoc *)GetDocument())->Show_Cursor (false);
	}

    if (Graphics::Shared_Frame_Device()) {
        bReturn = Graphics::Resize_Frame_Device(cx,cy,false);
    } else {
        Graphics::FrameDeviceOptions options;
        options.window = m_hWnd; options.width = cx; options.height = cy;
        options.backbuffer_format = Graphics::RHITextureFormat::BGRA8_UNorm;
        bReturn = Graphics::Initialize_Frame_Device(options);
    }

    ASSERT (bReturn);
    if (bReturn && (m_pCamera == nullptr))
    {
        // Instantiate a new camera class
	    m_pCamera.Assign_No_Add_Ref (new CameraClass ());
        bReturn = (m_pCamera != nullptr);

        // Were we successful in creating a camera?
        ASSERT (m_pCamera);
        if (m_pCamera)
        {
            // Create a transformation matrix
            const auto transform = Engine::Math::AffineTransform3::From_Translation({0.0F, 0.0F, 35.0F});

	        // Point the camera in this direction (I think)
            m_pCamera->Set_Transform (To_W3D(transform));
        }

		  //
		  //	Attach the 'listener' to the camera
		  //
		  WWAudioClass::Get_Instance ()->Get_Sound_Scene ()->Attach_Listener_To_Obj (m_pCamera.Peek());
    }

	Reset_FOV ();

	 if (m_pLightMesh == nullptr)
	 {
		ResourceFileClass light_mesh_file (nullptr, "Light.w3d");
		WW3DAssetManager::Get_Instance()->Load_3D_Assets (light_mesh_file);

		m_pLightMesh.Assign_No_Add_Ref (WW3DAssetManager::Get_Instance()->Create_Render_Obj ("LIGHT"));
		ASSERT (m_pLightMesh != nullptr);
		m_bLightMeshInScene = false;
	 }


    // Remember whether or not we are initialized
    m_bInitialized = bReturn;

    if (m_bInitialized && (m_TimerID == 0))
    {
		// Kick off a timer that we can use to update
		// the display (kinda like a game loop iterator)
		TIMECAPS caps = { 0 };
		::timeGetDevCaps (&caps, sizeof (TIMECAPS));
		UINT freq = max (caps.wPeriodMin, 16U);
		m_TimerID = (UINT)::timeSetEvent (freq,
													 freq,
													 fnTimerCallback,
													 (DWORD_PTR)m_hWnd,
													 TIME_PERIODIC);
    }

	// Return the TRUE/FALSE result code
	return bReturn;
}


////////////////////////////////////////////////////////////////////////////
//
//  OnSize
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnSize
(
    UINT nType,
    int cx,
    int cy
)
{
	// Allow the base class to process this message
    CView::OnSize (nType, cx, cy);

	if (m_bInitialized) {

		if (m_iWindowed == 0) {
			cx = g_iWidth;
			cy = g_iHeight;
		}

		// Change the resolution of the rendering device to
		// match that of the view's current dimensions
		if (m_iWindowed == 1) {
			Graphics::Resize_Frame_Device(cx, cy, false);
		}

		// Force a repaint of the screen
		Reset_FOV ();
		RepaintView ();
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnDestroy
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnDestroy ()
{
	// Allow the base class to process this message
	CView::OnDestroy ();

	//
	//	Remove the listener from the camera
	//
	WWAudioClass::Get_Instance ()->Get_Sound_Scene ()->Attach_Listener_To_Obj (nullptr);

	//
	// Free the camera object
	//
	m_pCamera.Clear();
	m_pLightMesh.Clear();

	// Is there an update thread running?
	if (m_TimerID == 0) {

		// Stop the timer
		::timeKillEvent ((UINT)m_TimerID);
		m_TimerID = 0;
	}

	// Cache this information in the registry
	TCHAR temp_string[10];
	::itoa (m_iWindowed, temp_string, 10);
	theApp.WriteProfileString ("Config", "Windowed", temp_string);

	// We are no longer initialized
	m_bInitialized = FALSE;
}


////////////////////////////////////////////////////////////////////////////
//
//  OnInitialUpdate
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnInitialUpdate ()
{
	// Allow the base class to process this message
    CView::OnInitialUpdate ();

	CW3DViewDoc* doc = (CW3DViewDoc *)GetDocument();
	if (doc)
	{
		// Ask the document to initialize the scene (if it hasn't
		// already done so)
		doc->InitScene ();
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Set_Lowest_LOD
//
////////////////////////////////////////////////////////////////////////////
void
Set_Lowest_LOD (RenderObjClass *render_obj)
{
	if (render_obj != nullptr) {
		for (int index = 0; index < render_obj->Get_Num_Sub_Objects (); index ++) {
			RenderObjClass *psub_obj = render_obj->Get_Sub_Object (index);
			if (psub_obj != nullptr) {
				Set_Lowest_LOD (psub_obj);
			}
			REF_PTR_RELEASE (psub_obj);
		}

		//
		// Switcht this LOD to its lowest level
		//
		if (render_obj->Class_ID () == RenderObjClass::CLASSID_HLOD) {
			((HLodClass *)render_obj)->Set_LOD_Level (0);
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Allow_Update
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Allow_Update (bool onoff)
{
	if (onoff) {
		m_UpdateCounter --;
	} else {
		m_UpdateCounter ++;
	}
}

////////////////////////////////////////////////////////////////////////////
//
//  RepaintView
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::RepaintView
(
	BOOL bUpdateAnimation,
	DWORD ticks_to_use
)
{
	//
	//	Simple check to avoid re-entrance
	//
	static bool _already_painting = false;
	if (_already_painting)
		return;
	_already_painting = true;

	 //
	 // Are we in a valid state?
	 //
	 CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument();
	 if (doc->Is_Initialized () && doc->GetScene () && m_UpdateCounter == 0) {

		// Only update the frame if the animation is
		// supposed to be playing
		int cur_ticks = timeGetTime();
		int ticks_elapsed = cur_ticks - m_dwLastFrameUpdate;
		m_dwLastFrameUpdate = cur_ticks;

		// Update the W3D frame times according to our elapsed tick count
		if (ticks_to_use == 0)
		{
			WW3D::Update_Logic_Frame_Time(ticks_elapsed * m_animationSpeed);
			WW3D::Sync(WW3D::Get_Fractional_Sync_Milliseconds() >= WWSyncMilliseconds);
		}
		else
		{
			WW3D::Update_Logic_Frame_Time(ticks_to_use);
			WW3D::Sync(true);
		}

		// Do we need to update the current animation?
		if ((m_animationState == AnimPlaying) &&
			bUpdateAnimation)
		{
			float animationSpeed = ((float)ticks_elapsed) / 1000.00F;
			animationSpeed = (animationSpeed * m_animationSpeed);
			doc->UpdateFrame (animationSpeed);
		}

		// Perform the object rotation if necessary
		if ((m_objectRotation != NoRotation) &&
			(bUpdateAnimation == TRUE))
		{
			Rotate_Object ();
		}

		// Perform the light rotation if necessary
		if ((m_LightRotation != NoRotation) &&
			(bUpdateAnimation == TRUE))
		{
			Rotate_Light ();
		}

		// Reset the current lod to be the lowest possible LOD...
		RenderObjClass *prender_obj = doc->GetDisplayedObject ();
		if ((prender_obj != nullptr) &&
			 (doc->GetScene ()->Are_LODs_Switching ()))
		{
			Set_Lowest_LOD (prender_obj);
		}

		// Update the metal map
		// assuming object is at origin
		MetalMapManagerClass *metal=_TheAssetMgr->Peek_Metal_Map_Manager();
		if (metal)
		{
			LightClass *pscene_light = doc->GetSceneLight();
			Vector3 ambient,diffuse,l,v;
			const auto &scene_ambient = doc->GetScene()->Get_Ambient_Light();
			ambient.Set(scene_ambient.x, scene_ambient.y, scene_ambient.z);
			pscene_light->Get_Diffuse(&diffuse);
			l=pscene_light->Get_Position();
			l.Normalize();
			v=m_pCamera->Get_Position();
			v.Normalize();
			metal->Update_Lighting(ambient,diffuse,l,v);
			metal->Update_Textures();
		}

		//
		//	Render the background BMP
		//
        if (!Graphics::Begin_Tool_Frame()) return;
        if (WW3D::Begin_Render(TRUE, TRUE, doc->GetBackgroundColor()) != WW3D_ERROR_OK) {
            Graphics::Abort_Tool_Frame();
            return;
        }

		WW3D::Render (doc->Get2DScene (), doc->Get2DCamera (), FALSE, FALSE);

		//
		// Render the background scene
		//
		if (doc->GetBackgroundObjectName ().GetLength () > 0) {
			WW3D::Render (doc->GetBackObjectScene (), doc->GetBackObjectCamera (), FALSE, FALSE);
		}

		//
		// Render the main scene
		//
		DWORD pt_high = 0L;

		// Wait for all previous rendering to complete before starting benchmark.
		DWORD profile_time = ::Get_CPU_Clock (pt_high);

		WW3D::Render (doc->GetScene (), m_pCamera.Peek(), FALSE, FALSE);

		// Wait for all rendering to complete before stopping benchmark.
		DWORD milliseconds = (::Get_CPU_Clock (pt_high) - profile_time) / 1000;

		//
		// Render the cursor
		//
		WW3D::Render (doc->GetCursorScene (), doc->Get2DCamera (), FALSE, FALSE);

		// Render the dazzles
		doc->Render_Dazzles(m_pCamera.Peek());

        // Finish out the rendering process
        WW3D::End_Render();
        if (!Graphics::End_Tool_Frame()) engine::debug::log_info("Viewer frame submission failed.\n");


		//
		//	Let the audio class think
		//
		WWAudioClass::Get_Instance ()->On_Frame_Update (WW3D::Get_Logic_Frame_Time_Milliseconds());

		//
		//	Update the count of particles and polys in the status bar
		//
		if ((cur_ticks - m_ParticleCountUpdate > 250)) {
			m_ParticleCountUpdate = cur_ticks;
			doc->Update_Particle_Count ();

			int polys = (prender_obj != nullptr) ? prender_obj->Get_Num_Polys () : 0;
			((CMainFrame *)::AfxGetMainWnd ())->UpdatePolygonCount (polys);
		}

		//
		//	Update the frame time in the status bar
		//
		((CMainFrame *)::AfxGetMainWnd ())->Update_Frame_Time (milliseconds);
	}

	_already_painting = false;
}


////////////////////////////////////////////////////////////////////////////
//
//  UpdateDisplay
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::UpdateDisplay ()
{
	// Get the document to display
    CW3DViewDoc* doc = (CW3DViewDoc *)GetDocument();

    // Are we in a valid state?
    /*if (m_bInitialized && doc->GetScene ())
    {
        RenderObjClass *pCRenderObj = doc->GetDisplayedObject ();
        if (pCRenderObj)
        {
            Matrix3D transform = pCRenderObj->Get_Transform ();
            transform.Rotate_X (0.05F);
            transform.Rotate_Y (0.05F);
            transform.Rotate_Z (0.05F);

            pCRenderObj->Set_Transform (transform);
        }

		// Render the current view inside the frame
        WW3D::Begin_Render (TRUE, TRUE, Vector3 (0.2,0.4,0.6));
		WW3D::Render (doc->GetScene (), m_pCamera.Peek(), FALSE, FALSE);
		WW3D::End_Render ();
    } */
}


////////////////////////////////////////////////////////////////////////////
//
//  WindowProc
//
////////////////////////////////////////////////////////////////////////////
LRESULT
CGraphicView::WindowProc
(
    UINT message,
    WPARAM wParam,
    LPARAM lParam
)
{
	// Is this the repaint message we are expecting?
	if (message == WM_USER+101) {

		//
		//	Force the repaint...
		//
		RepaintView ();
		RemoveProp (m_hWnd, "WaitingToProcess");

	} else if (message == WM_PAINT) {

		// If we are in fullscreen mode, then erase the window background
		if (m_iWindowed == 0) {

			// Get the client rectangle of the window
			RECT rect;
			GetClientRect (&rect);

			// Get the window's DC
			HDC hDC = ::GetDC (m_hWnd);
			if (hDC) {

				// Erase the background
				::FillRect (hDC, &rect, (HBRUSH)(COLOR_WINDOW + 1));
				::ReleaseDC (m_hWnd, hDC);
			}
		}

		RepaintView (FALSE);
		ValidateRect (nullptr);
		return 0;

	} else if (message == WM_KEYDOWN) {

		if ((wParam == VK_CONTROL) && (m_bLightMeshInScene == false)) {
			CW3DViewDoc* doc = (CW3DViewDoc *)GetDocument();
			m_pLightMesh->Add (doc->GetScene ());
			m_bLightMeshInScene = true;
		}

	} else if (message == WM_KEYUP) {

		if ((wParam == VK_CONTROL) && (m_bLightMeshInScene == true)) {
			CW3DViewDoc* doc = (CW3DViewDoc *)GetDocument();
			m_pLightMesh->Remove ();
			m_bLightMeshInScene = false;
		}
	}

	// Allow the base class to process this message
	return CView::WindowProc(message, wParam, lParam);
}


////////////////////////////////////////////////////////////////////////////
//
//  fnTimerCallback
//
////////////////////////////////////////////////////////////////////////////
void CALLBACK
fnTimerCallback
(
	UINT uID,
	UINT uMsg,
	DWORD_PTR dwUser,
	DWORD_PTR dw1,
	DWORD_PTR dw2
)
{
	HWND hwnd = (HWND)dwUser;
	if (hwnd != nullptr) {

		// Send this event off to the view to process (hackish, but fine for now)
		if ((GetProp (hwnd, "WaitingToProcess") == nullptr) &&
			 (GetProp (hwnd, "Inactive") == nullptr)) {

			SetProp (hwnd, "WaitingToProcess", (HANDLE)1);

			// Send the message to the view so it will be in the
			// same thread (Surrender doesn't seem to be thread-safe)
			::PostMessage (hwnd, WM_USER + 101, 0, 0L);
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLButtonDown
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnLButtonDown
(
    UINT nFlags,
    CPoint point
)
{
	// Capture all mouse messages
	SetCapture ();

	// Mouse button is down
	m_bMouseDown = TRUE;
	m_lastPoint = point;

	if (m_bRMouseDown) {
		::SetCursor (::LoadCursor (::AfxGetResourceHandle (), MAKEINTRESOURCE (IDC_CURSOR_GRAB)));
		((CW3DViewDoc *)GetDocument())->Set_Cursor ("grab.tga");
	} else {
		((CW3DViewDoc *)GetDocument())->Set_Cursor ("orbit.tga");
	}

	CView::OnLButtonDown (nFlags, point);
}


////////////////////////////////////////////////////////////////////////////
//
//  OnLButtonUp
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnLButtonUp
(
    UINT nFlags,
    CPoint point
)
{
    if (!m_bRMouseDown)
    {
        // Release the mouse capture
        ReleaseCapture ();
    }

    // Mouse button is up
    m_bMouseDown = FALSE;

    if (m_bRMouseDown == TRUE)
    {
        ::SetCursor (::LoadCursor (::AfxGetResourceHandle (), MAKEINTRESOURCE (IDC_CURSOR_ZOOM)));
		  ((CW3DViewDoc *)GetDocument())->Set_Cursor ("zoom.tga");
    }
    else
    {
        ::SetCursor (::LoadCursor (nullptr, MAKEINTRESOURCE (IDC_ARROW)));
		  ((CW3DViewDoc *)GetDocument())->Set_Cursor ("cursor.tga");
    }

	// Allow the base class to process this message
    CView::OnLButtonUp (nFlags, point);
}

float minZoomAdjust = 0.0F;
Engine::Math::Vector3 sphereCenter;
Engine::Math::Quaternion rotation;


////////////////////////////////////////////////////////////////////////////
//
//  OnMouseMove
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnMouseMove
(
    UINT nFlags,
    CPoint point
)
{
	int iDeltaX = m_lastPoint.x-point.x;
	int iDeltaY = m_lastPoint.y-point.y;

	// Get the document to display
	CW3DViewDoc* doc = (CW3DViewDoc *)GetDocument();

	if (!(nFlags & MK_CONTROL) && m_bLightMeshInScene) {
		m_pLightMesh->Remove ();
		m_bLightMeshInScene = false;
	} else if ((nFlags & MK_CONTROL) && (m_bLightMeshInScene == false)) {
		m_pLightMesh->Add (doc->GetScene ());
		m_bLightMeshInScene = true;
	}

	// Is the mouse button down?
	if (m_bMouseDown && m_bRMouseDown)
	{
		// Get the transformation matrix for the camera and its inverse
		Engine::Math::AffineTransform3 transform = To_Core_Math(m_pCamera->Get_Transform());

		RECT rect;
		GetClientRect (&rect);

		float midPointX = float(rect.right >> 1);
		float midPointY = float(rect.bottom >> 1);

		float lastPointX = ((float)m_lastPoint.x - midPointX) / midPointX;
		float lastPointY = (midPointY - (float)m_lastPoint.y) / midPointY;

		float pointX = ((float)point.x - midPointX) / midPointX;
		float pointY = (midPointY - (float)point.y) / midPointY;


		Engine::Math::Vector3 cameraPan{-m_CameraDistance * (pointX - lastPointX),
			-m_CameraDistance * (pointY - lastPointY), 0.0F};

		transform.Adjust_Translation(transform.Transform_Vector(cameraPan));

		Engine::Math::Vector3 move = rotation.Rotate_Vector(cameraPan);
		sphereCenter += move;

		// Move the camera back to get a good view of the object
		m_pCamera->Set_Transform (To_W3D(transform));

		m_lastPoint = point;
	}
	// Is the mouse button down?
	else if ((nFlags & MK_CONTROL) && m_bMouseDown)
	{
		LightClass *pSceneLight = doc->GetSceneLight ();
		if ((pSceneLight != nullptr) && (m_pLightMesh != nullptr))
		{
			RECT rect;
			GetClientRect (&rect);

			float midPointX = float(rect.right >> 1);
			float midPointY = float(rect.bottom >> 1);

			float lastPointX = ((float)m_lastPoint.x - midPointX) / midPointX;
			float lastPointY = (midPointY - (float)m_lastPoint.y) / midPointY;

			float pointX = ((float)point.x - midPointX) / midPointX;
			float pointY = (midPointY - (float)point.y) / midPointY;

			Engine::Math::Quaternion mouse_motion = Engine::Math::Quaternion::Trackball_Drag(
				{lastPointX, lastPointY}, {pointX, pointY}, 0.8F).Conjugate();
			Engine::Math::Quaternion light_orientation;
			Engine::Math::Quaternion camera = Quaternion_From_W3D(m_pCamera->Get_Transform());
			Engine::Math::Quaternion cur_light = Quaternion_From_W3D(pSceneLight->Get_Transform());

			light_orientation = camera;
			light_orientation = light_orientation * mouse_motion;
			light_orientation = light_orientation * camera.Conjugate();
			light_orientation = light_orientation * cur_light;
			light_orientation = light_orientation.Normalized();

			// Matrix3D::Inverse_Transform_Vector (orthogonal inverse)
			const Engine::Math::AffineTransform3 light_transform = To_Core_Math(pSceneLight->Get_Transform());
			const Engine::Math::Vector3 to_center =
				light_transform.Orthogonal_Inverse().Transform_Point(sphereCenter);

			Engine::Math::AffineTransform3 light_tm = light_orientation.To_Rotation_Transform();
			light_tm.Set_Translation(sphereCenter);
			light_tm.Adjust_Translation(light_tm.Transform_Vector(-to_center));

			m_pLightMesh->Set_Transform(To_W3D(light_tm));
			pSceneLight->Set_Transform(To_W3D(light_tm));
		}

		m_lastPoint = point;
	}
	// Is the mouse button down?
	else if ((nFlags & MK_CONTROL) && m_bRMouseDown)
	{
		// Get the currently displayed object
		CW3DViewDoc *doc= (CW3DViewDoc *)GetDocument();
		LightClass *pscene_light = doc->GetSceneLight ();
		RenderObjClass *prender_obj = doc->GetDisplayedObject ();
		if ((pscene_light != nullptr) && (prender_obj != nullptr)) {

			// Calculate a light adjustment factor
			CRect rect;
			GetClientRect (&rect);
			float deltay = (float(iDeltaY))/(float(rect.bottom - rect.top));
	float adjustment = deltay * (m_ViewedSphere.radius * 3.0F);

			// Determine the light's new position based on this factor
			Engine::Math::AffineTransform3 transform = To_Core_Math(pscene_light->Get_Transform());
			transform.Adjust_Translation(transform.Transform_Vector({0, 0, adjustment}));

			// Determine what the distance from the light to the object
			// would be with this new position
			const Engine::Math::Vector3 light_pos = transform.Translation();
			const Engine::Math::Vector3 obj_pos = To_Core_Math(prender_obj->Get_Position());
			float distance = (light_pos - obj_pos).Length ();

			// If the new position is acceptable, move the light
			if (distance > m_ViewedSphere.radius) {
				m_pLightMesh->Set_Transform (To_W3D(transform));
				pscene_light->Set_Transform (To_W3D(transform));
			}
		}

		m_lastPoint = point;
	}
	// Is the mouse button down?
	else if (m_bMouseDown)
	{
		// Get the document to display
		CW3DViewDoc* doc = (CW3DViewDoc *)GetDocument();

		// Are we in a valid state?
		if (m_bInitialized && doc->GetScene ())
		{
			RenderObjClass *pCRenderObj = doc->GetDisplayedObject ();
			if (pCRenderObj)
			{
				RECT rect;
				GetClientRect (&rect);

				float midPointX = float(rect.right >> 1);
				float midPointY = float(rect.bottom >> 1);

				float lastPointX = ((float)m_lastPoint.x - midPointX) / midPointX;
				float lastPointY = (midPointY - (float)m_lastPoint.y) / midPointY;

				float pointX = ((float)point.x - midPointX) / midPointX;
				float pointY = (midPointY - (float)point.y) / midPointY;

				// Rotate around the object (orbit) using a 0.00F - 1.00F percentage of
				// the mouse coordinates
				rotation = Engine::Math::Quaternion::Trackball_Drag(
					{lastPointX, lastPointY}, {pointX, pointY}, 0.8F);

				// Do we want to 'lock-out' all rotation except X?
				if (m_allowedCameraRotation == OnlyRotateX)
				{
					rotation = Lock_Rotation_To_Axis(rotation, 0);
				}
				// Do we want to 'lock-out' all rotation except Y?
				else if (m_allowedCameraRotation == OnlyRotateY)
				{
					rotation = Lock_Rotation_To_Axis(rotation, 1);
				}
				// Do we want to 'lock-out' all rotation except Z?
				else if (m_allowedCameraRotation == OnlyRotateZ)
				{
					rotation = Lock_Rotation_To_Axis(rotation, 2);
				}

				// Get the transformation matrix for the camera and its inverse
				Engine::Math::AffineTransform3 transform = To_Core_Math(m_pCamera->Get_Transform());
				const Engine::Math::AffineTransform3 inverseMatrix = transform.Orthogonal_Inverse();
				const Engine::Math::Vector3 to_object = inverseMatrix.Transform_Point(sphereCenter);

				transform.Adjust_Translation(transform.Transform_Vector(to_object));
				transform.Post_Apply_Rotation(rotation.To_Rotation_Transform());
				transform.Adjust_Translation(transform.Transform_Vector(-to_object));

				// Rotate and translate the camera
				m_pCamera->Set_Transform (To_W3D(transform));

				doc->GetBackObjectCamera ()->Set_Transform (To_W3D(transform));
				doc->GetBackObjectCamera ()->Set_Position (Vector3 (0.00F, 0.00F, 0.00F));
			}
		}

		m_lastPoint = point;
	}
	else if (m_bRMouseDown)
	{
		m_lastPoint = point;

		// Get the transformation matrix for the camera and its inverse
		Engine::Math::AffineTransform3 transform = To_Core_Math(m_pCamera->Get_Transform());
		if (iDeltaY != 0)
		{

			// Get the bouding rectangle of the main view
			CRect rect;
			GetClientRect (&rect);

			float deltay = (float(iDeltaY))/(float(rect.bottom - rect.top));
			float adjustment = deltay * m_CameraDistance * 3.0F;

			if ((adjustment < minZoomAdjust) && (adjustment >= 0.00F))
			{
				 adjustment = minZoomAdjust;
			}

			if ((adjustment > -minZoomAdjust) && (adjustment <= 0.00F))
			{
				 adjustment = -minZoomAdjust;
			}

			if ((m_CameraDistance + adjustment) > 0.00F)
			{
				m_CameraDistance += adjustment;
				transform.Adjust_Translation(transform.Transform_Vector({0.0F, 0.0F, adjustment}));

				// Move the camera back to get a good view of the object
				m_pCamera->Set_Transform (To_W3D(transform));

				// Get the main window of our app
				CMainFrame *pCMainWnd = (CMainFrame *)::AfxGetMainWnd ();
				if (pCMainWnd != nullptr)
				{
					// Ensure the background camera matches the main camera
					CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument();
					doc->GetBackObjectCamera ()->Set_Transform (To_W3D(transform));
					doc->GetBackObjectCamera ()->Set_Position (Vector3 (0.00F, 0.00F, 0.00F));

					// Update the current object if necessary
					RenderObjClass *prender_obj = doc->GetDisplayedObject ();
					if (prender_obj != nullptr) {

						// Ensure the status bar is updated with the correct poly count
						pCMainWnd->UpdatePolygonCount (prender_obj->Get_Num_Polys ());
					}

					// Ensure the status bar is updated with the correct camera distance
					pCMainWnd->UpdateCameraDistance (m_CameraDistance);
				}

			}
		}

		m_lastPoint = point;
	}

	// Allow the base class to process this message
	CView::OnMouseMove (nFlags, point);
}


////////////////////////////////////////////////////////////////////////////
//
//  Reset_Camera_To_Display_Emitter
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Reset_Camera_To_Display_Emitter (ParticleEmitterClass &emitter)
{
	// Get some of the emitter settings
	const Engine::Math::Vector3 velocity = To_Core_Math(emitter.Get_Start_Velocity());
	const Engine::Math::Vector3 acceleration = To_Core_Math(emitter.Get_Acceleration());
	float lifetime = emitter.Get_Lifetime ();

	// Determine what the max extent covered by a particle will be.
	Engine::Math::Vector3 distance = (velocity * lifetime) + ((acceleration * (lifetime * lifetime)) / 2.0F);

	// Do we need to take into account acceleration?
	Engine::Math::Vector3 distance_maxima{};
	if ((acceleration.x != 0) || (acceleration.y != 0) || (acceleration.z != 0)) {

		// Determine at what time (for each x,y,z) a maxima will occur.
		Engine::Math::Vector3 time_max{};
		time_max.x = (acceleration.x != 0) ? ((-velocity.x) / acceleration.x) : 0.00F;
		time_max.y = (acceleration.y != 0) ? ((-velocity.y) / acceleration.y) : 0.00F;
		time_max.z = (acceleration.z != 0) ? ((-velocity.z) / acceleration.z) : 0.00F;

		// Is there a maxima for the X direction?
		if ((time_max.x >= 0.0F) && (time_max.x < lifetime)) {
			distance_maxima.x = (velocity.x * time_max.x) + ((acceleration.x * (time_max.x * time_max.x)) / 2.0F);
			distance_maxima.x = std::abs(distance_maxima.x);
		}

		// Is there a maxima for the Y direction?
		if ((time_max.y >= 0.0F) && (time_max.y < lifetime)) {
			distance_maxima.y = (velocity.y * time_max.y) + ((acceleration.y * (time_max.y * time_max.y)) / 2.0F);
			distance_maxima.y = std::abs(distance_maxima.y);
		}

		// Is there a maxima for the Z direction?
		if ((time_max.z >= 0.0F) && (time_max.z < lifetime)) {
			distance_maxima.z = (velocity.z * time_max.z) + ((acceleration.z * (time_max.z * time_max.z)) / 2.0F);
			distance_maxima.z = std::abs(distance_maxima.z);
		}
	}

	distance.x = std::abs(distance.x);
	distance.y = std::abs(distance.y);
	distance.z = std::abs(distance.z);

	// Determine what the maximum distance convered in a single direction is
	float max_dist = (std::max)(distance.x, distance.y);
	max_dist = (std::max)(max_dist, distance.z);
	max_dist = (std::max)(max_dist, distance_maxima.x);
	max_dist = (std::max)(max_dist, distance_maxima.y);
	max_dist = (std::max)(max_dist, distance_maxima.z);

	Engine::Math::Vector3 center = distance / 2.00F;
	center.x = (std::max)(center.x, distance_maxima.x / 2.00F);
	center.y = (std::max)(center.y, distance_maxima.y / 2.00F);
	center.z = (std::max)(center.z, distance_maxima.z / 2.00F);

	// Build a logical sphere from the emitters settings
	// that should provide a good viewing distance for the emitter.
	Engine::Math::Sphere3 sphere;
	sphere.center = center;
	sphere.radius = (std::max)(emitter.Get_Particle_Size () * 5, (max_dist * 3.0F) / 5.0F);

	// View this sphere
	Reset_Camera_To_Display_Sphere (sphere);
}


////////////////////////////////////////////////////////////////////////////
//
//  Reset_Camera_To_Display_Sphere
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Reset_Camera_To_Display_Sphere (const Engine::Math::Sphere3 &sphere)
{
	// Calculate a default camera distance to view this sphere
	m_CameraDistance = sphere.radius * 3.00F;
	m_CameraDistance = (m_CameraDistance < 1.0F) ? 1.0F : m_CameraDistance;

	// Calculate a transform that is the appropriate distance
	// from the sphere center and is looking at the center
	Engine::Math::AffineTransform3 transform = Camera_Look_At(
		sphere.center + Engine::Math::Vector3{m_CameraDistance, 0, 0}, sphere.center);

	// Record some variables for later use
	sphereCenter	= sphere.center;
	m_ObjectCenter	= sphereCenter;
	minZoomAdjust	= m_CameraDistance / 190.0F;
	rotation			= Engine::Math::Quaternion::From_Rotation(transform);

	// Move the camera back to get a good view of the object
	m_pCamera->Set_Transform (To_W3D(transform));

	// Make the same adjustment for the scene light
	CW3DViewDoc* doc = (CW3DViewDoc *)GetDocument();
	LightClass *pSceneLight = doc->GetSceneLight ();
	if ((m_pLightMesh != nullptr) && (pSceneLight != nullptr)) {

		// Reposition the light and its 'mesh' as appropriate
		Engine::Math::AffineTransform3 light_transform = Engine::Math::AffineTransform3::From_Translation(sphereCenter);
		light_transform.Adjust_Translation({0, 0, 0.7F * m_CameraDistance});
		pSceneLight->Set_Transform (To_W3D(light_transform));
		m_pLightMesh->Set_Transform (To_W3D(light_transform));

		// Scale the light's mesh appropriately
		static float last_scale = 1.0F;
		m_pLightMesh->Scale (m_CameraDistance / (14 * last_scale));

		last_scale = m_CameraDistance / 14;
	}

	float max_dist = m_CameraDistance * 60.0F;
	float min_dist = max (0.2F, minZoomAdjust / 2);

	// Set the clipping planes so objects are clipped correctly
	if (doc->Are_Clip_Planes_Manual () == false) {
		m_pCamera->Set_Clip_Planes (min_dist, max_dist);

		// Adjust the fog near clipping plane to the new value, but
		// leave the far clip plane alone (since it is scene dependent
		// not camera dependent).
		float fog_near, fog_far;
		doc->GetScene()->Get_Fog_Range(&fog_near, &fog_far);
		doc->GetScene()->Set_Fog_Range(min_dist, fog_far);
		doc->GetScene()->Recalculate_Fog_Planes();
	}

	// Reset the background camera to match the main camera
	doc->GetBackObjectCamera ()->Set_Transform (To_W3D(transform));
	doc->GetBackObjectCamera ()->Set_Position (Vector3 (0.00F, 0.00F, 0.00F));

	// Update the camera distance in the status bar
	CMainFrame *pCMainWnd = (CMainFrame *)::AfxGetMainWnd ();
	if (pCMainWnd != nullptr) {
		pCMainWnd->UpdateCameraDistance (m_CameraDistance);
		pCMainWnd->UpdateFrameCount (0, 0, 0);
	}

	// Record the sphere we are viewing for later
	m_ViewedSphere = sphere;
}


////////////////////////////////////////////////////////////////////////////
//
//  Reset_Camera_To_Display_Object
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Reset_Camera_To_Display_Object (RenderObjClass &render_object)
{
	// Reset the camera to get a good look at this object's bounding sphere
	const auto render_sphere = render_object.Get_Bounding_Sphere ();
	Engine::Math::Sphere3 sp{To_Core_Math(render_sphere.Center), render_sphere.Radius};
	Reset_Camera_To_Display_Sphere (sp);

	// Should we update the camera's position as well?
	int index = render_object.Get_Bone_Index ("CAMERA");
	if (index > 0) {

		// Convert the bone's transform into a camera transform
		Engine::Math::AffineTransform3 transform = To_Core_Math(render_object.Get_Bone_Transform (index));
		if (m_CameraBonePosX) {
			const Engine::Math::AffineTransform3 camera_transform = Engine::Math::AffineTransform3::From_Basis(
				{0, -1, 0}, {0, 0, 1}, {-1, 0, 0});
			transform = Compose(transform, camera_transform);
		}

		// Pass the new transform onto the camera
		CameraClass *camera = GetCamera ();
		camera->Set_Transform (To_W3D(transform));
	}

	// Update the polygon count in the main window
	CMainFrame *pCMainWnd = (CMainFrame *)::AfxGetMainWnd ();
	if (pCMainWnd != nullptr) {
		pCMainWnd->UpdatePolygonCount (render_object.Get_Num_Polys ());
	}

	// Load the settings in the default.dat if its in the local directory.
	Load_Default_Dat ();
}


////////////////////////////////////////////////////////////////////////////
//
//  Load_Default_Dat
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Load_Default_Dat ()
{
	// Get the directory where this executable was run from
	TCHAR filename[MAX_PATH];
	::GetModuleFileName (nullptr, filename, sizeof (filename));

	// Strip the filename from the path
	LPTSTR ppath = ::strrchr (filename, '\\');
	if (ppath != nullptr) {
		ppath[0] = 0;
	}

	// Concat the default.dat filename onto the path
	strlcat(filename, "\\default.dat", ARRAY_SIZE(filename));

	// Does the file exist in the directory?
	if (::GetFileAttributes (filename) != 0xFFFFFFFF) {

		// Ask the document to load the settings from this data file
		CW3DViewDoc *pCDoc = (CW3DViewDoc *)GetDocument ();
		if (pCDoc != nullptr) {
			pCDoc->LoadSettings (filename);
		}
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  OnRButtonUp
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnRButtonUp
(
    UINT nFlags,
    CPoint point
)
{
	// Mouse button is up
	m_bRMouseDown = FALSE;

	if (m_bMouseDown) {
		((CW3DViewDoc *)GetDocument())->Set_Cursor ("orbit.tga");
	} else {
		::SetCursor (::LoadCursor (nullptr, MAKEINTRESOURCE (IDC_ARROW)));
		((CW3DViewDoc *)GetDocument())->Set_Cursor ("cursor.tga");
		ReleaseCapture ();
	}

	// Allow the base class to process this message
	CView::OnRButtonUp(nFlags, point);
}

////////////////////////////////////////////////////////////////////////////
//
//  OnRButtonDown
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnRButtonDown
(
    UINT nFlags,
    CPoint point
)
{
    // Capture all mouse messages
    SetCapture ();

    // Mouse button is down
    m_bRMouseDown = TRUE;
    m_lastPoint = point;

    if (m_bMouseDown)
    {
        ::SetCursor (::LoadCursor (::AfxGetResourceHandle (), MAKEINTRESOURCE (IDC_CURSOR_GRAB)));
		  ((CW3DViewDoc *)GetDocument())->Set_Cursor ("grab.tga");
    }
    else
    {
        ::SetCursor (::LoadCursor (::AfxGetResourceHandle (), MAKEINTRESOURCE (IDC_CURSOR_ZOOM)));
		  ((CW3DViewDoc *)GetDocument())->Set_Cursor ("zoom.tga");
    }

	// Allow the base class to process this message
    CView::OnRButtonDown(nFlags, point);
}


////////////////////////////////////////////////////////////////////////////
//
//  SetAnimationState
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::SetAnimationState (ANIMATION_STATE animationState)
{
    // Has the state changed?
    if (m_animationState != animationState)
    {
        switch (animationState)
        {
            // We want to stop the animation
            case AnimStopped:
            {
                // Get the document so we can get our current object
                CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument();
                ASSERT_VALID (doc);

                // Get the currently displayed object
                RenderObjClass *pCRenderObj = doc->GetDisplayedObject ();
                if (pCRenderObj)
                {
                    // Reset the animation to frame 0

                    if (doc->GetCurrentAnimation()) {
                    	pCRenderObj->Set_Animation (doc->GetCurrentAnimation (), 0);
                    }
                }

                // Reset the animation to frame 0
                doc->ResetAnimation ();
            }
            break;

            case AnimPlaying:
            {
					CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument ();
					doc->Play_Animation_Sound ();

                // Reset the frame timer
					 m_dwLastFrameUpdate = timeGetTime ();
            }
            break;
        }

        // Save the new state
        m_animationState = animationState;
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  SetCameraPos
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::SetCameraPos (CAMERA_POS cameraPos)
{
    // Get the document so we can get our current object
    CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument();
    ASSERT_VALID (doc);

    // Get the currently displayed object
    RenderObjClass *pCRenderObj = doc->GetDisplayedObject ();
    if (pCRenderObj)
    {
        Engine::Math::Sphere3 sphere = m_ViewedSphere;

        m_CameraDistance = sphere.radius * 3.00F;
        m_CameraDistance = (m_CameraDistance < 1.0F) ? 1.0F : m_CameraDistance;
        m_CameraDistance = (m_CameraDistance > 400.0F) ? 400.0F : m_CameraDistance;

		Engine::Math::AffineTransform3 transform;

        switch (cameraPos)
        {
            case CameraFront:
            {
				transform = Camera_Look_At(sphere.center + Engine::Math::Vector3{m_CameraDistance, 0, 0},
					sphere.center);
            }
            break;

            case CameraBack:
            {
				transform = Camera_Look_At(sphere.center + Engine::Math::Vector3{-m_CameraDistance, 0, 0},
					sphere.center);
            }
            break;

            case CameraLeft:
            {
				transform = Camera_Look_At(sphere.center + Engine::Math::Vector3{0, -m_CameraDistance, 0},
					sphere.center);
            }
            break;

            case CameraRight:
            {
				transform = Camera_Look_At(sphere.center + Engine::Math::Vector3{0, m_CameraDistance, 0},
					sphere.center);
            }
            break;

            case CameraTop:
            {
				transform = Camera_Look_At(sphere.center + Engine::Math::Vector3{0, 0, m_CameraDistance},
					sphere.center, 3.1415926535F);
            }
            break;

            case CameraBottom:
            {
				transform = Camera_Look_At(sphere.center + Engine::Math::Vector3{0, 0, -m_CameraDistance},
					sphere.center, 3.1415926535F);
            }
            break;
        }

	    // Move the camera back to get a good view of the object
	    m_pCamera->Set_Transform (To_W3D(transform));

        // Get the main window of our app
        CMainFrame *pCMainWnd = (CMainFrame *)::AfxGetMainWnd ();
        if (pCMainWnd != nullptr)
        {
            CW3DViewDoc* doc = (CW3DViewDoc *)GetDocument();

	    doc->GetBackObjectCamera ()->Set_Transform (To_W3D(transform));
            doc->GetBackObjectCamera ()->Set_Position (Vector3 (0.00F, 0.00F, 0.00F));

            RenderObjClass *pCRenderObj = doc->GetDisplayedObject ();
            if (pCRenderObj)
            {
                pCMainWnd->UpdatePolygonCount (pCRenderObj->Get_Num_Polys ());
            }

            pCMainWnd->UpdateCameraDistance(m_CameraDistance);
        }
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  RotateObject
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::RotateObject (OBJECT_ROTATION rotation)
{
    // Is this rotation different?
    if (m_objectRotation != rotation)
    {
        // Save the rotation state
        m_objectRotation = rotation;
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  SetAllowedCameraRotation
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::SetAllowedCameraRotation (CAMERA_ROTATION cameraRotation)
{
    // Store this for later reference
    m_allowedCameraRotation = cameraRotation;
}


////////////////////////////////////////////////////////////////////////////
//
//  ResetObject
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::ResetObject ()
{
    // Get the current document
    CW3DViewDoc *doc = ::GetCurrentDocument ();

    ASSERT (doc);
    if (doc)
    {
        // Get the currently displayed object
        RenderObjClass *pCRenderObj = doc->GetDisplayedObject ();
        if (pCRenderObj)
        {
            // Reset the rotation of the object
            pCRenderObj->Set_Transform (To_W3D(Engine::Math::AffineTransform3::Identity()));
        }
    }
}


////////////////////////////////////////////////////////////////////////////
//
//  OnGetMinMaxInfo
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::OnGetMinMaxInfo (MINMAXINFO FAR* lpMMI)
{
	CView::OnGetMinMaxInfo (lpMMI);
}


////////////////////////////////////////////////////////////////////////////
//
//  Rotate_Object
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Rotate_Object ()
{
	// Get the document to display
	CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument();

	// Get the currently displayed object
	RenderObjClass *prender_obj = doc->GetDisplayedObject ();
	if (prender_obj != nullptr)
	{
		// Get the current transform for the object
	Engine::Math::AffineTransform3 transform = To_Core_Math(prender_obj->Get_Transform());

		if ((m_objectRotation & RotateX) == RotateX) {
			transform.Post_Apply_Rotation(Engine::Math::AffineTransform3::Rotation_X(0.05F));
		} else if ((m_objectRotation & RotateXBack) == RotateXBack) {
			transform.Post_Apply_Rotation(Engine::Math::AffineTransform3::Rotation_X(-0.05F));
		}

		if ((m_objectRotation & RotateY) == RotateY) {
			transform.Post_Apply_Rotation(Engine::Math::AffineTransform3::Rotation_Y(-0.05F));
		} else if ((m_objectRotation & RotateYBack) == RotateYBack) {
			transform.Post_Apply_Rotation(Engine::Math::AffineTransform3::Rotation_Y(0.05F));
		}

		if ((m_objectRotation & RotateZ) == RotateZ) {
			transform.Post_Apply_Rotation(Engine::Math::AffineTransform3::Rotation_Z(0.05F));
		} else if ((m_objectRotation & RotateZBack) == RotateZBack) {
			transform.Post_Apply_Rotation(Engine::Math::AffineTransform3::Rotation_Z(-0.05F));
		}

		if (!Is_Orthogonal(transform)) {
			Re_Orthogonalize(transform);
		}

		// Set the new transform for the object
		prender_obj->Set_Transform (To_W3D(transform));
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Rotate_Light
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Rotate_Light ()
{
	// Get the document to display
	CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument();

	// Get the currently displayed object
	LightClass *pscene_light = doc->GetSceneLight ();
	RenderObjClass *prender_obj = doc->GetDisplayedObject ();
	if ((pscene_light != nullptr) && (prender_obj != nullptr)) {
		Engine::Math::AffineTransform3 rotation_matrix;

		// Build a rotation matrix that contains the x,y,z
		// rotations we want to apply to the light
		if ((m_LightRotation & RotateX) == RotateX) {
			rotation_matrix.Post_Apply_Rotation(Engine::Math::AffineTransform3::Rotation_X(0.05F));
		} else if ((m_LightRotation & RotateXBack) == RotateXBack) {
			rotation_matrix.Post_Apply_Rotation(Engine::Math::AffineTransform3::Rotation_X(-0.05F));
		}

		if ((m_LightRotation & RotateY) == RotateY) {
			rotation_matrix.Post_Apply_Rotation(Engine::Math::AffineTransform3::Rotation_Y(-0.05F));
		} else if ((m_LightRotation & RotateYBack) == RotateYBack) {
			rotation_matrix.Post_Apply_Rotation(Engine::Math::AffineTransform3::Rotation_Y(0.05F));
		}

		if ((m_LightRotation & RotateZ) == RotateZ) {
			rotation_matrix.Post_Apply_Rotation(Engine::Math::AffineTransform3::Rotation_Z(0.05F));
		} else if ((m_LightRotation & RotateZBack) == RotateZBack) {
			rotation_matrix.Post_Apply_Rotation(Engine::Math::AffineTransform3::Rotation_Z(-0.05F));
		}

		//
		//	Now, use the rotation matrix to rotate the
		// light 'around' the displayed object (in its coordinate system)
		//
		const Engine::Math::AffineTransform3 coord_system = To_Core_Math(prender_obj->Get_Transform());
		const Engine::Math::AffineTransform3 coord_inv = coord_system.Orthogonal_Inverse();

		const Engine::Math::AffineTransform3 light_transform = To_Core_Math(pscene_light->Get_Transform());
		const Engine::Math::AffineTransform3 coord_to_obj = Compose(coord_inv, light_transform);
		Engine::Math::AffineTransform3 transform = Compose(
			Compose(coord_system, rotation_matrix), coord_to_obj);

		// Ensure the matrix hasn't degenerated
		if (!Is_Orthogonal(transform)) {
			Re_Orthogonalize(transform);
		}

		// Pass the new transform onto the light
		m_pLightMesh->Set_Transform (To_W3D(transform));
		pscene_light->Set_Transform (To_W3D(transform));
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Set_FOV
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Set_FOV (double hfov, double vfov, bool force)
{
	CW3DViewDoc *doc = (CW3DViewDoc *)GetDocument();

	if (force || (doc->Is_FOV_Manual () == false)) {
		m_pCamera->Set_View_Plane (hfov, vfov);
	}
}


////////////////////////////////////////////////////////////////////////////
//
//  Reset_FOV
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Reset_FOV ()
{
	int cx = 0;
	int cy = 0;

	if (m_iWindowed == 0) {
		cx = g_iWidth;
		cy = g_iHeight;
	} else {
		CRect rect;
		GetClientRect (&rect);
		cx = rect.Width ();
		cy = rect.Height ();
	}

	// update the camera FOV settings
	// take the larger of the two dimensions, give it the
	// full desired FOV, then give the other dimension an
	// FOV proportional to its relative size
	double hfov,vfov;
	if (cy > cx) {

		vfov = (float)(((double)45.0f) * 3.141592654f / 180.0);	// legacy DEG_TO_RAD
		hfov = (double)cx / (double)cy * vfov;
	} else  {
		hfov = (float)(((double)45.0f) * 3.141592654f / 180.0);	// legacy DEG_TO_RAD
		vfov = (double)cy / (double)cx * hfov;
	}

	// Reset the field of view
	Set_FOV (hfov, vfov);
}


////////////////////////////////////////////////////////////////////////////
//
//  Set_Camera_Distance
//
////////////////////////////////////////////////////////////////////////////
void
CGraphicView::Set_Camera_Distance (float dist)
{
	m_CameraDistance = dist;

	//
	//	Reposition the camera
	//
	const Engine::Math::AffineTransform3 new_transform = Camera_Look_At(
		m_ViewedSphere.center + Engine::Math::Vector3{m_CameraDistance, 0, 0}, m_ViewedSphere.center);
	m_pCamera->Set_Transform (To_W3D(new_transform));

	//
	// Update the status bar
	//
	CMainFrame *main_wnd = (CMainFrame *)::AfxGetMainWnd ();
	if (main_wnd != nullptr) {
		main_wnd->UpdateCameraDistance (m_CameraDistance);
	}
}
