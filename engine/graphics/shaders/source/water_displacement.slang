cbuffer WaterConstants : register(b0)
{
    row_major float4x4 World;
    row_major float4x4 View;
    row_major float4x4 Projection;
    float4 WaterShroudProjection;
    float4 WaterAnimation;
    float4 WaterCameraPosition;
    float4 WaterDisplacementDomain;
    float4 WaterTint;
    float4 WaterEffects;
    float4 WaterSurfaceOptions;
    float4 FogColor;
    float4 WaterFogState;
};
// RA3-style water displacement producer.
//
// Ocean.fx consumes a generated displacement field.  The original producer
// renders wave particles, blurs that field, and adds the animated static
// displacement map.  This programmable pass owns the same producer boundary:
// it writes the encoded field into a render target before Ocean.fx consumes
// it.  The broad wave field is generated in the producer so the ocean vertex
// shader never treats the authored static DDS as a dynamic displacement map.

struct WaterDisplacementVertexInput
{
    float3 position : POSITION0;
    float2 texcoord0 : TEXCOORD0;
};

struct WaterDisplacementVertexOutput
{
    float4 position : SV_POSITION;
    float2 texcoord0 : TEXCOORD0;
};

[shader("vertex")] WaterDisplacementVertexOutput WaterDisplacementVertexMain(
    WaterDisplacementVertexInput input)
{
    WaterDisplacementVertexOutput output;
    output.position = float4(input.position,1);
    output.texcoord0 = input.texcoord0;
    return output;
}

Texture2D WaterStaticDisplacementTexture : register(t0);
SamplerState WaterStaticDisplacementSampler : register(s0);



float2 Rotate_Static_Displacement(float2 uv)
{
    const float cosine = 0.2588190;
    const float sine = 0.9659258;
    return float2(
        dot(uv, float2(cosine, -sine)),
        dot(uv, float2(sine, cosine)));
}

float2 Rotate_Static_Displacement_Inverse(float2 uv)
{
    const float cosine = 0.2588190;
    const float sine = 0.9659258;
    return float2(
        dot(uv, float2(cosine, sine)),
        dot(uv, float2(-sine, cosine)));
}

[shader("pixel")] float4 WaterDisplacementPixelMain(
    WaterDisplacementVertexOutput input) : SV_TARGET
{
    const float time = WaterAnimation.z;
    const float2 domain_origin = WaterDisplacementDomain.xy;
    const float2 domain_size = max(abs(WaterDisplacementDomain.zw),
        float2(1.0, 1.0));

    // The producer follows OceanDisplacement.fx: its render target has an
    // inverted Y mapping because the ocean vertex path samples (x, 1-y).
    const float2 world_position = domain_origin +
        float2(input.texcoord0.x, 1.0 - input.texcoord0.y) * domain_size;

    // These are the three waves declared by RA3 Ocean.fx.  WaveAmplitude and
    // WaveFrequency use the shipped high-quality defaults from that shader.
    const float wave_amplitude = 3.0;
    const float wave_frequency = 0.2;
    const float phase0 = dot(world_position, float2(0.2, -0.7)) *
        0.1 * wave_frequency + time * 1.0;
    const float phase1 = dot(world_position, float2(-1.0, -0.7)) *
        0.25 * wave_frequency + time * 0.5;
    const float phase2 = dot(world_position, float2(-1.0, 0.2)) *
        0.15 * wave_frequency + time * 1.5;
    const float amplitude0 = 1.0 * wave_amplitude;
    const float amplitude1 = 0.5 * wave_amplitude;
    const float amplitude2 = 0.5 * wave_amplitude;
    const float wave0 = amplitude0 * sin(phase0);
    const float wave1 = amplitude1 * sin(phase1);
    const float wave2 = amplitude2 * sin(phase2);
    const float wave_height = max(wave0, 0.0) + max(wave1, 0.0) +
        max(wave2, 0.0);

    // The displacement producer stores XY lateral displacement and Z height
    // in the same normalized field consumed by Ocean.fx (* 300 in the
    // ocean vertex stage). The lateral component is kept within the producer
    // range used by the RA3 high path to prevent inverted wave cells.
    const float2 wave_gradient =
        amplitude0 * 0.1 * wave_frequency * cos(phase0) *
            float2(0.2, -0.7) +
        amplitude1 * 0.25 * wave_frequency * cos(phase1) *
            float2(-1.0, -0.7) +
        amplitude2 * 0.15 * wave_frequency * cos(phase2) *
            float2(-1.0, 0.2);
    float2 lateral_displacement = clamp(-wave_gradient * 0.002,
        float2(-0.13, -0.13), float2(0.13, 0.13));

    // StaticDisplacement in OceanDisplacement.fx samples the authored wave
    // map's alpha channel twice using the 75-degree divergent octaves and
    // adds the result into every displacement component.
    const float2 static_uv = world_position * 0.0025;
    const float2 static_uv0 = Rotate_Static_Displacement(static_uv) +
        float2(time * 0.0225, 0.0);
    const float2 static_uv1 = Rotate_Static_Displacement_Inverse(static_uv) +
        float2(time * 0.0225, 0.0);
    const float static_sample0 = WaterStaticDisplacementTexture.Sample(
        WaterStaticDisplacementSampler, static_uv0).a * 2.0 - 1.0;
    const float static_sample1 = WaterStaticDisplacementTexture.Sample(
        WaterStaticDisplacementSampler, static_uv1).a * 2.0 - 1.0;
    const float static_displacement = (static_sample0 + static_sample1) *
        0.06;

    return float4(lateral_displacement.x + static_displacement,
        lateral_displacement.y + static_displacement,
        wave_height / 300.0 + static_displacement, 0.0);
}
