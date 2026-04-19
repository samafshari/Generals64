#pragma once

#include "Core/Device.h"
#include "Core/Buffer.h"
#include "Core/Shader.h"
#include "Core/Texture.h"
#include "Math/RenderMath.h"

namespace Render
{

static constexpr uint32_t kMaxDirectionalLights = 3;
static constexpr uint32_t kMaxPointLights = 4;

// Per-frame constants shared by all shaders
struct alignas(16) FrameConstants
{
    Render::Float4x4 viewProjection;
    Render::Float4 cameraPos;
    Render::Float4 ambientColor;
    Render::Float4 lightDirections[kMaxDirectionalLights];
    Render::Float4 lightColors[kMaxDirectionalLights];
    Render::Float4 lightingOptions; // x = directional light count, y = time, z = point light count
    Render::Float4 pointLightPositions[kMaxPointLights]; // xyz = position, w = outer radius
    Render::Float4 pointLightColors[kMaxPointLights];    // rgb = color * intensity, a = inner radius
    Render::Float4 shroudParams; // x = 1/worldW, y = 1/worldH, z = worldOffsetX, w = worldOffsetY (0 = disabled)
    Render::Float4 atmosphereParams; // x = fog density, y = scatter power, z = specular intensity, w = unused
    // Sun shadow map — populated by BeginShadowPass and read by receiver shaders.
    Render::Float4x4 sunViewProjection;
    // x = shadow darkness (0 = no shadow, 1 = fully black), y = depth bias,
    // z = 1/shadowMapSize (texel size), w = 1 if shadow map ready else 0.
    Render::Float4 shadowParams;
    // x = debug visualization mode (0 = production PCF, 1 = always lit,
    //      2 = shadow map depth, 3 = footprint outline, 4 = receiver depth),
    // y = caster color-debug intensity (0 = off, 1 = fully replace lit color
    //      with per-caster hashed color — set by shadow pass only),
    // z, w = reserved.
    Render::Float4 shadowParams2;
    // Animated cloud-shadow noise. Layered on top of the sun shadow so a slow
    // procedural noise drifts across the map and darkens ground as if large
    // clouds were passing between sun and scene.
    //   x = intensity        (0 = off, 1 = strong darkening under cloud peaks)
    //   y = scale            (noise period in world units; larger = bigger clouds)
    //   z = speed            (drift rate; world units per second)
    //   w = coverage offset  (-0.5 .. 0.5, shifts mean density)
    Render::Float4 cloudParams;
    // Additional cloud knobs (split so cloudParams layout stays stable):
    //   x = wind angle in radians (direction clouds drift toward)
    //   y = sharpness (1 = smooth, >1 sharpens edges via pow())
    //   z,w = reserved
    Render::Float4 cloudParams2;
};

// Per-object constants
struct alignas(16) ObjectConstants
{
    Render::Float4x4 world;
    Render::Float4 color;
    // x = cosmetic shader variant id (cast from int via float). 0 = stock.
    // y = isPlayerDrawable (0/1) so the PS doesn't apply effects to terrain etc.
    // z, w = reserved.
    Render::Float4 shaderParams;
};

// Mesh decal projection constants (slot b2)
struct alignas(16) MeshDecalConstants
{
    Render::Float4x4 decalProjection; // world -> decal UV space
    Render::Float4 decalParams;       // x = backface threshold, y = opacity
};

// Construction-ghost shader constants (slot b5)
struct alignas(16) GhostConstants
{
    Render::Float4 ghostTint;   // rgb = tint color, a = tint intensity (0..1)
    Render::Float4 ghostParams; // x = opacity, yzw = unused
};

// Vertex format for 3D meshes
struct Vertex3D
{
    Render::Float3 position;
    Render::Float3 normal;
    Render::Float2 texcoord;
    uint32_t color; // RGBA packed
};

// Vertex format for terrain/custom-edge passes that need two UV sets.
struct Vertex3DMasked
{
    Render::Float3 position;
    Render::Float3 normal;
    Render::Float2 texcoord0;
    Render::Float2 texcoord1;
    uint32_t color; // RGBA packed
};

// Vertex format for 2D UI
struct Vertex2D
{
    Render::Float2 position;
    Render::Float2 texcoord;
    uint32_t color;
};

class Renderer
{
public:
    static Renderer& Instance();

    bool Init(void* nativeWindowHandle, bool debug = false);
    void Shutdown();

    void BeginFrame();
    void EndFrame();
    void Resize(int width, int height);

