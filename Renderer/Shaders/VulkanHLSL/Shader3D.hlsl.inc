R"HLSL(

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

    // Building shadows — per-caster rotated rectangles darken terrain
    // fragments whose XY lands inside. buildingShadowCount.x = live count.
    // Each rect: xy = world center, z = halfWidth, w = halfHeight.
    // Each rot:  xy = (cos(yaw), sin(yaw)).
    float4 buildingShadowCount;
    float4 buildingShadowRect[256];
    float4 buildingShadowRot[256];
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

// Procedural gradient noise for cloud shadows (Perlin-style).
// Gradient noise is C2-continuous across cell boundaries, so the grid
// never shows as visible creases — the value-noise version produced
// sharp V-shaped edges at high zoom because cubic smoothstep is only
// C1 and sin-hashed corners produced large jumps between neighbours.
float2 CloudHash2(float2 i)
{
    float2 h = float2(dot(i, float2(127.1, 311.7)),
                      dot(i, float2(269.5,  183.3)));
    return frac(sin(h) * 43758.5453) * 2.0 - 1.0; // unit-ish gradient
}

float CloudNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    // Quintic smoothstep (C2-continuous): 6f^5 - 15f^4 + 10f^3
    float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    float a = dot(CloudHash2(i),                  f);
    float b = dot(CloudHash2(i + float2(1, 0)),   f - float2(1, 0));
    float c = dot(CloudHash2(i + float2(0, 1)),   f - float2(0, 1));
    float d = dot(CloudHash2(i + float2(1, 1)),   f - float2(1, 1));

    // Gradient noise is in ~[-0.7, 0.7]; remap to [0, 1].
    float n = lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
    return n * 0.7 + 0.5;
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

    // Three octaves of gradient noise (FBM). Base frequency is lower than
    // the old value-noise version so cloud blobs are larger than a single
    // tank — prevents individual units sitting on a sharp shadow edge.
    float n  = CloudNoise(cloudUV * 3.0)  * 0.55;
    n       += CloudNoise(cloudUV * 6.0)  * 0.30;
    n       += CloudNoise(cloudUV * 12.0) * 0.15;

    // Remap: 0.55..1.0 range so shadows are subtle (not pitch black)
    float shadow = saturate(n * 0.45 + 0.55);

    return color * shadow;
}

// Building shadow rects are currently disabled — the shadow-pass work in
// commit 8da4e13 produced a broken visual (darkened cloud shadows too),
// so this function short-circuits to a no-op until the pipeline is re-landed.
// cbuffer fields (buildingShadowCount / Rect / Rot) remain so FrameConstants
// offsets don't shift for other shaders.
float3 ApplyBuildingShadow(float3 color, float3 worldPos)
{
    return color;
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



// GPU shadow map disabled — return full-lit. The shadow-pass bring-up in
// commit 8da4e13 darkened cloud shadows too and produced visibly wrong
// output; this no-op restores pre-commit visuals until the pipeline is
// re-landed correctly. Signature / cbuffer bindings preserved so all
// PSMain* call sites compile unchanged.
float ComputeShadow(float3 worldPos)
{
    return 1.0;
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
)HLSL"
R"HLSL(
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
    finalColor.rgb = ApplyBuildingShadow(finalColor.rgb, input.worldPos);
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
    finalColor.rgb = ApplyBuildingShadow(finalColor.rgb, input.worldPos);
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
    finalColor.rgb = ApplyBuildingShadow(finalColor.rgb, input.worldPos);
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
    finalColor.rgb = ApplyBuildingShadow(finalColor.rgb, input.worldPos);
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
)HLSL"
R"HLSL(
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

)HLSL"
