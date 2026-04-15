#include "W3DDevice/GameClient/TerrainRenderer.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/ImageCache.h"
#include "W3DDevice/GameClient/ModelRenderer.h"
#include "W3DDevice/GameClient/TileData.h"
#include "GameClient/Water.h"
#include "GameClient/TerrainRoads.h"
#include "Common/GlobalData.h"
#include "Common/MapObject.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/TerrainLogic.h"
#include "W3DDevice/GameClient/RoadGeometry.h"
#include "WW3D2/camera.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/ww3d.h"
#include "WWMath/matrix3d.h"
#include "WWMath/matrix4.h"
#include "RenderUtils.h"

#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <unordered_map>

using Render::ToXM;

// Defined in D3D11Shims.cpp - creates a W3D render object from the asset manager
extern WorldHeightMap* GetTerrainHeightMap(); // defined in D3D11Shims.cpp
extern RenderObjClass* CreateRenderObjCompat(const char* name, float scale, int color,
    const char* oldTexture, const char* newTexture);
// Walks W3DDisplay::m_3DScene + GameClient drawable list and submits MESH/HLOD
// render objects to ModelRenderer for the water reflection RT pass. Caller
// must have already bound the reflected camera + SetReflectionMesh3DState.
extern int RenderReflectionMeshesDX11();

// ============================================================================
// FrustumPlanes - view frustum extraction and culling
// ============================================================================

void FrustumPlanes::ExtractFromViewProj(const Render::Float4x4& m)
{
    // Gribb-Hartmann method: extract 6 planes from view-projection matrix
    // Left
    planes[0] = { m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41 };
    // Right
    planes[1] = { m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41 };
    // Bottom
    planes[2] = { m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42 };
    // Top
    planes[3] = { m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42 };
    // Near
    planes[4] = { m._13, m._23, m._33, m._43 };
    // Far
    planes[5] = { m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43 };

    // Normalize each plane
    for (int i = 0; i < 6; ++i)
    {
        float len = sqrtf(planes[i].x * planes[i].x + planes[i].y * planes[i].y + planes[i].z * planes[i].z);
        if (len > 0.0001f)
        {
            planes[i].x /= len; planes[i].y /= len; planes[i].z /= len; planes[i].w /= len;
        }
    }
}

bool FrustumPlanes::TestAABB(const Render::Float3& aabbMin, const Render::Float3& aabbMax) const
{
    for (int i = 0; i < 6; ++i)
    {
        // P-vertex: the corner most in the direction of the plane normal
        float px = planes[i].x >= 0 ? aabbMax.x : aabbMin.x;
        float py = planes[i].y >= 0 ? aabbMax.y : aabbMin.y;
        float pz = planes[i].z >= 0 ? aabbMax.z : aabbMin.z;
        if (planes[i].x * px + planes[i].y * py + planes[i].z * pz + planes[i].w < 0)
            return false;
    }
    return true;
}

bool FrustumPlanes::TestSphere(const Render::Float3& center, float radius) const
{
    for (int i = 0; i < 6; ++i)
    {
        float dist = planes[i].x * center.x + planes[i].y * center.y + planes[i].z * center.z + planes[i].w;
        if (dist < -radius)
            return false;
    }
    return true;
}

void ExtractFrustumFromCamera(CameraClass* camera, FrustumPlanes& out)
{
    if (!camera)
        return;

    Matrix3D camViewMtx;
    camera->Get_View_Matrix(&camViewMtx);
    RenderUtils::SanitizeMatrix3D(camViewMtx);

    Matrix4x4 camProjMtx;
    camera->Get_D3D_Projection_Matrix(&camProjMtx);

    // Matrix3DToFloat4x4 bakes the transpose, producing row-major VP matching the shader
    Render::Float4x4 view = RenderUtils::Matrix3DToFloat4x4(camViewMtx);
    Render::Float4x4 proj = RenderUtils::Matrix4x4ToFloat4x4(camProjMtx);

    DirectX::XMMATRIX xmVP = DirectX::XMMatrixMultiply(
        DirectX::XMLoadFloat4x4(&ToXM(view)), DirectX::XMLoadFloat4x4(&ToXM(proj)));
    Render::Float4x4 viewProj;
    DirectX::XMStoreFloat4x4(&ToXM(viewProj), xmVP);

    // For row-major VP (clip = v * VP), the Gribb-Hartmann planes use COLUMNS:
    // Left:   col4 + col1
    // Right:  col4 - col1
    // Bottom: col4 + col2
    // Top:    col4 - col2
    // Near:   col3
    // Far:    col4 - col3
    // In row-major XMFLOAT4X4, column j is: (_1j, _2j, _3j, _4j)
    auto& m = viewProj;
    out.planes[0] = { m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41 }; // left
    out.planes[1] = { m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41 }; // right
    out.planes[2] = { m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42 }; // bottom
    out.planes[3] = { m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42 }; // top
    out.planes[4] = { m._13,         m._23,          m._33,          m._43 };          // near
    out.planes[5] = { m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43 }; // far

    // Normalize
    for (int i = 0; i < 6; ++i)
    {
        float len = sqrtf(out.planes[i].x * out.planes[i].x +
                          out.planes[i].y * out.planes[i].y +
                          out.planes[i].z * out.planes[i].z);
        if (len > 0.0001f)
        {
            out.planes[i].x /= len;
            out.planes[i].y /= len;
            out.planes[i].z /= len;
            out.planes[i].w /= len;
        }
    }
}

static const float TERRAIN_XY_FACTOR    = 10.0f;
static const float TERRAIN_HEIGHT_SCALE = TERRAIN_XY_FACTOR / 16.0f;
static const float WATER_SURFACE_OFFSET = 0.25f;
static const Render::Float3 kFallbackLightDirection = { -0.3f, -0.2f, -0.8f };
static const Render::Float4 kFallbackLightColor = { 1.0f, 0.95f, 0.85f, 1.0f };
static const Render::Float4 kFallbackAmbientColor = { 0.35f, 0.35f, 0.40f, 1.0f };

static void AppendTerrainTrace(const char* format, ...)
{
    return; // Debug logging removed
}

