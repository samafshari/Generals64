#pragma once

// Embedded HLSL shader source code.
// These are compiled at startup via D3DCompile.

namespace Render
{

static const char* g_shader3D = R"HLSL(

static const int MAX_DIRECTIONAL_LIGHTS = 3;
static const int MAX_POINT_LIGHTS = 4;

cbuffer FrameConstants : register(b0)
{
    row_major float4x4 viewProjection;
    float4 cameraPos;
    float4 ambientColor;
    float4 lightDirections[MAX_DIRECTIONAL_LIGHTS];
    float4 lightColors[MAX_DIRECTIONAL_LIGHTS];
    float4 lightingOptions; // x = dir light count, y = time (ms), z = point light count, w = water height
    float4 pointLightPositions[MAX_POINT_LIGHTS]; // xyz = position, w = outer radius
    float4 pointLightColors[MAX_POINT_LIGHTS];    // rgb = color * intensity, a = inner radius
    float4 cloudParams;  // x = UV scale (1/315), y = X scroll speed, z = Y scroll speed, w = enabled (0/1)
    float4 shroudParams; // x = 1/worldWidth, y = 1/worldHeight, z = worldOffsetX, w = worldOffsetY  (0 if disabled)
    float4 atmosphereParams; // x = fog density, y = scatter power, z = specular intensity, w = unused
    row_major float4x4 shadowMapMatrix; // world -> shadow UV + depth
    float4 shadowParams;                // x = enabled (0/1), y = texel size, z = bias, w = unused
};

cbuffer ObjectConstants : register(b1)
{
    row_major float4x4 world;
    float4 objectColor;
    // x = cosmetic shader variant id. 0 = stock (no effect).
    // y = isPlayerDrawable (0/1) — gates the effect so terrain etc. are unaffected.
    // z, w = reserved.
    float4 shaderParams;
};

// --- Vertex formats ---

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 color    : COLOR;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : WORLDPOS;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 color    : COLOR;
};

struct VSInput2Tex
{
    float3 position  : POSITION;
    float3 normal    : NORMAL;
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
    float4 color     : COLOR;
};

struct PSInput2Tex
{
    float4 position  : SV_POSITION;
    float3 worldPos  : WORLDPOS;
    float3 normal    : NORMAL;
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
    float4 color     : COLOR;
};

// --- Textures and samplers ---

Texture2D diffuseTexture : register(t0);
Texture2D bumpTexture    : register(t1);
Texture2D depthTexture   : register(t2); // scene depth for water shore foam
Texture2D shroudTexture  : register(t3);
Texture2D<float> shadowMapTexture : register(t4);
SamplerState linearSampler : register(s0);
SamplerState wrapSampler   : register(s1); // always WRAP mode, for tiling textures
SamplerComparisonState shadowSampler : register(s2);

// --- Shared lighting functions ---

float3 SafeNormal(float3 N)
{
    return length(N) > 0.001 ? normalize(N) : float3(0, 1, 0);
}

float3 ComputeLighting(float3 worldPos, float3 N)
{
    float3 lighting = ambientColor.rgb;

    [unroll]
    for (int i = 0; i < MAX_DIRECTIONAL_LIGHTS; ++i)
    {
        if (i >= (int)lightingOptions.x)
            break;
        float3 L = normalize(-lightDirections[i].xyz);
        float NdotL = saturate(dot(N, L));
        lighting += lightColors[i].rgb * NdotL;
    }

    int numPointLights = (int)lightingOptions.z;
    [unroll]
    for (int p = 0; p < MAX_POINT_LIGHTS; ++p)
    {
        if (p >= numPointLights)
            break;
        float3 lightPos = pointLightPositions[p].xyz;
        float outerRadius = pointLightPositions[p].w;
        float3 lightColor = pointLightColors[p].rgb;
        float innerRadius = pointLightColors[p].a;
        float3 toLight = lightPos - worldPos;
        float dist = length(toLight);
        float attenuation = 1.0 - saturate((dist - innerRadius) / max(outerRadius - innerRadius, 0.001));
        attenuation *= attenuation;
        float3 L = toLight / max(dist, 0.001);
        float NdotL = dot(N, L) * 0.5 + 0.5;
        lighting += lightColor * attenuation * NdotL;
    }

    // Match the original DX8 fixed-function clamp on per-vertex diffuse
    // color before D3DTOP_MODULATE — Generals' lighting INI sums to >1.0
    // (afternoon ambient+sun+sky+bounce ≈ 1.57) and the original game
    // relied on this implicit clamp to keep textures from washing out.
    return saturate(lighting);
}

float3 ApplyUnderwaterFade(float3 color, float worldZ)
{
    float waterZ = lightingOptions.w;
    if (waterZ > 0.01 && worldZ < waterZ)
    {
        float depthFade = 0.5;
        float depthScale = saturate((waterZ - worldZ) / waterZ);
        color *= 1.0 - depthScale * (1.0 - depthFade);
    }
    return color;
}

// Procedural value noise for cloud shadows (replaces texture lookup).
// Uses hash-based smooth noise with two octaves to approximate the
// original TSCloudMed.tga scrolling cloud pattern.
float CloudNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f); // smoothstep

    // Hash corners (fast integer-based)
    float a = frac(sin(dot(i,                 float2(127.1, 311.7))) * 43758.5453);
    float b = frac(sin(dot(i + float2(1, 0),  float2(127.1, 311.7))) * 43758.5453);
    float c = frac(sin(dot(i + float2(0, 1),  float2(127.1, 311.7))) * 43758.5453);
    float d = frac(sin(dot(i + float2(1, 1),  float2(127.1, 311.7))) * 43758.5453);

    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// Per-pixel cloud shadow: procedural scrolling noise.
// Returns darkening multiplier (1.0 = no shadow, <1.0 = shadowed).
float3 ApplyCloudShadow(float3 color, float3 worldPos)
{
    if (cloudParams.w < 0.5)
        return color;

    float uvScale = cloudParams.x;
    float timeMs = lightingOptions.y;
    float2 cloudUV = worldPos.xy * uvScale + cloudParams.yz * (timeMs * 0.001);

    // Two octaves of noise, matching the look of the original cloud texture
    float n  = CloudNoise(cloudUV * 8.0) * 0.65;
    n       += CloudNoise(cloudUV * 16.0) * 0.35;

    // Remap: 0.55..1.0 range so shadows are subtle (not pitch black)
    float shadow = saturate(n * 0.45 + 0.55);

    return color * shadow;
}

// Per-pixel fog of war: samples shroud grid texture from world position.
// shroudParams.xy = inverse world dimensions, shroudParams.zw = world offset
float3 ApplyShroud(float3 color, float3 worldPos)
{
    if (shroudParams.x < 0.00001)
        return color; // shroud disabled (params not set)
    float2 shroudUV = (worldPos.xy - shroudParams.zw) * shroudParams.xy;
    float3 shroud = shroudTexture.Sample(linearSampler, shroudUV).rgb;
    return color * shroud;
}

// Atmospheric scattering: distance fog tinted warm toward sun, cool away.
// Creates depth and cinematic atmosphere across the scene.
float3 ApplyAtmosphere(float3 color, float3 worldPos)
{
    float fogDensity = atmosphereParams.x;
    if (fogDensity < 0.000001) return color;

    float dist = length(worldPos - cameraPos.xyz);
    float fogFactor = 1.0 - exp(-dist * fogDensity);
    fogFactor = saturate(fogFactor);

    // View direction toward the pixel
    float3 V = normalize(worldPos - cameraPos.xyz);
    float3 sunDir = normalize(-lightDirections[0].xyz);

    // Warm scatter near sun direction, cool elsewhere
    float sunInfluence = saturate(dot(V, sunDir));
    sunInfluence = pow(sunInfluence, atmosphereParams.y); // scatter power

    float3 coolFog = float3(0.45, 0.55, 0.7);  // blue-gray haze
    float3 warmFog = float3(0.85, 0.7, 0.45) * lightColors[0].rgb; // sun-tinted gold
    float3 fogColor = lerp(coolFog, warmFog, sunInfluence);

    // Blend ambient into fog so it matches scene lighting
    fogColor = fogColor * 0.7 + ambientColor.rgb * 0.3;

    return lerp(color, fogColor, fogFactor);
}

