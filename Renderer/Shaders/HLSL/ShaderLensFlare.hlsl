
struct VSInput { float2 position : POSITION; float2 texcoord : TEXCOORD; };
struct PSInput { float4 position : SV_POSITION; float2 texcoord : TEXCOORD; };

Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer LensFlareConstants : register(b0)
{
    float2 sunScreenUV;
    float sunOnScreen;
    float intensity;
    float4 sunColor;
    float2 texelSize;
    float2 pad;
};

PSInput VSPost(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.texcoord = input.texcoord;
    return output;
}

// Procedural circle: soft circular gradient
float Circle(float2 uv, float2 center, float radius, float softness)
{
    float d = length(uv - center);
    return 1.0 - smoothstep(radius - softness, radius + softness, d);
}

// Anamorphic streak: horizontal line through a point
float Streak(float2 uv, float2 center, float width, float falloff)
{
    float dy = abs(uv.y - center.y);
    float dx = abs(uv.x - center.x);
    float horiz = exp(-dy * dy / (width * width));
    float taper = exp(-dx * falloff);
    return horiz * taper;
}

float4 PSLensFlare(PSInput input) : SV_TARGET
{
    float3 scene = sceneTexture.Sample(linearSampler, input.texcoord).rgb;
    if (sunOnScreen < 0.01) return float4(scene, 1.0);

    float2 uv = input.texcoord;
    float2 sunUV = sunScreenUV;
    float2 center = float2(0.5, 0.5);

    // Direction from sun to screen center (ghosts appear along this axis)
    float2 flareAxis = center - sunUV;
    float3 flareColor = sunColor.rgb * intensity * sunOnScreen;
    float3 flare = float3(0, 0, 0);

    // --- Ghost orbs along the flare axis ---
    // Each ghost is at a different position along sun→center→opposite
    float ghostPositions[6] = { 0.2, 0.4, 0.6, 0.8, 1.2, 1.6 };
    float ghostSizes[6]     = { 0.04, 0.02, 0.06, 0.015, 0.035, 0.05 };
    float ghostBrights[6]   = { 0.15, 0.1, 0.2, 0.08, 0.12, 0.1 };
    float3 ghostTints[6] = {
        float3(1.0, 0.8, 0.5),
        float3(0.5, 0.8, 1.0),
        float3(0.8, 1.0, 0.6),
        float3(1.0, 0.6, 0.8),
        float3(0.6, 0.7, 1.0),
        float3(1.0, 0.9, 0.5)
    };

    for (int i = 0; i < 6; i++)
    {
        float2 ghostPos = sunUV + flareAxis * ghostPositions[i];
        float ghost = Circle(uv, ghostPos, ghostSizes[i], ghostSizes[i] * 0.6);
        flare += ghost * ghostBrights[i] * ghostTints[i] * flareColor;
    }

    // --- Sun halo: large soft glow around the sun ---
    float halo = Circle(uv, sunUV, 0.15, 0.12);
    flare += halo * 0.2 * flareColor;

    // --- Anamorphic horizontal streak through sun ---
    float streak = Streak(uv, sunUV, 0.006, 2.5);
    flare += streak * 0.15 * flareColor;

    // --- Subtle starburst: 6-pointed star pattern ---
    float2 toSun = uv - sunUV;
    float angle = atan2(toSun.y, toSun.x);
    float dist = length(toSun);
    float star = pow(abs(cos(angle * 3.0)), 12.0); // 6 spikes
    float starFalloff = exp(-dist * 15.0);
    flare += star * starFalloff * 0.08 * flareColor;

    return float4(scene + flare, 1.0);
}
