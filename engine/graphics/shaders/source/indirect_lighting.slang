#include "resource_access.slangh"
cbuffer IndirectParameters : register(b0) {
    row_major float4x4 Projection;
    row_major float4x4 InverseProjection;
    row_major float4x4 View;
    float Radius,OcclusionRadius,Bias,Strength;
    uint Operation;
    float3 Padding;
};
GRAPHICS_TEXTURE_2D(Scene,float4,0)
GRAPHICS_TEXTURE_2D(Depth,float,1)
GRAPHICS_TEXTURE_2D(Diffuse,float4,2)
GRAPHICS_TEXTURE_2D(Normals,float4,3)
GRAPHICS_TEXTURE_2D(Ambient,float4,4)
GRAPHICS_TEXTURE_2D(Bounce,float4,5)
GRAPHICS_SAMPLER(LinearClamp,0)
struct VertexInput {float3 position:POSITION;float4 color:COLOR0;float2 uv:TEXCOORD0;};
struct VertexOutput {float4 position:SV_POSITION;float2 uv:TEXCOORD0;};
[shader("vertex")] VertexOutput indirect_vertex(VertexInput input) {
    VertexOutput output;output.position=float4(input.position,1);output.uv=input.uv;return output;
}
float3 Position(float2 uv,float depth) {
    const float4 clip=mul(InverseProjection,float4(uv*float2(2,-2)+float2(-1,1),depth,1));
    return clip.xyz/clip.w;
}
float3 Normal(int2 pixel) {
    const float3 world=Normals().Load(int3(pixel,0)).xyz;
    return normalize(mul((float3x3)View,world));
}
[shader("pixel")] float4 indirect_pixel(VertexOutput input):SV_TARGET {
    uint width,height;Depth().GetDimensions(width,height);
    const int2 size=int2(width,height);
    const int2 pixel=clamp(int2(input.uv*size),0,size-1);
    const float depth=Depth().Load(int3(pixel,0));
    const float3 encoded=Normals().Load(int3(pixel,0)).xyz;
    const bool valid=depth<1 && dot(encoded,encoded)>.01;
    if(!valid) return Operation==0 ? float4(0,0,0,1) : Scene().Load(int3(pixel,0));
    const float3 position=Position(input.uv,depth);
    const float3 normal=Normal(pixel);
    if(Operation==0) {
        const float3 tangent=normalize(cross(abs(normal.z)>.9 ? float3(0,1,0) : float3(0,0,1),normal));
        const float3 bitangent=cross(normal,tangent);
        float3 incident=0;float obscured=0;
        const uint rays=12,steps=12;
        // Fixed stratification and bilateral reconstruction avoid temporal noise
        // and do not reuse invalid history behind moving objects or camera cuts.
        const uint2 tile=uint2(input.position.xy)&3;
        const float rotation=(tile.x+4*tile.y)*.3926990817;
        [loop] for(uint ray=0;ray<rays;++ray) {
            const float u=(ray+.5)/rays,phi=ray*2.39996323+rotation;
            const float3 direction=tangent*(cos(phi)*sqrt(u))+bitangent*(sin(phi)*sqrt(u))+normal*sqrt(1-u);
            float previous=Bias;
            [loop] for(uint step=1;step<=steps;++step) {
                const float t=Bias+Radius*pow(float(step)/steps,2);
                const float3 probe=position+normal*Bias+direction*t;
                const float4 clip=mul(Projection,float4(probe,1));
                if(clip.w<=0) break;
                const float3 ndc=clip.xyz/clip.w;
                const float2 uv=ndc.xy*float2(.5,-.5)+.5;
                if(any(uv<0)||any(uv>=1)||ndc.z<=0||ndc.z>=1) break;
                const int2 hit=int2(uv*size);
                const float sampled=Depth().Load(int3(hit,0));
                if(sampled<1) {
                    const float3 surface=Position(uv,sampled);
                    const float separation=surface.z-probe.z;
                    if(separation>0 && separation<max(1.f,(t-previous)*.75)) {
                        // Reject back-facing samples and self intersections.
                        const float3 source_normal=Normals().Load(int3(hit,0)).xyz;
                        if(dot(source_normal,source_normal)>.01 && dot(Normal(hit),-direction)>0
                            && distance(surface,position)>Bias*2) {
                            incident+=Scene().Load(int3(hit,0)).rgb;
                            obscured+=saturate(1-t/OcclusionRadius);
                            break;
                        }
                    }
                }
                previous=t;
            }
        }
        // Cosine-weighted hemisphere sampling cancels Lambert's PI factor.
        return float4(incident/rays,saturate(1-obscured/rays));
    }
    uint bounce_width,bounce_height;Bounce().GetDimensions(bounce_width,bounce_height);
    const float2 half_pixel=input.uv*float2(bounce_width,bounce_height)-.5;
    const int2 center=int2(round(half_pixel));
    float4 sum=0;float total=0;
    [loop] for(int y=-1;y<=1;++y) [loop] for(int x=-1;x<=1;++x) {
        const int2 tap=clamp(center+int2(x,y),0,int2(bounce_width,bounce_height)-1);
        const float2 uv=(float2(tap)+.5)/float2(bounce_width,bounce_height);
        const int2 full=clamp(int2(uv*size),0,size-1);
        const float3 tap_normal=Normals().Load(int3(full,0)).xyz;
        if(dot(tap_normal,tap_normal)<.01) continue;
        const float3 tap_position=Position(uv,Depth().Load(int3(full,0)));
        const float plane_distance=abs(dot(tap_position-position,normal));
        const float agreement=saturate(dot(normal,Normal(full)));
        const float2 delta=float2(tap)-half_pixel;
        const float weight=exp(-dot(delta,delta)*.5-plane_distance/max(Bias,.01))*pow(agreement,8);
        sum+=Bounce().Load(int3(tap,0))*weight;total+=weight;
    }
    const float4 bounce=total>1e-5 ? sum/total : float4(0,0,0,1);
    const float4 scene=Scene().Load(int3(pixel,0));
    const float3 ambient=Ambient().Load(int3(pixel,0)).rgb;
    const float3 reflectance=Diffuse().Load(int3(pixel,0)).rgb;
    return float4(max(scene.rgb-ambient*(1-bounce.a)*Strength,0)+reflectance*bounce.rgb*Strength,scene.a);
}
