#pragma once

// =============================================================================
// D3D8 TYPE DEFINITIONS REPLACEMENT
// =============================================================================
// This file provides the D3D8 type definitions (structs, enums, typedefs) that
// the legacy codebase expects. It does NOT provide any D3D8 functionality.
// All rendering is done through the new D3D11 Renderer.
//
// These are ONLY type definitions - the same way <cstdint> defines int32_t.
// No D3D8 DLL is loaded, no D3D8 functions exist.
// =============================================================================

#include <windows.h>
#include <mmsystem.h> // for MAKEFOURCC

// COM types needed by web browser stubs
struct IDispatch;
typedef IDispatch* LPDISPATCH;
#include "GpuMatrix.h"

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))
#endif

// =============================================================================
// SIMPLE DEFINES
// =============================================================================

// D3D result codes
#ifndef D3D_OK
#define D3D_OK 0
#endif
#ifndef D3D_SDK_VERSION
#define D3D_SDK_VERSION 120
#endif
#ifndef D3DERR_DEVICENOTRESET
#define D3DERR_DEVICENOTRESET ((HRESULT)0x88760869L)
#endif
#ifndef D3DERR_DEVICELOST
#define D3DERR_DEVICELOST ((HRESULT)0x88760868L)
#endif

// Gamma ramp constants
#define D3DSGR_CALIBRATE 0x00000001L
#define D3DSGR_NO_CALIBRATION 0x00000000L

// Present rate/interval
#define D3DPRESENT_RATE_DEFAULT 0x00000000
#define D3DPRESENT_INTERVAL_DEFAULT 0x00000000L
#define D3DPRESENT_INTERVAL_ONE 0x00000001L
#define D3DPRESENT_INTERVAL_IMMEDIATE 0x80000000L

// Back buffer type
typedef enum _D3DBACKBUFFER_TYPE
{
    D3DBACKBUFFER_TYPE_MONO = 0,
    D3DBACKBUFFER_TYPE_LEFT = 1,
    D3DBACKBUFFER_TYPE_RIGHT = 2,
} D3DBACKBUFFER_TYPE;

// Gamma ramp struct
typedef struct _D3DGAMMARAMP
{
    WORD red[256];
    WORD green[256];
    WORD blue[256];
} D3DGAMMARAMP;

// D3D color type
typedef DWORD D3DCOLOR;

#ifndef D3DCOLOR_ARGB
#define D3DCOLOR_ARGB(a,r,g,b) ((D3DCOLOR)((((a)&0xff)<<24)|(((r)&0xff)<<16)|(((g)&0xff)<<8)|((b)&0xff)))
#define D3DCOLOR_RGBA(r,g,b,a) D3DCOLOR_ARGB(a,r,g,b)
#define D3DCOLOR_XRGB(r,g,b)  D3DCOLOR_ARGB(0xff,r,g,b)
#endif

// =============================================================================
// ALL ENUMS
// =============================================================================

// D3DFORMAT
typedef enum _D3DFORMAT
{
    D3DFMT_UNKNOWN = 0,
    D3DFMT_R8G8B8 = 20,
    D3DFMT_A8R8G8B8 = 21,
    D3DFMT_X8R8G8B8 = 22,
    D3DFMT_R5G6B5 = 23,
    D3DFMT_X1R5G5B5 = 24,
    D3DFMT_A1R5G5B5 = 25,
    D3DFMT_A4R4G4B4 = 26,
    D3DFMT_R3G3B2 = 27,
    D3DFMT_A8 = 28,
    D3DFMT_A8R3G3B2 = 29,
    D3DFMT_X4R4G4B4 = 30,
    D3DFMT_A2B10G10R10 = 31,
    D3DFMT_G16R16 = 34,
    D3DFMT_A8P8 = 40,
    D3DFMT_P8 = 41,
    D3DFMT_L8 = 50,
    D3DFMT_A8L8 = 51,
    D3DFMT_A4L4 = 52,
    D3DFMT_V8U8 = 60,
    D3DFMT_L6V5U5 = 61,
    D3DFMT_X8L8V8U8 = 62,
    D3DFMT_Q8W8V8U8 = 63,
    D3DFMT_V16U16 = 64,
    D3DFMT_W11V11U10 = 65,
    D3DFMT_A2W10V10U10 = 67,
    D3DFMT_DXT1 = MAKEFOURCC('D','X','T','1'),
    D3DFMT_DXT2 = MAKEFOURCC('D','X','T','2'),
    D3DFMT_DXT3 = MAKEFOURCC('D','X','T','3'),
    D3DFMT_DXT4 = MAKEFOURCC('D','X','T','4'),
    D3DFMT_DXT5 = MAKEFOURCC('D','X','T','5'),
    D3DFMT_D16_LOCKABLE = 70,
    D3DFMT_D32 = 71,
    D3DFMT_D15S1 = 73,
    D3DFMT_D24S8 = 75,
    D3DFMT_D16 = 80,
    D3DFMT_D24X8 = 77,
    D3DFMT_D24X4S4 = 79,
    D3DFMT_UYVY = MAKEFOURCC('U','Y','V','Y'),
    D3DFMT_YUY2 = MAKEFOURCC('Y','U','Y','2'),
    D3DFMT_VERTEXDATA = 100,
    D3DFMT_INDEX16 = 101,
    D3DFMT_INDEX32 = 102,
} D3DFORMAT;

// D3DPOOL
typedef enum _D3DPOOL
{
    D3DPOOL_DEFAULT = 0,
    D3DPOOL_MANAGED = 1,
    D3DPOOL_SYSTEMMEM = 2,
    D3DPOOL_SCRATCH = 3,
} D3DPOOL;

// D3DPRIMITIVETYPE
typedef enum _D3DPRIMITIVETYPE
{
    D3DPT_POINTLIST = 1,
    D3DPT_LINELIST = 2,
    D3DPT_LINESTRIP = 3,
    D3DPT_TRIANGLELIST = 4,
    D3DPT_TRIANGLESTRIP = 5,
    D3DPT_TRIANGLEFAN = 6,
} D3DPRIMITIVETYPE;

