#include "resource_access.slangh"
#include "environment_lighting.slangh"
cbuffer PropViewConstants : register(b0)
{
    row_major float4x4 ViewProjection;
    float4 ShroudProjection;
    float4 FogColor;
    float4 FogState;
    float4 CameraPosition;
    row_major float4x4 View;
};
struct PropLightingData {

    float4 SceneAmbient;
    float4 LightDirection[4];
    float4 LightDiffuse[4];
    float4 LightSpecular[4];
    float4 LightPosition[4];
    float4 LightAttenuation[4];
    float4 LightAmbient[4];
    float4 LightSpot[4];
};
cbuffer PropLightingConstants : register(b1) { PropLightingData SharedLighting; };
cbuffer PropMaterialConstants : register(b2)
{
    float Textured;
    float SecondaryTexture;
    float AlphaCutoff;
    float PrimaryGradient;
    float DetailColor;
    float DetailAlpha;
    float SecondaryGradient;
    float Shroud;
    float ShroudOnly;
    float Opacity;
    float TextureLuminance;
    float NormalInWorldSpace;
    row_major float4x4 UVTransform[2];
    float4 UVSources;
    float4 BumpMatrix;
    float SurfaceModel;
    uint SurfaceMaps;
    float SurfaceNormalScale;
    float SurfaceNormalFlipGreen;
    float SurfaceSpecularScale;
    float SurfaceEmissiveScale;
    float SurfaceRoughness;
    float SurfaceMetallic;
    float SurfaceOcclusionStrength;
    uint SurfaceSpecularChannel;
    uint SurfaceTeamChannel;
    float SurfaceTeamMultiplier;
    float4 SurfaceTeamColor;
    float4 SurfaceHeight;
    float4 MuzzleFlashState;
    float4 VertexMaterialOverride;
};
cbuffer PropObjectConstants : register(b3)
{
    row_major float4x4 World;
};
// Record draws use the same object-constant binding with a different layout.
// A single record needs no per-draw storage-buffer upload/indirection.
cbuffer PropRecordSelectionConstants : register(b3)
{
    uint RecordIndex;
};
GRAPHICS_TEXTURE_2D(BaseTexture, float4, 0)
GRAPHICS_TEXTURE_2D(DetailTexture, float4, 1)
GRAPHICS_TEXTURE_2D(ShroudTexture, float4, 3)
GRAPHICS_SAMPLER(BaseSampler, 0)
GRAPHICS_SAMPLER(DetailSampler, 1)
GRAPHICS_SAMPLER(ShroudSampler, 3)
struct PropInput
{
    float3 position : POSITION0;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float2 secondary_uv : TEXCOORD1;
    float3 normal : NORMAL0;
    float4 material_ambient : COLOR1;
    float4 material_diffuse : COLOR2;
    float4 material_emissive : COLOR3;
    float4 material_specular : COLOR4;
    float4 secondary_color : COLOR5;
    float4 tangent : TEXCOORD2;
    float bone_index : TEXCOORD3;
};
struct PropOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float4 secondary_color : COLOR1;
    float4 material_ambient : COLOR2;
    float4 material_diffuse : COLOR3;
    float4 material_emissive : COLOR4;
    float4 material_specular : COLOR5;
    float3 normal : TEXCOORD4;
    float3 world_position : TEXCOORD5;
    float3 camera_position : TEXCOORD6;
    float3 camera_normal : TEXCOORD7;
    float3 reflection : TEXCOORD8;
    float2 uv : TEXCOORD0;
    float2 secondary_uv : TEXCOORD1;
    float2 shroud_uv : TEXCOORD2;
    float distance : TEXCOORD3;
    float4 tangent : TEXCOORD9;
    nointerpolation uint record_index : TEXCOORD10;
};

