// Render::Debug — implementation.
//
// Buffer model: a single std::vector<DebugVertex> accumulator that's
// reset each frame. Lines are stored as consecutive vertex pairs
// (matching the LineList topology), so push_back is just two appends
// per line and there's no index buffer to manage. The whole vector
// gets uploaded with one VertexBuffer::Update at flush time and drawn
// with a single Device::Draw call.
//
// Why not std::deque or a fixed-size array: vector with reserve() up
// to capacity is the cheapest option, and capacity is hard-capped at
// kMaxLineVertices to bound memory at ~1 MB worst case.

#include "DebugDraw.h"

#include "Core/Device.h"
#include "Core/Buffer.h"
#include "Core/Shader.h"

#include <vector>
#include <cmath>

namespace Render
{
namespace Debug
{

// ============================================================================
// Vertex format and queue state
// ============================================================================
struct DebugVertex
{
    Float3   position;
    uint32_t color;  // RGBA, memory order R,G,B,A
};

namespace
{
    // The accumulator. Cleared every frame after flush.
    std::vector<DebugVertex> g_vertices;
    bool g_initialized = false;

    // Pi for circle / sphere math (no engine math header dependency).
    constexpr float kPi    = 3.14159265358979323846f;
    constexpr float kTwoPi = 6.28318530717958647692f;

    // Push a line as two vertices. Drops silently when the per-frame
    // capacity is exceeded — debug draw is best-effort and must never
    // block the rendering path.
    inline void PushLine(const Float3& a, const Float3& b, uint32_t color)
    {
        if (g_vertices.size() + 2 > kMaxLineVertices)
            return;
        g_vertices.push_back({ a, color });
        g_vertices.push_back({ b, color });
    }

