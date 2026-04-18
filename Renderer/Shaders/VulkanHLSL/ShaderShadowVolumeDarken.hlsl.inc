R"HLSL(
// Full-screen shadow darkening pass. Fired after the stencil-volume pass
// has incremented/decremented the stencil buffer per-pixel. A fullscreen
// triangle is generated from SV_VertexID (no VB bound). The PS outputs a
// flat shadow color; the stencil test (NOT_EQUAL 0) gates the write so
// only pixels covered by a net-positive stencil count receive the tint.
// Blend state (multiplicative: DEST * SRC) multiplies the backbuffer
// against shadowColor.rgb, darkening it.

cbuffer DarkenConstants : register(b0)
{
    float4 shadowColor; // rgb = shadow tint (<1 darkens), a = unused
};

struct PSInput
{
    float4 position : SV_POSITION;
};

PSInput VSMain(uint vertexID : SV_VertexID)
{
    PSInput output;
    // Big fullscreen tri — three corners cover the viewport.
    // ID 0 -> (-1,-1), 1 -> (-1, 3), 2 -> ( 3,-1)
    float2 p;
    p.x = (vertexID == 2) ?  3.0 : -1.0;
    p.y = (vertexID == 1) ?  3.0 : -1.0;
    output.position = float4(p, 0.0, 1.0);
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return shadowColor;
}
)HLSL"
