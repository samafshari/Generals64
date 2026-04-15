// TerrainTextureClass stubs - D3D8 terrain texturing replaced by D3D11.
#include "texture.h"
#include "W3DDevice/GameClient/TerrainTex.h"
class WorldHeightMap;

TerrainTextureClass::TerrainTextureClass(int h) : TextureClass((IDirect3DBaseTexture8*)nullptr) {}
TerrainTextureClass::TerrainTextureClass(int h, int w) : TextureClass((IDirect3DBaseTexture8*)nullptr) {}
int TerrainTextureClass::update(WorldHeightMap*) { return 0; }
bool TerrainTextureClass::updateFlat(WorldHeightMap*, int, int, int, int) { return false; }
void TerrainTextureClass::setLOD(int) {}
void TerrainTextureClass::Apply(unsigned int) {}

AlphaTerrainTextureClass::AlphaTerrainTextureClass(TextureClass* p) : TextureClass((IDirect3DBaseTexture8*)nullptr) {}
void AlphaTerrainTextureClass::Apply(unsigned int) {}

AlphaEdgeTextureClass::AlphaEdgeTextureClass(int h, MipCountType m) : TextureClass((IDirect3DBaseTexture8*)nullptr) {}
int AlphaEdgeTextureClass::update(WorldHeightMap*) { return 0; }
void AlphaEdgeTextureClass::Apply(unsigned int) {}

LightMapTerrainTextureClass::LightMapTerrainTextureClass(AsciiString n, MipCountType m) : TextureClass((IDirect3DBaseTexture8*)nullptr) {}
void LightMapTerrainTextureClass::Apply(unsigned int) {}

ScorchTextureClass::ScorchTextureClass(MipCountType m) : TextureClass((IDirect3DBaseTexture8*)nullptr) {}
void ScorchTextureClass::Apply(unsigned int) {}

CloudMapTerrainTextureClass::CloudMapTerrainTextureClass(MipCountType m) : TextureClass((IDirect3DBaseTexture8*)nullptr) {}
void CloudMapTerrainTextureClass::Apply(unsigned int) {}