    // Flush all pending Render::Debug primitives onto the backbuffer.
    // Called from W3DDisplay::draw between the engine's post chain and
    // Inspector::Render so debug overlays sit on top of the rendered
    // game but BELOW ImGui panels (which would otherwise look broken
    // when a wireframe sliced through them). The flush re-binds the
    // FrameConstants cbuffer at b0 because intervening 2D draws may
    // have replaced it with ScreenConstants. Pipeline state used:
    // NoCull rasterizer, Alpha blend, Depth-disabled (so lines always
    // draw on top of geometry — debug overlays should never be hidden
    // behind terrain or buildings).
    void FlushDebugDraw();

    // --- Game viewport (inspector "render game inside an ImGui window") ---
    //
    // When enabled, the engine renders into an off-screen texture
    // (matching the current backbuffer dimensions). The inspector's
    // Game window samples that texture via ImGui::Image to display
    // the game inside a draggable, resizable, dockable ImGui window.
    // When disabled, the engine renders directly to the swap chain
    // backbuffer as before — zero overhead.
    void EnableGameViewport(bool enabled);
    bool IsGameViewportEnabled() const;
    int  GetGameViewportWidth()  const;
    int  GetGameViewportHeight() const;
#ifdef BUILD_WITH_D3D11
    ID3D11ShaderResourceView* GetGameViewportSRV() const;
#endif

    // Camera
    void SetCamera(const Render::Float4x4& view, const Render::Float4x4& projection, const Render::Float3& cameraPos);

    // Lighting
    void SetSunLight(const Render::Float3& direction, const Render::Float4& color);
    void SetDirectionalLights(const Render::Float3* directions, const Render::Float4* colors, uint32_t count);
    void SetAmbientLight(const Render::Float4& color);

    // When true, the engine's per-frame TOD reapply (TerrainRenderer /
    // ModelRenderer) and setTimeOfDay skip writing ambient/directional
    // lights — so live edits from the Lights inspector panel stick.
    bool LightsOverridden() const { return m_lightsOverridden; }
    void SetLightsOverridden(bool v) { m_lightsOverridden = v; }

    // Point lights (for explosions, etc.)
    // positions: xyz = world position, w = outer radius
    // colors: rgb = color * intensity, a = inner radius
    void SetPointLights(const Render::Float4* positions, const Render::Float4* colors, uint32_t count);
    void ClearPointLights();
    void SetWaterHeight(float height) { m_frameData.lightingOptions.w = height; }
    void SetTime(float timeMs) { m_frameData.lightingOptions.y = timeMs; }
    void SetShroudParams(float invWorldW, float invWorldH, float offsetX, float offsetY);
    void BindShroudTexture(const Texture* shroudTex);
    void PushFrameConstants() { m_savedFrameData = m_frameData; }
    void PopFrameConstants() { m_frameData = m_savedFrameData; FlushFrameConstants(); }

    // Post-processing chain — call Begin before a sequence of effects, End after.
    // Between Begin/End, effects read/write ping-pong RTs instead of copying the backbuffer each time.
    void BeginPostChain();
    void EndPostChain();

    void SetBloomEnabled(bool enabled) { m_bloomEnabled = enabled; }
    bool IsBloomEnabled() const { return m_bloomEnabled; }
    void ApplyPostProcessing();

    // Particle FX post-processing (heat distortion + glow around explosions)
    void SetParticleGlowEnabled(bool enabled) { m_particleGlowEnabled = enabled; }
    void SetHeatDistortionEnabled(bool enabled) { m_heatDistortEnabled = enabled; }
    bool IsParticleGlowEnabled() const { return m_particleGlowEnabled; }
    bool IsHeatDistortionEnabled() const { return m_heatDistortEnabled; }
    void CapturePreParticleScene();
    void ApplyParticleFX();

    // Laser beam 3D state: unlit glow profile shader with additive blend + depth test
    void SetLaserGlow3DState();

    // Reflection RT mesh state: regular lit shader, opaque blend, depth test +
    // write enabled, NO cull. Used to render scene meshes into the water
    // reflection RT — the reflected camera flips screen-space winding so
    // backface culling would hide every mesh; we accept the slight overdraw
    // cost of double-sided rendering inside the small (256x256) reflection RT.
    void SetReflectionMesh3DState();

    // Occluded-silhouette state: unlit shader, alpha blend, default cull,
    // depth test = GREATER, depth write = OFF. Used to draw a colored
    // silhouette of every unit only where it's behind another mesh — the
    // greater-than depth test passes only when there's something closer to
    // the camera, so the mesh appears only on the obscured pixels. Matches
    // the original DX8 game's stencil-driven "see units through walls" effect.
    void SetOccludedSilhouette3DState();