float2 Rotate_Muzzle_Flash_UV(float2 uv, float2 pivot, float angle)
{
    float sine, cosine;
    sincos(angle, sine, cosine);
    const float2 centered = uv - pivot;
    // Match the row-vector rotation used by the RA3 source shader.
    return float2(centered.x * cosine + centered.y * sine,
        -centered.x * sine + centered.y * cosine) + pivot;
}

void Prepare_Muzzle_Flash_UV(float2 source_uv,
    out float2 primary_uv, out float2 secondary_uv)
{
    const float2 pivot = MuzzleFlashState.zw;
    primary_uv = Rotate_Muzzle_Flash_UV(source_uv, pivot, MuzzleFlashState.y);
    // The second sample counter-rotates around the same authored pivot.
    secondary_uv = Rotate_Muzzle_Flash_UV(source_uv, pivot, -MuzzleFlashState.y);
}
PropOutput Transform_Prop_Vertex(PropInput input, row_major float4x4 WorldTransform)
{
    PropOutput output;
    output.record_index = 0;
    float3 world_position = mul(WorldTransform,float4(input.position,1)).xyz;
    float3 world_normal = NormalInWorldSpace > 0.5 ? input.normal : mul(WorldTransform,float4(input.normal,0)).xyz;
    output.position = mul(ViewProjection,float4(world_position,1));
    output.color = input.color;
    output.secondary_color = input.secondary_color;
    output.normal = normalize(world_normal);
    output.tangent = float4(mul(WorldTransform,float4(input.tangent.xyz,0)).xyz,input.tangent.w);
    if (SurfaceModel > .5 && NormalInWorldSpace < .5) {
        const float3 r0=WorldTransform[0].xyz, r1=WorldTransform[1].xyz, r2=WorldTransform[2].xyz;
        const float determinant=dot(r0,cross(r1,r2));
        const float orientation=determinant < 0 ? -1 : 1;
        const float3x3 cofactor=float3x3(cross(r1,r2),cross(r2,r0),cross(r0,r1));
        output.normal=normalize(mul(cofactor,input.normal)*orientation);
        output.tangent.w*=orientation;
    }
    output.camera_position = mul(View,float4(world_position,1)).xyz;
    output.camera_normal = 0;
    output.reflection = 0;
    // These vectors exist only for generated texture coordinates. Ordinary
    // authored UVs need neither normalisation nor a reflection calculation.
    if (UVSources.x==2 || UVSources.x==3 || UVSources.z==2 || UVSources.z==3)
        output.camera_normal = normalize(mul(View,float4(world_normal,0)).xyz);
    if (UVSources.x==3 || UVSources.z==3)
        output.reflection = reflect(normalize(output.camera_position),output.camera_normal);
    output.world_position = world_position;
    output.material_ambient = input.material_ambient;
    output.material_diffuse = input.material_diffuse;
    output.material_emissive = input.material_emissive;
    if (VertexMaterialOverride.w != 0) {
        output.material_diffuse.a = VertexMaterialOverride.x;
        if (VertexMaterialOverride.z != 0)
            output.material_diffuse.rgb = VertexMaterialOverride.xxx;
    }
    output.material_emissive.rgb *= VertexMaterialOverride.y;
    output.material_specular = input.material_specular;
    if (MuzzleFlashState.x > 0.5)
        Prepare_Muzzle_Flash_UV(input.uv, output.uv, output.secondary_uv);
    else {
        output.uv = input.uv;
        output.secondary_uv = input.secondary_uv;
    }
    if ((SurfaceMaps & 256u) != 0) {
        output.uv.x += input.color.a;
        output.color.a = 1.0;
    }
    output.shroud_uv = world_position.xy*ShroudProjection.xy+ShroudProjection.zw;
    output.distance = length(CameraPosition.xyz-world_position);
    return output;
}
struct PropInstance { row_major float4x4 world; };
GRAPHICS_STORAGE_BUFFER(PropInstances, PropInstance, 127)
[shader("vertex")] PropOutput prop_vertex(PropInput input) {
    return Transform_Prop_Vertex(input, World);
}
[shader("vertex")] PropOutput prop_vertex_instanced(PropInput input, uint instance : SV_InstanceID) {
    return Transform_Prop_Vertex(input, PropInstances()[instance].world);
}

