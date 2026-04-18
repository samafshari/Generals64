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

)HLSL"