// D3DTRANSFORMSTATETYPE
typedef enum _D3DTRANSFORMSTATETYPE
{
    D3DTS_VIEW = 2,
    D3DTS_PROJECTION = 3,
    D3DTS_TEXTURE0 = 16,
    D3DTS_TEXTURE1 = 17,
    D3DTS_TEXTURE2 = 18,
    D3DTS_TEXTURE3 = 19,
    D3DTS_WORLD = 256,
} D3DTRANSFORMSTATETYPE;

// D3DRENDERSTATETYPE
typedef enum _D3DRENDERSTATETYPE
{
    D3DRS_ZENABLE = 7,
    D3DRS_FILLMODE = 8,
    D3DRS_SHADEMODE = 9,
    D3DRS_LINEPATTERN = 10,
    D3DRS_ZWRITEENABLE = 14,
    D3DRS_ALPHATESTENABLE = 15,
    D3DRS_LASTPIXEL = 16,
    D3DRS_SRCBLEND = 19,
    D3DRS_DESTBLEND = 20,
    D3DRS_CULLMODE = 22,
    D3DRS_ZFUNC = 23,
    D3DRS_ALPHAREF = 24,
    D3DRS_ALPHAFUNC = 25,
    D3DRS_DITHERENABLE = 26,
    D3DRS_ALPHABLENDENABLE = 27,
    D3DRS_FOGENABLE = 28,
    D3DRS_SPECULARENABLE = 29,
    D3DRS_ZVISIBLE = 30,
    D3DRS_FOGCOLOR = 34,
    D3DRS_FOGTABLEMODE = 35,
    D3DRS_FOGSTART = 36,
    D3DRS_FOGEND = 37,
    D3DRS_FOGDENSITY = 38,
    D3DRS_EDGEANTIALIAS = 40,
    D3DRS_ZBIAS = 47,
    D3DRS_RANGEFOGENABLE = 48,
    D3DRS_STENCILENABLE = 52,
    D3DRS_STENCILFAIL = 53,
    D3DRS_STENCILZFAIL = 54,
    D3DRS_STENCILPASS = 55,
    D3DRS_STENCILFUNC = 56,
    D3DRS_STENCILREF = 57,
    D3DRS_STENCILMASK = 58,
    D3DRS_STENCILWRITEMASK = 59,
    D3DRS_TEXTUREFACTOR = 60,
    D3DRS_WRAP0 = 128,
    D3DRS_WRAP1 = 129,
    D3DRS_WRAP2 = 130,
    D3DRS_WRAP3 = 131,
    D3DRS_CLIPPING = 136,
    D3DRS_LIGHTING = 137,
    D3DRS_AMBIENT = 139,
    D3DRS_FOGVERTEXMODE = 140,
    D3DRS_COLORVERTEX = 141,
    D3DRS_LOCALVIEWER = 142,
    D3DRS_NORMALIZENORMALS = 143,
    D3DRS_DIFFUSEMATERIALSOURCE = 145,
    D3DRS_SPECULARMATERIALSOURCE = 146,
    D3DRS_AMBIENTMATERIALSOURCE = 147,
    D3DRS_EMISSIVEMATERIALSOURCE = 148,
    D3DRS_VERTEXBLEND = 151,
    D3DRS_CLIPPLANEENABLE = 152,
    D3DRS_SOFTWAREVERTEXPROCESSING = 153,
    D3DRS_POINTSIZE = 154,
    D3DRS_POINTSIZE_MIN = 155,
    D3DRS_POINTSPRITEENABLE = 156,
    D3DRS_POINTSCALEENABLE = 157,
    D3DRS_POINTSCALE_A = 158,
    D3DRS_POINTSCALE_B = 159,
    D3DRS_POINTSCALE_C = 160,
    D3DRS_MULTISAMPLEANTIALIAS = 161,
    D3DRS_MULTISAMPLEMASK = 162,
    D3DRS_PATCHEDGESTYLE = 163,
    D3DRS_PATCHSEGMENTS = 164,
    D3DRS_DEBUGMONITORTOKEN = 165,
    D3DRS_POINTSIZE_MAX = 166,
    D3DRS_INDEXEDVERTEXBLENDENABLE = 167,
    D3DRS_COLORWRITEENABLE = 168,
    D3DRS_TWEENFACTOR = 170,
    D3DRS_BLENDOP = 171,
    D3DRS_POSITIONORDER = 172,
    D3DRS_NORMALORDER = 173,
    D3DRS_FORCE_DWORD = 0x7fffffff,
} D3DRENDERSTATETYPE;

// D3DTEXTURESTAGESTATETYPE
typedef enum _D3DTEXTURESTAGESTATETYPE
{
    D3DTSS_COLOROP = 1,
    D3DTSS_COLORARG1 = 2,
    D3DTSS_COLORARG2 = 3,
    D3DTSS_ALPHAOP = 4,
    D3DTSS_ALPHAARG1 = 5,
    D3DTSS_ALPHAARG2 = 6,
    D3DTSS_BUMPENVMAT00 = 7,
    D3DTSS_BUMPENVMAT01 = 8,
    D3DTSS_BUMPENVMAT10 = 9,
    D3DTSS_BUMPENVMAT11 = 10,
    D3DTSS_TEXCOORDINDEX = 11,
    D3DTSS_ADDRESSU = 13,
    D3DTSS_ADDRESSV = 14,
    D3DTSS_BORDERCOLOR = 15,
    D3DTSS_MAGFILTER = 16,
    D3DTSS_MINFILTER = 17,
    D3DTSS_MIPFILTER = 18,
    D3DTSS_MIPMAPLODBIAS = 19,
    D3DTSS_MAXMIPLEVEL = 20,
    D3DTSS_MAXANISOTROPY = 21,
    D3DTSS_BUMPENVLSCALE = 22,
    D3DTSS_BUMPENVLOFFSET = 23,
    D3DTSS_TEXTURETRANSFORMFLAGS = 24,
    D3DTSS_COLORARG0 = 26,
    D3DTSS_ALPHAARG0 = 27,
    D3DTSS_RESULTARG = 28,
    D3DTSS_FORCE_DWORD = 0x7fffffff,
} D3DTEXTURESTAGESTATETYPE;

