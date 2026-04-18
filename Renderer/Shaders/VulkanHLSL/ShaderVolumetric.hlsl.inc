R"HLSL(

struct VSInput { float2 position : POSITION; float2 texcoord : TEXCOORD; };
struct PSInput { float4 position : SV_POSITION; float2 texcoord : TEXCOORD; };

Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer VolumetricConstants : register(b0)
{
    row_major float4x4 invViewProjection;
    float4 camPos;
    float4 clouds[32];       // xyz = position, w = radius
    float4 cloudColors[32];  // rgb = color, a = density
    float4 gfClouds[8];      // ground fog: xyz = position, w = radius
    float4 gfColors[8];      // ground fog: rgb = color, a = density
    float4 sunDirection;
    float4 sunColorV;
    float2 texelSize;
    float time;
    float numClouds;
    float numGroundFog;
    float3 padV;
};

PSInput VSPost(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.texcoord = input.texcoord;
    return output;
}

// 3D value noise
float hash3(float3 p)
{
    p = frac(p * float3(443.8975, 397.2973, 491.1871));
    p += dot(p, p.yxz + 19.19);
    return frac((p.x + p.y) * p.z);
}

float noise3D(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);

    return lerp(lerp(lerp(hash3(i + float3(0,0,0)), hash3(i + float3(1,0,0)), f.x),
                     lerp(hash3(i + float3(0,1,0)), hash3(i + float3(1,1,0)), f.x), f.y),
                lerp(lerp(hash3(i + float3(0,0,1)), hash3(i + float3(1,0,1)), f.x),
                     lerp(hash3(i + float3(0,1,1)), hash3(i + float3(1,1,1)), f.x), f.y), f.z);
}

// Fractal Brownian Motion — 3 octaves for cloud shape
float fbm3(float3 p)
{
    float v = noise3D(p) * 0.5;
    v += noise3D(p * 2.1 + 1.7) * 0.25;
    v += noise3D(p * 4.3 + 3.1) * 0.125;
    return v;
}