namespace Render
{

TerrainRenderer& TerrainRenderer::Instance()
{
    static TerrainRenderer s_instance;
    return s_instance;
}

void TerrainRenderer::BuildTerrainTextureAtlas(WorldHeightMap* heightMap)
{
    if (m_textureCreated || !heightMap)
        return;

    int numTiles = 0;
    for (int i = 0; i < NUM_SOURCE_TILES; ++i)
    {
        if (heightMap->getSourceTile(i) != nullptr)
            ++numTiles;
        else
            break;
    }

    if (numTiles == 0)
    {
        uint32_t pixels[4] = { 0xFF3A6B28, 0xFF4A7A34, 0xFF5A6B3A, 0xFF6B7A48 };
        m_terrainTexture.CreateFromRGBA(Renderer::Instance().GetDevice(), pixels, 2, 2, true);
        m_textureCreated = true;
        m_atlasWidth = 2;
        m_atlasHeight = 2;
        AppendTerrainTrace("TerrainRenderer::BuildTerrainTextureAtlas fallback (no tiles)\n");
        return;
    }

    // Call updateTileTexturePositions to assign tile atlas positions.
    // This sets m_tileLocationInTexture on each TileData.
    Int edgeHeight = 0;
    Int atlasHeight = heightMap->updateTileTexturePositions(&edgeHeight);
    if (atlasHeight <= 0)
        atlasHeight = 512;

    // Set m_terrainTexHeight so getUVForNdx() normalizes V coordinates correctly.
    heightMap->m_terrainTexHeight = atlasHeight;

    m_atlasWidth = TEXTURE_WIDTH;
    m_atlasHeight = atlasHeight;

    std::vector<uint32_t> atlasPixels(m_atlasWidth * m_atlasHeight, 0xFF204020);

    int tilesPlaced = 0;
    for (int i = 0; i < numTiles; ++i)
    {
        TileData* tile = heightMap->getSourceTile(i);
        if (!tile)
            continue;

        ICoord2D pos = tile->m_tileLocationInTexture;
        if (pos.x < 0 || pos.y < 0 ||
            pos.x + TILE_PIXEL_EXTENT > m_atlasWidth ||
            pos.y + TILE_PIXEL_EXTENT > m_atlasHeight)
            continue;

        const uint8_t* src = tile->getDataPtr();
        for (int ty = 0; ty < TILE_PIXEL_EXTENT; ++ty)
        {
            // TileData is bottom-up
            int srcRow = (TILE_PIXEL_EXTENT - 1 - ty);
            int dstRow = pos.y + ty;
            if (dstRow >= m_atlasHeight) continue;

            for (int tx = 0; tx < TILE_PIXEL_EXTENT; ++tx)
            {
                int srcIdx = (srcRow * TILE_PIXEL_EXTENT + tx) * TILE_BYTES_PER_PIXEL;
                uint8_t b = src[srcIdx + 0];
                uint8_t g = src[srcIdx + 1];
                uint8_t r = src[srcIdx + 2];
                uint8_t a = src[srcIdx + 3];

                int dstX = pos.x + tx;
                if (dstX >= m_atlasWidth) continue;

                // ABGR for D3D11 R8G8B8A8_UNORM
                atlasPixels[dstRow * m_atlasWidth + dstX] = (a << 24) | (b << 16) | (g << 8) | r;
            }
        }
        ++tilesPlaced;
    }

    // Replicate a 4-pixel wrap-around border around every texture class.
    // updateTileTexturePositions reserves a 4-pixel gutter between class
    // groups (TILE_OFFSET=8 → 4px on each side). Without filling that
    // gutter with valid edge content, two artifacts appear:
    //   1. Bilinear filtering at the edge of a tile picks up the gutter
    //      fill color and produces dark seams between tiles.
    //   2. GenerateMips averages the gutter into the mip chain. At mip
    //      level 3+ each tile's content shrinks to <=8 texels and the
    //      gutter dominates, producing the literal checkerboard pattern
    //      (alternating tile-color / gutter-color cells) seen at normal
    //      camera distances.
    // The original DX8 TerrainTextureClass::update did this exact wrap;
    // it was lost when the upload moved into the new D3D11 path.
    auto wrapClassBorder = [&](int ox, int oy, int classW)
    {
        // Step 1: left/right column wrap-around for every row in the class.
        // Each border column becomes a copy of a column inside the class
        // such that bilinear sampling across the class edge sees the
        // opposite edge of the same class.
        for (int j = 0; j < classW; ++j)
        {
            const int row = oy + j;
            if (row < 0 || row >= m_atlasHeight) continue;
            uint32_t* line = &atlasPixels[row * m_atlasWidth];
            for (int k = 0; k < 4; ++k)
            {
                // Left gutter: pixels (ox-4..ox-1) ← (ox+classW-4..ox+classW-1)
                const int leftCol = ox - 4 + k;
                const int srcRight = ox + classW - 4 + k;
                if (leftCol >= 0 && srcRight >= 0 && srcRight < m_atlasWidth)
                    line[leftCol] = line[srcRight];
                // Right gutter: pixels (ox+classW..ox+classW+3) ← (ox..ox+3)
                const int rightCol = ox + classW + k;
                const int srcLeft = ox + k;
                if (rightCol < m_atlasWidth && srcLeft >= 0)
                    line[rightCol] = line[srcLeft];
            }
        }

        // Step 2: top/bottom row wrap. Done after columns so the corner
        // pixels (gutter intersections) inherit the already-wrapped side
        // borders for free.
        for (int k = 0; k < 4; ++k)
        {
            const int topRow      = oy - 4 + k;
            const int srcBotRow   = oy + classW - 4 + k;
            const int botRow      = oy + classW + k;
            const int srcTopRow   = oy + k;

            for (int x = ox - 4; x < ox + classW + 4; ++x)
            {
                if (x < 0 || x >= m_atlasWidth) continue;

                if (topRow >= 0 && srcBotRow >= 0 && srcBotRow < m_atlasHeight)
                    atlasPixels[topRow * m_atlasWidth + x] =
                        atlasPixels[srcBotRow * m_atlasWidth + x];

                if (botRow < m_atlasHeight && srcTopRow >= 0 && srcTopRow < m_atlasHeight)
                    atlasPixels[botRow * m_atlasWidth + x] =
                        atlasPixels[srcTopRow * m_atlasWidth + x];
            }
        }
    };

    int classBordersWritten = 0;
    for (int texClass = 0; texClass < heightMap->m_numTextureClasses; ++texClass)
    {
        const int classW = heightMap->m_textureClasses[texClass].width * TILE_PIXEL_EXTENT;
        const int ox = heightMap->m_textureClasses[texClass].positionInTexture.x;
        const int oy = heightMap->m_textureClasses[texClass].positionInTexture.y;
        if (ox <= 0 || oy <= 0 || classW <= 0) continue;
        if (ox + classW > m_atlasWidth || oy + classW > m_atlasHeight) continue;
        wrapClassBorder(ox, oy, classW);
        ++classBordersWritten;
    }

    m_terrainTexture.CreateFromRGBA(
        Renderer::Instance().GetDevice(),
        atlasPixels.data(), m_atlasWidth, m_atlasHeight, true);
    m_textureCreated = true;

    AppendTerrainTrace(
        "TerrainRenderer::BuildTerrainTextureAtlas tiles=%d placed=%d atlas=%dx%d texHeight=%d classBorders=%d\n",
        numTiles, tilesPlaced, m_atlasWidth, m_atlasHeight, atlasHeight, classBordersWritten);
}

void TerrainRenderer::BuildEdgeTextureAtlas(WorldHeightMap* heightMap)
{
    if (m_edgingTextureCreated || !heightMap)
        return;

    // Count edge tiles
    int numEdgeTiles = heightMap->m_numEdgeTiles;
    if (numEdgeTiles <= 0)
    {
        AppendTerrainTrace("TerrainRenderer::BuildEdgeTextureAtlas no edge tiles\n");
        return;
    }

    // updateTileTexturePositions was already called by BuildTerrainTextureAtlas,
    // so m_edgeTiles already have their atlas positions assigned.
    // Determine the edge atlas dimensions.
    int edgeAtlasHeight = 0;
    for (int i = 0; i < numEdgeTiles; ++i)
    {
        TileData* tile = heightMap->getEdgeTile(i);
        if (!tile) continue;
        ICoord2D pos = tile->m_tileLocationInTexture;
        if (pos.x <= 0) continue;  // all real edge offsets start at 4+
        int bottom = pos.y + TILE_PIXEL_EXTENT;
        if (bottom > edgeAtlasHeight)
            edgeAtlasHeight = bottom;
    }

    if (edgeAtlasHeight <= 0)
    {
        AppendTerrainTrace("TerrainRenderer::BuildEdgeTextureAtlas edge tiles have no positions\n");
        return;
    }

    // Round up to power of 2
    int pow2Height = 1;
    while (pow2Height < edgeAtlasHeight)
        pow2Height *= 2;

    m_edgingAtlasWidth = TEXTURE_WIDTH;
    m_edgingAtlasHeight = pow2Height;

    // Build the edge texture: RGBA, with alpha encoding per the original algorithm:
    //   black pixels (0,0,0) -> alpha=0x80 (shows base terrain through)
    //   white pixels (255,255,255) -> alpha=0x00 (fully transparent)
    //   other pixels -> alpha=0xFF (shows edge tile color)
    std::vector<uint32_t> edgePixels(m_edgingAtlasWidth * m_edgingAtlasHeight, 0x80808080);

    int tilesPlaced = 0;
    for (int tileNdx = 0; tileNdx < numEdgeTiles; ++tileNdx)
    {
        TileData* tile = heightMap->getEdgeTile(tileNdx);
        if (!tile) continue;
        ICoord2D pos = tile->m_tileLocationInTexture;
        if (pos.x <= 0) continue;

        const uint8_t* src = tile->getDataPtr();
        if (!src) continue;

        for (int ty = 0; ty < TILE_PIXEL_EXTENT; ++ty)
        {
            // Edge tile data is bottom-up, atlas is top-down
            int srcRow = (TILE_PIXEL_EXTENT - 1 - ty);
            int dstRow = pos.y + ty;
            if (dstRow >= m_edgingAtlasHeight) continue;

            for (int tx = 0; tx < TILE_PIXEL_EXTENT; ++tx)
            {
                int srcIdx = (srcRow * TILE_PIXEL_EXTENT + tx) * TILE_BYTES_PER_PIXEL;
                uint8_t b = src[srcIdx + 0];
                uint8_t g = src[srcIdx + 1];
                uint8_t r = src[srcIdx + 2];

                int dstX = pos.x + tx;
                if (dstX >= m_edgingAtlasWidth) continue;

                // Determine alpha per the original AlphaEdgeTextureClass encoding
                uint8_t a;
                if (r == 0 && g == 0 && b == 0)
                    a = 0x80;   // black: base terrain shows through at half alpha
                else if (r == 0xFF && g == 0xFF && b == 0xFF)
                    a = 0x00;   // white: fully transparent (no edge here)
                else
                    a = 0xFF;   // colored: edge tile artwork is fully visible

                // ABGR for D3D11 R8G8B8A8_UNORM
                edgePixels[dstRow * m_edgingAtlasWidth + dstX] =
                    (static_cast<uint32_t>(a) << 24) |
                    (static_cast<uint32_t>(b) << 16) |
                    (static_cast<uint32_t>(g) << 8) |
                    r;
            }
        }
        ++tilesPlaced;
    }

    m_edgingTexture.CreateFromRGBA(
        Renderer::Instance().GetDevice(),
        edgePixels.data(), m_edgingAtlasWidth, m_edgingAtlasHeight, true);
    m_edgingTextureCreated = true;

    // Store the actual height for UV normalization (matching original m_alphaEdgeHeight)
    // The actual allocated height is pow2Height but UVs normalize against it.
    heightMap->m_alphaEdgeHeight = m_edgingAtlasHeight;

    AppendTerrainTrace(
        "TerrainRenderer::BuildEdgeTextureAtlas edgeTiles=%d placed=%d atlas=%dx%d\n",
        numEdgeTiles, tilesPlaced, m_edgingAtlasWidth, m_edgingAtlasHeight);
}

static void computeNormal(const unsigned char* data, int x, int y, int w, int h, float* nx, float* ny, float* nz)
{
    int idx = y * w + x;
    float height = (float)data[idx];
    float hL = (x > 0) ? (float)data[idx - 1] : height;
    float hR = (x < w - 1) ? (float)data[idx + 1] : height;
    float hD = (y > 0) ? (float)data[idx - w] : height;
    float hU = (y < h - 1) ? (float)data[idx + w] : height;

    float dzdx = (hR - hL) * TERRAIN_HEIGHT_SCALE / (2.0f * TERRAIN_XY_FACTOR);
    float dzdy = (hU - hD) * TERRAIN_HEIGHT_SCALE / (2.0f * TERRAIN_XY_FACTOR);

    float nnx = -dzdx, nny = -dzdy, nnz = 1.0f;
    float len = sqrtf(nnx * nnx + nny * nny + nnz * nnz);
    *nx = nnx / len;
    *ny = nny / len;
    *nz = nnz / len;
}

static uint32_t PackTerrainColor(UnsignedByte alpha = 255)
{
    return (static_cast<uint32_t>(alpha) << 24) | 0x00FFFFFF;
}

static bool HasVisibleBlend(const UnsignedByte alpha[4])
{
    return alpha[0] != 0 || alpha[1] != 0 || alpha[2] != 0 || alpha[3] != 0;
}

static void AppendTerrainQuad(
    std::vector<Render::Vertex3D>& vertices,
    std::vector<uint32_t>& indices,
    float px0,
    float px1,
    float py0,
    float py1,
    float h00,
    float h10,
    float h01,
    float h11,
    const Render::Float3 normals[4],
    const float U[4],
    const float V[4],
    const UnsignedByte alpha[4],
    bool flip)
{
    const uint32_t baseIndex = static_cast<uint32_t>(vertices.size());
    vertices.resize(baseIndex + 4);

    vertices[baseIndex + 0].position = { px0, py0, h00 };
    vertices[baseIndex + 0].normal = normals[0];
    vertices[baseIndex + 0].texcoord = { U[0], V[0] };
    vertices[baseIndex + 0].color = PackTerrainColor(alpha[0]);

    vertices[baseIndex + 1].position = { px1, py0, h10 };
    vertices[baseIndex + 1].normal = normals[1];
    vertices[baseIndex + 1].texcoord = { U[1], V[1] };
    vertices[baseIndex + 1].color = PackTerrainColor(alpha[1]);

    vertices[baseIndex + 2].position = { px0, py1, h01 };
    vertices[baseIndex + 2].normal = normals[2];
    vertices[baseIndex + 2].texcoord = { U[3], V[3] };
    vertices[baseIndex + 2].color = PackTerrainColor(alpha[3]);

    vertices[baseIndex + 3].position = { px1, py1, h11 };
    vertices[baseIndex + 3].normal = normals[3];
    vertices[baseIndex + 3].texcoord = { U[2], V[2] };
    vertices[baseIndex + 3].color = PackTerrainColor(alpha[2]);

    if (flip)
    {
        indices.push_back(baseIndex + 0);
        indices.push_back(baseIndex + 1);
        indices.push_back(baseIndex + 3);
        indices.push_back(baseIndex + 0);
        indices.push_back(baseIndex + 3);
        indices.push_back(baseIndex + 2);
    }
    else
    {
        indices.push_back(baseIndex + 0);
        indices.push_back(baseIndex + 1);
        indices.push_back(baseIndex + 2);
        indices.push_back(baseIndex + 1);
        indices.push_back(baseIndex + 3);
        indices.push_back(baseIndex + 2);
    }
}

static void AppendTerrainMaskedQuad(
    std::vector<Render::Vertex3DMasked>& vertices,
    std::vector<uint32_t>& indices,
    float px0,
    float px1,
    float py0,
    float py1,
    float h00,
    float h10,
    float h01,
    float h11,
    const Render::Float3 normals[4],
    const float baseU[4],
    const float baseV[4],
    const float maskU[4],
    const float maskV[4],
    bool flip)
{
    const uint32_t baseIndex = static_cast<uint32_t>(vertices.size());
    vertices.resize(baseIndex + 4);

    vertices[baseIndex + 0].position = { px0, py0, h00 };
    vertices[baseIndex + 0].normal = normals[0];
    vertices[baseIndex + 0].texcoord0 = { baseU[0], baseV[0] };
    vertices[baseIndex + 0].texcoord1 = { maskU[0], maskV[0] };
    vertices[baseIndex + 0].color = PackTerrainColor();

    vertices[baseIndex + 1].position = { px1, py0, h10 };
    vertices[baseIndex + 1].normal = normals[1];
    vertices[baseIndex + 1].texcoord0 = { baseU[1], baseV[1] };
    vertices[baseIndex + 1].texcoord1 = { maskU[1], maskV[1] };
    vertices[baseIndex + 1].color = PackTerrainColor();

    vertices[baseIndex + 2].position = { px0, py1, h01 };
    vertices[baseIndex + 2].normal = normals[2];
    vertices[baseIndex + 2].texcoord0 = { baseU[3], baseV[3] };
    vertices[baseIndex + 2].texcoord1 = { maskU[3], maskV[3] };
    vertices[baseIndex + 2].color = PackTerrainColor();

    vertices[baseIndex + 3].position = { px1, py1, h11 };
    vertices[baseIndex + 3].normal = normals[3];
    vertices[baseIndex + 3].texcoord0 = { baseU[2], baseV[2] };
    vertices[baseIndex + 3].texcoord1 = { maskU[2], maskV[2] };
    vertices[baseIndex + 3].color = PackTerrainColor();

    if (flip)
    {
        indices.push_back(baseIndex + 0);
        indices.push_back(baseIndex + 1);
        indices.push_back(baseIndex + 3);
        indices.push_back(baseIndex + 0);
        indices.push_back(baseIndex + 3);
        indices.push_back(baseIndex + 2);
    }
    else
    {
        indices.push_back(baseIndex + 0);
        indices.push_back(baseIndex + 1);
        indices.push_back(baseIndex + 2);
        indices.push_back(baseIndex + 1);
        indices.push_back(baseIndex + 3);
        indices.push_back(baseIndex + 2);
    }
}

static bool HasLightingData(const GlobalData::TerrainLighting& light)
{
    constexpr float epsilon = 0.0001f;
    return
        std::fabs(light.ambient.red) > epsilon ||
        std::fabs(light.ambient.green) > epsilon ||
        std::fabs(light.ambient.blue) > epsilon ||
        std::fabs(light.diffuse.red) > epsilon ||
        std::fabs(light.diffuse.green) > epsilon ||
        std::fabs(light.diffuse.blue) > epsilon ||
        std::fabs(light.lightPos.x) > epsilon ||
        std::fabs(light.lightPos.y) > epsilon ||
        std::fabs(light.lightPos.z) > epsilon;
}

static bool HasDirectionalLight(const GlobalData::TerrainLighting& light)
{
    constexpr float epsilon = 0.0001f;
    const float diffuseMagnitude =
        std::fabs(light.diffuse.red) +
        std::fabs(light.diffuse.green) +
        std::fabs(light.diffuse.blue);
    const float directionMagnitude =
        std::fabs(light.lightPos.x) +
        std::fabs(light.lightPos.y) +
        std::fabs(light.lightPos.z);
    return diffuseMagnitude > epsilon && directionMagnitude > epsilon;
}

static void ApplyTerrainLighting(Render::Renderer& renderer)
{
    // Inspector Lights panel is editing values live — don't clobber.
    if (renderer.LightsOverridden())
        return;

    if (!TheGlobalData)
    {
        renderer.SetSunLight(kFallbackLightDirection, kFallbackLightColor);
        renderer.SetAmbientLight(kFallbackAmbientColor);
        return;
    }

    const TimeOfDay timeOfDay = TheGlobalData->m_timeOfDay;
    const GlobalData::TerrainLighting* lights = TheGlobalData->m_terrainLighting[timeOfDay];

    Render::Float3 directions[Render::kMaxDirectionalLights] = {};
    Render::Float4 colors[Render::kMaxDirectionalLights] = {};
    Render::Float4 ambient = { 0.0f, 0.0f, 0.0f, 1.0f };
    uint32_t lightCount = 0;
    bool hasAnyLightingData = false;

    for (int i = 0; i < MAX_GLOBAL_LIGHTS && lightCount < Render::kMaxDirectionalLights; ++i)
    {
        const GlobalData::TerrainLighting& light = lights[i];
        hasAnyLightingData = hasAnyLightingData || HasLightingData(light);

        ambient.x += light.ambient.red;
        ambient.y += light.ambient.green;
        ambient.z += light.ambient.blue;

        if (!HasDirectionalLight(light))
            continue;

        directions[lightCount] = { light.lightPos.x, light.lightPos.y, light.lightPos.z };
        colors[lightCount] = { light.diffuse.red, light.diffuse.green, light.diffuse.blue, 1.0f };
        ++lightCount;
    }

    if (!hasAnyLightingData)
    {
        renderer.SetSunLight(kFallbackLightDirection, kFallbackLightColor);
        renderer.SetAmbientLight(kFallbackAmbientColor);
        return;
    }

    ambient.x = std::max(0.0f, ambient.x);
    ambient.y = std::max(0.0f, ambient.y);
    ambient.z = std::max(0.0f, ambient.z);
    renderer.SetDirectionalLights(directions, colors, lightCount);
    renderer.SetAmbientLight(ambient);
}

static bool ApplyW3DCamera(Render::Renderer& renderer, CameraClass* camera, Render::Float3* cameraPosOut = nullptr)
{
    if (camera == nullptr)
        return false;

    Matrix3D camTransform;
    camera->Get_View_Matrix(&camTransform);
    RenderUtils::SanitizeMatrix3D(camTransform);

    Matrix4x4 projMatrix4x4;
    camera->Get_D3D_Projection_Matrix(&projMatrix4x4);

    Render::Float4x4 viewMatrix = RenderUtils::Matrix3DToFloat4x4(camTransform);
    Render::Float4x4 projMatrix = RenderUtils::Matrix4x4ToFloat4x4(projMatrix4x4);

    const Vector3 camPos = camera->Get_Position();
    const Render::Float3 cameraPos = { camPos.X, camPos.Y, camPos.Z };

    renderer.SetCamera(viewMatrix, projMatrix, cameraPos);
    if (cameraPosOut != nullptr)
        *cameraPosOut = cameraPos;

    // Apply the camera's viewport as a D3D11 viewport.
    {
        Vector2 vpMin, vpMax;
        camera->Get_Viewport(vpMin, vpMax);
        float zMin = 0.0f, zMax = 1.0f;
        camera->Get_Depth_Range(&zMin, &zMax);

        float rtW = (float)renderer.GetWidth();
        float rtH = (float)renderer.GetHeight();

        float vpX = vpMin.X * rtW;
        float vpY = vpMin.Y * rtH;
        float vpW = (vpMax.X - vpMin.X) * rtW;
        float vpH = (vpMax.Y - vpMin.Y) * rtH;

        renderer.SetViewport(vpX, vpY, vpW, vpH, zMin, zMax);
    }

    return true;
}

static Render::Float4 ComputeWaterTint()
{
    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
    float alpha = 0.5f;

    const TimeOfDay timeOfDay = TheGlobalData ? TheGlobalData->m_timeOfDay : TIME_OF_DAY_MORNING;
    const WaterSetting& waterSetting = WaterSettings[timeOfDay];
    const RGBAColorInt* diffuse = &waterSetting.m_transparentWaterDiffuse;

    if (diffuse->red == 0 && diffuse->green == 0 && diffuse->blue == 0 && diffuse->alpha == 0)
        diffuse = &waterSetting.m_waterDiffuseColor;

    if (diffuse->red != 0 || diffuse->green != 0 || diffuse->blue != 0)
    {
        red = diffuse->red / 255.0f;
        green = diffuse->green / 255.0f;
        blue = diffuse->blue / 255.0f;
    }

    if (diffuse->alpha != 0)
        alpha = diffuse->alpha / 255.0f;

    if (TheWaterTransparency != nullptr)
    {
        red *= TheWaterTransparency->m_standingWaterColor.red;
        green *= TheWaterTransparency->m_standingWaterColor.green;
        blue *= TheWaterTransparency->m_standingWaterColor.blue;
    }

    return {
        std::clamp(red, 0.0f, 1.0f),
        std::clamp(green, 0.0f, 1.0f),
        std::clamp(blue, 0.0f, 1.0f),
        std::clamp(alpha, 0.0f, 1.0f)
    };
}

void TerrainRenderer::BuildMesh(WorldHeightMap* heightMap)
{
    if (!heightMap)
        return;

    const int oldMapWidth = m_mapWidth;
    const int oldMapHeight = m_mapHeight;
    const int oldBorderSize = m_borderSize;

    m_mapWidth  = heightMap->getXExtent();
    m_mapHeight = heightMap->getYExtent();

    if (m_mapWidth < 2 || m_mapHeight < 2)
        return;

    int borderSize = heightMap->getBorderSize();
    m_borderSize = borderSize;
    if (TheGlobalData)
        m_borderShroudLevel = TheGlobalData->m_shroudAlpha;

    // Terrain geometry changes invalidate shroud texture/grid sizing.
    if (oldMapWidth != m_mapWidth || oldMapHeight != m_mapHeight || oldBorderSize != m_borderSize)
    {
        m_shroudGrid.clear();
        m_shroudWidth = 0;
        m_shroudHeight = 0;
        m_shroudTextureReady = false;
        m_shroudTexture = Texture();
    }

    // The D3D11 renderer generates geometry for the ENTIRE map using absolute
    // cell indices. But getUVData/getAlphaUVData add m_drawOriginX/Y internally
    // (they expect draw-origin-relative coordinates from the old HeightMap renderer).
    // Reset the draw origin to 0 so absolute indices work correctly.
    heightMap->setDrawOrg(0, 0);

    const unsigned char* data = heightMap->getDataPtr();
    if (!data)
        return;

    // terrain_heights.log diagnostic removed

    AppendTerrainTrace(
        "TerrainRenderer::BuildMesh map=%dx%d border=%d tile0=%p\n",
        m_mapWidth, m_mapHeight, borderSize, heightMap->getSourceTile(0));

    // Try to build the terrain texture atlas now if tiles are available
    if (m_atlasWidth <= 2 && heightMap->getSourceTile(0) != nullptr)
    {
        m_textureCreated = false;
        BuildTerrainTextureAtlas(heightMap);
    }

    // Build the edge texture atlas for custom edging tiles
    if (!m_edgingTextureCreated && heightMap->m_numEdgeTiles > 0)
    {
        BuildEdgeTextureAtlas(heightMap);
    }

    bool hasUVData = m_textureCreated && m_atlasWidth > 2 && heightMap->getSourceTile(0) != nullptr;

    int cellsX = m_mapWidth - 1;
    int cellsY = m_mapHeight - 1;
    const uint32_t totalVerts = static_cast<uint32_t>(cellsX * cellsY * 4);
    const uint32_t totalIndices = static_cast<uint32_t>(cellsX * cellsY * 6);

    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    std::vector<Vertex3D> blendVertices;
    std::vector<uint32_t> blendIndices;
    std::vector<Vertex3D> extraBlendVertices;
    std::vector<uint32_t> extraBlendIndices;
    std::vector<Vertex3DMasked> edgingBaseVertices;
    std::vector<uint32_t> edgingBaseIndices;
    std::vector<Vertex3D> edgingVertices;
    std::vector<uint32_t> edgingIndices;

    vertices.reserve(totalVerts);
    indices.reserve(totalIndices);
    blendVertices.reserve(totalVerts / 4);
    blendIndices.reserve(totalIndices / 4);
    extraBlendVertices.reserve(totalVerts / 8);
    extraBlendIndices.reserve(totalIndices / 8);
    edgingBaseVertices.reserve(totalVerts / 8);
    edgingBaseIndices.reserve(totalIndices / 8);
    edgingVertices.reserve(totalVerts / 8);
    edgingIndices.reserve(totalIndices / 8);

    int uvHits = 0;
    int uvMisses = 0;
    int blendCells = 0;
    int extraBlendCells = 0;
    int edgingCells = 0;
    int flippedCells = 0;

    for (int cy = 0; cy < cellsY; cy++)
    {
        for (int cx = 0; cx < cellsX; cx++)
        {
            int x0 = cx, x1 = cx + 1;
            int y0 = cy, y1 = cy + 1;

            float px0 = (float)(x0 - borderSize) * TERRAIN_XY_FACTOR;
            float px1 = (float)(x1 - borderSize) * TERRAIN_XY_FACTOR;
            float py0 = (float)(y0 - borderSize) * TERRAIN_XY_FACTOR;
            float py1 = (float)(y1 - borderSize) * TERRAIN_XY_FACTOR;

            float h00 = (float)data[y0 * m_mapWidth + x0] * TERRAIN_HEIGHT_SCALE;
            float h10 = (float)data[y0 * m_mapWidth + x1] * TERRAIN_HEIGHT_SCALE;
            float h01 = (float)data[y1 * m_mapWidth + x0] * TERRAIN_HEIGHT_SCALE;
            float h11 = (float)data[y1 * m_mapWidth + x1] * TERRAIN_HEIGHT_SCALE;

            float baseU[4] = {};
            float baseV[4] = {};
            float blendU[4] = {};
            float blendV[4] = {};
            float extraU[4] = {};
            float extraV[4] = {};
            UnsignedByte baseAlpha[4] = { 255, 255, 255, 255 };
            UnsignedByte blendAlpha[4] = {};
            UnsignedByte extraAlpha[4] = {};
            bool flipForBlend = false;
            bool extraFlip = false;
            bool extraCliff = false;

            if (hasUVData)
            {
                heightMap->getUVData(cx, cy, baseU, baseV);
                heightMap->getAlphaUVData(cx, cy, blendU, blendV, blendAlpha, &flipForBlend);
                const bool hasExtraBlend = heightMap->getExtraAlphaUVData(
                    cx, cy, extraU, extraV, extraAlpha, &extraFlip, &extraCliff);

                // Cliff-forced diagonal flip: when the terrain has a cliff edge,
                // choose the diagonal that minimizes height discontinuity across
                // the triangle split. Original: if (cliffState && |h00-h11| > |h10-h01|) flip
                if (extraCliff)
                {
                    if (fabsf(h00 - h11) > fabsf(h10 - h01))
                        extraFlip = !extraFlip;
                }

                if (hasExtraBlend && HasVisibleBlend(extraAlpha))
                    ++extraBlendCells;

                if (uvHits < 5)
                {
                    AppendTerrainTrace(
                        "UV[%d,%d] U=[%.4f,%.4f,%.4f,%.4f] V=[%.4f,%.4f,%.4f,%.4f] texH=%d\n",
                        cx, cy, baseU[0], baseU[1], baseU[2], baseU[3], baseV[0], baseV[1], baseV[2], baseV[3],
                        heightMap->m_terrainTexHeight);
                }

                if (flipForBlend)
                    ++flippedCells;

                ++uvHits;

                if (HasVisibleBlend(blendAlpha))
                    ++blendCells;
            }
            else
            {
                float fu = (float)cx / 8.0f;
                float fv = (float)cy / 8.0f;
                float du = 1.0f / 8.0f;
                float dv = 1.0f / 8.0f;
                baseU[0] = fu;
                baseV[0] = fv;
                baseU[1] = fu + du;
                baseV[1] = fv;
                baseU[2] = fu + du;
                baseV[2] = fv + dv;
                baseU[3] = fu;
                baseV[3] = fv + dv;
                ++uvMisses;
            }

            Render::Float3 normals[4];
            float nx = 0.0f;
            float ny = 0.0f;
            float nz = 1.0f;
            computeNormal(data, x0, y0, m_mapWidth, m_mapHeight, &nx, &ny, &nz);
            normals[0] = { nx, ny, nz };
            computeNormal(data, x1, y0, m_mapWidth, m_mapHeight, &nx, &ny, &nz);
            normals[1] = { nx, ny, nz };
            computeNormal(data, x0, y1, m_mapWidth, m_mapHeight, &nx, &ny, &nz);
            normals[2] = { nx, ny, nz };
            computeNormal(data, x1, y1, m_mapWidth, m_mapHeight, &nx, &ny, &nz);
            normals[3] = { nx, ny, nz };

            AppendTerrainQuad(
                vertices,
                indices,
                px0,
                px1,
                py0,
                py1,
                h00,
                h10,
                h01,
                h11,
                normals,
                baseU,
                baseV,
                baseAlpha,
                flipForBlend);

            if (HasVisibleBlend(blendAlpha))
            {
                AppendTerrainQuad(
                    blendVertices,
                    blendIndices,
                    px0,
                    px1,
                    py0,
                    py1,
                    h00,
                    h10,
                    h01,
                    h11,
                    normals,
                    blendU,
                    blendV,
                    blendAlpha,
                    flipForBlend);
            }

            if (HasVisibleBlend(extraAlpha))
            {
                AppendTerrainQuad(
                    extraBlendVertices,
                    extraBlendIndices,
                    px0,
                    px1,
                    py0,
                    py1,
                    h00,
                    h10,
                    h01,
                    h11,
                    normals,
                    extraU,
                    extraV,
                    extraAlpha,
                    extraFlip);
            }

            // Custom terrain edging: cells with customBlendEdgeClass >= 0 use
            // explicit edge tile artwork instead of simple alpha blending.
            // This replicates the logic from the original W3DCustomEdging.cpp.
            if (hasUVData && m_edgingTextureCreated)
            {
                Int cellNdx = cx + cy * m_mapWidth;
                Int blend = heightMap->m_blendTileNdxes[cellNdx];
                if (blend != 0 && heightMap->m_blendedTiles[blend].customBlendEdgeClass >= 0)
                {
                    const TBlendTileInfo& bi = heightMap->m_blendedTiles[blend];
                    float uOffset = 0.0f;
                    float vOffset = 0.0f;
                    bool hasEdgeUV = true;

                    if (bi.horiz)
                    {
                        uOffset = 0.0f;
                        vOffset = 0.25f * (1 + (cy & 1));
                        if (bi.inverted & INVERTED_MASK)
                            uOffset = 0.75f;
                    }
                    else if (bi.vert)
                    {
                        vOffset = 0.75f;
                        uOffset = 0.25f * (1 + (cx & 1));
                        if (bi.inverted & INVERTED_MASK)
                            vOffset = 0.0f;
                    }
                    else if (bi.rightDiagonal)
                    {
                        if (bi.longDiagonal)
                        {
                            vOffset = 0.25f;
                            uOffset = 0.5f;
                            if (bi.inverted & INVERTED_MASK)
                            {
                                uOffset = 0.5f;
                                vOffset = 0.5f;
                            }
                        }
                        else
                        {
                            vOffset = 0.75f;
                            uOffset = 0.0f;
                            if (bi.inverted & INVERTED_MASK)
                            {
                                uOffset = 0.0f;
                                vOffset = 0.0f;
                            }
                        }
                    }
                    else if (bi.leftDiagonal)
                    {
                        if (bi.longDiagonal)
                        {
                            uOffset = 0.25f;
                            vOffset = 0.25f;
                            if (bi.inverted & INVERTED_MASK)
                            {
                                uOffset = 0.25f;
                                vOffset = 0.5f;
                            }
                        }
                        else
                        {
                            vOffset = 0.75f;
                            uOffset = 0.75f;
                            if (bi.inverted & INVERTED_MASK)
                            {
                                uOffset = 0.75f;
                                vOffset = 0.0f;
                            }
                        }
                    }
                    else
                    {
                        hasEdgeUV = false;
                    }

                    if (hasEdgeUV)
                    {
                        // Get the UV range for this edge class in the edge texture atlas
                        Region2D range;
                        heightMap->getUVForBlend(bi.customBlendEdgeClass, &range);

                        // Map the sub-tile offset into the atlas UV range
                        float edgeU0 = range.lo.x + range.width() * uOffset;
                        float edgeV0 = range.lo.y + range.height() * vOffset;
                        float edgeU1 = edgeU0 + 0.25f * range.width();
                        float edgeV1 = edgeV0 + 0.25f * range.height();

                        // Build 4 UVs for the quad corners:
                        // corner order: [0]=(x0,y0), [1]=(x1,y0), [2]=(x1,y1), [3]=(x0,y1)
                        float edgeU[4] = { edgeU0, edgeU1, edgeU1, edgeU0 };
                        float edgeV[4] = { edgeV1, edgeV1, edgeV0, edgeV0 };

                        AppendTerrainMaskedQuad(
                            edgingBaseVertices,
                            edgingBaseIndices,
                            px0,
                            px1,
                            py0,
                            py1,
                            h00,
                            h10,
                            h01,
                            h11,
                            normals,
                            blendU,
                            blendV,
                            edgeU,
                            edgeV,
                            flipForBlend);

                        UnsignedByte edgeAlpha[4] = { 255, 255, 255, 255 };

                        AppendTerrainQuad(
                            edgingVertices,
                            edgingIndices,
                            px0,
                            px1,
                            py0,
                            py1,
                            h00,
                            h10,
                            h01,
                            h11,
                            normals,
                            edgeU,
                            edgeV,
                            edgeAlpha,
                            flipForBlend);
                        ++edgingCells;
                    }
                }
            }
        }
    }

    AppendTerrainTrace(
        "TerrainRenderer::BuildMesh verts=%u indices=%u uvHits=%d uvMisses=%d blendCells=%d extraBlendCells=%d edgingCells=%d flipped=%d hasUV=%d atlas=%dx%d\n",
        static_cast<unsigned>(vertices.size()),
        static_cast<unsigned>(indices.size()),
        uvHits,
        uvMisses,
        blendCells,
        extraBlendCells,
        edgingCells,
        flippedCells,
        hasUVData ? 1 : 0,
        m_atlasWidth,
        m_atlasHeight);

    auto& device = Renderer::Instance().GetDevice();
    if (!m_vertexBuffer.Create(
            device, vertices.data(), static_cast<uint32_t>(vertices.size()), sizeof(Vertex3D), false) ||
        !m_indexBuffer.Create32(device, indices.data(), static_cast<uint32_t>(indices.size()), false))
    {
        m_ready = false;
        return;
    }

    if (!blendVertices.empty() && !blendIndices.empty())
    {
        if (!m_blendVertexBuffer.Create(
                device, blendVertices.data(), static_cast<uint32_t>(blendVertices.size()), sizeof(Vertex3D), false) ||
            !m_blendIndexBuffer.Create32(device, blendIndices.data(), static_cast<uint32_t>(blendIndices.size()), false))
        {
            m_ready = false;
            return;
        }
        m_blendVertexCount = static_cast<uint32_t>(blendVertices.size());
        m_blendIndexCount = static_cast<uint32_t>(blendIndices.size());
    }
    else
    {
        m_blendVertexBuffer = VertexBuffer();
        m_blendIndexBuffer = IndexBuffer();
        m_blendVertexCount = 0;
        m_blendIndexCount = 0;
    }

    if (!extraBlendVertices.empty() && !extraBlendIndices.empty())
    {
        if (!m_extraBlendVertexBuffer.Create(
                device,
                extraBlendVertices.data(),
                static_cast<uint32_t>(extraBlendVertices.size()),
                sizeof(Vertex3D),
                false) ||
            !m_extraBlendIndexBuffer.Create32(
                device, extraBlendIndices.data(), static_cast<uint32_t>(extraBlendIndices.size()), false))
        {
            m_ready = false;
            return;
        }
        m_extraBlendVertexCount = static_cast<uint32_t>(extraBlendVertices.size());
        m_extraBlendIndexCount = static_cast<uint32_t>(extraBlendIndices.size());
    }
    else
    {
        m_extraBlendVertexBuffer = VertexBuffer();
        m_extraBlendIndexBuffer = IndexBuffer();
        m_extraBlendVertexCount = 0;
        m_extraBlendIndexCount = 0;
    }

    if (!edgingBaseVertices.empty() && !edgingBaseIndices.empty())
    {
        if (!m_edgingBaseVertexBuffer.Create(
                device,
                edgingBaseVertices.data(),
                static_cast<uint32_t>(edgingBaseVertices.size()),
                sizeof(Vertex3DMasked),
                false) ||
            !m_edgingBaseIndexBuffer.Create32(
                device, edgingBaseIndices.data(), static_cast<uint32_t>(edgingBaseIndices.size()), false))
        {
            m_ready = false;
            return;
        }
        m_edgingBaseVertexCount = static_cast<uint32_t>(edgingBaseVertices.size());
        m_edgingBaseIndexCount = static_cast<uint32_t>(edgingBaseIndices.size());
    }
    else
    {
        m_edgingBaseVertexBuffer = VertexBuffer();
        m_edgingBaseIndexBuffer = IndexBuffer();
        m_edgingBaseVertexCount = 0;
        m_edgingBaseIndexCount = 0;
    }

    // Custom edging buffers
    if (!edgingVertices.empty() && !edgingIndices.empty())
    {
        if (!m_edgingVertexBuffer.Create(
                device,
                edgingVertices.data(),
                static_cast<uint32_t>(edgingVertices.size()),
                sizeof(Vertex3D),
                false) ||
            !m_edgingIndexBuffer.Create32(
                device, edgingIndices.data(), static_cast<uint32_t>(edgingIndices.size()), false))
        {
            m_ready = false;
            return;
        }
        m_edgingVertexCount = static_cast<uint32_t>(edgingVertices.size());
        m_edgingIndexCount = static_cast<uint32_t>(edgingIndices.size());
        AppendTerrainTrace(
            "TerrainRenderer::BuildMesh edging verts=%u indices=%u\n",
            m_edgingVertexCount, m_edgingIndexCount);
    }
    else
    {
        m_edgingVertexBuffer = VertexBuffer();
        m_edgingIndexBuffer = IndexBuffer();
        m_edgingVertexCount = 0;
        m_edgingIndexCount = 0;
    }

    m_vertexCount = static_cast<uint32_t>(vertices.size());
    m_indexCount = static_cast<uint32_t>(indices.size());
    m_ready = true;
}

// Point-in-polygon test (ray casting method)
static bool PointInPolygon(float px, float py, const PolygonTrigger* trigger)
{
    int n = trigger->getNumPoints();
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++)
    {
        float xi = static_cast<float>(trigger->getPoint(i)->x);
        float yi = static_cast<float>(trigger->getPoint(i)->y);
        float xj = static_cast<float>(trigger->getPoint(j)->x);
        float yj = static_cast<float>(trigger->getPoint(j)->y);

        if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi))
            inside = !inside;
    }
    return inside;
}

