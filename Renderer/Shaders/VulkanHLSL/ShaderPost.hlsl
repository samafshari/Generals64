
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
