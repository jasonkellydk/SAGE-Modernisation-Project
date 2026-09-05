/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

#ifdef PROFILER_ENABLED

#ifdef RTS_ZEROHOUR
import Graphics.Backends.DX11.Coexistence;
import Graphics.Scene.Screen.Filters;
#endif
#include "W3DDevice/GameClient/W3DProfilerFrameCapture.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"

#include "WW3D2/Backend/RenderBackend.h"
#include "WW3D2/SurfaceClass.h"
#include "WW3D2/Texture.h"
#include "WW3D2/WW3D.h"
#include "WW3D2/WW3DFormat.h"
#include "WWMath/wwmath.h"
#include <algorithm>
#include <cstring>

W3DProfilerFrameCapture::W3DProfilerFrameCapture()
{
}

W3DProfilerFrameCapture::~W3DProfilerFrameCapture()
{
	if (m_swizzleShader)
	{
		WW3D::Get_Render_Backend()->Release_Pixel_Shader(m_swizzleShader);
		m_swizzleShader = 0;
	}
}

bool W3DProfilerFrameCapture::ShouldReuseLastCapture(UnsignedInt currentTimeMs) const
{
	return PROFILER_FRAME_IMAGE_INTERVAL_MS > 0
		&& currentTimeMs - m_lastCaptureTimeMs < PROFILER_FRAME_IMAGE_INTERVAL_MS
		&& !m_lastCapturePixels.empty();
}

