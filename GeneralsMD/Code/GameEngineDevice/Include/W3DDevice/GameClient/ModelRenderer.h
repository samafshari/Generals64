#pragma once

#include "Renderer.h"
#include "W3DDevice/GameClient/TerrainRenderer.h" // for FrustumPlanes
#include <unordered_map>
#include <vector>

class CameraClass;
class RenderObjClass;
class MeshClass;
class MeshModelClass;
class TextureClass;
class HLodClass;
class DistLODClass;
class Matrix3D;

namespace Render
{

class ModelRenderer
{
public:
    static ModelRenderer& Instance();

    enum MeshBlendMode
    {
        BLEND_OPAQUE = 0,
        BLEND_ALPHA_TEST,
        BLEND_ALPHA,
        BLEND_ADDITIVE,
        BLEND_ADDITIVE_ALPHA,
    };

    void BeginFrame(CameraClass* camera);
    void RenderRenderObject(RenderObjClass* renderObject, bool isSubObject = false);
    void SetFogDarkening(float multiplier) { m_fogDarkening = multiplier; }
    // Additive tint applied to opaque drawables (selection glow, capture flash,
    // damage pulse). Drives the legacy DX8-style "add tint to vertex color"
    // path inside ComputeMeshColor.
    void SetTintColor(float r, float g, float b) { m_tintR = r; m_tintG = g; m_tintB = b; }
    void ClearTintColor() { m_tintR = 0; m_tintG = 0; m_tintB = 0; }

    // Construction-ghost mode for placement previews. While enabled, RenderMesh
    // takes a dedicated path that binds the ghost shader + alpha-blend state and
    // outputs at `opacity`, optionally lerping the lit color toward `tintRGB`
    // by `tintIntensity`. Bypasses the normal opaque/alpha-test/translucent
    // batch logic entirely. Decals are skipped in ghost mode (a faction logo
    // on a translucent preview reads as noise).
    void SetGhostMode(float tintR, float tintG, float tintB,
                      float tintIntensity, float opacity);
    void ClearGhostMode();
    bool IsGhostMode() const { return m_ghostMode; }
    // Shadow caster mode: when true, RenderMesh skips per-batch state changes
    // (Restore3DState / SetAlphaTest3DState) so the externally-bound shadow
    // depth shader and shadow render-target stay in place. Used by the GPU
    // shadow-map pass to render unit/building meshes into the depth target.
    void SetShadowCasterMode(bool enabled) { m_shadowCasterMode = enabled; }
    bool IsInShadowCasterMode() const { return m_shadowCasterMode; }

    // Silhouette override mode: when set, ComputeMeshColor returns the
    // overrideColor unconditionally (ignoring HOUSECOLOR meshes, fog
    // darkening, tint envelopes, alpha override). Used by the occluded-
    // silhouette pass to draw every mesh as a flat colored fill regardless
    // of its texture or material. Pair with SetShadowCasterMode(true) to
    // also skip per-batch state changes and translucent batches.
    void SetSilhouetteOverride(const Render::Float4& color)
    {
        m_silhouetteMode = true;
        m_silhouetteColor = color;
    }
    void ClearSilhouetteOverride() { m_silhouetteMode = false; }
    void FlushTranslucent(); // sort and render deferred translucent meshes
    void Shutdown();

    // Particle buffers discovered on model sub-objects during rendering.
    // The scene iteration pass uses this to render them via RenderParticleBufferDX11.
    std::vector<RenderObjClass*> m_pendingParticleBuffers;
    void ClearPendingParticleBuffers() { m_pendingParticleBuffers.clear(); }

    // GPU projected mesh decals (faction logos on buildings). The decal
    // texture is grayscale art tinted at draw time by the owning render
    // object's house color; opacity comes from the source material pass.
    void AddMeshDecal(MeshModelClass* model, uint32_t decalID,
                      const Render::Float4x4& projMatrix,
                      TextureClass* decalTexture, float backfaceThreshold,
                      float opacity = 1.0f);
    void RemoveMeshDecal(MeshModelClass* model, uint32_t decalID);

    // 1x1 white texture used by external draw paths (e.g. waypoint lines,
    // tracers) that need to bind *something* to slot 0 so the unlit shader's
    // `texColor * vertex_color` evaluates to vertex_color instead of zero.
    Texture* GetWhiteTexture();

private:
    ModelRenderer() = default;

    struct StaticMeshBatch
    {
        VertexBuffer vertexBuffer;
        IndexBuffer indexBuffer;
        Texture* texture = nullptr;
        int pass = 0;
        MeshBlendMode blendMode = BLEND_OPAQUE;
        bool depthWrite = true;
        bool isDynamic = false;      // true for skinned meshes (VB updated each frame)
        bool isZhcaTexture = false;  // ZHCA_ "house icon" texture (Command
                                     // Center emblem etc.). Triggers a shader
                                     // branch that keys the dark background
                                     // to transparent and draws the foreground
                                     // in the player's house color.
    };

