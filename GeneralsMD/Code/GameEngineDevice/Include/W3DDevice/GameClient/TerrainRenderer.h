#pragma once

#include "Renderer.h"
#include <cstdint>
#include <vector>
#include <string>

class WorldHeightMap;
class CameraClass;
class RenderObjClass;
class MeshClass;

// Frustum planes for view culling (used by both terrain and model rendering)
struct FrustumPlanes
{
    Render::Float4 planes[6];
    void ExtractFromViewProj(const Render::Float4x4& viewProj);
    bool TestAABB(const Render::Float3& aabbMin, const Render::Float3& aabbMax) const;
    bool TestSphere(const Render::Float3& center, float radius) const;
};

void ExtractFrustumFromCamera(CameraClass* camera, FrustumPlanes& out);

namespace Render
{

class TerrainRenderer
{
public:
    static TerrainRenderer& Instance();

    void BuildMesh(WorldHeightMap* heightMap);
    void Render(CameraClass* camera, WorldHeightMap* heightMap = nullptr);
    void RenderSkyBox(CameraClass* camera);
    void RenderWater(CameraClass* camera);
    void RenderRoads(CameraClass* camera);
    void RenderBridges(CameraClass* camera);
    void RenderShroud(CameraClass* camera);
    void Shutdown();

    bool IsReady() const { return m_ready; }
    float GetMaxWaterHeight() const { return m_maxWaterHeight; }
    void Invalidate();

    void BuildRoadMesh(WorldHeightMap* heightMap);
    void BuildBridgeMeshes(WorldHeightMap* heightMap);

    // Shroud interface - called by W3DDisplay
    void ClearShroud();
    void SetShroudLevel(int x, int y, uint8_t alpha);
    void SetBorderShroudLevel(uint8_t level);

private:
    TerrainRenderer() = default;

    void BuildTerrainTextureAtlas(WorldHeightMap* heightMap);
    void BuildEdgeTextureAtlas(WorldHeightMap* heightMap);
    void BuildWaterMesh();
    void BuildSkyBoxMesh();
    void BuildShroudMesh();
    void UpdateShroudTexture();

    VertexBuffer m_vertexBuffer;
    IndexBuffer  m_indexBuffer;
    VertexBuffer m_blendVertexBuffer;
    IndexBuffer  m_blendIndexBuffer;
    VertexBuffer m_extraBlendVertexBuffer;
    IndexBuffer  m_extraBlendIndexBuffer;
    VertexBuffer m_waterVertexBuffer;
    IndexBuffer  m_waterIndexBuffer;
    std::vector<Vertex3D> m_waterVerticesCPU; // CPU-side copy for UV animation
    Texture      m_terrainTexture;
    Texture      m_waterFallbackTexture;

    // Water reflection render target (256x256)
    Texture      m_reflectionRT;
    bool         m_reflectionRTReady = false;

    // Road rendering
    struct RoadBatch
    {
        VertexBuffer vertexBuffer;
        IndexBuffer indexBuffer;
        Texture texture;
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0;
    };
    std::vector<RoadBatch> m_roadBatches;
    bool m_roadsReady = false;

    // Bridge rendering - uses the same batch structure as roads
    // Each bridge piece (left/section/right) becomes one or more batches
    struct BridgePiece
    {
        RenderObjClass* renderObj = nullptr; // Loaded W3D mesh piece with transform set
    };
    struct BridgeInstance
    {
        std::vector<BridgePiece> pieces;
    };
    std::vector<BridgeInstance> m_bridges;
    bool m_bridgesReady = false;

    uint32_t m_indexCount = 0;
    uint32_t m_vertexCount = 0;
    uint32_t m_blendIndexCount = 0;
    uint32_t m_blendVertexCount = 0;
    uint32_t m_extraBlendIndexCount = 0;
    uint32_t m_extraBlendVertexCount = 0;

    // Custom terrain edging - renders edge tile artwork at terrain transitions
    // where the map designer specified explicit edge textures (customBlendEdgeClass >= 0)
    // instead of simple alpha blending.
    VertexBuffer m_edgingVertexBuffer;
    IndexBuffer  m_edgingIndexBuffer;
    VertexBuffer m_edgingBaseVertexBuffer;
    IndexBuffer  m_edgingBaseIndexBuffer;
    Texture      m_edgingTexture;
    uint32_t m_edgingIndexCount = 0;
    uint32_t m_edgingVertexCount = 0;
    uint32_t m_edgingBaseIndexCount = 0;
    uint32_t m_edgingBaseVertexCount = 0;
    int m_edgingAtlasWidth = 0;
    int m_edgingAtlasHeight = 0;
    bool m_edgingTextureCreated = false;

    uint32_t m_waterIndexCount = 0;
    uint32_t m_waterVertexCount = 0;

    int m_mapWidth = 0;
    int m_mapHeight = 0;
    int m_borderSize = 0;
    int m_atlasWidth = 0;
    int m_atlasHeight = 0;
    int m_waterBuildTimeOfDay = -1;
    float m_maxWaterHeight = 0.0f;
    Render::Float3 m_debugSampleWorld = { 0.0f, 0.0f, 0.0f };

    bool m_ready = false;
    bool m_waterReady = false;
    bool m_textureCreated = false;
    bool m_waterFallbackTextureReady = false;

    // Sky box rendering
    enum { SKY_FACE_N = 0, SKY_FACE_E, SKY_FACE_S, SKY_FACE_W, SKY_FACE_T, SKY_FACE_COUNT };
    VertexBuffer m_skyVertexBuffers[SKY_FACE_COUNT];
    IndexBuffer  m_skyIndexBuffers[SKY_FACE_COUNT];
    bool m_skyBoxReady = false;

    // Shroud/fog of war rendering
    static const int MAX_SHROUD_WIDTH = 512;
    static const int MAX_SHROUD_HEIGHT = 512;
    std::vector<uint8_t> m_shroudGrid;       // alpha per cell: 0=clear, 128=fogged, 255=shrouded
    int m_shroudWidth = 0;                    // matches partition grid width (shroud cells)
    int m_shroudHeight = 0;                   // matches partition grid height (shroud cells)
    uint8_t m_borderShroudLevel = 255;        // shroud level for border cells
    bool m_shroudDirty = true;                // true when texture needs re-upload
    bool m_shroudMeshReady = false;
    bool m_shroudTextureReady = false;
    Texture m_shroudTexture;                  // dynamic GPU texture
    VertexBuffer m_shroudVertexBuffer;
    IndexBuffer  m_shroudIndexBuffer;
    uint32_t m_shroudIndexCount = 0;
    uint32_t m_shroudVertexCount = 0;
};

} // namespace Render
