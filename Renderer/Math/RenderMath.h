#pragma once

#include <cstdint>
#include <cmath>

// Platform-neutral math types for the Renderer public API.
// On Windows, these are layout-compatible with DirectX::XMFLOAT* types
// (same member names, same memory layout) so reinterpret_cast between them is safe.

namespace Render
{

struct Float2
{
    float x, y;

    Float2() = default;
    constexpr Float2(float _x, float _y) : x(_x), y(_y) {}
};

struct Float3
{
    float x, y, z;

    Float3() = default;
    constexpr Float3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
};

struct Float4
{
    float x, y, z, w;

    Float4() = default;
    constexpr Float4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

struct Float4x4
{
    union
    {
        struct
        {
            float _11, _12, _13, _14;
            float _21, _22, _23, _24;
            float _31, _32, _33, _34;
            float _41, _42, _43, _44;
        };
        float m[4][4];
    };

    Float4x4() = default;
    constexpr Float4x4(float m00, float m01, float m02, float m03,
                       float m10, float m11, float m12, float m13,
                       float m20, float m21, float m22, float m23,
                       float m30, float m31, float m32, float m33)
        : _11(m00), _12(m01), _13(m02), _14(m03)
        , _21(m10), _22(m11), _23(m12), _24(m13)
        , _31(m20), _32(m21), _33(m22), _34(m23)
        , _41(m30), _42(m31), _43(m32), _44(m33) {}
};

struct Int4
{
    int32_t x, y, z, w;