    struct MeshDecalInfo
    {
        Render::Float4x4 projMatrix;   // world -> decal UV space
        Texture* decalTexture = nullptr;
        uint32_t decalID = 0;
        float backfaceThreshold = 0.0f;
        float opacity = 1.0f;          // material opacity (multiplied with texture alpha)
    };

    struct StaticMeshCacheEntry
    {
        StaticMeshCacheEntry();
        ~StaticMeshCacheEntry();
        StaticMeshCacheEntry(const StaticMeshCacheEntry&) = delete;
        StaticMeshCacheEntry& operator=(const StaticMeshCacheEntry&) = delete;
        StaticMeshCacheEntry(StaticMeshCacheEntry&& that) noexcept;
        StaticMeshCacheEntry& operator=(StaticMeshCacheEntry&& that) noexcept;

        MeshModelClass* model = nullptr;
        std::vector<StaticMeshBatch> batches;
        std::vector<MeshDecalInfo> decals;
    };

    void RenderMesh(MeshClass* mesh);
    void RenderHLod(HLodClass* hlod);
    void RenderDistLod(DistLODClass* distLod);
    void RenderGenericChildren(RenderObjClass* renderObject);

    bool BuildStaticMeshCache(MeshClass* mesh, StaticMeshCacheEntry& cacheEntry);
    Texture* ResolveTexture(TextureClass* texture);

    static Render::Float4x4 ToWorldMatrix(const Matrix3D& matrix);
    Render::Float4 ComputeMeshColor(MeshClass* mesh, int pass) const;

    std::unordered_map<const MeshModelClass*, StaticMeshCacheEntry> m_staticMeshes;
    std::unordered_map<const TextureClass*, Texture> m_textureFromW3D;
    Texture m_whiteTexture;
    bool m_whiteTextureReady = false;
    CameraClass* m_camera = nullptr;

    // Frustum planes extracted once per frame in BeginFrame, used for model culling
    FrustumPlanes m_frustum;
    bool m_frustumValid = false;

    // Camera position cached for distance sorting
    float m_frameData_camX = 0, m_frameData_camY = 0, m_frameData_camZ = 0;

    // Team/house color from the top-level render object, propagated to sub-meshes.
    unsigned int m_currentObjectColor = 0;
    int          m_currentObjectShaderId = 0;

    // Fog darkening multiplier (1.0 = normal, 0.5 = fogged/half brightness)
    float m_fogDarkening = 1.0f;

    // Additive tint from Drawable's flash/selection envelopes (capture flash, selection glow)
    float m_tintR = 0, m_tintG = 0, m_tintB = 0;

    // Construction-ghost mode (placement preview). When true, RenderMesh binds
    // the ghost shader + alpha-blend state and emits at m_ghostOpacity, optionally
    // tinted by m_ghostTintR/G/B by m_ghostTintIntensity. Set/cleared by the
    // drawable-rendering shim around the placement icon's draw().
    bool  m_ghostMode = false;
    float m_ghostTintR = 0, m_ghostTintG = 0, m_ghostTintB = 0;
    float m_ghostTintIntensity = 0.0f; // 0 = no tint, 1 = full color replacement
    float m_ghostOpacity = 1.0f;

    // Current blend state tracking to avoid redundant D3D state changes
    MeshBlendMode m_currentBlendMode = BLEND_OPAQUE;

    // Shadow caster mode flag — when true, RenderMesh skips per-batch state
    // changes so the externally-bound shadow depth shader stays in place.
    bool m_shadowCasterMode = false;

    // Silhouette override mode — when true, ComputeMeshColor returns
    // m_silhouetteColor unconditionally (used by the occluded-unit
    // silhouette pass to draw every mesh as a flat colored fill).
    bool m_silhouetteMode = false;
    Render::Float4 m_silhouetteColor = { 1, 1, 1, 1 };

    // Deferred translucent draws: collected during RenderMesh, flushed sorted by distance
    struct DeferredDraw
    {
        const StaticMeshBatch* batch;
        Render::Float4x4 worldMatrix;
        Render::Float4 color;
        float distSq;              // squared distance to camera for sorting
        unsigned int objectColor;  // m_currentObjectColor at queue time —
                                   // needed for ZHCA icon recolor during flush
        int shaderId;              // m_currentObjectShaderId at queue time —
                                   // so the cosmetic shader effect applies to
                                   // the ZHCA icon during the translucent flush
    };
    std::vector<DeferredDraw> m_deferredTranslucent;
};

} // namespace Render