// Compute minimum distance from point (px, py) to any edge of the polygon.
// Used for shoreline alpha blending: vertices near the edge fade out.
static float DistanceToPolygonEdge(float px, float py, const PolygonTrigger* trigger)
{
    int n = trigger->getNumPoints();
    float minDist = 1e9f;

    for (int i = 0, j = n - 1; i < n; j = i++)
    {
        float ax = static_cast<float>(trigger->getPoint(j)->x);
        float ay = static_cast<float>(trigger->getPoint(j)->y);
        float bx = static_cast<float>(trigger->getPoint(i)->x);
        float by = static_cast<float>(trigger->getPoint(i)->y);

        // Project point onto the line segment AB, clamped to [0,1]
        float abx = bx - ax;
        float aby = by - ay;
        float apx = px - ax;
        float apy = py - ay;
        float abLenSq = abx * abx + aby * aby;

        float t = 0.0f;
        if (abLenSq > 1e-6f)
            t = std::clamp((apx * abx + apy * aby) / abLenSq, 0.0f, 1.0f);

        // Closest point on segment
        float cx = ax + t * abx;
        float cy = ay + t * aby;
        float dx = px - cx;
        float dy = py - cy;
        float dist = sqrtf(dx * dx + dy * dy);

        if (dist < minDist)
            minDist = dist;
    }

    return minDist;
}

