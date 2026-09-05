#pragma once

#include "../core/DX11BackendComponent.h"

namespace dx11_backend
{
	template <typename Host>
	class DX11RenderStateBackend;

	template <typename Host>
	class DX11RenderStateBackend : public DX11BackendComponent<Host, DX11RenderStateBackend<Host>>
	{
	public:
		void Invalidate_Cached_Render_States();
		void Set_Render_State(unsigned state, unsigned value);
		unsigned Get_Render_State(unsigned state) const;
		void Set_Texture_Stage_State(unsigned stage, unsigned state, unsigned value);
		void Set_Ambient(const Vector3 &color);
		void Set_Light_Environment(LightEnvironmentClass *light_env);
		void Set_Fog(bool enable, const Vector3 &color, float start, float end);
		void Set_Material_Values(const RenderBackendMaterial &material);
		void Set_Fill_Mode(RenderBackendFillMode mode);
		RenderBackendFillMode Get_Fill_Mode() const;
		void Set_Color_Write_Mask(RenderBackendColorWriteMask mask);
		RenderBackendColorWriteMask Get_Color_Write_Mask() const;
		void Set_Alpha_Blend_Enabled(bool enable);
		void Set_Blend_Operation(RenderBackendBlendOperation operation);
		void Set_Blend_Factors(RenderBackendBlendFactor source, RenderBackendBlendFactor destination);
		void Set_Source_Blend_Factor(RenderBackendBlendFactor factor);
		void Set_Destination_Blend_Factor(RenderBackendBlendFactor factor);
		void Set_Alpha_Test_Enabled(bool enable);
		void Set_Alpha_Test_Function(RenderBackendCompareFunction function);
		void Set_Alpha_Test_Reference(unsigned reference);
		void Set_Fog_Enabled(bool enable);
		void Set_Fog_Color(unsigned color);
		void Set_Depth_Bias(unsigned bias);
		unsigned Get_Depth_Bias() const;
		void Set_Texture_Factor(unsigned color);
		void Set_Depth_Test_Enabled(bool enable);
		void Set_Depth_Write_Enabled(bool enable);
		void Set_Depth_Function(RenderBackendCompareFunction function);
		void Set_Cull_Mode(RenderBackendCullMode mode);
		RenderBackendCullMode Get_Cull_Mode() const;
		void Push_Cull_Mode_Override(RenderBackendCullMode mode);
		void Pop_Cull_Mode_Override();
		void Set_Point_Sprite_Enabled(bool enable);
		void Set_Point_Scale_Enabled(bool enable);
		void Set_Point_Size(float size);
		void Set_Point_Size_Min(float size);
		void Set_Point_Size_Max(float size);
		void Set_Point_Scale(float scale_a, float scale_b, float scale_c);
		void Set_Shade_Mode(RenderBackendShadeMode mode);
		void Set_Lighting_Enabled(bool enable);
		void Set_Normalize_Normals(bool enable);
		void Set_Specular_Enabled(bool enable);
		void Set_Material_Color_Sources(RenderBackendMaterialSource ambient, RenderBackendMaterialSource diffuse, RenderBackendMaterialSource emissive);
		void Set_NPatch_Segments(float segments);
		RenderBackendStencilState Get_Stencil_State() const;
		RenderBackendFogState Get_Fog_State() const;
		void Set_Stencil_Enabled(bool enable);
		void Set_Stencil_Function(RenderBackendCompareFunction function);
		void Set_Stencil_Reference(unsigned reference);
		void Set_Stencil_Read_Mask(unsigned mask);
		void Set_Stencil_Write_Mask(unsigned mask);
		void Set_Stencil_Z_Fail_Operation(RenderBackendStencilOperation operation);
		void Set_Stencil_Fail_Operation(RenderBackendStencilOperation operation);
		void Set_Stencil_Pass_Operation(RenderBackendStencilOperation operation);
		void Set_Texture_Operation(unsigned stage, RenderBackendTextureComponent component, RenderBackendTextureOperation operation);
		void Set_Texture_Argument(unsigned stage, RenderBackendTextureComponent component, unsigned argument_index, RenderBackendTextureArgument argument, RenderBackendTextureArgumentModifiers modifiers = RenderBackendTextureArgumentModifiers::None);
		void Set_Texture_Coordinate_Source(unsigned stage, RenderBackendTextureCoordinateSource source, unsigned uv_array_index = 0);
		void Set_Texture_Transform_Flags(unsigned stage, RenderBackendTextureTransformFlags flags);
		void Set_Texture_Address_Mode(unsigned stage, bool u_coordinate, RenderBackendTextureAddressMode mode);
		void Set_Texture_Filter(unsigned stage, RenderBackendTextureFilterType type, RenderBackendTextureFilter filter);
		void Set_Texture_Max_Anisotropy(unsigned stage, unsigned level);
		void Set_Texture_Bump_Environment_Matrix(unsigned stage, float m00, float m01, float m10, float m11, float scale = 1.0f, float offset = 0.0f);
		void Set_Material(const VertexMaterialClass *material);
		void Set_Light(unsigned index, const LightClass &light);
		void Set_Light_From_State(unsigned index, const RenderBackendLight *light);
		void Disable_Light(unsigned index);
		void Capture_Render_State(RenderStateStruct &state);
		void Apply_Render_State(const RenderStateStruct &state);
		void Begin_Backend_Statistics();
	void End_Backend_Statistics();
	void Restore_Render_State();
	void Apply_Render_State_Changes();
	void Begin_Programmable_Pass();
	void Apply_Programmable_Render_State_Changes();
	void End_Programmable_Pass();
	};
}
