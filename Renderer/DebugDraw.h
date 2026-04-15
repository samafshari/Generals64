// Render::Debug — immediate-mode debug primitive renderer.
//
// Code anywhere in the engine can queue lines, boxes, spheres, arrows
// in world space using these calls. The renderer flushes the entire
// queue once per frame at EndFrame time, before swap chain Present,
// using a dedicated unlit colored-line shader. Primitives are not
// persistent — they live for one frame only and the queue is cleared
// after each flush. Re-submit them every frame for continuous
// visualization (matching how Unity's Debug.DrawLine works).
//
// Coordinates are in world space. Colors are 32-bit RGBA in memory
// order (red in the low byte) which matches the engine's
// UByte4Norm vertex color convention.
//
// Pipeline state used:
//   - Vertex format: position (Float3) + color (uint32 RGBA)
//   - Topology: LineList
//   - Rasterizer: NoCull (lines have no winding)
//   - Blend: Alpha (so debug overlays can fade)
//   - Depth: test enabled, write disabled (lines respect occlusion
//     but don't corrupt the z-buffer for subsequent passes)
//
// Capacity is fixed at MaxLineVertices vertices per frame. Submitting
// more than that simply drops the overflow silently — debug draw is
// best-effort and should never crash gameplay.
#pragma once

#include "Math/RenderMath.h"
#include <cstdint>

namespace Render
{

class Device;
class Shader;
class VertexBuffer;
class RasterizerState;
class BlendState;
class DepthStencilState;

namespace Debug
{

// ----- Color helpers ---------------------------------------------------------
// Pack a 32-bit RGBA color in memory order R,G,B,A. Equivalent to ImGui's
// IM_COL32 macro and the engine's UByte4Norm vertex color layout.
constexpr uint32_t MakeRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
    return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
}

// Common preset colors. Saturated enough to read against bright HDR
// terrain and dark night maps without needing per-map tuning.
constexpr uint32_t kRed     = MakeRGBA(255,  60,  60);
constexpr uint32_t kGreen   = MakeRGBA( 60, 255, 100);
constexpr uint32_t kBlue    = MakeRGBA( 80, 140, 255);
constexpr uint32_t kYellow  = MakeRGBA(255, 220,  60);
constexpr uint32_t kCyan    = MakeRGBA( 60, 255, 255);
constexpr uint32_t kMagenta = MakeRGBA(255,  60, 220);
constexpr uint32_t kOrange  = MakeRGBA(255, 150,  40);
constexpr uint32_t kWhite   = MakeRGBA(255, 255, 255);
constexpr uint32_t kBlack   = MakeRGBA(  0,   0,   0);

// Maximum vertices per frame. Each line consumes 2 vertices, so this
// caps line count at MaxLineVertices/2 = 32K lines = ~5K wireframe
// boxes per frame, comfortably more than any debug visualization
// should need.
constexpr uint32_t kMaxLineVertices = 65536;

// ----- Queue API -------------------------------------------------------------
// All primitives persist for the current frame only. Coordinates are world
// space. Re-submit every frame for continuous visualization.

void Line(const Float3& a, const Float3& b, uint32_t color);

// Three-axis cross marker (six lines forming +X/+Y/+Z axes with the
// given half-size). Cheap and visible from any angle.
void Cross(const Float3& center, float size, uint32_t color);

// 12-edge wireframe axis-aligned bounding box from min/max corners.
void AABB(const Float3& minCorner, const Float3& maxCorner, uint32_t color);

// Same as AABB but the box is rotated about the world Z axis by yaw
// radians. Used for object selection in RTS games where ground units
// have a heading angle but stay flat on the terrain plane.
void OBB(const Float3& center, const Float3& halfExtents, float yawZ, uint32_t color);

// 4 RTS-style corner brackets at the top and bottom of an OBB. The
// bracketSize controls how long each bracket arm is as a fraction of
// the half-extent. Used for the inspector's selection visualization
// because it looks more "selection-y" than a full wireframe box.
void OBBCorners(const Float3& center, const Float3& halfExtents, float yawZ,
                uint32_t color, float bracketSize = 0.35f);

// Wireframe sphere built from three orthogonal great circles plus a
// few latitude rings. Cheap (~64 lines for default segments).
void Sphere(const Float3& center, float radius, uint32_t color, int segments = 16);

// Single great circle around the given normal axis. Used for ground
// circles (normal = Z) and arbitrary range visualizations.
void Circle(const Float3& center, const Float3& normal, float radius,
            uint32_t color, int segments = 32);

// Convenience: a circle on the XY plane at the given Z. Used for
// weapon range, vision range, selection ground rings.
void GroundCircle(const Float3& center, float radius, uint32_t color, int segments = 32);

// Arrow from start to end with a small triangular head at the tip.
// The head size is auto-scaled from the line length so short and
// long arrows both look proportional.
void Arrow(const Float3& start, const Float3& end, uint32_t color);

// 3-axis world coordinate gizmo (red X, green Y, blue Z) at the
// given center with each axis of length size.
void AxisGizmo(const Float3& center, float size = 50.0f);

// ----- Renderer integration --------------------------------------------------
// Called by Render::Renderer. Game/inspector code should not touch these.

bool Init(Device& device);
void Shutdown();
void Clear();
size_t QueuedLineCount();

// Flush the queue. The caller (Renderer) is responsible for binding
// the FrameConstants cbuffer at b0 before calling this — the debug
// shader samples viewProjection from there.
void Flush(Device& device,
           Shader& shader,
           VertexBuffer& vb,
           RasterizerState& raster,
           BlendState& blend,
           DepthStencilState& depthRO);

} // namespace Debug
} // namespace Render