void TerrainRenderer::BuildWaterMesh()
{
    auto& device = Renderer::Instance().GetDevice();

    const TimeOfDay timeOfDay = TheGlobalData ? TheGlobalData->m_timeOfDay : TIME_OF_DAY_MORNING;

    // Standing water in the legacy renderer is mapped in world space at a much
    // lower frequency than the earlier placeholder DX11 pass. Using world-space
    // UVs here prevents the texture from repeating several times inside a single
    // terrain cell.
    const float GRID_SPACING = TERRAIN_XY_FACTOR;   // 10 world units per grid cell
    const float WATER_UV_REPEAT_WORLD = 150.0f;     // Matches legacy drawTrapezoidWater()

    // Shoreline fade distance: vertices within this distance (world units) of the
    // polygon edge will have their alpha reduced for a soft feathered shoreline.
    const float SHORELINE_FADE_DIST = GRID_SPACING * 1.5f;  // 1.5 grid cells

    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    int polygonCount = 0;

    for (PolygonTrigger* trigger = PolygonTrigger::getFirstPolygonTrigger(); trigger != nullptr; trigger = trigger->getNext())
    {
        if (!trigger->isWaterArea() || trigger->getNumPoints() < 3)
            continue;

        // Compute bounding box and average Z height of the water polygon
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        float avgZ = 0.0f;
        int pointCount = trigger->getNumPoints();
        for (int i = 0; i < pointCount; ++i)
        {
            const ICoord3D* pt = trigger->getPoint(i);
            float px = static_cast<float>(pt->x);
            float py = static_cast<float>(pt->y);
            float pz = static_cast<float>(pt->z);
            minX = std::min(minX, px);
            minY = std::min(minY, py);
            maxX = std::max(maxX, px);
            maxY = std::max(maxY, py);
            avgZ += pz;
        }
        avgZ /= static_cast<float>(pointCount);
        float waterZ = avgZ + WATER_SURFACE_OFFSET;
        m_maxWaterHeight = std::max(m_maxWaterHeight, waterZ);

        // Extend bounds slightly and snap to grid
        minX = floorf(minX / GRID_SPACING) * GRID_SPACING;
        minY = floorf(minY / GRID_SPACING) * GRID_SPACING;
        maxX = ceilf(maxX / GRID_SPACING) * GRID_SPACING;
        maxY = ceilf(maxY / GRID_SPACING) * GRID_SPACING;

        int gridW = static_cast<int>((maxX - minX) / GRID_SPACING) + 1;
        int gridH = static_cast<int>((maxY - minY) / GRID_SPACING) + 1;
        if (gridW < 2 || gridH < 2)
            continue;

        // Build a 2D grid of vertex indices (-1 means outside polygon)
        std::vector<int> gridIndices(gridW * gridH, -1);

        for (int gy = 0; gy < gridH; ++gy)
        {
            for (int gx = 0; gx < gridW; ++gx)
            {
                float wx = minX + gx * GRID_SPACING;
                float wy = minY + gy * GRID_SPACING;

                // Include vertex if inside polygon (or on edge within 1 cell margin)
                if (PointInPolygon(wx, wy, trigger))
                {
                    // Compute shoreline alpha: fade out near polygon edges
                    float edgeDist = DistanceToPolygonEdge(wx, wy, trigger);
                    float shoreAlpha = std::clamp(edgeDist / SHORELINE_FADE_DIST, 0.0f, 1.0f);
                    // Apply smoothstep for a more natural gradual fade
                    shoreAlpha = shoreAlpha * shoreAlpha * (3.0f - 2.0f * shoreAlpha);
                    uint8_t alphaValue = static_cast<uint8_t>(shoreAlpha * 255.0f);

                    Vertex3D vertex = {};
                    vertex.position = { wx, wy, waterZ };
                    // Encode river flag in normal.x. Standing water has the
                    // normal pointing straight up (0,0,1). Rivers store a tiny
                    // X bias (0.01) which the water shader detects to enable
                    // scrolling-V flow. The bias is small enough that it
                    // doesn't perceptibly affect lighting (NdotL ≈ unchanged
                    // for a near-vertical normal).
                    const float riverFlag = trigger->isRiver() ? 0.01f : 0.0f;
                    vertex.normal = { riverFlag, 0.0f, 1.0f };
                    vertex.texcoord = {
                        wx / WATER_UV_REPEAT_WORLD,
                        wy / WATER_UV_REPEAT_WORLD
                    };
                    // Pack vertex color with shoreline alpha (RGBA)
                    vertex.color = (static_cast<uint32_t>(alphaValue) << 24) | 0x00FFFFFF;

                    gridIndices[gy * gridW + gx] = static_cast<int>(vertices.size());
                    vertices.push_back(vertex);
                }
            }
        }

        // Build triangles from the grid - only where all 3 vertices exist
        for (int gy = 0; gy < gridH - 1; ++gy)
        {
            for (int gx = 0; gx < gridW - 1; ++gx)
            {
                int i00 = gridIndices[gy * gridW + gx];
                int i10 = gridIndices[gy * gridW + gx + 1];
                int i01 = gridIndices[(gy + 1) * gridW + gx];
                int i11 = gridIndices[(gy + 1) * gridW + gx + 1];

                // Single-sided. The water render path uses m_rasterNoCull
                // (Renderer::SetWaterBump3DState) so the water is still
                // visible from below; previously each quad was emitted with
                // BOTH winding orders, doubling the per-pixel overdraw on
                // top of the alpha-blended feather layers — up to 5 layers ×
                // 2 = 10× overdraw made the water look uniformly dark with
                // baked-in alpha tinting on every layer.
                if (i00 >= 0 && i10 >= 0 && i01 >= 0)
                {
                    indices.push_back(static_cast<uint32_t>(i00));
                    indices.push_back(static_cast<uint32_t>(i10));
                    indices.push_back(static_cast<uint32_t>(i01));
                }
                if (i10 >= 0 && i11 >= 0 && i01 >= 0)
                {
                    indices.push_back(static_cast<uint32_t>(i10));
                    indices.push_back(static_cast<uint32_t>(i11));
                    indices.push_back(static_cast<uint32_t>(i01));
                }
            }
        }

        ++polygonCount;
    }

    if (vertices.empty() || indices.empty())
    {
        m_waterVertexBuffer = VertexBuffer();
        m_waterIndexBuffer = IndexBuffer();
        m_waterVerticesCPU.clear();
        m_waterVertexCount = 0;
        m_waterIndexCount = 0;
        m_waterBuildTimeOfDay = static_cast<int>(timeOfDay);
        m_waterReady = false;
        return;
    }

    // Create as dynamic buffer so we can update UVs each frame for wave animation
    if (!m_waterVertexBuffer.Create(
            device, vertices.data(), static_cast<uint32_t>(vertices.size()), sizeof(Vertex3D), true) ||
        !m_waterIndexBuffer.Create32(device, indices.data(), static_cast<uint32_t>(indices.size()), false))
    {
        m_waterReady = false;
        return;
    }

    // Keep a CPU-side copy of the base vertex data for UV animation.
    // RenderWater will offset the UVs from these base values each frame.
    m_waterVerticesCPU = std::move(vertices);

    m_waterVertexCount = static_cast<uint32_t>(m_waterVerticesCPU.size());
    m_waterIndexCount = static_cast<uint32_t>(indices.size());
    m_waterBuildTimeOfDay = static_cast<int>(timeOfDay);
    m_waterReady = true;

    AppendTerrainTrace(
        "TerrainRenderer::BuildWaterMesh polygons=%d verts=%u indices=%u grid=%.0f uvRepeatWorld=%.1f shoreFade=%.1f\n",
        polygonCount,
        m_waterVertexCount,
        m_waterIndexCount,
        GRID_SPACING,
        WATER_UV_REPEAT_WORLD,
        SHORELINE_FADE_DIST);
}

void TerrainRenderer::Render(CameraClass* camera, WorldHeightMap* heightMap)
{
    static int s_loggedFrames = 0;

    if (!m_ready || !camera)
        return;

    // Lazily build real terrain texture atlas when tiles become available.
    // m_atlasWidth <= 2 means we only have the fallback green texture.
    if (m_atlasWidth <= 2 && heightMap && heightMap->getSourceTile(0) != nullptr)
    {
        m_textureCreated = false; // Clear fallback so BuildTerrainTextureAtlas creates real atlas
        BuildTerrainTextureAtlas(heightMap);
        if (m_textureCreated && m_atlasWidth > 2)
        {
            AppendTerrainTrace(
                "TerrainRenderer: Lazy rebuild triggered, terrainTexHeight=%d atlasH=%d\n",
                heightMap->m_terrainTexHeight, m_atlasHeight);
            m_ready = false;
            BuildMesh(heightMap);
            if (!m_ready)
                return;
        }
    }

    auto& renderer = Renderer::Instance();

    Render::Float3 cameraPos = {};
    if (!ApplyW3DCamera(renderer, camera, &cameraPos))
        return;

    if (s_loggedFrames < 3)
    {
        AppendTerrainTrace(
            "TerrainRenderer::Render frame=%d camPos=(%.2f,%.2f,%.2f) indices=%u verts=%u atlas=%dx%d\n",
            s_loggedFrames, cameraPos.x, cameraPos.y, cameraPos.z, m_indexCount, m_vertexCount, m_atlasWidth, m_atlasHeight);
    }

    ApplyTerrainLighting(renderer);
    renderer.SetWaterHeight(m_maxWaterHeight); // for underwater depth fade in shader
    renderer.FlushFrameConstants();

    Render::Float4x4 worldIdentity;
    DirectX::XMStoreFloat4x4(&ToXM(worldIdentity), DirectX::XMMatrixIdentity());

    // Always bind our terrain texture. If the atlas isn't built yet, use fallback.
    if (!m_textureCreated)
    {
        // Create a simple solid green fallback so we never render with stale UI textures
        uint32_t green[4] = { 0xFF3A6B28, 0xFF4A7A34, 0xFF5A6B3A, 0xFF6B7A48 };
        m_terrainTexture.CreateFromRGBA(renderer.GetDevice(), green, 2, 2, true);
        m_textureCreated = true;
        m_atlasWidth = 2;
        m_atlasHeight = 2;
    }
    // Cloud shadows are now procedural in the shader — no texture needed.
    // Just set the enabled flag based on user setting and time of day.
    bool cloudEnabled = TheGlobalData && TheGlobalData->m_useCloudMap
                     && TheGlobalData->m_timeOfDay != TIME_OF_DAY_NIGHT;
    renderer.SetCloudShadowEnabled(cloudEnabled);

    // Bind shroud texture for per-pixel fog of war (sampled in PSMain via ApplyShroud)
    if (m_shroudDirty)
        UpdateShroudTexture();
    if (m_shroudTextureReady && m_shroudTexture.IsValid() && !m_shroudGrid.empty() &&
        m_shroudWidth > 0 && m_shroudHeight > 0)
    {
        float cellWorldSize = TERRAIN_XY_FACTOR;
        if (ThePartitionManager)
        {
            const Real partitionCellSize = ThePartitionManager->getCellSize();
            if (partitionCellSize > 0.01f)
                cellWorldSize = partitionCellSize;
        }

        float worldOriginX = 0.0f;
        float worldOriginY = 0.0f;
        if (TheTerrainLogic)
        {
            Region3D mapExtent;
            TheTerrainLogic->getExtent(&mapExtent);
            worldOriginX = mapExtent.lo.x;
            worldOriginY = mapExtent.lo.y;
        }

        // Texture has a 1-texel border around playable shroud data.
        // Map world playable area [origin .. origin + shroudSize*cell] onto
        // texture interior [1 .. shroudSize] while preserving border sampling outside.
        const float shroudTexW = (float)(m_shroudWidth + 2);
        const float shroudTexH = (float)(m_shroudHeight + 2);
        const float invWorldW = 1.0f / (shroudTexW * cellWorldSize);
        const float invWorldH = 1.0f / (shroudTexH * cellWorldSize);
        renderer.SetShroudParams(
            invWorldW,
            invWorldH,
            worldOriginX - cellWorldSize,
            worldOriginY - cellWorldSize);
        renderer.BindShroudTexture(&m_shroudTexture);
    }
    else
    {
        renderer.SetShroudParams(0.0f, 0.0f, 0.0f, 0.0f); // disable
    }

    renderer.FlushFrameConstants();

    // Use CLAMP sampler for terrain atlas to prevent bilinear bleeding at atlas edges
    // Helper lambda to rebind shroud texture after state changes
    auto bindOverlayTextures = [&]() {
        if (m_shroudTextureReady && m_shroudTexture.IsValid())
            renderer.BindShroudTexture(&m_shroudTexture);
    };

    renderer.Restore3DState();
    renderer.BindLinearClampSampler();
    bindOverlayTextures();
    renderer.Draw3D(m_vertexBuffer, m_indexBuffer, &m_terrainTexture, worldIdentity);

    if (m_blendIndexCount > 0)
    {
        renderer.SetAlphaBlend3DState();
        renderer.BindLinearClampSampler();
        bindOverlayTextures();
        renderer.Draw3D(m_blendVertexBuffer, m_blendIndexBuffer, &m_terrainTexture, worldIdentity);
    }

    if (m_extraBlendIndexCount > 0)
    {
        renderer.SetAlphaBlend3DState();
        renderer.BindLinearClampSampler();
        bindOverlayTextures();
        renderer.Draw3D(m_extraBlendVertexBuffer, m_extraBlendIndexBuffer, &m_terrainTexture, worldIdentity);
    }

    if (m_edgingBaseIndexCount > 0 && m_edgingTextureCreated)
    {
        renderer.SetTerrainEdgeBase3DState();
        renderer.BindLinearClampSampler();
        bindOverlayTextures();
        renderer.Draw3DMasked(
            m_edgingBaseVertexBuffer,
            m_edgingBaseIndexBuffer,
            &m_terrainTexture,
            &m_edgingTexture,
            worldIdentity);
    }

    if (m_edgingIndexCount > 0 && m_edgingTextureCreated)
    {
        renderer.SetTerrainEdgeArt3DState();
        renderer.BindLinearClampSampler();
        bindOverlayTextures();
        renderer.Draw3D(m_edgingVertexBuffer, m_edgingIndexBuffer, &m_edgingTexture, worldIdentity);
    }

    // Restore default WRAP sampler for subsequent 3D passes (models, etc.)
    renderer.Restore3DState();

    if (s_loggedFrames < 3)
        ++s_loggedFrames;
}

void TerrainRenderer::RenderShadowDepth()
{
    // Caller (W3DDisplay shadow pass) has already bound the shadow depth
    // target and the shadow depth shader, and set viewProjection in the
    // cbuffer to the light-space VP. We must NOT call ApplyW3DCamera here
    // (which would override lightVP). Just submit the terrain mesh so the
    // bound vertex shader writes light-space depth values into the shadow
    // texture. We skip the alpha-blend overlays and the edging passes —
    // those are tiny detail layers that don't meaningfully cast shadow.
    if (!m_ready) return;
    if (m_indexCount == 0) return;
    auto& renderer = Renderer::Instance();
    Render::Float4x4 worldIdentity;
    DirectX::XMStoreFloat4x4(&ToXM(worldIdentity), DirectX::XMMatrixIdentity());
    // Texture binding is irrelevant for the shadow depth shader (which
    // doesn't sample), but Draw3D requires *something* in slot 0 — pass
    // the existing terrain texture so it doesn't unbind the previous one.
    renderer.Draw3D(m_vertexBuffer, m_indexBuffer, &m_terrainTexture, worldIdentity);
}

