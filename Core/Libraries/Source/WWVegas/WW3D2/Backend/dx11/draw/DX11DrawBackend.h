#pragma once

#include "../core/DX11BackendComponent.h"

namespace dx11_backend
{
	template <typename Host>
	class DX11DrawBackend;

	template <typename Host>
	class DX11DrawBackend : public DX11BackendComponent<Host, DX11DrawBackend<Host>>
	{
	public:
		void Set_Vertex_Buffer(RenderBackendVertexBuffer *buffer, unsigned offset_bytes, unsigned stride_bytes, unsigned stream = 0);
		void Set_Index_Buffer(RenderBackendIndexBuffer *buffer);
		void Set_Vertex_Format(RenderBackendVertexFormat format);
		void Set_Vertex_Buffer(const VertexBufferClass *vb, unsigned stream = 0);
		void Set_Vertex_Buffer(const DynamicVBAccessClass &vba);
		void Set_Index_Buffer(const IndexBufferClass *ib, unsigned short index_base_offset);
		void Set_Index_Buffer(const DynamicIBAccessClass &iba, unsigned short index_base_offset);
		void Set_Index_Buffer_Index_Offset(unsigned offset);
		void Set_Projection_Transform_With_Z_Bias(const Matrix4x4 &matrix, float znear, float zfar);
		void Set_Transform(RenderBackendTransform transform, const Matrix4x4 &matrix);
		void Set_Transform(RenderBackendTransform transform, const Matrix3D &matrix);
		void Get_Transform(RenderBackendTransform transform, Matrix4x4 &matrix);
		void Set_Transform(RenderBackendTransform transform, const float *matrix_elements);
		void Get_Transform(RenderBackendTransform transform, float *matrix_elements);
		void Set_World_Identity();
		void Set_View_Identity();
		bool Is_World_Identity();
		bool Is_View_Identity();
	};
}
