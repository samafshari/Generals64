#pragma once

// Embedded HLSL compute shader source for AI offloading.
// Compiled at startup via D3DCompile with cs_5_0 profile.

namespace Render
{

// Compute shader for batch closest-point-on-path computation.
// Each thread processes one unit's path query independently.
static const char* g_csPathPoint = R"HLSL(

struct PathSegment
{
    float2 startPos;
    float2 direction;    // normalized direction to next node
    float  segmentLen;   // 2D distance to next node
    uint   layer;
    float2 _pad;         // pad to 32 bytes
};

struct UnitPathQuery
{
    float2 unitPos;
    uint   pathOffset;   // index into segment array
    uint   pathCount;    // number of segments in this unit's path
};

struct PathResult
{
    float2 goalPos;
    float  distAlongPath;  // remaining distance to end
    uint   closestSegIdx;
};

StructuredBuffer<PathSegment>   g_segments : register(t0);
StructuredBuffer<UnitPathQuery> g_queries  : register(t1);
RWStructuredBuffer<PathResult>  g_results  : register(u0);

[numthreads(64, 1, 1)]
void CSPathPoint(uint3 id : SV_DispatchThreadID)
{
    UnitPathQuery q = g_queries[id.x];
    if (q.pathCount == 0)
        return;

    float closestDistSqr = 99999999.0;
    float2 closestPoint = float2(0, 0);
    float totalLen = 0;
    float lenToClosest = 0;
    uint closestIdx = 0;

    for (uint i = 0; i < q.pathCount; i++)
    {
        PathSegment seg = g_segments[q.pathOffset + i];

        // Project unit position onto segment
        float2 toPos = q.unitPos - seg.startPos;
        float along = dot(toPos, seg.direction);
        along = clamp(along, 0.0, seg.segmentLen);

        // Compute point on segment
        float2 pt = seg.startPos + along * seg.direction;
        float2 offset = q.unitPos - pt;
        float distSqr = dot(offset, offset);

        if (distSqr < closestDistSqr)
        {
            closestDistSqr = distSqr;
            closestPoint = pt;
            lenToClosest = totalLen + along;
            closestIdx = i;
        }
        totalLen += seg.segmentLen;
    }

    // Compute goal position: target next node after closest segment
    uint nextIdx = closestIdx + 1;
    if (nextIdx < q.pathCount)
    {
        PathSegment nextSeg = g_segments[q.pathOffset + nextIdx];
        float2 nextEnd = nextSeg.startPos + nextSeg.direction * nextSeg.segmentLen;
        // Blend between closest point and next node based on how far along the segment we are
        PathSegment closeSeg = g_segments[q.pathOffset + closestIdx];
        float2 toPos = q.unitPos - closeSeg.startPos;
        float along = clamp(dot(toPos, closeSeg.direction), 0.0, closeSeg.segmentLen);
        float t = (closeSeg.segmentLen > 0.001) ? along / closeSeg.segmentLen : 0.0;
        // If more than halfway through segment, target next node
        if (t > 0.5)
            closestPoint = nextEnd;
    }

    g_results[id.x].goalPos = closestPoint;
    g_results[id.x].distAlongPath = totalLen - lenToClosest;
    g_results[id.x].closestSegIdx = closestIdx;
}

)HLSL";

// Compute shader for batch angle/distance calculations for locomotor.
// Pre-computes atan2, sqrt, and angle diff for all moving units.
static const char* g_csLocoMath = R"HLSL(

struct LocoInput
{
    float2 unitPos;
    float2 goalPos;
    float  unitAngle;
    float  maxTurnRate;
    float2 _pad;
};

struct LocoOutput
{
    float desiredAngle;
    float angleDiff;
    float distance;
    float distSqr;
};

StructuredBuffer<LocoInput>   g_inputs  : register(t0);
RWStructuredBuffer<LocoOutput> g_outputs : register(u0);

static const float PI = 3.14159265358979323846;

// Normalize angle to [-PI, PI]
float normalizeAngle(float a)
{
    while (a > PI) a -= 2.0 * PI;
    while (a < -PI) a += 2.0 * PI;
    return a;
}

[numthreads(64, 1, 1)]
void CSLocoMath(uint3 id : SV_DispatchThreadID)
{
    LocoInput inp = g_inputs[id.x];
    float2 delta = inp.goalPos - inp.unitPos;

    float dSqr = dot(delta, delta);
    float dist = sqrt(dSqr);
    float desired = atan2(delta.y, delta.x);
    float diff = normalizeAngle(desired - inp.unitAngle);

    g_outputs[id.x].desiredAngle = desired;
    g_outputs[id.x].angleDiff = diff;
    g_outputs[id.x].distance = dist;
    g_outputs[id.x].distSqr = dSqr;
}

)HLSL";

} // namespace Render