// Surface specular: subtle Blinn-Phong on all lit geometry.
// Makes buildings/terrain gleam in sunlight.
float3 ApplySurfaceSpecular(float3 color, float3 worldPos, float3 N)
{
    float specIntensity = atmosphereParams.z;
    if (specIntensity < 0.001) return color;

    float3 V = normalize(cameraPos.xyz - worldPos);
    float3 L = normalize(-lightDirections[0].xyz);
    float3 H = normalize(V + L);
    float NdotH = saturate(dot(N, H));
    float spec = pow(NdotH, 24.0) * specIntensity;
    return color + spec * lightColors[0].rgb;
}

)HLSL"

// Second part of g_shader3D (MSVC string literal size limit requires splitting)
R"HLSL(

// GPU Shadow Map: 3x3 PCF sampling. shadowParams.w is a debug mode:
//   0 = normal PCF shadow (floor=0.25 for contrast during bring-up)
//   1 = visualize raw stored shadow-map depth (proves the caster pass
//       wrote depth — if terrain shows a dark gradient, casters work)
//   2 = force-darken everything inside the light frustum to 0.3 (proves
//       the matrix chain maps receivers into [0,1] UV range)
float ComputeShadow(float3 worldPos)
{
    if (shadowParams.x < 0.5) return 1.0;
    float4 shadowClip = mul(float4(worldPos, 1.0), shadowMapMatrix);
    float3 shadowUV = shadowClip.xyz;
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0) return 1.0;

    int debugMode = (int)shadowParams.w;
    if (debugMode == 1)
    {
        return shadowMapTexture.SampleLevel(linearSampler, shadowUV.xy, 0).r;
    }
    if (debugMode == 2)
    {
        return 0.3;
    }

    float depth = shadowUV.z - shadowParams.z;
    float ts = shadowParams.y;
    float shadow = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            shadow += shadowMapTexture.SampleCmpLevelZero(shadowSampler, shadowUV.xy + float2(x, y) * ts, depth);
    shadow /= 9.0;
    return lerp(0.25, 1.0, shadow);
}

// --- Vertex shaders ---

PSInput VSMain(VSInput input)
{
    PSInput output;
    float4 worldPos = mul(float4(input.position, 1.0), world);
    output.worldPos = worldPos.xyz;
    output.position = mul(worldPos, viewProjection);
    float3 worldNormal = mul(float3(input.normal), (float3x3)world);
    float nLen = length(worldNormal);
    output.normal = nLen > 0.001 ? worldNormal / nLen : float3(0, 1, 0);
    output.texcoord = input.texcoord;
    output.color = input.color * objectColor;
    return output;
}

PSInput2Tex VSMainTwoTex(VSInput2Tex input)
{
    PSInput2Tex output;
    float4 worldPos = mul(float4(input.position, 1.0), world);
    output.worldPos = worldPos.xyz;
    output.position = mul(worldPos, viewProjection);
    float3 worldNormal = mul(float3(input.normal), (float3x3)world);
    float nLen = length(worldNormal);
    output.normal = nLen > 0.001 ? worldNormal / nLen : float3(0, 1, 0);
    output.texcoord0 = input.texcoord0;
    output.texcoord1 = input.texcoord1;
    output.color = input.color * objectColor;
    return output;
}

// Skybox VS: forces depth = 1.0 (far plane) so everything draws on top
PSInput VSMainSkybox(VSInput input)
{
    PSInput output;
    float4 worldPos = mul(float4(input.position, 1.0), world);
    output.worldPos = worldPos.xyz;
    output.position = mul(worldPos, viewProjection);
    output.position.z = output.position.w; // depth = 1.0 at far plane
    output.normal = float3(0, 1, 0);
    output.texcoord = input.texcoord;
    output.color = input.color * objectColor;
    return output;
}

// Water VS: flat mesh with UV animation (scroll + wobble) and feathering Z offset.
// objectColor.w encodes the feathering Z offset (0 for base layer).
// UV animation uses lightingOptions.y (time in ms) for scroll and wobble.
PSInput VSMainWater(VSInput input)
{
    PSInput output;
    float4 worldPos = mul(float4(input.position, 1.0), world);
    worldPos.z += 0.4; // small Z bias above terrain

    // Feathering: objectColor.w carries the Z offset for this layer
    worldPos.z += objectColor.w;

    output.worldPos = worldPos.xyz;
    output.position = mul(worldPos, viewProjection);
    float3 worldNormal = mul(float3(input.normal), (float3x3)world);
    float nLen = length(worldNormal);
    output.normal = nLen > 0.001 ? worldNormal / nLen : float3(0, 0, 1);

    // UV animation: scroll + wobble computed on GPU instead of CPU
    float timeMs = lightingOptions.y;
    float uScroll = timeMs * 0.00003;
    float vScroll = timeMs * 0.00003;
    float wobbleTime = timeMs * 0.00006;
    float wobble = 0.02 * cos(11.0 * wobbleTime) * sin(25.0 * wobbleTime + worldPos.x * 3.14159 / 40.0);
    output.texcoord = input.texcoord + float2(uScroll + wobble, vScroll);

    output.color = float4(objectColor.xyz, 1.0);
    return output;
}

// --- Pixel shaders ---

// =============================================================================
//  Cosmetic shader effects (player profile-driven).
//
//  Variants must match GeneralsRemastered.Data.ShaderEffects.All:
//    0 stock  1 pulse  2 rainbow  3 shimmer
//    4 chrome 5 holographic 6 hex camo 7 frost
//
//  Pure-math (no texture samples), per-draw uniform branch — modern GPUs
//  treat this as scalar, effectively free for stock-shader meshes.
// =============================================================================

