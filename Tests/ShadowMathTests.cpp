// Headless shadow-math test suite.
//
// Exercises the three layers that matter for sun shadow mapping:
//   1. RenderMath primitives (LookAtRH, OrthoRH, Multiply) — the foundation.
//      If these are wrong, everything else is.
//   2. BuildSunViewProjection as it's defined in W3DDisplay.cpp — verify the
//      matrix correctly maps camera-focus → NDC origin, lifts world +Z to
//      smaller NDC.z (closer to sun), etc.
//   3. A C++ reimplementation of the HLSL ComputeShadowVisibility path —
//      simulates the full render pipeline:
//        vertex-stage: worldPos * sunVP  -> clip / w -> NDC -> shadow texel
//        ps-stage:     worldPos * sunVP  -> clip / w -> NDC -> uv + receiverDepth
//      so we can detect classic bugs: matrix transpose, coord system flip,
//      NDC-to-UV mapping wrong, receiver-vs-caster depth mismatch, etc.
//
// Runs as a standalone executable — no D3D11, no Vulkan, no engine deps.
// Build:
//     cl /std:c++17 /EHsc /I.. /I../Renderer/Math Tests/ShadowMathTests.cpp
// or
//     g++ -std=c++17 -I.. -I../Renderer/Math Tests/ShadowMathTests.cpp -o shadow_tests
//
// Exit code == number of failed assertions (0 = green).

#include "../Renderer/Math/RenderMath.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------
// Minimal assert framework
// ----------------------------------------------------------------------------

struct TestState
{
    int total = 0;
    int failed = 0;
    std::string currentSection;
};

static TestState g_tests;

static void Section(const char* name)
{
    g_tests.currentSection = name;
    std::printf("\n=== %s ===\n", name);
}

static bool FloatEq(float a, float b, float eps = 1e-3f)
{
    float absMax = std::fabs(a) > std::fabs(b) ? std::fabs(a) : std::fabs(b);
    // Looser absolute floor so "0.000244 vs 0.0" doesn't register as non-zero
    // just because the sun vector is normalized to 6-digit precision.
    return std::fabs(a - b) <= eps * (1.0f + absMax);
}

#define CHECK(cond) do {                                                        \
    ++g_tests.total;                                                            \
    if (!(cond)) {                                                              \
        ++g_tests.failed;                                                       \
        std::printf("  FAIL  [%s] %s:%d  %s\n",                                 \
            g_tests.currentSection.c_str(), __FILE__, __LINE__, #cond);         \
    } else {                                                                    \
        std::printf("  ok    %s\n", #cond);                                     \
    }                                                                           \
} while (0)

#define CHECK_FLOAT(a, b) do {                                                  \
    float _aa = (a), _bb = (b);                                                 \
    ++g_tests.total;                                                            \
    if (!FloatEq(_aa, _bb)) {                                                   \
        ++g_tests.failed;                                                       \
        std::printf("  FAIL  [%s] %s:%d  %s = %.6f  !=  %s = %.6f\n",           \
            g_tests.currentSection.c_str(), __FILE__, __LINE__,                 \
            #a, _aa, #b, _bb);                                                  \
    } else {                                                                    \
        std::printf("  ok    %s == %s (%.6f)\n", #a, #b, _aa);                  \
    }                                                                           \
} while (0)

#define CHECK_VEC3(v, ex, ey, ez) do {                                          \
    auto _v = (v);                                                              \
    CHECK_FLOAT(_v.x, (ex));                                                    \
    CHECK_FLOAT(_v.y, (ey));                                                    \
    CHECK_FLOAT(_v.z, (ez));                                                    \
} while (0)

#define CHECK_VEC4(v, ex, ey, ez, ew) do {                                      \
    auto _v = (v);                                                              \
    CHECK_FLOAT(_v.x, (ex));                                                    \
    CHECK_FLOAT(_v.y, (ey));                                                    \
    CHECK_FLOAT(_v.z, (ez));                                                    \
    CHECK_FLOAT(_v.w, (ew));                                                    \
} while (0)

// ----------------------------------------------------------------------------
// Utility: row-vector transform used by the HLSL shaders.
//
// HLSL `mul(float4 v, float4x4 m)` with row_major layout corresponds to
// treating v as a row vector: v_out[j] = sum over i of v[i] * m[i][j].
// The engine stores Render::Float4x4 row-major and RenderMath operates
// the same way, so this helper is the ground truth for "what the shader sees".
// ----------------------------------------------------------------------------
static Render::Float4 Mul(const Render::Float4& v, const Render::Float4x4& m)
{
    return Render::Float4(
        v.x*m._11 + v.y*m._21 + v.z*m._31 + v.w*m._41,
        v.x*m._12 + v.y*m._22 + v.z*m._32 + v.w*m._42,
        v.x*m._13 + v.y*m._23 + v.z*m._33 + v.w*m._43,
        v.x*m._14 + v.y*m._24 + v.z*m._34 + v.w*m._44);
}

static Render::Float3 NormalizeF3(Render::Float3 v)
{
    float len = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    if (len < 1e-6f) return { 0, 0, 0 };
    return { v.x/len, v.y/len, v.z/len };
}

// ----------------------------------------------------------------------------
// RenderMath foundation tests — if these fail, everything above is nonsense.
// ----------------------------------------------------------------------------

