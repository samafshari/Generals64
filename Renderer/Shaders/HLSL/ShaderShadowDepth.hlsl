
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