// Cheap deterministic 2D hash (returns 0..1).
float Hash12(float2 p)
{
    float3 p3 = frac(p.xyx * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

// Worley-ish cellular noise (returns 0..1).
float CellNoise(float2 uv)
{
    float2 i = floor(uv);
    float2 f = frac(uv);
    float minDist = 1.0;
    [unroll] for (int y = -1; y <= 1; ++y)
    [unroll] for (int x = -1; x <= 1; ++x)
    {
        float2 neighbor = float2(x, y);
        float2 cellPt = float2(Hash12(i + neighbor), Hash12(i + neighbor + 17.13));
        float2 diff = neighbor + cellPt - f;
        minDist = min(minDist, dot(diff, diff));
    }
    return saturate(sqrt(minDist));
}

// Rotate hue of an RGB color by `angle` radians.
float3 HueRotate(float3 rgb, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    // YIQ-based hue rotation matrix — fast, no HSV conversion needed.
    float3x3 m = float3x3(
        0.299 + 0.701 * c + 0.168 * s, 0.587 - 0.587 * c + 0.330 * s, 0.114 - 0.114 * c - 0.497 * s,
        0.299 - 0.299 * c - 0.328 * s, 0.587 + 0.413 * c + 0.035 * s, 0.114 - 0.114 * c + 0.292 * s,
        0.299 - 0.300 * c + 1.250 * s, 0.587 - 0.588 * c - 1.050 * s, 0.114 + 0.886 * c - 0.203 * s);
    return saturate(mul(m, rgb));
}

float3 ApplyShaderEffect(float3 inColor, float3 worldPos, float3 N, float4 objColor, int variant, float tMs)
{
    float t = tMs * 0.001;
    float3 teamRgb = objColor.rgb;

    if (variant == 1) // Pulse — breathing brightness
    {
        float puls = 0.65 + 0.55 * sin(t * 3.0);
        return inColor * puls;
    }
    if (variant == 2) // Rainbow — rotate hue
    {
        return HueRotate(inColor, t * 1.5);
    }
    if (variant == 3) // Shimmer — bright sparkles
    {
        float sparkle = Hash12(worldPos.xy * 30.0 + t * 8.0);
        return inColor + teamRgb * pow(sparkle, 6.0) * 1.6;
    }
    if (variant == 4) // Chrome — desaturated body + rim highlight
    {
        float3 V = normalize(cameraPos.xyz - worldPos);
        float fres = pow(1.0 - saturate(dot(N, V)), 3.0);
        float gray = dot(inColor, float3(0.3, 0.6, 0.1));
        return lerp(gray.xxx * 1.1, inColor, 0.25) + fres.xxx * 0.9;
    }
    if (variant == 5) // Holographic — iridescent fresnel bands
    {
        float3 V = normalize(cameraPos.xyz - worldPos);
        float fres = 1.0 - saturate(dot(N, V));
        float3 iri = float3(
            sin(fres * 12.0 + t * 1.7),
            sin(fres * 12.0 + t * 1.7 + 2.1),
            sin(fres * 12.0 + t * 1.7 + 4.2)) * 0.5 + 0.5;
        return inColor + iri * fres * 0.8;
    }
    if (variant == 6) // Hex camo — two-tone Worley pattern at team color
    {
        float cell = CellNoise(worldPos.xy * 0.4);
        float pattern = step(0.45, cell);
        float3 dark = inColor * 0.55;
        return lerp(dark, teamRgb * 1.1, pattern * 0.7);
    }
    if (variant == 7) // Frost — desaturate + cyan rim
    {
        float3 V = normalize(cameraPos.xyz - worldPos);
        float fres = pow(1.0 - saturate(dot(N, V)), 2.0);
        float gray = dot(inColor, float3(0.3, 0.6, 0.1));
        return lerp(gray.xxx * 1.05, inColor, 0.45) + float3(0.3, 0.6, 1.0) * fres * 0.7;
    }
    return inColor;
}

// Standard terrain/model shader with integrated cloud shadows
float4 PSMain(PSInput input) : SV_TARGET
{
    float4 texColor = diffuseTexture.Sample(linearSampler, input.texcoord);
    float3 N = SafeNormal(input.normal);

    // ZHCA "house icon" batches (Command Center emblem, HQ faction logo).
    // The asset-loader-recolored texture has α forced to 1 everywhere and the
    // dark pixels form the background we want to key out. The CPU side has
    // forced input.color to the owning player's house color. Only the
    // deferred-translucent flush raises this flag, so opaque infantry/unit
    // ZHCA batches keep their standard lit path untouched.
    if (shaderParams.w > 0.5)
    {
        float3 playerColor = input.color.rgb;
        // Run the player's selected cosmetic shader effect on the house color
        // so the icon animates with the rest of the player's owned drawables
        // (pulse, rainbow, shimmer, etc.). Gated by isPlayerDrawable + accent
        // mesh flag which FlushTranslucent forces on for ZHCA icons.
        int variantZ = (int)shaderParams.x;
        if (variantZ != 0 && shaderParams.y > 0.5 && shaderParams.z > 0.5)
        {
            playerColor = ApplyShaderEffect(playerColor, input.worldPos, N,
                                            objectColor, variantZ, lightingOptions.y);
        }
        float brightness = max(texColor.r, max(texColor.g, texColor.b));
        clip(brightness - 0.05);
        float3 rgbZ = playerColor * brightness;
        rgbZ = ApplyShroud(rgbZ, input.worldPos);
        return float4(rgbZ, brightness);
    }

    float4 finalColor = texColor * input.color;
    finalColor.rgb *= ComputeLighting(input.worldPos, N);
    finalColor.rgb *= ComputeShadow(input.worldPos);
    finalColor.rgb = ApplySurfaceSpecular(finalColor.rgb, input.worldPos, N);
    finalColor.rgb = ApplyCloudShadow(finalColor.rgb, input.worldPos);
    finalColor.rgb = ApplyUnderwaterFade(finalColor.rgb, input.worldPos.z);
    finalColor.rgb = ApplyShroud(finalColor.rgb, input.worldPos);
    finalColor.rgb = ApplyAtmosphere(finalColor.rgb, input.worldPos);

    // Cosmetic player-profile shader effect. Per-draw uniform branch;
    // gated by isPlayerDrawable so terrain/props/particles aren't affected.
    int variant = (int)shaderParams.x;
    if (variant != 0 && shaderParams.y > 0.5 && shaderParams.z > 0.5)
    {
        finalColor.rgb = ApplyShaderEffect(finalColor.rgb, input.worldPos, N,
                                           objectColor, variant, lightingOptions.y);
    }
    return finalColor;
}

// Unlit shader for additive FX (lasers, streaks, line effects).
// No lighting, shadows, shroud, or atmosphere — just texture * vertex color.
float4 PSMainUnlit(PSInput input) : SV_TARGET
{
    float4 texColor = diffuseTexture.Sample(linearSampler, input.texcoord);
    return texColor * input.color;
}

// Skybox pixel shader. The sky dome must NEVER receive shadow — the sun itself
// is what casts those shadows, so darkening the sky at points where a building
// would occlude it is nonsensical and shows up as "patches of dark sky" moving
// as the shadow-map ortho frustum slides with the camera. Also skip the
// surface specular/shroud so the sky stays uniform.
float4 PSMainSkybox(PSInput input) : SV_TARGET
{
    float4 texColor = diffuseTexture.Sample(linearSampler, input.texcoord);
    float4 finalColor = texColor * input.color;
    finalColor.rgb = ApplyAtmosphere(finalColor.rgb, input.worldPos);
    return finalColor;
}

// Heat-distortion smudge shader.
// Approximates the original DX8 W3DSmudgeManager refraction pass: copies
// the back buffer into a "scene" texture (bound on slot 1, bumpTexture),
// samples it at offset UVs derived from the smudge texture (slot 0,
// diffuseTexture — typically a noise/normal pattern). The screen-space UV
// is reconstructed from input.position.xy / viewport. The smudge texture's
// (R,G) channels minus 0.5 give a -1..1 displacement that's scaled by the
// vertex color's alpha (smudge intensity).
//
// Used by the doParticles smudge fallback path when g_useEnhancedSmudges
// is enabled. Default OFF — when ON, smudge particles render as visible
// heat-haze ripples around explosions.
cbuffer SmudgeConstants : register(b3)
{
    float4 smudgeViewportSize; // x=width, y=height, z=1/width, w=1/height
};
float4 PSMainSmudge(PSInput input) : SV_TARGET
{
    // Smudge "displacement" texture (slot 0)
    float4 smudgeSample = diffuseTexture.Sample(linearSampler, input.texcoord);
    // Scene back buffer copy (slot 1)
    // Per-particle alpha controls displacement strength
    float strength = input.color.a * 0.04;  // 4% of screen width max distortion
    float2 disp = (smudgeSample.rg - 0.5) * 2.0 * strength;

    float2 screenUV = input.position.xy * smudgeViewportSize.zw;
    screenUV += disp;
    screenUV = saturate(screenUV);
    float4 sceneColor = bumpTexture.Sample(linearSampler, screenUV);

    // Output alpha = smudge texture's alpha modulated by vertex color
    return float4(sceneColor.rgb, smudgeSample.a * input.color.a);
}

// Laser beam glow: U=0..1 across beam width, computes bright core + soft edge falloff.
// Vertex color carries the beam color and alpha.
float4 PSLaserGlow(PSInput input) : SV_TARGET
{
    // U: 0 at one edge, 1 at other edge → remap to signed distance from center
    float d = abs(input.texcoord.x - 0.5) * 2.0; // 0=center, 1=edge
    float d2 = d * d;

    // Sharp bright core + wider soft glow, using cheap rational falloff
    float core = 1.0 / (1.0 + d2 * 80.0);   // tight bright center
    float glow = 1.0 / (1.0 + d2 * 5.0);    // wider soft halo

    float3 col = input.color.rgb;
    float3 beam = lerp(col, float3(1,1,1), core * 0.8) * (core * 2.5 + glow * 0.7);
    float alpha = input.color.a * (core + glow * 0.3);

    return float4(beam, alpha);
}

float4 PSMainAlphaTest(PSInput input) : SV_TARGET
{
    float4 texColor = diffuseTexture.Sample(linearSampler, input.texcoord);
    float3 N = SafeNormal(input.normal);

    clip(texColor.a - 0.3);
    float4 finalColor = texColor * input.color;
    finalColor.rgb *= ComputeLighting(input.worldPos, N);
    finalColor.rgb *= ComputeShadow(input.worldPos);
    finalColor.rgb = ApplySurfaceSpecular(finalColor.rgb, input.worldPos, N);
    finalColor.rgb = ApplyCloudShadow(finalColor.rgb, input.worldPos);
    finalColor.rgb = ApplyShroud(finalColor.rgb, input.worldPos);
    finalColor.rgb = ApplyAtmosphere(finalColor.rgb, input.worldPos);

    int variant = (int)shaderParams.x;
    if (variant != 0 && shaderParams.y > 0.5 && shaderParams.z > 0.5)
    {
        finalColor.rgb = ApplyShaderEffect(finalColor.rgb, input.worldPos, N,
                                           objectColor, variant, lightingOptions.y);
    }
    return finalColor;
}

)HLSL"

// 2b: split for MSVC string literal size limit.
R"HLSL(

// GPU projected mesh decal (faction logos on buildings).
// Re-renders the same mesh geometry; projects world position into decal UV
// space. The decal texture is grayscale art (faction emblem); the player
// color is delivered through input.color from the CPU at draw time, so the
// shader just multiplies texture by player color. No material-diffuse path.
cbuffer MeshDecalConstants : register(b2)
{
    row_major float4x4 decalProjection; // world -> decal UV space
    float4 decalParams;                 // x = backface threshold, y = opacity
};

float4 PSMeshDecal(PSInput input) : SV_TARGET
{
    float3 projDir = normalize(float3(decalProjection._31, decalProjection._32, decalProjection._33));
    float3 N = SafeNormal(input.normal);
    if (dot(N, -projDir) < decalParams.x)
        discard;

    float4 decalSpace = mul(float4(input.worldPos, 1.0), decalProjection);
    float3 decalUV = decalSpace.xyz / decalSpace.w;
    if (any(decalUV < 0.0) || any(decalUV > 1.0))
        discard;

    // ZHCA_ faction-logo authoring convention (inverted mapping):
    //   * TGA-transparent pixels (alpha == 0)        -> solid black
    //   * TGA-red / any opaque non-dark pixels       -> solid black
    //   * TGA-opaque AND dark pixels (symbol/border) -> punched out (transparent)
    //     so the building texture shows through the emblem holes.
    //
    // In one expression: draw black wherever the texel is NOT "opaque-and-dark".
    // isDarkOpaque == 1 exactly where the TGA has an opaque dark texel
    // (high alpha, low brightness); that's the region we want to skip.
    float4 tex = diffuseTexture.Sample(linearSampler, decalUV.xy);
    float brightness   = max(tex.r, max(tex.g, tex.b));
    float isDarkOpaque = tex.a * (1.0 - brightness);
    float a = (1.0 - isDarkOpaque) * decalParams.y;
    clip(a - 0.01);

    float3 rgb = ApplyShroud(float3(0.0, 0.0, 0.0), input.worldPos);
    return float4(rgb, a);
}

// Construction-ghost shader. Used by the placement preview drawable while the
// player chooses where to drop a building. The pixel runs the regular lit/
// shadowed path so the ghost reads as a faint version of the real model, then
// optionally blends the lit color toward a tint color (red for invalid
// placement). Output alpha is the drawable's opacity, fed by an alpha-blend
// pipeline state with depth-write disabled.
cbuffer GhostConstants : register(b5)
{
    float4 ghostTint;   // rgb = tint color, a = tint intensity (0..1)
    float4 ghostParams; // x = opacity, yzw = unused
};

float4 PSGhost(PSInput input) : SV_TARGET
{
    float4 texColor = diffuseTexture.Sample(linearSampler, input.texcoord);
    clip(texColor.a - 0.05); // discard fully-transparent texels
    float3 N = SafeNormal(input.normal);
    float3 rgb = (texColor * input.color).rgb;
    rgb *= ComputeLighting(input.worldPos, N);
    rgb *= ComputeShadow(input.worldPos);
    rgb = lerp(rgb, ghostTint.rgb, ghostTint.a);
    rgb = ApplyShroud(rgb, input.worldPos);
    return float4(rgb, ghostParams.x);
}

)HLSL"

