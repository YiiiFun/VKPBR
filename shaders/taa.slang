/* Copyright (c) 2025 Holochip Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

struct TAAParams {
    column_major float4x4 invCurrentViewProj;
    column_major float4x4 previousViewProj;
    float4 screenHistoryDepth; // width, height, history weight, depth threshold
    float4 jitterPixels;        // current jitter xy, previous jitter xy
    float4 tuning;              // sharpness, history valid, reserved, reserved
    int4 control;              // enabled, debug view, reserved, reserved
};

[[vk::binding(0, 0)]] Sampler2D currentColorTex;
[[vk::binding(1, 0)]] Sampler2D currentDepthTex;
[[vk::binding(2, 0)]] Sampler2D historyColorTex;
[[vk::binding(3, 0)]] Sampler2D historyDepthTex;
[[vk::binding(4, 0)]] RWTexture2D<float4> resolvedColorOut;
[[vk::binding(5, 0)]] RWTexture2D<float> resolvedDepthOut;
[[vk::binding(6, 0)]] ConstantBuffer<TAAParams> params;

float3 RGBToYCoCg(float3 rgb)
{
    return float3(
        dot(rgb, float3(0.25, 0.5, 0.25)),
        dot(rgb, float3(0.5, 0.0, -0.5)),
        dot(rgb, float3(-0.25, 0.5, -0.25)));
}

float3 YCoCgToRGB(float3 ycocg)
{
    float y = ycocg.x;
    float co = ycocg.y;
    float cg = ycocg.z;
    return float3(y + co - cg, y + cg, y - co - cg);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint2 size = uint2(params.screenHistoryDepth.xy);
    if (any(id.xy >= size)) {
        return;
    }

    float2 texel = rcp(params.screenHistoryDepth.xy);
    float2 uv = (float2(id.xy) + 0.5) * texel;
    // Projection jitter moves rasterized features by -jitterPixels. Sample the
    // jittered inputs at that shifted location so the resolved output remains
    // on a stable, unjittered pixel grid.
    float2 currentUV = clamp(
        uv - params.jitterPixels.xy * texel,
        texel * 0.5,
        1.0 - texel * 0.5);
    float4 current = currentColorTex.SampleLevel(currentUV, 0.0);
    float depth = currentDepthTex.SampleLevel(currentUV, 0.0).r;
    resolvedDepthOut[id.xy] = depth;

    if (params.control.x == 0 || params.tuning.y < 0.5 || depth >= 0.99999) {
        resolvedColorOut[id.xy] = current;
        return;
    }

    float4 currentClip = float4(currentUV * 2.0 - 1.0, depth, 1.0);
    float4 world = mul(params.invCurrentViewProj, currentClip);
    world /= max(abs(world.w), 1e-6);
    float4 previousClip = mul(params.previousViewProj, world);
    float2 previousNdc = previousClip.xy / max(abs(previousClip.w), 1e-6);
    // History is stored on the stable output grid. Remove the previous
    // projection jitter before sampling it.
    float2 previousUV = previousNdc * 0.5 + 0.5 + params.jitterPixels.zw * texel;
    float2 motion = uv - previousUV;

    bool inside = previousClip.w > 0.0 && all(previousUV >= 0.0) && all(previousUV <= 1.0);
    float expectedPreviousDepth = previousClip.z / max(abs(previousClip.w), 1e-6);
    float historyDepth = inside ? historyDepthTex.SampleLevel(previousUV, 0.0).r : 1.0;
    float depthTolerance = params.screenHistoryDepth.w * max(1.0, abs(expectedPreviousDepth));
    bool historyAccepted = inside && abs(historyDepth - expectedPreviousDepth) <= depthTolerance;

    float3 neighborhoodMin = float3(1e20);
    float3 neighborhoodMax = float3(-1e20);
    float3 neighborhoodSum = float3(0.0);
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            float2 sampleUV = clamp(currentUV + float2(x, y) * texel, texel * 0.5, 1.0 - texel * 0.5);
            float3 sampleColor = currentColorTex.SampleLevel(sampleUV, 0.0).rgb;
            float3 sampleYCoCg = RGBToYCoCg(sampleColor);
            neighborhoodMin = min(neighborhoodMin, sampleYCoCg);
            neighborhoodMax = max(neighborhoodMax, sampleYCoCg);
            neighborhoodSum += sampleColor;
        }
    }

    float3 history = historyColorTex.SampleLevel(previousUV, 0.0).rgb;
    history = YCoCgToRGB(clamp(RGBToYCoCg(history), neighborhoodMin, neighborhoodMax));

    float motionPixels = length(motion * params.screenHistoryDepth.xy);
    float motionConfidence = saturate(1.0 - motionPixels * 0.08);
    float historyWeight = historyAccepted ? params.screenHistoryDepth.z * motionConfidence : 0.0;
    float3 resolved = lerp(current.rgb, history, historyWeight);

    float3 neighborhoodAverage = neighborhoodSum / 9.0;
    resolved = max(resolved + (current.rgb - neighborhoodAverage) * params.tuning.x, 0.0);

    if (params.control.y == 1) {
        resolved = float3(0.5 + motion.x * 32.0, 0.5 + motion.y * 32.0, 0.0);
    } else if (params.control.y == 2) {
        resolved = historyWeight.xxx;
    } else if (params.control.y == 3) {
        resolved = historyAccepted ? float3(0.05, 0.8, 0.1) : float3(0.9, 0.05, 0.05);
    }

    resolvedColorOut[id.xy] = float4(resolved, current.a);
}
