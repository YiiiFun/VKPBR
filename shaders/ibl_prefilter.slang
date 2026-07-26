/* Copyright (c) 2025 Holochip Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Prefilters the environment cubemap for specular IBL using GGX
 * importance sampling. Each mip level corresponds to a different
 * roughness value (0 = mirror, maxMip = diffuse).
 */

struct Params {
    float4 texelSize;  // x=cubeSize, y=roughness, z=unused, w=unused
};

[[vk::binding(0, 0)]] SamplerCube envCube;
[[vk::binding(1, 0)]] RWTexture2DArray<float4> prefilterOut;
// Push constant: recorded per-dispatch, immune to host-side UBO overwrite races.
[[vk::push_constant]] Params params;

static const float PI = 3.14159265359;

float3 CubeDir(uint face, float2 uv)
{
    float u = uv.x * 2.0 - 1.0;
    float v = uv.y * 2.0 - 1.0;
    float3 dir = float3(0.0, 0.0, 0.0);
    switch (face)
    {
        case 0: dir = float3( 1.0,    v, -u); break;
        case 1: dir = float3(-1.0,    v,  u); break;
        case 2: dir = float3(   u,  1.0, -v); break;
        case 3: dir = float3(   u, -1.0,  v); break;
        case 4: dir = float3(   u,    v,  1.0); break;
        case 5: dir = float3(  -u,    v, -1.0); break;
    }
    return normalize(dir);
}

// Low-discrepancy sequence (Van der Corput)
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Spherical to cartesian
    float3 H;
    H.x = sinTheta * cos(phi);
    H.y = sinTheta * sin(phi);
    H.z = cosTheta;

    // Tangent to world space
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint cubeSize = (uint)params.texelSize.x;
    if (id.x >= cubeSize || id.y >= cubeSize)
        return;

    uint face = id.z;
    if (face >= 6)
        return;

    float2 uv = (float2(id.xy) + 0.5) / float(cubeSize);
    float3 N = CubeDir(face, uv);
    float3 V = N;

    float roughness = params.texelSize.y;
    float3 prefilteredColor = float3(0.0, 0.0, 0.0);
    float totalWeight = 0.0;

    uint sampleCount = 1024u; // enough for smooth results at 128x128
    [loop]
    for (uint i = 0u; i < sampleCount; ++i)
    {
        float2 Xi = Hammersley(i, sampleCount);
        float3 H = ImportanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
        {
            prefilteredColor += envCube.SampleLevel(L, 0.0).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefilteredColor = prefilteredColor / max(totalWeight, 0.001);
    prefilterOut[int3(id.x, id.y, face)] = float4(prefilteredColor, 1.0);
}
