R"HLSL(

cbuffer FrameConstants : register(b0)
{
    row_major float4x4 viewProjection;
    float4 cameraPos;
};

cbuffer SnowConstants : register(b2)
{
    float4 snowGrid;     // x = emitterSpacing, y = quadHalfSize, z = snowCeiling, w = heightTraveled
    float4 snowAnim;     // x = amplitude, y = freqScaleX, z = freqScaleY, w = boxDimensions
    int4   snowOrigin;   // x = cubeOriginX, y = cubeOriginY, z = gridWidth, w = unused
    float4 snowCamRight; // xyz = camera right vector
    float4 snowCamUp;    // xyz = camera up vector
    float4 snowCamFwd;   // xyz = camera forward vector, w = cull distance
};

StructuredBuffer<float> noiseTable : register(t3);
Texture2D snowTexture : register(t0);
SamplerState linearSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

PSInput VSSnow(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    PSInput output;

    // Grid coordinates from instanceID
    int gridW = snowOrigin.z;
    int localY = (int)instanceID / gridW;
    int localX = (int)instanceID - localY * gridW;
    int gx = snowOrigin.x + localX;
    int gy = snowOrigin.y + localY;

    // Noise lookup (64x64 wrapping)
    int noiseX = (gx + 100000) & 63;
    int noiseY = (gy + 100000) & 63;
    float startH = noiseTable[noiseX + noiseY * 64];

    // Current height: ceiling minus animated offset (wraps around boxDimensions)
    float h0 = snowGrid.z - fmod(snowGrid.w + startH, snowAnim.w);

    // World position
    float3 pos;
    pos.x = (float)gx * snowGrid.x;
    pos.y = (float)gy * snowGrid.x;
    pos.z = h0;

    // Sine-wave lateral offset
    pos.x += snowAnim.x * sin(h0 * snowAnim.y + (float)gx);
    pos.y += snowAnim.x * sin(h0 * snowAnim.z + (float)gy);

    // Frustum culling: skip particles behind camera (output degenerate triangle)
    float3 toParticle = pos - cameraPos.xyz;
    float dotFwd = dot(toParticle, snowCamFwd.xyz);
    if (dotFwd < -snowCamFwd.w)
    {
        output.position = float4(0, 0, -1, 1);
        output.texcoord = float2(0, 0);
        return output;
    }

    // Billboard corners from vertexID (6 verts = 2 triangles)
    float2 cornerOffsets[6] = {
        float2(-1,  1), float2(-1, -1), float2( 1, -1),
        float2(-1,  1), float2( 1, -1), float2( 1,  1)
    };
    float2 cornerUVs[6] = {
        float2(0, 0), float2(0, 1), float2(1, 1),
        float2(0, 0), float2(1, 1), float2(1, 0)
    };

    float2 off = cornerOffsets[vertexID] * snowGrid.y;
    pos += snowCamRight.xyz * off.x + snowCamUp.xyz * off.y;

    output.position = mul(float4(pos, 1.0), viewProjection);
    output.texcoord = cornerUVs[vertexID];
    return output;
}

float4 PSSnow(PSInput input) : SV_TARGET
{
    float4 tex = snowTexture.Sample(linearSampler, input.texcoord);
    return tex * float4(1, 1, 1, 0.7);
}

)HLSL"