struct PropInstanceRecord {
    row_major float4x4 world;
    uint4 skin;
};
GRAPHICS_STORAGE_BUFFER(PropRecords, PropInstanceRecord, 127)
GRAPHICS_STORAGE_BUFFER(PropRecordIndices, uint, 126)
struct PropBone { row_major float3x4 transform; };
GRAPHICS_STORAGE_BUFFER(PropSkinPalette, PropBone, 125)
GRAPHICS_STORAGE_BUFFER(PropRecordLighting, PropLightingData, 124)
[shader("vertex")] PropOutput prop_vertex_records(PropInput input, uint instance : SV_InstanceID) {
    const uint index = RecordIndex == 0xffffffffu ? PropRecordIndices()[instance] : RecordIndex;
    const PropInstanceRecord record = PropRecords()[index];
    if (record.skin.y != 0) {
        const row_major float3x4 bone = PropSkinPalette()[record.skin.x+uint(input.bone_index)].transform;
        input.position = mul(bone,float4(input.position,1));
        if (NormalInWorldSpace < .5) input.normal = mul(bone,float4(input.normal,0));
        input.tangent.xyz = mul(bone,float4(input.tangent.xyz,0));
    }
    PropOutput output = Transform_Prop_Vertex(input, record.world);
    output.record_index = index;
    return output;
}