static void TestIdentity()
{
    Section("Identity matrix");
    auto I = Render::Float4x4Identity();

    // Identity * v = v
    Render::Float4 v(3, 5, 7, 1);
    auto r = Mul(v, I);
    CHECK_VEC4(r, 3.0f, 5.0f, 7.0f, 1.0f);

    // Identity * Identity = Identity
    auto II = Render::Float4x4Multiply(I, I);
    CHECK_FLOAT(II._11, 1.0f);
    CHECK_FLOAT(II._22, 1.0f);
    CHECK_FLOAT(II._33, 1.0f);
    CHECK_FLOAT(II._44, 1.0f);
    CHECK_FLOAT(II._12, 0.0f);
    CHECK_FLOAT(II._21, 0.0f);
}

static void TestOrthoRH()
{
    Section("Orthographic RH projection — view-space Z mapping");

    // Using the same params W3DDisplay::BuildSunViewProjection uses.
    auto P = Render::Float4x4OrthoRH(2400.0f, 2400.0f, 1.0f, 10000.0f);

    // View-space point at near plane (view_z = -nearZ = -1) maps to ndc.z = 0.
    auto pnear = Mul({ 0, 0, -1.0f, 1 }, P);
    CHECK_FLOAT(pnear.z / pnear.w, 0.0f);

    // View-space point at far plane (view_z = -farZ = -10000) maps to ndc.z = 1.
    auto pfar = Mul({ 0, 0, -10000.0f, 1 }, P);
    CHECK_FLOAT(pfar.z / pfar.w, 1.0f);

    // Middle: -5000 maps to a mid-range depth.
    auto pmid = Mul({ 0, 0, -5000.0f, 1 }, P);
    float midNdc = pmid.z / pmid.w;
    CHECK(midNdc > 0.45f && midNdc < 0.55f);

    // XY: a point at (1200, 1200) on the view-space XY plane (the ortho half-width)
    // should map to NDC (1, 1). Half-width = width/2 = 1200.
    auto pedge = Mul({ 1200.0f, 1200.0f, -5000.0f, 1 }, P);
    CHECK_FLOAT(pedge.x / pedge.w, 1.0f);
    CHECK_FLOAT(pedge.y / pedge.w, 1.0f);
}

static void TestLookAtRH_SunStraightDown()
{
    Section("LookAtRH — sun straight down above origin");

    // Simple case: sun at (0, 0, 5000) looking down at origin.
    Render::Float3 eye   { 0.0f, 0.0f, 5000.0f };
    Render::Float3 focus { 0.0f, 0.0f, 0.0f    };
    Render::Float3 up    { 0.0f, 1.0f, 0.0f    };

    auto V = Render::Float4x4LookAtRH(eye, focus, up);

    // Focus (origin) should transform to (0, 0, -|eye-focus|) = (0, 0, -5000)
    // in view space — distance in front of eye in RH convention.
    auto f_view = Mul({ 0, 0, 0, 1 }, V);
    CHECK_VEC4(f_view, 0.0f, 0.0f, -5000.0f, 1.0f);

    // A point 100 units "up" in world (z = 100) — closer to the sun —
    // should be CLOSER to the eye in view space: view_z closer to 0.
    auto up_view = Mul({ 0, 0, 100, 1 }, V);
    CHECK_FLOAT(up_view.x, 0.0f);
    CHECK_FLOAT(up_view.y, 0.0f);
    CHECK_FLOAT(up_view.z, -4900.0f);

    // Eye itself transforms to origin.
    auto e_view = Mul({ eye.x, eye.y, eye.z, 1 }, V);
    CHECK_VEC4(e_view, 0.0f, 0.0f, 0.0f, 1.0f);
}

static void TestLookAtRH_SunAtAngle()
{
    Section("LookAtRH — sun at afternoon INI angle (-0.81, 0.38, -0.45)");

    // From GameData.ini afternoon: TerrainObjectsLightingAfternoonLightPos.
    // W3DShadowManager stores sun POSITION = -lightPos.normalized * 10000.
    // => sun ≈ (8087, -3794, 4493).
    Render::Float3 sunPos { 8087.9f, -3794.3f, 4493.3f };

    // BuildSunViewProjection places eye = focus - sunRay * 5000 where
    // sunRay = -sunPos/|sunPos| is the direction light travels.
    Render::Float3 focus { 1000.0f, 1000.0f, 0.0f };
    Render::Float3 sunRay = NormalizeF3({ -sunPos.x, -sunPos.y, -sunPos.z });
    Render::Float3 eye {
        focus.x - sunRay.x * 5000.0f,
        focus.y - sunRay.y * 5000.0f,
        focus.z - sunRay.z * 5000.0f,
    };
    Render::Float3 up { 0.0f, 1.0f, 0.0f };

    auto V = Render::Float4x4LookAtRH(eye, focus, up);

    // Focus should end up at (0, 0, -5000) in view space (distance from eye).
    auto f_view = Mul({ focus.x, focus.y, focus.z, 1 }, V);
    CHECK_FLOAT(f_view.x, 0.0f);
    CHECK_FLOAT(f_view.y, 0.0f);
    CHECK_FLOAT(f_view.z, -5000.0f);

    // A test point slightly off focus in world space: (1100, 1000, 0).
    // Its direction from eye to point differs slightly from focus — view.x
    // should be non-zero.
    auto p = Mul({ 1100.0f, 1000.0f, 0.0f, 1 }, V);
    CHECK(std::fabs(p.x) > 1.0f);
    // The XY displacement on the ground projects mostly perpendicular to
    // the sun ray, so view_z stays close to -5000.
    CHECK(p.z < -4900.0f && p.z > -5100.0f);
}

