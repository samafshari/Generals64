#pragma once

// ============================================================================
// RenderUtils.h — Modern D3D11 replacements for DX8Wrapper utility functions
// ============================================================================
// These standalone functions replace DX8Wrapper::Convert_Color,
// DX8Wrapper::Convert_Color_Clamp, and other utility methods that had
// zero D3D8 dependency but lived in the DX8Wrapper class.
//
// Usage: #include "RenderUtils.h" and call RenderUtils::ConvertColor()
// instead of DX8Wrapper::Convert_Color().
// ============================================================================

#include <cmath>
#include <algorithm>
#include "Math/RenderMath.h"

class Matrix3D;
class Matrix4x4;

namespace RenderUtils
{

// --- Matrix conversion utilities ---

// Convert W3D Matrix3D (3x4, column-vector convention) to row-major Float4x4.
// Bakes the transpose into element ordering — no runtime XMMatrixTranspose needed.
inline Render::Float4x4 Matrix3DToFloat4x4(const Matrix3D& m)
{
    return {
        m[0][0], m[1][0], m[2][0], 0.0f,
        m[0][1], m[1][1], m[2][1], 0.0f,
        m[0][2], m[1][2], m[2][2], 0.0f,
        m[0][3], m[1][3], m[2][3], 1.0f
    };
}

// Convert W3D Matrix4x4 to row-major Float4x4 (with transpose baked in).
inline Render::Float4x4 Matrix4x4ToFloat4x4(const Matrix4x4& m)
{
    return {
        m[0][0], m[1][0], m[2][0], m[3][0],
        m[0][1], m[1][1], m[2][1], m[3][1],
        m[0][2], m[1][2], m[2][2], m[3][2],
        m[0][3], m[1][3], m[2][3], m[3][3]
    };
}

// Build a rotation-only view matrix (strips translation for skybox rendering).
inline Render::Float4x4 Matrix3DToRotationOnlyFloat4x4(const Matrix3D& m)
{
    return {
        m[0][0], m[1][0], m[2][0], 0.0f,
        m[0][1], m[1][1], m[2][1], 0.0f,
        m[0][2], m[1][2], m[2][2], 0.0f,
        0.0f,    0.0f,    0.0f,    1.0f
    };
}

// Check if a Matrix3D has any NaN or inf values.
inline bool IsFiniteMatrix3D(const Matrix3D& m)
{
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
            if (!std::isfinite(m[r][c]))
                return false;
    return true;
}

// Sanitize a Matrix3D: reset to identity if any element is NaN/inf.
inline void SanitizeMatrix3D(Matrix3D& m)
{
    if (!IsFiniteMatrix3D(m))
        m.Make_Identity();
}

// --- Color conversion utilities ---

// Convert packed ARGB color (0xAARRGGBB) to float4 (R,G,B,A)
inline void UnpackColorARGB(unsigned int argb, float& r, float& g, float& b, float& a)
{
    a = ((argb >> 24) & 0xFF) / 255.0f;
    r = ((argb >> 16) & 0xFF) / 255.0f;
    g = ((argb >> 8) & 0xFF) / 255.0f;
    b = (argb & 0xFF) / 255.0f;
}

// Convert float4 (R,G,B,A) to packed ARGB color (0xAARRGGBB)
inline unsigned int PackColorARGB(float r, float g, float b, float a)
{
    auto clamp = [](float v) -> unsigned char {
        if (v <= 0.0f) return 0;
        if (v >= 1.0f) return 255;
        return (unsigned char)(v * 255.0f + 0.5f);
    };
    return ((unsigned int)clamp(a) << 24) |
           ((unsigned int)clamp(r) << 16) |
           ((unsigned int)clamp(g) << 8) |
           (unsigned int)clamp(b);
}

// Convert float4 (R,G,B,A) to packed ABGR color for D3D11 R8G8B8A8_UNORM
inline unsigned int PackColorABGR(float r, float g, float b, float a)
{
    auto clamp = [](float v) -> unsigned char {
        if (v <= 0.0f) return 0;
        if (v >= 1.0f) return 255;
        return (unsigned char)(v * 255.0f + 0.5f);
    };
    return ((unsigned int)clamp(a) << 24) |
           ((unsigned int)clamp(b) << 16) |
           ((unsigned int)clamp(g) << 8) |
           (unsigned int)clamp(r);
}

// Convert ARGB to ABGR (swap R and B channels)
inline unsigned int ARGBtoABGR(unsigned int argb)
{
    unsigned int a = (argb >> 24) & 0xFF;
    unsigned int r = (argb >> 16) & 0xFF;
    unsigned int g = (argb >> 8) & 0xFF;
    unsigned int b = argb & 0xFF;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

} // namespace RenderUtils
