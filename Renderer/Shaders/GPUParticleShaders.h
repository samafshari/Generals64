// GPU Particle System Shaders — volumetric smoke trails for missiles and planes
// Compute shader simulates particles, VS/PS render as soft camera-facing billboards.

#pragma once

namespace Render {

// Compute shader: update particle positions, ages, sizes each frame
static const char* g_shaderGPUParticleUpdate = R"HLSL(

struct Particle
{
    float3 position;
    float  age;
    float3 velocity;
    float  lifetime;
    float  size;
    float  startSize;
    float  growRate;
    float  alpha;
    float3 color;
    uint   alive;
};

RWStructuredBuffer<Particle> particles : register(u0);

cbuffer UpdateConstants : register(b0)
{
    float deltaTime;
    float turbulenceStrength;
    float dragCoeff;
    float gravity;
    uint  maxParticles;
    float3 windDirection;
};

// Simple noise for turbulence
float hash(float n) { return frac(sin(n) * 43758.5453); }
float noise3D(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = i.x + i.y * 157.0 + i.z * 113.0;
    return lerp(lerp(lerp(hash(n), hash(n+1.0), f.x),
                     lerp(hash(n+157.0), hash(n+158.0), f.x), f.y),
                lerp(lerp(hash(n+113.0), hash(n+114.0), f.x),
                     lerp(hash(n+270.0), hash(n+271.0), f.x), f.y), f.z);
}

[numthreads(256, 1, 1)]
void CSUpdate(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= maxParticles) return;

    Particle p = particles[id.x];
    if (!p.alive) return;

    p.age += deltaTime;
    if (p.age >= p.lifetime)
    {
        p.alive = 0;
        p.alpha = 0;
        particles[id.x] = p;
        return;
    }

    float t = p.age / p.lifetime; // 0..1

    // Drag
    p.velocity *= (1.0 - dragCoeff * deltaTime);

    // Gravity (slight upward drift for hot exhaust)
    p.velocity.z += gravity * deltaTime;

    // Wind
    p.velocity += windDirection * deltaTime * 0.5;

    // Turbulence — noise-based displacement for organic motion
    float3 noisePos = p.position * 0.05 + p.age * 0.3;
    float3 turb;
    turb.x = noise3D(noisePos) - 0.5;
    turb.y = noise3D(noisePos + 100.0) - 0.5;
    turb.z = noise3D(noisePos + 200.0) - 0.5;
    p.velocity += turb * turbulenceStrength * deltaTime;

    // Integrate position
    p.position += p.velocity * deltaTime;

    // Size stays at startSize (born at peak)
    p.size = p.startSize;

    // Alpha: no fade-in, only fade-out. Born at full opacity, dissipates over lifetime.
    float fadeOut = saturate((1.0 - t) * 2.0);
    p.alpha = fadeOut * fadeOut;

    // Color: hot white → warm gray → cool transparent
    float cooling = t * t; // faster cooling at start
    p.color = lerp(float3(1.0, 0.95, 0.85), float3(0.6, 0.6, 0.65), cooling);

    particles[id.x] = p;
}

)HLSL";

// Vertex + Pixel shaders: render particles as camera-facing soft billboards
static const char* g_shaderGPUParticleRender = R"HLSL(

struct Particle
{
    float3 position;
    float  age;
    float3 velocity;
    float  lifetime;
    float  size;
    float  startSize;
    float  growRate;
    float  alpha;
    float3 color;
    uint   alive;
};

StructuredBuffer<Particle> particles : register(t0);
Texture2D depthTexture : register(t1);
Texture2D noiseTexture : register(t2);
SamplerState linearSampler : register(s0);

cbuffer FrameConstants : register(b0)
{
    row_major float4x4 viewProjection;
    float4 cameraPos;
};

cbuffer RenderConstants : register(b1)
{
    float4 cameraRight;   // camera right vector in world space
    float4 cameraUp;      // camera up vector in world space
    float2 screenSize;
    float  nearPlane;
    float  farPlane;
    uint   maxParticles;
    float  softDepthScale; // how quickly particles fade near surfaces
    float  noiseScale;     // noise texture UV scale
    float  opacityMultiplier;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float  alpha    : TEXCOORD1;
    float3 color    : TEXCOORD2;
    float  depth    : TEXCOORD3; // linear depth for soft particle
};

// VS: expand particle into camera-facing quad (4 verts per particle via SV_VertexID)
VSOutput VSParticle(uint vertexID : SV_VertexID)
{
    VSOutput o = (VSOutput)0;

    uint particleIdx = vertexID / 4;
    uint cornerIdx = vertexID % 4;

    if (particleIdx >= maxParticles)
    {
        o.position = float4(0, 0, -1, 1); // clip
        return o;
    }

    Particle p = particles[particleIdx];
    if (!p.alive || p.alpha < 0.001)
    {
        o.position = float4(0, 0, -1, 1);
        return o;
    }

    // Billboard corner offsets
    float2 corners[4] = {
        float2(-1, -1),
        float2( 1, -1),
        float2( 1,  1),
        float2(-1,  1)
    };
    float2 uv[4] = {
        float2(0, 1),
        float2(1, 1),
        float2(1, 0),
        float2(0, 0)
    };

    float2 corner = corners[cornerIdx];
    float halfSize = p.size * 0.5;

    float3 worldPos = p.position
        + cameraRight.xyz * corner.x * halfSize
        + cameraUp.xyz * corner.y * halfSize;

    o.position = mul(float4(worldPos, 1.0), viewProjection);
    o.texcoord = uv[cornerIdx];
    o.alpha = p.alpha;
    o.color = p.color;

    // Linear depth for soft particles
    float3 toCamera = worldPos - cameraPos.xyz;
    o.depth = length(toCamera);

    return o;
}

float4 PSParticle(VSOutput input) : SV_TARGET
{
    // Circular soft falloff from center
    float2 centered = input.texcoord * 2.0 - 1.0;
    float dist = length(centered);
    if (dist > 1.0) discard;

    // Smooth circular mask with soft edge
    float mask = 1.0 - dist;
    mask = mask * mask; // quadratic falloff for softer edges

    // Noise-based opacity variation for volumetric look
    float2 noiseUV = input.texcoord * noiseScale + input.depth * 0.001;
    float noiseSample = noiseTexture.Sample(linearSampler, noiseUV).r;
    float volumetric = lerp(0.7, 1.0, noiseSample); // subtle variation

    float alpha = input.alpha * mask * volumetric * opacityMultiplier;

    // Premultiplied alpha for correct blending
    return float4(input.color * alpha, alpha);
}

)HLSL";

} // namespace Render