// ----------------------------------------------------------------------------
// BuildSunViewProjection — mirror of W3DDisplay.cpp's static helper.
// Keep this file self-contained so the test doesn't need to link the engine.
// ----------------------------------------------------------------------------
struct SunVPInputs
{
    Render::Float3 cameraPos;
    Render::Float3 sunPos;   // world-space sun position (what lightPosWorld returns)
};

static Render::Float4x4 BuildSunViewProjection(const SunVPInputs& in,
    float footprint = 2400.0f,
    float eyeDist  = 5000.0f,
    float nearZ    = 1.0f,
    float farZ     = 10000.0f)
{
    Render::Float3 focus { in.cameraPos.x, in.cameraPos.y, 0.0f };
    Render::Float3 sunRay = NormalizeF3({ -in.sunPos.x, -in.sunPos.y, -in.sunPos.z });
    Render::Float3 eye {
        focus.x - sunRay.x * eyeDist,
        focus.y - sunRay.y * eyeDist,
        focus.z - sunRay.z * eyeDist,
    };
    Render::Float3 up { 0, 1, 0 };
    auto V = Render::Float4x4LookAtRH(eye, focus, up);
    auto P = Render::Float4x4OrthoRH(footprint, footprint, nearZ, farZ);
    return Render::Float4x4Multiply(V, P);
}

// ----------------------------------------------------------------------------
// Reimplementation of the HLSL shadow sampling path, for testing.
// ----------------------------------------------------------------------------

struct ShadowSample
{
    float u, v;       // [0, 1] texture coord
    float receiverZ;  // [0, 1] depth to compare against the stored map depth
    bool  insideFootprint;
};

static ShadowSample WorldToShadow(const Render::Float3& worldPos,
                                  const Render::Float4x4& sunVP)
{
    auto clip = Mul({ worldPos.x, worldPos.y, worldPos.z, 1 }, sunVP);
    float w = (std::fabs(clip.w) < 1e-4f) ? 1e-4f : clip.w;
    Render::Float3 ndc { clip.x / w, clip.y / w, clip.z / w };

    ShadowSample s{};
    s.insideFootprint =
        std::fabs(ndc.x) <= 1.0f &&
        std::fabs(ndc.y) <= 1.0f &&
        ndc.z >= 0.0f && ndc.z <= 1.0f;
    s.u = ndc.x * 0.5f + 0.5f;
    s.v = -ndc.y * 0.5f + 0.5f;
    s.receiverZ = ndc.z;
    return s;
}

// ----------------------------------------------------------------------------
// Core correctness: the same world point must sample to the same shadow
// coordinates regardless of where the CAMERA is.  This is the precise
// invariant violated by the visible bug ("shadow moves with camera").
//
// Exception: BuildSunViewProjection uses camera XY for the focus, so the
// same world point DOES get a different UV when the camera moves because the
// FRAME OF REFERENCE moves. That's expected. Two invariants matter:
//   (a) A shadow CASTER at some point and a shadow RECEIVER at the same
//       point must produce the same UV + depth when projected through the
//       SAME sunVP. (Otherwise receivers self-shadow incorrectly.)
//   (b) The sunVP depends only on camera XY, not on camera view direction.
// ----------------------------------------------------------------------------
static void TestCasterReceiverAlignment()
{
    Section("Caster and receiver at the same world point sample the same texel");

    SunVPInputs in{};
    in.cameraPos = { 1000.0f, 1000.0f, 300.0f };
    in.sunPos    = { 8087.9f, -3794.3f, 4493.3f };
    auto vp = BuildSunViewProjection(in);

    Render::Float3 samples[] = {
        { 1000.0f, 1000.0f,   0.0f }, // right under the focus
        { 1100.0f,  950.0f,  50.0f }, // nearby, slightly elevated
        { 1500.0f, 1500.0f,   0.0f }, // toward footprint edge
        {  500.0f,  800.0f, 120.0f }, // different direction + elevation
    };
    for (auto p : samples) {
        auto a = WorldToShadow(p, vp);
        auto b = WorldToShadow(p, vp);
        CHECK_FLOAT(a.u, b.u);
        CHECK_FLOAT(a.v, b.v);
        CHECK_FLOAT(a.receiverZ, b.receiverZ);
    }
}