// Third part of g_shader3D (MSVC string literal size limit requires splitting)
R"HLSL(

// Custom edge art pass: ALPHAREF=0x84, ALPHAFUNC=GREATEREQUAL
float4 PSMainAlphaTestEdge(PSInput input) : SV_TARGET
{
    float4 texColor = diffuseTexture.Sample(linearSampler, input.texcoord);
    clip(texColor.a - (132.0f / 255.0f));
    float3 N = SafeNormal(input.normal);
    float4 finalColor = texColor * input.color;
    finalColor.rgb *= ComputeLighting(input.worldPos, N);
    finalColor.rgb *= ComputeShadow(input.worldPos);
    finalColor.rgb = ApplySurfaceSpecular(finalColor.rgb, input.worldPos, N);
    finalColor.rgb = ApplyCloudShadow(finalColor.rgb, input.worldPos);
    finalColor.rgb = ApplyShroud(finalColor.rgb, input.worldPos);
    finalColor.rgb = ApplyAtmosphere(finalColor.rgb, input.worldPos);

    int variant = (int)shaderParams.x;
    if (variant != 0 && shaderParams.y > 0.5 && shaderParams.z > 0.5)
    {
        finalColor.rgb = ApplyShaderEffect(finalColor.rgb, input.worldPos, N,
                                           objectColor, variant, lightingOptions.y);
    }
    return finalColor;
}

// Terrain edge base pass: two-texture mask. ALPHAREF=0x7B, ALPHAFUNC=LESSEQUAL
float4 PSMainTerrainMaskBase(PSInput2Tex input) : SV_TARGET
{
    float4 maskColor = bumpTexture.Sample(linearSampler, input.texcoord1);
    clip((123.0f / 255.0f) - maskColor.a);
    float4 texColor = diffuseTexture.Sample(linearSampler, input.texcoord0);
    float3 N = SafeNormal(input.normal);
    float4 finalColor = texColor * input.color;
    finalColor.rgb *= ComputeLighting(input.worldPos, N);
    finalColor.rgb *= ComputeShadow(input.worldPos);
    finalColor.rgb = ApplySurfaceSpecular(finalColor.rgb, input.worldPos, N);
    finalColor.rgb = ApplyCloudShadow(finalColor.rgb, input.worldPos);
    finalColor.rgb = ApplyUnderwaterFade(finalColor.rgb, input.worldPos.z);
    finalColor.rgb = ApplyShroud(finalColor.rgb, input.worldPos);
    finalColor.rgb = ApplyAtmosphere(finalColor.rgb, input.worldPos);
    return finalColor;
}

