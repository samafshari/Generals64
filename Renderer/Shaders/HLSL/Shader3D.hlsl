
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

    // Generals' lighting INI tunes ambient + 3 directional lights to often
    // sum well above 1.0 (e.g. afternoon: 0.22 ambient + 1.0 sun + 0.23 sky
    // + 0.12 bounce ≈ 1.57). The original DX8 fixed-function pipeline
    // implicitly clamped per-vertex diffuse color to [0,1] before the
    // D3DTOP_MODULATE texture stage, so the artist-tuned values never blew
    // out textures. Without this saturate() the texture gets multiplied by
    // >1 and clips to white, producing the desaturated low-contrast look.
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

// GPU Shadow Map: 3x3 PCF sampling for soft directional shadows.
// shadowParams.x gates the whole path. shadowParams.w is a debug mode:
//   0 = normal PCF shadow, floor=0.25 (dramatic for easy validation)
//   1 = visualize raw shadow map depth as red gradient (unit tests the
//       caster pass — if terrain shows a red gradient, casters ARE
//       writing to the shadow map and the matrix chain is correct)
//   2 = force-darken everything inside the light frustum to 0.3 (unit
//       tests the receiver path — if terrain darkens uniformly, the
//       matrix maps valid UVs, just the comparison/sample is failing)
float ComputeShadow(float3 worldPos)
{
    if (shadowParams.x < 0.5) return 1.0;
    float4 shadowClip = mul(float4(worldPos, 1.0), shadowMapMatrix);
    float3 shadowUV = shadowClip.xyz;
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0) return 1.0;

    int debugMode = (int)shadowParams.w;
    if (debugMode == 1)
    {
        // Dump raw stored depth as red. Bright red = far caster depth,
        // dark red = near caster depth. No casters in UV region = 1.0 (far clear).
        float stored = shadowMapTexture.SampleLevel(linearSampler, shadowUV.xy, 0).r;
        return stored; // hijack — multiplied into finalColor.rgb
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

    float4 finalColor = texColor * input.color;
    finalColor.rgb *= ComputeLighting(input.worldPos, N);
    finalColor.rgb *= ComputeShadow(input.worldPos);
    finalColor.rgb = ApplySurfaceSpecular(finalColor.rgb, input.worldPos, N);
    finalColor.rgb = ApplyCloudShadow(finalColor.rgb, input.worldPos);
    finalColor.rgb = ApplyUnderwaterFade(finalColor.rgb, input.worldPos.z);
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

// GPU projected mesh decal (faction logos on buildings).
// Decal texture is grayscale art; player color arrives via input.color from
// the CPU at draw time. No material-diffuse path.
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

    float4 tex = diffuseTexture.Sample(linearSampler, decalUV.xy);
    float brightness   = max(tex.r, max(tex.g, tex.b));
    float isDarkOpaque = tex.a * (1.0 - brightness);
    float a = (1.0 - isDarkOpaque) * decalParams.y;
    clip(a - 0.01);

    float3 rgb = ApplyShroud(float3(0.0, 0.0, 0.0), input.worldPos);
    return float4(rgb, a);
}

// Construction-ghost shader. Lit pass with optional tint mix; output alpha
// fed by an alpha-blend pipeline state with depth-write disabled.
cbuffer GhostConstants : register(b5)
{
    float4 ghostTint;   // rgb = tint color, a = tint intensity (0..1)
    float4 ghostParams; // x = opacity, yzw = unused
};

float4 PSGhost(PSInput input) : SV_TARGET
{
    float4 texColor = diffuseTexture.Sample(linearSampler, input.texcoord);
    clip(texColor.a - 0.05);
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
//         shore foam) — opt-in via Inspector toggle "Enhanced Water"
//         (g_useEnhancedWater), default OFF.
float4 PSMainWaterBump(PSInput input) : SV_TARGET
{
    float timeMs = lightingOptions.y;
    float useEnhanced = atmosphereParams.w; // 0=classic, 1=enhanced

    // River flag in input.normal.x (0.01 = river, 0 = standing water).
    float isRiver = step(0.005, input.normal.x);
    float2 sampleUV = input.texcoord;
    if (useEnhanced > 0.5 && isRiver > 0.5)
        sampleUV.y += timeMs * 0.00006;
    float4 texColor = diffuseTexture.Sample(linearSampler, sampleUV);

    float4 finalColor = texColor * input.color;

    if (useEnhanced < 0.5)
    {
        // CLASSIC
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
        return finalColor;
    }

    // ENHANCED
    float2 bumpUV1 = input.texcoord * 4.0  + float2(timeMs *  0.00012,  timeMs *  0.00009);
    float2 bumpUV2 = input.texcoord * 7.0  + float2(timeMs * -0.00007,  timeMs *  0.00011);
    float4 b1 = bumpTexture.Sample(wrapSampler, bumpUV1);
    float4 b2 = bumpTexture.Sample(wrapSampler, bumpUV2);
    float3 bumpNormal = float3(
        ((b1.r + b2.r) - 1.0) * 0.45,
        ((b1.g + b2.g) - 1.0) * 0.45,
        1.0);
    float3 N = normalize(input.normal + bumpNormal);
    float3 lighting = ComputeLighting(input.worldPos, N);
    finalColor.rgb *= lighting;
    float3 V = normalize(cameraPos.xyz - input.worldPos);
    float NdotV = saturate(dot(N, V));
    float fresnel = pow(1.0 - NdotV, 5.0) * 0.85 + 0.05;
    float3 skyTint = float3(0.45, 0.55, 0.70);
    finalColor.rgb = lerp(finalColor.rgb, skyTint * lighting, fresnel * 0.35);
    float3 H = normalize(V + normalize(-lightDirections[0].xyz));
    float spec = pow(saturate(dot(N, H)), 96.0) * 0.55;
    finalColor.rgb += spec * lightColors[0].rgb;
    int2 screenCoord = int2(input.position.xy);
    float sceneDepth = depthTexture.Load(int3(screenCoord, 0)).r;
    float waterDepth = input.position.z;
    float depthDelta = sceneDepth - waterDepth;
    float foam = saturate(1.0 - depthDelta * 2000.0);
    foam = pow(foam, 2.0);
    float foamNoise = b1.b * 0.5 + b2.b * 0.5;
    finalColor.rgb += float3(0.85, 0.92, 1.0) * foam * (0.6 + foamNoise * 0.4);
    return finalColor;
}