    // Enhanced visual effects
    void SetShockwaveEnabled(bool enabled) { m_shockwaveEnabled = enabled; }
    void SetGodRaysEnabled(bool enabled) { m_godRaysEnabled = enabled; }
    void SetCinematicEnabled(bool enabled) { m_cinematicEnabled = enabled; }
    void SetChromaEnabled(bool enabled) { m_chromaEnabled = enabled; }
    void SetVignetteEnabled(bool enabled) { m_vignetteEnabled = enabled; }
    void SetColorGradeEnabled(bool enabled) { m_colorGradeEnabled = enabled; }
    // Black & white filter (FT_VIEW_BW_FILTER from script). Hijacks the
    // cinematic shader's saturation knob — when enabled, ApplyCinematic
    // forces saturation to 0 so the entire scene desaturates to grayscale,
    // matching the original game's PASS_BLACKWHITE_FILTER post-process.
    void SetBwFilterEnabled(bool enabled) { m_bwFilterEnabled = enabled; }
    void SetColorAwareFxEnabled(bool enabled) { m_colorAwareFxEnabled = enabled; }
    void SetFilmGrainEnabled(bool enabled) { m_filmGrainEnabled = enabled; }
    void SetSharpenEnabled(bool enabled) { m_sharpenEnabled = enabled; }
    void SetTiltShiftEnabled(bool enabled) { m_tiltShiftEnabled = enabled; }

    void SetAtmosphereEnabled(bool enabled);
    void SetSurfaceSpecularEnabled(bool enabled);
    void SetLensFlareEnabled(bool enabled) { m_lensFlareEnabled = enabled; }
    void ApplyLensFlare();
    void SetVolumetricExplosionsEnabled(bool enabled) { m_volumetricEnabled = enabled; }
    void ApplyVolumetricExplosions();
    void ApplyShockwave();
    void ApplyExplosionFlash();
    void SetExplosionFlashEnabled(bool enabled) { m_explosionFlashEnabled = enabled; }
    void ApplyGodRays();
    void ApplyCinematic();
    void ApplyFilmGrain();
    void ApplySharpen();
    void ApplyTiltShift();

    // GPU instanced snow
    struct alignas(16) SnowConstants
    {
        Render::Float4 snowGrid;     // x=spacing, y=quadHalfSize, z=ceiling, w=heightTraveled
        Render::Float4 snowAnim;     // x=amplitude, y=freqScaleX, z=freqScaleY, w=boxDimensions
        Render::Int4   snowOrigin;   // x=cubeOriginX, y=cubeOriginY, z=gridWidth
        Render::Float4 snowCamRight; // xyz=camera right
        Render::Float4 snowCamUp;    // xyz=camera up
        Render::Float4 snowCamFwd;   // xyz=camera forward, w=cull distance
    };
    void DrawSnowInstanced(uint32_t instanceCount, const Texture* snowTex,
                           GPUBuffer& noiseBuffer, const SnowConstants& params);

    // GPU instanced terrain decals (shadows, faction logos, selection rings)
    struct DecalInstance
    {
        float posX, posY;
        float offsetX, offsetY;
        float sizeX, sizeY;
        float angle;
        uint32_t color; // ABGR packed
    };
    struct alignas(16) DecalConstants
    {
        Render::Float4 hmTransform; // xy = 1/(extent*mapFactor), zw = border/extent
        Render::Float4 hmParams;    // x = zBias, y = blend mode (0=mult,1=alpha,2=additive), zw = unused
    };
    enum class DecalBlend { Multiplicative, Alpha, Additive };
    void DrawDecalsInstanced(uint32_t instanceCount, const Texture* decalTex,
                             GPUBuffer& instanceBuffer, const Texture* heightmapTex,
                             const DecalConstants& params, DecalBlend blend);

    // 3D rendering
    void Draw3D(const VertexBuffer& vb, const IndexBuffer& ib, const Texture* texture, const Render::Float4x4& world, const Render::Float4& color = {1,1,1,1});
    void Draw3DIndexed(const VertexBuffer& vb, const IndexBuffer& ib, uint32_t indexCount, const Texture* texture, const Render::Float4x4& world, const Render::Float4& color = {1,1,1,1});
    void Draw3DNoIndex(const VertexBuffer& vb, uint32_t vertexCount, const Texture* texture, const Render::Float4x4& world, const Render::Float4& color = {1,1,1,1});
    void Draw3DMasked(const VertexBuffer& vb, const IndexBuffer& ib, const Texture* baseTexture, const Texture* maskTexture, const Render::Float4x4& world, const Render::Float4& color = {1,1,1,1});

