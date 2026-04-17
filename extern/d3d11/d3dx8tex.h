#pragma once
#include "d3d8.h"
#include "d3dx8core.h"

// D3DXGetImageInfoFromFileInMemory
struct D3DXIMAGE_INFO {
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT MipLevels;
    D3DFORMAT Format;
    UINT ResourceType;
    D3DXIMAGE_FILEFORMAT ImageFileFormat;
};

inline HRESULT D3DXGetImageInfoFromFileInMemory(const void*, UINT, D3DXIMAGE_INFO* info) {
    if (info) memset(info, 0, sizeof(*info));
    return S_OK;
}

inline HRESULT D3DXCreateTextureFromFileInMemoryEx(
    IDirect3DDevice8*, const void*, UINT,
    UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
    DWORD, DWORD, unsigned, D3DXIMAGE_INFO*,
    void*, IDirect3DTexture8**) { return E_NOTIMPL; }

inline HRESULT D3DXLoadSurfaceFromFileInMemory(
    IDirect3DSurface8*, const void*, const RECT*,
    const void*, UINT, const RECT*, DWORD, unsigned,
    D3DXIMAGE_INFO*) { return S_OK; }

// D3DXFilterTexture
inline HRESULT D3DXFilterTexture(IDirect3DTexture8*, const void*, UINT, DWORD) { return S_OK; }
