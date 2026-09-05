#include "SortingRenderer.h"
#include "WW3D2/GraphicsGeometry.h"
bool SortingRendererClass::m_enabled=true;
void SortingRendererClass::Flush()
{
    if (m_enabled) Flush_Graphics_Transparent_Geometry();
    else Clear_Graphics_Transparent_Geometry();
}
void SortingRendererClass::Deinit() { Clear_Graphics_Transparent_Geometry(); }
