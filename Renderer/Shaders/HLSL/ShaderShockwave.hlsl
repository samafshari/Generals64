
struct VSInput { float2 position : POSITION; float2 texcoord : TEXCOORD; };
struct PSInput { float4 position : SV_POSITION; float2 texcoord : TEXCOORD; };

Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer ShockwaveConstants : register(b0)
{
    float4 shockwaves[8]; // xy = screen UV, z = phase (0..1.5), w = intensity (1..0)
    float2 texelSize;
    float time;
    float shockwaveCount;
};

PSInput VSPost(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.texcoord = input.texcoord;
    return output;
}

float4 PSShockwave(PSInput input) : SV_TARGET
{
    float2 uv = input.texcoord;
    float2 totalOffset = float2(0, 0);
    int count = (int)shockwaveCount;

    for (int i = 0; i < 8; i++)
    {
        if (i >= count) break;
        float2 center = shockwaves[i].xy;
        float phase = shockwaves[i].z;
        float intensity = shockwaves[i].w;
        if (intensity < 0.01) continue;

        float2 toPixel = uv - center;
        // Correct for aspect ratio so rings are circular
        float aspect = texelSize.y / texelSize.x;
        float2 corrected = float2(toPixel.x, toPixel.y * aspect);
        float dist = length(corrected);

        // Micro shockwave: barely-there distortion ripple
        float ringRadius = phase * 0.04;
        float ringWidth = 0.004 + phase * 0.002;
        float ring = exp(-pow((dist - ringRadius) / ringWidth, 2.0));
        ring *= intensity;

        float2 dir = toPixel / max(length(toPixel), 0.0001);
        totalOffset += dir * ring * 0.002;
    }

    return sceneTexture.Sample(linearSampler, uv + totalOffset);
}
