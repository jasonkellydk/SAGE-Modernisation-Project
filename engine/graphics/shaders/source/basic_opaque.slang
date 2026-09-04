struct BasicOpaqueVertexInput
{
    float3 position : POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct BasicOpaqueVertexOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float3 world_position : TEXCOORD1;
};

struct SkinnedOpaqueVertexInput
{
    float3 position : POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    uint4 bone_indices : BLENDINDICES0;
    float4 bone_weights : BLENDWEIGHT0;
};

struct GPUInstanceData
{
    row_major float4x4 transform;
    float4 bounds;
    uint mesh_index;
    uint material_index;
    uint flags;
    uint reserved;
    uint bone_matrix_index;
    uint bone_matrix_count;
    uint bone_reserved0;
    uint bone_reserved1;
};

struct GPUBoneMatrixData
{
    row_major float4x4 transform;
};

StructuredBuffer<GPUInstanceData> Instances : register(t127);

struct GPULightData
{
    float4 position_range;
    float4 direction_intensity;
    float4 color_inner_angle;
    float outer_angle;
    uint type;
    uint flags;
    uint shadow_data_index;
};

StructuredBuffer<GPULightData> Lights : register(t126);

struct GPUViewData
{
    row_major float4x4 view_projection;
};

StructuredBuffer<GPUViewData> Views : register(t125);

StructuredBuffer<GPUBoneMatrixData> BoneMatrices : register(t124);

cbuffer MaterialParameters : register(b0)
{
    float4 Values[4];
};

BasicOpaqueVertexOutput basic_opaque_vertex(BasicOpaqueVertexInput input, uint instance_id : SV_InstanceID)
{
    BasicOpaqueVertexOutput output;
    const float4 world_position = mul(float4(input.position, 1.0), Instances[instance_id].transform);
    output.position = mul(world_position, Views[0].view_projection);
    output.color = input.color;
    output.world_position = world_position.xyz;
    return output;
}

BasicOpaqueVertexOutput basic_opaque_skinned_vertex(SkinnedOpaqueVertexInput input, uint instance_id : SV_InstanceID)
{
    BasicOpaqueVertexOutput output;
    const GPUInstanceData instance = Instances[instance_id];
    float4 skinned_position = 0.0.xxxx;
    if (instance.bone_matrix_count != 0)
    {
        skinned_position += mul(float4(input.position, 1.0), BoneMatrices[instance.bone_matrix_index + input.bone_indices.x].transform) * input.bone_weights.x;
        skinned_position += mul(float4(input.position, 1.0), BoneMatrices[instance.bone_matrix_index + input.bone_indices.y].transform) * input.bone_weights.y;
        skinned_position += mul(float4(input.position, 1.0), BoneMatrices[instance.bone_matrix_index + input.bone_indices.z].transform) * input.bone_weights.z;
        skinned_position += mul(float4(input.position, 1.0), BoneMatrices[instance.bone_matrix_index + input.bone_indices.w].transform) * input.bone_weights.w;
    }
    else
    {
        skinned_position = float4(input.position, 1.0);
    }
    const float4 world_position = mul(skinned_position, instance.transform);
    output.position = mul(world_position, Views[0].view_projection);
    output.color = input.color;
    output.world_position = world_position.xyz;
    return output;
}

float4 basic_opaque_pixel(BasicOpaqueVertexOutput input) : SV_TARGET
{
    float3 light_color = 1.0.xxx;
    if (Values[1].x > 0.5)
    {
        const GPULightData light = Lights[0];
        const float distance_to_light = length(input.world_position - light.position_range.xyz);
        const float attenuation = saturate(1.0 - distance_to_light / max(light.position_range.w, 0.0001));
        const float enabled = (light.flags & 1) != 0 ? 1.0 : 0.0;
        light_color = light.color_inner_angle.rgb * light.direction_intensity.w * (0.2 + 0.8 * attenuation) * enabled;
    }
    return float4(input.color.rgb * Values[0].rgb * light_color, input.color.a * Values[0].a);
}
