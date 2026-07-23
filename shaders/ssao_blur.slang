/* Copyright (c) 2025 Holochip Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Depth-aware bilateral blur for AO.
 * Uses depth difference as an edge-stopping weight to prevent
 * bleeding across geometry boundaries (walls, edges, etc).
 */

struct SSAOBlurParams {
    column_major float4x4 proj;
    column_major float4x4 invProj;
    float4 screenRadiusBias;   // x=width, y=height, z=radius, w=bias
    float4 intensitySamples;   // z=blurEnabled
    float4 gtaoParams;         // x=directions, y=steps, z=thickness, w=falloff
};

[[vk::binding(0, 0)]] Sampler2D aoIn;
[[vk::binding(1, 0)]] RWTexture2D<float> aoOut;
[[vk::binding(2, 0)]] ConstantBuffer<SSAOBlurParams> params;
[[vk::binding(3, 0)]] Sampler2D depthTex;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width = (uint)params.screenRadiusBias.x;
    uint height = (uint)params.screenRadiusBias.y;
    if (id.x >= width || id.y >= height) {
        return;
    }

    float2 texelSize = rcp(params.screenRadiusBias.xy);
    float2 uv = (float2(id.xy) + 0.5) * texelSize;

    if (params.intensitySamples.z < 0.5) {
        aoOut[id.xy] = aoIn.SampleLevel(uv, 0.0).r;
        return;
    }

    float centerDepth = depthTex.SampleLevel(uv, 0.0).r;

    float sum = 0.0;
    float weightSum = 0.0;
    [unroll]
    for (int y = -2; y <= 2; ++y) {
        [unroll]
        for (int x = -2; x <= 2; ++x) {
            float2 duv = float2((float)x, (float)y) * texelSize;
            float sampleDepth = depthTex.SampleLevel(uv + duv, 0.0).r;

            // Spatial weight: closer taps weigh more
            float spatialW = 1.0 / (1.0 + abs((float)x) + abs((float)y));

            // Depth edge-stopping weight: don't blur across depth discontinuities
            float depthDiff = abs(sampleDepth - centerDepth);
            float depthW = exp(-depthDiff * depthDiff * 1000.0);

            float w = spatialW * depthW;
            sum += aoIn.SampleLevel(uv + duv, 0.0).r * w;
            weightSum += w;
        }
    }

    aoOut[id.xy] = saturate(sum / max(weightSum, 1e-6));
}
