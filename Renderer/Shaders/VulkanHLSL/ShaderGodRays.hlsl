
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