    // Rotate (x,y) about the origin by yaw radians (Z-axis rotation).
    // Used by OBB / OBBCorners since RTS-style objects rotate around Z.
    inline void RotateXY(float& x, float& y, float yaw)
    {
        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        const float nx = x * c - y * s;
        const float ny = x * s + y * c;
        x = nx;
        y = ny;
    }
}

// ============================================================================
// Init / Shutdown / Clear
// ============================================================================
bool Init(Device& /*device*/)
{
    g_vertices.reserve(4096);
    g_initialized = true;
    return true;
}

void Shutdown()
{
    g_vertices.clear();
    g_vertices.shrink_to_fit();
    g_initialized = false;
}

void Clear()
{
    g_vertices.clear();
}

size_t QueuedLineCount()
{
    return g_vertices.size() / 2;
}

// ============================================================================
// Primitives
// ============================================================================
void Line(const Float3& a, const Float3& b, uint32_t color)
{
    PushLine(a, b, color);
}

void Cross(const Float3& center, float size, uint32_t color)
{
    PushLine({center.x - size, center.y, center.z},
             {center.x + size, center.y, center.z}, color);
    PushLine({center.x, center.y - size, center.z},
             {center.x, center.y + size, center.z}, color);
    PushLine({center.x, center.y, center.z - size},
             {center.x, center.y, center.z + size}, color);
}

void AABB(const Float3& mn, const Float3& mx, uint32_t color)
{
    // 8 corners of the box
    const Float3 c000{mn.x, mn.y, mn.z};
    const Float3 c100{mx.x, mn.y, mn.z};
    const Float3 c010{mn.x, mx.y, mn.z};
    const Float3 c110{mx.x, mx.y, mn.z};
    const Float3 c001{mn.x, mn.y, mx.z};
    const Float3 c101{mx.x, mn.y, mx.z};
    const Float3 c011{mn.x, mx.y, mx.z};
    const Float3 c111{mx.x, mx.y, mx.z};

    // Bottom (z = min)
    PushLine(c000, c100, color);
    PushLine(c100, c110, color);
    PushLine(c110, c010, color);
    PushLine(c010, c000, color);

    // Top (z = max)
    PushLine(c001, c101, color);
    PushLine(c101, c111, color);
    PushLine(c111, c011, color);
    PushLine(c011, c001, color);

    // Vertical edges
    PushLine(c000, c001, color);
    PushLine(c100, c101, color);
    PushLine(c110, c111, color);
    PushLine(c010, c011, color);
}

void OBB(const Float3& center, const Float3& he, float yawZ, uint32_t color)
{
    // 8 corners in local box space, then rotate XY by yaw, then translate
    Float3 corners[8] = {
        { -he.x, -he.y, -he.z }, {  he.x, -he.y, -he.z },
        {  he.x,  he.y, -he.z }, { -he.x,  he.y, -he.z },
        { -he.x, -he.y,  he.z }, {  he.x, -he.y,  he.z },
        {  he.x,  he.y,  he.z }, { -he.x,  he.y,  he.z },
    };
    for (int i = 0; i < 8; ++i)
    {
        RotateXY(corners[i].x, corners[i].y, yawZ);
        corners[i].x += center.x;
        corners[i].y += center.y;
        corners[i].z += center.z;
    }
    // Bottom rectangle
    PushLine(corners[0], corners[1], color);
    PushLine(corners[1], corners[2], color);
    PushLine(corners[2], corners[3], color);
    PushLine(corners[3], corners[0], color);
    // Top rectangle
    PushLine(corners[4], corners[5], color);
    PushLine(corners[5], corners[6], color);
    PushLine(corners[6], corners[7], color);
    PushLine(corners[7], corners[4], color);
    // Vertical edges
    PushLine(corners[0], corners[4], color);
    PushLine(corners[1], corners[5], color);
    PushLine(corners[2], corners[6], color);
    PushLine(corners[3], corners[7], color);
}

void OBBCorners(const Float3& center, const Float3& he, float yawZ,
                uint32_t color, float bracketSize)
{
    // Build the 8 corners exactly like OBB
    Float3 corners[8] = {
        { -he.x, -he.y, -he.z }, {  he.x, -he.y, -he.z },
        {  he.x,  he.y, -he.z }, { -he.x,  he.y, -he.z },
        { -he.x, -he.y,  he.z }, {  he.x, -he.y,  he.z },
        {  he.x,  he.y,  he.z }, { -he.x,  he.y,  he.z },
    };
    for (int i = 0; i < 8; ++i)
    {
        RotateXY(corners[i].x, corners[i].y, yawZ);
        corners[i].x += center.x;
        corners[i].y += center.y;
        corners[i].z += center.z;
    }

    // For each corner, draw 3 short bracket arms along the edges that
    // meet at it. The arms are bracketSize fraction of each edge.
    // Edge graph: each corner index → its three neighbours.
    static const int kEdge[8][3] = {
        { 1, 3, 4 }, { 0, 2, 5 }, { 1, 3, 6 }, { 0, 2, 7 },
        { 0, 5, 7 }, { 1, 4, 6 }, { 2, 5, 7 }, { 3, 4, 6 },
    };

    auto LerpToward = [](const Float3& from, const Float3& to, float t) -> Float3 {
        return { from.x + (to.x - from.x) * t,
                 from.y + (to.y - from.y) * t,
                 from.z + (to.z - from.z) * t };
    };

    for (int i = 0; i < 8; ++i)
    {
        for (int n = 0; n < 3; ++n)
        {
            const Float3 stub = LerpToward(corners[i], corners[kEdge[i][n]], bracketSize);
            PushLine(corners[i], stub, color);
        }
    }
}

void Circle(const Float3& center, const Float3& normal, float radius,
            uint32_t color, int segments)
{
    if (segments < 3) segments = 3;

    // Build two basis vectors perpendicular to the normal so we can
    // sweep an angle around in the plane defined by `normal`.
    // Choose the cross with whichever world axis is least parallel.
    Float3 n = normal;
    const float nl = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
    if (nl < 1e-6f)
        return;
    n.x /= nl; n.y /= nl; n.z /= nl;

    Float3 axis = (std::fabs(n.z) < 0.9f) ? Float3{0,0,1} : Float3{1,0,0};
    Float3 u = {
        n.y * axis.z - n.z * axis.y,
        n.z * axis.x - n.x * axis.z,
        n.x * axis.y - n.y * axis.x
    };
    const float ul = std::sqrt(u.x*u.x + u.y*u.y + u.z*u.z);
    u.x /= ul; u.y /= ul; u.z /= ul;
    Float3 v = {
        n.y * u.z - n.z * u.y,
        n.z * u.x - n.x * u.z,
        n.x * u.y - n.y * u.x
    };

    Float3 prev = {
        center.x + u.x * radius,
        center.y + u.y * radius,
        center.z + u.z * radius
    };
    for (int i = 1; i <= segments; ++i)
    {
        const float t = (float)i / (float)segments * kTwoPi;
        const float c = std::cos(t);
        const float s = std::sin(t);
        Float3 next = {
            center.x + (u.x * c + v.x * s) * radius,
            center.y + (u.y * c + v.y * s) * radius,
            center.z + (u.z * c + v.z * s) * radius
        };
        PushLine(prev, next, color);
        prev = next;
    }
}

void GroundCircle(const Float3& center, float radius, uint32_t color, int segments)
{
    Circle(center, {0, 0, 1}, radius, color, segments);
}

void Sphere(const Float3& center, float radius, uint32_t color, int segments)
{
    // 3 great circles + a few latitude rings. Total ~ 4 * segments lines.
    Circle(center, {1, 0, 0}, radius, color, segments);
    Circle(center, {0, 1, 0}, radius, color, segments);
    Circle(center, {0, 0, 1}, radius, color, segments);
}

void Arrow(const Float3& start, const Float3& end, uint32_t color)
{
    // Shaft
    PushLine(start, end, color);

    // Compute direction and length so we can build a head proportional
    // to the arrow's screen length.
    Float3 dir = { end.x - start.x, end.y - start.y, end.z - start.z };
    const float len = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
    if (len < 1e-4f)
        return;
    dir.x /= len; dir.y /= len; dir.z /= len;

    // Pick an "up-ish" axis to build the head triangle plane.
    const Float3 worldUp = (std::fabs(dir.z) < 0.95f) ? Float3{0,0,1} : Float3{0,1,0};
    Float3 right = {
        dir.y * worldUp.z - dir.z * worldUp.y,
        dir.z * worldUp.x - dir.x * worldUp.z,
        dir.x * worldUp.y - dir.y * worldUp.x
    };
    const float rl = std::sqrt(right.x*right.x + right.y*right.y + right.z*right.z);
    if (rl < 1e-6f)
        return;
    right.x /= rl; right.y /= rl; right.z /= rl;

    const float headLen = len * 0.18f;
    const float headRad = len * 0.07f;
    const Float3 base = {
        end.x - dir.x * headLen,
        end.y - dir.y * headLen,
        end.z - dir.z * headLen
    };
    const Float3 left = {
        base.x + right.x * headRad,
        base.y + right.y * headRad,
        base.z + right.z * headRad
    };
    const Float3 rightP = {
        base.x - right.x * headRad,
        base.y - right.y * headRad,
        base.z - right.z * headRad
    };
    PushLine(end, left,  color);
    PushLine(end, rightP, color);
}

void AxisGizmo(const Float3& center, float size)
{
    PushLine(center, {center.x + size, center.y, center.z}, kRed);
    PushLine(center, {center.x, center.y + size, center.z}, kGreen);
    PushLine(center, {center.x, center.y, center.z + size}, kBlue);
}

// ============================================================================
// Flush — upload + draw
// ============================================================================
void Flush(Device& device,
           Shader& shader,
           VertexBuffer& vb,
           RasterizerState& raster,
           BlendState& blend,
           DepthStencilState& depthRO)
{
    if (g_vertices.empty())
        return;

    // Upload everything in one pass. The vertex buffer is dynamic
    // and was created with capacity = kMaxLineVertices, so we can
    // memcpy directly without reallocating.
    const uint32_t byteCount = (uint32_t)(g_vertices.size() * sizeof(DebugVertex));
    vb.Update(device, g_vertices.data(), byteCount);

    // Bind pipeline state. Caller must have already updated and bound
    // the FrameConstants cbuffer at b0 — we read viewProjection from
    // it inside the debug shader.
    shader.Bind(device);
    vb.Bind(device);
    raster.Bind(device);
    blend.Bind(device);
    depthRO.Bind(device);

    device.SetTopology(Topology::LineList);
    device.Draw((uint32_t)g_vertices.size(), 0);

    // Don't clear here — Renderer::EndFrame controls clear timing so
    // panels can choose to keep some primitives across frames in
    // future (Phase 3 will need persistent debug shapes for selection
    // history). For Phase 2.5 the renderer calls Clear() after Flush.
}

} // namespace Debug
} // namespace Render
