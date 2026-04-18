R"HLSL(
// Shadow-volume extrusion pass. Vertices are pre-extruded on the CPU and
// pushed as a plain XYZ stream in world space via the W3DBufferManager VB
// pool; this VS only feeds them through the view-projection matrix. The PS
// is never executed because the two stencil passes both disable color
// writes via the blend state — but D3D11 still requires a bound PS, so we
// ship a trivial passthrough.

cbuffer FrameConstants : register(b0)
{
    row_major float4x4 viewProjection;
    // rest of FrameConstants ignored
};

cbuffer ObjectConstants : register(b1)
{
    row_major float4x4 world;
    float4 objectColor;
    float4 shaderParams;
};

struct VSInput
{
    float3 position : POSITION;
};

struct PSInput
{
    float4 position : SV_POSITION;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    float4 worldPos = mul(float4(input.position, 1.0), world);
    output.position = mul(worldPos, viewProjection);
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(0, 0, 0, 0);
}
)HLSL"
