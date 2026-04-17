#pragma once
// D3DX8 math is replaced by standalone matrix definitions

// GpuMatrix.h is in the Renderer directory - but since d3d11lib
// includes the extern/d3d11 directory, we need GpuMatrix.h here too.
// Just inline the definitions:
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

#ifndef D3DX_PI
#define D3DX_PI 3.14159265358979323846f
#endif

// D3DXVECTOR4 - actual math type, not a stub
typedef struct D3DXVECTOR4 {
    float x, y, z, w;
    D3DXVECTOR4() : x(0), y(0), z(0), w(0) {}
    D3DXVECTOR4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
} D3DXVECTOR4;

// D3DXVECTOR3
typedef struct D3DXVECTOR3 {
    float x, y, z;
    D3DXVECTOR3() : x(0), y(0), z(0) {}
    D3DXVECTOR3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
} D3DXVECTOR3;

// D3DXVec4Transform - real matrix-vector multiply
inline D3DXVECTOR4* D3DXVec4Transform(D3DXVECTOR4* pOut, const D3DXVECTOR4* pV, const D3DXMATRIX* pM)
{
    float x = pV->x * pM->m[0][0] + pV->y * pM->m[1][0] + pV->z * pM->m[2][0] + pV->w * pM->m[3][0];
    float y = pV->x * pM->m[0][1] + pV->y * pM->m[1][1] + pV->z * pM->m[2][1] + pV->w * pM->m[3][1];
    float z = pV->x * pM->m[0][2] + pV->y * pM->m[1][2] + pV->z * pM->m[2][2] + pV->w * pM->m[3][2];
    float w = pV->x * pM->m[0][3] + pV->y * pM->m[1][3] + pV->z * pM->m[2][3] + pV->w * pM->m[3][3];
    pOut->x = x; pOut->y = y; pOut->z = z; pOut->w = w;
    return pOut;
}

// D3DXMatrixMultiply - real matrix multiply
inline D3DXMATRIX* D3DXMatrixMultiply(D3DXMATRIX* pOut, const D3DXMATRIX* pM1, const D3DXMATRIX* pM2)
{
    D3DXMATRIX result;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            result.m[i][j] = pM1->m[i][0]*pM2->m[0][j] + pM1->m[i][1]*pM2->m[1][j] +
                             pM1->m[i][2]*pM2->m[2][j] + pM1->m[i][3]*pM2->m[3][j];
    *pOut = result;
    return pOut;
}

// D3DXVec4Dot - real dot product
inline float D3DXVec4Dot(const D3DXVECTOR4* pV1, const D3DXVECTOR4* pV2)
{
    return pV1->x*pV2->x + pV1->y*pV2->y + pV1->z*pV2->z + pV1->w*pV2->w;
}

// D3DXGetFVFVertexSize
inline UINT D3DXGetFVFVertexSize(DWORD FVF)
{
    UINT size = 0;
    if (FVF & 0x002) size += 12; // XYZ
    if (FVF & 0x004) size += 16; // XYZRHW
    if (FVF & 0x008) size += 16; // XYZB1
    if (FVF & 0x010) size += 12; // NORMAL
    if (FVF & 0x020) size += 4;  // PSIZE
    if (FVF & 0x040) size += 4;  // DIFFUSE
    if (FVF & 0x080) size += 4;  // SPECULAR
    UINT texCount = (FVF >> 8) & 0xF;
    size += texCount * 8; // 2 floats per texcoord by default
    return size > 0 ? size : 12; // minimum XYZ
}

// D3DXMatrixIdentity
inline D3DXMATRIX* D3DXMatrixIdentity(D3DXMATRIX* pOut)
{
    pOut->_11 = 1; pOut->_12 = 0; pOut->_13 = 0; pOut->_14 = 0;
    pOut->_21 = 0; pOut->_22 = 1; pOut->_23 = 0; pOut->_24 = 0;
    pOut->_31 = 0; pOut->_32 = 0; pOut->_33 = 1; pOut->_34 = 0;
    pOut->_41 = 0; pOut->_42 = 0; pOut->_43 = 0; pOut->_44 = 1;
    return pOut;
}

// D3DXMatrixRotationZ
#include <cmath>
inline D3DXMATRIX* D3DXMatrixRotationZ(D3DXMATRIX* pOut, float angle)
{
    float c = cosf(angle), s = sinf(angle);
    D3DXMatrixIdentity(pOut);
    pOut->_11 = c;  pOut->_12 = s;
    pOut->_21 = -s; pOut->_22 = c;
    return pOut;
}
