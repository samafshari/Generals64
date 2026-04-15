#pragma once
// colorconv.h — Standalone color conversion functions
// Replaces DX8Wrapper::Convert_Color without DX8Wrapper dependency.

#include "vector4.h"
#include "vector3.h"

// Convert packed ARGB (0xAARRGGBB) to Vector4 (R,G,B,A)
inline Vector4 W3D_Convert_Color(unsigned int color)
{
    Vector4 col;
    col[3] = ((color & 0xff000000) >> 24) / 255.0f;
    col[0] = ((color & 0xff0000) >> 16) / 255.0f;
    col[1] = ((color & 0xff00) >> 8) / 255.0f;
    col[2] = (color & 0xff) / 255.0f;
    return col;
}

// Convert Vector3 RGB + alpha to packed ARGB
inline unsigned int W3D_Convert_Color(const Vector3& color, float alpha)
{
    auto clampByte = [](float v) -> unsigned char {
        int i = (int)(v * 255.0f + 0.5f);
        return (unsigned char)(i < 0 ? 0 : (i > 255 ? 255 : i));
    };
    return ((unsigned int)clampByte(alpha) << 24) |
           ((unsigned int)clampByte(color.X) << 16) |
           ((unsigned int)clampByte(color.Y) << 8) |
           (unsigned int)clampByte(color.Z);
}

// Convert Vector4 RGBA to packed ARGB
inline unsigned int W3D_Convert_Color(const Vector4& color)
{
    return W3D_Convert_Color(Vector3(color.X, color.Y, color.Z), color.W);
}

// Clamped version (clamps input to 0-1 before converting)
inline unsigned int W3D_Convert_Color_Clamp(const Vector4& color)
{
    auto clamp01 = [](float v) -> float { return v < 0 ? 0 : (v > 1 ? 1 : v); };
    Vector4 c(clamp01(color.X), clamp01(color.Y), clamp01(color.Z), clamp01(color.W));
    return W3D_Convert_Color(c);
}
