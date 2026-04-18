R"HLSL(

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

)HLSL"
