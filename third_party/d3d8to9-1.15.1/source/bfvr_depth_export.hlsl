// BFVR-owned D3D9 depth export shaders.
//
// These are compiled to ps_3_0 bytecode and embedded in
// bfvr_depth_export_shaders.hpp. The standalone AO depth probe validates the
// bytecode against the exact adapter before the live stereo path may use it.

sampler2D DepthSampler : register(s0);

float SampleDepth(float2 uv)
{
    return tex2D(DepthSampler, uv).r;
}

float4 ExportDepthFloat(float2 uv : TEXCOORD0) : COLOR0
{
    const float depth = SampleDepth(uv);
    return float4(depth, depth, depth, 1.0f);
}

float4 ExportDepthPacked(float2 uv : TEXCOORD0) : COLOR0
{
    // The common base-255 RGB encoding keeps the representable interval below
    // one so a cleared depth of 1.0 does not wrap back to zero.
    const float depth = min(SampleDepth(uv), 0.99999994f);
    float3 encoded = frac(depth * float3(1.0f, 255.0f, 65025.0f));
    encoded -= encoded.yzz * float3(1.0f / 255.0f, 1.0f / 255.0f, 0.0f);
    return float4(encoded, 1.0f);
}
