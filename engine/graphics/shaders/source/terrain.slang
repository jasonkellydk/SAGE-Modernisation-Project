#include "resource_access.slangh"
#include "environment_lighting.slangh"
#include "pbr.slangh"
GRAPHICS_TEXTURE_2D(TerrainNormalTexture, float4, 5)
GRAPHICS_TEXTURE_2D(TerrainRoughnessTexture, float4, 6)
GRAPHICS_TEXTURE_2D(TerrainHeightTexture, float4, 7)
GRAPHICS_TEXTURE_2D(TerrainDiffuseTexture, float4, 0)
GRAPHICS_TEXTURE_2D(TerrainBlendTexture, float4, 1)
GRAPHICS_TEXTURE_2D(TerrainCloudTexture, float4, 2)
GRAPHICS_TEXTURE_2D(TerrainLightMapTexture, float4, 3)
GRAPHICS_TEXTURE_2D(TerrainShroudTexture, float4, 4)

GRAPHICS_SAMPLER(TerrainDiffuseSampler, 0)
GRAPHICS_SAMPLER(TerrainBlendSampler, 1)
GRAPHICS_SAMPLER(TerrainCloudSampler, 2)
GRAPHICS_SAMPLER(TerrainLightMapSampler, 3)
GRAPHICS_SAMPLER(TerrainShroudSampler, 4)

struct TerrainVertexInput
{
    float3 position : POSITION0;
    float4 color0 : COLOR0;
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
    float3 normal : NORMAL0;
};

struct TerrainVertexOutput
{
    float4 position : SV_POSITION;
    float4 color0 : COLOR0;
    float2 diffuse_uv : TEXCOORD0;
    float2 blend_uv : TEXCOORD1;
    float2 cloud_uv : TEXCOORD2;
    float2 lightmap_uv : TEXCOORD3;
    float2 shroud_uv : TEXCOORD4;
    float3 world_position : TEXCOORD5;
    float3 normal : TEXCOORD6;
};

cbuffer TerrainConstants : register(b0)
{
    row_major float4x4 ViewProjection;
    float4 TerrainCloudProjection;
    float4 TerrainLightMapProjection;
    float4 TerrainShroudProjection;
    float4 TerrainFeatures;
    float4 TerrainLighting;
    float4 TerrainOptions;
    float4 TerrainCameraPosition;
    float4 TerrainSurface;
};

float2 TerrainProjectedCoordinate(float3 world_position, float4 projection)
{
    return world_position.xy * projection.xy + projection.zw;
}

[shader("vertex")] TerrainVertexOutput terrain_vertex(TerrainVertexInput input)
{
    TerrainVertexOutput output;
    float4 world_position = float4(input.position, 1.0);

    output.position = mul(ViewProjection, world_position);
    output.color0 = input.color0;
    output.diffuse_uv = input.texcoord0;
    output.blend_uv = input.texcoord1;
    output.cloud_uv = TerrainProjectedCoordinate(world_position.xyz,
        TerrainCloudProjection);
    output.lightmap_uv = TerrainProjectedCoordinate(world_position.xyz,
        TerrainLightMapProjection);
    output.shroud_uv = TerrainProjectedCoordinate(world_position.xyz,
        TerrainShroudProjection);
    output.world_position = world_position.xyz;
    output.normal = input.normal;
    return output;
}

struct TerrainPixelInput
{
    float4 position : SV_POSITION;
    float4 color0 : COLOR0;
    float2 diffuse_uv : TEXCOORD0;
    float2 blend_uv : TEXCOORD1;
    float2 cloud_uv : TEXCOORD2;
    float2 lightmap_uv : TEXCOORD3;
    float2 shroud_uv : TEXCOORD4;
    float3 world_position : TEXCOORD5;
    float3 normal : TEXCOORD6;
};