    // 2D rendering (screen-space, pixel coords)
    void Begin2D();
    void DrawRect(float x, float y, float w, float h, uint32_t color);
    void DrawImage(const Texture& texture, float x, float y, float w, float h, uint32_t tint = 0xFFFFFFFF);
    void DrawImageUV(const Texture& texture, float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t tint = 0xFFFFFFFF);
    // Draws `texture` into the destination rect with the sampled texels rotated
    // 90° counter-clockwise on screen (matching a `-90°` image rotation as seen
    // by the user). Used by the post-defeat observer HUD where team logos are
    // exported with the wrong axis orientation.
    void DrawImageUVRotatedCCW90(const Texture& texture, float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t tint = 0xFFFFFFFF);
    void DrawLine(float x1, float y1, float x2, float y2, float width, uint32_t color);
    void DrawTri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color);
    void Set2DGrayscale(bool grayscale);
    void End2D();

    // Set the D3D11 viewport to a sub-region of the render target.
    // Used by 3D rendering to restrict the scene to the camera's viewport
    // (e.g. excluding the control bar area at the bottom of the screen).
    void SetViewport(float x, float y, float width, float height, float minDepth = 0.0f, float maxDepth = 1.0f);

    // Reset the D3D11 viewport to cover the full render target.
    // Called before 2D rendering to ensure UI draws across the entire screen.
    void ResetViewport();

    // Re-upload frame constants (camera, lighting) to the GPU.
    // Call after SetCamera/SetSunLight/SetAmbientLight if rendering 3D
    // between BeginFrame and Begin2D.
    void FlushFrameConstants();

    // Re-bind default 3D pipeline states (rasterizer, blend, depth, sampler).
    // Call before a new 3D rendering pass to ensure correct state.
    void Restore3DState();
    void SetAlphaBlend3DState();
    void SetAlphaTest3DState();
    // Particle-specific state functions. Always bind the *unlit* shader so
    // particles aren't darkened by ComputeLighting/ComputeShadow/ApplyShroud
    // /ApplyAtmosphere — the original DX8 build used the unlit sprite shader
    // presets for every particle blend mode.
    void SetParticleAlphaBlend3DState();
    void SetParticleMultiplicative3DState();
    void SetParticleAlphaTest3DState();
    void SetDecalAlphaBlend3DState();
    void SetDecalMultiplicative3DState();
    void SetDecalAdditive3DState();
    // Heat-distortion smudge state. Snapshots the current backbuffer into the
    // scene RT and binds it as the bumpTexture (slot 1) for the smudge shader,
    // which displaces UVs based on the smudge texture's RG channels and reads
    // from the snapshot. Sets a viewport-size constant buffer at slot 3.
    void SetSmudge3DState();
    void SetTerrainEdgeBase3DState();
    void SetTerrainEdgeArt3DState();
    void SetAdditive3DState();
    void SetMeshDecal3DState();
    // Construction-ghost state: lit ghost shader + alpha blend with depth
    // test ON / depth write OFF, so the ghost can layer over existing
    // geometry without depth-fighting it. Combine with UploadGhostConstants
    // before issuing draws.
    void SetGhost3DState();
    void UploadGhostConstants(const GhostConstants& gc);
    void DrawMeshDecal(const VertexBuffer& vb, const IndexBuffer& ib,
                       const Texture* decalTexture,
                       const MeshDecalConstants& decalConst,
                       const Render::Float4x4& world,
                       const Render::Float4& color = {1,1,1,1});
    void SetAdditiveAlpha3DState();
    void SetShroudOverlay3DState();
    void SetMultiplicative3DState();
    void SetSkybox3DState();
    void SetWaterBump3DState(const Texture* bumpTexture);
    void SetEnhancedWaterEnabled(bool enabled);
    void BeginWaterDepthRead();
    void EndWaterDepthRead();
    void SetDepthWriteEnabled(bool enabled);
    void SetDepthDisabled();
    void BindLinearClampSampler();
    void SetRenderTarget(Texture& rt, int width, int height);
    void RestoreBackBuffer();

