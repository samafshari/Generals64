// W3DRoadBufferDX11Stubs.cpp — Minimal stubs for DX8/WW3D symbols referenced
// by W3DRoadBuffer.cpp. The road geometry pipeline only uses CPU arrays;
// these stubs satisfy the linker for DX8 rendering functions that exist
// in W3DRoadBuffer.cpp but are never invoked in the D3D11 path.

#include "Lib/BaseType.h"

// Forward declare the classes matching the exact signatures the linker expects.
// We avoid including the full DX8 headers to prevent pulling in more dependencies.

#include "WW3D2/dx8vertexbuffer.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/dx8wrapper.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"

#include <cstring>

// VertexBufferClass — base of DX8VertexBufferClass
VertexBufferClass::VertexBufferClass(unsigned, unsigned, unsigned short, unsigned)
{
    memset(this, 0, sizeof(*this));
}
VertexBufferClass::~VertexBufferClass() {}
void VertexBufferClass::Add_Engine_Ref() const {}
void VertexBufferClass::Release_Engine_Ref() const {}

VertexBufferClass::WriteLockClass::WriteLockClass(VertexBufferClass* vb, int)
    : VertexBufferLockClass(vb) { Vertices = nullptr; }
VertexBufferClass::WriteLockClass::~WriteLockClass() {}

// IndexBufferClass — base of DX8IndexBufferClass
IndexBufferClass::IndexBufferClass(unsigned, unsigned short)
{
    memset(this, 0, sizeof(*this));
}
IndexBufferClass::~IndexBufferClass() {}
void IndexBufferClass::Add_Engine_Ref() const {}
void IndexBufferClass::Release_Engine_Ref() const {}

// IndexBufferClass::WriteLockClass has private members, initialize with memset
IndexBufferClass::WriteLockClass::WriteLockClass(IndexBufferClass*, int)
{ memset(this, 0, sizeof(*this)); }
IndexBufferClass::WriteLockClass::~WriteLockClass() {}

// DX8VertexBufferClass
DX8VertexBufferClass::DX8VertexBufferClass(unsigned int fvf, unsigned short count, UsageType, unsigned int)
    : VertexBufferClass(BUFFER_TYPE_DX8, fvf, count, 0) {}
DX8VertexBufferClass::~DX8VertexBufferClass() {}

// DX8IndexBufferClass
DX8IndexBufferClass::DX8IndexBufferClass(unsigned short count, UsageType)
    : IndexBufferClass(BUFFER_TYPE_DX8, count) {}
DX8IndexBufferClass::~DX8IndexBufferClass() {}

// DX8Wrapper stubs — never called
void DX8Wrapper::Set_Vertex_Buffer(const VertexBufferClass*, unsigned int) {}
void DX8Wrapper::Set_Index_Buffer(const IndexBufferClass*, unsigned short) {}
void DX8Wrapper::Draw_Triangles(unsigned short, unsigned short, unsigned short, unsigned short) {}

RenderStateStruct DX8Wrapper::render_state = {};
unsigned int DX8Wrapper::render_state_changed = 0;

// BaseHeightMapRenderObjClass — terrain height for road geometry.
// In the D3D11 port, TheTerrainRenderObject may be null when roads load.
// Provide a global fallback heightmap pointer set by BuildRoadMesh.
WorldHeightMap* g_roadBuildHeightMap = nullptr;

Real BaseHeightMapRenderObjClass::getMaxCellHeight(Real x, Real y) const
{
    // 'this' may be null if TheTerrainRenderObject hasn't been created yet
    if (this && m_map) return m_map->getHeight(x, y);
    if (g_roadBuildHeightMap) return g_roadBuildHeightMap->getHeight(x, y);
    return 0.0f;
}

Int BaseHeightMapRenderObjClass::getStaticDiffuse(Int, Int)
{
    return (Int)0xFFFFFFFF; // full white — D3D11 shader handles lighting
}