// Water shader. Two paths gated by atmosphereParams.w:
//   0.0 = classic (single normal, simple lighting + spec — matches the
//         original DX8 build's look)
//   1.0 = enhanced (dual scrolling normals, fresnel sky tint, depth-based
//         shore foam, soft transparency) — opt-in via Inspector toggle
//         "Enhanced Water" (g_useEnhancedWater), default OFF.
// Both branches honor input.color so the C++ side's tint/feathering still
// works.
float4 PSMainWaterBump(PSInput input) : SV_TARGET
{
    float timeMs = lightingOptions.y;
    float useEnhanced = atmosphereParams.w; // 0=classic, 1=enhanced

    // River flag is encoded in input.normal.x (0.01 = river, 0 = standing
    // water — set CPU-side in BuildWaterMesh). On rivers, scroll the diffuse
    // sample's V over time so the river visibly flows downstream. Only in
    // enhanced mode; classic still draws rivers as flat lakes (matches what
    // previously shipped).
    float isRiver = step(0.005, input.normal.x);
    float2 sampleUV = input.texcoord;
    if (useEnhanced > 0.5 && isRiver > 0.5)
        sampleUV.y += timeMs * 0.00006;
    float4 texColor = diffuseTexture.Sample(linearSampler, sampleUV);

    float4 finalColor = texColor * input.color;

    if (useEnhanced < 0.5)
    {
        // CLASSIC: single bump sample, gentle scaled normal, simple spec.
        float2 bumpUV = input.texcoord * 3.0 + float2(timeMs * 0.0001, timeMs * 0.00007);
        float4 bumpSample = bumpTexture.Sample(wrapSampler, bumpUV);
        float3 bumpNormal = float3((bumpSample.r - 0.5) * 0.12, (bumpSample.g - 0.5) * 0.12, 1.0);
        float3 N = normalize(input.normal + bumpNormal);
        float3 lighting = ComputeLighting(input.worldPos, N);
        finalColor.rgb *= lighting;
        float3 V = normalize(cameraPos.xyz - input.worldPos);
        float3 H = normalize(V + normalize(-lightDirections[0].xyz));
        float spec = pow(saturate(dot(N, H)), 32.0) * 0.15;
        finalColor.rgb += spec * lightColors[0].rgb;
        // Shoreline fade: less water alpha where the lake floor is very
        // close to the surface, so the foam quads can do more visual work
        // at the water-land boundary. Threshold 0.00002 in NDC depth — far
        // smaller than the previous 0.0008 attempt which killed the whole
        // water surface. Effective only within a few world units of the
        // shore on a typical RTS perspective camera.
        int2 sc = int2(input.position.xy);
        float sd = depthTexture.Load(int3(sc, 0)).r;
        float shore = saturate((sd - input.position.z) / 0.00002);
        finalColor.a *= shore;
        return finalColor;
    }

    // ENHANCED PATH (only when atmosphereParams.w == 1.0)
    // Polish tuning (iter 10): audit run revealed visible tessellation/
    // tile pattern from high bump scale + high freq normals. Dampened all
    // the aggressive parameters so the enhanced look is a subtle upgrade
    // over classic, not a different art direction.
    float2 bumpUV1 = input.texcoord * 2.0  + float2(timeMs *  0.00012,  timeMs *  0.00009);
    float2 bumpUV2 = input.texcoord * 3.0  + float2(timeMs * -0.00007,  timeMs *  0.00011);
    float4 b1 = bumpTexture.Sample(wrapSampler, bumpUV1);
    float4 b2 = bumpTexture.Sample(wrapSampler, bumpUV2);
    float3 bumpNormal = float3(
        ((b1.r + b2.r) - 1.0) * 0.15,
        ((b1.g + b2.g) - 1.0) * 0.15,
        1.0);
    float3 N = normalize(input.normal + bumpNormal);

    float3 lighting = ComputeLighting(input.worldPos, N);
    finalColor.rgb *= lighting;

    // Fresnel sky tint — dampened from 0.35 → 0.15 mix so water keeps its
    // INI-authored color in the middle, only tinting slightly at grazing
    // angles near the camera.
    float3 V = normalize(cameraPos.xyz - input.worldPos);
    float NdotV = saturate(dot(N, V));
    float fresnel = pow(1.0 - NdotV, 5.0) * 0.85 + 0.05;
    float3 skyTint = float3(0.45, 0.55, 0.70);
    finalColor.rgb = lerp(finalColor.rgb, skyTint * lighting, fresnel * 0.15);

    // Subtle specular sparkle — loosened from pow96*0.55 to pow48*0.25
    float3 H = normalize(V + normalize(-lightDirections[0].xyz));
    float spec = pow(saturate(dot(N, H)), 48.0) * 0.25;
    finalColor.rgb += spec * lightColors[0].rgb;

    // Depth-based shore foam using smoothstep (smoother than saturate+pow).
    // Tightened threshold from 1/2000 to 1/3500 so foam is narrower — only
    // right at the shore. Foam color dampened from pure white to a subtle
    // blue-tinted white so it doesn't look like a hard ring.
    int2 screenCoord = int2(input.position.xy);
    float sceneDepth = depthTexture.Load(int3(screenCoord, 0)).r;
    float waterDepth = input.position.z;
    float depthDelta = sceneDepth - waterDepth;
    float foam = 1.0 - smoothstep(0.0, 0.00035, depthDelta);
    float foamNoise = b1.b * 0.5 + b2.b * 0.5;
    finalColor.rgb += float3(0.65, 0.75, 0.85) * foam * (0.5 + foamNoise * 0.3);

    return finalColor;
}

)HLSL";


// GPU-instanced snow: one quad per instance, position computed in VS from noise table
static const char* g_shaderSnow = R"HLSL(

cbuffer FrameConstants : register(b0)
{
    row_major float4x4 viewProjection;
    float4 cameraPos;
};

cbuffer SnowConstants : register(b2)
{
    float4 snowGrid;     // x = emitterSpacing, y = quadHalfSize, z = snowCeiling, w = heightTraveled
    float4 snowAnim;     // x = amplitude, y = freqScaleX, z = freqScaleY, w = boxDimensions
    int4   snowOrigin;   // x = cubeOriginX, y = cubeOriginY, z = gridWidth, w = unused
    float4 snowCamRight; // xyz = camera right vector
    float4 snowCamUp;    // xyz = camera up vector
    float4 snowCamFwd;   // xyz = camera forward vector, w = cull distance
};

StructuredBuffer<float> noiseTable : register(t3);
Texture2D snowTexture : register(t0);
SamplerState linearSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

PSInput VSSnow(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    PSInput output;

    // Grid coordinates from instanceID
    int gridW = snowOrigin.z;
    int localY = (int)instanceID / gridW;
    int localX = (int)instanceID - localY * gridW;
    int gx = snowOrigin.x + localX;
    int gy = snowOrigin.y + localY;

    // Noise lookup (64x64 wrapping)
    int noiseX = (gx + 100000) & 63;
    int noiseY = (gy + 100000) & 63;
    float startH = noiseTable[noiseX + noiseY * 64];

    // Current height: ceiling minus animated offset (wraps around boxDimensions)
    float h0 = snowGrid.z - fmod(snowGrid.w + startH, snowAnim.w);

    // World position
    float3 pos;
    pos.x = (float)gx * snowGrid.x;
    pos.y = (float)gy * snowGrid.x;
    pos.z = h0;

    // Sine-wave lateral offset
    pos.x += snowAnim.x * sin(h0 * snowAnim.y + (float)gx);
    pos.y += snowAnim.x * sin(h0 * snowAnim.z + (float)gy);

    // Frustum culling: skip particles behind camera (output degenerate triangle)
    float3 toParticle = pos - cameraPos.xyz;
    float dotFwd = dot(toParticle, snowCamFwd.xyz);
    if (dotFwd < -snowCamFwd.w)
    {
        output.position = float4(0, 0, -1, 1);
        output.texcoord = float2(0, 0);
        return output;
    }

    // Billboard corners from vertexID (6 verts = 2 triangles)
    float2 cornerOffsets[6] = {
        float2(-1,  1), float2(-1, -1), float2( 1, -1),
        float2(-1,  1), float2( 1, -1), float2( 1,  1)
    };
    float2 cornerUVs[6] = {
        float2(0, 0), float2(0, 1), float2(1, 1),
        float2(0, 0), float2(1, 1), float2(1, 0)
    };

    float2 off = cornerOffsets[vertexID] * snowGrid.y;
    pos += snowCamRight.xyz * off.x + snowCamUp.xyz * off.y;

    output.position = mul(float4(pos, 1.0), viewProjection);
    output.texcoord = cornerUVs[vertexID];
    return output;
}

float4 PSSnow(PSInput input) : SV_TARGET
{
    float4 tex = snowTexture.Sample(linearSampler, input.texcoord);
    return tex * float4(1, 1, 1, 0.7);
}

)HLSL";


// GPU-instanced decals: terrain-projected quads with heightmap sampling in VS.
// Each instance is a DecalInstance struct from the StructuredBuffer.
// The vertex shader generates quad corners from SV_VertexID, rotates/scales them,
// and samples the heightmap texture for terrain-conforming Z.
static const char* g_shaderDecal = R"HLSL(

cbuffer FrameConstants : register(b0)
{
    row_major float4x4 viewProjection;
    float4 cameraPos;
};

cbuffer DecalConstants : register(b2)
{
    // xy = world-to-UV scale: 1.0 / (extent * MAP_XY_FACTOR)
    // zw = world-to-UV offset: borderSize / extent
    float4 hmTransform;
    // x = z bias above terrain, y-w = unused
    float4 hmParams;
};

struct DecalInstance
{
    float2 position;
    float2 offset;
    float2 size;
    float  angle;
    uint   color; // ABGR packed
};

StructuredBuffer<DecalInstance> g_Decals : register(t3);
Texture2D<float> g_Heightmap : register(t4);
SamplerState g_Sampler : register(s0);
Texture2D g_DecalTexture : register(t0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
    float4 color    : COLOR;
};

float4 UnpackABGR(uint packed)
{
    return float4(
        float(packed & 0xFF) / 255.0,
        float((packed >> 8) & 0xFF) / 255.0,
        float((packed >> 16) & 0xFF) / 255.0,
        float((packed >> 24) & 0xFF) / 255.0
    );
}