// D3DTEXTUREOP
typedef enum _D3DTEXTUREOP
{
    D3DTOP_DISABLE = 1,
    D3DTOP_SELECTARG1 = 2,
    D3DTOP_SELECTARG2 = 3,
    D3DTOP_MODULATE = 4,
    D3DTOP_MODULATE2X = 5,
    D3DTOP_MODULATE4X = 6,
    D3DTOP_ADD = 7,
    D3DTOP_ADDSIGNED = 8,
    D3DTOP_ADDSIGNED2X = 9,
    D3DTOP_SUBTRACT = 10,
    D3DTOP_ADDSMOOTH = 11,
    D3DTOP_BLENDDIFFUSEALPHA = 12,
    D3DTOP_BLENDTEXTUREALPHA = 13,
    D3DTOP_BLENDFACTORALPHA = 14,
    D3DTOP_BLENDTEXTUREALPHAPM = 15,
    D3DTOP_BLENDCURRENTALPHA = 16,
    D3DTOP_PREMODULATE = 17,
    D3DTOP_MODULATEALPHA_ADDCOLOR = 18,
    D3DTOP_MODULATECOLOR_ADDALPHA = 19,
    D3DTOP_MODULATEINVALPHA_ADDCOLOR = 20,
    D3DTOP_MODULATEINVCOLOR_ADDALPHA = 21,
    D3DTOP_BUMPENVMAP = 22,
    D3DTOP_BUMPENVMAPLUMINANCE = 23,
    D3DTOP_DOTPRODUCT3 = 24,
    D3DTOP_MULTIPLYADD = 25,
    D3DTOP_LERP = 26,
} D3DTEXTUREOP;

// D3DBLEND
typedef enum _D3DBLEND
{
    D3DBLEND_ZERO = 1,
    D3DBLEND_ONE = 2,
    D3DBLEND_SRCCOLOR = 3,
    D3DBLEND_INVSRCCOLOR = 4,
    D3DBLEND_SRCALPHA = 5,
    D3DBLEND_INVSRCALPHA = 6,
    D3DBLEND_DESTALPHA = 7,
    D3DBLEND_INVDESTALPHA = 8,
    D3DBLEND_DESTCOLOR = 9,
    D3DBLEND_INVDESTCOLOR = 10,
    D3DBLEND_SRCALPHASAT = 11,
    D3DBLEND_BOTHSRCALPHA = 12,
    D3DBLEND_BOTHINVSRCALPHA = 13,
} D3DBLEND;

// D3DBLENDOP
typedef enum _D3DBLENDOP
{
    D3DBLENDOP_ADD = 1,
    D3DBLENDOP_SUBTRACT = 2,
    D3DBLENDOP_REVSUBTRACT = 3,
    D3DBLENDOP_MIN = 4,
    D3DBLENDOP_MAX = 5,
} D3DBLENDOP;

// D3DCMPFUNC
typedef enum _D3DCMPFUNC
{
    D3DCMP_NEVER = 1,
    D3DCMP_LESS = 2,
    D3DCMP_EQUAL = 3,
    D3DCMP_LESSEQUAL = 4,
    D3DCMP_GREATER = 5,
    D3DCMP_NOTEQUAL = 6,
    D3DCMP_GREATEREQUAL = 7,
    D3DCMP_ALWAYS = 8,
} D3DCMPFUNC;

// D3DCULL
typedef enum _D3DCULL
{
    D3DCULL_NONE = 1,
    D3DCULL_CW = 2,
    D3DCULL_CCW = 3,
} D3DCULL;

// D3DFILLMODE
typedef enum _D3DFILLMODE
{
    D3DFILL_POINT = 1,
    D3DFILL_WIREFRAME = 2,
    D3DFILL_SOLID = 3,
} D3DFILLMODE;

// D3DSHADEMODE
typedef enum _D3DSHADEMODE
{
    D3DSHADE_FLAT = 1,
    D3DSHADE_GOURAUD = 2,
    D3DSHADE_PHONG = 3,
} D3DSHADEMODE;

// D3DSTENCILOP
typedef enum _D3DSTENCILOP
{
    D3DSTENCILOP_KEEP = 1,
    D3DSTENCILOP_ZERO = 2,
    D3DSTENCILOP_REPLACE = 3,
    D3DSTENCILOP_INCRSAT = 4,
    D3DSTENCILOP_DECRSAT = 5,
    D3DSTENCILOP_INVERT = 6,
    D3DSTENCILOP_INCR = 7,
    D3DSTENCILOP_DECR = 8,
} D3DSTENCILOP;

// D3DFOGMODE
typedef enum _D3DFOGMODE
{
    D3DFOG_NONE = 0,
    D3DFOG_EXP = 1,
    D3DFOG_EXP2 = 2,
    D3DFOG_LINEAR = 3,
} D3DFOGMODE;

// D3DZBUFFERTYPE
typedef enum _D3DZBUFFERTYPE
{
    D3DZB_FALSE = 0,
    D3DZB_TRUE = 1,
    D3DZB_USEW = 2,
} D3DZBUFFERTYPE;

// D3DMATERIALCOLORSOURCE
typedef enum _D3DMATERIALCOLORSOURCE
{
    D3DMCS_MATERIAL = 0,
    D3DMCS_COLOR1 = 1,
    D3DMCS_COLOR2 = 2,
} D3DMATERIALCOLORSOURCE;

