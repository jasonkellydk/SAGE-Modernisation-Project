/* DX11 render-state subsystem. */
#include "Backend/dx11/state/DX11RenderStateBackend.h"
#include "Backend/dx11/core/DX11BackendInternals.h"

namespace dx11_backend
{
void DX11BackendState::Upload_Constants()
{
	if (device == nullptr || context == nullptr || vertex_constant_buffer == nullptr ||
		pixel_constant_buffer == nullptr)
	{
		return;
	}
	if (!constant_state_dirty && constant_buffers_bound)
	{
		return;
	}
	bool upload_succeeded = true;

	DX11VertexConstantData vertex_data = {};
	std::memcpy(vertex_data.world, &transforms[static_cast<unsigned>(RenderBackendTransform::World)],
		sizeof(vertex_data.world));
	std::memcpy(vertex_data.view, &transforms[static_cast<unsigned>(RenderBackendTransform::View)],
		sizeof(vertex_data.view));
	std::memcpy(vertex_data.projection, &transforms[static_cast<unsigned>(RenderBackendTransform::Projection)],
		sizeof(vertex_data.projection));
	vertex_data.viewport[0] = static_cast<float>(viewport.x);
	vertex_data.viewport[1] = static_cast<float>(viewport.y);
	vertex_data.viewport[2] = static_cast<float>(viewport.width);
	vertex_data.viewport[3] = static_cast<float>(viewport.height);
	std::memcpy(vertex_data.legacy, vertex_constants.data(), sizeof(vertex_data.legacy));

	D3D11_MAPPED_SUBRESOURCE mapped_resource = {};
	const bool vertex_constants_changed = !uploaded_vertex_constants_valid ||
		std::memcmp(&uploaded_vertex_constants, &vertex_data,
			sizeof(vertex_data)) != 0;
	if (vertex_constants_changed)
	{
		if (SUCCEEDED(context->Map(vertex_constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource)))
		{
			std::memcpy(mapped_resource.pData, &vertex_data, sizeof(vertex_data));
			context->Unmap(vertex_constant_buffer, 0);
			uploaded_vertex_constants = vertex_data;
			uploaded_vertex_constants_valid = true;
		}
		else
		{
			upload_succeeded = false;
		}
	}

	DX11PixelConstantData pixel_data = {};
	std::memcpy(pixel_data.material_diffuse.values, material.diffuse, sizeof(material.diffuse));
	std::memcpy(pixel_data.material_ambient.values, material.ambient, sizeof(material.ambient));
	std::memcpy(pixel_data.material_emissive.values, material.emissive, sizeof(material.emissive));
	std::memcpy(pixel_data.material_specular.values, material.specular, sizeof(material.specular));
	std::memcpy(pixel_data.fog_color.values, fog_color, sizeof(fog_color));
	const Vector4 unpacked_texture_factor = Color_From_Packed(texture_factor);
	std::memcpy(pixel_data.texture_factor.values, &unpacked_texture_factor,
		sizeof(unpacked_texture_factor));
	std::memcpy(pixel_data.legacy, pixel_constants.data(), sizeof(pixel_data.legacy));
	pixel_data.pixel_state0.values[0] = fog_start;
	pixel_data.pixel_state0.values[1] = fog_end;
	pixel_data.pixel_state0.values[2] = fog_enabled ? 1.0f : 0.0f;
	pixel_data.pixel_state0.values[3] = alpha_test_enabled ? 1.0f : 0.0f;
	pixel_data.pixel_state1.values[0] = static_cast<unsigned>(alpha_function);
	pixel_data.pixel_state1.values[1] = alpha_reference;
	pixel_data.pixel_state1.values[2] = lighting_enabled ? 1u : 0u;
	pixel_data.pixel_state1.values[3] = specular_enabled ? 1u : 0u;
	pixel_data.material_source_state.values[0] = static_cast<unsigned>(ambient_source);
	pixel_data.material_source_state.values[1] = static_cast<unsigned>(diffuse_source);
	pixel_data.material_source_state.values[2] = static_cast<unsigned>(emissive_source);
	for (unsigned index = 0; index < MAX_TEXTURE_STAGES; ++index)
	{
		DX11StageState &stage = stages[index];
		pixel_data.stage_enabled[index].values[0] = textures[index] != nullptr ? 1u : 0u;
		pixel_data.stage_color_operation[index].values[0] = static_cast<unsigned>(stage.color_operation);
		pixel_data.stage_alpha_operation[index].values[0] = static_cast<unsigned>(stage.alpha_operation);
		pixel_data.stage_coordinate_state[index].values[0] = static_cast<unsigned>(stage.coordinate_source);
		pixel_data.stage_coordinate_state[index].values[1] = stage.uv_array_index;
		pixel_data.stage_coordinate_state[index].values[2] = static_cast<unsigned>(stage.transform_flags);
		for (unsigned argument = 0; argument < 3; ++argument)
		{
			pixel_data.stage_color_argument[index].values[argument] =
				static_cast<unsigned>(stage.color_argument[argument]);
			pixel_data.stage_alpha_argument[index].values[argument] =
				static_cast<unsigned>(stage.alpha_argument[argument]);
			pixel_data.stage_color_modifier[index].values[argument] =
				static_cast<unsigned>(stage.color_modifiers[argument]);
			pixel_data.stage_alpha_modifier[index].values[argument] =
				static_cast<unsigned>(stage.alpha_modifiers[argument]);
		}
		std::memcpy(pixel_data.texture_transform[index],
			&transforms[static_cast<unsigned>(RenderBackend_Texture_Transform(index))],
			sizeof(pixel_data.texture_transform[index]));
		pixel_data.bump_matrix[index].values[0] = stage.bump_matrix[0];
		pixel_data.bump_matrix[index].values[1] = stage.bump_matrix[1];
		pixel_data.bump_matrix[index].values[2] = stage.bump_matrix[2];
		pixel_data.bump_matrix[index].values[3] = stage.bump_matrix[3];
		pixel_data.bump_params[index].values[0] = stage.bump_matrix[4];
		pixel_data.bump_params[index].values[1] = stage.bump_matrix[5];
	}
	pixel_data.material_power.values[0] = material.power;
	pixel_data.scene_ambient.values[0] = ambient[0];
	pixel_data.scene_ambient.values[1] = ambient[1];
	pixel_data.scene_ambient.values[2] = ambient[2];
	const Matrix4x4 & view = transforms[static_cast<unsigned>(RenderBackendTransform::View)];
	for (unsigned index = 0; index < lights.size(); ++index)
	{
		const RenderBackendLight & light = lights[index];
		std::memcpy(pixel_data.light_diffuse[index].values, light.diffuse,
			sizeof(light.diffuse));
		std::memcpy(pixel_data.light_specular[index].values, light.specular,
			sizeof(light.specular));
		std::memcpy(pixel_data.light_ambient[index].values, light.ambient,
			sizeof(light.ambient));
		pixel_data.light_enabled.values[index] = light_enabled[index] ? 1u : 0u;
		switch (light.type)
		{
		case RenderBackendLightType::Point:
			pixel_data.light_position[index].values[3] = 1.0f;
			break;
		case RenderBackendLightType::Spot:
			pixel_data.light_position[index].values[3] = 2.0f;
			break;
		case RenderBackendLightType::Directional:
			pixel_data.light_position[index].values[3] = 3.0f;
			break;
		default:
			pixel_data.light_position[index].values[3] = 0.0f;
			break;
		}
		Vector3 position(light.position[0], light.position[1], light.position[2]);
		Vector4 transformed_position;
		if (light_position_camera_space[index])
		{
			transformed_position = Vector4(position[0], position[1], position[2], 1.0f);
		}
		else
		{
			Matrix4x4::Transform_Vector(view, position, &transformed_position);
		}
		pixel_data.light_position[index].values[0] = transformed_position[0];
		pixel_data.light_position[index].values[1] = transformed_position[1];
		pixel_data.light_position[index].values[2] = transformed_position[2];

		Vector3 direction(light.direction[0], light.direction[1], light.direction[2]);
		Vector4 transformed_direction;
		if (light_direction_camera_space[index])
		{
			transformed_direction = Vector4(direction[0], direction[1], direction[2], 0.0f);
		}
		else
		{
			Matrix4x4::Transform_Vector(view,
				Vector4(direction[0], direction[1], direction[2], 0.0f),
				&transformed_direction);
		}
		pixel_data.light_direction[index].values[0] = transformed_direction[0];
		pixel_data.light_direction[index].values[1] = transformed_direction[1];
		pixel_data.light_direction[index].values[2] = transformed_direction[2];
		pixel_data.light_attenuation[index].values[0] = light.attenuation0;
		pixel_data.light_attenuation[index].values[1] = light.attenuation1;
		pixel_data.light_attenuation[index].values[2] = light.attenuation2;
		pixel_data.light_attenuation[index].values[3] = light.range;
		pixel_data.light_spot[index].values[0] = light.falloff;
		pixel_data.light_spot[index].values[1] = light.theta;
		pixel_data.light_spot[index].values[2] = light.phi;
	}
	const bool pixel_constants_changed = !uploaded_pixel_constants_valid ||
		std::memcmp(&uploaded_pixel_constants, &pixel_data,
			sizeof(pixel_data)) != 0;
	if (pixel_constants_changed)
	{
		if (SUCCEEDED(context->Map(pixel_constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource)))
		{
			std::memcpy(mapped_resource.pData, &pixel_data, sizeof(pixel_data));
			context->Unmap(pixel_constant_buffer, 0);
			uploaded_pixel_constants = pixel_data;
			uploaded_pixel_constants_valid = true;
		}
		else
		{
			upload_succeeded = false;
		}
	}

	if (!constant_buffers_bound)
	{
		ID3D11Buffer *vertex_buffers_to_bind[] = {vertex_constant_buffer};
		ID3D11Buffer *pixel_buffers_to_bind[] = {pixel_constant_buffer};
		context->VSSetConstantBuffers(0, 1, vertex_buffers_to_bind);
		context->PSSetConstantBuffers(1, 1, pixel_buffers_to_bind);
		constant_buffers_bound = true;
	}
	if (upload_succeeded)
	{
		constant_state_dirty = false;
	}
}

void DX11BackendState::Apply_D3D_States()
{
	if (device == nullptr || context == nullptr)
	{
		return;
	}
	if (native_state_valid && !native_state_dirty &&
		!constant_state_dirty && constant_buffers_bound)
	{
		return;
	}
	const bool update_native_state = native_state_dirty || !native_state_valid;
	if (update_native_state)
	{

	D3D11_BLEND_DESC blend_description = {};
	blend_description.RenderTarget[0].BlendEnable = alpha_blend_enabled ? TRUE : FALSE;
	blend_description.RenderTarget[0].SrcBlend = To_D3D_Blend(source_blend);
	blend_description.RenderTarget[0].DestBlend = To_D3D_Blend(destination_blend);
	blend_description.RenderTarget[0].BlendOp = To_D3D_Blend_Operation(blend_operation);
		blend_description.RenderTarget[0].SrcBlendAlpha = To_D3D_Alpha_Blend(source_blend);
		blend_description.RenderTarget[0].DestBlendAlpha = To_D3D_Alpha_Blend(destination_blend);
	blend_description.RenderTarget[0].BlendOpAlpha = To_D3D_Blend_Operation(blend_operation);
	blend_description.RenderTarget[0].RenderTargetWriteMask = static_cast<UINT8>(color_write_mask);
	if (!blend_description_valid || std::memcmp(&cached_blend_description,
		&blend_description, sizeof(blend_description)) != 0)
	{
		ID3D11BlendState *new_blend_state = nullptr;
		if (SUCCEEDED(device->CreateBlendState(&blend_description, &new_blend_state)))
		{
			Release_Com(blend_state);
			blend_state = new_blend_state;
			cached_blend_description = blend_description;
			blend_description_valid = true;
		}
	}

	D3D11_DEPTH_STENCIL_DESC depth_description = {};
	depth_description.DepthEnable = depth_test_enabled ? TRUE : FALSE;
	depth_description.DepthWriteMask = depth_write_enabled ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
	depth_description.DepthFunc = To_D3D_Compare(depth_function);
	depth_description.StencilEnable = stencil_enabled ? TRUE : FALSE;
	depth_description.StencilReadMask = static_cast<UINT8>(stencil_read_mask);
	depth_description.StencilWriteMask = static_cast<UINT8>(stencil_write_mask);
	depth_description.FrontFace.StencilFunc = To_D3D_Compare(stencil_function);
	depth_description.FrontFace.StencilDepthFailOp = To_D3D_Stencil(stencil_z_fail);
	depth_description.FrontFace.StencilFailOp = To_D3D_Stencil(stencil_fail);
	depth_description.FrontFace.StencilPassOp = To_D3D_Stencil(stencil_pass);
	depth_description.BackFace = depth_description.FrontFace;
	if (!depth_description_valid || std::memcmp(&cached_depth_description,
		&depth_description, sizeof(depth_description)) != 0)
	{
		ID3D11DepthStencilState *new_depth_state = nullptr;
		if (SUCCEEDED(device->CreateDepthStencilState(&depth_description, &new_depth_state)))
		{
			Release_Com(depth_state);
			depth_state = new_depth_state;
			cached_depth_description = depth_description;
			depth_description_valid = true;
		}
	}

	const RenderBackendCullMode effective_cull_mode =
		cull_mode_override_count != 0 ?
			cull_mode_overrides[cull_mode_override_count - 1] : cull_mode;
	D3D11_RASTERIZER_DESC raster_description = {};
	raster_description.FillMode = fill_mode == RenderBackendFillMode::Point ? D3D11_FILL_SOLID :
		(fill_mode == RenderBackendFillMode::Wireframe ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID);
	raster_description.CullMode = effective_cull_mode == RenderBackendCullMode::None ? D3D11_CULL_NONE : D3D11_CULL_BACK;
	// D3D11 culls the back face, so a CCW front face means clockwise faces are
	// culled. The neutral enum preserves D3D9's "cull this winding" meaning:
	// Clockwise -> CCW front faces, CounterClockwise -> CW front faces.
	raster_description.FrontCounterClockwise = effective_cull_mode == RenderBackendCullMode::Clockwise ? TRUE : FALSE;
	raster_description.DepthBias = static_cast<INT>(depth_bias);
	raster_description.DepthBiasClamp = 0.0f;
	raster_description.SlopeScaledDepthBias = 0.0f;
	raster_description.DepthClipEnable = TRUE;
	if (!raster_description_valid || std::memcmp(&cached_raster_description,
		&raster_description, sizeof(raster_description)) != 0)
	{
		ID3D11RasterizerState *new_raster_state = nullptr;
		if (SUCCEEDED(device->CreateRasterizerState(&raster_description, &new_raster_state)))
		{
			Release_Com(rasterizer_state);
			rasterizer_state = new_raster_state;
			cached_raster_description = raster_description;
			raster_description_valid = true;
		}
	}

	for (unsigned index = 0; index < MAX_TEXTURE_STAGES; ++index)
	{
		D3D11_SAMPLER_DESC sampler_description = {};
		sampler_description.Filter = To_D3D_Filter(stages[index]);
		sampler_description.AddressU = stages[index].address_u == RenderBackendTextureAddressMode::Clamp ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
		sampler_description.AddressV = stages[index].address_v == RenderBackendTextureAddressMode::Clamp ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
		sampler_description.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		sampler_description.MipLODBias = 0.0f;
		sampler_description.MaxAnisotropy = static_cast<UINT>(std::clamp(stages[index].anisotropy, 1u, 16u));
		sampler_description.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
		sampler_description.MinLOD = 0.0f;
		sampler_description.MaxLOD = D3D11_FLOAT32_MAX;
		if (!sampler_description_valid[index] || std::memcmp(
			&cached_sampler_descriptions[index], &sampler_description,
			sizeof(sampler_description)) != 0)
		{
			ID3D11SamplerState *new_sampler = nullptr;
			if (SUCCEEDED(device->CreateSamplerState(&sampler_description, &new_sampler)))
			{
				Release_Com(samplers[index]);
				samplers[index] = new_sampler;
				cached_sampler_descriptions[index] = sampler_description;
				sampler_description_valid[index] = true;
			}
		}
	}

	const float blend_factor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	ID3D11ShaderResourceView *views[MAX_TEXTURE_STAGES] = {};
	ID3D11SamplerState *sampler_views[MAX_TEXTURE_STAGES] = {};
	for (unsigned index = 0; index < MAX_TEXTURE_STAGES; ++index)
	{
		views[index] = textures[index] == nullptr ? nullptr : textures[index]->shader_resource_view;
		sampler_views[index] = samplers[index];
	}
	const bool blend_state_changed = !native_state_valid ||
		applied_blend_state != blend_state;
	const bool depth_state_changed = !native_state_valid ||
		applied_depth_state != depth_state ||
		applied_stencil_reference != stencil_reference;
	const bool rasterizer_state_changed = !native_state_valid ||
		applied_rasterizer_state != rasterizer_state;
	bool shader_resources_changed = !native_state_valid;
	bool samplers_changed = !native_state_valid;
	for (unsigned index = 0; index < MAX_TEXTURE_STAGES; ++index)
	{
		shader_resources_changed = shader_resources_changed ||
			applied_shader_resources[index] != views[index];
		samplers_changed = samplers_changed ||
			applied_samplers[index] != sampler_views[index];
	}
	if (blend_state_changed)
	{
		context->OMSetBlendState(blend_state, blend_factor, 0xffffffff);
	}
	if (depth_state_changed)
	{
		context->OMSetDepthStencilState(depth_state, stencil_reference);
	}
	if (rasterizer_state_changed)
	{
		context->RSSetState(rasterizer_state);
	}
	if (shader_resources_changed)
	{
		context->PSSetShaderResources(0, MAX_TEXTURE_STAGES, views);
	}
	if (samplers_changed)
	{
		context->PSSetSamplers(0, MAX_TEXTURE_STAGES, sampler_views);
	}
	applied_blend_state = blend_state;
	applied_depth_state = depth_state;
	applied_rasterizer_state = rasterizer_state;
	applied_stencil_reference = stencil_reference;
	for (unsigned index = 0; index < MAX_TEXTURE_STAGES; ++index)
	{
		applied_shader_resources[index] = views[index];
		applied_samplers[index] = sampler_views[index];
	}
	native_state_valid = true;
	native_state_dirty = false;
	}
	if (constant_state_dirty || !constant_buffers_bound)
	{
		Upload_Constants();
	}
}
template <typename Host>
void DX11RenderStateBackend<Host>::Invalidate_Cached_Render_States()
{
	this->State().applied_render_state_valid = false;
	this->State().native_state_valid = false;
	this->State().native_state_dirty = true;
	this->State().constant_state_dirty = true;
	this->State().shader_bindings_valid = false;
	ShaderClass::Invalidate();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Render_State(unsigned state, unsigned value)
{
	if (state < this->State().render_states.size())
	{
		this->State().render_states[state] = value;
		this->State().Mark_All_State_Dirty();
	}
}

template <typename Host>
unsigned DX11RenderStateBackend<Host>::Get_Render_State(unsigned state) const
{
	return state < this->State().render_states.size() ?
		this->State().render_states[state] : 0;
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Texture_Stage_State(unsigned stage, unsigned state, unsigned value)
{
	if (stage < MAX_TEXTURE_STAGES && state < 64)
	{
		this->State().texture_stage_states[stage][state] = value;
		this->State().Mark_All_State_Dirty();
	}
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Ambient(const Vector3 & color)
{
	this->State().ambient[0] = color[0];
	this->State().ambient[1] = color[1];
	this->State().ambient[2] = color[2];
	this->State().Mark_Constant_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Light_Environment(LightEnvironmentClass * light_env)
{
	if (light_env == nullptr)
	{
		for (unsigned index = 0; index < this->State().lights.size(); ++index)
		{
			Disable_Light(index);
		}
		return;
	}

	Set_Ambient(light_env->Get_Equivalent_Ambient());
	const unsigned light_count = std::min<unsigned>(
		static_cast<unsigned>(std::max(0, light_env->Get_Light_Count())),
		static_cast<unsigned>(this->State().lights.size()));
	for (unsigned index = 0; index < light_count; ++index)
	{
		RenderBackendLight light = {};
		const Vector3 & diffuse = light_env->isPointLight(static_cast<int>(index)) ?
			light_env->getPointDiffuse(static_cast<int>(index)) :
			light_env->Get_Light_Diffuse(static_cast<int>(index));
		light.diffuse[0] = diffuse[0];
		light.diffuse[1] = diffuse[1];
		light.diffuse[2] = diffuse[2];
		light.diffuse[3] = 1.0f;
		if (light_env->isPointLight(static_cast<int>(index)))
		{
			light.type = RenderBackendLightType::Point;
			const Vector3 & ambient = light_env->getPointAmbient(static_cast<int>(index));
			light.ambient[0] = ambient[0];
			light.ambient[1] = ambient[1];
			light.ambient[2] = ambient[2];
			light.ambient[3] = 1.0f;
			const Vector3 & position = light_env->getPointCenter(static_cast<int>(index));
			light.position[0] = position[0];
			light.position[1] = position[1];
			light.position[2] = position[2];
			light.range = light_env->getPointOrad(static_cast<int>(index));
			const float inner_radius = light_env->getPointIrad(static_cast<int>(index));
			light.attenuation0 = 1.0f;
			light.attenuation1 = std::fabs(inner_radius - light.range) < 1.0e-5f ||
				inner_radius <= 1.0e-5f ? 0.0f : 0.1f / inner_radius;
			light.attenuation2 = light.range > 1.0e-5f ? 8.0f / (light.range * light.range) : 0.0f;
		}
		else
		{
			light.type = RenderBackendLightType::Directional;
			const Vector3 direction = -light_env->Get_Light_Direction(static_cast<int>(index));
			light.direction[0] = direction[0];
			light.direction[1] = direction[1];
			light.direction[2] = direction[2];
			if (index == 0)
			{
				light.specular[0] = 1.0f;
				light.specular[1] = 1.0f;
				light.specular[2] = 1.0f;
				light.specular[3] = 1.0f;
			}
		}
		this->State().lights[index] = light;
		this->State().light_enabled[index] = true;
		this->State().light_position_camera_space[index] = false;
		this->State().light_direction_camera_space[index] = true;
		this->State().render_state.Lights[index] = light;
		this->State().render_state.LightEnable[index] = true;
	}
	for (unsigned index = light_count; index < this->State().lights.size(); ++index)
	{
		Disable_Light(index);
	}
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Fog(bool enable, const Vector3 & color, float start, float end)
{
	this->State().fog_enabled = enable;
	this->State().fog_color[0] = color[0];
	this->State().fog_color[1] = color[1];
	this->State().fog_color[2] = color[2];
	this->State().fog_color[3] = 1.0f;
	this->State().fog_start = start;
	this->State().fog_end = end;
	this->State().Mark_Constant_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Material_Values(const RenderBackendMaterial & material)
{
	this->State().material = material;
	this->State().Mark_Constant_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Fill_Mode(RenderBackendFillMode mode)
{
	this->State().fill_mode = mode;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
RenderBackendFillMode DX11RenderStateBackend<Host>::Get_Fill_Mode() const
{
	return this->State().fill_mode;
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Color_Write_Mask(RenderBackendColorWriteMask mask)
{
	this->State().color_write_mask = mask;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
RenderBackendColorWriteMask DX11RenderStateBackend<Host>::Get_Color_Write_Mask() const
{
	return this->State().color_write_mask;
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Alpha_Blend_Enabled(bool enable)
{
	this->State().alpha_blend_enabled = enable;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Blend_Operation(RenderBackendBlendOperation operation)
{
	this->State().blend_operation = operation;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Blend_Factors(RenderBackendBlendFactor source, RenderBackendBlendFactor destination)
{
	this->State().source_blend = source;
	this->State().destination_blend = destination;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Source_Blend_Factor(RenderBackendBlendFactor factor)
{
	this->State().source_blend = factor;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Destination_Blend_Factor(RenderBackendBlendFactor factor)
{
	this->State().destination_blend = factor;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Alpha_Test_Enabled(bool enable)
{
	this->State().alpha_test_enabled = enable;
	this->State().Mark_Constant_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Alpha_Test_Function(RenderBackendCompareFunction function)
{
	this->State().alpha_function = function;
	this->State().Mark_Constant_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Alpha_Test_Reference(unsigned reference)
{
	this->State().alpha_reference = reference;
	this->State().Mark_Constant_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Fog_Enabled(bool enable)
{
	this->State().fog_enabled = enable;
	this->State().Mark_Constant_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Fog_Color(unsigned color)
{
	const Vector4 unpacked = Color_From_Packed(color);
	this->State().fog_color[0] = unpacked[0];
	this->State().fog_color[1] = unpacked[1];
	this->State().fog_color[2] = unpacked[2];
	this->State().fog_color[3] = unpacked[3];
	this->State().Mark_Constant_State_Dirty();
}

template <typename Host>
unsigned DX11RenderStateBackend<Host>::Get_Depth_Bias() const
{
    return this->State().depth_bias;
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Depth_Bias(unsigned bias)
{
	this->State().depth_bias = bias;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Texture_Factor(unsigned color)
{
	this->State().texture_factor = color;
	this->State().Mark_Constant_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Depth_Test_Enabled(bool enable)
{
	this->State().depth_test_enabled = enable;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Depth_Write_Enabled(bool enable)
{
	this->State().depth_write_enabled = enable;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Depth_Function(RenderBackendCompareFunction function)
{
	this->State().depth_function = function;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Cull_Mode(RenderBackendCullMode mode)
{
	this->State().cull_mode = mode;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
RenderBackendCullMode DX11RenderStateBackend<Host>::Get_Cull_Mode() const
{
	return this->State().cull_mode;
}

template <typename Host>
void DX11RenderStateBackend<Host>::Push_Cull_Mode_Override(RenderBackendCullMode mode)
{
	if (this->State().cull_mode_override_count <
		this->State().cull_mode_overrides.size())
	{
		this->State().cull_mode_overrides[
			this->State().cull_mode_override_count++] = mode;
		this->State().Mark_Native_State_Dirty();
	}
}

template <typename Host>
void DX11RenderStateBackend<Host>::Pop_Cull_Mode_Override()
{
	if (this->State().cull_mode_override_count != 0)
	{
		--this->State().cull_mode_override_count;
		this->State().Mark_Native_State_Dirty();
	}
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Point_Sprite_Enabled(bool enable)
{
	this->State().point_sprite_enabled = enable;
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Point_Scale_Enabled(bool enable)
{
	this->State().point_scale_enabled = enable;
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Point_Size(float size)
{
	this->State().point_size = size;
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Point_Size_Min(float size)
{
	this->State().point_size_min = size;
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Point_Size_Max(float size)
{
	this->State().point_size_max = size;
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Point_Scale(float scale_a, float scale_b, float scale_c)
{
	this->State().point_scale[0] = scale_a;
	this->State().point_scale[1] = scale_b;
	this->State().point_scale[2] = scale_c;
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Shade_Mode(RenderBackendShadeMode mode)
{
	this->State().shade_mode = mode;
	this->State().Mark_Constant_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Lighting_Enabled(bool enable)
{
	this->State().lighting_enabled = enable;
	this->State().Mark_Constant_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Normalize_Normals(bool enable)
{
	this->State().normalize_normals = enable;
	this->State().Mark_Constant_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Specular_Enabled(bool enable)
{
	this->State().specular_enabled = enable;
	this->State().Mark_Constant_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Material_Color_Sources(RenderBackendMaterialSource ambient, RenderBackendMaterialSource diffuse, RenderBackendMaterialSource emissive)
{
	this->State().ambient_source = ambient;
	this->State().diffuse_source = diffuse;
	this->State().emissive_source = emissive;
	this->State().Mark_Constant_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_NPatch_Segments(float segments)
{
	this->State().npatch_segments = segments;
}

template <typename Host>
RenderBackendFogState DX11RenderStateBackend<Host>::Get_Fog_State() const
{
    const auto& state=this->State();
    RenderBackendFogState result;
    result.enabled=state.fog_enabled;
    std::memcpy(result.color,state.fog_color,sizeof(result.color));
    result.start=state.fog_start;
    result.end=state.fog_end;
    return result;
}

template <typename Host>
RenderBackendStencilState DX11RenderStateBackend<Host>::Get_Stencil_State() const
{
    const auto& state = this->State();
    return {state.stencil_enabled,state.stencil_reference,state.stencil_read_mask,
        state.stencil_write_mask,state.stencil_function,state.stencil_fail,
        state.stencil_z_fail,state.stencil_pass};
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Stencil_Enabled(bool enable)
{
	this->State().stencil_enabled = enable;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Stencil_Function(RenderBackendCompareFunction function)
{
	this->State().stencil_function = function;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Stencil_Reference(unsigned reference)
{
	this->State().stencil_reference = reference;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Stencil_Read_Mask(unsigned mask)
{
	this->State().stencil_read_mask = mask;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Stencil_Write_Mask(unsigned mask)
{
	this->State().stencil_write_mask = mask;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Stencil_Z_Fail_Operation(RenderBackendStencilOperation operation)
{
	this->State().stencil_z_fail = operation;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Stencil_Fail_Operation(RenderBackendStencilOperation operation)
{
	this->State().stencil_fail = operation;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Stencil_Pass_Operation(RenderBackendStencilOperation operation)
{
	this->State().stencil_pass = operation;
	this->State().Mark_Native_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Texture_Operation(unsigned stage, RenderBackendTextureComponent component, RenderBackendTextureOperation operation)
{
	if (stage >= MAX_TEXTURE_STAGES)
	{
		return;
	}
	if (component == RenderBackendTextureComponent::Color)
	{
		this->State().stages[stage].color_operation = operation;
	}
	else
	{
		this->State().stages[stage].alpha_operation = operation;
	}
	this->State().Mark_Constant_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Texture_Argument(unsigned stage, RenderBackendTextureComponent component, unsigned argument_index, RenderBackendTextureArgument argument, RenderBackendTextureArgumentModifiers modifiers)
{
	if (stage >= MAX_TEXTURE_STAGES || argument_index >= 3)
	{
		return;
	}
	DX11StageState & stage_state = this->State().stages[stage];
	if (component == RenderBackendTextureComponent::Color)
	{
		stage_state.color_argument[argument_index] = argument;
		stage_state.color_modifiers[argument_index] = modifiers;
	}
	else if (argument_index == 1 || argument_index == 2)
	{
		stage_state.alpha_argument[argument_index] = argument;
		stage_state.alpha_modifiers[argument_index] = modifiers;
	}
	this->State().Mark_Constant_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Texture_Coordinate_Source(unsigned stage, RenderBackendTextureCoordinateSource source, unsigned uv_array_index)
{
	if (stage < MAX_TEXTURE_STAGES)
	{
		this->State().stages[stage].coordinate_source = source;
		this->State().stages[stage].uv_array_index = std::min(uv_array_index, MAX_TEXTURE_STAGES - 1);
		this->State().Mark_Constant_State_Dirty();
	}
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Texture_Transform_Flags(unsigned stage, RenderBackendTextureTransformFlags flags)
{
	if (stage < MAX_TEXTURE_STAGES)
	{
		this->State().stages[stage].transform_flags = flags;
		this->State().Mark_Constant_State_Dirty();
	}
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Texture_Address_Mode(unsigned stage, bool u_coordinate, RenderBackendTextureAddressMode mode)
{
	if (stage < MAX_TEXTURE_STAGES)
	{
		if (u_coordinate)
		{
			this->State().stages[stage].address_u = mode;
		}
		else
		{
			this->State().stages[stage].address_v = mode;
		}
		this->State().Mark_Native_State_Dirty();
	}
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Texture_Filter(unsigned stage, RenderBackendTextureFilterType type, RenderBackendTextureFilter filter)
{
	if (stage < MAX_TEXTURE_STAGES)
	{
		DX11StageState & stage_state = this->State().stages[stage];
		switch (type)
		{
		case RenderBackendTextureFilterType::Minification: stage_state.min_filter = filter; break;
		case RenderBackendTextureFilterType::Magnification: stage_state.mag_filter = filter; break;
		case RenderBackendTextureFilterType::MipMap: stage_state.mip_filter = filter; break;
		}
		this->State().Mark_Native_State_Dirty();
	}
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Texture_Max_Anisotropy(unsigned stage, unsigned level)
{
	if (stage < MAX_TEXTURE_STAGES)
	{
		this->State().stages[stage].anisotropy = std::max(1u, level);
		this->State().Mark_Native_State_Dirty();
	}
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Texture_Bump_Environment_Matrix(unsigned stage, float m00, float m01, float m10, float m11, float scale, float offset)
{
	if (stage < MAX_TEXTURE_STAGES)
	{
		float *matrix = this->State().stages[stage].bump_matrix;
		matrix[0] = m00;
		matrix[1] = m01;
		matrix[2] = m10;
		matrix[3] = m11;
		matrix[4] = scale;
		matrix[5] = offset;
		this->State().Mark_Constant_State_Dirty();
	}
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Material(const VertexMaterialClass * material)
{
	REF_PTR_SET(this->State().render_state.material,
		const_cast<VertexMaterialClass *>(material));
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Light(unsigned index, const LightClass & light)
{
	if (index >= this->State().lights.size())
	{
		return;
	}

	RenderBackendLight target = {};
	switch (light.Get_Type())
	{
	case LightClass::POINT: target.type = RenderBackendLightType::Point; break;
	case LightClass::SPOT: target.type = RenderBackendLightType::Spot; break;
	case LightClass::DIRECTIONAL: target.type = RenderBackendLightType::Directional; break;
	default: target.type = RenderBackendLightType::Unknown; break;
	}
	Vector3 color;
	light.Get_Diffuse(&color);
	color *= light.Get_Intensity();
	target.diffuse[0] = color[0];
	target.diffuse[1] = color[1];
	target.diffuse[2] = color[2];
	target.diffuse[3] = 1.0f;
	light.Get_Specular(&color);
	color *= light.Get_Intensity();
	target.specular[0] = color[0];
	target.specular[1] = color[1];
	target.specular[2] = color[2];
	target.specular[3] = 1.0f;
	light.Get_Ambient(&color);
	color *= light.Get_Intensity();
	target.ambient[0] = color[0];
	target.ambient[1] = color[1];
	target.ambient[2] = color[2];
	target.ambient[3] = 1.0f;
	color = light.Get_Position();
	target.position[0] = color[0];
	target.position[1] = color[1];
	target.position[2] = color[2];
	light.Get_Spot_Direction(color);
	target.direction[0] = color[0];
	target.direction[1] = color[1];
	target.direction[2] = color[2];
	target.range = light.Get_Attenuation_Range();
	target.falloff = light.Get_Spot_Exponent();
	target.theta = light.Get_Spot_Angle();
	target.phi = light.Get_Spot_Angle();
	double attenuation_start = 0.0;
	double attenuation_end = 0.0;
	light.Get_Far_Attenuation_Range(attenuation_start, attenuation_end);
	target.attenuation0 = 1.0f;
	target.attenuation1 = std::fabs(attenuation_start - attenuation_end) < 1.0e-5 ?
		0.0f : attenuation_start > 1.0e-5 ? static_cast<float>(1.0 / attenuation_start) : 0.0f;
	target.attenuation2 = 0.0f;
	this->State().lights[index] = target;
	this->State().light_enabled[index] = true;
	this->State().light_position_camera_space[index] = false;
	this->State().light_direction_camera_space[index] = false;
	this->State().render_state.Lights[index] = target;
	this->State().render_state.LightEnable[index] = true;
	this->State().Mark_Constant_State_Dirty();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Set_Light_From_State(unsigned index, const RenderBackendLight * light)
{
	if (index >= this->State().lights.size())
	{
		return;
	}
	if (light == nullptr)
	{
		this->State().light_enabled[index] = false;
		this->State().light_position_camera_space[index] = false;
		this->State().light_direction_camera_space[index] = false;
		this->State().render_state.LightEnable[index] = false;
		this->State().Mark_Constant_State_Dirty();
	}
	else
	{
		this->State().lights[index] = *light;
		this->State().light_enabled[index] = true;
		this->State().light_position_camera_space[index] = false;
		this->State().light_direction_camera_space[index] = false;
		this->State().render_state.Lights[index] = *light;
		this->State().render_state.LightEnable[index] = true;
		this->State().Mark_Constant_State_Dirty();
	}
}

template <typename Host>
void DX11RenderStateBackend<Host>::Disable_Light(unsigned index)
{
	if (index < this->State().lights.size())
	{
		this->State().light_enabled[index] = false;
		this->State().light_position_camera_space[index] = false;
		this->State().light_direction_camera_space[index] = false;
		this->State().render_state.LightEnable[index] = false;
		this->State().Mark_Constant_State_Dirty();
	}
}

template <typename Host>
void DX11RenderStateBackend<Host>::Capture_Render_State(RenderStateStruct & state)
{
	state = this->State().render_state;
}

template <typename Host>
void DX11RenderStateBackend<Host>::Apply_Render_State(const RenderStateStruct & state)
{
	this->State().Mark_All_State_Dirty();
	this->State().render_state = state;
	this->State().current_shader = ShaderClass(state.shader_bits);
	this->State().transforms[static_cast<unsigned>(RenderBackendTransform::World)] = state.world;
	this->State().transforms[static_cast<unsigned>(RenderBackendTransform::View)] = state.view;
	this->State().base_vertex_offset = state.index_base_offset + state.vba_offset;
	this->State().index_offset = state.iba_offset;
	Apply_Render_State_Changes();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Begin_Backend_Statistics()
{
	
}

template <typename Host>
void DX11RenderStateBackend<Host>::End_Backend_Statistics()
{
	
}

template <typename Host>
void DX11RenderStateBackend<Host>::Restore_Render_State()
{
	this->State().render_state = RenderStateStruct();
	this->State().current_shader = ShaderClass();
	this->State().textures.fill(nullptr);
	this->State().programmable_texture = nullptr;
	this->State().direct_texture_overrides.fill(nullptr);
	this->State().direct_texture_override_valid.fill(false);
	this->State().texture_kinds.fill(RenderBackendTextureKind::Texture2D);
	this->State().Invalidate_Default_Pixel_Shader_Selection();
	this->State().applied_render_state_valid = false;
	this->State().Mark_All_State_Dirty();
	this->State().Apply_D3D_States();
}

template <typename Host>
void DX11RenderStateBackend<Host>::Begin_Programmable_Pass()
{
	this->State().programmable_pass_active = true;
	// A programmable pass owns the native pipeline state independently of the
	// legacy render-state snapshot.  The previous draw may have changed native
	// bindings without changing the neutral state fields, so the cache cannot
	// be trusted at this boundary.  Rebuild all native bindings before the
	// explicit material is drawn.
	this->State().applied_render_state_valid = false;
	this->State().native_state_valid = false;
	this->State().native_state_dirty = true;
	this->State().constant_state_dirty = true;
	this->State().constant_buffers_bound = false;
	this->State().shader_bindings_valid = false;
}

template <typename Host>
void DX11RenderStateBackend<Host>::Apply_Programmable_Render_State_Changes()
{
	if (this->State().context != nullptr)
	{
		// Commit only the explicit backend state. Do not execute ShaderClass,
		// legacy material, or legacy texture callbacks here.
		this->State().Apply_D3D_States();

		// Slot eight is reserved for the extra resource of an explicit modern
		// material. It intentionally does not participate in the legacy
		// constant-buffer stage arrays above.
		ID3D11ShaderResourceView *view =
			this->State().programmable_texture == nullptr ? nullptr :
			this->State().programmable_texture->shader_resource_view;
		this->State().context->PSSetShaderResources(MAX_TEXTURE_STAGES, 1,
			&view);
		// The extra programmable slot is currently used for scene depth, so it
		// always uses the clamped sampler owned by the final regular stage.
		ID3D11SamplerState *sampler =
			this->State().samplers[MAX_TEXTURE_STAGES - 1];
		this->State().context->PSSetSamplers(MAX_TEXTURE_STAGES, 1, &sampler);
	}
}

template <typename Host>
void DX11RenderStateBackend<Host>::End_Programmable_Pass()
{
	if (!this->State().programmable_pass_active)
	{
		return;
	}

	this->State().programmable_pass_active = false;
	this->State().programmable_texture = nullptr;
	this->State().applied_render_state_valid = false;
	this->State().native_state_valid = false;
	this->State().native_state_dirty = true;
	this->State().constant_state_dirty = true;
	this->State().shader_bindings_valid = false;
}

template <typename Host>
void DX11RenderStateBackend<Host>::Apply_Render_State_Changes()
{
	DX11BackendState &impl = this->State();
	if (impl.context == nullptr)
	{
		return;
	}

	const bool state_valid = impl.applied_render_state_valid;
	const bool shader_changed = !state_valid ||
		impl.applied_shader_bits != impl.render_state.shader_bits ||
		ShaderClass::Is_Dirty();
	bool texture_changed = !state_valid;
	for (unsigned stage = 0; stage < MAX_TEXTURE_STAGES && !texture_changed; ++stage)
	{
		texture_changed = impl.applied_textures[stage] != impl.render_state.Textures[stage];
	}
	const bool material_changed = !state_valid ||
		impl.applied_material != impl.render_state.material;
	bool lights_changed = !state_valid;
	for (unsigned index = 0; index < impl.lights.size() && !lights_changed; ++index)
	{
		lights_changed = impl.applied_light_enable[index] !=
			impl.render_state.LightEnable[index] ||
			!Lights_Equal(impl.applied_lights[index], impl.render_state.Lights[index]);
	}
	bool vertex_buffers_changed = !state_valid ||
		impl.applied_vba_offset != impl.render_state.vba_offset ||
		impl.applied_vba_count != impl.render_state.vba_count;
	for (unsigned stream = 0; stream < MAX_VERTEX_STREAMS && !vertex_buffers_changed; ++stream)
	{
		vertex_buffers_changed =
			impl.applied_vertex_buffers[stream] != impl.render_state.vertex_buffers[stream] ||
			impl.applied_vertex_buffer_types[stream] !=
			impl.render_state.vertex_buffer_types[stream];
		VertexBufferClass *vertex_buffer = impl.render_state.vertex_buffers[stream];
		if ((!impl.direct_vertex_binding_override || stream != 0) &&
			(vertex_buffer == nullptr ||
				(impl.render_state.vertex_buffer_types[stream] != BUFFER_TYPE_SORTING &&
					impl.render_state.vertex_buffer_types[stream] != BUFFER_TYPE_DYNAMIC_SORTING)))
		{
			const DX11VertexBuffer *expected_buffer = vertex_buffer == nullptr ? nullptr :
				static_cast<const DX11VertexBuffer *>(vertex_buffer->Get_Backend_Buffer());
			vertex_buffers_changed = vertex_buffers_changed ||
				impl.vertex_buffers[stream] != expected_buffer;
		}
	}
	bool index_buffer_changed = !state_valid ||
		impl.applied_index_buffer != impl.render_state.index_buffer ||
		impl.applied_index_buffer_type != impl.render_state.index_buffer_type ||
		impl.applied_iba_offset != impl.render_state.iba_offset ||
		impl.applied_index_base_offset != impl.render_state.index_base_offset;
	if (!impl.direct_index_binding_override &&
		(impl.render_state.index_buffer == nullptr ||
			(impl.render_state.index_buffer_type != BUFFER_TYPE_SORTING &&
				impl.render_state.index_buffer_type != BUFFER_TYPE_DYNAMIC_SORTING)))
	{
		const DX11IndexBuffer *expected_buffer = impl.render_state.index_buffer == nullptr ? nullptr :
			static_cast<const DX11IndexBuffer *>(impl.render_state.index_buffer->Get_Backend_Buffer());
		index_buffer_changed = index_buffer_changed || impl.index_buffer != expected_buffer;
	}

	// Shader/material/texture application is the part of the deferred WW3D
	// state that can execute engine callbacks.  Only replay it when its source
	// state changed.  In particular, this leaves a low-level vertex/index
	// binding in place when a shadow draw only changes its world transform.
	if (shader_changed || texture_changed || material_changed)
	{
		impl.applying_render_state = true;
		if (shader_changed)
		{
			impl.current_shader = ShaderClass(impl.render_state.shader_bits);
			impl.current_shader.Apply();
		}
		if (texture_changed)
		{
			for (unsigned stage = 0; stage < MAX_TEXTURE_STAGES; ++stage)
			{
				if (impl.render_state.Textures[stage] != nullptr)
				{
					impl.render_state.Textures[stage]->Apply(stage);
				}
				else
				{
					TextureBaseClass::Apply_Null(stage);
				}
			}
		}
		if (material_changed)
		{
			if (impl.render_state.material != nullptr)
			{
				impl.render_state.material->Apply();
			}
			else
			{
				VertexMaterialClass::Apply_Null();
			}
		}
		impl.applying_render_state = false;
	}
	for (unsigned stage = 0; stage < MAX_TEXTURE_STAGES; ++stage)
	{
		if (impl.direct_texture_override_valid[stage])
		{
			impl.textures[stage] = impl.direct_texture_overrides[stage];
		}
	}
	if (lights_changed)
	{
		for (unsigned index = 0; index < impl.lights.size(); ++index)
		{
			if (impl.render_state.LightEnable[index])
			{
				impl.lights[index] = impl.render_state.Lights[index];
				impl.light_enabled[index] = true;
			}
			else
			{
				impl.light_enabled[index] = false;
			}
		}
	}
	impl.transforms[static_cast<unsigned>(RenderBackendTransform::World)] = impl.render_state.world;
	impl.transforms[static_cast<unsigned>(RenderBackendTransform::View)] = impl.render_state.view;
	impl.base_vertex_offset = impl.render_state.index_base_offset + impl.render_state.vba_offset;
	impl.index_offset = impl.render_state.iba_offset;
	if (vertex_buffers_changed)
	{
		impl.applying_render_state_buffers = true;
		for (unsigned stream = 0; stream < MAX_VERTEX_STREAMS; ++stream)
		{
			VertexBufferClass *vertex_buffer = impl.render_state.vertex_buffers[stream];
			if (vertex_buffer != nullptr && impl.render_state.vertex_buffer_types[stream] != BUFFER_TYPE_SORTING &&
				impl.render_state.vertex_buffer_types[stream] != BUFFER_TYPE_DYNAMIC_SORTING)
			{
				if (stream == 0)
				{
					impl.Set_Current_Vertex_Layout(vertex_buffer->Get_Format_Layout());
				}
				const unsigned stride = vertex_buffer->Get_Vertex_Size();
				// WW3D's draw contract carries the dynamic access offset as the
				// draw's base vertex.  DX11 binds every stream at byte offset zero.
				this->Backend().Set_Vertex_Buffer(vertex_buffer->Get_Backend_Buffer(), 0, stride, stream);
				if (stream == 0)
				{
					impl.direct_vertex_binding_override = false;
				}
			}
			else if (vertex_buffer == nullptr)
			{
				this->Backend().Set_Vertex_Buffer(static_cast<RenderBackendVertexBuffer *>(nullptr), 0, 0, stream);
				if (stream == 0)
				{
					impl.direct_vertex_binding_override = false;
				}
			}
		}
		impl.applying_render_state_buffers = false;
	}
	if (index_buffer_changed)
	{
		if (impl.render_state.index_buffer != nullptr &&
			impl.render_state.index_buffer_type != BUFFER_TYPE_SORTING &&
			impl.render_state.index_buffer_type != BUFFER_TYPE_DYNAMIC_SORTING)
		{
			this->Backend().Set_Index_Buffer(impl.render_state.index_buffer->Get_Backend_Buffer());
			impl.direct_index_binding_override = false;
		}
		else if (impl.render_state.index_buffer == nullptr)
		{
			this->Backend().Set_Index_Buffer(static_cast<RenderBackendIndexBuffer *>(nullptr));
			impl.direct_index_binding_override = false;
		}
	}
	impl.Apply_D3D_States();

	impl.applied_shader_bits = impl.render_state.shader_bits;
	impl.applied_material = impl.render_state.material;
	for (unsigned stage = 0; stage < MAX_TEXTURE_STAGES; ++stage)
	{
		impl.applied_textures[stage] = impl.render_state.Textures[stage];
	}
	for (unsigned index = 0; index < impl.lights.size(); ++index)
	{
		impl.applied_lights[index] = impl.render_state.Lights[index];
		impl.applied_light_enable[index] = impl.render_state.LightEnable[index];
	}
	impl.applied_world = impl.render_state.world;
	impl.applied_view = impl.render_state.view;
	for (unsigned stream = 0; stream < MAX_VERTEX_STREAMS; ++stream)
	{
		impl.applied_vertex_buffers[stream] = impl.render_state.vertex_buffers[stream];
		impl.applied_vertex_buffer_types[stream] = impl.render_state.vertex_buffer_types[stream];
	}
	impl.applied_index_buffer = impl.render_state.index_buffer;
	impl.applied_index_buffer_type = impl.render_state.index_buffer_type;
	impl.applied_vba_offset = impl.render_state.vba_offset;
	impl.applied_vba_count = impl.render_state.vba_count;
	impl.applied_iba_offset = impl.render_state.iba_offset;
	impl.applied_index_base_offset = impl.render_state.index_base_offset;
	impl.applied_render_state_valid = true;
}

template class DX11RenderStateBackend<DX11BackendRuntime>;
}