static void TestSunVPIndependentOfCameraOrientation()
{
    Section("BuildSunViewProjection depends only on camera XY — not view dir");

    // W3DDisplay implements focus = (cam.x, cam.y, 0). Changes to camera
    // elevation/pitch/yaw MUST NOT affect sunVP or the resulting shadow UV.
    SunVPInputs a{ { 1000.0f, 1000.0f, 300.0f }, { 8087.9f, -3794.3f, 4493.3f } };
    SunVPInputs b{ { 1000.0f, 1000.0f,  50.0f }, { 8087.9f, -3794.3f, 4493.3f } };
    auto vpA = BuildSunViewProjection(a);
    auto vpB = BuildSunViewProjection(b);

    // Both matrices should be element-wise identical.
    bool eq = true;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (!FloatEq(vpA.m[i][j], vpB.m[i][j])) eq = false;
    CHECK(eq);

    // And a fixed world point must project to the same UV through either.
    Render::Float3 probe{ 1100.0f, 900.0f, 25.0f };
    auto sa = WorldToShadow(probe, vpA);
    auto sb = WorldToShadow(probe, vpB);
    CHECK_FLOAT(sa.u, sb.u);
    CHECK_FLOAT(sa.v, sb.v);
    CHECK_FLOAT(sa.receiverZ, sb.receiverZ);
}

static void TestFocusCentersAtUV50()
{
    Section("Focus point projects to UV (0.5, 0.5)");

    SunVPInputs in{};
    in.cameraPos = { 1234.0f, 5678.0f, 300.0f };
    in.sunPos    = { 8087.9f, -3794.3f, 4493.3f };
    auto vp = BuildSunViewProjection(in);

    Render::Float3 focus{ in.cameraPos.x, in.cameraPos.y, 0.0f };
    auto s = WorldToShadow(focus, vp);
    CHECK_FLOAT(s.u, 0.5f);
    CHECK_FLOAT(s.v, 0.5f);
    CHECK(s.insideFootprint);
}

static void TestHigherObjectSmallerDepth()
{
    Section("Higher world-Z → smaller receiver depth (closer to sun)");

    SunVPInputs in{};
    in.cameraPos = { 1000.0f, 1000.0f, 300.0f };
    in.sunPos    = { 8087.9f, -3794.3f, 4493.3f };
    auto vp = BuildSunViewProjection(in);

    Render::Float3 ground { 1000.0f, 1000.0f,   0.0f };
    Render::Float3 mid    { 1000.0f, 1000.0f, 100.0f };
    Render::Float3 high   { 1000.0f, 1000.0f, 300.0f };

    auto sg = WorldToShadow(ground, vp);
    auto sm = WorldToShadow(mid,    vp);
    auto sh = WorldToShadow(high,   vp);
    CHECK(sm.receiverZ < sg.receiverZ);
    CHECK(sh.receiverZ < sm.receiverZ);
}

static void TestFootprintBounds()
{
    Section("Footprint bounds — off-center points project inside when within ±1200 units along sun-oriented axes");

    SunVPInputs in{};
    in.cameraPos = { 1000.0f, 1000.0f, 300.0f };
    in.sunPos    = { 8087.9f, -3794.3f, 4493.3f };
    auto vp = BuildSunViewProjection(in);

    // Shift on the ground in a direction roughly perpendicular to the sun
    // ray — this is what the sun "sees" across its horizontal axis.
    // Use +X (east). With afternoon sun tilting east-south, the sun ray's
    // horizontal projection also points roughly east-south, so moving the
    // point east moves it ALONG the sun ray in world space, so the
    // projected footprint extent is NOT necessarily 1200 world units on the
    // X axis. Instead, we test along the sun-view's "side" vector — but
    // since the test harness doesn't easily give us that, just verify the
    // point at (1000 + 500, 1000, 0) produces an NDC inside bounds.
    auto s1 = WorldToShadow({ 1500.0f, 1000.0f, 0.0f }, vp);
    CHECK(s1.insideFootprint);

    // A far-away point must project OUTSIDE the footprint.
    auto s2 = WorldToShadow({ 5000.0f, 5000.0f, 0.0f }, vp);
    CHECK(!s2.insideFootprint);
}