float2 Prop_Coordinate(uint stage,PropOutput input)
{
    float mode = stage==0 ? UVSources.x : UVSources.z;
    float projected = stage==0 ? UVSources.y : UVSources.w;
    float4 coordinate = float4(stage==0 ? input.uv : input.secondary_uv,0,1);
    if (mode==1) coordinate = float4(input.camera_position,1);
    else if (mode==2) coordinate = float4(input.camera_normal,1);
    else if (mode==3) coordinate = float4(input.reflection,1);
    else if (mode==4) coordinate = float4(input.world_position,1);
    coordinate = mul(UVTransform[stage],coordinate);
    if (projected > 0.5) coordinate.xyz /= max(abs(coordinate.w),0.000001);
    return coordinate.xy;
}
#include "prop_surface.slangh"
float4 Shade_Prop_Effect(PropOutput input, PropLightingData lighting)
{
    ClipEnvironment(input.world_position);
    if (ShroudOnly > 0.5) return float4(ShroudTexture().Sample(ShroudSampler(),input.shroud_uv).rgb,1);
    if (MuzzleFlashState.x > 0.5 && Textured > 0.5 && SecondaryTexture > 0.5) {
        float4 color = BaseTexture().Sample(BaseSampler(),Prop_Coordinate(0,input));
        color *= DetailTexture().Sample(DetailSampler(),Prop_Coordinate(1,input));
        const float vertex_alpha = input.color.a;
        const float material_alpha = input.material_diffuse.a;
        const float3 authored_rgb = input.color.rgb*input.material_diffuse.rgb
            + input.material_emissive.rgb;
        color.rgb *= authored_rgb*Opacity;
        color.a *= vertex_alpha*material_alpha*Opacity;
        if (Shroud > 0.5) color.rgb *= ShroudTexture().Sample(ShroudSampler(),input.shroud_uv).rgb;
        if (FogState.z > 0.5) {
            const float fog = saturate((abs(input.camera_position.z)-FogState.x)/max(FogState.y-FogState.x,0.0001));
            color.rgb = lerp(color.rgb,FogColor.rgb,fog);
        }
        return float4(Scene_Legacy_Color(color.rgb),saturate(color.a));
    }
    float4 diffuse = input.color*input.material_diffuse;
    if (input.material_ambient.w > 0.5) {
        const float3 normal=Surface_Normalize(input.normal,float3(0,0,1));
        const float3 view_direction=Surface_Normalize(CameraPosition.xyz-input.world_position,normal);
        const float roughness=clamp(pow(2/(max(input.material_specular.w,0)+2),.25),.12,.85);
        const float3 lit=Surface_Lighting(Surface_Linear(input.material_diffuse.rgb),
            normal,view_direction,input.world_position,roughness,0)+Surface_Linear(input.material_emissive.rgb);
        diffuse=float4(Surface_Encoded(lit),input.material_diffuse.a);
    }
    float4 color = Textured > 0.5 ? BaseTexture().Sample(BaseSampler(),Prop_Coordinate(0,input)) : float4(1,1,1,1);
    float2 bump_offset = 0;
    if (Textured < 0.5) color = diffuse;
    else if (PrimaryGradient == 1) color *= diffuse;
    else if (PrimaryGradient == 2) color = float4(color.rgb+diffuse.rgb,diffuse.a);
    else if (PrimaryGradient == 5) color *= diffuse*float4(2,2,2,1);
    else if (PrimaryGradient == 3 || PrimaryGradient == 4) {
        float2 signed_bump = color.rg*2-1;
        bump_offset = float2(dot(signed_bump,BumpMatrix.xy),dot(signed_bump,BumpMatrix.zw));
        color = diffuse;
    }
    if (TextureLuminance > 0.5) {
        // Preserve the packed texture-factor multiply-add / dot-product material.
        const float3 factor = float3(165,202,142)/255.0;
        const float3 shifted = BaseTexture().Sample(BaseSampler(),Prop_Coordinate(0,input)).rgb*(128.0/255.0)+(128.0/255.0)-0.5;
        color.rgb = saturate(4*dot(shifted,factor-0.5)).xxx;
    }
    if (SecondaryTexture > 0.5) {
        float4 detail = DetailTexture().Sample(DetailSampler(),Prop_Coordinate(1,input)+bump_offset);
        float3 local = color.rgb;
        if (DetailColor == 1) color.rgb = detail.rgb;
        else if (DetailColor == 2) color.rgb *= detail.rgb;
        else if (DetailColor == 3) color.rgb = local+(1-local)*detail.rgb;
        else if (DetailColor == 4) color.rgb += detail.rgb;
        else if (DetailColor == 5) color.rgb -= detail.rgb;
        else if (DetailColor == 6) color.rgb = detail.rgb-local;
        else if (DetailColor == 7) color.rgb = lerp(detail.rgb,local,color.a);
        else if (DetailColor == 8) color.rgb = lerp(detail.rgb,local,detail.a);
        else if (DetailColor == 9) color.rgb += detail.rgb-0.5;
        else if (DetailColor == 10) color.rgb = (local+detail.rgb-0.5)*2;
        else if (DetailColor == 11) color.rgb *= detail.rgb*2;
        else if (DetailColor == 12) color.rgb += color.a*detail.rgb;
        if (DetailAlpha == 1) color.a = detail.a;
        else if (DetailAlpha == 2) color.a *= detail.a;
        else if (DetailAlpha == 3) color.a += (1-color.a)*detail.a;
    }
    if (SecondaryGradient > 0.5) color.rgb += input.secondary_color.rgb;
    color.a *= Opacity;
    clip(color.a-AlphaCutoff);
    if (Shroud > 0.5) color.rgb *= ShroudTexture().Sample(ShroudSampler(),input.shroud_uv).rgb;
    if (FogState.z > 0.5) {
        const float fog = saturate((abs(input.camera_position.z)-FogState.x)/max(FogState.y-FogState.x,0.0001));
        color.rgb = lerp(color.rgb,FogColor.rgb,fog);
    }
    return float4(Scene_Legacy_Color(color.rgb),saturate(color.a));
}