PSInput VSDecal(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    PSInput output;
    DecalInstance inst = g_Decals[instanceID];

    // 6 vertices = 2 triangles for a quad
    float2 corners[6] = {
        float2(-0.5, -0.5), float2( 0.5, -0.5), float2( 0.5,  0.5),
        float2(-0.5, -0.5), float2( 0.5,  0.5), float2(-0.5,  0.5)
    };
    float2 uvs[6] = {
        float2(0, 0), float2(1, 0), float2(1, 1),
        float2(0, 0), float2(1, 1), float2(0, 1)
    };

    float2 local = corners[vertexID];

    // Rotate and scale to world space
    float cosA = cos(inst.angle);
    float sinA = sin(inst.angle);
    float2 rotated = float2(
        local.x * cosA - local.y * sinA,
        local.x * sinA + local.y * cosA
    );
    float2 worldXY = rotated * inst.size + inst.position + inst.offset;

    // Sample heightmap for terrain-conforming Z
    float2 hmUV = worldXY * hmTransform.xy + hmTransform.zw;
    float worldZ = g_Heightmap.SampleLevel(g_Sampler, hmUV, 0) + hmParams.x;

    output.position = mul(float4(worldXY, worldZ, 1.0), viewProjection);
    output.texcoord = uvs[vertexID];
    output.color = UnpackABGR(inst.color);
    return output;
}

float4 PSDecal(PSInput input) : SV_TARGET
{
    float4 tex = g_DecalTexture.Sample(g_Sampler, input.texcoord);
    float4 result = tex * input.color;
    if (hmParams.y > 0.5 && tex.a > 0.99)
        result.a = max(result.r, max(result.g, result.b));
    return result;
}

)HLSL";


// Post-processing shaders: bloom extraction, blur, and composite
static const char* g_shaderPost = R"HLSL(

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

Texture2D sceneTexture  : register(t0);
Texture2D bloomTexture  : register(t1);
SamplerState linearSampler : register(s0);

cbuffer PostConstants : register(b0)
{
    float2 texelSize;   // 1.0 / render target size
    float bloomThreshold;
    float bloomIntensity;
};

PSInput VSPost(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.texcoord = input.texcoord;
    return output;
}

// Extract bright pixels for bloom
float4 PSBloomExtract(PSInput input) : SV_TARGET
{
    float3 color = sceneTexture.Sample(linearSampler, input.texcoord).rgb;
    float brightness = dot(color, float3(0.2126, 0.7152, 0.0722));
    float contrib = max(0, brightness - bloomThreshold);
    return float4(color * (contrib / max(brightness, 0.001)), 1.0);
}

// 9-tap Gaussian blur (horizontal or vertical, controlled by texelSize direction)
float4 PSBlur(PSInput input) : SV_TARGET
{
    float2 uv = input.texcoord;
    float3 result = sceneTexture.Sample(linearSampler, uv).rgb * 0.227027;
    result += sceneTexture.Sample(linearSampler, uv + texelSize * 1.0).rgb * 0.1945946;
    result += sceneTexture.Sample(linearSampler, uv - texelSize * 1.0).rgb * 0.1945946;
    result += sceneTexture.Sample(linearSampler, uv + texelSize * 2.0).rgb * 0.1216216;
    result += sceneTexture.Sample(linearSampler, uv - texelSize * 2.0).rgb * 0.1216216;
    result += sceneTexture.Sample(linearSampler, uv + texelSize * 3.0).rgb * 0.054054;
    result += sceneTexture.Sample(linearSampler, uv - texelSize * 3.0).rgb * 0.054054;
    result += sceneTexture.Sample(linearSampler, uv + texelSize * 4.0).rgb * 0.016216;
    result += sceneTexture.Sample(linearSampler, uv - texelSize * 4.0).rgb * 0.016216;
    return float4(result, 1.0);
}

// Final composite: scene + bloom
float4 PSComposite(PSInput input) : SV_TARGET
{
    float3 scene = sceneTexture.Sample(linearSampler, input.texcoord).rgb;
    float3 bloom = bloomTexture.Sample(linearSampler, input.texcoord).rgb;
    float3 result = scene + bloom * bloomIntensity;
    return float4(result, 1.0);
}

)HLSL";


// FSR-style two-pass video upscaling: EASU (upsampling) + RCAS (sharpening).
// Pass 1 (EASU): Edge-adaptive spatial upsampling from low-res source to display-res RT.
// Pass 2 (RCAS): Contrast-adaptive sharpening on the upsampled result.
static const char* g_shaderFSR = R"HLSL(

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

Texture2D srcTexture : register(t0);
SamplerState linearSampler : register(s0);
SamplerState pointSampler  : register(s1);

cbuffer FSRConstants : register(b0)
{
    float2 srcTexelSize;   // 1.0 / source texture dimensions
    float  sharpness;      // RCAS: 0 = max sharpening, 2 = none
    float  pad;
};

PSInput VSPost(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.texcoord = input.texcoord;
    return output;
}

// ── Pass 1: EASU — Bicubic Catmull-Rom Upsampling ──
// Uses a 4x4 Catmull-Rom kernel for high-quality upsampling.
// Significantly sharper than bilinear with no ringing artifacts.
// Separable: apply 1D Catmull-Rom weights in X then Y.
float4 PSEASU(PSInput input) : SV_TARGET
{
    float2 uv = input.texcoord;
    float2 srcRes = 1.0 / srcTexelSize;

    // Position in source texel space
    float2 pos = uv * srcRes - 0.5;
    float2 f = frac(pos);
    float2 pos0 = (floor(pos) + 0.5) * srcTexelSize; // center of texel [0,0]

    // Catmull-Rom weights for 4 taps: t is fractional position (0..1)
    // w(-1) = -0.5t^3 + t^2 - 0.5t
    // w( 0) =  1.5t^3 - 2.5t^2 + 1
    // w( 1) = -1.5t^3 + 2t^2 + 0.5t
    // w( 2) =  0.5t^3 - 0.5t^2

    float4 wx, wy;
    {
        float t = f.x;
        float t2 = t * t;
        float t3 = t2 * t;
        wx = float4(
            -0.5*t3 + t2 - 0.5*t,
             1.5*t3 - 2.5*t2 + 1.0,
            -1.5*t3 + 2.0*t2 + 0.5*t,
             0.5*t3 - 0.5*t2
        );
    }
    {
        float t = f.y;
        float t2 = t * t;
        float t3 = t2 * t;
        wy = float4(
            -0.5*t3 + t2 - 0.5*t,
             1.5*t3 - 2.5*t2 + 1.0,
            -1.5*t3 + 2.0*t2 + 0.5*t,
             0.5*t3 - 0.5*t2
        );
    }

    // Sample 4x4 grid using linear sampler (clamp handles edges)
    float3 result = float3(0, 0, 0);
    for (int row = -1; row <= 2; row++)
    {
        float3 rowSum = float3(0, 0, 0);
        for (int col = -1; col <= 2; col++)
        {
            float2 sampleUV = pos0 + float2(col, row) * srcTexelSize;
            // Clamp to valid UV range to avoid wrap artifacts at edges
            sampleUV = clamp(sampleUV, srcTexelSize * 0.5, 1.0 - srcTexelSize * 0.5);
            float3 s = srcTexture.SampleLevel(pointSampler, sampleUV, 0).rgb;
            rowSum += s * wx[col + 1];
        }
        result += rowSum * wy[row + 1];
    }

    return float4(result, 1.0);
}

// ── Pass 2: RCAS — Robust Contrast Adaptive Sharpening ──
// Reads from the EASU output (now at display resolution) and sharpens.
// srcTexelSize must be updated to 1.0 / display resolution for this pass.
float4 PSRCAS(PSInput input) : SV_TARGET
{
    float2 uv = input.texcoord;
    float2 ts = srcTexelSize; // now = 1/display res

    float3 c = srcTexture.SampleLevel(pointSampler, uv, 0).rgb;
    float3 n = srcTexture.SampleLevel(pointSampler, uv + float2(0, -ts.y), 0).rgb;
    float3 s = srcTexture.SampleLevel(pointSampler, uv + float2(0,  ts.y), 0).rgb;
    float3 e = srcTexture.SampleLevel(pointSampler, uv + float2( ts.x, 0), 0).rgb;
    float3 w = srcTexture.SampleLevel(pointSampler, uv + float2(-ts.x, 0), 0).rgb;

    float3 lw = float3(0.299, 0.587, 0.114);
    float lC = dot(c, lw), lN = dot(n, lw), lS = dot(s, lw);
    float lE = dot(e, lw), lW = dot(w, lw);

    float mn = min(min(lN, lS), min(lE, lW));
    float mx = max(max(lN, lS), max(lE, lW));

    float sharpenAmt = sqrt(min(mn, 1.0 - mx) / max(mx, 0.04));
    float kernel = -(1.0 / lerp(8.0, 5.0, saturate(sharpenAmt - sharpness)));

    float3 result = (n + s + e + w) * kernel + c * (1.0 - 4.0 * kernel);

    // Clamp to neighborhood to prevent ringing
    float3 minC = min(min(n, s), min(e, w));
    float3 maxC = max(max(n, s), max(e, w));
    result = clamp(result, minC, maxC);

    return float4(result, 1.0);
}

)HLSL";


