
cbuffer FrameConstants : register(b0)
{
    row_major float4x4 viewProjection;
    float4 cameraPos;
};

cbuffer DecalConstants : register(b2)
{
    // xy = world-to-UV scale: 1.0 / (extent * MAP_XY_FACTOR)
    // zw = world-to-UV offset: borderSize / extent
    float4 hmTransform;
    // x = z bias above terrain, y-w = unused
    float4 hmParams;
};

struct DecalInstance
{
    float2 position;
    float2 offset;
    float2 size;
    float  angle;
    uint   color; // ABGR packed
};

StructuredBuffer<DecalInstance> g_Decals : register(t3);
Texture2D<float> g_Heightmap : register(t4);
SamplerState g_Sampler : register(s0);
Texture2D g_DecalTexture : register(t0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
    float4 color    : COLOR;
};

float4 UnpackABGR(uint packed)
{
    return float4(
        float(packed & 0xFF) / 255.0,
        float((packed >> 8) & 0xFF) / 255.0,
        float((packed >> 16) & 0xFF) / 255.0,
        float((packed >> 24) & 0xFF) / 255.0
    );
}

PSInput VSDecal(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    PSInput output;
    DecalInstance inst = g_Decals[instanceID];

    // 6 vertices = 2 triangles for a quad
    float2 corners[6] = {
        float2(-0.5, -0.5), float2( 0.5, -0.5), float2( 0.5,  0.5),
        float2(-0.5, -0.5), float2( 0.5,  0.5), float2(-0.5,  0.5)
    };
    float2 uvs[6] = {
        float2(0, 0), float2(1, 0), float2(1, 1),
        float2(0, 0), float2(1, 1), float2(0, 1)
    };

    float2 local = corners[vertexID];

    // Rotate and scale to world space
    float cosA = cos(inst.angle);
    float sinA = sin(inst.angle);
    float2 rotated = float2(
        local.x * cosA - local.y * sinA,
        local.x * sinA + local.y * cosA
    );
    float2 worldXY = rotated * inst.size + inst.position + inst.offset;

    // Sample heightmap for terrain-conforming Z
    float2 hmUV = worldXY * hmTransform.xy + hmTransform.zw;
    float worldZ = g_Heightmap.SampleLevel(g_Sampler, hmUV, 0) + hmParams.x;

    output.position = mul(float4(worldXY, worldZ, 1.0), viewProjection);
    output.texcoord = uvs[vertexID];
    output.color = UnpackABGR(inst.color);
    return output;
}

float4 PSDecal(PSInput input) : SV_TARGET
{
    float4 tex = g_DecalTexture.Sample(g_Sampler, input.texcoord);
    float4 result = tex * input.color;
    // hmParams.y = blend mode: 1+ = alpha/additive (faction logos, selection circles).
    // Some decal textures lack an alpha channel (24-bit TGA / DXT1), so all pixels
    // have alpha == 1 and the black background becomes opaque. For those textures,
    // derive alpha from brightness so black = transparent, colored = opaque.
    if (hmParams.y > 0.5 && tex.a > 0.99)
        result.a = max(result.r, max(result.g, result.b));
    return result;
}