float4 Shade_Modern_Prop_Pixel(PropOutput input, PropLightingData lighting)
{
        ClipEnvironment(input.world_position);
        float4 surface=Shade_Prop_Surface(input, lighting);
        surface.a*=Opacity;
        clip(surface.a-AlphaCutoff);
        if (Shroud > .5) {
            const float3 visibility=ShroudTexture().Sample(ShroudSampler(),input.shroud_uv).rgb;
            surface.rgb*=visibility;Surface_Visibility(visibility);
        }
        if (FogState.z > .5) {
            const float fog=saturate((abs(input.camera_position.z)-FogState.x)/max(FogState.y-FogState.x,.0001));
            surface.rgb=lerp(surface.rgb,Scene_Legacy_Color(FogColor.rgb),fog);
            Surface_Visibility((1-fog).xxx);
        }
        return float4(max(surface.rgb,0),saturate(surface.a));
}
float4 Shade_Prop_Pixel(PropOutput input, PropLightingData lighting)
{
    float4 result;
    if (ShroudOnly < .5 && MuzzleFlashState.x < .5 &&
        (SurfaceModel > .5 || (EnvironmentPBR.x > .5 && input.material_ambient.w > .5
            && SurfaceHeight.w >= 0 && PrimaryGradient <= 1)))
        result=Shade_Modern_Prop_Pixel(input,lighting);
    else result=Shade_Prop_Effect(input,lighting);
    return result;
}
[shader("pixel")] SurfaceOutput prop_pixel(PropOutput input) {
    return Surface_Output(Shade_Prop_Pixel(input, SharedLighting));
}
[shader("pixel")] SurfaceOutput prop_pixel_records(PropOutput input) {
    return Surface_Output(Shade_Prop_Pixel(input, PropRecordLighting()[input.record_index]));
}

// Only interpolate values that affect material discard or environment clipping.
// Wrapping the shared transform preserves every coordinate-generation mode,
// while dead-code elimination removes lighting normals, tangents and colours.
struct PropDepthOutput {
    float4 position : SV_POSITION;
    float3 alpha : COLOR0;
    float2 uv : TEXCOORD0;
    float2 secondary_uv : TEXCOORD1;
    float3 world_position : TEXCOORD5;
    float3 camera_position : TEXCOORD6;
    float3 camera_normal : TEXCOORD7;
    float3 reflection : TEXCOORD8;
};
PropDepthOutput Depth_Prop_Output(PropOutput input) {
    PropDepthOutput output;
    output.position=input.position;
    output.alpha=float3(input.color.a,input.material_diffuse.a,input.material_ambient.w);
    output.uv=input.uv;
    output.secondary_uv=input.secondary_uv;
    output.world_position=input.world_position;
    output.camera_position=input.camera_position;
    output.camera_normal=input.camera_normal;
    output.reflection=input.reflection;
    return output;
}
[shader("vertex")] PropDepthOutput prop_depth_vertex(PropInput input) {
    return Depth_Prop_Output(prop_vertex(input));
}
[shader("vertex")] PropDepthOutput prop_depth_vertex_instanced(PropInput input, uint instance : SV_InstanceID) {
    return Depth_Prop_Output(prop_vertex_instanced(input,instance));
}
[shader("vertex")] PropDepthOutput prop_depth_vertex_records(PropInput input, uint instance : SV_InstanceID) {
    return Depth_Prop_Output(prop_vertex_records(input,instance));
}
[shader("pixel")] void prop_depth_pixel(PropDepthOutput depth) {
    PropOutput input=(PropOutput)0;
    input.position=depth.position;
    input.color.a=depth.alpha.x;
    input.material_diffuse.a=depth.alpha.y;
    input.material_ambient.w=depth.alpha.z;
    input.uv=depth.uv;
    input.secondary_uv=depth.secondary_uv;
    input.world_position=depth.world_position;
    input.camera_position=depth.camera_position;
    input.camera_normal=depth.camera_normal;
    input.reflection=depth.reflection;
    Shade_Prop_Pixel(input, SharedLighting);
}
