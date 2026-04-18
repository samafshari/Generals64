R"HLSL(

struct VSInput
{
    float2 position : POSITION;
    float2 texcoord : TEXCOORD;
    float4 color    : COLOR;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
    float4 color    : COLOR;
};

cbuffer ScreenConstants : register(b0)
{
    float2 screenSize;
    float2 padding;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    // Convert pixel coordinates to NDC: [0,width] -> [-1,1], [0,height] -> [1,-1]
    output.position.x = (input.position.x / screenSize.x) * 2.0 - 1.0;
    output.position.y = 1.0 - (input.position.y / screenSize.y) * 2.0;
    output.position.z = 0.0;
    output.position.w = 1.0;
    output.texcoord = input.texcoord;
    output.color = input.color;
    return output;
}

float4 PSMainColor(PSInput input) : SV_TARGET
{
    return input.color;
}

Texture2D uiTexture : register(t0);
SamplerState linearSampler : register(s0);

float4 PSMainTextured(PSInput input) : SV_TARGET
{
    return uiTexture.Sample(linearSampler, input.texcoord) * input.color;
}

float4 PSMainGrayscale(PSInput input) : SV_TARGET
{
    float4 texColor = uiTexture.Sample(linearSampler, input.texcoord);
    float gray = dot(texColor.rgb, float3(0.299, 0.587, 0.114));
    return float4(gray, gray, gray, texColor.a) * input.color;
}

)HLSL"