// Particle FX post-processing: extract particle contribution + heat distortion
static const char* g_shaderParticleFX = R"HLSL(

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

)HLSL";


// Screen-space shockwave: expanding distortion rings from explosion positions
static const char* g_shaderShockwave = R"HLSL(

struct VSInput { float2 position : POSITION; float2 texcoord : TEXCOORD; };
struct PSInput { float4 position : SV_POSITION; float2 texcoord : TEXCOORD; };

Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer ShockwaveConstants : register(b0)
{
    float4 shockwaves[8]; // xy = screen UV, z = phase (0..1.5), w = intensity (1..0)
    float2 texelSize;
    float time;
    float shockwaveCount;
};

PSInput VSPost(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.texcoord = input.texcoord;
    return output;
}

float4 PSShockwave(PSInput input) : SV_TARGET
{
    float2 uv = input.texcoord;
    float2 totalOffset = float2(0, 0);
    int count = (int)shockwaveCount;

    for (int i = 0; i < 8; i++)
    {
        if (i >= count) break;
        float2 center = shockwaves[i].xy;
        float phase = shockwaves[i].z;
        float intensity = shockwaves[i].w;
        if (intensity < 0.01) continue;

        float2 toPixel = uv - center;
        // Correct for aspect ratio so rings are circular
        float aspect = texelSize.y / texelSize.x;
        float2 corrected = float2(toPixel.x, toPixel.y * aspect);
        float dist = length(corrected);

        // Micro shockwave: barely-there distortion ripple
        float ringRadius = phase * 0.04;
        float ringWidth = 0.004 + phase * 0.002;
        float ring = exp(-pow((dist - ringRadius) / ringWidth, 2.0));
        ring *= intensity;

        float2 dir = toPixel / max(length(toPixel), 0.0001);
        totalOffset += dir * ring * 0.002;
    }

    return sceneTexture.Sample(linearSampler, uv + totalOffset);
}

)HLSL";


// Volumetric god rays: radial blur from sun screen position
static const char* g_shaderGodRays = R"HLSL(

struct VSInput { float2 position : POSITION; float2 texcoord : TEXCOORD; };
struct PSInput { float4 position : SV_POSITION; float2 texcoord : TEXCOORD; };

Texture2D sceneTexture : register(t0);
Texture2D godRayTexture : register(t1);
SamplerState linearSampler : register(s0);

cbuffer GodRayConstants : register(b0)
{
    float2 sunScreenUV;  // sun position in UV space
    float density;       // 0.5 — how quickly rays converge
    float decay;         // 0.97 — falloff per sample
    float weight;        // 0.4 — individual sample weight
    float exposure;      // 0.25 — final brightness
    float threshold;     // 0.6 — bright pixel extraction threshold
    float numSamples;    // 32
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

// Pass 1: Extract bright pixels for god ray source
float4 PSGodRayExtract(PSInput input) : SV_TARGET
{
    float3 color = sceneTexture.Sample(linearSampler, input.texcoord).rgb;
    float brightness = dot(color, float3(0.2126, 0.7152, 0.0722));
    float contrib = max(0.0, brightness - threshold);
    // Weight by proximity to sun for directional rays
    float2 toSun = sunScreenUV - input.texcoord;
    float sunDist = length(toSun);
    float sunWeight = saturate(1.0 - sunDist * 1.2);
    return float4(color * contrib * sunWeight, 1.0);
}

// Pass 2: Radial blur toward sun (volumetric scatter)
float4 PSGodRayBlur(PSInput input) : SV_TARGET
{
    float2 uv = input.texcoord;
    float2 deltaUV = (uv - sunScreenUV) * density / numSamples;

    float3 color = float3(0, 0, 0);
    float illuminationDecay = 1.0;
    float2 sampleUV = uv;

    for (int i = 0; i < 48; i++)
    {
        if (i >= (int)numSamples) break;
        sampleUV -= deltaUV;
        sampleUV = clamp(sampleUV, 0.001, 0.999);
        float3 s = sceneTexture.Sample(linearSampler, sampleUV).rgb;
        s *= illuminationDecay * weight;
        color += s;
        illuminationDecay *= decay;
    }

    return float4(color * exposure, 1.0);
}

// Pass 3: Additive composite of god rays onto scene
float4 PSGodRayComposite(PSInput input) : SV_TARGET
{
    float3 scene = sceneTexture.Sample(linearSampler, input.texcoord).rgb;
    float3 rays = godRayTexture.Sample(linearSampler, input.texcoord).rgb;
    return float4(scene + rays, 1.0);
}

)HLSL";


// Cinematic post-processing: chromatic aberration + vignette + color grading
static const char* g_shaderCinematic = R"HLSL(

struct VSInput { float2 position : POSITION; float2 texcoord : TEXCOORD; };
struct PSInput { float4 position : SV_POSITION; float2 texcoord : TEXCOORD; };

Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer CinematicConstants : register(b0)
{
    float2 texelSize;
    float chromaAmount;        // strength of RGB split (e.g. 0.004)
    float vignetteStrength;    // how dark edges get (e.g. 0.8)
    float colorGradeIntensity; // teal/warm grading strength (e.g. 0.35)
    float saturation;          // 1.0 = normal, 1.15 = boosted
    float contrast;            // 1.0 = normal, 1.1 = boosted
    float pad;
};

PSInput VSPost(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.texcoord = input.texcoord;
    return output;
}

float4 PSCinematic(PSInput input) : SV_TARGET
{
    float2 uv = input.texcoord;
    float2 fromCenter = uv - 0.5;
    float distSq = dot(fromCenter, fromCenter);

    // --- Chromatic Aberration ---
    float caScale = distSq * chromaAmount;
    float3 color;
    color.r = sceneTexture.Sample(linearSampler, uv + fromCenter * caScale).r;
    color.g = sceneTexture.Sample(linearSampler, uv).g;
    color.b = sceneTexture.Sample(linearSampler, uv - fromCenter * caScale).b;

    // --- Contrast ---
    color = (color - 0.5) * contrast + 0.5;
    color = max(color, 0.0);

    // --- Saturation ---
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(float3(luma, luma, luma), color, saturation);

    // --- Cinematic Color Grading ---
    // Teal shadows + warm orange highlights (blockbuster look)
    float3 shadowTint  = float3(-0.02, 0.04, 0.08);  // cool blue-teal
    float3 highlightTint = float3(0.06, 0.03, -0.02); // warm orange
    float3 grade = lerp(shadowTint, highlightTint, saturate(luma));
    color += grade * colorGradeIntensity;

    // --- Vignette ---
    // Smooth darkening toward edges
    float vignette = 1.0 - distSq * vignetteStrength;
    vignette = saturate(vignette * vignette); // sharper falloff
    color *= vignette;

    return float4(max(color, 0.0), 1.0);
}

)HLSL";


// Film grain: animated noise overlay
// Procedural lens flare: ghosts, halo, and anamorphic streak from sun position
// Volumetric explosion clouds: raymarched 3D noise spheres at explosion positions
static const char* g_shaderVolumetric = R"HLSL(

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

)HLSL";


static const char* g_shaderLensFlare = R"HLSL(

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

)HLSL";


static const char* g_shaderFilmGrain = R"HLSL(

struct VSInput { float2 position : POSITION; float2 texcoord : TEXCOORD; };
struct PSInput { float4 position : SV_POSITION; float2 texcoord : TEXCOORD; };

Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer FilmGrainConstants : register(b0)
{
    float2 texelSize;
    float grainIntensity; // 0.06 = subtle
    float time;
};

PSInput VSPost(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.texcoord = input.texcoord;
    return output;
}

