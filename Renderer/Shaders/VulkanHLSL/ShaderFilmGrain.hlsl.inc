R"HLSL(

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

)HLSL"