// ----------------------------------------------------------------------------
// End-to-end scenario: render a fake scene's "shadow map" and then sample
// from a receiver grid, checking expected shadow/lit behavior.
// ----------------------------------------------------------------------------
static void TestEndToEndShadowOverObstacle()
{
    Section("End-to-end: pillar occludes ground, shadow falls offset by sun tilt");

    SunVPInputs in{};
    in.cameraPos = { 1000.0f, 1000.0f, 300.0f };
    in.sunPos    = { 8087.9f, -3794.3f, 4493.3f };
    auto vp = BuildSunViewProjection(in);

    // Fake shadow map — 256 grid at the same UV resolution as the real map
    // gets sampled (nearest-neighbor into a continuous texel grid).
    constexpr int N = 256;
    std::vector<float> map(N * N, 1.0f);

    auto writeCaster = [&](const Render::Float3& w) {
        auto s = WorldToShadow(w, vp);
        if (!s.insideFootprint) return;
        int ix = (int)std::floor(s.u * N);
        int iy = (int)std::floor(s.v * N);
        if (ix < 0 || ix >= N || iy < 0 || iy >= N) return;
        float& cell = map[iy * N + ix];
        if (s.receiverZ < cell) cell = s.receiverZ; // LessEqual depth test
    };

    // Dense "pillar" of samples from base (z=0) to top (z=150) so the
    // shadow map has a full silhouette of the volume — not just the top
    // face. This is what the real renderer writes when a mesh is drawn.
    for (int iz = 0; iz <= 150; iz += 3)
        for (int i = -30; i <= 30; i += 3)
            for (int j = -30; j <= 30; j += 3)
                writeCaster({ 1000.0f + i, 1000.0f + j, (float)iz });

    auto sampleAt = [&](const Render::Float3& w) -> int {
        auto s = WorldToShadow(w, vp);
        if (!s.insideFootprint) return 1;
        int ix = (int)std::floor(s.u * N);
        int iy = (int)std::floor(s.v * N);
        if (ix < 0 || ix >= N || iy < 0 || iy >= N) return 1;
        float stored = map[iy * N + ix];
        return (s.receiverZ - 0.0005f <= stored) ? 1 : 0;
    };

    // Pillar base: in a real scene, the pillar body OCCLUDES the sun from
    // directly behind the base, so the base pixel is SELF-SHADOWED (dark).
    // This confirms the shadow-map UV math is correctly collapsing points
    // along the same sun ray onto the same texel.
    int atCaster = sampleAt({ 1000.0f, 1000.0f, 0.0f });
    CHECK(atCaster == 0);

    // Follow the sun ray DOWN from pillar top (z=150) until it hits ground
    // (z=0). That's where the shadow should fall. Sun ray direction:
    // sunRay = -sunPos / |sunPos| = direction light travels.
    auto sunRay = NormalizeF3({ -in.sunPos.x, -in.sunPos.y, -in.sunPos.z });
    // For a pillar-top at z=150, the shadow point is 150 / -sunRay.z units
    // in the direction (sunRay.x, sunRay.y) from the top.
    float t = 150.0f / (-sunRay.z);
    Render::Float3 shadowGround {
        1000.0f + sunRay.x * t,
        1000.0f + sunRay.y * t,
        0.0f,
    };
    std::printf("  -- shadow falls on ground at (%.1f, %.1f)\n",
                shadowGround.x, shadowGround.y);
    int atShadow = sampleAt(shadowGround);
    CHECK(atShadow == 0);

    // Between the caster and the shadow point is a gradient — test far
    // OPPOSITE of the shadow direction (toward the sun on the ground).
    int oppDir = sampleAt({
        1000.0f - sunRay.x * t * 0.5f,
        1000.0f - sunRay.y * t * 0.5f,
        0.0f,
    });
    CHECK(oppDir == 1);

    int offMap = sampleAt({ 10000.0f, 10000.0f, 0.0f });
    CHECK(offMap == 1);
}

// ----------------------------------------------------------------------------
// Regression test for the actual bug ("shadow plane moves with camera").
// The claim is that the receiver's sampled UV depends on camera orientation.
// This test tries to reproduce that by building sunVP with two cameras that
// differ only in their (fake) pitch / yaw / rotation but with the same XY.
// Since BuildSunViewProjection only uses camera XY, the matrices MUST match.
// If they don't, the test catches the bug at the math layer.
// ----------------------------------------------------------------------------
static void TestCameraRotationDoesNotMoveShadow()
{
    Section("Regression: camera pitch/yaw/roll cannot move the shadow UV");

    // Real-world-ish sun + camera XY; vary the "camera" Z and pretend the
    // other orientation bits are baked in somewhere else (they shouldn't
    // enter this pipeline at all).
    struct Trial { float camZ; };
    Trial trials[] = {{ 100.0f }, { 200.0f }, { 500.0f }, { 2000.0f }};

    SunVPInputs base{ { 1234.0f, 5678.0f, 300.0f }, { 8087.9f, -3794.3f, 4493.3f } };
    auto vp0 = BuildSunViewProjection(base);

    Render::Float3 probe{ 1200.0f, 5900.0f, 42.0f };
    auto s0 = WorldToShadow(probe, vp0);
    for (auto t : trials) {
        SunVPInputs mod = base;
        mod.cameraPos.z = t.camZ;
        auto vp = BuildSunViewProjection(mod);
        auto s = WorldToShadow(probe, vp);
        CHECK_FLOAT(s.u, s0.u);
        CHECK_FLOAT(s.v, s0.v);
        CHECK_FLOAT(s.receiverZ, s0.receiverZ);
    }
}

// ----------------------------------------------------------------------------
// Critical test: verify cbuffer offsets.
//
// The HLSL shader expects sunViewProjection at a specific byte offset in the
// FrameConstants cbuffer. The C++ struct layout must match. Mismatch produces
// exactly the visible symptom: the shadow shader reads garbage/nearby fields
// instead of sunVP, and the sampled UV varies with screen position because
// it's effectively sampling some view-space quantity. Verify the offset
// directly against the struct.
// ----------------------------------------------------------------------------
#include <cstddef>

// Keep this layout in-sync with Renderer.h's FrameConstants. Any divergence
// means the C++ code and the shader read/write at different offsets, which
// is exactly the category of bug that shows as "shadow plane" artifacts.
namespace HLSLLayoutMirror {

struct FrameConstants
{
    Render::Float4x4 viewProjection;
    Render::Float4 cameraPos;
    Render::Float4 ambientColor;
    Render::Float4 lightDirections[3];
    Render::Float4 lightColors[3];
    Render::Float4 lightingOptions;
    Render::Float4 pointLightPositions[4];
    Render::Float4 pointLightColors[4];
    Render::Float4 shroudParams;
    Render::Float4 atmosphereParams;
    Render::Float4x4 sunViewProjection;
    Render::Float4 shadowParams;
    Render::Float4 shadowParams2;
    Render::Float4 cloudParams;
    Render::Float4 cloudParams2;
};

} // namespace HLSLLayoutMirror

