/* Copyright (c) 2025 Holochip Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

struct SSAOParams {
    column_major float4x4 proj;
    column_major float4x4 invProj;
    float4 screenRadiusBias;   // x=width, y=height, z=radius, w=bias
    float4 intensitySamples;   // x=intensity, y=sampleCount, z=reserved, w=reserved
    float4 gtaoParams;         // x=directions, y=steps, z=thickness, w=falloff
};

[[vk::binding(0, 0)]] Sampler2D depthTex;
[[vk::binding(1, 0)]] RWTexture2D<float> aoOut;
[[vk::binding(2, 0)]] ConstantBuffer<SSAOParams> params;

static const float PI = 3.14159265359;

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float3 ReconstructViewPosition(float2 uv, float depth)
{
    float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 view = mul(params.invProj, clip);
    return view.xyz / max(abs(view.w), 1e-6);
}

float3 ReconstructNormal(float2 uv, float2 texelSize, float3 centerVS)
{
    float depthX = depthTex.SampleLevel(uv + float2(texelSize.x, 0.0), 0.0).r;
    float depthY = depthTex.SampleLevel(uv + float2(0.0, texelSize.y), 0.0).r;
    float3 px = ReconstructViewPosition(uv + float2(texelSize.x, 0.0), depthX);
    float3 py = ReconstructViewPosition(uv + float2(0.0, texelSize.y), depthY);
    float3 n = normalize(cross(py - centerVS, px - centerVS));
    return all(isfinite(n)) ? n : float3(0.0, 0.0, 1.0);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width = (uint)params.screenRadiusBias.x;
    uint height = (uint)params.screenRadiusBias.y;
    if (id.x >= width || id.y >= height) {
        return;
    }

    float2 screenSize = params.screenRadiusBias.xy;
    float2 texelSize = rcp(screenSize);
    float2 uv = (float2(id.xy) + 0.5) * texelSize;

    float depth = depthTex.SampleLevel(uv, 0.0).r;
    if (depth >= 0.99999) {
        aoOut[id.xy] = 1.0;
        return;
    }

    float3 centerVS = ReconstructViewPosition(uv, depth);
    float3 normalVS = ReconstructNormal(uv, texelSize, centerVS);
    if (normalVS.z < 0.0) {
        normalVS = -normalVS;
    }

    float radius = max(params.screenRadiusBias.z, 0.001);
    float bias = max(params.screenRadiusBias.w, 0.0);
    float intensity = saturate(params.intensitySamples.x);
    uint sampleCount = clamp((uint)params.intensitySamples.y, 1u, 64u);

    float rand = Hash12(float2(id.xy));
    float3 up = abs(normalVS.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
    float3 tangent = normalize(cross(up, normalVS));
    float3 bitangent = cross(normalVS, tangent);

    float occlusion = 0.0;
    [loop]
    for (uint i = 0; i < sampleCount; ++i) {
        float fi = (float(i) + 0.5) / float(sampleCount);
        float angle = 2.0 * PI * frac(fi * 7.0 + rand);
        float r = sqrt(fi);
        float z = sqrt(saturate(1.0 - r * r));
        float3 hemi = normalize(tangent * (cos(angle) * r) + bitangent * (sin(angle) * r) + normalVS * z);

        float scale = lerp(0.15, 1.0, fi * fi);
        float3 sampleVS = centerVS + hemi * radius * scale;

        float4 sampleClip = mul(params.proj, float4(sampleVS, 1.0));
        float2 sampleUV = (sampleClip.xy / max(abs(sampleClip.w), 1e-6)) * 0.5 + 0.5;
        if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) {
            continue;
        }

        float sampleDepth = depthTex.SampleLevel(sampleUV, 0.0).r;
        if (sampleDepth >= 0.99999) {
            continue;
        }

        float3 surfaceVS = ReconstructViewPosition(sampleUV, sampleDepth);
        float3 surfaceDelta = surfaceVS - centerVS;
        float dist = length(surfaceVS - centerVS);
        float rangeWeight = 1.0 - smoothstep(radius * 0.35, radius, dist);
        float facingWeight = dist > 1e-5 ? saturate(dot(surfaceDelta / dist, normalVS) * 1.5) : 0.0;
        occlusion += ((surfaceVS.z >= sampleVS.z + bias) ? 1.0 : 0.0) * rangeWeight * facingWeight;
    }

    float rawAO = saturate(1.0 - occlusion / float(sampleCount));
    float ao = lerp(1.0, rawAO, intensity);
    aoOut[id.xy] = ao;
}