float4 PSVolumetric(PSInput input) : SV_TARGET
{
    float3 scene = sceneTexture.Sample(linearSampler, input.texcoord).rgb;

    int count = (int)numClouds;
    if (count <= 0 && numGroundFog < 0.5) return float4(scene, 1.0);

    // Reconstruct world-space ray from screen UV
    float2 ndc = input.texcoord * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 worldFar = mul(float4(ndc, 0.5, 1.0), invViewProjection);
    worldFar.xyz /= worldFar.w;
    float3 rayDir = normalize(worldFar.xyz - camPos.xyz);
    float3 rayOrigin = camPos.xyz;

    float3 totalLight = float3(0, 0, 0);
    float totalAlpha = 0.0;

    int cloudCount = (int)numClouds;
    for (int e = 0; e < cloudCount && e < 32; e++)
    {
        float3 center = clouds[e].xyz;
        float radius = clouds[e].w;
        float density = cloudColors[e].a;
        if (radius < 1.0 || density < 0.01) continue;

        // Ray-sphere intersection
        // Cloud expands aggressively as it dissipates — linear spread, not quadratic
        float fade = 1.0 - density;
        float spreadFactor = 1.0 + fade * 6.0; // linear: 7x radius when fully faded
        float spreadRadius = radius * spreadFactor;
        float spreadDensity = density * density; // quadratic thin — visible longer while spreading

        float3 oc = rayOrigin - center;
        float b = dot(oc, rayDir);
        float c = dot(oc, oc) - spreadRadius * spreadRadius;
        float disc = b * b - c;
        if (disc < 0.0) continue;

        float sqrtDisc = sqrt(disc);
        float tNear = max(0.0, -b - sqrtDisc);
        float tFar = -b + sqrtDisc;
        if (tFar < 0.0) continue;

        float stepSize = (tFar - tNear) / 14.0;
        float accumulated = 0.0;
        float3 lightAccum = float3(0, 0, 0);

        float3 L = normalize(-sunDirection.xyz);
        float3 expColor = cloudColors[e].rgb;

        for (int s = 0; s < 6; s++)
        {
            float t = tNear + (float(s) + 0.5) * stepSize;
            float3 pos = rayOrigin + rayDir * t;
            float3 local = (pos - center) / spreadRadius;

            // Sphere falloff: dense in center, fading at edge
            float distSq = dot(local, local);
            float sphereFade = saturate(1.0 - distSq);
            sphereFade *= sphereFade;

            // Animated 3D FBM noise — billowing cloud shape (slow animation = lingering smoke)
            // Noise slows down as cloud dissipates (density-scaled animation speed)
            float animSpeed = 0.15 + density * 0.3;
            float3 noisePos = pos * (0.1 / max(radius, 1.0)) * 8.0;
            noisePos += float3(time * animSpeed, time * animSpeed * 0.6, -time * animSpeed * 0.4);
            float n = noise3D(noisePos);

            // Cloud density — uses spreadDensity so cloud thins as it expands
            float d = n * sphereFade * spreadDensity;
            d = max(0.0, d - 0.06);

            if (d < 0.001) continue;

            // Lighting: sun + self-illumination + ambient floor (never fully black)
            float NdotL = dot(normalize(local), L) * 0.4 + 0.6;
            float selfGlow = sphereFade * spreadDensity * 0.35;
            float3 ambient = float3(0.15, 0.13, 0.12); // warm gray minimum light
            float3 lit = ambient + sunColorV.rgb * NdotL * 0.3 + expColor * (0.4 + selfGlow);

            // Accumulate with front-to-back compositing
            float alpha = d * stepSize / spreadRadius * 6.0;
            lightAccum += lit * alpha * (1.0 - accumulated);
            accumulated += alpha * (1.0 - accumulated);

            if (accumulated > 0.95) break;
        }

        totalLight += lightAccum;
        totalAlpha = saturate(totalAlpha + accumulated);
    }

    // --- Ground AOE fog (toxin, radiation, napalm) ---
    // Pancake-shaped volumes: wide horizontally, thin vertically (hugs ground)
    int gfCount = (int)numGroundFog;
    for (int g = 0; g < gfCount && g < 4; g++)
    {
        float3 center = gfClouds[g].xyz;
        float radius = gfClouds[g].w;
        float gfDensity = gfColors[g].a;
        if (radius < 1.0 || gfDensity < 0.01) continue;

        // Squash vertical: treat as a disc (radius wide, height = radius * 0.2)
        float height = radius * 0.2;
        float3 toCenter = rayOrigin - center;

        // Simple vertical slab test + horizontal disc test
        float tEnterZ = (center.z - height - rayOrigin.z) / (rayDir.z + 0.0001);
        float tExitZ = (center.z + height - rayOrigin.z) / (rayDir.z + 0.0001);
        if (tEnterZ > tExitZ) { float tmp = tEnterZ; tEnterZ = tExitZ; tExitZ = tmp; }
        float tNear = max(0.0, tEnterZ);
        float tFar = tExitZ;
        if (tFar < 0.0) continue;

        // 8 steps through the slab
        float stepSize = (tFar - tNear) / 8.0;
        float accumulated = 0.0;
        float3 lightAccum = float3(0, 0, 0);
        float3 gfColor = gfColors[g].rgb;
        float3 L = normalize(-sunDirection.xyz);

        for (int s = 0; s < 8; s++)
        {
            float t = tNear + (float(s) + 0.5) * stepSize;
            float3 pos = rayOrigin + rayDir * t;

            // Horizontal distance from center
            float2 hDist = pos.xy - center.xy;
            float hDistSq = dot(hDist, hDist) / (radius * radius);
            if (hDistSq > 1.0) continue;
            float hFade = saturate(1.0 - hDistSq);
            hFade *= hFade;

            // Vertical fade (densest at ground level)
            float vDist = abs(pos.z - center.z) / max(height, 0.1);
            float vFade = saturate(1.0 - vDist);

            // 3D noise for organic shape
            float3 noisePos = pos * 0.08;
            noisePos += float3(time * 0.2, time * 0.15, time * 0.05);
            float n = noise3D(noisePos);

            float d = n * hFade * vFade * gfDensity;
            d = max(0.0, d - 0.04);
            if (d < 0.001) continue;

            // Lighting with ambient floor
            float NdotL = 0.7; // ground fog is mostly flat-lit
            float3 ambient = float3(0.12, 0.1, 0.09);
            float3 lit = ambient + sunColorV.rgb * NdotL * 0.2 + gfColor * 0.5;

            float alpha = d * stepSize / height * 3.0;
            lightAccum += lit * alpha * (1.0 - accumulated);
            accumulated += alpha * (1.0 - accumulated);
            if (accumulated > 0.9) break;
        }

        totalLight += lightAccum;
        totalAlpha = saturate(totalAlpha + accumulated);
    }

    return float4(lerp(scene, scene * 0.6 + totalLight, totalAlpha * 0.7), 1.0);
}

)HLSL"