// D3DVERTEXBLENDFLAGS
typedef enum _D3DVERTEXBLENDFLAGS
{
    D3DVBF_DISABLE = 0,
    D3DVBF_1WEIGHTS = 1,
    D3DVBF_2WEIGHTS = 2,
    D3DVBF_3WEIGHTS = 3,
    D3DVBF_TWEENING = 255,
    D3DVBF_0WEIGHTS = 256,
} D3DVERTEXBLENDFLAGS;

// D3DPATCHEDGESTYLE
typedef enum _D3DPATCHEDGESTYLE
{
    D3DPATCHEDGE_DISCRETE = 0,
    D3DPATCHEDGE_CONTINUOUS = 1,
} D3DPATCHEDGESTYLE;

// D3DDEBUGMONITORTOKENS
typedef enum _D3DDEBUGMONITORTOKENS
{
    D3DDMT_ENABLE = 0,
    D3DDMT_DISABLE = 1,
} D3DDEBUGMONITORTOKENS;

// D3DTEXTURETRANSFORMFLAGS
typedef enum _D3DTEXTURETRANSFORMFLAGS
{
    D3DTTFF_DISABLE = 0,
    D3DTTFF_COUNT1 = 1,
    D3DTTFF_COUNT2 = 2,
    D3DTTFF_COUNT3 = 3,
    D3DTTFF_COUNT4 = 4,
    D3DTTFF_PROJECTED = 256,
} D3DTEXTURETRANSFORMFLAGS;

// D3DTEXTUREFILTERTYPE
typedef enum _D3DTEXTUREFILTERTYPE
{
    D3DTEXF_NONE = 0,
    D3DTEXF_POINT = 1,
    D3DTEXF_LINEAR = 2,
    D3DTEXF_ANISOTROPIC = 3,
    D3DTEXF_FLATCUBIC = 4,
    D3DTEXF_GAUSSIANCUBIC = 5,
} D3DTEXTUREFILTERTYPE;

// D3DTEXTUREADDRESS
typedef enum _D3DTEXTUREADDRESS
{
    D3DTADDRESS_WRAP = 1,
    D3DTADDRESS_MIRROR = 2,
    D3DTADDRESS_CLAMP = 3,
    D3DTADDRESS_BORDER = 4,
    D3DTADDRESS_MIRRORONCE = 5,
} D3DTEXTUREADDRESS;

// D3DLIGHTTYPE
typedef enum _D3DLIGHTTYPE
{
    D3DLIGHT_POINT = 1,
    D3DLIGHT_SPOT = 2,
    D3DLIGHT_DIRECTIONAL = 3,
} D3DLIGHTTYPE;

// D3DDEVTYPE
typedef enum _D3DDEVTYPE
{
    D3DDEVTYPE_HAL = 1,
    D3DDEVTYPE_REF = 2,
    D3DDEVTYPE_SW = 3,
} D3DDEVTYPE;

// D3DSWAPEFFECT
typedef enum _D3DSWAPEFFECT
{
    D3DSWAPEFFECT_DISCARD = 1,
    D3DSWAPEFFECT_FLIP = 2,
    D3DSWAPEFFECT_COPY = 3,
    D3DSWAPEFFECT_COPY_VSYNC = 4,
} D3DSWAPEFFECT;

// D3DXIMAGE_FILEFORMAT
typedef enum _D3DXIMAGE_FILEFORMAT
{
    D3DXIFF_BMP = 0,
    D3DXIFF_JPG = 1,
    D3DXIFF_TGA = 2,
    D3DXIFF_PNG = 3,
    D3DXIFF_DDS = 4,
} D3DXIMAGE_FILEFORMAT;

// =============================================================================
// SIMPLE STRUCTS
// =============================================================================

// D3DCOLORVALUE - may already be defined by dxgitype.h when d3d11.h is included
#ifndef D3DCOLORVALUE_DEFINED
#ifndef __dxgitype_h__  // DXGI header guard
typedef struct _D3DCOLORVALUE
{
    float r, g, b, a;
} D3DCOLORVALUE;
#else
// dxgitype.h already defined D3DCOLORVALUE
#endif
#define D3DCOLORVALUE_DEFINED
#endif

// D3DVECTOR - also defined in Windows SDK dsound.h
#ifndef D3DVECTOR_DEFINED
#define D3DVECTOR_DEFINED
typedef struct _D3DVECTOR
{
    float x, y, z;
} D3DVECTOR;
#endif

// D3DLIGHT8
typedef struct _D3DLIGHT8
{
    D3DLIGHTTYPE Type;
    D3DCOLORVALUE Diffuse;
    D3DCOLORVALUE Specular;
    D3DCOLORVALUE Ambient;
    D3DVECTOR Position;
    D3DVECTOR Direction;
    float Range;
    float Falloff;
    float Attenuation0;
    float Attenuation1;
    float Attenuation2;
    float Theta;
    float Phi;
} D3DLIGHT8;

// D3DMATERIAL8
typedef struct _D3DMATERIAL8
{
    D3DCOLORVALUE Diffuse;
    D3DCOLORVALUE Ambient;
    D3DCOLORVALUE Specular;
    D3DCOLORVALUE Emissive;
    float Power;
} D3DMATERIAL8;

// D3DVIEWPORT8
typedef struct _D3DVIEWPORT8
{
    DWORD X, Y;
    DWORD Width, Height;
    float MinZ, MaxZ;
} D3DVIEWPORT8;

// D3DSURFACE_DESC
typedef struct _D3DSURFACE_DESC
{
    D3DFORMAT Format;
    UINT Type;
    DWORD Usage;
    D3DPOOL Pool;
    UINT Size;
    UINT MultiSampleType;
    UINT Width;
    UINT Height;
} D3DSURFACE_DESC;

// D3DLOCKED_RECT
typedef struct _D3DLOCKED_RECT
{
    INT Pitch;
    void* pBits;
} D3DLOCKED_RECT;