void TerrainRenderer::RenderWater(CameraClass* camera)
{
    static int s_loggedFrames = 0;

    if (camera == nullptr)
        return;

    const TimeOfDay todEnum = TheGlobalData ? TheGlobalData->m_timeOfDay : TIME_OF_DAY_MORNING;
    const int timeOfDay = static_cast<int>(todEnum);
    if (!m_waterReady || m_waterBuildTimeOfDay != timeOfDay)
        BuildWaterMesh();

    if (!m_waterReady || m_waterIndexCount == 0 || m_waterVerticesCPU.empty())
        return;

    auto& renderer = Renderer::Instance();
    if (!ApplyW3DCamera(renderer, camera))
        return;

    ApplyTerrainLighting(renderer);
    renderer.FlushFrameConstants();

    // Water UV animation (scroll + wobble) is now done in the VSMainWater vertex shader
    // using lightingOptions.y (time in ms). No CPU-side vertex copy or upload needed.
    // The base vertex buffer with static UVs is uploaded only once when BuildWaterMesh() runs.
    const WaterSetting& waterSetting = WaterSettings[todEnum];

    // --- Resolve water texture ---
    Texture* waterTexture = nullptr;
    if (TheWaterTransparency != nullptr && !TheWaterTransparency->m_standingWaterTexture.isEmpty())
        waterTexture = ImageCache::Instance().GetTexture(renderer.GetDevice(), TheWaterTransparency->m_standingWaterTexture.str());

    if (waterTexture == nullptr)
    {
        if (!waterSetting.m_waterTextureFile.isEmpty())
            waterTexture = ImageCache::Instance().GetTexture(renderer.GetDevice(), waterSetting.m_waterTextureFile.str());
    }

    if (waterTexture == nullptr)
    {
        if (!m_waterFallbackTextureReady)
        {
            const uint32_t whitePixel = 0xFFFFFFFF;
            m_waterFallbackTexture.CreateFromRGBA(renderer.GetDevice(), &whitePixel, 1, 1, false);
            m_waterFallbackTextureReady = true;
        }
        waterTexture = &m_waterFallbackTexture;
    }

    const Render::Float4 waterTint = ComputeWaterTint();
    Render::Float4x4 worldIdentity;
    DirectX::XMStoreFloat4x4(&ToXM(worldIdentity), DirectX::XMMatrixIdentity());

    // --- Water reflection pass: render skybox into 256x256 offscreen RT ---
    // Original renders the entire scene reflected; we render just the skybox
    // which provides the most visible reflection contribution.
    extern bool g_debugDisableReflection;
    {
        const int REFL_SIZE = 256;
        if (!m_reflectionRTReady && !g_debugDisableReflection)
        {
            m_reflectionRTReady = m_reflectionRT.CreateRenderTarget(
                renderer.GetDevice(), REFL_SIZE, REFL_SIZE);
        }
        if (m_reflectionRTReady && !g_debugDisableReflection)
        {
            renderer.SetRenderTarget(m_reflectionRT, REFL_SIZE, REFL_SIZE);

            // Build reflected camera: mirror the view matrix around the water plane (Z=0).
            Matrix3D viewMtx;
            camera->Get_View_Matrix(&viewMtx);
            RenderUtils::SanitizeMatrix3D(viewMtx);

            // Negate Z axis components to flip reflection around Z=0 plane.
            // In the W3D Matrix3D layout: row[r][c], negate column 2 (Z) and translation Z.
            viewMtx[0][2] = -viewMtx[0][2];
            viewMtx[1][2] = -viewMtx[1][2];
            viewMtx[2][0] = -viewMtx[2][0];
            viewMtx[2][1] = -viewMtx[2][1];
            viewMtx[2][3] = -viewMtx[2][3];

            Render::Float4x4 reflView = RenderUtils::Matrix3DToFloat4x4(viewMtx);

            Matrix4x4 projMtx;
            camera->Get_D3D_Projection_Matrix(&projMtx);
            Render::Float4x4 projMatrix = RenderUtils::Matrix4x4ToFloat4x4(projMtx);

            const Vector3 camPos = camera->Get_Position();
            renderer.SetCamera(reflView, projMatrix, { camPos.X, camPos.Y, -camPos.Z });
            renderer.SetAmbientLight({ 1, 1, 1, 1 });
            renderer.SetDirectionalLights(nullptr, nullptr, 0);
            renderer.FlushFrameConstants();

            // Render skybox into reflection RT
            RenderSkyBox(camera);

            // Enhanced reflection: also render terrain into the RT so the
            // reflection shows hills/mountains, matching the original DX8
            // W3DWater.cpp:1393 renderMirror behavior. The reflected camera
            // is below the water plane looking up, so triangle winding is
            // inverted — terrain geometry is single-sided so without raster
            // changes the backfaces would cull. The water tint composite at
            // 0.12 (classic) hides the artifact; the enhanced 0.40 tint
            // shows enough to see the silhouette.
            extern bool g_useEnhancedWater;
            if (g_useEnhancedWater && m_ready)
            {
                renderer.Restore3DState();
                renderer.Draw3D(m_vertexBuffer, m_indexBuffer, &m_terrainTexture, worldIdentity);

                // Reflection mesh casters: walk the scene + drawable list
                // and render solid geometry (units, buildings, trees) into
                // the reflection RT. Uses NoCull rasterizer because the
                // reflected camera flips screen-space winding. Reuses
                // ModelRenderer's ShadowCasterMode to suppress translucent
                // batches and particle side-effects (we want only opaque
                // silhouettes — particles + translucent decals don't
                // belong in a 256x256 reflection).
                renderer.SetReflectionMesh3DState();
                RenderReflectionMeshesDX11();
            }

            renderer.RestoreBackBuffer();

            // Re-apply the normal camera for water rendering
            ApplyW3DCamera(renderer, camera);
            ApplyTerrainLighting(renderer);
            renderer.FlushFrameConstants();
        }
    }

    // Try loading bump/noise texture for water surface perturbation
    static Render::Texture* s_waterBumpTex = nullptr;
    static bool s_waterBumpLoaded = false;
    if (!s_waterBumpLoaded)
    {
        auto& dev = renderer.GetDevice();
        s_waterBumpTex = Render::ImageCache::Instance().GetTexture(dev, "Noise0000.tga");
        if (!s_waterBumpTex) s_waterBumpTex = Render::ImageCache::Instance().GetTexture(dev, "noise0000.tga");
        if (!s_waterBumpTex) s_waterBumpTex = Render::ImageCache::Instance().GetTexture(dev, "caust00.tga");
        s_waterBumpLoaded = true;
    }

    if (TheWaterTransparency != nullptr && TheWaterTransparency->m_additiveBlend)
        renderer.SetAdditive3DState();
    else if (s_waterBumpTex)
        renderer.SetWaterBump3DState(s_waterBumpTex);
    else
        renderer.SetAlphaBlend3DState();

    // Bind depth buffer as SRV for shore foam (read-only DSV allows simultaneous depth test + SRV)
    renderer.BeginWaterDepthRead();

    // Multi-layer feathering: draw water at multiple Z heights with decreasing alpha.
    // Z offset is passed via objectColor.w to the VSMainWater vertex shader, eliminating
    // per-layer CPU vertex copies and GPU buffer re-uploads.
    // Original uses FEATHER_THICKNESS=4.0, alpha per layer: 1->255, 2->200, 3->140, 4->110, 5->80
    int featherLayers = TheGlobalData ? TheGlobalData->m_featherWater : 0;
    if (featherLayers > 1 && featherLayers <= 5)
    {
        const float FEATHER_THICKNESS = 4.0f;
        const float layerStep = FEATHER_THICKNESS / (float)featherLayers;
        static const float layerAlphas[] = { 1.0f, 255.0f/255.0f, 200.0f/255.0f, 140.0f/255.0f, 110.0f/255.0f, 80.0f/255.0f };

        for (int layer = 0; layer < featherLayers; ++layer)
        {
            float zOffset = layerStep * (float)layer;
            float layerAlpha = (layer + 1 < 6) ? layerAlphas[layer + 1] : 0.3f;

            // Pack Z offset into objectColor.w for VSMainWater to apply
            Render::Float4 layerTint = waterTint;
            layerTint.w = zOffset; // VSMainWater reads this as Z offset, alpha from layerAlpha
            // Encode alpha in the RGB channels by scaling them
            layerTint.x *= layerAlpha;
            layerTint.y *= layerAlpha;
            layerTint.z *= layerAlpha;

            renderer.Draw3D(m_waterVertexBuffer, m_waterIndexBuffer, waterTexture, worldIdentity, layerTint);
        }
    }
    else
    {
        // No feathering: Z offset = 0
        Render::Float4 baseTint = waterTint;
        baseTint.w = 0.0f; // no Z offset
        renderer.Draw3D(m_waterVertexBuffer, m_waterIndexBuffer, waterTexture, worldIdentity, baseTint);
    }
    // Done reading depth for shore foam — restore writable DSV
    renderer.EndWaterDepthRead();

    // Sparkle/noise overlay: subtle additive pass with WaterSurfaceBubbles.tga.
    // The base tint matches the previous shipping default; when the user
    // opts into Enhanced Water, we boost the sparkle to match the original
    // DX8 trapezoid water shader's stage-1 brightness.
    extern bool g_useEnhancedWater;
    {
        static Texture* s_sparklesTex = nullptr;
        static bool s_sparklesLoaded = false;
        if (!s_sparklesLoaded)
        {
            s_sparklesTex = ImageCache::Instance().GetTexture(renderer.GetDevice(), "WaterSurfaceBubbles.tga");
            if (!s_sparklesTex) s_sparklesTex = ImageCache::Instance().GetTexture(renderer.GetDevice(), "watersurfacebubbles.tga");
            s_sparklesLoaded = true;
        }
        if (s_sparklesTex)
        {
            renderer.SetAdditive3DState();
            Render::Float4 sparkleTint = g_useEnhancedWater
                ? Render::Float4{ 0.25f, 0.25f, 0.30f, 0.35f }   // enhanced
                : Render::Float4{ 0.04f, 0.04f, 0.04f, 0.08f };  // classic
            renderer.Draw3D(m_waterVertexBuffer, m_waterIndexBuffer, s_sparklesTex, worldIdentity, sparkleTint);
        }
    }

    // Reflection overlay: blend the 256x256 reflection RT onto water surface.
    // Classic intensity is subtle (matches what previously shipped). The
    // enhanced toggle bumps it to a more visible sky reflection like the
    // original game's bump-env reflection composite.
    if (m_reflectionRTReady)
    {
        renderer.SetAdditive3DState();
        Render::Float4 reflTint = g_useEnhancedWater
            ? Render::Float4{ 0.40f, 0.40f, 0.45f, 0.5f }    // enhanced
            : Render::Float4{ 0.12f, 0.12f, 0.15f, 0.2f };   // classic
        renderer.Draw3D(m_waterVertexBuffer, m_waterIndexBuffer, &m_reflectionRT, worldIdentity, reflTint);
    }

    renderer.Restore3DState();

    if (s_loggedFrames < 6)
    {
        AppendTerrainTrace(
            "TerrainRenderer::RenderWater frame=%d verts=%u indices=%u texture=%d additive=%d (GPU UV anim)\n",
            s_loggedFrames,
            m_waterVertexCount,
            m_waterIndexCount,
            waterTexture != nullptr ? 1 : 0,
            (TheWaterTransparency != nullptr && TheWaterTransparency->m_additiveBlend) ? 1 : 0);
        ++s_loggedFrames;
    }
}

void TerrainRenderer::Invalidate()
{
    m_ready = false;
    m_waterReady = false;
    m_waterBuildTimeOfDay = -1;
    m_waterVerticesCPU.clear();
    m_shroudMeshReady = false;
    m_bridgesReady = false;
}

void TerrainRenderer::Shutdown()
{
    m_ready = false;
    m_waterReady = false;
    m_textureCreated = false;
    m_waterFallbackTextureReady = false;
    m_indexCount = 0;
    m_vertexCount = 0;
    m_blendIndexCount = 0;
    m_blendVertexCount = 0;
    m_extraBlendIndexCount = 0;
    m_extraBlendVertexCount = 0;
    m_waterIndexCount = 0;
    m_waterVertexCount = 0;
    m_atlasWidth = 0;
    m_atlasHeight = 0;
    m_waterBuildTimeOfDay = -1;
    m_vertexBuffer = VertexBuffer();
    m_indexBuffer = IndexBuffer();
    m_blendVertexBuffer = VertexBuffer();
    m_blendIndexBuffer = IndexBuffer();
    m_extraBlendVertexBuffer = VertexBuffer();
    m_extraBlendIndexBuffer = IndexBuffer();
    m_edgingVertexBuffer = VertexBuffer();
    m_edgingIndexBuffer = IndexBuffer();
    m_edgingBaseVertexBuffer = VertexBuffer();
    m_edgingBaseIndexBuffer = IndexBuffer();
    m_edgingTexture = Texture();
    m_edgingIndexCount = 0;
    m_edgingVertexCount = 0;
    m_edgingBaseIndexCount = 0;
    m_edgingBaseVertexCount = 0;
    m_edgingAtlasWidth = 0;
    m_edgingAtlasHeight = 0;
    m_edgingTextureCreated = false;
    m_waterVertexBuffer = VertexBuffer();
    m_waterIndexBuffer = IndexBuffer();
    m_waterVerticesCPU.clear();
    m_terrainTexture = Texture();
    m_waterFallbackTexture = Texture();
    m_roadBatches.clear();
    m_roadsReady = false;

    // Bridge cleanup
    for (auto& bridge : m_bridges)
    {
        for (auto& piece : bridge.pieces)
        {
            if (piece.renderObj)
            {
                piece.renderObj->Release_Ref();
                piece.renderObj = nullptr;
            }
        }
    }
    m_bridges.clear();
    m_bridgesReady = false;

    for (int i = 0; i < SKY_FACE_COUNT; ++i)
    {
        m_skyVertexBuffers[i] = VertexBuffer();
        m_skyIndexBuffers[i] = IndexBuffer();
    }
    m_skyBoxReady = false;

    // Shroud cleanup
    m_shroudGrid.clear();
    m_shroudWidth = 0;
    m_shroudHeight = 0;
    m_shroudDirty = true;
    m_shroudMeshReady = false;
    m_shroudTextureReady = false;
    m_shroudTexture = Texture();
    m_shroudVertexBuffer = VertexBuffer();
    m_shroudIndexBuffer = IndexBuffer();
    m_shroudIndexCount = 0;
    m_shroudVertexCount = 0;
    m_borderShroudLevel = 255;
}

// ============================================================================
// Road Rendering - reads MapObjects with FLAG_ROAD_* and builds textured
// quad strips on top of the terrain.
// ============================================================================

// Get interpolated terrain height at world (x,y) using bilinear interpolation
static float SampleTerrainHeight(WorldHeightMap* hm, float x, float y)
{
    const float inv = 1.0f / TERRAIN_XY_FACTOR;
    float xdiv = x * inv;
    float ydiv = y * inv;
    float ixf = floorf(xdiv);
    float iyf = floorf(ydiv);
    float fx = xdiv - ixf;
    float fy = ydiv - iyf;

    int ix = (int)ixf + hm->getBorderSize();
    int iy = (int)iyf + hm->getBorderSize();
    int xExt = hm->getXExtent();
    int yExt = hm->getYExtent();

    if (ix < 0) ix = 0; if (ix >= xExt - 1) ix = xExt - 2;
    if (iy < 0) iy = 0; if (iy >= yExt - 1) iy = yExt - 2;

    const UnsignedByte* data = hm->getDataPtr();
    int idx = ix + iy * xExt;
    float p0 = data[idx];
    float p1 = data[idx + 1];
    float p2 = data[idx + xExt + 1];
    float p3 = data[idx + xExt];

    float h = p0 * (1 - fx) * (1 - fy) + p1 * fx * (1 - fy) + p3 * (1 - fx) * fy + p2 * fx * fy;
    return h * TERRAIN_HEIGHT_SCALE;
}

