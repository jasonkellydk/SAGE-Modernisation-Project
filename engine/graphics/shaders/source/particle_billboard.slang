#include "pbr_brdf.slangh"
#include "environment_lighting.slangh"
#include "resource_access.slangh"
struct ParticleVertexInput
{
    float3 position : POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct ParticleVertexOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    uint emitter_flags : TEXCOORD1;
    uint texture_index : TEXCOORD2;
    float point_size_value : TEXCOORD3;
    float2 point_center : TEXCOORD4;
    nointerpolation float4 texture_region : TEXCOORD5;
    float3 world_position : TEXCOORD6;
    nointerpolation float4 animation : TEXCOORD7;
    nointerpolation float radius : TEXCOORD8;
    nointerpolation uint normal_texture_index : TEXCOORD9;
    nointerpolation float angle : TEXCOORD10;
};

struct ParticlePixelInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    uint emitter_flags : TEXCOORD1;
    uint texture_index : TEXCOORD2;
    float point_size_value : TEXCOORD3;
    float2 point_center : TEXCOORD4;
    nointerpolation float4 texture_region : TEXCOORD5;
    float3 world_position : TEXCOORD6;
    nointerpolation float4 animation : TEXCOORD7;
    nointerpolation float radius : TEXCOORD8;
    nointerpolation uint normal_texture_index : TEXCOORD9;
    nointerpolation float angle : TEXCOORD10;
};

struct GPUParticleData
{
    float4 position_lifetime;
    float4 velocity_size;
    float4 color;
    uint material_index;
    uint emitter_flags;
    float angle;
    uint texture_index;
    float4 texture_region;
    float4 animation;
    uint normal_texture_index;
    uint3 padding;
};

GRAPHICS_STORAGE_BUFFER(Particles, GPUParticleData, 127)
GRAPHICS_TEXTURE_ARRAY(Textures, float4, 0, 8)
GRAPHICS_TEXTURE_2D(ParticleSceneDepth, float, 126)
GRAPHICS_SAMPLER(ParticleSampler, 0)

cbuffer MaterialParameters : register(b0)
{
    float4 Values[4];
};

cbuffer ParticleFrame : register(b1)
{
    row_major float4x4 ParticleView;
    row_major float4x4 ParticleProjection;
    uint4 ParticleOffset;
    float4 ParticleDepthOptions;
};

ParticleVertexOutput particle_billboard_vertex(ParticleVertexInput input, uint instance_id : SV_InstanceID)
{
    const GPUParticleData particle = Particles()[ParticleOffset.x + instance_id];
    const float cosine = cos(particle.angle);
    const float sine = sin(particle.angle);
    const bool billboard = (particle.emitter_flags & 2u) != 0;
    const bool point_sprite = (particle.emitter_flags & 32u) != 0;
    const float size = particle.velocity_size.w;
    const float4 world_center = float4(particle.position_lifetime.xyz,1);
    const float4 view_center = mul(ParticleView,world_center);
    const float4 clip_center = mul(ParticleProjection,view_center);
    ParticleVertexOutput output;
    if (point_sprite) {
        output.position = clip_center;
        output.position.xy += input.position.xy * max(size,1.0) / Values[1].xy * clip_center.w;
    } else if (billboard) {
        const float2 corner = float2(cosine*input.position.x-sine*input.position.y,
            sine*input.position.x+cosine*input.position.y)*size;
        output.position = mul(ParticleProjection,view_center+float4(corner,0,0));
    } else {
        // Ground quads rotate in world XY. Project each corner to retain
        // perspective interpolation and its depth against scene geometry.
        const float2 corner = float2(cosine*input.position.x+sine*input.position.y,
            -sine*input.position.x+cosine*input.position.y)*size;
        output.position = mul(ParticleProjection,mul(ParticleView,world_center+float4(corner,0,0)));
    }
    output.animation=particle.animation;
    output.radius=size;
    output.normal_texture_index=particle.normal_texture_index;
    output.angle=particle.angle;
    output.point_size_value = point_sprite ? max(size,1.0) : 1.0;
    output.point_center = clip_center.xy / max(abs(clip_center.w),0.000001);
    output.color = particle.color * Values[0];
    output.world_position=world_center.xyz;
    // Ground effects assign UV(0,0) to the rotated world (+X,+Y)
    // corner; billboards assign it to the upper-left view-space corner.
    output.uv = !billboard && !point_sprite ? float2(1.0-input.uv.x,input.uv.y) : input.uv;
    output.emitter_flags = particle.emitter_flags;
    output.texture_index = particle.texture_index;
    output.texture_region = particle.texture_region;
    return output;
}