// D3DCAPS8 (minimal)
typedef struct _D3DCAPS8
{
    D3DDEVTYPE DeviceType;
    UINT AdapterOrdinal;
    DWORD Caps;
    DWORD Caps2;
    DWORD Caps3;
    DWORD PresentationIntervals;
    DWORD CursorCaps;
    DWORD DevCaps;
    DWORD PrimitiveMiscCaps;
    DWORD RasterCaps;
    DWORD ZCmpCaps;
    DWORD SrcBlendCaps;
    DWORD DestBlendCaps;
    DWORD AlphaCmpCaps;
    DWORD ShadeCaps;
    DWORD TextureCaps;
    DWORD TextureFilterCaps;
    DWORD CubeTextureFilterCaps;
    DWORD VolumeTextureFilterCaps;
    DWORD TextureAddressCaps;
    DWORD VolumeTextureAddressCaps;
    DWORD LineCaps;
    DWORD MaxTextureWidth;
    DWORD MaxTextureHeight;
    DWORD MaxVolumeExtent;
    DWORD MaxTextureRepeat;
    DWORD MaxTextureAspectRatio;
    DWORD MaxAnisotropy;
    float MaxVertexW;
    float GuardBandLeft, GuardBandTop, GuardBandRight, GuardBandBottom;
    float ExtentsAdjust;
    DWORD StencilCaps;
    DWORD FVFCaps;
    DWORD TextureOpCaps;
    DWORD MaxTextureBlendStages;
    DWORD MaxSimultaneousTextures;
    DWORD VertexProcessingCaps;
    DWORD MaxActiveLights;
    DWORD MaxUserClipPlanes;
    DWORD MaxVertexBlendMatrices;
    DWORD MaxVertexBlendMatrixIndex;
    float MaxPointSize;
    DWORD MaxPrimitiveCount;
    DWORD MaxVertexIndex;
    DWORD MaxStreams;
    DWORD MaxStreamStride;
    DWORD VertexShaderVersion;
    DWORD MaxVertexShaderConst;
    DWORD PixelShaderVersion;
    float MaxPixelShaderValue;
} D3DCAPS8;

// D3DADAPTER_IDENTIFIER8
typedef struct _D3DADAPTER_IDENTIFIER8
{
    char Driver[512];
    char Description[512];
    LARGE_INTEGER DriverVersion;
    DWORD VendorId;
    DWORD DeviceId;
    DWORD SubSysId;
    DWORD Revision;
    GUID DeviceIdentifier;
    DWORD WHQLLevel;
} D3DADAPTER_IDENTIFIER8;

// D3DPRESENT_PARAMETERS
typedef struct _D3DPRESENT_PARAMETERS_
{
    UINT BackBufferWidth;
    UINT BackBufferHeight;
    D3DFORMAT BackBufferFormat;
    UINT BackBufferCount;
    UINT MultiSampleType;
    D3DSWAPEFFECT SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed;
    BOOL EnableAutoDepthStencil;
    D3DFORMAT AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz;
    UINT FullScreen_PresentationInterval;
} D3DPRESENT_PARAMETERS;

// D3DDISPLAYMODE
typedef struct _D3DDISPLAYMODE
{
    UINT Width;
    UINT Height;
    UINT RefreshRate;
    D3DFORMAT Format;
} D3DDISPLAYMODE;

// =============================================================================
// #define FLAGS
// =============================================================================

// Texture arguments
#define D3DTA_SELECTMASK    0x0000000f
#define D3DTA_DIFFUSE       0x00000000
#define D3DTA_CURRENT       0x00000001
#define D3DTA_TEXTURE       0x00000002
#define D3DTA_TFACTOR       0x00000003
#define D3DTA_SPECULAR      0x00000004
#define D3DTA_COMPLEMENT    0x00000010
#define D3DTA_ALPHAREPLICATE 0x00000020

// D3DLOCK flags
#define D3DLOCK_READONLY  0x00000010L
#define D3DLOCK_DISCARD   0x00002000L
#define D3DLOCK_NOOVERWRITE 0x00001000L
#define D3DLOCK_NOSYSLOCK 0x00000800L

// D3DUSAGE flags
#define D3DUSAGE_RENDERTARGET   0x00000001L
#define D3DUSAGE_DEPTHSTENCIL   0x00000002L
#define D3DUSAGE_WRITEONLY      0x00000008L
#define D3DUSAGE_SOFTWAREPROCESSING 0x00000010L
#define D3DUSAGE_DONOTCLIP      0x00000020L
#define D3DUSAGE_POINTS         0x00000040L
#define D3DUSAGE_RTPATCHES      0x00000080L
#define D3DUSAGE_NPATCHES       0x00000100L
#define D3DUSAGE_DYNAMIC        0x00000200L

// FVF flags
#define D3DFVF_XYZ              0x002
#define D3DFVF_XYZRHW           0x004
#define D3DFVF_NORMAL           0x010
#define D3DFVF_DIFFUSE          0x040
#define D3DFVF_SPECULAR         0x080
#define D3DFVF_TEX0             0x000
#define D3DFVF_TEX1             0x100
#define D3DFVF_TEX2             0x200
#define D3DFVF_TEX3             0x300
#define D3DFVF_TEX4             0x400
#define D3DFVF_XYZB1            0x006
#define D3DFVF_XYZB2            0x008
#define D3DFVF_XYZB3            0x00a
#define D3DFVF_XYZB4            0x00c
#define D3DFVF_XYZB5            0x00e
#define D3DFVF_PSIZE            0x020
#define D3DFVF_TEXCOUNT_MASK    0xf00
#define D3DFVF_TEXCOUNT_SHIFT   8
#define D3DFVF_LASTBETA_UBYTE4  0x1000
#define D3DFVF_POSITION_MASK    0x00E

