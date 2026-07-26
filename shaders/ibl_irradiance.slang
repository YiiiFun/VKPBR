/* Copyright (c) 2025 Holochip Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Convolves the environment cubemap to produce a diffuse irradiance map.
 * Each texel integrates incoming radiance over the hemisphere centered
 * on the texel's direction, weighted by cosine (Lambert).
 */

struct Params {
    float4 texelSize;  // x=cubeSize, y=1/cubeSize, z=unused, w=unused
};

[[vk::binding(0, 0)]] SamplerCube envCube;
[[vk::binding(1, 0)]] RWTexture2DArray<float4> irradianceOut;
// Push constant: recorded per-dispatch, immune to host-side UBO overwrite races.
[[vk::push_constant]] Params params;

static const float PI = 3.14159265359;

// Convert UV (face, u, v) to a cubemap direction.
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
    float3 normal = CubeDir(face, uv);

    // Build a tangent frame for the hemisphere integration
    float3 up = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);

    float3 irradiance = float3(0.0, 0.0, 0.0);
    float sampleCount = 0.0;

    // Low-resolution hemisphere integration (64x64 = 4096 samples)
    // Using a uniform phi/theta grid; for a 32x32 irradiance map this is sufficient.
    uint sampleDelta = 32;
    float step = 1.0 / float(sampleDelta);

    [loop]
    for (uint phi_i = 0; phi_i < sampleDelta; ++phi_i)
    {
        float phi = float(phi_i) * step * 2.0 * PI;
        [loop]
        for (uint theta_i = 0; theta_i < sampleDelta; ++theta_i)
        {
            float theta = (float(theta_i) + 0.5) * step * 0.5 * PI;
            // Spherical to cartesian (hemisphere around normal)
            float sinTheta = sin(theta);
            float cosTheta = cos(theta);
            float3 sampleDir = tangent * (sinTheta * cos(phi))
                             + bitangent * (sinTheta * sin(phi))
                             + normal * cosTheta;

            irradiance += envCube.SampleLevel(sampleDir, 0.0).rgb * cosTheta * sinTheta;
            sampleCount += 1.0;
        }
    }

    irradiance = irradiance * (PI / sampleCount);
    irradianceOut[int3(id.x, id.y, face)] = float4(irradiance, 1.0);
}
