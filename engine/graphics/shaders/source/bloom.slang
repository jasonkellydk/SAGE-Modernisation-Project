// Based on Electronic Arts' Red Alert 3 PostFX_Bloom.fx (2008):
// https://github.com/electronicarts/CnC_Modding_Support/blob/main/Red%20Alert%203/Shaders/PostFX_Bloom.fx
// Uses its squared extraction curve and five bilinear Gaussian taps.
// The input here is the game's uncompressed display color; RA3's render-target
// decompression and D3D9 half-texel correction do not apply to this pipeline.
struct VertexInput
{
    float3 position : POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};
struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};
cbuffer BloomParameters : register(b0)
{
    float2 TexelSize;
    uint Operation;
    float Intensity;
};
Texture2D<float4> Source : register(t0);
Texture2D<float4> Scene : register(t1);
SamplerState LinearClamp : register(s0);

[shader("vertex")] VertexOutput bloom_vertex(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 1);
    output.uv = input.uv;
    return output;
}

[shader("pixel")] float4 bloom_pixel(VertexOutput input) : SV_TARGET
{
    if (Operation == 0)
    {
        // Four bilinear samples cover the 4x4 source footprint.
        float3 color = (Source.Sample(LinearClamp, input.uv + TexelSize * float2(-1,-1)).rgb
            + Source.Sample(LinearClamp, input.uv + TexelSize * float2(1,-1)).rgb
            + Source.Sample(LinearClamp, input.uv + TexelSize * float2(-1,1)).rgb
            + Source.Sample(LinearClamp, input.uv + TexelSize * float2(1,1)).rgb) * 0.25;
        float brightness = dot(color, float3(0.3,0.3,0.3));
        float excess = max(brightness - 0.11, 0.0);
        return float4(color * (excess * excess / (brightness + 0.0001)), 0);
    }
    if (Operation == 3)
    {
        float4 scene = Scene.Sample(LinearClamp, input.uv);
        return float4(scene.rgb + Source.Sample(LinearClamp, input.uv).rgb * Intensity, scene.a);
    }
    float2 direction = Operation == 1 ? float2(TexelSize.x,0) : float2(0,TexelSize.y);
    float3 color = Source.Sample(LinearClamp, input.uv).rgb * 252.0;
    color += (Source.Sample(LinearClamp, input.uv - direction * 1.65).rgb
        + Source.Sample(LinearClamp, input.uv + direction * 1.65).rgb) * 120.0;
    color += (Source.Sample(LinearClamp, input.uv - direction * 3.55).rgb
        + Source.Sample(LinearClamp, input.uv + direction * 3.55).rgb) * 10.0;
    return float4(color / 512.0, 0);
}
