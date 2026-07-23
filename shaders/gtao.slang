/* Copyright (c) 2025 Holochip Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * GTAO (Ground-Truth Ambient Occlusion) compute shader.
 * Uses screen-space horizon search along multiple directions,
 * sharing the same descriptor layout and UBO as the SSAO pass.
 */

struct SSAOParams {
    column_major float4x4 proj;
    column_major float4x4 invProj;
    float4 screenRadiusBias;   // x=width, y=height, z=radius, w=bias
    float4 intensitySamples;   // x=intensity, y=sampleCount, z=blurEnabled, w=reserved
    float4 gtaoParams;         // x=directions, y=steps, z=thickness, w=falloff
};

[[vk::binding(0, 0)]] Sampler2D depthTex;
[[vk::binding(1, 0)]] RWTexture2D<float> aoOut;
[[vk::binding(2, 0)]] ConstantBuffer<SSAOParams> params;

static const float PI = 3.14159265359;

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
    if (n.z < 0.0) n = -n;
    return all(isfinite(n)) ? n : float3(0.0, 0.0, 1.0);
}

// Compute the horizon angle for a given screen-space direction.
// Returns the tangent of the horizon angle (rise / run in view space).
float2 HorizonSearch(float2 uv, float2 dir, float3 centerVS, float3 normalVS,
                     float radius, uint steps, float thickness, float2 texelSize)
{
    float2 horizon = float2(-1.0, -1.0); // (positive dir, negative dir) as tan(angle)

    // Positive direction
    {
        float stepSize = radius / float(steps);
        float3 prevVS = centerVS;
        [loop]
        for (uint s = 1; s <= steps; ++s)
        {
            float2 sampleUV = uv + dir * stepSize * float(s);
            if (any(sampleUV < 0.0) || any(sampleUV > 1.0))
                break;

            float sampleDepth = depthTex.SampleLevel(sampleUV, 0.0).r;
            if (sampleDepth >= 0.99999)
                continue;

            float3 sampleVS = ReconstructViewPosition(sampleUV, sampleDepth);
            float3 delta = sampleVS - centerVS;
            float dist = length(delta);

            // Thickness test: skip samples that are too far behind the surface
            // (likely foreground geometry, not occluder)
            if (dist > thickness * radius)
                break;

            // Compute the tangent of the horizon angle: vertical offset / horizontal distance
            // Project delta onto the plane defined by normal
            float horizDist = length(delta - normalVS * dot(delta, normalVS));
            if (horizDist < 1e-5)
                continue;

            float tanAngle = dot(delta, normalVS) / horizDist;
            horizon.x = max(horizon.x, tanAngle);

            prevVS = sampleVS;
        }
    }

    // Negative direction
    {
        float stepSize = radius / float(steps);
        [loop]
        for (uint s = 1; s <= steps; ++s)
        {
            float2 sampleUV = uv - dir * stepSize * float(s);
            if (any(sampleUV < 0.0) || any(sampleUV > 1.0))
                break;

            float sampleDepth = depthTex.SampleLevel(sampleUV, 0.0).r;
            if (sampleDepth >= 0.99999)
                continue;

            float3 sampleVS = ReconstructViewPosition(sampleUV, sampleDepth);
            float3 delta = sampleVS - centerVS;
            float dist = length(delta);

            if (dist > thickness * radius)
                break;

            float horizDist = length(delta - normalVS * dot(delta, normalVS));
            if (horizDist < 1e-5)
                continue;

            float tanAngle = dot(delta, normalVS) / horizDist;
            horizon.y = max(horizon.y, tanAngle);
        }
    }

    return horizon;
}

// Integrate the occlusion from two horizon angles (as tangents).
// Uses the closed-form integration of cos(theta) over the visible arc.
float IntegrateArc(float2 horizon, float3 normalVS, float3 dirVS)
{
    // Convert tangent to angle: angle = atan(tan)
    float h1 = atan(horizon.x);
    float h2 = atan(horizon.y);

    // The actual horizon angles clipped against the surface tangent plane
    // The hemisphere is centered around the normal; the integration plane is
    // defined by (normal, direction)
    float3 tangentVS = normalize(cross(cross(normalVS, dirVS), normalVS));

    // Clip horizons to the tangent plane (half-arc starting from the normal)
    float n1 = -PI / 2.0; // start from tangent plane
    float n2 = PI / 2.0;

    // Clip to the hemisphere above the surface
    h1 = min(h1, n2);
    h2 = min(h2, n2);
    h1 = max(h1, n1);
    h2 = max(h2, n1);

    // Integration of cos(theta) from h2 to h1
    // Result = (sin(h1) - sin(h2)) / 2 ... but we need the actual formula
    // The GTAO integration is:
    //   occlusion += (cos(clamp(h1, ...)) + cos(clamp(h2, ...))) * 0.25
    // Using the simplified version from the SCAO/GTAO approach:
    float cos_h1 = cos(h1);
    float cos_h2 = cos(h2);

    // Weight by the direction's alignment with the tangent plane
    return (1.0 - cos_h1 * cos_h1 * 0.25 - cos_h2 * cos_h2 * 0.25);
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

    float radius = max(params.screenRadiusBias.z, 0.001);
    float bias = max(params.screenRadiusBias.w, 0.0);
    float intensity = saturate(params.intensitySamples.x);

    uint directions = clamp((uint)params.gtaoParams.x, 1u, 16u);
    uint steps = clamp((uint)params.gtaoParams.y, 1u, 32u);
    float thickness = max(params.gtaoParams.z, 0.01);
    float falloff = saturate(params.gtaoParams.w);

    // Project radius to screen space: convert view-space radius to pixel radius
    // using the projection at the center depth
    float4 centerClip = mul(params.proj, float4(centerVS, 1.0));
    float centerNdcZ = centerClip.z / max(abs(centerClip.w), 1e-6);
    float fovScale = abs(params.proj[1][1]); // vertical fov factor
    float viewDist = abs(centerVS.z);
    float screenRadiusPx = max(radius * fovScale / max(viewDist, 0.01) * screenSize.y, 2.0);

    float occlusion = 0.0;

    [loop]
    for (uint d = 0; d < directions; ++d)
    {
        float angle = (PI * float(d) + PI * 0.5) / float(directions);

        // Screen-space direction for this slice
        float2 screenDir = float2(cos(angle), sin(angle)) * screenRadiusPx * texelSize;

        float2 horizon = HorizonSearch(uv, screenDir, centerVS, normalVS,
                                        radius, steps, thickness, texelSize);

        // Approximate the direction vector in view space for weighting
        // Use the screen-space direction projected back
        float3 dirVS = normalize(float3(screenDir.x, -screenDir.y, 0.0));

        // Simple integration: combine the two horizon tangents
        float h1 = atan(max(horizon.x + bias, 0.0));
        float h2 = atan(max(horizon.y + bias, 0.0));

        // The occlusion from this slice: integrate cos over the occluded arc
        // Using the simplified formula: occlusion_slice = 1 - cos(h1) - cos(h2) (normalized)
        float sliceOcc = 1.0 - (cos(h1) + cos(h2)) * 0.5;
        sliceOcc = saturate(sliceOcc);

        // Falloff: reduce occlusion from samples far from the center
        // (already handled by thickness, but add explicit falloff weight)
        sliceOcc *= lerp(1.0, 0.3, falloff);

        occlusion += sliceOcc;
    }

    occlusion /= float(directions);

    float rawAO = saturate(1.0 - occlusion * intensity);
    aoOut[id.xy] = rawAO;
}