    // --- Sun shadow map pass ---
    //
    // Bind the depth-only shadow RT, override FrameConstants.viewProjection
    // with the sun's orthographic VP (`sunVP`), and bind a depth-only shader.
    // Scene geometry can then be re-rendered with the usual Draw3D* calls to
    // populate the shadow depth texture. EndShadowPass restores the backbuffer,
    // viewport, and original frame constants. While the pass is active,
    // SetCamera() ignores incoming VP matrices so re-entrant terrain/model
    // renderers don't stomp sunVP mid-pass.
    void BeginShadowPass(const Render::Float4x4& sunVP);
    void EndShadowPass();
    bool IsShadowPassActive() const { return m_shadowPassActive; }
    // Must be bound to t4 + comparison sampler on s2 for receivers to sample.
    // Called automatically by Restore3DState / the main 3D state setters.
    void BindShadowMapAsSRV();
    // Shadow map square size in texels. Callers that trigger a viewport reset
    // inside the shadow pass (e.g. ModelRenderer::BeginFrame installs the
    // camera's backbuffer-sized viewport) re-assert the shadow viewport by
    // calling SetViewport(0, 0, size, size, 0, 1).
    uint32_t GetShadowMapSize() const { return kShadowMapSize; }

    // Live-tuning accessors for the Inspector's Shadows panel. Each returns
    // a reference into m_frameData so the panel can drive the shader uniforms
    // with a single slider/toggle. FlushFrameConstants propagates the edits
    // on the next 3D pass.
    Render::Float4& ShadowParams()  { return m_frameData.shadowParams; }
    Render::Float4& ShadowParams2() { return m_frameData.shadowParams2; }
    Render::Float4& CloudParams()   { return m_frameData.cloudParams; }
    Render::Float4& CloudParams2()  { return m_frameData.cloudParams2; }

#ifdef BUILD_WITH_D3D11
    // SRV of the shadow depth target, for displaying the shadow map as an
    // ImGui image in the Shadows panel. Null until the first frame after
    // Init runs.
    ID3D11ShaderResourceView* GetShadowMapSRV() const
    { return m_shadowMap.IsValid() ? m_shadowMap.GetSRV() : nullptr; }
#endif

    // FSR video upscaling: renders srcTexture through FSR shader to the backbuffer
    // at (dstX, dstY, dstW, dstH). Call between Begin2D/End2D.
    void DrawImageFSR(const Texture& srcTexture, float dstX, float dstY, float dstW, float dstH);
    bool IsFSRReady() const { return m_fsrReady; }

    Device& GetDevice() { return m_device; }

    // Invalidate the Draw3D per-frame ObjectConstants (VS/PS @ b1) rebind
    // cache. Call after any subsystem has touched VS b1 directly so the
    // next Draw3D will re-bind m_cbObject rather than trusting the cache.
    void InvalidateObjectCBCache() { m_objectCBBound = false; }

    int GetWidth() const { return m_device.GetWidth(); }
    int GetHeight() const { return m_device.GetHeight(); }

    bool CaptureScreenshot(const char* filename) { return m_device.CaptureScreenshot(filename); }
    const FrameConstants& GetFrameData() const { return m_frameData; }
    FrameConstants& GetFrameData() { return m_frameData; }

private:
    Renderer() = default;

    bool CreateShaders();
    bool CreateStates();
    bool Create2DResources();
    void Flush2DBatch();

    Device m_device;

    // Inspector override — see SetLightsOverridden.
    bool m_lightsOverridden = false;

    // Shaders
    Shader m_shader3D;
    Shader m_shader3DAlphaTest;
    Shader m_shader3DTerrainMask;
    Shader m_shader3DEdgeAlphaTest;
    Shader m_shader3DWaterBump;
    Shader m_shader3DSkybox;
    Shader m_shaderSnow;
    Shader m_shaderDecal;
    Shader m_shaderMeshDecal;
    Shader m_shaderGhost;       // construction-ghost (placement preview)
    Shader m_shader3DUnlit;    // unlit FX (streaks, additive lines)
    Shader m_shader3DSmudge;   // heat-distortion refraction (smudges)
    Shader m_shader3DLaserGlow; // laser beam glow profile
    Shader m_shader2D;
    Shader m_shader2DTextured;
    Shader m_shader2DGrayscale;
    Shader m_shaderDebug;       // Render::Debug — colored line/point overlay

    // Constant buffers
    ConstantBuffer m_cbFrame;
    ConstantBuffer m_cbObject;
    ConstantBuffer m_cbSnow;
    ConstantBuffer m_cbDecal;
    ConstantBuffer m_cbMeshDecal;
    ConstantBuffer m_cbGhost;
    ConstantBuffer m_cbSmudge;