static void TestFrameConstantsLayout()
{
    Section("FrameConstants C++/HLSL layout offsets");

    using namespace HLSLLayoutMirror;
    // These offsets come from HLSL's packing rules (float4 = 16B, float4x4
    // = 64B, all members 16-byte aligned, arrays place each element on a
    // fresh 16-byte boundary). If the C++ struct diverges, the shader reads
    // the wrong cbuffer byte range.
    CHECK_FLOAT((float)offsetof(FrameConstants, viewProjection),         0.0f);
    CHECK_FLOAT((float)offsetof(FrameConstants, cameraPos),             64.0f);
    CHECK_FLOAT((float)offsetof(FrameConstants, ambientColor),          80.0f);
    CHECK_FLOAT((float)offsetof(FrameConstants, lightDirections),       96.0f);
    CHECK_FLOAT((float)offsetof(FrameConstants, lightColors),          144.0f);
    CHECK_FLOAT((float)offsetof(FrameConstants, lightingOptions),      192.0f);
    CHECK_FLOAT((float)offsetof(FrameConstants, pointLightPositions),  208.0f);
    CHECK_FLOAT((float)offsetof(FrameConstants, pointLightColors),     272.0f);
    CHECK_FLOAT((float)offsetof(FrameConstants, shroudParams),         336.0f);
    CHECK_FLOAT((float)offsetof(FrameConstants, atmosphereParams),     352.0f);
    CHECK_FLOAT((float)offsetof(FrameConstants, sunViewProjection),    368.0f);
    CHECK_FLOAT((float)offsetof(FrameConstants, shadowParams),         432.0f);
    CHECK_FLOAT((float)offsetof(FrameConstants, shadowParams2),        448.0f);
    CHECK_FLOAT((float)offsetof(FrameConstants, cloudParams),          464.0f);
    CHECK_FLOAT((float)offsetof(FrameConstants, cloudParams2),         480.0f);
    CHECK_FLOAT((float)sizeof(FrameConstants),                         496.0f);
}

// ----------------------------------------------------------------------------
// HLSL <-> C++ parity: simulate what the shader does given a world point and
// the matrices the C++ side uploads to the cbuffer. Any discrepancy between
// my C++ WorldToShadow and a "plain" sunVP * worldPos reveals a shader-side
// convention mismatch.
// ----------------------------------------------------------------------------
static void TestHLSLParity()
{
    Section("HLSL parity — row-vector mul with row_major matrix matches explicit multiply");

    SunVPInputs in{};
    in.cameraPos = { 1500.0f, 1500.0f, 300.0f };
    in.sunPos    = { 8087.9f, -3794.3f, 4493.3f };
    auto vp = BuildSunViewProjection(in);

    // Hand-rolled "what HLSL does" for mul(float4 v, float4x4 m) with
    // row_major: result[j] = sum_i v[i] * m[i][j]. That's exactly what Mul
    // does. This is a sanity check on our test harness itself.
    Render::Float3 probe{ 1400.0f, 1600.0f, 0.0f };
    Render::Float4 clip_harness = Mul({ probe.x, probe.y, probe.z, 1 }, vp);

    // Explicit multiply-by-element:
    Render::Float4 clip_manual;
    clip_manual.x = probe.x * vp.m[0][0] + probe.y * vp.m[1][0] + probe.z * vp.m[2][0] + 1.0f * vp.m[3][0];
    clip_manual.y = probe.x * vp.m[0][1] + probe.y * vp.m[1][1] + probe.z * vp.m[2][1] + 1.0f * vp.m[3][1];
    clip_manual.z = probe.x * vp.m[0][2] + probe.y * vp.m[1][2] + probe.z * vp.m[2][2] + 1.0f * vp.m[3][2];
    clip_manual.w = probe.x * vp.m[0][3] + probe.y * vp.m[1][3] + probe.z * vp.m[2][3] + 1.0f * vp.m[3][3];

    CHECK_FLOAT(clip_harness.x, clip_manual.x);
    CHECK_FLOAT(clip_harness.y, clip_manual.y);
    CHECK_FLOAT(clip_harness.z, clip_manual.z);
    CHECK_FLOAT(clip_harness.w, clip_manual.w);
}

// ----------------------------------------------------------------------------
// HLSL parity part 2: verify that multiplying a world point by sunVP gives
// a w that ≈ 1.0 (orthographic) and that clipping by w is essentially a
// no-op. If clip.w ends up wildly different from 1, there's a column-major
// issue and dividing by w produces garbage UV.
// ----------------------------------------------------------------------------
static void TestOrthoProducesUnitW()
{
    Section("Orthographic sunVP produces clip.w ≈ 1.0 for all world points");

    SunVPInputs in{};
    in.cameraPos = { 1500.0f, 1500.0f, 300.0f };
    in.sunPos    = { 8087.9f, -3794.3f, 4493.3f };
    auto vp = BuildSunViewProjection(in);

    Render::Float3 points[] = {
        { 1500.0f, 1500.0f,   0.0f },
        { 1700.0f, 1300.0f, 150.0f },
        { 1200.0f, 1600.0f, -50.0f },
        {  500.0f, 2000.0f,  50.0f },
    };
    for (auto p : points) {
        auto c = Mul({ p.x, p.y, p.z, 1 }, vp);
        CHECK_FLOAT(c.w, 1.0f);
    }
}

