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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : W3DView                                                      *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/Tools/W3DView/VolumeRandomDialog.cpp                                                                                                                                                                                                                                                                                                                              $Modtime::                                                             $*
 *                                                                                             *
 *                    $Revision:: 2                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#include "StdAfx.h"
#include "W3DView.h"
#include "VolumeRandomDialog.h"
#include "Utils.h"

import Engine.Core.Math.Vector3;

#ifdef RTS_DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


////////////////////////////////////////////////////////////////////
//
//	VolumeRandomDialogClass
//
////////////////////////////////////////////////////////////////////
VolumeRandomDialogClass::VolumeRandomDialogClass (Engine::Math::RandomVector3Generator *randomizer, CWnd *pParent)
	:	m_Randomizer (randomizer),
		CDialog (VolumeRandomDialogClass::IDD, pParent)
{
	//{{AFX_DATA_INIT(VolumeRandomDialogClass)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}


////////////////////////////////////////////////////////////////////
//
//	DoDataExchange
//
////////////////////////////////////////////////////////////////////
void
VolumeRandomDialogClass::DoDataExchange (CDataExchange *pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(VolumeRandomDialogClass)
	DDX_Control(pDX, IDC_SPHERE_RADIUS_SPIN, m_SphereRadiusSpin);
	DDX_Control(pDX, IDC_CYLINDER_RADIUS_SPIN, m_CylinderRadiusSpin);
	DDX_Control(pDX, IDC_CYLINDER_HEIGHT_SPIN, m_CylinderHeightSpin);
	DDX_Control(pDX, IDC_BOX_Z_SPIN, m_BoxZSpin);
	DDX_Control(pDX, IDC_BOX_Y_SPIN, m_BoxYSpin);
	DDX_Control(pDX, IDC_BOX_X_SPIN, m_BoxXSpin);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(VolumeRandomDialogClass, CDialog)
	//{{AFX_MSG_MAP(VolumeRandomDialogClass)
	ON_BN_CLICKED(IDC_BOX_RADIO, OnBoxRadio)
	ON_BN_CLICKED(IDC_CYLINDER_RADIO, OnCylinderRadio)
	ON_BN_CLICKED(IDC_SPHERE_RADIO, OnSphereRadio)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


////////////////////////////////////////////////////////////////////
//
//	OnOK
//
////////////////////////////////////////////////////////////////////
void
VolumeRandomDialogClass::OnOK ()
{
	if (SendDlgItemMessage (IDC_BOX_RADIO, BM_GETCHECK) == 1) {

		//
		//	Create a box randomizer
		//
		Engine::Math::Vector3 extents {0, 0, 0};
		extents.x = ::GetDlgItemFloat (m_hWnd, IDC_BOX_X_EDIT);
		extents.y = ::GetDlgItemFloat (m_hWnd, IDC_BOX_Y_EDIT);
		extents.z = ::GetDlgItemFloat (m_hWnd, IDC_BOX_Z_EDIT);
		m_Randomizer = new Engine::Math::RandomVector3Generator (
			Engine::Math::Vector3Distribution::Box, extents, 0);
	} else if (SendDlgItemMessage (IDC_SPHERE_RADIO, BM_GETCHECK) == 1) {

		//
		//	What type of sphere is this, hollow or solid?
		//
		float radius = ::GetDlgItemFloat (m_hWnd, IDC_SPHERE_RADIUS_EDIT);
		if (SendDlgItemMessage (IDC_SPHERE_HOLLOW_CHECK, BM_GETCHECK) == 1) {
			m_Randomizer = new Engine::Math::RandomVector3Generator (
				Engine::Math::Vector3Distribution::SphereSurface, {radius, radius, radius}, 0);
		} else {
			m_Randomizer = new Engine::Math::RandomVector3Generator (
				Engine::Math::Vector3Distribution::SolidSphere, {radius, radius, radius}, 0);
		}
	} else if (SendDlgItemMessage (IDC_CYLINDER_RADIO, BM_GETCHECK) == 1) {

		//
		//	Create a cylinder randomizer
		//
		float radius = ::GetDlgItemFloat (m_hWnd, IDC_CYLINDER_RADIUS_EDIT);
		float height = ::GetDlgItemFloat (m_hWnd, IDC_CYLINDER_HEIGHT_EDIT);
		// Cylinder dimensions: x = extent along the axis (legacy "height"), y = radius
		m_Randomizer = new Engine::Math::RandomVector3Generator (
			Engine::Math::Vector3Distribution::Cylinder, {height, radius, radius}, 0);
	}

	CDialog::OnOK ();
}


////////////////////////////////////////////////////////////////////
//
//	OnInitDialog
//
////////////////////////////////////////////////////////////////////
BOOL
VolumeRandomDialogClass::OnInitDialog ()
{
	CDialog::OnInitDialog ();

	//
	//	Start with some default values
	//
	Engine::Math::Vector3 initial_box {1, 1, 1};
	float initial_sphere_radius = 1.0F;
	bool initial_sphere_hollow = false;
	float initial_cylinder_radius = 1.0F;
	float initial_cylinder_height = 1.0F;
	UINT initial_type = IDC_BOX_RADIO;

	//
	//	Initialize from the provided randomizer
	//
	if (m_Randomizer != nullptr) {

		// What type of randomizer is this?
		const Engine::Math::Vector3 dimensions = m_Randomizer->Dimensions ();
		switch (m_Randomizer->Distribution ())
		{
			case Engine::Math::Vector3Distribution::Box:
				initial_type = IDC_BOX_RADIO;
				initial_box = dimensions;
				break;

			case Engine::Math::Vector3Distribution::SolidSphere:
				initial_type = IDC_SPHERE_RADIO;
				initial_sphere_radius = dimensions.x;
				initial_sphere_hollow = false;
				break;

			case Engine::Math::Vector3Distribution::SphereSurface:
				initial_type = IDC_SPHERE_RADIO;
				initial_sphere_radius = dimensions.x;
				initial_sphere_hollow = true;
				break;

			case Engine::Math::Vector3Distribution::Cylinder:
				initial_type = IDC_CYLINDER_RADIO;
				initial_cylinder_radius = dimensions.y;
				initial_cylinder_height = dimensions.x;
				break;

			default:
				ASSERT (0);
				break;
		}
	}

	//
	//	Initialize the box controls
	//
	::Initialize_Spinner (m_BoxXSpin, initial_box.x, -10000, 10000);
	::Initialize_Spinner (m_BoxYSpin, initial_box.y, -10000, 10000);
	::Initialize_Spinner (m_BoxZSpin, initial_box.z, -10000, 10000);

	//
	//	Initialize the sphere controls
	//
	::Initialize_Spinner (m_SphereRadiusSpin, initial_sphere_radius, 0, 10000);
	SendDlgItemMessage (IDC_SPHERE_HOLLOW_CHECK, BM_SETCHECK, (WPARAM)initial_sphere_hollow);

	//
	//	Initialize the cylinder controls
	//
	::Initialize_Spinner (m_CylinderRadiusSpin, initial_cylinder_radius, 0, 10000);
	::Initialize_Spinner (m_CylinderHeightSpin, initial_cylinder_height, 0, 10000);

	//
	//	Check the appropriate radio
	//
	SendDlgItemMessage (initial_type, BM_SETCHECK, (WPARAM)TRUE);
	Update_Enable_State ();
	return TRUE;
}


////////////////////////////////////////////////////////////////////
//
//	OnBoxRadio
//
////////////////////////////////////////////////////////////////////
void
VolumeRandomDialogClass::OnBoxRadio ()
{
	Update_Enable_State ();
}


////////////////////////////////////////////////////////////////////
//
//	OnCylinderRadio
//
////////////////////////////////////////////////////////////////////
void
VolumeRandomDialogClass::OnCylinderRadio ()
{
	Update_Enable_State ();
}


////////////////////////////////////////////////////////////////////
//
//	OnSphereRadio
//
////////////////////////////////////////////////////////////////////
void
VolumeRandomDialogClass::OnSphereRadio ()
{
	Update_Enable_State ();
}


////////////////////////////////////////////////////////////////////
//
//	Update_Enable_State
//
////////////////////////////////////////////////////////////////////
void
VolumeRandomDialogClass::Update_Enable_State ()
{
	bool enable_box_ctrls = (SendDlgItemMessage (IDC_BOX_RADIO, BM_GETCHECK) == 1);
	bool enable_sphere_ctrls = (SendDlgItemMessage (IDC_SPHERE_RADIO, BM_GETCHECK) == 1);
	bool enable_cylinder_ctrls = (SendDlgItemMessage (IDC_CYLINDER_RADIO, BM_GETCHECK) == 1);

	//
	//	Update the box controls
	//
	::EnableWindow (::GetDlgItem (m_hWnd, IDC_BOX_X_EDIT), enable_box_ctrls);
	::EnableWindow (::GetDlgItem (m_hWnd, IDC_BOX_Y_EDIT), enable_box_ctrls);
	::EnableWindow (::GetDlgItem (m_hWnd, IDC_BOX_Z_EDIT), enable_box_ctrls);
	::EnableWindow (::GetDlgItem (m_hWnd, IDC_BOX_X_SPIN), enable_box_ctrls);
	::EnableWindow (::GetDlgItem (m_hWnd, IDC_BOX_Y_SPIN), enable_box_ctrls);
	::EnableWindow (::GetDlgItem (m_hWnd, IDC_BOX_Z_SPIN), enable_box_ctrls);

	//
	//	Update the sphere controls
	//
	::EnableWindow (::GetDlgItem (m_hWnd, IDC_SPHERE_RADIUS_EDIT), enable_sphere_ctrls);
	::EnableWindow (::GetDlgItem (m_hWnd, IDC_SPHERE_RADIUS_SPIN), enable_sphere_ctrls);
	::EnableWindow (::GetDlgItem (m_hWnd, IDC_SPHERE_HOLLOW_CHECK), enable_sphere_ctrls);

	//
	//	Update the cylinder controls
	//
	::EnableWindow (::GetDlgItem (m_hWnd, IDC_CYLINDER_RADIUS_EDIT), enable_cylinder_ctrls);
	::EnableWindow (::GetDlgItem (m_hWnd, IDC_CYLINDER_RADIUS_SPIN), enable_cylinder_ctrls);
	::EnableWindow (::GetDlgItem (m_hWnd, IDC_CYLINDER_HEIGHT_EDIT), enable_cylinder_ctrls);
	::EnableWindow (::GetDlgItem (m_hWnd, IDC_CYLINDER_HEIGHT_SPIN), enable_cylinder_ctrls);
}


////////////////////////////////////////////////////////////////////
//
//	OnNotify
//
////////////////////////////////////////////////////////////////////
BOOL
VolumeRandomDialogClass::OnNotify
(
	WPARAM wParam,
	LPARAM lParam,
	LRESULT *pResult
)
{
	//
	//	Update the spinner control if necessary
	//
	NMHDR *pheader = (NMHDR *)lParam;
	if ((pheader != nullptr) && (pheader->code == UDN_DELTAPOS)) {
		LPNMUPDOWN pupdown = (LPNMUPDOWN)lParam;
		::Update_Spinner_Buddy (pheader->hwndFrom, pupdown->iDelta);
	}

	// Allow the base class to process this message
	return CDialog::OnNotify (wParam, lParam, pResult);
}
