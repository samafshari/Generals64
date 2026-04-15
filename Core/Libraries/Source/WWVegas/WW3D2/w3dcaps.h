#pragma once
// w3dcaps.h — Standalone hardware capability queries and state for D3D11
// Replaces DX8Wrapper::Get_Current_Caps() and DX8Wrapper::Is_Initted()
// without DX8Wrapper dependency.

namespace W3DCaps
{
    inline bool Support_TnL() { return true; }
    inline bool Support_DXTC() { return true; }
    inline bool Support_NPatches() { return false; } // not used in D3D11
    inline bool Support_ZBias() { return true; }
    inline bool Support_Dot3() { return true; }
    inline bool Support_Bump_Envmap() { return true; }
    inline bool Support_Bump_Envmap_Luminance() { return true; }
    inline bool Support_Texture_Format(int /*format*/) { return true; }
    inline bool Needs_Vertex_Normals() { return true; }
    inline int Get_Max_Textures_Per_Pass() { return 8; }

    // Device state — replaces DX8Wrapper::Is_Initted()
    inline bool Is_Device_Initted() { return true; }
}

// Standalone transform access — replaces DX8Wrapper::Get_Transform
// Implemented in dx8wrapper.cpp, reads from DX8Transforms[] cache.
class Matrix4x4;
namespace W3DTransform
{
    void Get_View(Matrix4x4& m);
    void Get_World(Matrix4x4& m);
    void Get_Projection(Matrix4x4& m);
}
