R"HLSL(

// Projected shadow / decal renderer.
//
// Faithful port of the DX8 _PresetMultiplicative / _PresetAlpha /
// _PresetAdditive shaders used by W3DProjectedShadowManager for flat
// texture decals stamped onto the heightmap. Vertices come pre-transformed
// to world space by queueDecal() (it walks the heightmap grid and writes
// one vertex per cell corner) so the VS only applies viewProjection.

cbuffer FrameConstants : register(b0)
{
    row_major float4x4 viewProjection;
    float4 cameraPos;
};

struct VSInput
{
    float3 position : POSITION;
    float4 color    : COLOR;
    float2 texcoord : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 texcoord : TEXCOORD;
};

Texture2D g_DecalTexture : register(t0);
SamplerState g_Sampler   : register(s0);

PSInput VSShadowDecal(VSInput input)
{
    PSInput o;
    o.position = mul(float4(input.position, 1.0), viewProjection);
    o.color    = input.color;
    o.texcoord = input.texcoord;
    return o;
}

float4 PSShadowDecal(PSInput input) : SV_TARGET
{
    float4 tex = g_DecalTexture.Sample(g_Sampler, input.texcoord);
    return tex * input.color;
}
)HLSL"