    // Pipeline states
    RasterizerState m_rasterDefault;
    RasterizerState m_rasterNoCull;
    RasterizerState m_rasterDecalBias;
    RasterizerState m_rasterNoCullLaserBias;
    RasterizerState m_rasterWireframe;
    BlendState m_blendOpaque;
    BlendState m_blendAlpha;
    BlendState m_blendAdditive;
    BlendState m_blendMultiplicative;
    DepthStencilState m_depthDefault;
    DepthStencilState m_depthNoWrite;
    DepthStencilState m_depthDisabled;
    // Greater-than depth test with no write: used for the occluded-silhouette
    // pass where we want to draw mesh pixels ONLY where the depth buffer is
    // closer (something else is in front). No write so the obscured-pixel
    // overlay doesn't pollute the depth buffer for subsequent passes.
    DepthStencilState m_depthGreaterNoWrite;

    SamplerState m_samplerLinear;
    SamplerState m_samplerLinearClamp;
    SamplerState m_samplerPoint;
    SamplerState m_samplerShadow; // comparison sampler for shadow map PCF

    // --- Sun shadow map ---
    Texture m_shadowMap;            // D32_FLOAT depth target (SRV-capable)
    Shader m_shaderShadowMesh;      // depth-only VS/PS for Vertex3D geometry
    Shader m_shaderShadowTerrain;   // depth-only VS/PS for Vertex3DMasked terrain
    DepthStencilState m_depthShadowWrite; // Depth enable + write, LessEqual
    BlendState m_blendNoColorWrite;       // Color writes off (depth only)
    bool m_shadowPassActive = false;
    Render::Float4x4 m_savedViewProjection = {};
    static constexpr uint32_t kShadowMapSize = 2048;

    // Render::Debug dynamic vertex buffer (colored line list)
    VertexBuffer m_vbDebug;

    // Off-screen render target for the inspector's "Game" window.
    // Created lazily on the first EnableGameViewport(true) call so
    // we don't pay the memory cost when the inspector is never used.
    Texture m_gameViewportRT;
    bool    m_gameViewportEnabled = false;

    // 2D batch rendering
    VertexBuffer m_vb2D;
    static const uint32_t MAX_2D_VERTICES = 8192;
    Vertex2D m_2DVertices[MAX_2D_VERTICES];
    uint32_t m_2DVertexCount = 0;
    const Texture* m_current2DTexture = nullptr;
    bool m_in2DMode = false;
    bool m_grayscale2D = false;

    // Draw call state caching - avoid redundant D3D11 API calls
    bool m_objectCBBound = false;
    const Texture* m_lastBoundTexture = nullptr;

public:
    /// <summary>
    /// Set the cosmetic shader effect variant id for the next drawcalls.
    /// Caller (typically ModelRenderer per render object) is responsible
    /// for resetting it back to 0 after the player-owned drawable is done
    /// so terrain/props/particles don't pick up the effect.
    /// </summary>
    void SetCurrentShaderEffect(int shaderId, bool isPlayerDrawable)
    {
        m_currentShaderId = shaderId;
        m_currentIsPlayerDrawable = isPlayerDrawable;
    }
    /// <summary>
    /// Marks whether the next Draw3D is an accent mesh (HOUSECOLOR variant
    /// in the W3D model). The cosmetic shader effect is gated on this so
    /// only the team-colored accent trim animates — not the entire building
    /// or the whole unit body. Reset to false between meshes.
    /// </summary>
    void SetCurrentIsAccentMesh(bool isAccent) { m_currentIsAccentMesh = isAccent; }
    /// <summary>
    /// Marks whether the next Draw3D uses a ZHCA_ "house icon" texture
    /// (Command Center emblem, HQ faction logo, etc.). PSMain / PSMainAlphaTest
    /// branch on this to key out the dark background and draw the foreground
    /// in the player's house color. Reset to false between batches.
    /// </summary>
    void SetCurrentIsZhcaTexture(bool isZhca) { m_currentIsZhcaTexture = isZhca; }
private:
    int  m_currentShaderId         = 0;
    bool m_currentIsPlayerDrawable = false;
    bool m_currentIsAccentMesh     = false;
    bool m_currentIsZhcaTexture    = false;

    // Frame data
    FrameConstants m_frameData;
    FrameConstants m_savedFrameData;
    Render::Float4x4 m_lastValidVP = {};
    Render::Float4 m_lastValidCameraPos = {0, 0, 0, 1};

    // FSR video upscaling (two-pass: EASU + RCAS)
    Shader m_shaderEASU;
    Shader m_shaderRCAS;
    ConstantBuffer m_cbFSR;
    Texture m_fsrRT;         // intermediate RT at display resolution
    int m_fsrRTWidth = 0;
    int m_fsrRTHeight = 0;
    bool m_fsrReady = false;

