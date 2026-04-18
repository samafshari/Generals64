R"HLSL(

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

)HLSL"