void TerrainRenderer::BuildRoadMesh(WorldHeightMap* heightMap)
{
    if (!heightMap || !TheTerrainRoads)
        return;

    m_roadBatches.clear();
    m_roadsReady = false;

    // Generate road geometry using the ported original W3DRoadBuffer logic.
    // This runs the full pipeline: addMapObjects → updateCountsAndFlags →
    // insertTeeIntersections → insertCurveSegments → insertCrossTypeJoins →
    // preloadRoadsInVertexAndIndexBuffers, with exact miter/offset/curve math.
    RoadGeometry::RoadMeshOutput roadMesh = RoadGeometry::GenerateRoadGeometry(heightMap);

    auto& device = Renderer::Instance().GetDevice();

    // Convert RoadGeometry batches to D3D11 vertex/index buffers
    for (auto& batch : roadMesh.batches)
    {
        if (batch.vertices.empty() || batch.indices.empty())
            continue;

        // Convert RoadVertex (x,y,z,diffuse,u,v) to Render::Vertex3D
        std::vector<Render::Vertex3D> renderVerts(batch.vertices.size());
        for (size_t v = 0; v < batch.vertices.size(); ++v)
        {
            const auto& rv = batch.vertices[v];
            // RoadVertex diffuse is ARGB; Render::Vertex3D color is ABGR
            uint32_t argb = rv.diffuse;
            uint8_t a = (argb >> 24) & 0xFF;
            uint8_t r = (argb >> 16) & 0xFF;
            uint8_t g = (argb >> 8) & 0xFF;
            uint8_t b = argb & 0xFF;
            renderVerts[v].position = { rv.x, rv.y, rv.z };
            renderVerts[v].normal = { 0.0f, 0.0f, 1.0f };
            renderVerts[v].texcoord = { rv.u, rv.v };
            renderVerts[v].color = (a << 24) | (b << 16) | (g << 8) | r;
        }

        RoadBatch rb;
        rb.vertexBuffer.Create(device, renderVerts.data(),
            static_cast<uint32_t>(renderVerts.size()), sizeof(Render::Vertex3D));
        rb.indexBuffer.Create(device, batch.indices.data(),
            static_cast<uint32_t>(batch.indices.size()));
        rb.vertexCount = static_cast<uint32_t>(renderVerts.size());
        rb.indexCount = static_cast<uint32_t>(batch.indices.size());

        if (!batch.textureName.empty())
        {
            Render::Texture* tex = Render::ImageCache::Instance().GetTexture(
                device, batch.textureName.c_str());
            if (tex)
                rb.texture = *tex;
        }

        m_roadBatches.push_back(std::move(rb));
    }

    m_roadsReady = !m_roadBatches.empty();
    AppendTerrainTrace("BuildRoadMesh: %zu batches, ready=%d\n",
        m_roadBatches.size(), m_roadsReady ? 1 : 0);
}


void TerrainRenderer::RenderRoads(CameraClass* camera)
{
    static int s_roadLogFrames = 0;

    if (!m_roadsReady || m_roadBatches.empty() || !camera)
    {
        if (s_roadLogFrames < 5)
        {
            AppendTerrainTrace("RenderRoads: SKIP ready=%d batches=%d camera=%p\n",
                m_roadsReady ? 1 : 0, (int)m_roadBatches.size(), camera);
            ++s_roadLogFrames;
        }
        return;
    }

    auto& renderer = Renderer::Instance();

    ApplyW3DCamera(renderer, camera);
    ApplyTerrainLighting(renderer);
    renderer.FlushFrameConstants();

    Render::Float4x4 worldIdentity;
    DirectX::XMStoreFloat4x4(&ToXM(worldIdentity), DirectX::XMMatrixIdentity());

    // Roads use alpha blend to smoothly overlay on terrain with transparent edges.
    renderer.SetAlphaBlend3DState();

    // Bind shroud texture so roads get fog of war (clouds are procedural)
    if (m_shroudTextureReady && m_shroudTexture.IsValid())
        renderer.BindShroudTexture(&m_shroudTexture);

    Render::Float4 white = { 1.0f, 1.0f, 1.0f, 1.0f };

    for (const RoadBatch& batch : m_roadBatches)
    {
        if (s_roadLogFrames < 3)
        {
            AppendTerrainTrace("  Road batch: verts=%u indices=%u hasTex=%d\n",
                batch.vertexCount, batch.indexCount,
                batch.texture.IsValid() ? 1 : 0);
        }

        renderer.Draw3D(batch.vertexBuffer, batch.indexBuffer,
            &batch.texture, worldIdentity, white);
    }

    if (s_roadLogFrames < 5)
    {
        AppendTerrainTrace("RenderRoads: DREW %d batches\n", (int)m_roadBatches.size());
        ++s_roadLogFrames;
    }

    renderer.Restore3DState();
}

// ============================================================================
// Cloud Shadows - integrated per-pixel in PSMain via ApplyCloudShadow()
// ============================================================================

void TerrainRenderer::EnsureCloudTextureLoaded()
{
    // Cloud shadows are now fully procedural in the shader.
    // No texture loading needed.
    m_cloudTextureLoaded = true;
}

const Texture* TerrainRenderer::GetCloudTexture()
{
    // Cloud shadows are procedural — no texture.
    return nullptr;
}

void TerrainRenderer::RenderCloudShadows(CameraClass* camera)
{
    // Cloud shadows are procedural in PSMain (ApplyCloudShadow).
    (void)camera;
}

// ============================================================================
// Bridge Rendering - loads W3D bridge models and renders them via ModelRenderer
// ============================================================================

static const float BRIDGE_FLOAT_AMT = 0.25f;

void TerrainRenderer::BuildBridgeMeshes(WorldHeightMap* heightMap)
{
    if (!heightMap || !TheTerrainRoads)
        return;

    // Clear previous bridges
    for (auto& bridge : m_bridges)
    {
        for (auto& piece : bridge.pieces)
        {
            if (piece.renderObj)
            {
                piece.renderObj->Release_Ref();
                piece.renderObj = nullptr;
            }
        }
    }
    m_bridges.clear();
    m_bridgesReady = false;



    int bridgeCount = 0;

    for (MapObject* obj = MapObject::getFirstMapObject(); obj != nullptr; obj = obj->getNext())
    {
        if (!(obj->getFlags() & FLAG_BRIDGE_POINT1))
            continue;

        MapObject* obj2 = obj->getNext();
        if (!obj2 || !(obj2->getFlags() & FLAG_BRIDGE_POINT2))
        {
            AppendTerrainTrace("BuildBridgeMeshes: missing second bridge point\n");
            if (!obj2)
                break;
            continue;
        }

        // Get bridge template
        AsciiString bridgeName = obj->getName();
        TerrainRoadType* bridgeTemplate = TheTerrainRoads->findBridge(bridgeName);
        if (!bridgeTemplate)
        {
            AppendTerrainTrace("BuildBridgeMeshes: bridge template not found: '%s'\n",
                bridgeName.str());
            obj = obj2;
            continue;
        }

        // Get bridge endpoints
        const Coord3D* loc1 = obj->getLocation();
        const Coord3D* loc2 = obj2->getLocation();
        Vector3 from(loc1->x, loc1->y,
            SampleTerrainHeight(heightMap, loc1->x, loc1->y) + BRIDGE_FLOAT_AMT);
        Vector3 to(loc2->x, loc2->y,
            SampleTerrainHeight(heightMap, loc2->x, loc2->y) + BRIDGE_FLOAT_AMT);

        // Get model and texture names
        float scale = bridgeTemplate->getBridgeScale();
        const char* modelName = bridgeTemplate->getBridgeModel().str();
        const char* textureName = bridgeTemplate->getTexture().str();

        if (!modelName || !*modelName)
        {
            AppendTerrainTrace("BuildBridgeMeshes: no model name for '%s'\n", bridgeName.str());
            obj = obj2;
            continue;
        }

        AppendTerrainTrace("BuildBridgeMeshes: bridge '%s' model='%s' tex='%s' scale=%.2f\n",
            bridgeName.str(), modelName, textureName, scale);
        AppendTerrainTrace("  from=(%.1f,%.1f,%.1f) to=(%.1f,%.1f,%.1f)\n",
            from.X, from.Y, from.Z, to.X, to.Y, to.Z);

        // Build sub-object names for left/span/right pieces
        char leftName[256], spanName[256], rightName[256];
        snprintf(leftName, sizeof(leftName), "%s.BRIDGE_LEFT", modelName);
        snprintf(spanName, sizeof(spanName), "%s.BRIDGE_SPAN", modelName);
        snprintf(rightName, sizeof(rightName), "%s.BRIDGE_RIGHT", modelName);

        // Load the hierarchical model to get sub-object transforms
        RenderObjClass* rootObj = CreateRenderObjCompat(modelName, 1.0f, 0xFFFFFF, nullptr, nullptr);
        if (!rootObj)
        {
            AppendTerrainTrace("BuildBridgeMeshes: failed to load model '%s'\n", modelName);
            obj = obj2;
            continue;
        }

        // Find sub-object transforms by scanning the hierarchy
        Matrix3D leftMtx(true), sectionMtx(true), rightMtx(true);
        char leftMeshName[256] = {}, spanMeshName[256] = {}, rightMeshName[256] = {};
        strncpy(leftMeshName, leftName, sizeof(leftMeshName) - 1);
        strncpy(spanMeshName, spanName, sizeof(spanMeshName) - 1);
        strncpy(rightMeshName, rightName, sizeof(rightMeshName) - 1);

        for (int i = 0; i < rootObj->Get_Num_Sub_Objects(); i++)
        {
            RenderObjClass* sub = rootObj->Get_Sub_Object(i);
            if (!sub)
                continue;

            const char* subName = sub->Get_Name();
            Matrix3D mtx = sub->Get_Transform();

            if (_strnicmp(leftName, subName, strlen(leftName)) == 0)
            {
                leftMtx = mtx;
                strncpy(leftMeshName, subName, sizeof(leftMeshName) - 1);
            }
            if (_strnicmp(spanName, subName, strlen(spanName)) == 0)
            {
                sectionMtx = mtx;
                strncpy(spanMeshName, subName, sizeof(spanMeshName) - 1);
            }
            if (_strnicmp(rightName, subName, strlen(rightName)) == 0)
            {
                rightMtx = mtx;
                strncpy(rightMeshName, subName, sizeof(rightMeshName) - 1);
            }

            sub->Release_Ref();
        }

        rootObj->Release_Ref();
        rootObj = nullptr;

        // Load individual mesh pieces
        RenderObjClass* leftRObj = CreateRenderObjCompat(leftMeshName, 1.0f, 0xFFFFFF, nullptr, nullptr);
        RenderObjClass* sectionRObj = CreateRenderObjCompat(spanMeshName, 1.0f, 0xFFFFFF, nullptr, nullptr);
        RenderObjClass* rightRObj = CreateRenderObjCompat(rightMeshName, 1.0f, 0xFFFFFF, nullptr, nullptr);

        MeshClass* leftMesh = nullptr;
        MeshClass* sectionMesh = nullptr;
        MeshClass* rightMesh = nullptr;

        if (leftRObj && leftRObj->Class_ID() == RenderObjClass::CLASSID_MESH)
            leftMesh = static_cast<MeshClass*>(leftRObj);
        if (sectionRObj && sectionRObj->Class_ID() == RenderObjClass::CLASSID_MESH)
            sectionMesh = static_cast<MeshClass*>(sectionRObj);
        if (rightRObj && rightRObj->Class_ID() == RenderObjClass::CLASSID_MESH)
            rightMesh = static_cast<MeshClass*>(rightRObj);

        if (!leftMesh)
        {
            AppendTerrainTrace("BuildBridgeMeshes: no left mesh for '%s'\n", modelName);
            if (leftRObj) leftRObj->Release_Ref();
            if (sectionRObj) sectionRObj->Release_Ref();
            if (rightRObj) rightRObj->Release_Ref();
            obj = obj2;
            continue;
        }

        // Determine bridge type: sectional (left+sections+right) or fixed (left only)
        bool isSectional = (sectionMesh != nullptr && rightMesh != nullptr);

        // Compute mesh extents in model space (after applying sub-object transform)
        // to determine bridge piece sizes and alignment
        auto computeMeshExtents = [](MeshClass* mesh, const Matrix3D& mtx,
            float& minX, float& maxX, float& minY, float& maxY)
        {
            MeshModelClass* model = mesh->Peek_Model();
            if (!model)
                return;
            int vertCount = model->Get_Vertex_Count();
            Vector3* verts = model->Get_Vertex_Array();
            minX = FLT_MAX; maxX = -FLT_MAX;
            minY = FLT_MAX; maxY = -FLT_MAX;
            for (int v = 0; v < vertCount; v++)
            {
                Vector3 vert;
                Matrix3D::Transform_Vector(mtx, verts[v], &vert);
                if (vert.X < minX) minX = vert.X;
                if (vert.X > maxX) maxX = vert.X;
                if (vert.Y < minY) minY = vert.Y;
                if (vert.Y > maxY) maxY = vert.Y;
            }
        };

        float leftMinX, leftMaxX, minY, maxY;
        computeMeshExtents(leftMesh, leftMtx, leftMinX, leftMaxX, minY, maxY);

        float sectionMinX = leftMaxX, sectionMaxX = leftMaxX;
        float rightMinX = leftMaxX, rightMaxX = leftMaxX;

        if (isSectional)
        {
            float dummy1, dummy2;
            computeMeshExtents(sectionMesh, sectionMtx, sectionMinX, sectionMaxX, dummy1, dummy2);
            computeMeshExtents(rightMesh, rightMtx, rightMinX, rightMaxX, dummy1, dummy2);
        }

        float modelLength = rightMaxX - leftMinX;
        if (modelLength < 1.0f) modelLength = 1.0f;

        // Compute bridge direction and scaling vectors
        Vector3 vec = to - from;
        float desiredLength = vec.Length();
        if (desiredLength < 1.0f)
        {
            if (leftRObj) leftRObj->Release_Ref();
            if (sectionRObj) sectionRObj->Release_Ref();
            if (rightRObj) rightRObj->Release_Ref();
            obj = obj2;
            continue;
        }

        Vector3 vecNormal(-vec.Y, vec.X, 0);
        vecNormal.Normalize();
        vecNormal *= scale;

        // Height change along the bridge
        float deltaZ = to.Z - from.Z;
        deltaZ /= desiredLength;
        float deltaX = sqrtf(1.0f - deltaZ * deltaZ);
        Vector3 vecZ(-deltaZ, 0, deltaX);
        vecZ *= scale;

        // Compute number of span sections needed
        float spanLength = rightMinX - leftMaxX;
        int numSpans = 1;
        if (isSectional && spanLength > 0.01f)
        {
            float spannable = desiredLength - (modelLength - spanLength);
            numSpans = (int)floorf((spannable + spanLength / 2.0f) / spanLength);
            if (numSpans < 0) numSpans = 0;
        }

        float bridgeLength = modelLength + (numSpans - 1) * spanLength;
        if (bridgeLength < 1.0f) bridgeLength = 1.0f;
        float xOffset = -leftMinX;

        // Scale vec to map model X coordinates to world positions
        Vector3 vecScaled = vec;
        vecScaled /= bridgeLength;

        AppendTerrainTrace("  modelLen=%.1f bridgeLen=%.1f spans=%d spanLen=%.1f sectional=%d\n",
            modelLength, bridgeLength, numSpans, spanLength, isSectional ? 1 : 0);

        // Helper to build the world transform for a bridge piece:
        // Maps model-space vertex (after sub-object mtx) to world space:
        //   world = (v.X + pieceOffset) * vecScaled + v.Y * vecNormal + v.Z * vecZ + from
        //
        // The Matrix3D rows are:
        //   Row[0] = [vecScaled.X, vecNormal.X, vecZ.X, translation.X]
        //   Row[1] = [vecScaled.Y, vecNormal.Y, vecZ.Y, translation.Y]
        //   Row[2] = [vecScaled.Z, vecNormal.Z, vecZ.Z, translation.Z]
        //
        // This is then composed with the sub-object transform: worldMtx = bridgeMtx * subObjMtx
        auto buildPieceTransform = [&](const Matrix3D& subObjMtx, float pieceXOffset) -> Matrix3D
        {
            Vector3 translation = (pieceXOffset) * vecScaled + from;
            Matrix3D bridgeMtx(
                vecScaled.X, vecNormal.X, vecZ.X, translation.X,
                vecScaled.Y, vecNormal.Y, vecZ.Y, translation.Y,
                vecScaled.Z, vecNormal.Z, vecZ.Z, translation.Z
            );
            Matrix3D result;
            Matrix3D::Multiply(bridgeMtx, subObjMtx, &result);
            return result;
        };

        BridgeInstance bridgeInst;

        // Left piece
        {
            Matrix3D worldMtx = buildPieceTransform(leftMtx, xOffset);
            leftMesh->Set_Transform(worldMtx);
            BridgePiece piece;
            piece.renderObj = leftRObj;
            // Don't release - we're keeping the reference
            bridgeInst.pieces.push_back(piece);
        }

        // Section pieces (spans)
        if (isSectional)
        {
            for (int s = 0; s < numSpans; s++)
            {
                float pieceXOffset = xOffset + s * spanLength;
                Matrix3D worldMtx = buildPieceTransform(sectionMtx, pieceXOffset);

                // For each additional span, we need a separate mesh instance
                // because each has a different transform
                RenderObjClass* spanCopy = nullptr;
                if (s == 0)
                {
                    spanCopy = sectionRObj;
                    // Don't release - keeping the reference
                }
                else
                {
                    spanCopy = CreateRenderObjCompat(spanMeshName, 1.0f, 0xFFFFFF, nullptr, nullptr);
                    if (!spanCopy)
                        continue;
                }

                if (spanCopy->Class_ID() == RenderObjClass::CLASSID_MESH)
                {
                    static_cast<MeshClass*>(spanCopy)->Set_Transform(worldMtx);
                    BridgePiece piece;
                    piece.renderObj = spanCopy;
                    bridgeInst.pieces.push_back(piece);
                }
                else
                {
                    spanCopy->Release_Ref();
                }
            }

            // Right piece
            {
                float pieceXOffset = xOffset + (numSpans - 1) * spanLength;
                Matrix3D worldMtx = buildPieceTransform(rightMtx, pieceXOffset);
                rightMesh->Set_Transform(worldMtx);
                BridgePiece piece;
                piece.renderObj = rightRObj;
                bridgeInst.pieces.push_back(piece);
            }
        }
        else
        {
            // Fixed bridge - only left mesh, release unused
            if (sectionRObj) { sectionRObj->Release_Ref(); sectionRObj = nullptr; }
            if (rightRObj) { rightRObj->Release_Ref(); rightRObj = nullptr; }
        }

        m_bridges.push_back(std::move(bridgeInst));
        bridgeCount++;

        // Advance past the second bridge point
        obj = obj2;
    }

    m_bridgesReady = (bridgeCount > 0);
    AppendTerrainTrace("BuildBridgeMeshes: %d bridges built, ready=%d\n",
        bridgeCount, m_bridgesReady ? 1 : 0);
}