// FVF texture coordinate size macros
#define D3DFVF_TEXTUREFORMAT1 3
#define D3DFVF_TEXTUREFORMAT2 0
#define D3DFVF_TEXTUREFORMAT3 1
#define D3DFVF_TEXTUREFORMAT4 2
#define D3DFVF_TEXCOORDSIZE1(CoordIndex) (D3DFVF_TEXTUREFORMAT1 << (CoordIndex*2 + 16))
#define D3DFVF_TEXCOORDSIZE2(CoordIndex) (D3DFVF_TEXTUREFORMAT2 << (CoordIndex*2 + 16))
#define D3DFVF_TEXCOORDSIZE3(CoordIndex) (D3DFVF_TEXTUREFORMAT3 << (CoordIndex*2 + 16))
#define D3DFVF_TEXCOORDSIZE4(CoordIndex) (D3DFVF_TEXTUREFORMAT4 << (CoordIndex*2 + 16))

// Max texture coordinates
#define D3DDP_MAXTEXCOORD 8

// D3DCOLORWRITEENABLE flags
#define D3DCOLORWRITEENABLE_RED   1
#define D3DCOLORWRITEENABLE_GREEN 2
#define D3DCOLORWRITEENABLE_BLUE  4
#define D3DCOLORWRITEENABLE_ALPHA 8
#define D3DCOLORWRITEENABLE_ALL (D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_ALPHA)

// Device caps flags
#define D3DDEVCAPS_HWTRANSFORMANDLIGHT 0x00010000L
#define D3DPTFILTERCAPS_MAGFLINEAR     0x02000000L
#define D3DPTFILTERCAPS_MINFLINEAR     0x00000200L
#define D3DPTFILTERCAPS_MAGFANISOTROPIC 0x04000000L
#define D3DPTFILTERCAPS_MINFANISOTROPIC 0x00000400L
#define D3DPTFILTERCAPS_MIPFLINEAR     0x00020000L

// Device creation flags
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING 0x00000020L
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x00000040L
#define D3DCREATE_MIXED_VERTEXPROCESSING    0x00000080L

// Pointer typedefs
typedef struct IDirect3DTexture8* LPDIRECT3DTEXTURE8;
typedef struct IDirect3DDevice8* LPDIRECT3DDEVICE8;
typedef struct IDirect3DVertexBuffer8* LPDIRECT3DVERTEXBUFFER8;
typedef struct IDirect3DIndexBuffer8* LPDIRECT3DINDEXBUFFER8;
typedef struct IDirect3DSurface8* LPDIRECT3DSURFACE8;
typedef struct IDirect3DSwapChain8* LPDIRECT3DSWAPCHAIN8;

// D3DLOCKED_BOX (volume textures)
typedef struct _D3DLOCKED_BOX {
    INT RowPitch;
    INT SlicePitch;
    void* pBits;
} D3DLOCKED_BOX;

typedef struct _D3DBOX {
    UINT Left, Top, Right, Bottom, Front, Back;
} D3DBOX;

// D3DVOLUME_DESC defined after D3DRESOURCETYPE below

// D3DCUBEMAP_FACES
typedef enum _D3DCUBEMAP_FACES {
    D3DCUBEMAP_FACE_POSITIVE_X = 0,
    D3DCUBEMAP_FACE_NEGATIVE_X = 1,
    D3DCUBEMAP_FACE_POSITIVE_Y = 2,
    D3DCUBEMAP_FACE_NEGATIVE_Y = 3,
    D3DCUBEMAP_FACE_POSITIVE_Z = 4,
    D3DCUBEMAP_FACE_NEGATIVE_Z = 5,
} D3DCUBEMAP_FACES;

// D3DMULTISAMPLE_TYPE
typedef enum _D3DMULTISAMPLE_TYPE {
    D3DMULTISAMPLE_NONE = 0,
    D3DMULTISAMPLE_2_SAMPLES = 2,
    D3DMULTISAMPLE_4_SAMPLES = 4,
} D3DMULTISAMPLE_TYPE;

// D3DRESOURCETYPE
typedef enum _D3DRESOURCETYPE {
    D3DRTYPE_SURFACE = 1,
    D3DRTYPE_VOLUME = 2,
    D3DRTYPE_TEXTURE = 3,
    D3DRTYPE_VOLUMETEXTURE = 4,
    D3DRTYPE_CUBETEXTURE = 5,
    D3DRTYPE_VERTEXBUFFER = 6,
    D3DRTYPE_INDEXBUFFER = 7,
} D3DRESOURCETYPE;

// D3DVOLUME_DESC
typedef struct _D3DVOLUME_DESC {
    D3DFORMAT Format;
    D3DRESOURCETYPE Type;
    DWORD Usage;
    D3DPOOL Pool;
    UINT Size;
    UINT Width, Height, Depth;
} D3DVOLUME_DESC;

// MipCountType - matches WW3D2/texturefilter.h
#ifndef _MIPCOUNTTYPE_DEFINED
#define _MIPCOUNTTYPE_DEFINED
enum MipCountType
{
    MIP_LEVELS_ALL=0,
    MIP_LEVELS_1,
    MIP_LEVELS_2,
    MIP_LEVELS_3,
    MIP_LEVELS_4,
    MIP_LEVELS_5,
    MIP_LEVELS_6,
    MIP_LEVELS_7,
    MIP_LEVELS_8,
    MIP_LEVELS_10,
    MIP_LEVELS_11,
    MIP_LEVELS_12,
    MIP_LEVELS_MAX
};
#endif

// Forward declarations for WW3D2 types used in headers
class ShaderClass;
class ZTextureClass;
class SurfaceClass;
class VertexMaterialClass;

// TextureBaseClass - forward declared only. The real definition is in WW3D2/texture.h.
// In the GameEngineDevice target (which doesn't have texture.h), dx8wrapper.h inline
// functions that use TextureBaseClass will cause compile errors. Those callers should
// not exist in GameEngineDevice code - if they do, the include chain needs fixing.
class TextureBaseClass;
class TextureClass;

