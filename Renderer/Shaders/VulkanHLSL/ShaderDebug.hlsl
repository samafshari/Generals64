
cbuffer FrameConstants : register(b0)
{
    row_major float4x4 viewProjection;
    // (rest of FrameConstants ignored — we only sample the matrix.
    // The renderer is responsible for keeping the cbuffer layout
    // matching the C++ struct so this slice stays valid.)
};

struct VSInput
{
    float3 position : POSITION;
    float4 color    : COLOR;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = mul(float4(input.position, 1.0), viewProjection);
    output.color = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}