void TerrainRenderer::RenderBridges(CameraClass* camera)
{
    if (!m_bridgesReady || m_bridges.empty() || !camera)
        return;

    // Render bridge meshes — don't call BeginFrame here as it would overwrite
    // the constant buffer set by the main W3DView::draw() BeginFrame.
    auto& modelRenderer = ModelRenderer::Instance();
    Renderer::Instance().Restore3DState();

    for (const auto& bridge : m_bridges)
    {
        for (const auto& piece : bridge.pieces)
        {
            if (piece.renderObj)
            {
                modelRenderer.RenderRenderObject(piece.renderObj);
            }
        }
    }
}

// ============================================================================
// Sky Box rendering
// ============================================================================

void TerrainRenderer::BuildSkyBoxMesh()
{
    if (m_skyBoxReady)
        return;

    auto& device = Renderer::Instance().GetDevice();

    // Build a sky box as 5 quads (N, E, S, W, Top).
    // The box extends from -1 to +1 in each direction. The world transform
    // will scale it and position it around the camera each frame.
    //
    // In the game coordinate system:
    //   X = east/west
    //   Y = north/south
    //   Z = up
    //
    // We use the same vertex format as the 3D renderer (Vertex3D).
    // Normals point inward so back-face culling shows the inside.

    // Each face is a quad: 4 vertices, 6 indices (2 triangles).
    // UV coordinates map the full [0,1] range to each face texture.

    const float S = 1.0f; // half-extent

    // Face definitions: each face has 4 corner positions.
    // Winding order is set so the face normal points inward (toward camera).
    struct FaceVerts {
        Render::Float3 v[4];
    };

    // North face: faces south (negative Y side of the box, camera looks toward +Y to see it)
    // Actually, in C&C Generals coordinate system:
    //   The camera looks roughly toward -Y (south) and +Y is "into the screen" (north).
    //   North face = the face at +Y, visible when camera looks north (+Y direction).
    // The original sky box model "new_skybox" uses N/E/S/W/T textures on the faces.
    // We define the box faces so they map correctly.

    FaceVerts faces[SKY_FACE_COUNT];

    // Vertices are ordered so that when viewed from INSIDE the box, each
    // face's triangle winding is counter-clockwise (CCW).  The default
    // rasterizer uses FrontCounterClockwise=TRUE and culls back faces,
    // so CCW triangles (as seen by the camera inside) are front-facing.
    //
    // For each face the quad is: (0,1,2) and (0,2,3) with indices below.

    // North face (at +Y, camera inside looks toward +Y)
    faces[SKY_FACE_N].v[0] = {  S,  S, -S }; // bottom-right when viewed from inside
    faces[SKY_FACE_N].v[1] = { -S,  S, -S }; // bottom-left
    faces[SKY_FACE_N].v[2] = { -S,  S,  S }; // top-left
    faces[SKY_FACE_N].v[3] = {  S,  S,  S }; // top-right

    // East face (at +X, camera inside looks toward +X)
    faces[SKY_FACE_E].v[0] = {  S, -S, -S };
    faces[SKY_FACE_E].v[1] = {  S,  S, -S };
    faces[SKY_FACE_E].v[2] = {  S,  S,  S };
    faces[SKY_FACE_E].v[3] = {  S, -S,  S };

    // South face (at -Y, camera inside looks toward -Y)
    faces[SKY_FACE_S].v[0] = { -S, -S, -S };
    faces[SKY_FACE_S].v[1] = {  S, -S, -S };
    faces[SKY_FACE_S].v[2] = {  S, -S,  S };
    faces[SKY_FACE_S].v[3] = { -S, -S,  S };

    // West face (at -X, camera inside looks toward -X)
    faces[SKY_FACE_W].v[0] = { -S,  S, -S };
    faces[SKY_FACE_W].v[1] = { -S, -S, -S };
    faces[SKY_FACE_W].v[2] = { -S, -S,  S };
    faces[SKY_FACE_W].v[3] = { -S,  S,  S };

    // Top face (at +Z, camera inside looks up toward +Z)
    faces[SKY_FACE_T].v[0] = { -S, -S,  S };
    faces[SKY_FACE_T].v[1] = {  S, -S,  S };
    faces[SKY_FACE_T].v[2] = {  S,  S,  S };
    faces[SKY_FACE_T].v[3] = { -S,  S,  S };

    // Index data for a quad (2 triangles, CCW winding when viewed from inside)
    const uint16_t quadIndices[6] = { 0, 1, 2, 0, 2, 3 };

    for (int face = 0; face < SKY_FACE_COUNT; ++face)
    {
        Vertex3D verts[4];
        for (int v = 0; v < 4; ++v)
        {
            verts[v].position = faces[face].v[v];
            verts[v].normal = { 0.0f, 0.0f, 0.0f }; // Sky doesn't need lighting normals
            verts[v].color = 0xFFFFFFFF; // White, full alpha
        }
        // UV mapping: v0=bottom-right, v1=bottom-left, v2=top-left, v3=top-right
        // (as seen from inside the box)
        verts[0].texcoord = { 1.0f, 1.0f }; // bottom-right
        verts[1].texcoord = { 0.0f, 1.0f }; // bottom-left
        verts[2].texcoord = { 0.0f, 0.0f }; // top-left
        verts[3].texcoord = { 1.0f, 0.0f }; // top-right

        m_skyVertexBuffers[face].Create(device, verts, 4, sizeof(Vertex3D));
        m_skyIndexBuffers[face].Create(device, quadIndices, 6);
    }

    m_skyBoxReady = true;
    AppendTerrainTrace("TerrainRenderer::BuildSkyBoxMesh: sky box mesh built\n");
}

void TerrainRenderer::RenderSkyBox(CameraClass* camera)
{
    if (camera == nullptr)
        return;

    // Always attempt skybox rendering — the original gate on m_drawSkyBox
    // relied on the mission script calling doSkyBox(true) before the first
    // render frame. If the script fires late or the flag is never set, the
    // player sees the clear color (near-black) instead of sky. The overhead
    // of attempting skybox rendering is negligible, and if no sky textures
    // are found, we fall back to a solid blue sky below.
    if (TheGlobalData == nullptr)
        return;

    // Build the sky box mesh on first use
    if (!m_skyBoxReady)
        BuildSkyBoxMesh();

    if (!m_skyBoxReady)
        return;

    static int s_loggedFrames = 0;

    auto& renderer = Renderer::Instance();

    // Save current frame constants (camera, lighting) so we can restore after skybox
    renderer.PushFrameConstants();

    // Rotation-only view matrix: strip translation so skybox appears at infinity.
    // VSMainSkybox forces depth=1.0 so everything else draws on top.
    {
        Matrix3D camViewMtx;
        camera->Get_View_Matrix(&camViewMtx);
        RenderUtils::SanitizeMatrix3D(camViewMtx);

        Matrix4x4 projMtx;
        camera->Get_D3D_Projection_Matrix(&projMtx);

        Render::Float4x4 skyView = RenderUtils::Matrix3DToRotationOnlyFloat4x4(camViewMtx);
        Render::Float4x4 skyProj = RenderUtils::Matrix4x4ToFloat4x4(projMtx);

        renderer.SetCamera(skyView, skyProj, { 0, 0, 0 });
    }

    // Unlit white ambient so sky textures show at full brightness
    renderer.SetAmbientLight({ 1.0f, 1.0f, 1.0f, 1.0f });
    renderer.SetDirectionalLights(nullptr, nullptr, 0);
    renderer.FlushFrameConstants();

    // Unit-scale skybox centered at origin (rotation-only camera handles framing)
    Render::Float4x4 worldMatrix;
    DirectX::XMStoreFloat4x4(&ToXM(worldMatrix), DirectX::XMMatrixIdentity());

    Render::Float4 white = { 1.0f, 1.0f, 1.0f, 1.0f };

    // Use skybox shader: VSMainSkybox writes depth=1.0, normal depth test enabled
    renderer.SetSkybox3DState();

    // Get sky texture names from WaterTransparencySetting.
    // The OVERRIDE<> operator-> follows the override chain automatically.
    const char* skyTextureNames[SKY_FACE_COUNT] = { nullptr };
    if (TheWaterTransparency != nullptr)
    {
        if (!TheWaterTransparency->m_skyboxTextureN.isEmpty()) skyTextureNames[SKY_FACE_N] = TheWaterTransparency->m_skyboxTextureN.str();
        if (!TheWaterTransparency->m_skyboxTextureE.isEmpty()) skyTextureNames[SKY_FACE_E] = TheWaterTransparency->m_skyboxTextureE.str();
        if (!TheWaterTransparency->m_skyboxTextureS.isEmpty()) skyTextureNames[SKY_FACE_S] = TheWaterTransparency->m_skyboxTextureS.str();
        if (!TheWaterTransparency->m_skyboxTextureW.isEmpty()) skyTextureNames[SKY_FACE_W] = TheWaterTransparency->m_skyboxTextureW.str();
        if (!TheWaterTransparency->m_skyboxTextureT.isEmpty()) skyTextureNames[SKY_FACE_T] = TheWaterTransparency->m_skyboxTextureT.str();
    }

    // Also try WaterSettings for sky texture if WaterTransparency doesn't have useful names
    if (skyTextureNames[SKY_FACE_N] == nullptr)
    {
        const int timeOfDay = TheGlobalData ? static_cast<int>(TheGlobalData->m_timeOfDay) : static_cast<int>(TIME_OF_DAY_MORNING);
        const WaterSetting& waterSetting = WaterSettings[timeOfDay];
        if (!waterSetting.m_skyTextureFile.isEmpty())
        {
            // Use the single sky texture for all faces as a fallback
            for (int i = 0; i < SKY_FACE_COUNT; ++i)
                skyTextureNames[i] = waterSetting.m_skyTextureFile.str();
        }
    }

    auto& device = renderer.GetDevice();

    bool anySkyFaceRendered = false;
    for (int face = 0; face < SKY_FACE_COUNT; ++face)
    {
        Texture* tex = nullptr;
        if (skyTextureNames[face] != nullptr)
            tex = ImageCache::Instance().GetTexture(device, skyTextureNames[face]);

        // Skip faces without textures
        if (tex == nullptr)
            continue;

        renderer.Draw3D(m_skyVertexBuffers[face], m_skyIndexBuffers[face],
            tex, worldMatrix, white);
        anySkyFaceRendered = true;
    }

    // If no sky textures were found, render a solid blue sky so the player
    // doesn't see the dark clear-color through the sky region.
    if (!anySkyFaceRendered)
    {
        // Create a 1x1 solid blue texture as fallback
        static Texture s_fallbackSkyTex;
        static bool s_fallbackCreated = false;
        if (!s_fallbackCreated)
        {
            uint32_t bluePixel = 0xFFA06830; // ABGR: opaque, R=0x30, G=0x68, B=0xA0 (muted sky blue)
            s_fallbackSkyTex.CreateFromRGBA(device, &bluePixel, 1, 1, false);
            s_fallbackCreated = true;
        }
        for (int face = 0; face < SKY_FACE_COUNT; ++face)
        {
            renderer.Draw3D(m_skyVertexBuffers[face], m_skyIndexBuffers[face],
                &s_fallbackSkyTex, worldMatrix, white);
        }
    }

    // --- Sky plane and moon use world-space positions, so restore normal camera ---
    renderer.PopFrameConstants(); // restores camera + lighting from before skybox
    renderer.PushFrameConstants(); // re-push so we can modify lighting for sky plane

    // Override lighting for unlit sky plane
    renderer.SetAmbientLight({ 1.0f, 1.0f, 1.0f, 1.0f });
    renderer.SetDirectionalLights(nullptr, nullptr, 0);
    renderer.FlushFrameConstants();

    const Vector3 camPos = camera->Get_Position();

    // --- Sky plane: animated scrolling cloud/sky layer ---
    {
        const int timeOfDay = TheGlobalData ? static_cast<int>(TheGlobalData->m_timeOfDay) : static_cast<int>(TIME_OF_DAY_MORNING);
        const WaterSetting& waterSetting = WaterSettings[timeOfDay];
        const char* skyTexFile = waterSetting.m_skyTextureFile.isEmpty() ? nullptr : waterSetting.m_skyTextureFile.str();
        Texture* skyPlaneTex = skyTexFile ? ImageCache::Instance().GetTexture(device, skyTexFile) : nullptr;

        if (skyPlaneTex)
        {
            const float SKYPLANE_HEIGHT = 30.0f;
            const float SKYPLANE_SIZE = 384.0f * TERRAIN_XY_FACTOR; // 3840 world units

            // UV scrolling based on elapsed time and per-TOD scroll rates
            const float timeMs = static_cast<float>(WW3D::Get_Sync_Time());
            float skyTexelsPerUnit = waterSetting.m_skyTexelsPerUnit;
            if (skyTexelsPerUnit == 0.0f) skyTexelsPerUnit = 1.0f / SKYPLANE_SIZE;
            float uOff = timeMs * waterSetting.m_uScrollPerMs * skyTexelsPerUnit;
            float vOff = timeMs * waterSetting.m_vScrollPerMs * skyTexelsPerUnit;
            // Clamp to [0,1) to avoid precision loss
            uOff = uOff - floorf(uOff);
            vOff = vOff - floorf(vOff);

            // Build 4-vertex quad centered on camera
            float halfSize = SKYPLANE_SIZE * 0.5f;
            float px = camPos.X;
            float py = camPos.Y;
            float pz = SKYPLANE_HEIGHT;

            Vertex3D skyVerts[4] = {};
            // Per-vertex diffuse colors from WaterSettings (darker at horizon).
            // Original has 4 corner colors creating a gradient across the sky plane.
            // Convert ARGB to ABGR for D3D11 R8G8B8A8_UNORM.
            auto argbToAbgr = [](const RGBAColorInt& c) -> uint32_t {
                return ((uint32_t)c.alpha << 24) | ((uint32_t)c.blue << 16) | ((uint32_t)c.green << 8) | (uint32_t)c.red;
            };
            uint32_t c00 = argbToAbgr(waterSetting.m_vertex00Diffuse);
            uint32_t c10 = argbToAbgr(waterSetting.m_vertex10Diffuse);
            uint32_t c11 = argbToAbgr(waterSetting.m_vertex11Diffuse);
            uint32_t c01 = argbToAbgr(waterSetting.m_vertex01Diffuse);
            // If all corners are zero (no INI data), use white
            if ((c00 | c10 | c11 | c01) == 0) { c00 = c10 = c11 = c01 = 0xFFFFFFFF; }

            skyVerts[0].position = { px - halfSize, py - halfSize, pz };
            skyVerts[0].texcoord = { 0.0f + uOff, 0.0f + vOff };
            skyVerts[0].normal = { 0, 0, -1 };
            skyVerts[0].color = c00;

            skyVerts[1].position = { px + halfSize, py - halfSize, pz };
            skyVerts[1].texcoord = { 1.0f + uOff, 0.0f + vOff };
            skyVerts[1].normal = { 0, 0, -1 };
            skyVerts[1].color = c10;

            skyVerts[2].position = { px + halfSize, py + halfSize, pz };
            skyVerts[2].texcoord = { 1.0f + uOff, 1.0f + vOff };
            skyVerts[2].normal = { 0, 0, -1 };
            skyVerts[2].color = c11;

            skyVerts[3].position = { px - halfSize, py + halfSize, pz };
            skyVerts[3].texcoord = { 0.0f + uOff, 1.0f + vOff };
            skyVerts[3].normal = { 0, 0, -1 };
            skyVerts[3].color = c01;

            uint32_t skyIdx[6] = { 0, 1, 2, 0, 2, 3 };

            static VertexBuffer s_skyPlaneVB;
            static IndexBuffer s_skyPlaneIB;
            static bool s_skyPlaneReady = false;
            if (!s_skyPlaneReady)
            {
                s_skyPlaneVB.Create(device, nullptr, 4, sizeof(Vertex3D), true);
                s_skyPlaneIB.Create32(device, skyIdx, 6, false);
                s_skyPlaneReady = true;
            }
            s_skyPlaneVB.Update(device, skyVerts, sizeof(skyVerts));

            Render::Float4x4 identity;
            DirectX::XMStoreFloat4x4(&ToXM(identity), DirectX::XMMatrixIdentity());

            renderer.SetAlphaBlend3DState();
            renderer.SetDepthDisabled();
            renderer.Draw3D(s_skyPlaneVB, s_skyPlaneIB, skyPlaneTex, identity, white);
        }
    }

    // --- Moon/sun billboard: rendered at night only ---
    // Original: TSMoonLarg.tga at world position (150, 550, 30), size 45, camera-facing.
    {
        const TimeOfDay tod = TheGlobalData ? TheGlobalData->m_timeOfDay : TIME_OF_DAY_MORNING;
        if (tod == TIME_OF_DAY_NIGHT)
        {
            Texture* moonTex = ImageCache::Instance().GetTexture(device, "TSMoonLarg.tga");
            if (!moonTex) moonTex = ImageCache::Instance().GetTexture(device, "tsmoonlarg.tga");
            if (!moonTex) moonTex = ImageCache::Instance().GetTexture(device, "TSMoonLarg.dds");
            if (moonTex)
            {
                const float moonX = 150.0f, moonY = 550.0f, moonZ = 30.0f;
                const float moonSize = 45.0f;
                const float halfMoon = moonSize * 0.5f;

                // Camera-facing billboard using inverse view matrix rows
                Matrix3D viewMtx;
                camera->Get_View_Matrix(&viewMtx);
                // View matrix rows 0,1 are the camera right/up in world space
                float rx = viewMtx[0][0], ry = viewMtx[1][0], rz = viewMtx[2][0];
                float ux = viewMtx[0][1], uy = viewMtx[1][1], uz = viewMtx[2][1];

                Vertex3D moonV[4] = {};
                float mrx = rx * halfMoon, mry = ry * halfMoon, mrz = rz * halfMoon;
                float mux = ux * halfMoon, muy = uy * halfMoon, muz = uz * halfMoon;

                moonV[0].position = { moonX - mrx - mux, moonY - mry - muy, moonZ - mrz - muz };
                moonV[0].texcoord = { 0, 1 }; moonV[0].normal = {0,0,1}; moonV[0].color = 0xFFFFFFFF;
                moonV[1].position = { moonX + mrx - mux, moonY + mry - muy, moonZ + mrz - muz };
                moonV[1].texcoord = { 1, 1 }; moonV[1].normal = {0,0,1}; moonV[1].color = 0xFFFFFFFF;
                moonV[2].position = { moonX + mrx + mux, moonY + mry + muy, moonZ + mrz + muz };
                moonV[2].texcoord = { 1, 0 }; moonV[2].normal = {0,0,1}; moonV[2].color = 0xFFFFFFFF;
                moonV[3].position = { moonX - mrx + mux, moonY - mry + muy, moonZ - mrz + muz };
                moonV[3].texcoord = { 0, 0 }; moonV[3].normal = {0,0,1}; moonV[3].color = 0xFFFFFFFF;

                uint32_t moonIdx[6] = { 0, 1, 2, 0, 2, 3 };

                static VertexBuffer s_moonVB;
                static IndexBuffer s_moonIB;
                static bool s_moonReady = false;
                if (!s_moonReady) {
                    s_moonVB.Create(device, nullptr, 4, sizeof(Vertex3D), true);
                    s_moonIB.Create32(device, moonIdx, 6, false);
                    s_moonReady = true;
                }
                s_moonVB.Update(device, moonV, sizeof(moonV));

                Render::Float4x4 identity;
                DirectX::XMStoreFloat4x4(&ToXM(identity), DirectX::XMMatrixIdentity());

                renderer.SetAlphaBlend3DState();
                renderer.SetDepthWriteEnabled(false);
                renderer.Draw3D(s_moonVB, s_moonIB, moonTex, identity, white);
            }
        }
    }

    // Restore camera + lighting from before skybox
    renderer.PopFrameConstants();
    renderer.Restore3DState();

    if (s_loggedFrames < 3)
    {
        AppendTerrainTrace(
            "TerrainRenderer::RenderSkyBox frame=%d\n", s_loggedFrames);
        ++s_loggedFrames;
    }
}

