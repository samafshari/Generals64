
struct VSInput
{
    float2 position : POSITION;
    float2 texcoord : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

Texture2D srcTexture : register(t0);
SamplerState linearSampler : register(s0);
SamplerState pointSampler  : register(s1);

cbuffer FSRConstants : register(b0)
{
    float2 srcTexelSize;   // 1.0 / source texture dimensions
    float  sharpness;      // RCAS: 0 = max sharpening, 2 = none
    float  pad;
};

PSInput VSPost(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.texcoord = input.texcoord;
    return output;
}

// ── Pass 1: EASU — Bicubic Catmull-Rom Upsampling ──
// Uses a 4x4 Catmull-Rom kernel for high-quality upsampling.
// Significantly sharper than bilinear with no ringing artifacts.
// Separable: apply 1D Catmull-Rom weights in X then Y.
float4 PSEASU(PSInput input) : SV_TARGET
{
    float2 uv = input.texcoord;
    float2 srcRes = 1.0 / srcTexelSize;

    // Position in source texel space
    float2 pos = uv * srcRes - 0.5;
    float2 f = frac(pos);
    float2 pos0 = (floor(pos) + 0.5) * srcTexelSize; // center of texel [0,0]

    // Catmull-Rom weights for 4 taps: t is fractional position (0..1)
    // w(-1) = -0.5t^3 + t^2 - 0.5t
    // w( 0) =  1.5t^3 - 2.5t^2 + 1
    // w( 1) = -1.5t^3 + 2t^2 + 0.5t
    // w( 2) =  0.5t^3 - 0.5t^2

    float4 wx, wy;
    {
        float t = f.x;
        float t2 = t * t;
        float t3 = t2 * t;
        wx = float4(
            -0.5*t3 + t2 - 0.5*t,
             1.5*t3 - 2.5*t2 + 1.0,
            -1.5*t3 + 2.0*t2 + 0.5*t,
             0.5*t3 - 0.5*t2
        );
    }
    {
        float t = f.y;
        float t2 = t * t;
        float t3 = t2 * t;
        wy = float4(
            -0.5*t3 + t2 - 0.5*t,
             1.5*t3 - 2.5*t2 + 1.0,
            -1.5*t3 + 2.0*t2 + 0.5*t,
             0.5*t3 - 0.5*t2
        );
    }

    // Sample 4x4 grid using linear sampler (clamp handles edges)
    float3 result = float3(0, 0, 0);
    for (int row = -1; row <= 2; row++)
    {
        float3 rowSum = float3(0, 0, 0);
        for (int col = -1; col <= 2; col++)
        {
            float2 sampleUV = pos0 + float2(col, row) * srcTexelSize;
            // Clamp to valid UV range to avoid wrap artifacts at edges
            sampleUV = clamp(sampleUV, srcTexelSize * 0.5, 1.0 - srcTexelSize * 0.5);
            float3 s = srcTexture.SampleLevel(pointSampler, sampleUV, 0).rgb;
            rowSum += s * wx[col + 1];
        }
        result += rowSum * wy[row + 1];
    }

    return float4(result, 1.0);
}

// ── Pass 2: RCAS — Robust Contrast Adaptive Sharpening ──
// Reads from the EASU output (now at display resolution) and sharpens.
// srcTexelSize must be updated to 1.0 / display resolution for this pass.
float4 PSRCAS(PSInput input) : SV_TARGET
{
    float2 uv = input.texcoord;
    float2 ts = srcTexelSize; // now = 1/display res

    float3 c = srcTexture.SampleLevel(pointSampler, uv, 0).rgb;
    float3 n = srcTexture.SampleLevel(pointSampler, uv + float2(0, -ts.y), 0).rgb;
    float3 s = srcTexture.SampleLevel(pointSampler, uv + float2(0,  ts.y), 0).rgb;
    float3 e = srcTexture.SampleLevel(pointSampler, uv + float2( ts.x, 0), 0).rgb;
    float3 w = srcTexture.SampleLevel(pointSampler, uv + float2(-ts.x, 0), 0).rgb;

    float3 lw = float3(0.299, 0.587, 0.114);
    float lC = dot(c, lw), lN = dot(n, lw), lS = dot(s, lw);
    float lE = dot(e, lw), lW = dot(w, lw);

    float mn = min(min(lN, lS), min(lE, lW));
    float mx = max(max(lN, lS), max(lE, lW));

    float sharpenAmt = sqrt(min(mn, 1.0 - mx) / max(mx, 0.04));
    float kernel = -(1.0 / lerp(8.0, 5.0, saturate(sharpenAmt - sharpness)));

    float3 result = (n + s + e + w) * kernel + c * (1.0 - 4.0 * kernel);

    // Clamp to neighborhood to prevent ringing
    float3 minC = min(min(n, s), min(e, w));
    float3 maxC = max(max(n, s), max(e, w));
    result = clamp(result, minC, maxC);

    return float4(result, 1.0);
}
