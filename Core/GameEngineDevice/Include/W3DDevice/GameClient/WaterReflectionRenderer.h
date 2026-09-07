/*
** Command & Conquer Generals Zero Hour(tm)
**
** Scene-submission contract used by the water renderer.
*/

#pragma once

import Graphics.RHI;

class CameraClass;

class WaterReflectionRenderer
{
public:
	virtual ~WaterReflectionRenderer() = default;

	virtual void Render_Water_Reflection(CameraClass *camera,
		const Graphics::RHIViewport &viewport) = 0;
};
