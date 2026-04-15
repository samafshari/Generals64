#pragma once

// Standalone definitions for D3D matrix types.
// These replace the D3D8 SDK types with identical struct layouts.
// Used by the math library for conversion functions.

#ifndef _D3DMATRIX_DEFINED
#define _D3DMATRIX_DEFINED
typedef struct _D3DMATRIX {
    union {
        struct {
            float _11, _12, _13, _14;
            float _21, _22, _23, _24;
            float _31, _32, _33, _34;
            float _41, _42, _43, _44;
        };
        float m[4][4];
    };
} D3DMATRIX;
#endif

// D3DXMATRIX is identical layout to D3DMATRIX in the original D3DX8 SDK
#ifndef _D3DXMATRIX_DEFINED
#define _D3DXMATRIX_DEFINED
typedef struct D3DXMATRIX : public D3DMATRIX {
    D3DXMATRIX() {}
    D3DXMATRIX(
        float f11, float f12, float f13, float f14,
        float f21, float f22, float f23, float f24,
        float f31, float f32, float f33, float f34,
        float f41, float f42, float f43, float f44)
    {
        _11 = f11; _12 = f12; _13 = f13; _14 = f14;
        _21 = f21; _22 = f22; _23 = f23; _24 = f24;
        _31 = f31; _32 = f32; _33 = f33; _34 = f34;
        _41 = f41; _42 = f42; _43 = f43; _44 = f44;
    }
} D3DXMATRIX;
#endif

// D3DXVECTOR4 for Bezier math
#ifndef _D3DXVECTOR4_DEFINED
#define _D3DXVECTOR4_DEFINED
typedef struct D3DXVECTOR4 {
    float x, y, z, w;
    D3DXVECTOR4() : x(0), y(0), z(0), w(0) {}
    D3DXVECTOR4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
} D3DXVECTOR4;

// D3DXVec4Transform: transform vector by matrix (row-vector * matrix)
inline D3DXVECTOR4* D3DXVec4Transform(D3DXVECTOR4* pOut, const D3DXVECTOR4* pV, const D3DXMATRIX* pM)
{
    float x = pV->x * pM->_11 + pV->y * pM->_21 + pV->z * pM->_31 + pV->w * pM->_41;
    float y = pV->x * pM->_12 + pV->y * pM->_22 + pV->z * pM->_32 + pV->w * pM->_42;
    float z = pV->x * pM->_13 + pV->y * pM->_23 + pV->z * pM->_33 + pV->w * pM->_43;
    float w = pV->x * pM->_14 + pV->y * pM->_24 + pV->z * pM->_34 + pV->w * pM->_44;
    pOut->x = x; pOut->y = y; pOut->z = z; pOut->w = w;
    return pOut;
}

// D3DXMatrixMultiply: matrix * matrix
inline D3DXMATRIX* D3DXMatrixMultiply(D3DXMATRIX* pOut, const D3DXMATRIX* pM1, const D3DXMATRIX* pM2)
{
    D3DXMATRIX r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            r.m[i][j] = 0;
            for (int k = 0; k < 4; ++k)
                r.m[i][j] += pM1->m[i][k] * pM2->m[k][j];
        }
    *pOut = r;
    return pOut;
}
#endif