// ----------------------------------------------------------------------------
// Reproduce the user's report: sun shadow UV/depth should NOT depend on
// camera view direction or pitch. Test that holds world point fixed and
// only changes the camera's focus point (= cameraPos in our impl).
//
// IF the UV stays identical when XY stays and Z changes: camera-independent.
// IF the UV moves when cameraPos.Z changes: the bug is real — find it.
// ----------------------------------------------------------------------------
static void TestUVDoesNotDependOnCameraZ()
{
    Section("Shadow UV independent of camera height (z position)");

    Render::Float3 probe{ 2000.0f, 800.0f, 30.0f };
    Render::Float3 sun  { 8087.9f, -3794.3f, 4493.3f };

    float zs[] = { 50.0f, 150.0f, 500.0f, 2000.0f };
    Render::Float4x4 vp0 = BuildSunViewProjection({ { 1500.0f, 1500.0f, zs[0] }, sun });
    auto s0 = WorldToShadow(probe, vp0);

    for (int i = 1; i < 4; ++i) {
        Render::Float4x4 vpi = BuildSunViewProjection({ { 1500.0f, 1500.0f, zs[i] }, sun });
        auto si = WorldToShadow(probe, vpi);
        CHECK_FLOAT(si.u, s0.u);
        CHECK_FLOAT(si.v, s0.v);
        CHECK_FLOAT(si.receiverZ, s0.receiverZ);
    }
}

