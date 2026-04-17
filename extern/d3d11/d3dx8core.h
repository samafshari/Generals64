#pragma once
#include "d3d8.h"

// D3DX filter types
#define D3DX_FILTER_NONE     0x00000001
#define D3DX_FILTER_POINT    0x00000002
#define D3DX_FILTER_LINEAR   0x00000003
#define D3DX_FILTER_TRIANGLE 0x00000004
#define D3DX_FILTER_BOX      0x00000005
#define D3DX_DEFAULT         0xFFFFFFFF

// D3DXLoadSurfaceFromSurface - no-op since we don't have D3D8 surfaces
inline HRESULT D3DXLoadSurfaceFromSurface(
    IDirect3DSurface8*, const void*, const RECT*,
    IDirect3DSurface8*, const void*, const RECT*,
    DWORD, unsigned) { return S_OK; }

// D3DXLoadSurfaceFromMemory
inline HRESULT D3DXLoadSurfaceFromMemory(
    IDirect3DSurface8*, const void*, const RECT*,
    const void*, D3DFORMAT, UINT, const void*, const RECT*,
    DWORD, unsigned) { return S_OK; }

// D3DXCreateTexture
inline HRESULT D3DXCreateTexture(
    IDirect3DDevice8*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
    IDirect3DTexture8**) { return E_NOTIMPL; }

// D3DXCreateTextureFromFileInMemory
inline HRESULT D3DXCreateTextureFromFileInMemory(
    IDirect3DDevice8*, const void*, UINT, IDirect3DTexture8**) { return E_NOTIMPL; }

// D3DXSaveSurfaceToFile
inline HRESULT D3DXSaveSurfaceToFileA(const char*, UINT, IDirect3DSurface8*, const void*, const RECT*) { return S_OK; }
#define D3DXSaveSurfaceToFile D3DXSaveSurfaceToFileA

// D3DXIMAGE_FILEFORMAT is now defined in d3d8.h

// ID3DXFont stub
struct ID3DXFont {
    unsigned long AddRef() { return 1; }
    unsigned long Release() { return 0; }
};
