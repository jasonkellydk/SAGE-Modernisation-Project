#include "resource_access.slangh"
#include "environment_lighting.slangh"
#include "pbr.slangh"
cbuffer TreeConstants : register(b0)
{
    row_major float4x4 ViewProjection;
    float4 Sway[10];
    float4 ShroudProjection;
    float4 Options;
};
GRAPHICS_TEXTURE_2D(TreeTexture, float4, 0)
GRAPHICS_TEXTURE_2D(ShroudTexture, float4, 1)
GRAPHICS_TEXTURE_2D(TreeNormalMap, float4, 2)
GRAPHICS_TEXTURE_2D(TreeRoughnessMap, float4, 3)
GRAPHICS_TEXTURE_2D(TreeHeightMap, float4, 4)
GRAPHICS_SAMPLER(TreeSampler, 0)
GRAPHICS_SAMPLER(ShroudSampler, 1)
struct TreeInput
{
    float3 position : POSITION0;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float2 reserved : TEXCOORD1;
    float3 sway : NORMAL0;
};
struct TreeOutput
{
    float4 position : SV_POSITION;
    float3 color : COLOR0;
    float2 uv : TEXCOORD0;
    float2 shroud_uv : TEXCOORD1;
    float3 world_position : TEXCOORD2;
    float3 normal : TEXCOORD3;
};
[shader("vertex")] TreeOutput tree_vertex(TreeInput input)
{
    TreeOutput output;
    uint sway_index = uint(clamp(input.sway.x,1.0,10.0))-1;
    float3 world = input.position + (input.position.z-input.sway.z)*Sway[sway_index].xyz;
    output.position = mul(ViewProjection,float4(world,1));
    output.world_position = world;
    float3 n=float3(input.reserved,1-abs(input.reserved.x)-abs(input.reserved.y));
    if (n.z<0) n.xy=(1-abs(n.yx))*select(n.xy<0,-1.0,1.0);
    output.normal=normalize(n);
    output.color = input.color.rgb*input.sway.y;
    output.uv = input.uv;
    output.shroud_uv = world.xy*ShroudProjection.xy+ShroudProjection.zw;
    return output;
}
float4 Shade_tree_pixel(TreeOutput input)
{
    ClipEnvironment(input.world_position);
    float4 texel = TreeTexture().Sample(TreeSampler(),input.uv);
    clip(texel.a-Options.y);
    float3 visibility = Options.x > 0.5 ? ShroudTexture().Sample(ShroudSampler(),input.shroud_uv).rgb : float3(1,1,1);
    if (EnvironmentPBR.x > .5) {
        float3 n=normalize(input.normal);
        float roughness=.85;
        if (Options.w > .5) {
            float3 bump=TreeNormalMap().Sample(TreeSampler(),input.uv).xyz*2-1;
            if (Options.w > 1.5) bump.y=-bump.y;
            n=Surface_Mapped_Normal(n,input.world_position,input.uv,bump);
            n=Surface_Height_Normal(n,input.world_position,TreeHeightMap().Sample(TreeSampler(),input.uv).r*.04);
            roughness=clamp(TreeRoughnessMap().Sample(TreeSampler(),input.uv).r,.3,1);
        }
        float3 v=Surface_Normalize(EnvironmentCamera.xyz-input.world_position,n);
        if (dot(n,v)<0) n=-n;
        float3 albedo=Surface_Linear(texel.rgb)*input.color;
        float3 l=Surface_Normalize(EnvironmentSunDirection.xyz,n);
        float3 lit=Surface_Lighting(albedo,n,v,input.world_position,roughness,0);
        // Thin leaves transmit a small part of directional irradiance.
        lit+=albedo*EnvironmentSunRadiance.rgb*saturate(dot(-n,l))*.05*DirectionalVisibility(input.world_position);
        Surface_Visibility(visibility);
        return float4(Scene_Color(lit)*visibility,texel.a);
    }
    return float4(input.color*texel.rgb*visibility*Options.z,texel.a);
}

[shader("pixel")] SurfaceOutput tree_pixel(TreeOutput input)
{
    return Surface_Output(Shade_tree_pixel(input));
}