void W3DProfilerFrameCapture::Capture(UnsignedInt displayWidth, UnsignedInt displayHeight)
{
	if (!PROFILER_IS_CONNECTED)
		return;

	IRenderBackend *backend = WW3D::Get_Render_Backend();
	if (backend == nullptr)
		return;

	// the profiler expects an image every render frame. resend the last capture if we're inside the capture interval.
	const UnsignedInt currentTimeMs = WW3D::Get_Logic_Time_Milliseconds();
	if (ShouldReuseLastCapture(currentTimeMs))
	{
		PROFILER_FRAME_IMAGE(m_lastCapturePixels.data(), PROFILER_FRAME_IMAGE_SIZE, m_lastCaptureHeight, 0, false);
		return;
	}

	// allocate render target
	TextureClass *renderTarget = backend->Create_Render_Target(
		PROFILER_FRAME_IMAGE_SIZE, PROFILER_FRAME_IMAGE_SIZE, WW3D_FORMAT_A8R8G8B8);
	if (!renderTarget)
		return;

	// allocate surface class
	const Real aspectRatio = (Real)displayHeight / (Real)displayWidth;
	unsigned int profilerImageHeight = static_cast<unsigned int>(std::min(
		static_cast<int>(WWMath::Round(PROFILER_FRAME_IMAGE_SIZE * aspectRatio)),
		PROFILER_FRAME_IMAGE_SIZE));
	SurfaceClass *surfaceClass = NEW_REF(SurfaceClass, (PROFILER_FRAME_IMAGE_SIZE, profilerImageHeight, WW3D_FORMAT_A8R8G8B8));
	if (!surfaceClass)
	{
		REF_PTR_RELEASE(renderTarget);
		return;
	}

	// get the backbuffer
	SurfaceClass *backBuffer = backend->Get_Back_Buffer_Surface();
	if (!backBuffer)
	{
		REF_PTR_RELEASE(surfaceClass);
		REF_PTR_RELEASE(renderTarget);
		return;
	}

	SurfaceClass::SurfaceDescription backBufferDescription;
	backBuffer->Get_Description(backBufferDescription);

	// allocate intermediate texture
	const uintptr_t intermediateTexture = backend->Create_Transient_Render_Texture(
		backBufferDescription.Width, backBufferDescription.Height, backBufferDescription.Format);
	if (intermediateTexture == 0)
	{
		REF_PTR_RELEASE(backBuffer);
		REF_PTR_RELEASE(surfaceClass);
		REF_PTR_RELEASE(renderTarget);
		return;
	}

	// Copy the back buffer into the intermediate texture.
	if (!backend->Copy_Back_Buffer_To_Texture(intermediateTexture))
	{
		REF_PTR_RELEASE(backBuffer);
		REF_PTR_RELEASE(surfaceClass);
		REF_PTR_RELEASE(renderTarget);
		backend->Release_Transient_Render_Texture(intermediateTexture);
		return;
	}

	// release the backbuffer
	REF_PTR_RELEASE(backBuffer);

	// set render target to a small surface
	backend->Set_Render_Target(renderTarget);

	// set viewport
	RenderBackendViewport restoreViewport{};
	if (!backend->Get_Viewport(restoreViewport))
	{
		backend->Set_Render_Target(nullptr);
		backend->Release_Transient_Render_Texture(intermediateTexture);
		REF_PTR_RELEASE(surfaceClass);
		REF_PTR_RELEASE(renderTarget);
		return;
	}

	SurfaceClass::SurfaceDescription smallRenderDesc;
	surfaceClass->Get_Description(smallRenderDesc);

	RenderBackendViewport backend_viewport{0, 0, PROFILER_FRAME_IMAGE_SIZE, smallRenderDesc.Height, 0.0f, 1.0f};
	backend->Set_Viewport(backend_viewport);

#ifdef RTS_ZEROHOUR
    void* resource=nullptr; void* view=nullptr;
    auto* device=Graphics::Shared_Frame_Device();
    if (device && backend->Get_Shared_Texture_Resources(intermediateTexture,resource,view)) {
        const auto texture=Graphics::Import_Shared_Texture(resource,view);
        std::array<Graphics::ScreenFilterVertex,4> vertices{};
        vertices[0].position={1,-1,0}; vertices[0].uv={1,1};
        vertices[1].position={1,1,0}; vertices[1].uv={1,0};
        vertices[2].position={-1,-1,0}; vertices[2].uv={0,1};
        vertices[3].position={-1,1,0}; vertices[3].uv={0,0};
        Graphics::ScreenFilterParameters parameters; parameters.operation=3;
        Graphics::Get_Screen_Filter_Renderer().Draw(device->Immediate_Command_List(),vertices,parameters,{},texture);
        device->Destroy_Texture(texture);
        backend->Invalidate_Cached_Render_States();
    }
#else
	// bind swizzle shader
	backend->Set_Pixel_Shader(m_swizzleShader);
	static const Real kMaskR[4] = {1.0f, 0.0f, 0.0f, 0.0f};
	static const Real kMaskG[4] = {0.0f, 1.0f, 0.0f, 0.0f};
	static const Real kMaskB[4] = {0.0f, 0.0f, 1.0f, 0.0f};
	backend->Set_Pixel_Shader_Constant(0, kMaskR, 1);
	backend->Set_Pixel_Shader_Constant(1, kMaskG, 1);
	backend->Set_Pixel_Shader_Constant(2, kMaskB, 1);

	// draw texture scaled-down onto a small surface
	struct QuadVertex
	{
		Real x, y, z, rhw;
		Real u, v;
	} vtx[4];
	const Real left = -0.5f;
	const Real top = -0.5f;
	const Real right = (Real)PROFILER_FRAME_IMAGE_SIZE - 0.5f;
	const Real bottom = (Real)smallRenderDesc.Height - 0.5f;
	vtx[0] = {right, bottom, 0.0f, 1.0f, 1.0f, 1.0f};
	vtx[1] = {right, top,    0.0f, 1.0f, 1.0f, 0.0f};
	vtx[2] = {left,  bottom, 0.0f, 1.0f, 0.0f, 1.0f};
	vtx[3] = {left,  top,    0.0f, 1.0f, 0.0f, 0.0f};
	backend->Set_Texture_Handle(0, intermediateTexture);
	backend->Set_Vertex_Format(RenderBackendVertexFormat::TransformedPositionTexture);
	backend->Draw_Primitive_Up(RenderBackendPrimitiveType::TriangleStrip, 2, vtx,
		sizeof(QuadVertex), RenderBackendVertexFormat::TransformedPositionTexture);
	backend->Set_Pixel_Shader(0);
	backend->Set_Texture_Handle(0, 0);
#endif
	backend->Set_Viewport(restoreViewport);
	backend->Set_Render_Target(nullptr);

	// copy the small surface pixels from GPU to CPU
	backend->Copy_Render_Target_To_Surface(renderTarget, surfaceClass);
	backend->Release_Transient_Render_Texture(intermediateTexture);

	// send pixels to the profiler backend
	int pitch = 0;
	void *bits = surfaceClass->Lock(&pitch);
	if (bits)
	{
		const size_t rowBytes = (size_t)PROFILER_FRAME_IMAGE_SIZE * 4;
		m_lastCaptureHeight = smallRenderDesc.Height;
		m_lastCapturePixels.resize(rowBytes * m_lastCaptureHeight);

		const UnsignedByte *source = static_cast<const UnsignedByte *>(bits);
		UnsignedByte *destination = m_lastCapturePixels.data();
		for (UnsignedInt row = 0; row < m_lastCaptureHeight; ++row)
		{
			std::memcpy(destination + row * rowBytes, source + row * pitch, rowBytes);
		}

		PROFILER_FRAME_IMAGE(m_lastCapturePixels.data(), PROFILER_FRAME_IMAGE_SIZE, m_lastCaptureHeight, 0, false);
		surfaceClass->Unlock();
		m_lastCaptureTimeMs = currentTimeMs;
	}

	// cleanup
	REF_PTR_RELEASE(surfaceClass);
	REF_PTR_RELEASE(renderTarget);
}

#endif // PROFILER_ENABLED