// Hash-based pseudo-random noise
float GrainNoise(float2 co, float seed)
{
    return frac(sin(dot(co + seed, float2(12.9898, 78.233))) * 43758.5453);
}

float4 PSFilmGrain(PSInput input) : SV_TARGET
{
    float3 color = sceneTexture.Sample(linearSampler, input.texcoord).rgb;

    // Animated grain: different pattern each frame
    float grain = GrainNoise(input.texcoord * 500.0, time) * 2.0 - 1.0;

    // Apply grain more to midtones, less to pure black/white
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    float midtoneMask = 1.0 - abs(luma - 0.5) * 2.0;
    midtoneMask = saturate(midtoneMask + 0.3);

    color += grain * grainIntensity * midtoneMask;
    return float4(max(color, 0.0), 1.0);
}

)HLSL";


// Sharpen: contrast-adaptive sharpening (simplified RCAS)
static const char* g_shaderSharpen = R"HLSL(

struct VSInput { float2 position : POSITION; float2 texcoord : TEXCOORD; };
struct PSInput { float4 position : SV_POSITION; float2 texcoord : TEXCOORD; };

Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);
SamplerState pointSampler  : register(s1);

cbuffer SharpenConstants : register(b0)
{
    float2 texelSize;
    float sharpenAmount; // 0.5 = moderate, 1.0 = strong
    float pad;
};

PSInput VSPost(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.texcoord = input.texcoord;
    return output;
}

float4 PSSharpen(PSInput input) : SV_TARGET
{
    float2 uv = input.texcoord;
    float3 c = sceneTexture.SampleLevel(pointSampler, uv, 0).rgb;
    float3 n = sceneTexture.SampleLevel(pointSampler, uv + float2(0, -texelSize.y), 0).rgb;
    float3 s = sceneTexture.SampleLevel(pointSampler, uv + float2(0,  texelSize.y), 0).rgb;
    float3 e = sceneTexture.SampleLevel(pointSampler, uv + float2( texelSize.x, 0), 0).rgb;
    float3 w = sceneTexture.SampleLevel(pointSampler, uv + float2(-texelSize.x, 0), 0).rgb;

    // Luma-based adaptive sharpening kernel
    float3 lw = float3(0.299, 0.587, 0.114);
    float lC = dot(c, lw), lN = dot(n, lw), lS = dot(s, lw);
    float lE = dot(e, lw), lW = dot(w, lw);

    float mn = min(min(lN, lS), min(lE, lW));
    float mx = max(max(lN, lS), max(lE, lW));
    float contrast = mx - mn;

    // Scale sharpening by local contrast (sharpen flat areas less)
    float adaptiveAmount = sharpenAmount * saturate(1.0 - contrast * 3.0);
    float kernel = -adaptiveAmount * 0.25;

    float3 result = c * (1.0 - 4.0 * kernel) + (n + s + e + w) * kernel;

    // Clamp to neighbor range to prevent ringing
    float3 minC = min(min(n, s), min(e, w));
    float3 maxC = max(max(n, s), max(e, w));
    result = clamp(result, minC, maxC);

    return float4(result, 1.0);
}

)HLSL";


// Tilt shift: blur top and bottom of screen for fake depth-of-field
static const char* g_shaderTiltShift = R"HLSL(

struct VSInput { float2 position : POSITION; float2 texcoord : TEXCOORD; };
struct PSInput { float4 position : SV_POSITION; float2 texcoord : TEXCOORD; };

Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer TiltShiftConstants : register(b0)
{
    float2 texelSize;
    float focusCenter;  // Y position of focus band (0.45 = slightly above center)
    float focusWidth;   // half-width of sharp band (0.15)
    float blurStrength; // max blur radius in texels (3.0)
    float3 pad;
};

PSInput VSPost(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.texcoord = input.texcoord;
    return output;
}

float4 PSTiltShift(PSInput input) : SV_TARGET
{
    float2 uv = input.texcoord;

    // Compute blur amount based on distance from focus band
    float distFromFocus = abs(uv.y - focusCenter);
    float blur = saturate((distFromFocus - focusWidth) / (0.3));
    blur *= blur; // quadratic falloff for smooth transition
    blur *= blurStrength;

    if (blur < 0.5)
        return sceneTexture.Sample(linearSampler, uv);

    // 13-tap disc blur (6 samples + center)
    float2 blurSize = texelSize * blur;
    float3 color = sceneTexture.Sample(linearSampler, uv).rgb * 0.2;
    color += sceneTexture.Sample(linearSampler, uv + float2( 1.0,  0.0) * blurSize).rgb * 0.133;
    color += sceneTexture.Sample(linearSampler, uv + float2(-1.0,  0.0) * blurSize).rgb * 0.133;
    color += sceneTexture.Sample(linearSampler, uv + float2( 0.0,  1.0) * blurSize).rgb * 0.133;
    color += sceneTexture.Sample(linearSampler, uv + float2( 0.0, -1.0) * blurSize).rgb * 0.133;
    color += sceneTexture.Sample(linearSampler, uv + float2( 0.7,  0.7) * blurSize).rgb * 0.067;
    color += sceneTexture.Sample(linearSampler, uv + float2(-0.7,  0.7) * blurSize).rgb * 0.067;
    color += sceneTexture.Sample(linearSampler, uv + float2( 0.7, -0.7) * blurSize).rgb * 0.067;
    color += sceneTexture.Sample(linearSampler, uv + float2(-0.7, -0.7) * blurSize).rgb * 0.067;

    return float4(color, 1.0);
}

)HLSL";


static const char* g_shader2D = R"HLSL(

struct VSInput
{
    float2 position : POSITION;
    float2 texcoord : TEXCOORD;
    float4 color    : COLOR;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
    float4 color    : COLOR;
};

cbuffer ScreenConstants : register(b0)
{
    float2 screenSize;
    float2 padding;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    // Convert pixel coordinates to NDC: [0,width] -> [-1,1], [0,height] -> [1,-1]
    output.position.x = (input.position.x / screenSize.x) * 2.0 - 1.0;
    output.position.y = 1.0 - (input.position.y / screenSize.y) * 2.0;
    output.position.z = 0.0;
    output.position.w = 1.0;
    output.texcoord = input.texcoord;
    output.color = input.color;
    return output;
}

float4 PSMainColor(PSInput input) : SV_TARGET
{
    return input.color;
}

Texture2D uiTexture : register(t0);
SamplerState linearSampler : register(s0);

float4 PSMainTextured(PSInput input) : SV_TARGET
{
    return uiTexture.Sample(linearSampler, input.texcoord) * input.color;
}

float4 PSMainGrayscale(PSInput input) : SV_TARGET
{
    float4 texColor = uiTexture.Sample(linearSampler, input.texcoord);
    float gray = dot(texColor.rgb, float3(0.299, 0.587, 0.114));
    return float4(gray, gray, gray, texColor.a) * input.color;
}

)HLSL";

// Shadow map depth-only shader: renders geometry from light perspective
static const char* g_shaderShadowDepth = R"HLSL(

cbuffer FrameConstants : register(b0)
{
    row_major float4x4 viewProjection;
};

cbuffer ObjectConstants : register(b1)
{
    row_major float4x4 world;
    float4 objectColor;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 color    : COLOR;
};

float4 VSShadowDepth(VSInput input) : SV_POSITION
{
    float4 worldPos = mul(float4(input.position, 1.0), world);
    return mul(worldPos, viewProjection);
}

// Empty PS — depth-only rendering, no color output needed
void PSShadowDepth() { }

)HLSL";

// ============================================================================
// Debug draw shader: minimal unlit colored line/triangle renderer used by
// Render::Debug. Reuses FrameConstants slot b0 — only samples the
// viewProjection matrix, ignores everything else in the cbuffer. Vertex
// format is just position + RGBA color (no normals, no UVs).
// ============================================================================
static const char* g_shaderDebug = R"HLSL(

cbuffer FrameConstants : register(b0)
{
    row_major float4x4 viewProjection;
    // (rest of FrameConstants ignored — we only sample the matrix.
    // The renderer is responsible for keeping the cbuffer layout
    // matching the C++ struct so this slice stays valid.)
};

struct VSInput
{
    float3 position : POSITION;
    float4 color    : COLOR;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = mul(float4(input.position, 1.0), viewProjection);
    output.color = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}

)HLSL";

} // namespace Render