// Reconstruct both perspective and orthographic depth without assuming a
// particular near/far plane. Projection matrices use the engine's 0..1 depth.
float3 ParticleViewPosition(float2 pixel, float depth)
{
    float2 ndc=(pixel-Values[1].zw)/Values[1].xy*float2(2,-2)+float2(-1,1);
    float z=(ParticleProjection[2][3]-depth*ParticleProjection[3][3])/
        (depth*ParticleProjection[3][2]-ParticleProjection[2][2]);
    float w=ParticleProjection[3][2]*z+ParticleProjection[3][3];
    return float3((ndc.x*w-ParticleProjection[0][2]*z-ParticleProjection[0][3])/ParticleProjection[0][0],
        (ndc.y*w-ParticleProjection[1][2]*z-ParticleProjection[1][3])/ParticleProjection[1][1],z);
}
float3 ParticleWorldPosition(float3 view)
{
    return mul(transpose((float3x3)ParticleView),view-float3(ParticleView[0][3],ParticleView[1][3],ParticleView[2][3]));
}
float ParticlePhase(float cosine)
{
    const float g=.25;
    return (1-g*g)/(12.56637061*pow(max(1+g*g-2*g*cosine,.01),1.5));
}
float ParticleLightResponse(float3 light, float3 view, float3 normal)
{
    // Authored normal maps describe the smoke surface; unmapped particles
    // retain the scattering phase response. Both use the scene light/shadow data.
    return dot(normal,normal)>.5 ? saturate(dot(normal,light))/3.14159265 : ParticlePhase(dot(light,view));
}
float4 ParticleTexture(uint index,float2 uv,float2 dx,float2 dy)
{
#if defined(GRAPHICS_DX12)
    if(index<8u) return Textures[NonUniformResourceIndex(index)].SampleGrad(ParticleSampler(),uv,dx,dy);
#else
    switch(index) {
    case 0: return Textures[0].SampleGrad(ParticleSampler(),uv,dx,dy);
    case 1: return Textures[1].SampleGrad(ParticleSampler(),uv,dx,dy);
    case 2: return Textures[2].SampleGrad(ParticleSampler(),uv,dx,dy);
    case 3: return Textures[3].SampleGrad(ParticleSampler(),uv,dx,dy);
    case 4: return Textures[4].SampleGrad(ParticleSampler(),uv,dx,dy);
    case 5: return Textures[5].SampleGrad(ParticleSampler(),uv,dx,dy);
    case 6: return Textures[6].SampleGrad(ParticleSampler(),uv,dx,dy);
    case 7: return Textures[7].SampleGrad(ParticleSampler(),uv,dx,dy);
    }
#endif
    return 1.xxxx;
}
float3 ParticleIncident(float3 position, float3 toward_camera, float3 normal)
{
    float3 incident=(EnvironmentSkyRadiance.rgb+EnvironmentGroundRadiance.rgb)*.5*EnvironmentPBR.z;
    const float3 visibility=DirectionalVisibility(position);
    incident+=EnvironmentSunRadiance.rgb*ParticleLightResponse(EnvironmentSunDirection.xyz,toward_camera,normal)*visibility;
    [unroll] for(int sun=0;sun<2;++sun)
        incident+=EnvironmentSecondarySunRadiance[sun].rgb*ParticleLightResponse(EnvironmentSecondarySunDirection[sun].xyz,toward_camera,normal)*visibility;
    [loop] for(int i=0;i<min(int(EnvironmentLocalLightOptions.x),64);++i) {
        const float3 delta=EnvironmentLocalPositions[i].xyz-position;
        const float distance=length(delta);
        if(distance<EnvironmentLocalPositions[i].w) {
            const float3 light=Surface_Normalize(delta,float3(0,0,1));
            float attenuation=Surface_Radiant_Attenuation(distance,EnvironmentLocalPositions[i].w);
            if(EnvironmentLocalDirection[i].w>-1) {
                const float cone=dot(-light,Surface_Normalize(EnvironmentLocalDirection[i].xyz,float3(0,0,-1)));
                attenuation*=pow(saturate((cone-EnvironmentLocalDirection[i].w)/max(.0001,EnvironmentLocalSpot[i].x-EnvironmentLocalDirection[i].w)),max(1,EnvironmentLocalSpot[i].y));
            }
            attenuation*=LocalLightVisibility(i,position,float3(0,0,0));
            incident+=EnvironmentLocalDiffuse[i].rgb*attenuation*ParticleLightResponse(light,toward_camera,normal);
        }
    }
    return incident;
}
float4 particle_billboard_pixel(ParticlePixelInput input) : SV_TARGET
{
    const bool point_sprite = (input.emitter_flags & 32u) != 0;
    const float2 point_center_pixels = float2(
        Values[1].z + (input.point_center.x + 1.0) * 0.5 * Values[1].x,
        Values[1].w + (1.0 - input.point_center.y) * 0.5 * Values[1].y);
    const float2 sprite_uv = point_sprite
        ? (input.position.xy - point_center_pixels) / max(input.point_size_value, 1.0) + 0.5
        : input.uv;
    const float2 grid=max(input.animation.yz,1.xx);
    const float frame=clamp(input.animation.x,0,max(input.animation.w-1,0));
    const float first=floor(frame), next=min(first+1,max(input.animation.w-1,0));
    const float2 cell0=float2(fmod(first,grid.x),floor(first/grid.x));
    const float2 cell1=float2(fmod(next,grid.x),floor(next/grid.x));
    const float2 local_uv=grid.x*grid.y>1 ? clamp(sprite_uv,.006,.994) : sprite_uv;
    const float2 uv0=lerp(input.texture_region.xy,input.texture_region.zw,(cell0+local_uv)/grid);
    const float2 uv1=lerp(input.texture_region.xy,input.texture_region.zw,(cell1+local_uv)/grid);
    const float2 dx=ddx(sprite_uv)*(input.texture_region.zw-input.texture_region.xy)/grid;
    const float2 dy=ddy(sprite_uv)*(input.texture_region.zw-input.texture_region.xy)/grid;
    float4 first_color=ParticleTexture(input.texture_index,uv0,dx,dy);
    float4 next_color=ParticleTexture(input.texture_index,uv1,dx,dy);
    if(EnvironmentPBR.y>.5 && (input.emitter_flags&16u)==0) {
        first_color.rgb=Surface_Linear(first_color.rgb);
        next_color.rgb=Surface_Linear(next_color.rgb);
    }
    // Alpha-weighted frame interpolation avoids dark fringes on fading smoke.
    const float blend=frac(frame);
    float4 texture_color=lerp(first_color,next_color,blend);
    if(grid.x*grid.y>1 && (input.emitter_flags&8u)==0)
        texture_color.rgb=lerp(first_color.rgb*first_color.a,next_color.rgb*next_color.a,blend)/max(texture_color.a,.00001);
    float4 color=input.color*texture_color;
    if(EnvironmentPBR.y>.5 && (input.emitter_flags&16u)==0) {
        color.rgb=Surface_Linear(input.color.rgb)*texture_color.rgb;
        if((input.emitter_flags&(8u|128u))==0) {
            float3 normal=0;
            if(input.normal_texture_index!=0xffffffffu) {
                const float3 normal0=ParticleTexture(input.normal_texture_index,uv0,dx,dy).xyz*2-1;
                const float3 normal1=ParticleTexture(input.normal_texture_index,uv1,dx,dy).xyz*2-1;
                float3 tangent=Surface_Normalize(lerp(normal0,normal1,blend),float3(0,0,1));
                const float c=cos(input.angle),s=sin(input.angle);
                tangent.xy=float2(c*tangent.x-s*tangent.y,s*tangent.x+c*tangent.y);
                tangent.z*=ParticleProjection[3][2]>0 ? -1 : 1;
                normal=normalize(mul(transpose((float3x3)ParticleView),tangent));
            }
            const float3 surface=ParticleWorldPosition(ParticleViewPosition(input.position.xy,input.position.z));
            color.rgb*=ParticleIncident(surface,
                Surface_Normalize(EnvironmentCamera.xyz-surface,float3(0,0,1)),normal);
        }
    }
    if(ParticleDepthOptions.x>.5 && (input.emitter_flags&4u)==0) {
        const float scene_depth=ParticleSceneDepth().Load(int3(int2(input.position.xy),0));
        const float3 scene_position=ParticleViewPosition(input.position.xy,scene_depth);
        const float3 particle_position=ParticleViewPosition(input.position.xy,input.position.z);
        const float fade=max(0,length(scene_position)-length(particle_position))/max(input.radius*.35,.05);
        const float soft=saturate(fade);
        if((input.emitter_flags&8u)!=0) color.rgb*=soft;
        else if((input.emitter_flags&16u)!=0) color.rgb=lerp(1.xxx,color.rgb,soft);
        else color.a*=soft;
    }
    if ((input.emitter_flags & 4u) != 0 && color.a < (96.0 / 255.0))
        discard;
    return color;
}