    // Ping-pong render targets for post-processing chain (avoids CopyResource per pass)
    Texture m_pingRT;
    Texture m_pongRT;
    int m_ppReadIdx = 0; // 0 = read from ping, write to pong; 1 = opposite
    int m_ppWidth = 0, m_ppHeight = 0;
    bool m_inPostChain = false;
    bool m_postChainDirty = false; // true if any effect wrote to the chain
    void EnsurePingPongRTs();
    void CopyBackbufferToPing();
    void CopyPingToBackbuffer();
    Texture& GetPPReadRT() { return m_ppReadIdx == 0 ? m_pingRT : m_pongRT; }
    Texture& GetPPWriteRT() { return m_ppReadIdx == 0 ? m_pongRT : m_pingRT; }
    void SwapPP() { m_ppReadIdx = 1 - m_ppReadIdx; }
    void EnsureSceneRT();
    void CopyBackbufferToSceneRT();
    // Ensures the chain has input data; called lazily by the first effect in the chain
    void EnsurePostChainInput();

    // Post-processing (bloom)
    bool m_bloomEnabled = false;
    bool m_bloomReady = false;
    Shader m_shaderBloomExtract;
    Shader m_shaderBlur;
    Shader m_shaderComposite;
    ConstantBuffer m_cbPost;
    Texture m_sceneRT;        // full-res scene render target
    Texture m_bloomExtractRT; // half-res bright pixels
    Texture m_bloomBlurRT;    // half-res blurred bloom
    VertexBuffer m_fullscreenQuadVB;
    int m_bloomWidth = 0;
    int m_bloomHeight = 0;

    struct alignas(16) PostConstants
    {
        Render::Float2 texelSize;
        float bloomThreshold;
        float bloomIntensity;
    };

    // Particle FX post-processing
    bool m_particleGlowEnabled = false;
    bool m_heatDistortEnabled = false;
    bool m_particleFxReady = false;
    Shader m_shaderParticleExtract;
    Shader m_shaderHeatDistort;
    Shader m_shaderGlowComposite;
    ConstantBuffer m_cbParticleFX;
    Texture m_preParticleRT;       // full-res: scene snapshot before particles
    Texture m_particleExtractRT;   // half-res: extracted particle contribution
    Texture m_particleBlurRT;      // half-res: blurred particle glow temp
    int m_particleFxWidth = 0;
    int m_particleFxHeight = 0;

    struct alignas(16) ParticleFXConstants
    {
        Render::Float2 texelSize;
        float distortionStrength;
        float glowIntensity;
        float time;
        float colorAwareFx; // 1.0 = enabled, 0.0 = disabled (hue-based toxin/fire)
        float pad[2];
    };

    // Enhanced effects: shockwave, god rays, cinematic
    bool m_shockwaveEnabled = false;
    bool m_explosionFlashEnabled = false;
    float m_flashIntensity = 0.0f;
    bool m_godRaysEnabled = false;
    bool m_cinematicEnabled = false;
    bool m_colorAwareFxEnabled = false;
    bool m_atmosphereEnabled = false;
    bool m_surfaceSpecEnabled = false;
    bool m_lensFlareEnabled = false;
    bool m_volumetricEnabled = false;
    Texture m_volHalfRT;        // half-res RT for volumetric raymarching
    int m_volHalfW = 0, m_volHalfH = 0;

    // Persistent explosion cloud tracking — clouds linger after point lights disappear
    static constexpr uint32_t kMaxVolClouds = 128;        // tracked on CPU
    static constexpr uint32_t kMaxRenderedClouds = 32;   // sent to GPU per frame
    static constexpr uint32_t kMaxRenderedGroundFog = 8; // ground AOE sent to GPU per frame
    struct VolCloudInfo
    {
        float worldX, worldY, worldZ;
        float radius;
        float r, g, b;
        float density;    // 1.0 = fresh, fades to 0
        float age;        // seconds since spawn
        bool active;
    };
    VolCloudInfo m_volClouds[kMaxVolClouds] = {};
    void UpdateVolClouds();

    // Particle system death tracking — spawn fade-out clouds when particles vanish
    static constexpr uint32_t kMaxTrackedParticleSys = 64;
    struct TrackedParticleSys
    {
        uintptr_t id;
        float x, y, z;
        float r, g, b;
        float size;
        bool seenThisFrame;
        bool wasAlive;
    };
    TrackedParticleSys m_trackedPS[kMaxTrackedParticleSys] = {};

public:
    void TrackParticleSystem(uintptr_t id, float x, float y, float z, float r, float g, float b, float size);
    void FinishParticleTracking(); // call after all TrackParticleSystem calls — spawns clouds for dead systems

