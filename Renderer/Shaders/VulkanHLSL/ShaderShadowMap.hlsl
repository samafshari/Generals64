// Depth-only pass for sun shadow mapping.
//
// Called once per frame from W3DDisplay::draw, BEFORE the main scene.
// Renderer::BeginShadowPass installs the sun's orthographic view-projection
// into FrameConstants.viewProjection (the normal scene's VP is saved on a
// stack and restored at EndShadowPass). The scene geometry is then re-issued
// using this trivial VS + PS pair writing only to the shadow depth target.
//
// Two VS entry points: one for Vertex3D (mesh) and one for Vertex3DMasked
// (terrain) — identical behaviour, different input layouts. The PS is a
// no-op; the depth pass only needs depth writes.

cbuffer FrameConstants : register(b0)
{
    row_major float4x4 viewProjection; // During shadow pass, this is sunVP
    float4 cameraPos;
    float4 ambientColor;
    float4 lightDirections[3];
    float4 lightColors[3];
    float4 lightingOptions;
    float4 pointLightPositions[4];
    float4 pointLightColors[4];
    float4 shroudParams;
    float4 atmosphereParams;
    row_major float4x4 sunViewProjection;
    float4 shadowParams;
};

cbuffer ObjectConstants : register(b1)
{
    row_major float4x4 world;
    float4 objectColor;
    float4 shaderParams;
};

struct VSInputMesh
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 color    : COLOR;
};

struct VSInputTerrain
{
    float3 position  : POSITION;
    float3 normal    : NORMAL;
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
    float4 color     : COLOR;
};

struct PSInput
{
    float4 position : SV_POSITION;
};

PSInput VSShadowMesh(VSInputMesh input)
{
    PSInput o;
    float4 worldPos = mul(float4(input.position, 1.0), world);
    o.position = mul(worldPos, viewProjection);
    return o;
}

PSInput VSShadowTerrain(VSInputTerrain input)
{
    PSInput o;
    float4 worldPos = mul(float4(input.position, 1.0), world);
    o.position = mul(worldPos, viewProjection);
    return o;
}

// PS is bound but writes nothing — depth-only RT has no color attachment.
// D3D11 still requires a PS when rasterizing; the "return 0" is discarded.
float4 PSShadow(PSInput input) : SV_TARGET
{
    return float4(0, 0, 0, 0);
}
