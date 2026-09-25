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

// OpacityVectorDialog.cpp : implementation file
//

#include <cmath>

import Engine.Core.Math.EulerAngles3;
import Engine.Core.Math.Quaternion;
#include "StdAfx.h"
#include "W3DView.h"
#include "OpacityVectorDialog.h"
#include "WW3D2/SphereObj.h"
#include "WW3D2/RingObj.h"
#include "ColorBar.h"

#ifdef RTS_DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
// Legacy WWMath angle macros and WWMath::Wrap, kept with their original
// precision and single-step wrap-then-clamp behaviour.
double Legacy_Deg_To_Rad(double degrees) { return degrees * 3.141592654f / 180.0; }
float Legacy_Deg_To_RadF(float degrees) { return degrees * 3.141592654f / 180.0f; }
double Legacy_Rad_To_Deg(double radians) { return radians * 180.0 / 3.141592654f; }

float Legacy_Wrap(float val, float min, float max)
{
	if (val >= max) val -= (max - min);
	if (val < min) val += (max - min);
	if (val < min) val = min;
	if (val > max) val = max;
	return val;
}
}


/////////////////////////////////////////////////////////////////////////////
//
// OpacityVectorDialogClass
//
/////////////////////////////////////////////////////////////////////////////
OpacityVectorDialogClass::OpacityVectorDialogClass(CWnd* pParent /*=nullptr*/)
	:	m_OpacityBar (nullptr),
		m_RenderObj (nullptr),
		m_KeyIndex (0),
		CDialog(OpacityVectorDialogClass::IDD, pParent)
{
	//{{AFX_DATA_INIT(OpacityVectorDialogClass)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}


/////////////////////////////////////////////////////////////////////////////
//
// DoDataExchange
//
/////////////////////////////////////////////////////////////////////////////
void
OpacityVectorDialogClass::DoDataExchange (CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(OpacityVectorDialogClass)
	DDX_Control(pDX, IDC_SLIDER_Z, m_SliderZ);
	DDX_Control(pDX, IDC_SLIDER_Y, m_SliderY);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(OpacityVectorDialogClass, CDialog)
	//{{AFX_MSG_MAP(OpacityVectorDialogClass)
	ON_WM_HSCROLL()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()



/////////////////////////////////////////////////////////////////////////////
//
// OnInitDialog
//
/////////////////////////////////////////////////////////////////////////////
BOOL
OpacityVectorDialogClass::OnInitDialog ()
{
	CDialog::OnInitDialog();

	m_OpacityBar = ColorBarClass::Get_Color_Bar (::GetDlgItem (m_hWnd, IDC_OPACITY_BAR));
	ASSERT (m_OpacityBar);

	//
	// Setup the opacity bar
	//
	m_OpacityBar->Set_Range (0, 10);
	m_OpacityBar->Modify_Point (0, 0, 255, 255, 255);
	m_OpacityBar->Insert_Point (1, 10, 0, 0, 0);

	float value =  ::atan (((m_Value.intensity / 10.0F) * 11.0F)) / Legacy_Deg_To_Rad (84.5) * 10.0F;
	m_OpacityBar->Set_Selection_Pos (value);

	//
	//	Setup the sliders
	//
	m_SliderY.SetRange (0, 179);
	m_SliderZ.SetRange (0, 179);

	float log_test = ::log (8.0F);
	float log_test2 = ::_logb (log_test);
	float log_test3 = ::exp (log_test);

	//
	//	Convert the normalized vector to Euler angles...
	//


	const Engine::Math::Quaternion rotation{
		m_Value.angle.X, m_Value.angle.Y, m_Value.angle.Z, m_Value.angle.W};
	const Engine::Math::AffineTransform3 rotation_basis = rotation.To_Rotation_Transform();
	const Engine::Math::EulerAngles3 euler_angle =
		Engine::Math::EulerAngles3::From_Rotation_XYZ_Rotating_Frame(rotation_basis);
	float y_rot = Legacy_Rad_To_Deg (euler_angle.y);
	float z_rot = Legacy_Rad_To_Deg (euler_angle.z);

	y_rot = Legacy_Wrap (y_rot, 0, 360);
	z_rot = Legacy_Wrap (z_rot, 0, 360);

	m_SliderY.SetPos ((int)y_rot);
	m_SliderZ.SetPos ((int)z_rot);
	return TRUE;
}


/////////////////////////////////////////////////////////////////////////////
//
// OnOK
//
/////////////////////////////////////////////////////////////////////////////
void
OpacityVectorDialogClass::OnOK ()
{
	m_Value = Update_Value ();
	CDialog::OnOK ();
}


/////////////////////////////////////////////////////////////////////////////
//
// Update_Object
//
/////////////////////////////////////////////////////////////////////////////
void
OpacityVectorDialogClass::Update_Object ()
{
	Update_Object (Update_Value ());
}


/////////////////////////////////////////////////////////////////////////////
//
// Update_Value
//
/////////////////////////////////////////////////////////////////////////////
AlphaVectorStruct
OpacityVectorDialogClass::Update_Value ()
{
	AlphaVectorStruct value;

	int y_pos = m_SliderY.GetPos ();
	int z_pos = m_SliderZ.GetPos ();

	//float x_rot = Legacy_Deg_To_RadF ((float)x_pos);
	float y_rot = Legacy_Deg_To_RadF ((float)y_pos);
	float z_rot = Legacy_Deg_To_RadF ((float)z_pos);

	const Engine::Math::Quaternion y_rotation{
		0.0f, std::sin(y_rot * 0.5f), 0.0f, std::cos(y_rot * 0.5f)};
	const Engine::Math::Quaternion z_rotation{
		0.0f, 0.0f, std::sin(z_rot * 0.5f), std::cos(z_rot * 0.5f)};
	const Engine::Math::Quaternion rotation = y_rotation * z_rotation;
	value.angle = Quaternion(rotation.x, rotation.y, rotation.z, rotation.w);

	float percent = ::tan ((m_OpacityBar->Get_Selection_Pos () / 10.0F) * Legacy_Deg_To_Rad (84.5)) / 11.0F;
	percent = min (1.0F, percent);
	percent = max (0.0F, percent);

	value.intensity = 10.0F * percent;
	return value;
}


/////////////////////////////////////////////////////////////////////////////
//
// Update_Object
//
/////////////////////////////////////////////////////////////////////////////
void
OpacityVectorDialogClass::Update_Object (const AlphaVectorStruct &value)
{
	if (m_RenderObj != nullptr) {

		//
		//	Determine what type of object this is
		//
		switch (m_RenderObj->Class_ID ())
		{
			case RenderObjClass::CLASSID_SPHERE:
			{
				//
				//	Update the key with the new vector
				//

				SphereVectorChannelClass &vector_channel = ((SphereRenderObjClass *)m_RenderObj)->Get_Vector_Channel ();
				vector_channel.Set_Key_Value (m_KeyIndex, value);

				//
				//	Force the animation to restart
				//
				((SphereRenderObjClass *)m_RenderObj)->Restart_Animation ();
			}
			break;
		}
	}
}


/////////////////////////////////////////////////////////////////////////////
//
// OnCancel
//
/////////////////////////////////////////////////////////////////////////////
void
OpacityVectorDialogClass::OnCancel ()
{
	Update_Object (m_Value);
	CDialog::OnCancel ();
}


/////////////////////////////////////////////////////////////////////////////
//
// OnHScroll
//
/////////////////////////////////////////////////////////////////////////////
void
OpacityVectorDialogClass::OnHScroll
(
	UINT				nSBCode,
	UINT				nPos,
	CScrollBar *	pScrollBar
)
{
	//
	//	Update the object
	//
	Update_Object ();

	CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
}


/////////////////////////////////////////////////////////////
//
//  OnNotify
//
/////////////////////////////////////////////////////////////
BOOL
OpacityVectorDialogClass::OnNotify
(
	WPARAM wParam,
	LPARAM lParam,
	LRESULT *pResult
)
{
	CBR_NMHDR *color_bar_hdr = (CBR_NMHDR *)lParam;

	//
	//	Which control sent the notification?
	//
	switch (color_bar_hdr->hdr.idFrom)
	{
		case IDC_OPACITY_BAR:
		{
			//
			// Update the object
			//
			if (color_bar_hdr->hdr.code == CBRN_SEL_CHANGED) {
				Update_Object ();
			}
		}
		break;
	}

	return CDialog::OnNotify (wParam, lParam, pResult);
}