float3 Terrain_Surface_Normal(TerrainPixelInput input, float2 uv)
{
    float3 normal=Surface_Normalize(input.normal,float3(0,0,1));
    if (TerrainOptions.z > .5) return normal;
    float3 bump=TerrainNormalTexture().Sample(TerrainDiffuseSampler(),uv).xyz*2-1;
    if (TerrainSurface.y > .5) bump.y=-bump.y;
    normal=Surface_Mapped_Normal(normal,input.world_position,uv,bump);
    return Surface_Height_Normal(normal,input.world_position,
        TerrainHeightTexture().Sample(TerrainDiffuseSampler(),uv).r*TerrainSurface.z);
}
float4 Shade_Terrain_Surface(TerrainPixelInput input)
{
    const bool overlay=TerrainFeatures.w > 2.5;
    const float3 geometric_normal=Surface_Normalize(input.normal,float3(0,0,1));
    const float3 surface_view=Surface_Normalize(TerrainCameraPosition.xyz-input.world_position,geometric_normal);
    if (TerrainSurface.w > 0 && TerrainOptions.z < .5) {
        uint width,height; TerrainHeightTexture().GetDimensions(width,height);
        const float2 limit=8.0/float2(width,height); // Stay within periodic atlas gutters.
        input.diffuse_uv=Surface_Parallax(TerrainHeightTexture(),TerrainDiffuseSampler(),input.diffuse_uv,
            input.world_position,geometric_normal,surface_view,TerrainSurface.z*TerrainSurface.w,limit);
        input.blend_uv=Surface_Parallax(TerrainHeightTexture(),TerrainBlendSampler(),input.blend_uv,
            input.world_position,geometric_normal,surface_view,TerrainSurface.z*TerrainSurface.w,limit);
    }
    const float weight=overlay ? 0 : saturate(input.color0.a);
    const float3 base=Surface_Linear(TerrainDiffuseTexture().Sample(TerrainDiffuseSampler(),input.diffuse_uv).rgb);
    float3 albedo=base;
    float3 normal=Terrain_Surface_Normal(input,input.diffuse_uv);
    float roughness=TerrainOptions.z > .5 ? .8 : TerrainRoughnessTexture().Sample(TerrainDiffuseSampler(),input.diffuse_uv).r;
    if (!overlay) {
        albedo=lerp(base,Surface_Linear(TerrainBlendTexture().Sample(TerrainBlendSampler(),input.blend_uv).rgb),weight);
        normal=Surface_Normalize(lerp(normal,Terrain_Surface_Normal(input,input.blend_uv),weight),normal);
        if (TerrainOptions.z < .5) roughness=lerp(roughness,TerrainRoughnessTexture().Sample(TerrainBlendSampler(),input.blend_uv).r,weight);
    }
    roughness=clamp(roughness,.05,1);
    const float3 view=Surface_Normalize(TerrainCameraPosition.xyz-input.world_position,normal);
    const float3 visibility=DirectionalVisibility(input.world_position);
    // Macro variation belongs to reflectance, never to final illumination.
    if (TerrainFeatures.y > .5)
        albedo*=Surface_Linear(TerrainLightMapTexture().Sample(TerrainLightMapSampler(),input.lightmap_uv).rgb);
    float3 color=Surface_Lighting(albedo,normal,view,input.world_position,roughness,0);
    color=Scene_Color(color)*TerrainLighting.rgb;
    Surface_Visibility(TerrainLighting.rgb);
    if (TerrainOptions.y > .5) {
        const float3 shroud=TerrainShroudTexture().SampleLevel(TerrainShroudSampler(),input.shroud_uv,0).rgb;
        color*=shroud;Surface_Visibility(shroud);
    }
    return float4(color,overlay ? input.color0.a : 1);
}

float4 Shade_Terrain_Utility(TerrainPixelInput input)
{
    if (TerrainFeatures.w > 4.5)
        return float4(0.0, 0.0, 0.0, TerrainShroudTexture().SampleLevel(TerrainShroudSampler(), input.shroud_uv, 0.0).a);

	if (TerrainFeatures.w > 3.5)
		return float4(0.0, 0.0, 0.0, TerrainDiffuseTexture().SampleLevel(TerrainDiffuseSampler(), input.diffuse_uv, 0.0).a);

	if (TerrainFeatures.z > 0.5)
	{
		return float4(input.color0.rgb * TerrainLighting.rgb, 1.0);
	}

    float4 color = float4(0.0, 0.0, 0.0, 1.0);
    if (TerrainFeatures.w > 2.5)
    {
        // Third-layer tiles are an alpha overlay over the already-composed
        // terrain. Their vertex alpha is the overlay coverage.
        color = TerrainDiffuseTexture().Sample(TerrainDiffuseSampler(), input.diffuse_uv);
        color.rgb *= input.color0.rgb;
        color.a = input.color0.a;
    }
    else if (TerrainFeatures.w > 1.5)
    {
        // The shroud texture stores its modulation in RGB (R5G6B5 in the
        // release path), so this pass is intentionally independent of alpha.
        float3 shroud = TerrainShroudTexture().SampleLevel(TerrainShroudSampler(),
            input.shroud_uv, 0.0).rgb;
        return float4(shroud * TerrainLighting.rgb, 1.0);
    }

    else
    {
        float4 base = TerrainDiffuseTexture().Sample(TerrainDiffuseSampler(), input.diffuse_uv);
        float4 blend = TerrainBlendTexture().Sample(TerrainBlendSampler(), input.blend_uv);
        color = lerp(base, blend, saturate(input.color0.a));
        color.rgb *= input.color0.rgb;
    }

    color.rgb *= TerrainLighting.rgb;

    // Shroud modulation shares the surface depth and coverage.
    if (TerrainOptions.y > 0.5)
    {
        color.rgb *= TerrainShroudTexture().SampleLevel(TerrainShroudSampler(),
            input.shroud_uv, 0.0).rgb;
    }
    return color;
}

[shader("pixel")] SurfaceOutput terrain_pixel(TerrainPixelInput input)
{
    ClipEnvironment(input.world_position);
    // Keep one exit after selecting the lighting path. Early returns here
    // miscompile with the DX11 software driver's derivative control flow.
    float4 result;
    if (TerrainSurface.x > .5 && TerrainFeatures.z < .5
        && (TerrainFeatures.w < 1.5 || (TerrainFeatures.w > 2.5 && TerrainFeatures.w < 3.5)))
        result=Shade_Terrain_Surface(input);
    else
        result=Shade_Terrain_Utility(input);
    return Surface_Output(result);
}