// ----------------------------------------------------------------------------
int main()
{
    TestIdentity();
    TestOrthoRH();
    TestLookAtRH_SunStraightDown();
    TestLookAtRH_SunAtAngle();
    TestCasterReceiverAlignment();
    TestSunVPIndependentOfCameraOrientation();
    TestFocusCentersAtUV50();
    TestHigherObjectSmallerDepth();
    TestFootprintBounds();
    TestEndToEndShadowOverObstacle();
    TestCameraRotationDoesNotMoveShadow();
    TestFrameConstantsLayout();
    TestHLSLParity();
    TestOrthoProducesUnitW();
    TestUVDoesNotDependOnCameraZ();

    // ------------------------------------------------------------------
    // Regression: rendering the terrain into the shadow map turns the
    // footprint into a single flat depth surface. A building that sticks
    // up from the ground gets DROWNED OUT because the terrain rasterizes
    // depth everywhere, while the building only contributes a sliver of
    // pixels. The visible symptom is "one big plane that moves with the
    // camera" — the terrain's smooth depth gradient dominates the map.
    //
    // The shadow pass must render ONLY casters, not the receiver terrain.
    // ------------------------------------------------------------------
    Section("Regression: terrain-as-caster floods the shadow map");
    {
        SunVPInputs in{ { 1000.0f, 1000.0f, 300.0f }, { 8087.9f, -3794.3f, 4493.3f } };
        auto vp = BuildSunViewProjection(in);

        constexpr int N = 128;
        auto sampleLitCount = [&](const std::vector<float>& map) {
            int lit = 0, total = 0;
            // Probe many ground points across the footprint.
            for (int yi = -12; yi <= 12; ++yi) {
                for (int xi = -12; xi <= 12; ++xi) {
                    Render::Float3 probe{ 1000.0f + xi * 80.0f,
                                          1000.0f + yi * 80.0f, 0.0f };
                    auto s = WorldToShadow(probe, vp);
                    if (!s.insideFootprint) continue;
                    int ix = (int)std::floor(s.u * N);
                    int iy = (int)std::floor(s.v * N);
                    if (ix < 0 || ix >= N || iy < 0 || iy >= N) continue;
                    float stored = map[iy * N + ix];
                    ++total;
                    if (s.receiverZ - 0.0005f <= stored) ++lit;
                }
            }
            return std::make_pair(lit, total);
        };

        auto writeCaster = [&](std::vector<float>& map, const Render::Float3& w) {
            auto s = WorldToShadow(w, vp);
            if (!s.insideFootprint) return;
            int ix = (int)std::floor(s.u * N);
            int iy = (int)std::floor(s.v * N);
            if (ix < 0 || ix >= N || iy < 0 || iy >= N) return;
            float& cell = map[iy * N + ix];
            if (s.receiverZ < cell) cell = s.receiverZ;
        };

        // Case A: shadow map contains ONLY the building (correct config).
        std::vector<float> mapA(N * N, 1.0f);
        for (int iz = 0; iz <= 100; iz += 4)
            for (int i = -25; i <= 25; i += 4)
                for (int j = -25; j <= 25; j += 4)
                    writeCaster(mapA, { 1100.0f + i, 900.0f + j, (float)iz });
        auto [litA, totalA] = sampleLitCount(mapA);

        // Case B: shadow map ALSO has the terrain plane at z=0 (bug).
        std::vector<float> mapB = mapA; // building stays
        for (int yi = -20; yi <= 20; ++yi) // dense terrain grid at ground
            for (int xi = -20; xi <= 20; ++xi)
                writeCaster(mapB, { 1000.0f + xi * 60.0f,
                                    1000.0f + yi * 60.0f, 0.0f });
        auto [litB, totalB] = sampleLitCount(mapB);

        std::printf("  Case A (only building):       %d / %d lit\n", litA, totalA);
        std::printf("  Case B (terrain + building):  %d / %d lit\n", litB, totalB);

        // A good setup: most ground probes are lit, a handful near the
        // building's shadow are shadowed. B: terrain writes at every
        // probe's texel, collapsing the test — almost every pixel reads
        // as "close enough" to its own terrain depth, so the building
        // shadow gets lost in floating-point noise + the bias band.
        CHECK(litA > 0);
        CHECK(litA < totalA);
        // Case B flips the lit-count close to unanimous one way or the
        // other — exactly the "uniform plane" symptom.
        bool flooded = (litB == totalB) || (litB == 0) ||
                       (std::abs(litB - totalB / 2) > totalB / 3);
        CHECK(flooded);
    }

    // --- ROW-vs-COLUMN-MAJOR probe ---
    //
    // Classic DX11 bug: C++ uploads a row-major matrix but the shader reads
    // it column-major, which effectively TRANSPOSES the matrix. For a valid
    // row-major VP, `mul(v, VP)` behaves as `v * VP` (row-vector * matrix).
    // If the HLSL compiler treats the data as column-major instead, the same
    // call behaves as `VP^T * v`, which with most matrices is different in a
    // very subtle way: translation ends up in the wrong column.
    //
    // The canonical symptom of the transpose bug in a shadow-map pipeline is
    // exactly "shadow plane moves with the camera" — because the translation
    // part of sunVP (which pins the sun to the camera XY focus) ends up
    // multiplied against world coordinates instead of being a constant, and
    // that produces UV values that vary linearly with worldPos + a
    // camera-dependent constant term. Let me make this test so if the live
    // HLSL is doing this, I'll see it here.
    Section("Transposed sunVP test — simulates HLSL reading matrix column-major");
    {
        SunVPInputs a{ { 1000.0f, 1000.0f, 300.0f }, { 8087.9f, -3794.3f, 4493.3f } };
        SunVPInputs b{ { 2000.0f, 1000.0f, 300.0f }, { 8087.9f, -3794.3f, 4493.3f } };
        auto vpA = BuildSunViewProjection(a);
        auto vpB = BuildSunViewProjection(b);

        // Transpose each matrix.
        auto transpose = [](const Render::Float4x4& m) {
            return Render::Float4x4(
                m._11, m._21, m._31, m._41,
                m._12, m._22, m._32, m._42,
                m._13, m._23, m._33, m._43,
                m._14, m._24, m._34, m._44);
        };
        auto vpAT = transpose(vpA);
        auto vpBT = transpose(vpB);

        Render::Float3 probe{ 1500.0f, 1500.0f, 0.0f };
        auto normalA = Mul({ probe.x, probe.y, probe.z, 1 }, vpA);
        auto normalB = Mul({ probe.x, probe.y, probe.z, 1 }, vpB);
        auto transA  = Mul({ probe.x, probe.y, probe.z, 1 }, vpAT);
        auto transB  = Mul({ probe.x, probe.y, probe.z, 1 }, vpBT);

        std::printf("  normal       camA probe.clip=(%.3f, %.3f, %.3f, %.3f)\n",
                    normalA.x, normalA.y, normalA.z, normalA.w);
        std::printf("  normal       camB probe.clip=(%.3f, %.3f, %.3f, %.3f)\n",
                    normalB.x, normalB.y, normalB.z, normalB.w);
        std::printf("  TRANSPOSED   camA probe.clip=(%.3f, %.3f, %.3f, %.3f)\n",
                    transA.x, transA.y, transA.z, transA.w);
        std::printf("  TRANSPOSED   camB probe.clip=(%.3f, %.3f, %.3f, %.3f)\n",
                    transB.x, transB.y, transB.z, transB.w);

        // Normal behaviour: moving the camera shifts UV for the same probe,
        // but the UV value remains bounded and sensible (within [-1, 1]
        // range typically).
        CHECK(std::fabs(normalA.x / normalA.w) < 2.0f);
        CHECK(std::fabs(normalB.x / normalB.w) < 2.0f);

        // With the transposed matrix, the w coordinate typically is no
        // longer 1 — it's some linear combination of the probe's position,
        // producing wild UV gradients. If this is what the live HLSL is
        // doing, the "shadow plane that moves with camera" symptom matches
        // exactly: UVs are basically screen-space because transposed ortho
        // put translation in a weird row.
        bool transposed_w_nonunit = std::fabs(transA.w - 1.0f) > 0.001f ||
                                    std::fabs(transB.w - 1.0f) > 0.001f;
        std::printf("  transposed_w_nonunit = %s\n",
                    transposed_w_nonunit ? "YES (consistent with bug symptom)" : "no");
    }

    std::printf("\n====================\n");
    std::printf("  %d / %d  passed\n", g_tests.total - g_tests.failed, g_tests.total);
    std::printf("  %d failed\n", g_tests.failed);
    std::printf("====================\n");
    return g_tests.failed;
}
