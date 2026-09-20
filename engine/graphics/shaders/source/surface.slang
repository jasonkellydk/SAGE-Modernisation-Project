#include "resource_access.slangh"
#include "environment_lighting.slangh"
#include "pbr.slangh"
GRAPHICS_TEXTURE_2D(BaseTexture, float4, 0)
GRAPHICS_TEXTURE_2D(CloudTexture, float4, 1)
GRAPHICS_TEXTURE_2D(LightMapTexture, float4, 2)
GRAPHICS_TEXTURE_2D(ShroudTexture, float4, 3)
GRAPHICS_SAMPLER(BaseSampler, 0)
GRAPHICS_SAMPLER(CloudSampler, 1)
GRAPHICS_SAMPLER(LightMapSampler, 2)
GRAPHICS_SAMPLER(ShroudSampler, 3)
GRAPHICS_TEXTURE_2D(SurfaceNormalMap, float4, 4)
GRAPHICS_TEXTURE_2D(SurfaceRoughnessMap, float4, 5)
GRAPHICS_TEXTURE_2D(SurfaceHeightMap, float4, 6)
GRAPHICS_TEXTURE_2D(SurfaceMetallicMap, float4, 7)

cbuffer SurfaceConstants : register(b0)
{
    row_major float4x4 ViewProjection;
    float4 CloudProjection;
    float4 LightMapProjection;
    float4 ShroudProjection;
    float AlphaCutoff;
    float Textured;
    float Cloud;
    float LightMap;
    float MaskedModulation;
    float Shroud;
    float Opacity;
    float ShroudOnly;
    float4 SurfaceMaterial;
    float4 SurfaceDetail;
};

struct VertexInput
{
    float3 position : POSITION0;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};
struct PixelInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float2 world_xy : TEXCOORD1;
    float3 world_position : TEXCOORD2;
};
[shader("vertex")] PixelInput surface_vertex(VertexInput input)
{
    PixelInput output;
    output.position = mul(ViewProjection, float4(input.position, 1));
    output.color = input.color;
    output.uv = input.uv;
    output.world_xy = input.position.xy;
    output.world_position = input.position;
    return output;
}

float4 Shade_surface_pixel(PixelInput input)
{
    ClipEnvironment(input.world_position);
    if (ShroudOnly > 0.5) return float4(ShroudTexture().Sample(ShroudSampler(),
        input.world_xy * ShroudProjection.xy + ShroudProjection.zw).rgb, 1);
    float3 geometric_normal=Surface_Normalize(cross(ddx(input.world_position),ddy(input.world_position)),float3(0,0,1));
    if (geometric_normal.z < 0) geometric_normal=-geometric_normal;
    if (SurfaceMaterial.x > .5 && (uint(SurfaceMaterial.w)&4u)!=0 && SurfaceDetail.z > 0)
        input.uv=Surface_Parallax(SurfaceHeightMap(),BaseSampler(),input.uv,input.world_position,
            geometric_normal,Surface_Normalize(EnvironmentCamera.xyz-input.world_position,geometric_normal),
            SurfaceDetail.x*SurfaceDetail.z,.04.xx);
    float4 base = Textured > 0.5 ? BaseTexture().Sample(BaseSampler(), input.uv) : float4(1, 1, 1, 1);
    if (MaskedModulation > 0.5) {
        float3 noise = LightMapTexture().Sample(LightMapSampler(),
            input.world_xy * LightMapProjection.xy + LightMapProjection.zw).rgb;
        return float4(lerp(input.color.aaa, noise, base.a), 1);
    }
    float4 color = base * input.color;
    color.a *= Opacity;
    if (SurfaceMaterial.x > .5 && EnvironmentPBR.x > .5) {
        float3 normal=Surface_Normalize(cross(ddx(input.world_position),ddy(input.world_position)),float3(0,0,1));
        if (normal.z < 0) normal=-normal;
        uint maps=uint(SurfaceMaterial.w);
        if ((maps&1u)!=0) {
            float3 bump=SurfaceNormalMap().Sample(BaseSampler(),input.uv).xyz*2-1;
            if (SurfaceDetail.y > .5) bump.y=-bump.y;
            normal=Surface_Mapped_Normal(normal,input.world_position,input.uv,bump);
        }
        if ((maps&4u)!=0) normal=Surface_Height_Normal(normal,input.world_position,
            SurfaceHeightMap().Sample(BaseSampler(),input.uv).r*SurfaceDetail.x);
        float roughness=(maps&2u)!=0 ? SurfaceRoughnessMap().Sample(BaseSampler(),input.uv).r : SurfaceMaterial.y;
        float metallic=(maps&8u)!=0 ? SurfaceMetallicMap().Sample(BaseSampler(),input.uv).r : SurfaceMaterial.z;
        float3 albedo=Surface_Linear(base.rgb);
        if (LightMap > .5)
            albedo*=Surface_Linear(LightMapTexture().Sample(LightMapSampler(),
                input.world_xy*LightMapProjection.xy+LightMapProjection.zw).rgb);
        float3 view=Surface_Normalize(EnvironmentCamera.xyz-input.world_position,normal);
        float3 lit=Surface_Lighting(albedo,normal,view,input.world_position,roughness,metallic);
        color.rgb=Scene_Color(lit);
    }
    if (LightMap > .5 && SurfaceMaterial.x < .5) {
        const float4 visibility=LightMapTexture().Sample(LightMapSampler(),
            input.world_xy * LightMapProjection.xy + LightMapProjection.zw);
        color*=visibility;Surface_Visibility(visibility.rgb);
    }
    if (Shroud > 0.5) {
        const float3 visibility=ShroudTexture().Sample(ShroudSampler(),input.world_xy*ShroudProjection.xy+ShroudProjection.zw).rgb;
        color.rgb*=visibility;Surface_Visibility(visibility);
    }
    clip(color.a - AlphaCutoff);
    if (SurfaceMaterial.x < .5) color.rgb=Scene_Legacy_Color(color.rgb);
    return color;
}

[shader("pixel")] SurfaceOutput surface_pixel(PixelInput input)
{
    return Surface_Output(Shade_surface_pixel(input));
}
