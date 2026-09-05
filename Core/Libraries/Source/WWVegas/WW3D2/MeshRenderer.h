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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : ww3d                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/meshrenderer.h                          $*
 *                                                                                             *
 *              Original Author:: Jani Penttinen                                               *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 29                                                          $*
 *                                                                                             *
 * 06/27/02 KM Changes to max texture stage caps																*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

class CameraClass;
class DecalMeshClass;

// Scene lifecycle adapter. Mesh and material drawing belongs to graphics;
// this object retains only camera context and the scene's deferred decal list.
class MeshRendererClass {
public:
    void Init();
    void Shutdown();
    void Flush();
    void Invalidate(bool shutdown=false);
    void Set_Camera(CameraClass* camera) { m_camera=camera; }
    CameraClass* Peek_Camera() const { return m_camera; }
    void Add_To_Render_List(DecalMeshClass* decal);
    void Enable_Lighting(bool enabled) { m_lighting=enabled; }
    bool Is_Lighting_Enabled() const { return m_lighting; }
    void Set_Force_Multiply(bool enabled) { m_force_multiply=enabled; }
    bool Is_Force_Multiply_Enabled() const { return m_force_multiply; }
private:
    CameraClass* m_camera=nullptr;
    DecalMeshClass* m_decals=nullptr;
    bool m_lighting=true;
    bool m_force_multiply=false;
};
extern MeshRendererClass TheMeshRenderer;