// ============================================================================
// Shroud / Fog of War
// ============================================================================

void TerrainRenderer::ClearShroud()
{
    if (!m_shroudGrid.empty())
    {
        const uint8_t shroudLevel = TheGlobalData ? TheGlobalData->m_shroudAlpha : 0;
        memset(m_shroudGrid.data(), shroudLevel, m_shroudGrid.size());
    }
    m_shroudDirty = true;
}

void TerrainRenderer::SetShroudLevel(int x, int y, uint8_t alpha)
{
    // Logic shroud coordinates cover the playable terrain cells only. They do
    // not include the visual border vertices around the heightmap. Mapping them
    // 1:1 onto the full heightmap extents makes the reveal area too small and
    // squashes it into a coarse rectangle in one corner of the map.
    if (m_shroudGrid.empty())
    {
        int targetWidth = 0;
        int targetHeight = 0;

        // Use partition dimensions when available so logic and render grids are
        // always identical, even if map border/layout differs from assumptions.
        if (ThePartitionManager && ThePartitionManager->getCellSize() > 0.0f)
        {
            targetWidth = ThePartitionManager->getCellCountX();
            targetHeight = ThePartitionManager->getCellCountY();
        }

        // Fallback for very early calls before partition init.
        if (targetWidth <= 0 || targetHeight <= 0)
        {
            if (m_mapWidth > 0 && m_mapHeight > 0)
            {
                targetWidth = std::max(0, m_mapWidth - 1 - (m_borderSize * 2));
                targetHeight = std::max(0, m_mapHeight - 1 - (m_borderSize * 2));
            }
        }

        if (targetWidth <= 0 || targetHeight <= 0)
            return;

        if (TheGlobalData)
            m_borderShroudLevel = TheGlobalData->m_shroudAlpha;

        m_shroudWidth = targetWidth;
        m_shroudHeight = targetHeight;

        // Start fully shrouded.
        const uint8_t shroudLevel = TheGlobalData ? TheGlobalData->m_shroudAlpha : 0;
        m_shroudGrid.resize(m_shroudWidth * m_shroudHeight, shroudLevel);
        m_shroudDirty = true;
        AppendTerrainTrace("TerrainRenderer::SetShroudLevel: allocated grid %dx%d\n",
            m_shroudWidth, m_shroudHeight);
    }

    if (x < 0 || y < 0 || x >= m_shroudWidth || y >= m_shroudHeight)
        return;

    uint8_t& cell = m_shroudGrid[y * m_shroudWidth + x];
    if (cell != alpha)
    {
        cell = alpha;
        m_shroudDirty = true;
    }
}

void TerrainRenderer::SetBorderShroudLevel(uint8_t level)
{
    if (m_borderShroudLevel != level)
    {
        m_borderShroudLevel = level;
        m_shroudDirty = true;
    }
}

void TerrainRenderer::BuildShroudMesh()
{
    // Build a terrain-conforming grid mesh for the shroud overlay.
    // Each vertex is positioned at the terrain height so the shroud
    // follows terrain contours (no parallax on hills).
    if (m_shroudWidth <= 0 || m_shroudHeight <= 0)
        return;

    auto& device = Renderer::Instance().GetDevice();

    // Downsample the grid for performance - use every Nth cell
    const int SHROUD_GRID_STEP = 2;
    int gridW = (m_shroudWidth / SHROUD_GRID_STEP) + 1;
    int gridH = (m_shroudHeight / SHROUD_GRID_STEP) + 1;

    // Get heightmap for terrain height sampling
    WorldHeightMap* hm = GetTerrainHeightMap();
    int borderSize = hm ? hm->getBorderSize() : 0;

    std::vector<Vertex3D> verts(gridW * gridH);
    for (int gy = 0; gy < gridH; ++gy)
    {
        for (int gx = 0; gx < gridW; ++gx)
        {
            int cx = gx * SHROUD_GRID_STEP;
            int cy = gy * SHROUD_GRID_STEP;
            if (cx >= m_shroudWidth) cx = m_shroudWidth - 1;
            if (cy >= m_shroudHeight) cy = m_shroudHeight - 1;

            float wx = (float)cx * TERRAIN_XY_FACTOR;
            float wy = (float)cy * TERRAIN_XY_FACTOR;
            float wz = 0.0f;
            if (hm)
            {
                int hx = cx + borderSize;
                int hy = cy + borderSize;
                if (hx >= 0 && hx < hm->getXExtent() && hy >= 0 && hy < hm->getYExtent())
                    wz = hm->getHeight(hx, hy) * TERRAIN_HEIGHT_SCALE + 0.05f; // tiny offset above terrain
            }

            float u = (float)cx / (float)m_shroudWidth;
            float v = (float)cy / (float)m_shroudHeight;

            int idx = gy * gridW + gx;
            verts[idx].position = { wx, wy, wz };
            verts[idx].normal = { 0, 0, 1 };
            verts[idx].texcoord = { u, v };
            verts[idx].color = 0xFFFFFFFF;
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve((gridW - 1) * (gridH - 1) * 6);
    for (int gy = 0; gy < gridH - 1; ++gy)
    {
        for (int gx = 0; gx < gridW - 1; ++gx)
        {
            uint32_t tl = gy * gridW + gx;
            uint32_t tr = tl + 1;
            uint32_t bl = (gy + 1) * gridW + gx;
            uint32_t br = bl + 1;
            indices.push_back(tl); indices.push_back(tr); indices.push_back(bl);
            indices.push_back(tr); indices.push_back(br); indices.push_back(bl);
        }
    }

    if (!m_shroudVertexBuffer.Create(device, verts.data(), (uint32_t)verts.size(), sizeof(Vertex3D), false))
        return;
    if (!m_shroudIndexBuffer.Create32(device, indices.data(), (uint32_t)indices.size(), false))
        return;

    m_shroudVertexCount = (uint32_t)verts.size();
    m_shroudIndexCount = (uint32_t)indices.size();
    m_shroudMeshReady = true;

    AppendTerrainTrace("TerrainRenderer::BuildShroudMesh: grid %dx%d (%d verts, %d indices)\n",
        gridW, gridH, (int)verts.size(), (int)indices.size());
}

void TerrainRenderer::UpdateShroudTexture()
{
    if (m_shroudGrid.empty() || m_shroudWidth <= 0 || m_shroudHeight <= 0)
        return;

    auto& device = Renderer::Instance().GetDevice();
    const int shroudTextureWidth = m_shroudWidth + 2;
    const int shroudTextureHeight = m_shroudHeight + 2;

    if (m_shroudTextureReady &&
        (m_shroudTexture.GetWidth() != (uint32_t)shroudTextureWidth ||
         m_shroudTexture.GetHeight() != (uint32_t)shroudTextureHeight))
    {
        m_shroudTextureReady = false;
        m_shroudTexture = Texture();
    }

    // Create the dynamic texture on first use
    if (!m_shroudTextureReady)
    {
        if (!m_shroudTexture.CreateDynamic(device, shroudTextureWidth, shroudTextureHeight))
        {
            AppendTerrainTrace("TerrainRenderer::UpdateShroudTexture: CreateDynamic FAILED %dx%d\n",
                shroudTextureWidth, shroudTextureHeight);
            return;
        }
        m_shroudTextureReady = true;
        AppendTerrainTrace("TerrainRenderer::UpdateShroudTexture: created dynamic texture %dx%d\n",
            shroudTextureWidth, shroudTextureHeight);
    }

    // Convert the uint8_t brightness grid into RGBA pixels for multiplicative blend.
    // With multiplicative blend (dest * src_color), the RGB value controls terrain brightness:
    //   brightness=0   -> dest * 0 = black (fully shrouded)
    //   brightness=127 -> dest * 0.5 = half brightness (fogged)
    //   brightness=255 -> dest * 1 = unchanged (clear)
    // This matches the original D3D8 approach of SRCBLEND_ZERO + DSTBLEND_SRC_COLOR.
    auto packBrightness = [](uint8_t brightness) -> uint32_t
    {
        return 0xFF000000
            | (static_cast<uint32_t>(brightness))
            | (static_cast<uint32_t>(brightness) << 8)
            | (static_cast<uint32_t>(brightness) << 16);
    };

    uint8_t borderBrightness = m_borderShroudLevel;
    if (TheGlobalData && borderBrightness < TheGlobalData->m_shroudAlpha)
        borderBrightness = TheGlobalData->m_shroudAlpha;

    // Include a 1-texel border around playable cells so sampling just outside
    // map bounds uses explicit border shroud level instead of edge-cell replication.
    std::vector<uint32_t> pixels(shroudTextureWidth * shroudTextureHeight, packBrightness(borderBrightness));

    for (int y = 0; y < m_shroudHeight; ++y)
        for (int x = 0; x < m_shroudWidth; ++x)
            pixels[(y + 1) * shroudTextureWidth + (x + 1)] =
                packBrightness(m_shroudGrid[y * m_shroudWidth + x]);

    m_shroudTexture.UpdateFromRGBA(device, pixels.data(), shroudTextureWidth, shroudTextureHeight);
    m_shroudDirty = false;
}

void TerrainRenderer::RenderShroud(CameraClass* camera)
{
    // Fog of war is now computed per-pixel in PSMain via ApplyShroud().
    // The shroud texture is bound during Render() before terrain drawing.
    // This method handles deferred shroud texture updates only.
    (void)camera;

    if (m_shroudGrid.empty())
        return;

    if (m_shroudDirty)
        UpdateShroudTexture();
}

} // namespace Render