// =============================================================================
// COM INTERFACE DEFINITIONS
// =============================================================================
// These exist ONLY so that legacy headers with inline functions compile.
// No D3D8 DLL is loaded. All real rendering goes through the D3D11 Renderer.

struct IDirect3DBaseTexture8 {
    unsigned long AddRef() { return 1; }
    unsigned long Release() { return 0; }
    DWORD GetPriority() { return 0; }
    DWORD SetPriority(DWORD) { return 0; }
    unsigned GetLevelCount() { return 1; }
};

// IDirect3DSurface8 must be defined before IDirect3DTexture8 which uses it
struct IDirect3DSurface8 {
    UINT m_width = 0;
    UINT m_height = 0;
    D3DFORMAT m_format = D3DFMT_A8R8G8B8;
    unsigned char* m_data = nullptr;
    unsigned long m_refCount = 1;

    IDirect3DSurface8() = default;
    IDirect3DSurface8(UINT w, UINT h, D3DFORMAT fmt) : m_width(w), m_height(h), m_format(fmt) {
        UINT bpp = (fmt == D3DFMT_R5G6B5 || fmt == D3DFMT_A4R4G4B4 || fmt == D3DFMT_A1R5G5B5 || fmt == D3DFMT_X1R5G5B5) ? 2 : 4;
        m_data = new unsigned char[w * h * bpp];
        memset(m_data, 0, w * h * bpp);
    }
    ~IDirect3DSurface8() { delete[] m_data; }

    unsigned long AddRef() { return ++m_refCount; }
    unsigned long Release() { if (--m_refCount == 0) { delete this; return 0; } return m_refCount; }

    HRESULT GetDesc(D3DSURFACE_DESC* d) {
        if (!d) return E_FAIL;
        memset(d, 0, sizeof(*d));
        d->Width = m_width;
        d->Height = m_height;
        d->Format = m_format;
        return S_OK;
    }
    HRESULT LockRect(D3DLOCKED_RECT* lr, const RECT*, DWORD) {
        if (!lr) return E_FAIL;
        UINT bpp = (m_format == D3DFMT_R5G6B5 || m_format == D3DFMT_A4R4G4B4 || m_format == D3DFMT_A1R5G5B5 || m_format == D3DFMT_X1R5G5B5) ? 2 : 4;
        lr->Pitch = (INT)(m_width * bpp);
        lr->pBits = m_data;
        return S_OK;
    }
    HRESULT UnlockRect() { return S_OK; }
};

struct IDirect3DTexture8 : IDirect3DBaseTexture8 {
    IDirect3DSurface8* m_surface = nullptr;

    void SetSurface(IDirect3DSurface8* s) { m_surface = s; if (s) s->AddRef(); }
    ~IDirect3DTexture8() { if (m_surface) m_surface->Release(); }

