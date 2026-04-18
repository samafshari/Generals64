
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
