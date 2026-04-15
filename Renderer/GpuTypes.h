#pragma once

// Replacement types for D3D8 types used throughout the codebase.
// These are NOT a compatibility layer - they are clean type definitions
// that replace the old D3D8 types. Code using these should migrate to
// the new Renderer API over time.

#include <cstdint>
#include <cstring>

// D3D8 format enum replacements
enum WW3DFormat
{
    WW3D_FORMAT_UNKNOWN = 0,
    WW3D_FORMAT_R8G8B8,
    WW3D_FORMAT_A8R8G8B8,
    WW3D_FORMAT_X8R8G8B8,
    WW3D_FORMAT_R5G6B5,
    WW3D_FORMAT_A1R5G5B5,
    WW3D_FORMAT_A4R4G4B4,
    WW3D_FORMAT_A8,
    WW3D_FORMAT_L8,
    WW3D_FORMAT_DXT1,
    WW3D_FORMAT_DXT2,
    WW3D_FORMAT_DXT3,
    WW3D_FORMAT_DXT4,
    WW3D_FORMAT_DXT5,
    WW3D_FORMAT_U8V8,
    WW3D_FORMAT_COUNT
};

enum WW3DZFormat
{
    WW3D_ZFORMAT_UNKNOWN = 0,
    WW3D_ZFORMAT_D16,
    WW3D_ZFORMAT_D24S8,
    WW3D_ZFORMAT_D24X8,
    WW3D_ZFORMAT_D32,
    WW3D_ZFORMAT_COUNT
};

// MipCountType is defined in WW3D2/texturefilter.h as an enum

// D3DCOLOR replacement
typedef uint32_t GpuColor;
inline GpuColor MakeColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
    return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

// D3DMATRIX replacement - row-major 4x4 matrix
struct GpuMatrix4x4
{
    union {
        struct {
            float _11, _12, _13, _14;
            float _21, _22, _23, _24;
            float _31, _32, _33, _34;
            float _41, _42, _43, _44;
        };
        float m[4][4];
        float f[16];
    };

    GpuMatrix4x4()
    {
        memset(this, 0, sizeof(*this));
        _11 = _22 = _33 = _44 = 1.0f;
    }

    bool operator==(const GpuMatrix4x4& o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
    bool operator!=(const GpuMatrix4x4& o) const { return !(*this == o); }
};

// D3DLIGHT8 replacement
struct GpuLight
{
    enum Type { POINT = 1, SPOT = 2, DIRECTIONAL = 3 };
    Type type = DIRECTIONAL;
    float diffuse[4] = {1,1,1,1};
    float specular[4] = {0,0,0,0};
    float ambient[4] = {0,0,0,0};
    float position[3] = {0,0,0};
    float direction[3] = {0,0,-1};
    float range = 1000.0f;
    float falloff = 1.0f;
    float attenuation0 = 1.0f;
    float attenuation1 = 0.0f;
    float attenuation2 = 0.0f;
    float theta = 0.0f;
    float phi = 0.0f;
};

// D3DMATERIAL8 replacement
struct GpuMaterial
{
    float diffuse[4] = {1,1,1,1};
    float ambient[4] = {1,1,1,1};
    float specular[4] = {0,0,0,0};
    float emissive[4] = {0,0,0,0};
    float power = 0.0f;
};

// D3DVIEWPORT8 replacement
struct GpuViewport
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    float minZ = 0.0f;
    float maxZ = 1.0f;
};

// D3DADAPTER_IDENTIFIER8 replacement
struct GpuAdapterInfo
{
    char driver[512] = {};
    char description[512] = {};
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    uint32_t revision = 0;
};

// Transform state types (replacement for D3DTRANSFORMSTATETYPE)
enum GpuTransformState
{
    GPU_TS_VIEW = 2,
    GPU_TS_PROJECTION = 3,
    GPU_TS_WORLD = 256,
};

// Render state types (replacement for D3DRENDERSTATETYPE)
// Only the commonly used ones
enum GpuRenderState
{
    GPU_RS_ZENABLE = 7,
    GPU_RS_FILLMODE = 8,
    GPU_RS_SHADEMODE = 9,
    GPU_RS_ZWRITEENABLE = 14,
    GPU_RS_ALPHATESTENABLE = 15,
    GPU_RS_SRCBLEND = 19,
    GPU_RS_DESTBLEND = 20,
    GPU_RS_CULLMODE = 22,
    GPU_RS_ZFUNC = 23,
    GPU_RS_ALPHAREF = 24,
    GPU_RS_ALPHAFUNC = 25,
    GPU_RS_DITHERENABLE = 26,
    GPU_RS_ALPHABLENDENABLE = 27,
    GPU_RS_FOGENABLE = 28,
    GPU_RS_SPECULARENABLE = 29,
    GPU_RS_FOGCOLOR = 34,
    GPU_RS_FOGTABLEMODE = 35,
    GPU_RS_FOGSTART = 36,
    GPU_RS_FOGEND = 37,
    GPU_RS_FOGDENSITY = 38,
    GPU_RS_LIGHTING = 137,
    GPU_RS_AMBIENT = 139,
    GPU_RS_COLORVERTEX = 141,
    GPU_RS_NORMALIZENORMALS = 143,
    GPU_RS_DIFFUSEMATERIALSOURCE = 145,
    GPU_RS_SPECULARMATERIALSOURCE = 146,
    GPU_RS_AMBIENTMATERIALSOURCE = 147,
    GPU_RS_EMISSIVEMATERIALSOURCE = 148,
    GPU_RS_STENCILENABLE = 52,
    GPU_RS_STENCILFUNC = 56,
    GPU_RS_STENCILREF = 57,
    GPU_RS_STENCILMASK = 58,
    GPU_RS_STENCILWRITEMASK = 59,
    GPU_RS_STENCILFAIL = 53,
    GPU_RS_STENCILZFAIL = 54,
    GPU_RS_STENCILPASS = 55,
    GPU_RS_CLIPPING = 136,
    GPU_RS_CLIPPLANEENABLE = 152,
    GPU_RS_MAX = 256,
};

// Texture stage state types (replacement for D3DTEXTURESTAGESTATETYPE)
enum GpuTextureStageState
{
    GPU_TSS_COLOROP = 1,
    GPU_TSS_COLORARG1 = 2,
    GPU_TSS_COLORARG2 = 3,
    GPU_TSS_ALPHAOP = 4,
    GPU_TSS_ALPHAARG1 = 5,
    GPU_TSS_ALPHAARG2 = 6,
    GPU_TSS_TEXCOORDINDEX = 11,
    GPU_TSS_ADDRESSU = 13,
    GPU_TSS_ADDRESSV = 14,
    GPU_TSS_MINFILTER = 16,
    GPU_TSS_MAGFILTER = 17,
    GPU_TSS_MIPFILTER = 18,
    GPU_TSS_TEXTURETRANSFORMFLAGS = 24,
    GPU_TSS_RESULTARG = 28,
    GPU_TSS_MAX = 32,
};

// WW3D error codes
enum WW3DErrorType
{
    WW3D_ERROR_OK = 0,
    WW3D_ERROR_GENERIC,
    WW3D_ERROR_INITIALIZATION_FAILED,
    WW3D_ERROR_RENDERING_FAILED,
};
