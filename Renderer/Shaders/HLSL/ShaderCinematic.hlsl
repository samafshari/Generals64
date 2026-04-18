
struct VSInput { float2 position : POSITION; float2 texcoord : TEXCOORD; };
struct PSInput { float4 position : SV_POSITION; float2 texcoord : TEXCOORD; };

Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer CinematicConstants : register(b0)
{
    float2 texelSize;
    float chromaAmount;        // strength of RGB split (e.g. 0.004)
    float vignetteStrength;    // how dark edges get (e.g. 0.8)
    float colorGradeIntensity; // teal/warm grading strength (e.g. 0.35)
    float saturation;          // 1.0 = normal, 1.15 = boosted
    float contrast;            // 1.0 = normal, 1.1 = boosted
    float pad;
};

PSInput VSPost(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.texcoord = input.texcoord;
    return output;
}

float4 PSCinematic(PSInput input) : SV_TARGET
{
    float2 uv = input.texcoord;
    float2 fromCenter = uv - 0.5;
    float distSq = dot(fromCenter, fromCenter);

    // --- Chromatic Aberration ---
    float caScale = distSq * chromaAmount;
    float3 color;
    color.r = sceneTexture.Sample(linearSampler, uv + fromCenter * caScale).r;
    color.g = sceneTexture.Sample(linearSampler, uv).g;
    color.b = sceneTexture.Sample(linearSampler, uv - fromCenter * caScale).b;

    // --- Contrast ---
    color = (color - 0.5) * contrast + 0.5;
    color = max(color, 0.0);

    // --- Saturation ---
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(float3(luma, luma, luma), color, saturation);

    // --- Cinematic Color Grading ---
    // Teal shadows + warm orange highlights (blockbuster look)
    float3 shadowTint  = float3(-0.02, 0.04, 0.08);  // cool blue-teal
    float3 highlightTint = float3(0.06, 0.03, -0.02); // warm orange
    float3 grade = lerp(shadowTint, highlightTint, saturate(luma));
    color += grade * colorGradeIntensity;

    // --- Vignette ---
    // Smooth darkening toward edges
    float vignette = 1.0 - distSq * vignetteStrength;
    vignette = saturate(vignette * vignette); // sharper falloff
    color *= vignette;

    return float4(max(color, 0.0), 1.0);
}
