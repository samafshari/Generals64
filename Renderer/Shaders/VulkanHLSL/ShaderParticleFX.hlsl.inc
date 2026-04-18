R"HLSL(

struct VSInput
{
    float2 position : POSITION;
    float2 texcoord : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

Texture2D sceneTexture    : register(t0);  // post-particle scene (full-res)
Texture2D particleTexture : register(t1);  // pre-particle scene OR particle extract
SamplerState linearSampler : register(s0);

cbuffer PostConstants : register(b0)
{
    float2 texelSize;          // 1.0 / RT size
    float distortionStrength;  // heat warp intensity (e.g. 0.025)
    float glowIntensity;       // particle glow add strength (e.g. 0.6)
    float time;                // animation time for shimmer
    float colorAwareFx;        // 1.0 = enable toxin/fire hue detection
    float2 pad;
};

PSInput VSPost(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.texcoord = input.texcoord;
    return output;
}

// Extract particle contribution: post-particle minus pre-particle scene
float4 PSParticleExtract(PSInput input) : SV_TARGET
{
    float3 after  = sceneTexture.Sample(linearSampler, input.texcoord).rgb;
    float3 before = particleTexture.Sample(linearSampler, input.texcoord).rgb;
    float3 diff = max(0.0, after - before);
    return float4(diff, 1.0);
}

// Heat distortion: warp scene UVs using particle brightness gradient + shimmer
// t0 = full scene (post-particle), t1 = particle extract (raw or blurred)
float4 PSHeatDistort(PSInput input) : SV_TARGET
{
    float2 uv = input.texcoord;

    // Sample particle brightness at neighboring pixels for gradient
    float3 luma = float3(0.299, 0.587, 0.114);
    float bL = dot(particleTexture.Sample(linearSampler, uv + float2(-texelSize.x * 3.0, 0)).rgb, luma);
    float bR = dot(particleTexture.Sample(linearSampler, uv + float2( texelSize.x * 3.0, 0)).rgb, luma);
    float bU = dot(particleTexture.Sample(linearSampler, uv + float2(0, -texelSize.y * 3.0)).rgb, luma);
    float bD = dot(particleTexture.Sample(linearSampler, uv + float2(0,  texelSize.y * 3.0)).rgb, luma);

    float2 grad = float2(bR - bL, bD - bU);

    // Animated shimmer: small sine-wave perturbation keyed to UV position + time
    float shimmer = sin(uv.x * 120.0 + time * 4.0) * cos(uv.y * 90.0 + time * 3.0);

    // Color-aware: classify particle hue for toxin/fire-specific effects
    float3 particleColor = particleTexture.Sample(linearSampler, uv).rgb;
    float greenness = particleColor.g - max(particleColor.r, particleColor.b);
    float redness   = particleColor.r - max(particleColor.g * 0.5, particleColor.b);
    float brightness = dot(particleColor, float3(0.299, 0.587, 0.114));

    // Toxin: double distortion for green particles (anthrax/toxin cloud)
    float toxinBoost = (colorAwareFx > 0.5 && greenness > 0.04 && brightness > 0.02) ? 2.2 : 1.0;
    // Fire: boost warm glow for red/orange particles
    float fireBoost  = (colorAwareFx > 0.5 && redness > 0.04 && brightness > 0.02) ? 1.6 : 1.0;

    // Apply distortion with toxin boost
    float2 distortedUV = uv + grad * distortionStrength * toxinBoost * (1.0 + shimmer * 0.35);
    distortedUV = clamp(distortedUV, texelSize * 0.5, 1.0 - texelSize * 0.5);

    float3 scene = sceneTexture.Sample(linearSampler, distortedUV).rgb;

    // Add particle glow with fire-aware warm boost
    float3 glow = particleColor;
    glow.r *= fireBoost;
    scene += glow * glowIntensity;

    return float4(scene, 1.0);
}

// Glow-only composite: scene + blurred particle glow (no distortion)
float4 PSGlowComposite(PSInput input) : SV_TARGET
{
    float3 scene = sceneTexture.Sample(linearSampler, input.texcoord).rgb;
    float3 glow  = particleTexture.Sample(linearSampler, input.texcoord).rgb;

    // Color-aware fire boost
    float redness = glow.r - max(glow.g * 0.5, glow.b);
    float brightness = dot(glow, float3(0.299, 0.587, 0.114));
    float fireBoost = (colorAwareFx > 0.5 && redness > 0.04 && brightness > 0.02) ? 1.6 : 1.0;
    glow.r *= fireBoost;

    return float4(scene + glow * glowIntensity, 1.0);
}

)HLSL"