    unsigned GetLevelCount() { return 1; }
    HRESULT GetSurfaceLevel(unsigned, struct IDirect3DSurface8** s) {
        if (!s) return E_FAIL;
        if (m_surface) { m_surface->AddRef(); *s = m_surface; return S_OK; }
        *s = nullptr; return E_NOTIMPL;
    }
    HRESULT LockRect(unsigned level, D3DLOCKED_RECT* lr, const RECT* r, DWORD f) {
        if (m_surface) return m_surface->LockRect(lr, r, f);
        if (lr) { lr->Pitch = 0; lr->pBits = nullptr; }
        return S_OK;
    }
    HRESULT UnlockRect(unsigned) { return S_OK; }
    HRESULT GetLevelDesc(unsigned, void* d) {
        if (m_surface && d) { return m_surface->GetDesc((D3DSURFACE_DESC*)d); }
        return E_NOTIMPL;
    }
};
struct IDirect3DCubeTexture8 : IDirect3DBaseTexture8 {
    HRESULT GetLevelDesc(unsigned, D3DSURFACE_DESC* d) { if(d) memset(d,0,sizeof(*d)); return S_OK; }
    HRESULT GetCubeMapSurface(unsigned, unsigned, IDirect3DSurface8** s) { if(s) *s=nullptr; return E_NOTIMPL; }
    HRESULT LockRect(unsigned, unsigned, D3DLOCKED_RECT* lr, const RECT*, DWORD) { if(lr){lr->Pitch=0;lr->pBits=nullptr;} return S_OK; }
    HRESULT UnlockRect(unsigned, unsigned) { return S_OK; }
};
struct IDirect3DVolumeTexture8 : IDirect3DBaseTexture8 {
    HRESULT LockBox(unsigned, D3DLOCKED_BOX* lb, const D3DBOX*, DWORD) { if(lb){lb->RowPitch=0;lb->SlicePitch=0;lb->pBits=nullptr;} return S_OK; }
    HRESULT UnlockBox(unsigned) { return S_OK; }
    HRESULT GetLevelDesc(unsigned, void*) { return E_NOTIMPL; }
};
struct IDirect3DVertexBuffer8 {
    unsigned long AddRef() { return 1; }
    unsigned long Release() { return 0; }
    HRESULT Lock(unsigned, unsigned, BYTE**, DWORD) { return E_NOTIMPL; }
    HRESULT Unlock() { return S_OK; }
};
struct IDirect3DIndexBuffer8 {
    unsigned long AddRef() { return 1; }
    unsigned long Release() { return 0; }
    HRESULT Lock(unsigned, unsigned, BYTE**, DWORD) { return E_NOTIMPL; }
    HRESULT Unlock() { return S_OK; }
};
struct IDirect3DSwapChain8 {
    HRESULT Present(const RECT*, const RECT*, HWND, void*) { return S_OK; }
};
struct IDirect3DDevice8 {
    HRESULT TestCooperativeLevel() { return D3D_OK; }
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE, const D3DMATRIX*) { return S_OK; }
    HRESULT GetTransform(D3DTRANSFORMSTATETYPE, D3DMATRIX* m) { if(m) memset(m,0,sizeof(*m)); return S_OK; }
    HRESULT SetRenderState(D3DRENDERSTATETYPE, DWORD) { return S_OK; }
    HRESULT GetRenderState(D3DRENDERSTATETYPE, DWORD* v) { if(v) *v=0; return S_OK; }
    HRESULT SetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, DWORD) { return S_OK; }
    HRESULT GetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, DWORD* v) { if(v) *v=0; return S_OK; }
    HRESULT SetTexture(DWORD, IDirect3DBaseTexture8*) { return S_OK; }
    HRESULT SetVertexShader(DWORD) { return S_OK; }
    HRESULT SetPixelShader(DWORD) { return S_OK; }
    HRESULT SetVertexShaderConstant(DWORD, const void*, DWORD) { return S_OK; }
    HRESULT SetPixelShaderConstant(DWORD, const void*, DWORD) { return S_OK; }
    HRESULT SetStreamSource(UINT, IDirect3DVertexBuffer8*, UINT) { return S_OK; }
    HRESULT SetIndices(IDirect3DIndexBuffer8*, UINT) { return S_OK; }
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE, UINT, UINT, UINT, UINT) { return S_OK; }
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE, UINT, UINT) { return S_OK; }
    HRESULT SetMaterial(const D3DMATERIAL8*) { return S_OK; }
    HRESULT SetLight(DWORD, const D3DLIGHT8*) { return S_OK; }
    HRESULT LightEnable(DWORD, BOOL) { return S_OK; }
    HRESULT GetLight(DWORD, D3DLIGHT8*) { return S_OK; }
    HRESULT GetLightEnable(DWORD, BOOL*) { return S_OK; }
    HRESULT SetClipPlane(DWORD, const float*) { return S_OK; }
    HRESULT Clear(DWORD, const void*, DWORD, D3DCOLOR, float, DWORD) { return S_OK; }
    HRESULT BeginScene() { return S_OK; }
    HRESULT EndScene() { return S_OK; }
    HRESULT Present(const RECT*, const RECT*, HWND, void*) { return S_OK; }
    HRESULT SetViewport(const D3DVIEWPORT8*) { return S_OK; }
    HRESULT GetViewport(D3DVIEWPORT8* vp) { if(vp) memset(vp,0,sizeof(*vp)); return S_OK; }
    HRESULT CreateTexture(UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture8** out) {
        if (!out) return E_FAIL;
        auto* tex = new IDirect3DTexture8();
        auto* surf = new IDirect3DSurface8(w, h, fmt);
        tex->SetSurface(surf);
        surf->Release(); // tex now owns it
        *out = tex;
        return S_OK;
    }
    HRESULT CreateCubeTexture(UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DCubeTexture8**) { return E_NOTIMPL; }
    HRESULT CreateVolumeTexture(UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DVolumeTexture8**) { return E_NOTIMPL; }
    HRESULT CreateVertexBuffer(UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer8**) { return E_NOTIMPL; }
    HRESULT CreateIndexBuffer(UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DIndexBuffer8**) { return E_NOTIMPL; }
    HRESULT CreateImageSurface(UINT w, UINT h, D3DFORMAT fmt, IDirect3DSurface8** out) {
        if (!out) return E_FAIL;
        *out = new IDirect3DSurface8(w, h, fmt);
        return S_OK;
    }
    HRESULT CreateRenderTarget(UINT, UINT, D3DFORMAT, UINT, BOOL, IDirect3DSurface8**) { return E_NOTIMPL; }
    HRESULT CreateDepthStencilSurface(UINT, UINT, D3DFORMAT, UINT, IDirect3DSurface8**) { return E_NOTIMPL; }
    HRESULT GetBackBuffer(UINT, UINT, IDirect3DSurface8**) { return E_NOTIMPL; }
    HRESULT GetDepthStencilSurface(IDirect3DSurface8**) { return E_NOTIMPL; }
    HRESULT SetRenderTarget(IDirect3DSurface8*, IDirect3DSurface8*) { return S_OK; }
    HRESULT GetRenderTarget(IDirect3DSurface8**) { return E_NOTIMPL; }
    HRESULT GetDeviceCaps(D3DCAPS8* c) { if(c) memset(c,0,sizeof(*c)); return S_OK; }
    HRESULT GetDisplayMode(D3DDISPLAYMODE* m) { if(m) memset(m,0,sizeof(*m)); return S_OK; }
    HRESULT GetFrontBuffer(IDirect3DSurface8*) { return E_NOTIMPL; }
    HRESULT CopyRects(IDirect3DSurface8*, const RECT*, UINT, IDirect3DSurface8*, const POINT*) { return S_OK; }
    HRESULT SetGammaRamp(DWORD, const void*) {}
    HRESULT GetGammaRamp(void*) {}
    HRESULT CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS*, IDirect3DSwapChain8**) { return E_NOTIMPL; }
    HRESULT UpdateTexture(IDirect3DBaseTexture8*, IDirect3DBaseTexture8*) { return S_OK; }
    HRESULT ResourceManagerDiscardBytes(DWORD) { return S_OK; }
    UINT GetAvailableTextureMem() { return 512 * 1024 * 1024; }
};
struct IDirect3D8 {
    HRESULT GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER8* id) { if(id) memset(id,0,sizeof(*id)); return S_OK; }
    UINT GetAdapterCount() { return 1; }
    UINT GetAdapterModeCount(UINT) { return 1; }
    HRESULT EnumAdapterModes(UINT, UINT, D3DDISPLAYMODE* m) { if(m) memset(m,0,sizeof(*m)); return S_OK; }
    HRESULT GetAdapterDisplayMode(UINT, D3DDISPLAYMODE* m) { if(m) memset(m,0,sizeof(*m)); return S_OK; }
    HRESULT CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD, UINT, D3DFORMAT) { return S_OK; }
    HRESULT CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT) { return S_OK; }
    HRESULT CreateDevice(UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice8**) { return E_NOTIMPL; }
    unsigned long Release() { return 0; }
};