    Int4() = default;
    constexpr Int4(int32_t _x, int32_t _y, int32_t _z, int32_t _w) : x(_x), y(_y), z(_z), w(_w) {}
};

// --- Platform-neutral math operations ---
// Used by the Renderer internally so it does not depend on DirectXMath.

inline Float4x4 Float4x4Identity()
{
    return Float4x4(
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1);
}

inline Float4x4 Float4x4Multiply(const Float4x4& a, const Float4x4& b)
{
    Float4x4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            r.m[i][j] = 0;
            for (int k = 0; k < 4; ++k)
                r.m[i][j] += a.m[i][k] * b.m[k][j];
        }
    return r;
}

// 4x4 matrix inverse via cofactor expansion. Returns identity if singular.
inline Float4x4 Float4x4Inverse(const Float4x4& m)
{
    const float* s = &m._11;
    float inv[16], det;

    inv[0]  =  s[5]*s[10]*s[15] - s[5]*s[11]*s[14] - s[9]*s[6]*s[15] + s[9]*s[7]*s[14] + s[13]*s[6]*s[11] - s[13]*s[7]*s[10];
    inv[4]  = -s[4]*s[10]*s[15] + s[4]*s[11]*s[14] + s[8]*s[6]*s[15] - s[8]*s[7]*s[14] - s[12]*s[6]*s[11] + s[12]*s[7]*s[10];
    inv[8]  =  s[4]*s[9]*s[15]  - s[4]*s[11]*s[13] - s[8]*s[5]*s[15] + s[8]*s[7]*s[13] + s[12]*s[5]*s[11] - s[12]*s[7]*s[9];
    inv[12] = -s[4]*s[9]*s[14]  + s[4]*s[10]*s[13] + s[8]*s[5]*s[14] - s[8]*s[6]*s[13] - s[12]*s[5]*s[10] + s[12]*s[6]*s[9];
    inv[1]  = -s[1]*s[10]*s[15] + s[1]*s[11]*s[14] + s[9]*s[2]*s[15] - s[9]*s[3]*s[14] - s[13]*s[2]*s[11] + s[13]*s[3]*s[10];
    inv[5]  =  s[0]*s[10]*s[15] - s[0]*s[11]*s[14] - s[8]*s[2]*s[15] + s[8]*s[3]*s[14] + s[12]*s[2]*s[11] - s[12]*s[3]*s[10];
    inv[9]  = -s[0]*s[9]*s[15]  + s[0]*s[11]*s[13] + s[8]*s[1]*s[15] - s[8]*s[3]*s[13] - s[12]*s[1]*s[11] + s[12]*s[3]*s[9];
    inv[13] =  s[0]*s[9]*s[14]  - s[0]*s[10]*s[13] - s[8]*s[1]*s[14] + s[8]*s[2]*s[13] + s[12]*s[1]*s[10] - s[12]*s[2]*s[9];
    inv[2]  =  s[1]*s[6]*s[15]  - s[1]*s[7]*s[14]  - s[5]*s[2]*s[15] + s[5]*s[3]*s[14] + s[13]*s[2]*s[7]  - s[13]*s[3]*s[6];
    inv[6]  = -s[0]*s[6]*s[15]  + s[0]*s[7]*s[14]  + s[4]*s[2]*s[15] - s[4]*s[3]*s[14] - s[12]*s[2]*s[7]  + s[12]*s[3]*s[6];
    inv[10] =  s[0]*s[5]*s[15]  - s[0]*s[7]*s[13]  - s[4]*s[1]*s[15] + s[4]*s[3]*s[13] + s[12]*s[1]*s[7]  - s[12]*s[3]*s[5];
    inv[14] = -s[0]*s[5]*s[14]  + s[0]*s[6]*s[13]  + s[4]*s[1]*s[14] - s[4]*s[2]*s[13] - s[12]*s[1]*s[6]  + s[12]*s[2]*s[5];
    inv[3]  = -s[1]*s[6]*s[11]  + s[1]*s[7]*s[10]  + s[5]*s[2]*s[11] - s[5]*s[3]*s[10] - s[9]*s[2]*s[7]   + s[9]*s[3]*s[6];
    inv[7]  =  s[0]*s[6]*s[11]  - s[0]*s[7]*s[10]  - s[4]*s[2]*s[11] + s[4]*s[3]*s[10] + s[8]*s[2]*s[7]   - s[8]*s[3]*s[6];
    inv[11] = -s[0]*s[5]*s[11]  + s[0]*s[7]*s[9]   + s[4]*s[1]*s[11] - s[4]*s[3]*s[9]  - s[8]*s[1]*s[7]   + s[8]*s[3]*s[5];
    inv[15] =  s[0]*s[5]*s[10]  - s[0]*s[6]*s[9]   - s[4]*s[1]*s[10] + s[4]*s[2]*s[9]  + s[8]*s[1]*s[6]   - s[8]*s[2]*s[5];

    det = s[0]*inv[0] + s[1]*inv[4] + s[2]*inv[8] + s[3]*inv[12];
    if (det == 0.0f) return Float4x4Identity();

    float invDet = 1.0f / det;
    Float4x4 r;
    float* d = &r._11;
    for (int i = 0; i < 16; ++i) d[i] = inv[i] * invDet;
    return r;
}

// Right-handed look-at matrix (row-major)
inline Float4x4 Float4x4LookAtRH(const Float3& eye, const Float3& target, const Float3& up)
{
    float fx = target.x - eye.x, fy = target.y - eye.y, fz = target.z - eye.z;
    float flen = 1.0f / sqrtf(fx*fx + fy*fy + fz*fz);
    fx *= flen; fy *= flen; fz *= flen;

    // s = normalize(cross(f, up))
    float sx = fy*up.z - fz*up.y, sy = fz*up.x - fx*up.z, sz = fx*up.y - fy*up.x;
    float slen = 1.0f / sqrtf(sx*sx + sy*sy + sz*sz);
    sx *= slen; sy *= slen; sz *= slen;

    // u = cross(s, f)
    float ux = sy*fz - sz*fy, uy = sz*fx - sx*fz, uz = sx*fy - sy*fx;

    return Float4x4(
         sx,  ux, -fx, 0,
         sy,  uy, -fy, 0,
         sz,  uz, -fz, 0,
        -(sx*eye.x + sy*eye.y + sz*eye.z),
        -(ux*eye.x + uy*eye.y + uz*eye.z),
         (fx*eye.x + fy*eye.y + fz*eye.z),
        1);
}

// Right-handed orthographic projection (row-major, depth [0,1])
inline Float4x4 Float4x4OrthoRH(float width, float height, float nearZ, float farZ)
{
    float range = 1.0f / (nearZ - farZ);
    return Float4x4(
        2.0f/width, 0,          0,           0,
        0,          2.0f/height,0,           0,
        0,          0,          range,       0,
        0,          0,          nearZ*range, 1);
}

// Transform a Float4 by a Float4x4 (row-vector * matrix, row-major)
inline Float4 Float4Transform(const Float4& v, const Float4x4& m)
{
    return Float4(
        v.x*m._11 + v.y*m._21 + v.z*m._31 + v.w*m._41,
        v.x*m._12 + v.y*m._22 + v.z*m._32 + v.w*m._42,
        v.x*m._13 + v.y*m._23 + v.z*m._33 + v.w*m._43,
        v.x*m._14 + v.y*m._24 + v.z*m._34 + v.w*m._44);
}

inline float Float3Dot(const Float3& a, const Float3& b)
{
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

inline float Float3Length(const Float3& v)
{
    return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
}

inline Float3 Float3Normalize(const Float3& v)
{
    float len = Float3Length(v);
    if (len < 0.00001f) return Float3(0, 0, 0);
    float inv = 1.0f / len;
    return Float3(v.x*inv, v.y*inv, v.z*inv);
}

inline Float3 Float3Add(const Float3& a, const Float3& b) { return Float3(a.x+b.x, a.y+b.y, a.z+b.z); }
inline Float3 Float3Sub(const Float3& a, const Float3& b) { return Float3(a.x-b.x, a.y-b.y, a.z-b.z); }
inline Float3 Float3Scale(const Float3& v, float s) { return Float3(v.x*s, v.y*s, v.z*s); }
inline Float4 Float4Sub(const Float4& a, const Float4& b) { return Float4(a.x-b.x, a.y-b.y, a.z-b.z, a.w-b.w); }
inline Float4 Float4Scale(const Float4& v, float s) { return Float4(v.x*s, v.y*s, v.z*s, v.w*s); }

} // namespace Render

// Conversion helpers for DirectXMath interop (Windows only).
// These are zero-cost reinterpret_casts — the types have identical layout.
#ifdef _WIN32
#include <DirectXMath.h>

namespace Render
{

inline const DirectX::XMFLOAT2& ToXM(const Float2& v) { return reinterpret_cast<const DirectX::XMFLOAT2&>(v); }
inline const DirectX::XMFLOAT3& ToXM(const Float3& v) { return reinterpret_cast<const DirectX::XMFLOAT3&>(v); }
inline const DirectX::XMFLOAT4& ToXM(const Float4& v) { return reinterpret_cast<const DirectX::XMFLOAT4&>(v); }
inline const DirectX::XMFLOAT4X4& ToXM(const Float4x4& v) { return reinterpret_cast<const DirectX::XMFLOAT4X4&>(v); }
inline const DirectX::XMINT4& ToXM(const Int4& v) { return reinterpret_cast<const DirectX::XMINT4&>(v); }

inline DirectX::XMFLOAT2& ToXM(Float2& v) { return reinterpret_cast<DirectX::XMFLOAT2&>(v); }
inline DirectX::XMFLOAT3& ToXM(Float3& v) { return reinterpret_cast<DirectX::XMFLOAT3&>(v); }
inline DirectX::XMFLOAT4& ToXM(Float4& v) { return reinterpret_cast<DirectX::XMFLOAT4&>(v); }
inline DirectX::XMFLOAT4X4& ToXM(Float4x4& v) { return reinterpret_cast<DirectX::XMFLOAT4X4&>(v); }
inline DirectX::XMINT4& ToXM(Int4& v) { return reinterpret_cast<DirectX::XMINT4&>(v); }

inline const Float2& FromXM(const DirectX::XMFLOAT2& v) { return reinterpret_cast<const Float2&>(v); }
inline const Float3& FromXM(const DirectX::XMFLOAT3& v) { return reinterpret_cast<const Float3&>(v); }
inline const Float4& FromXM(const DirectX::XMFLOAT4& v) { return reinterpret_cast<const Float4&>(v); }
inline const Float4x4& FromXM(const DirectX::XMFLOAT4X4& v) { return reinterpret_cast<const Float4x4&>(v); }
inline const Int4& FromXM(const DirectX::XMINT4& v) { return reinterpret_cast<const Int4&>(v); }

} // namespace Render
#endif