    // Ground AOE volumetric fog (toxin, radiation, napalm)
    static constexpr uint32_t kMaxGroundFog = 8;
    struct GroundFogInfo
    {
        float worldX, worldY, worldZ;
        float radius;
        float r, g, b;
        float density;
        bool active;
    };
    GroundFogInfo m_groundFog[kMaxGroundFog] = {};
    bool m_modernAOEEnabled = false;

public:
    void SetModernAOEEnabled(bool enabled) { m_modernAOEEnabled = enabled; }
    void SetGroundFog(int index, float x, float y, float z, float radius, float r, float g, float b, float density);
    void ClearGroundFog();

    Shader m_shaderLensFlare;
    Shader m_shaderVolumetric;
    ConstantBuffer m_cbLensFlare;
    ConstantBuffer m_cbVolumetric;
    bool m_chromaEnabled = false;
    bool m_vignetteEnabled = false;
    bool m_colorGradeEnabled = false;
    bool m_filmGrainEnabled = false;
    bool m_sharpenEnabled = false;
    bool m_tiltShiftEnabled = false;
    bool m_bwFilterEnabled = false;

    Shader m_shaderShockwave;
    Shader m_shaderGodRayExtract;
    Shader m_shaderGodRayBlur;
    Shader m_shaderGodRayComposite;
    Shader m_shaderCinematic;

    ConstantBuffer m_cbShockwave;
    ConstantBuffer m_cbGodRays;
    ConstantBuffer m_cbCinematic;
    ConstantBuffer m_cbFilmGrain;
    ConstantBuffer m_cbSharpen;
    ConstantBuffer m_cbTiltShift;

    Shader m_shaderFilmGrain;
    Shader m_shaderSharpen;
    Shader m_shaderTiltShift;

    Texture m_godRayExtractRT;  // half-res bright extraction
    Texture m_godRayBlurRT;     // half-res blurred rays

    // Shockwave tracking: detect new explosions from point lights
    static constexpr uint32_t kMaxShockwaves = 8;
    struct ShockwaveInfo
    {
        float screenX, screenY;
        float phase;
        float intensity;
        float worldX, worldY;
        bool active;
    };
    ShockwaveInfo m_shockwaves[kMaxShockwaves] = {};
    void UpdateShockwaves();

    struct alignas(16) ShockwaveConstants
    {
        Render::Float4 shockwaves[kMaxShockwaves]; // xy=screenUV, z=phase, w=intensity
        Render::Float2 texelSize;
        float time;
        float shockwaveCount;
    };

    struct alignas(16) GodRayConstants
    {
        Render::Float2 sunScreenUV;
        float density;
        float decay;
        float weight;
        float exposure;
        float threshold;
        float numSamples;
        Render::Float2 texelSize;
        Render::Float2 pad;
    };

    struct alignas(16) CinematicConstants
    {
        Render::Float2 texelSize;
        float chromaAmount;
        float vignetteStrength;
        float colorGradeIntensity;
        float saturation;
        float contrast;
        float pad;
    };

    struct alignas(16) VolumetricConstants
    {
        Render::Float4x4 invViewProjection;
        Render::Float4 camPos;
        Render::Float4 clouds[32];
        Render::Float4 cloudColors[32];
        Render::Float4 gfClouds[8];
        Render::Float4 gfColors[8];
        Render::Float4 sunDirection;
        Render::Float4 sunColor;
        Render::Float2 texelSize;
        float time;
        float numClouds;
        float numGroundFog;
        float padV[3];
    };

    struct alignas(16) LensFlareConstants
    {
        Render::Float2 sunScreenUV;
        float sunOnScreen;  // 1.0 = visible, 0.0 = off-screen
        float intensity;
        Render::Float4 sunColor;
        Render::Float2 texelSize;
        Render::Float2 pad;
    };

    struct alignas(16) FilmGrainConstants
    {
        Render::Float2 texelSize;
        float grainIntensity;
        float time;
    };

    struct alignas(16) SharpenConstants
    {
        Render::Float2 texelSize;
        float sharpenAmount;
        float pad;
    };

    struct alignas(16) TiltShiftConstants
    {
        Render::Float2 texelSize;
        float focusCenter;
        float focusWidth;
        float blurStrength;
        float pad[3];
    };

    struct alignas(16) FSRConstants
    {
        Render::Float2 texelSize;
        float sharpness;
        float pad;
    };
};

} // namespace Render
