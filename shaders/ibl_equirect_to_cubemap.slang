/* Copyright (c) 2025 Holochip Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Converts an equirectangular HDR environment map to a 6-face cubemap.
 * Each thread writes one texel of one cube face (selected by layer index).
 */

struct Params {
    float4 texelSize;  // x=cubeSize, y=1/cubeSize, z=unused, w=unused
};

[[vk::binding(0, 0)]] Sampler2D equirectMap;
[[vk::binding(1, 0)]] RWTexture2DArray<float4> cubeOut;
// Push constant: recorded per-dispatch, immune to host-side UBO overwrite races.
[[vk::push_constant]] Params params;

static const float PI = 3.14159265359;

// Convert UV (face, u, v) to a cubemap direction.
// Face ordering: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
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

// Convert a direction to equirectangular UV
float2 DirToEquirect(float3 dir)
{
    float u = atan2(dir.z, dir.x) / (2.0 * PI) + 0.5;
    float v = asin(clamp(dir.y, -1.0, 1.0)) / PI + 0.5;
    return float2(u, v);
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
    float3 dir = CubeDir(face, uv);
    float2 equiUV = DirToEquirect(dir);

    float4 color = equirectMap.SampleLevel(equiUV, 0.0);
    cubeOut[int3(id.x, id.y, face)] = color;
}
