R"HLSL(

cbuffer FrameConstants : register(b0)
{
    row_major float4x4 viewProjection;
};

cbuffer ObjectConstants : register(b1)
{
    row_major float4x4 world;
    float4 objectColor;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 color    : COLOR;
};

float4 VSShadowDepth(VSInput input) : SV_POSITION
{
    float4 worldPos = mul(float4(input.position, 1.0), world);
    return mul(worldPos, viewProjection);
}

// Empty PS — depth-only rendering, no color output needed
void PSShadowDepth() { }

// Flat-alpha PS for silhouette baking: writes black with full alpha
// wherever geometry rasterizes. The target texture is cleared to
// (0,0,0,0) beforehand, so the result is a 2D mask of the model's
// projected silhouette — exactly what the decal shader needs as a
// shadow texture. Reused with the same VSShadowDepth entry point.
float4 PSShadowSilhouette() : SV_TARGET
{
    return float4(0, 0, 0, 1);
}

)HLSL"
