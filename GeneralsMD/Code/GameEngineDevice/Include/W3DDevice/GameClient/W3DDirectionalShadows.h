#pragma once

class RenderObjClass;
class RenderInfoClass;
class Shadow;

Shadow* Create_Directional_Shadow(RenderObjClass* object);
void Reset_Directional_Shadows();
bool Collect_Directional_Shadow_Casters(RenderInfoClass& info);
bool Render_Directional_Shadow_Maps(RenderInfoClass& info);
