#include "Renderer.h"
#include "Shaders/ShaderSource.h"
#include "Core/ShaderCompiler.h"
#include "DebugDraw.h"
#ifdef USE_SDL
#include "SDLPlatform.h"
#endif
#include <cstring>
#include <algorithm>

// Helper: compile shader from HLSL source (D3D11 runtime), or load
// precompiled SPIR-V bytecode (Vulkan). Returns true if either succeeds.
static bool CompileOrLoadVS(Render::Shader& shader, Render::Device& device,
                            const char* hlslSource, const char* entryPoint,
                            const char* spirvPath)
{
    // Try runtime compilation first (works on D3D11)
    if (shader.CompileVS(device, hlslSource, entryPoint))
        return true;

    // Fall back to precompiled SPIR-V (for Vulkan)
    if (spirvPath)
    {
        auto bytecode = Render::LoadShaderBytecode(spirvPath);
        if (!bytecode.empty())
        {
            return shader.LoadVS(device, bytecode.data(), bytecode.size());
        }
    }
    return false;
}

static bool CompileOrLoadPS(Render::Shader& shader, Render::Device& device,
                            const char* hlslSource, const char* entryPoint,
                            const char* spirvPath)
{
    if (shader.CompilePS(device, hlslSource, entryPoint))
        return true;

    if (spirvPath)
    {
        auto bytecode = Render::LoadShaderBytecode(spirvPath);
        if (!bytecode.empty())
        {
            return shader.LoadPS(device, bytecode.data(), bytecode.size());
        }
    }
    return false;
}

// Game uses D3DCOLOR format: 0xAARRGGBB (ARGB)
// D3D11 R8G8B8A8_UNORM expects bytes: R,G,B,A in memory
// On little-endian: 0xAARRGGBB stored as BB,GG,RR,AA
// D3D11 reads: R=BB, G=GG, B=RR, A=AA (swapped R and B)
// Fix: swap R and B to convert ARGB -> ABGR
static inline uint32_t argbToAbgr(uint32_t argb)
{
    uint32_t a = (argb >> 24) & 0xFF;
    uint32_t r = (argb >> 16) & 0xFF;
    uint32_t g = (argb >> 8) & 0xFF;
    uint32_t b = argb & 0xFF;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

namespace Render
{

Renderer& Renderer::Instance()
{
    static Renderer s_instance;
    return s_instance;
}

bool Renderer::Init(void* nativeWindowHandle, bool debug)
{
    DeviceConfig config;
    config.nativeWindowHandle = nativeWindowHandle;
    // Vsync OFF — uncap render fps. The classic engine clocks the
    // simulation off its own logic-frame rate (TheGameLogic ticks at
    // a fixed step), so unlocking Present only affects how often the
    // GPU redraws between sim ticks. We pay nothing in correctness
    // for it but get smoother camera motion / mouse on high-Hz panels.
    config.vsync = false;
    config.debug = debug;

#ifdef USE_SDL
    // Pass window dimensions for backends that don't query the window directly
    config.width = Platform::SDLPlatform::Instance().GetWidth();
    config.height = Platform::SDLPlatform::Instance().GetHeight();
#endif

    if (!m_device.Init(config))
        return false;

    if (!CreateShaders())
        return false;

    if (!CreateStates())
        return false;

    if (!Create2DResources())
        return false;

    // Create constant buffers
    m_cbFrame.Create(m_device, sizeof(FrameConstants));
    m_cbObject.Create(m_device, sizeof(ObjectConstants));
    m_cbGhost.Create(m_device, sizeof(GhostConstants));

    // Set default frame data
    m_frameData.viewProjection = Float4x4Identity();
    m_frameData.cameraPos = { 0, 0, 0, 1 };
    m_frameData.ambientColor = { 0.3f, 0.3f, 0.3f, 1 };
    for (uint32_t i = 0; i < kMaxDirectionalLights; ++i)
    {
        m_frameData.lightDirections[i] = { 0.0f, -1.0f, 0.0f, 0.0f };
        m_frameData.lightColors[i] = { 0.0f, 0.0f, 0.0f, 0.0f };
    }
    m_frameData.lightColors[0] = { 1.0f, 1.0f, 1.0f, 1.0f };
    m_frameData.lightingOptions = { 1.0f, 0.0f, 0.0f, 0.0f };
    m_frameData.shroudParams = { 0.0f, 0.0f, 0.0f, 0.0f }; // disabled until game sets it
    m_frameData.atmosphereParams = { 0.0f, 0.0f, 0.0f, 0.0f }; // atmosphere off by default

    return true;
}

void Renderer::Shutdown()
{
#ifdef BUILD_WITH_VULKAN
    // Destroy all renderer-owned GPU resources before shutting down the device.
    // D3D11 uses ComPtrs which auto-release, but Vulkan needs explicit cleanup.

    // Shaders
    m_shader3D.Destroy(m_device);
    m_shader3DAlphaTest.Destroy(m_device);
    m_shader3DTerrainMask.Destroy(m_device);
    m_shader3DEdgeAlphaTest.Destroy(m_device);
    m_shader3DWaterBump.Destroy(m_device);
    m_shader3DSkybox.Destroy(m_device);
    m_shaderSnow.Destroy(m_device);
    m_shaderDecal.Destroy(m_device);
    m_shaderMeshDecal.Destroy(m_device);
    m_shaderGhost.Destroy(m_device);
    m_shader3DUnlit.Destroy(m_device);
    m_shader3DSmudge.Destroy(m_device);
    m_shader3DLaserGlow.Destroy(m_device);
    m_shader2D.Destroy(m_device);
    m_shader2DTextured.Destroy(m_device);
    m_shader2DGrayscale.Destroy(m_device);
    m_shaderEASU.Destroy(m_device);
    m_shaderRCAS.Destroy(m_device);
    m_shaderBloomExtract.Destroy(m_device);
    m_shaderBlur.Destroy(m_device);
    m_shaderComposite.Destroy(m_device);
    m_shaderParticleExtract.Destroy(m_device);
    m_shaderHeatDistort.Destroy(m_device);
    m_shaderGlowComposite.Destroy(m_device);
    m_shaderLensFlare.Destroy(m_device);
    m_shaderVolumetric.Destroy(m_device);
    m_shaderShockwave.Destroy(m_device);
    m_shaderGodRayExtract.Destroy(m_device);
    m_shaderGodRayBlur.Destroy(m_device);
    m_shaderGodRayComposite.Destroy(m_device);
    m_shaderCinematic.Destroy(m_device);
    m_shaderFilmGrain.Destroy(m_device);
    m_shaderSharpen.Destroy(m_device);
    m_shaderTiltShift.Destroy(m_device);

    // Constant buffers
    m_cbFrame.Destroy(m_device);
    m_cbObject.Destroy(m_device);
    m_cbSnow.Destroy(m_device);
    m_cbDecal.Destroy(m_device);
    m_cbMeshDecal.Destroy(m_device);
    m_cbGhost.Destroy(m_device);
    m_cbFSR.Destroy(m_device);
    m_cbPost.Destroy(m_device);
    m_cbParticleFX.Destroy(m_device);
    m_cbLensFlare.Destroy(m_device);
    m_cbVolumetric.Destroy(m_device);
    m_cbShockwave.Destroy(m_device);
    m_cbGodRays.Destroy(m_device);
    m_cbCinematic.Destroy(m_device);
    m_cbFilmGrain.Destroy(m_device);
    m_cbSharpen.Destroy(m_device);
    m_cbTiltShift.Destroy(m_device);

    // Vertex buffers
    m_vb2D.Destroy(m_device);
    m_fullscreenQuadVB.Destroy(m_device);

    // Textures (render targets and intermediate buffers)
    m_fsrRT.Destroy(m_device);
    m_pingRT.Destroy(m_device);
    m_pongRT.Destroy(m_device);
    m_sceneRT.Destroy(m_device);
    m_bloomExtractRT.Destroy(m_device);
    m_bloomBlurRT.Destroy(m_device);
    m_preParticleRT.Destroy(m_device);
    m_particleExtractRT.Destroy(m_device);
    m_particleBlurRT.Destroy(m_device);
    m_volHalfRT.Destroy(m_device);
    m_godRayExtractRT.Destroy(m_device);
    m_godRayBlurRT.Destroy(m_device);

    // Samplers
    m_samplerLinear.Destroy(m_device);
    m_samplerLinearClamp.Destroy(m_device);
    m_samplerPoint.Destroy(m_device);
#endif

    m_device.Shutdown();
}

bool Renderer::CreateShaders()
{
    // 3D shader — compile from HLSL (D3D11) or load SPIR-V (Vulkan)
    if (!CompileOrLoadVS(m_shader3D, m_device, g_shader3D, "VSMain", "Shaders/spirv/Shader3D_VSMain.spv"))
        return false;
    if (!CompileOrLoadPS(m_shader3D, m_device, g_shader3D, "PSMain", "Shaders/spirv/Shader3D_PSMain.spv"))
        return false;

    VertexAttribute layout3D[] = {
        { "POSITION", 0, VertexFormat::Float3,    offsetof(Vertex3D, position) },
        { "NORMAL",   0, VertexFormat::Float3,    offsetof(Vertex3D, normal) },
        { "TEXCOORD", 0, VertexFormat::Float2,    offsetof(Vertex3D, texcoord) },
        { "COLOR",    0, VertexFormat::UByte4Norm, offsetof(Vertex3D, color) },
    };
    if (!m_shader3D.CreateInputLayout(m_device, layout3D, _countof(layout3D), sizeof(Vertex3D)))
        return false;

    // 3D unlit shader for additive FX (lasers, streaks, line effects)
    if (!CompileOrLoadVS(m_shader3DUnlit, m_device, g_shader3D, "VSMain", "Shaders/spirv/Shader3D_VSMain.spv"))
        return false;
    if (!CompileOrLoadPS(m_shader3DUnlit, m_device, g_shader3D, "PSMainUnlit", "Shaders/spirv/Shader3D_PSMainUnlit.spv"))
        return false;
    if (!m_shader3DUnlit.CreateInputLayout(m_device, layout3D, _countof(layout3D), sizeof(Vertex3D)))
        return false;

    // 3D laser glow shader (computes glow profile from UV)
    if (!CompileOrLoadVS(m_shader3DLaserGlow, m_device, g_shader3D, "VSMain", "Shaders/spirv/Shader3D_VSMain.spv"))
        return false;
    if (!CompileOrLoadPS(m_shader3DLaserGlow, m_device, g_shader3D, "PSLaserGlow", "Shaders/spirv/Shader3D_PSLaserGlow.spv"))
        return false;
    if (!m_shader3DLaserGlow.CreateInputLayout(m_device, layout3D, _countof(layout3D), sizeof(Vertex3D)))
        return false;

    // 3D smudge shader (heat-haze refraction). VS is the same as the regular
    // 3D path; PS samples the back-buffer copy (bumpTexture/scene RT) at
    // displaced UVs derived from the smudge texture. SmudgeConstants cbuffer
    // at slot b3 carries the viewport size for SV_POSITION → UV conversion.
    if (!CompileOrLoadVS(m_shader3DSmudge, m_device, g_shader3D, "VSMain", "Shaders/spirv/Shader3D_VSMain.spv"))
        return false;
    if (!CompileOrLoadPS(m_shader3DSmudge, m_device, g_shader3D, "PSMainSmudge", "Shaders/spirv/Shader3D_PSMainSmudge.spv"))
        return false;
    if (!m_shader3DSmudge.CreateInputLayout(m_device, layout3D, _countof(layout3D), sizeof(Vertex3D)))
        return false;

    // 3D alpha-test shader (same VS, different PS with clip())
    if (!CompileOrLoadVS(m_shader3DAlphaTest, m_device, g_shader3D, "VSMain", "Shaders/spirv/Shader3D_VSMain.spv"))
        return false;
    if (!CompileOrLoadPS(m_shader3DAlphaTest, m_device, g_shader3D, "PSMainAlphaTest", "Shaders/spirv/Shader3D_PSMainAlphaTest.spv"))
        return false;
    if (!m_shader3DAlphaTest.CreateInputLayout(m_device, layout3D, _countof(layout3D), sizeof(Vertex3D)))
        return false;

    // GPU projected mesh decal shader (faction logos on buildings)
    if (!CompileOrLoadVS(m_shaderMeshDecal, m_device, g_shader3D, "VSMain", "Shaders/spirv/Shader3D_VSMain.spv"))
        return false;
    if (!CompileOrLoadPS(m_shaderMeshDecal, m_device, g_shader3D, "PSMeshDecal", "Shaders/spirv/Shader3D_PSMeshDecal.spv"))
        return false;
    if (!m_shaderMeshDecal.CreateInputLayout(m_device, layout3D, _countof(layout3D), sizeof(Vertex3D)))
        return false;

    // Construction-ghost shader (placement preview translucent material)
    if (!CompileOrLoadVS(m_shaderGhost, m_device, g_shader3D, "VSMain", "Shaders/spirv/Shader3D_VSMain.spv"))
        return false;
    if (!CompileOrLoadPS(m_shaderGhost, m_device, g_shader3D, "PSGhost", "Shaders/spirv/Shader3D_PSGhost.spv"))
        return false;
    if (!m_shaderGhost.CreateInputLayout(m_device, layout3D, _countof(layout3D), sizeof(Vertex3D)))
        return false;

    // 3D custom-edge alpha-test shader (rejects 0x80 mask pixels, keeps 0xFF artwork)
    if (!CompileOrLoadVS(m_shader3DEdgeAlphaTest, m_device, g_shader3D, "VSMain", "Shaders/spirv/Shader3D_VSMain.spv"))
        return false;
    if (!CompileOrLoadPS(m_shader3DEdgeAlphaTest, m_device, g_shader3D, "PSMainAlphaTestEdge", "Shaders/spirv/Shader3D_PSMainAlphaTestEdge.spv"))
        return false;
    if (!m_shader3DEdgeAlphaTest.CreateInputLayout(m_device, layout3D, _countof(layout3D), sizeof(Vertex3D)))
        return false;

    VertexAttribute layout3DMasked[] = {
        { "POSITION", 0, VertexFormat::Float3,    offsetof(Vertex3DMasked, position) },
        { "NORMAL",   0, VertexFormat::Float3,    offsetof(Vertex3DMasked, normal) },
        { "TEXCOORD", 0, VertexFormat::Float2,    offsetof(Vertex3DMasked, texcoord0) },
        { "TEXCOORD", 1, VertexFormat::Float2,    offsetof(Vertex3DMasked, texcoord1) },
        { "COLOR",    0, VertexFormat::UByte4Norm, offsetof(Vertex3DMasked, color) },
    };
    if (!CompileOrLoadVS(m_shader3DTerrainMask, m_device, g_shader3D, "VSMainTwoTex", "Shaders/spirv/Shader3D_VSMainTwoTex.spv"))
        return false;
    if (!CompileOrLoadPS(m_shader3DTerrainMask, m_device, g_shader3D, "PSMainTerrainMaskBase", "Shaders/spirv/Shader3D_PSMainTerrainMaskBase.spv"))
        return false;
    if (!m_shader3DTerrainMask.CreateInputLayout(m_device, layout3DMasked, _countof(layout3DMasked), sizeof(Vertex3DMasked)))
        return false;

    // 3D water bump shader (water VS with GPU UV animation, water-specific PS with bump texture)
    if (!CompileOrLoadVS(m_shader3DWaterBump, m_device, g_shader3D, "VSMainWater", "Shaders/spirv/Shader3D_VSMainWater.spv"))
        return false;
    if (!CompileOrLoadPS(m_shader3DWaterBump, m_device, g_shader3D, "PSMainWaterBump", "Shaders/spirv/Shader3D_PSMainWaterBump.spv"))
        return false;
    if (!m_shader3DWaterBump.CreateInputLayout(m_device, layout3D, _countof(layout3D), sizeof(Vertex3D)))
        return false;

    // 3D skybox shader (forces depth = 1.0).
    if (!CompileOrLoadVS(m_shader3DSkybox, m_device, g_shader3D, "VSMainSkybox", "Shaders/spirv/Shader3D_VSMainSkybox.spv"))
        return false;
    if (!CompileOrLoadPS(m_shader3DSkybox, m_device, g_shader3D, "PSMainSkybox", "Shaders/spirv/Shader3D_PSMainSkybox.spv"))
        return false;
    if (!m_shader3DSkybox.CreateInputLayout(m_device, layout3D, _countof(layout3D), sizeof(Vertex3D)))
        return false;

    // 2D color shader
    if (!CompileOrLoadVS(m_shader2D, m_device, g_shader2D, "VSMain", "Shaders/spirv/Shader2D_VSMain.spv"))
        return false;
    if (!CompileOrLoadPS(m_shader2D, m_device, g_shader2D, "PSMainColor", "Shaders/spirv/Shader2D_PSMainColor.spv"))
        return false;

    VertexAttribute layout2D[] = {
        { "POSITION", 0, VertexFormat::Float2,    offsetof(Vertex2D, position) },
        { "TEXCOORD", 0, VertexFormat::Float2,    offsetof(Vertex2D, texcoord) },
        { "COLOR",    0, VertexFormat::UByte4Norm, offsetof(Vertex2D, color) },
    };
    if (!m_shader2D.CreateInputLayout(m_device, layout2D, _countof(layout2D), sizeof(Vertex2D)))
        return false;

    // 2D textured shader (shares VS with 2D color shader)
    if (!CompileOrLoadVS(m_shader2DTextured, m_device, g_shader2D, "VSMain", "Shaders/spirv/Shader2D_VSMain.spv"))
        return false;
    if (!CompileOrLoadPS(m_shader2DTextured, m_device, g_shader2D, "PSMainTextured", "Shaders/spirv/Shader2D_PSMainTextured.spv"))
        return false;
    if (!m_shader2DTextured.CreateInputLayout(m_device, layout2D, _countof(layout2D), sizeof(Vertex2D)))
        return false;

    // 2D grayscale shader (for disabled/grayed-out UI buttons)
    if (!CompileOrLoadVS(m_shader2DGrayscale, m_device, g_shader2D, "VSMain", "Shaders/spirv/Shader2D_VSMain.spv"))
        return false;
    if (!CompileOrLoadPS(m_shader2DGrayscale, m_device, g_shader2D, "PSMainGrayscale", "Shaders/spirv/Shader2D_PSMainGrayscale.spv"))
        return false;
    if (!m_shader2DGrayscale.CreateInputLayout(m_device, layout2D, _countof(layout2D), sizeof(Vertex2D)))
        return false;

    // GPU instanced snow — optional, non-fatal
    {
        bool snowOk = true;
        snowOk = snowOk && CompileOrLoadVS(m_shaderSnow, m_device, g_shaderSnow, "VSSnow", "Shaders/spirv/ShaderSnow_VSSnow.spv");
        snowOk = snowOk && CompileOrLoadPS(m_shaderSnow, m_device, g_shaderSnow, "PSSnow", "Shaders/spirv/ShaderSnow_PSSnow.spv");
        // No input layout needed — vertices come from SV_VertexID/SV_InstanceID
        if (snowOk)
            m_cbSnow.Create(m_device, sizeof(SnowConstants));
    }

    // GPU instanced decals — optional, non-fatal
    {
        bool decalOk = true;
        decalOk = decalOk && CompileOrLoadVS(m_shaderDecal, m_device, g_shaderDecal, "VSDecal", "Shaders/spirv/ShaderDecal_VSDecal.spv");
        decalOk = decalOk && CompileOrLoadPS(m_shaderDecal, m_device, g_shaderDecal, "PSDecal", "Shaders/spirv/ShaderDecal_PSDecal.spv");
        if (decalOk)
            m_cbDecal.Create(m_device, sizeof(DecalConstants));
    }

    // Mesh decal constant buffer
    m_cbMeshDecal.Create(m_device, sizeof(MeshDecalConstants));

    // Fullscreen quad VB — shared by post-processing (bloom) and FSR
    {
        struct PostVertex { float x, y, u, v; };
        PostVertex fsQuad[4] = {
            { -1.0f,  1.0f, 0.0f, 0.0f },
            {  1.0f,  1.0f, 1.0f, 0.0f },
            { -1.0f, -1.0f, 0.0f, 1.0f },
            {  1.0f, -1.0f, 1.0f, 1.0f },
        };
        m_fullscreenQuadVB.Create(m_device, fsQuad, 4, sizeof(PostVertex));
    }

    // FSR video upscaling shaders (EASU + RCAS) — optional, non-fatal
    {
        VertexAttribute layoutPost[] = {
            { "POSITION", 0, VertexFormat::Float2, 0 },
            { "TEXCOORD", 0, VertexFormat::Float2, 8 },
        };
        bool fsrOk = true;
        fsrOk = fsrOk && CompileOrLoadVS(m_shaderEASU, m_device, g_shaderFSR, "VSPost", "Shaders/spirv/ShaderFSR_VSPost.spv");
        fsrOk = fsrOk && CompileOrLoadPS(m_shaderEASU, m_device, g_shaderFSR, "PSEASU", "Shaders/spirv/ShaderFSR_PSEASU.spv");
        fsrOk = fsrOk && m_shaderEASU.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);
        fsrOk = fsrOk && CompileOrLoadVS(m_shaderRCAS, m_device, g_shaderFSR, "VSPost", "Shaders/spirv/ShaderFSR_VSPost.spv");
        fsrOk = fsrOk && CompileOrLoadPS(m_shaderRCAS, m_device, g_shaderFSR, "PSRCAS", "Shaders/spirv/ShaderFSR_PSRCAS.spv");
        fsrOk = fsrOk && m_shaderRCAS.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);
        if (fsrOk)
        {
            m_cbFSR.Create(m_device, sizeof(FSRConstants));
            m_fsrReady = true;
        }
    }

    // Post-processing shaders (bloom) — optional, don't fail init if they don't compile
    {
        VertexAttribute layoutPost[] = {
            { "POSITION", 0, VertexFormat::Float2, 0 },
            { "TEXCOORD", 0, VertexFormat::Float2, 8 },
        };
        bool bloomOk = true;
        bloomOk = bloomOk && CompileOrLoadVS(m_shaderBloomExtract, m_device, g_shaderPost, "VSPost", "Shaders/spirv/ShaderPost_VSPost.spv");
        bloomOk = bloomOk && CompileOrLoadPS(m_shaderBloomExtract, m_device, g_shaderPost, "PSBloomExtract", "Shaders/spirv/ShaderPost_PSBloomExtract.spv");
        bloomOk = bloomOk && m_shaderBloomExtract.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);
        bloomOk = bloomOk && CompileOrLoadVS(m_shaderBlur, m_device, g_shaderPost, "VSPost", "Shaders/spirv/ShaderPost_VSPost.spv");
        bloomOk = bloomOk && CompileOrLoadPS(m_shaderBlur, m_device, g_shaderPost, "PSBlur", "Shaders/spirv/ShaderPost_PSBlur.spv");
        bloomOk = bloomOk && m_shaderBlur.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);
        bloomOk = bloomOk && CompileOrLoadVS(m_shaderComposite, m_device, g_shaderPost, "VSPost", "Shaders/spirv/ShaderPost_VSPost.spv");
        bloomOk = bloomOk && CompileOrLoadPS(m_shaderComposite, m_device, g_shaderPost, "PSComposite", "Shaders/spirv/ShaderPost_PSComposite.spv");
        bloomOk = bloomOk && m_shaderComposite.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);

        if (bloomOk)
        {
            m_cbPost.Create(m_device, sizeof(PostConstants));
        }
        else
        {
            m_bloomEnabled = false; // gracefully disable bloom if shaders fail
        }
    }

    // Particle FX post-processing shaders — optional, non-fatal
    {
        VertexAttribute layoutPost[] = {
            { "POSITION", 0, VertexFormat::Float2, 0 },
            { "TEXCOORD", 0, VertexFormat::Float2, 8 },
        };
        bool pfxOk = true;
        pfxOk = pfxOk && CompileOrLoadVS(m_shaderParticleExtract, m_device, g_shaderParticleFX, "VSPost", "Shaders/spirv/ShaderParticleFX_VSPost.spv");
        pfxOk = pfxOk && CompileOrLoadPS(m_shaderParticleExtract, m_device, g_shaderParticleFX, "PSParticleExtract", "Shaders/spirv/ShaderParticleFX_PSParticleExtract.spv");
        pfxOk = pfxOk && m_shaderParticleExtract.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);
        pfxOk = pfxOk && CompileOrLoadVS(m_shaderHeatDistort, m_device, g_shaderParticleFX, "VSPost", "Shaders/spirv/ShaderParticleFX_VSPost.spv");
        pfxOk = pfxOk && CompileOrLoadPS(m_shaderHeatDistort, m_device, g_shaderParticleFX, "PSHeatDistort", "Shaders/spirv/ShaderParticleFX_PSHeatDistort.spv");
        pfxOk = pfxOk && m_shaderHeatDistort.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);
        pfxOk = pfxOk && CompileOrLoadVS(m_shaderGlowComposite, m_device, g_shaderParticleFX, "VSPost", "Shaders/spirv/ShaderParticleFX_VSPost.spv");
        pfxOk = pfxOk && CompileOrLoadPS(m_shaderGlowComposite, m_device, g_shaderParticleFX, "PSGlowComposite", "Shaders/spirv/ShaderParticleFX_PSGlowComposite.spv");
        pfxOk = pfxOk && m_shaderGlowComposite.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);

        if (pfxOk)
        {
            m_cbParticleFX.Create(m_device, sizeof(ParticleFXConstants));
        }
        else
        {
            // Disable particle FX features if shaders failed to compile
            m_particleGlowEnabled = false;
            m_heatDistortEnabled = false;
        }
    }

    // Shockwave shader — optional, non-fatal
    {
        VertexAttribute layoutPost[] = {
            { "POSITION", 0, VertexFormat::Float2, 0 },
            { "TEXCOORD", 0, VertexFormat::Float2, 8 },
        };
        bool ok = true;
        ok = ok && CompileOrLoadVS(m_shaderShockwave, m_device, g_shaderShockwave, "VSPost", "Shaders/spirv/ShaderShockwave_VSPost.spv");
        ok = ok && CompileOrLoadPS(m_shaderShockwave, m_device, g_shaderShockwave, "PSShockwave", "Shaders/spirv/ShaderShockwave_PSShockwave.spv");
        ok = ok && m_shaderShockwave.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);
        if (ok)
            m_cbShockwave.Create(m_device, sizeof(ShockwaveConstants));
    }

    // God ray shaders — optional, non-fatal
    {
        VertexAttribute layoutPost[] = {
            { "POSITION", 0, VertexFormat::Float2, 0 },
            { "TEXCOORD", 0, VertexFormat::Float2, 8 },
        };
        bool ok = true;
        ok = ok && CompileOrLoadVS(m_shaderGodRayExtract, m_device, g_shaderGodRays, "VSPost", "Shaders/spirv/ShaderGodRays_VSPost.spv");
        ok = ok && CompileOrLoadPS(m_shaderGodRayExtract, m_device, g_shaderGodRays, "PSGodRayExtract", "Shaders/spirv/ShaderGodRays_PSGodRayExtract.spv");
        ok = ok && m_shaderGodRayExtract.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);
        ok = ok && CompileOrLoadVS(m_shaderGodRayBlur, m_device, g_shaderGodRays, "VSPost", "Shaders/spirv/ShaderGodRays_VSPost.spv");
        ok = ok && CompileOrLoadPS(m_shaderGodRayBlur, m_device, g_shaderGodRays, "PSGodRayBlur", "Shaders/spirv/ShaderGodRays_PSGodRayBlur.spv");
        ok = ok && m_shaderGodRayBlur.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);
        ok = ok && CompileOrLoadVS(m_shaderGodRayComposite, m_device, g_shaderGodRays, "VSPost", "Shaders/spirv/ShaderGodRays_VSPost.spv");
        ok = ok && CompileOrLoadPS(m_shaderGodRayComposite, m_device, g_shaderGodRays, "PSGodRayComposite", "Shaders/spirv/ShaderGodRays_PSGodRayComposite.spv");
        ok = ok && m_shaderGodRayComposite.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);
        if (ok)
            m_cbGodRays.Create(m_device, sizeof(GodRayConstants));
    }

    // Cinematic shader — optional, non-fatal
    {
        VertexAttribute layoutPost[] = {
            { "POSITION", 0, VertexFormat::Float2, 0 },
            { "TEXCOORD", 0, VertexFormat::Float2, 8 },
        };
        bool ok = true;
        ok = ok && CompileOrLoadVS(m_shaderCinematic, m_device, g_shaderCinematic, "VSPost", "Shaders/spirv/ShaderCinematic_VSPost.spv");
        ok = ok && CompileOrLoadPS(m_shaderCinematic, m_device, g_shaderCinematic, "PSCinematic", "Shaders/spirv/ShaderCinematic_PSCinematic.spv");
        ok = ok && m_shaderCinematic.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);
        if (ok)
            m_cbCinematic.Create(m_device, sizeof(CinematicConstants));
    }

    // Film grain shader — optional, non-fatal
    {
        VertexAttribute layoutPost[] = {
            { "POSITION", 0, VertexFormat::Float2, 0 },
            { "TEXCOORD", 0, VertexFormat::Float2, 8 },
        };
        bool ok = true;
        ok = ok && CompileOrLoadVS(m_shaderFilmGrain, m_device, g_shaderFilmGrain, "VSPost", "Shaders/spirv/ShaderFilmGrain_VSPost.spv");
        ok = ok && CompileOrLoadPS(m_shaderFilmGrain, m_device, g_shaderFilmGrain, "PSFilmGrain", "Shaders/spirv/ShaderFilmGrain_PSFilmGrain.spv");
        ok = ok && m_shaderFilmGrain.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);
        if (ok)
            m_cbFilmGrain.Create(m_device, sizeof(FilmGrainConstants));
    }

    // Sharpen shader — optional, non-fatal
    {
        VertexAttribute layoutPost[] = {
            { "POSITION", 0, VertexFormat::Float2, 0 },
            { "TEXCOORD", 0, VertexFormat::Float2, 8 },
        };
        bool ok = true;
        ok = ok && CompileOrLoadVS(m_shaderSharpen, m_device, g_shaderSharpen, "VSPost", "Shaders/spirv/ShaderSharpen_VSPost.spv");
        ok = ok && CompileOrLoadPS(m_shaderSharpen, m_device, g_shaderSharpen, "PSSharpen", "Shaders/spirv/ShaderSharpen_PSSharpen.spv");
        ok = ok && m_shaderSharpen.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);
        if (ok)
            m_cbSharpen.Create(m_device, sizeof(SharpenConstants));
    }

    // Tilt shift shader — optional, non-fatal
    {
        VertexAttribute layoutPost[] = {
            { "POSITION", 0, VertexFormat::Float2, 0 },
            { "TEXCOORD", 0, VertexFormat::Float2, 8 },
        };
        bool ok = true;
        ok = ok && CompileOrLoadVS(m_shaderTiltShift, m_device, g_shaderTiltShift, "VSPost", "Shaders/spirv/ShaderTiltShift_VSPost.spv");
        ok = ok && CompileOrLoadPS(m_shaderTiltShift, m_device, g_shaderTiltShift, "PSTiltShift", "Shaders/spirv/ShaderTiltShift_PSTiltShift.spv");
        ok = ok && m_shaderTiltShift.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);
        if (ok)
            m_cbTiltShift.Create(m_device, sizeof(TiltShiftConstants));
    }

    // Lens flare shader — optional, non-fatal
    {
        VertexAttribute layoutPost[] = {
            { "POSITION", 0, VertexFormat::Float2, 0 },
            { "TEXCOORD", 0, VertexFormat::Float2, 8 },
        };
        bool ok = true;
        ok = ok && CompileOrLoadVS(m_shaderLensFlare, m_device, g_shaderLensFlare, "VSPost", "Shaders/spirv/ShaderLensFlare_VSPost.spv");
        ok = ok && CompileOrLoadPS(m_shaderLensFlare, m_device, g_shaderLensFlare, "PSLensFlare", "Shaders/spirv/ShaderLensFlare_PSLensFlare.spv");
        ok = ok && m_shaderLensFlare.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);
        if (ok)
            m_cbLensFlare.Create(m_device, sizeof(LensFlareConstants));
    }

    // Volumetric explosion shader — optional, non-fatal
    {
        VertexAttribute layoutPost[] = {
            { "POSITION", 0, VertexFormat::Float2, 0 },
            { "TEXCOORD", 0, VertexFormat::Float2, 8 },
        };
        bool ok = true;
        ok = ok && CompileOrLoadVS(m_shaderVolumetric, m_device, g_shaderVolumetric, "VSPost", "Shaders/spirv/ShaderVolumetric_VSPost.spv");
        ok = ok && CompileOrLoadPS(m_shaderVolumetric, m_device, g_shaderVolumetric, "PSVolumetric", "Shaders/spirv/ShaderVolumetric_PSVolumetric.spv");
        ok = ok && m_shaderVolumetric.CreateInputLayout(m_device, layoutPost, _countof(layoutPost), sizeof(float) * 4);
        if (ok)
            m_cbVolumetric.Create(m_device, sizeof(VolumetricConstants));
    }

    // Render::Debug colored line shader. Reuses FrameConstants slot
    // b0 (only samples viewProjection), takes a tiny vertex format
    // of position+color so the dynamic VB stays small. Failure here
    // is non-fatal — debug overlays just won't render but the rest
    // of the engine keeps working.
    {
        struct DebugVertex { Render::Float3 position; uint32_t color; };
        VertexAttribute layoutDebug[] = {
            { "POSITION", 0, VertexFormat::Float3,    offsetof(DebugVertex, position) },
            { "COLOR",    0, VertexFormat::UByte4Norm, offsetof(DebugVertex, color) },
        };
        bool dbgOk = true;
        dbgOk = dbgOk && CompileOrLoadVS(m_shaderDebug, m_device, g_shaderDebug, "VSMain", "Shaders/spirv/ShaderDebug_VSMain.spv");
        dbgOk = dbgOk && CompileOrLoadPS(m_shaderDebug, m_device, g_shaderDebug, "PSMain", "Shaders/spirv/ShaderDebug_PSMain.spv");
        dbgOk = dbgOk && m_shaderDebug.CreateInputLayout(m_device, layoutDebug, _countof(layoutDebug), sizeof(DebugVertex));
        if (dbgOk)
        {
            Render::Debug::Init(m_device);
        }
    }

    return true;
}

bool Renderer::CreateStates()
{
    // Rasterizer states - use CCW front face for D3D8 winding compatibility
    m_rasterDefault.Create(m_device, FillMode::Solid, CullMode::Back, true);
    m_rasterNoCull.Create(m_device, FillMode::Solid, CullMode::None, true);
    m_rasterDecalBias.Create(m_device, FillMode::Solid, CullMode::Back, true, -100);
    m_rasterNoCullLaserBias.Create(m_device, FillMode::Solid, CullMode::None, true, -5000);
    m_rasterWireframe.Create(m_device, FillMode::Wireframe, CullMode::None, true);

    // Blend states
    m_blendOpaque.CreateOpaque(m_device);
    m_blendAlpha.CreateAlphaBlend(m_device);
    m_blendAdditive.CreateAdditive(m_device);
    m_blendMultiplicative.CreateMultiplicative(m_device);

    // Depth stencil states
    m_depthDefault.Create(m_device, true, true, CompareFunc::LessEqual);
    m_depthNoWrite.Create(m_device, true, false, CompareFunc::LessEqual);
    m_depthDisabled.Create(m_device, false, false);
    m_depthGreaterNoWrite.Create(m_device, true, false, CompareFunc::Greater);

    // Samplers
    m_samplerLinear.Create(m_device, Filter::MinMagMipLinear, AddressMode::Wrap);
    m_samplerLinearClamp.Create(m_device, Filter::MinMagMipLinear, AddressMode::Clamp);
    m_samplerPoint.Create(m_device, Filter::MinMagMipPoint, AddressMode::Clamp);

    return true;
}

bool Renderer::Create2DResources()
{
    if (!m_vb2D.Create(m_device, nullptr, MAX_2D_VERTICES, sizeof(Vertex2D), true))
        return false;

    // Render::Debug dynamic vertex buffer. Sized for kMaxLineVertices
    // (~64K vertices = 32K lines per frame) which is comfortably more
    // than any sensible debug overlay needs. Stride matches the
    // DebugVertex layout (position Float3 + uint32 color = 16 bytes).
    constexpr uint32_t kDebugVertexStride = 12 + 4;
    if (!m_vbDebug.Create(m_device, nullptr,
            Render::Debug::kMaxLineVertices, kDebugVertexStride, true))
        return false;

    return true;
}

void Renderer::BeginFrame()
{
    // Clear back buffer to black. (Earlier diagnostic code used magenta to
    // verify the clear path was working — it is.)
    m_device.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);

    // Reset viewport to full screen at the start of each frame
    ResetViewport();

    // Bind default states
    m_rasterDefault.Bind(m_device);
    m_blendOpaque.Bind(m_device);
    m_depthDefault.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);

    // Upload frame constants
    m_cbFrame.Update(m_device, &m_frameData, sizeof(m_frameData));
    m_cbFrame.BindVS(m_device, 0);
    m_cbFrame.BindPS(m_device, 0);

    // Reset draw call state caching
    m_objectCBBound = false;
    m_lastBoundTexture = nullptr;

#ifdef BUILD_WITH_VULKAN
    // Rewind dynamic-buffer ring cursors. The Vulkan renderer allocates
    // dynamic VBs (e.g. the 2D UI batch) 16x oversized so Update/Bind/Draw
    // cycles within one frame write to fresh offsets — avoids GPU reading
    // stale data when the CPU overwrites offset 0 between draws. Safe to
    // snap back to 0 here because Device::BeginFrame's in-flight fence
    // already guaranteed the previous frame's GPU reads completed.
    m_vb2D.ResetRing();
#endif
}

void Renderer::EndFrame()
{
#ifdef BUILD_WITH_VULKAN
    m_device.SetBackBuffer();
#endif
    m_device.EndFrame();
}

// --- Game viewport (off-screen render target for inspector Game window) ----
void Renderer::EnableGameViewport(bool enabled)
{
#ifdef BUILD_WITH_D3D11
    if (enabled)
    {
        // Lazy create on first enable. Sized to match the current
        // backbuffer; if the OS window resizes later we keep the
        // original size and let ImGui::Image scale the displayed
        // texture (cheaper than reallocating the RT every resize).
        if (!m_gameViewportRT.IsValid())
        {
            const uint32_t w = (uint32_t)m_device.GetWidth();
            const uint32_t h = (uint32_t)m_device.GetHeight();
            if (w == 0 || h == 0)
                return; // device not ready yet
            if (!m_gameViewportRT.CreateRenderTarget(m_device, w, h))
                return;
        }
        m_device.SetRedirectRTV(m_gameViewportRT.GetRTV());
        m_gameViewportEnabled = true;
    }
    else
    {
        m_device.SetRedirectRTV(nullptr);
        m_gameViewportEnabled = false;
    }
#else
    (void)enabled;
#endif
}

bool Renderer::IsGameViewportEnabled() const
{
    return m_gameViewportEnabled;
}

int Renderer::GetGameViewportWidth() const
{
    return (int)m_gameViewportRT.GetWidth();
}

int Renderer::GetGameViewportHeight() const
{
    return (int)m_gameViewportRT.GetHeight();
}

#ifdef BUILD_WITH_D3D11
ID3D11ShaderResourceView* Renderer::GetGameViewportSRV() const
{
    return m_gameViewportRT.IsValid() ? m_gameViewportRT.GetSRV() : nullptr;
}
#endif

void Renderer::FlushDebugDraw()
{
    if (Render::Debug::QueuedLineCount() == 0)
    {
        Render::Debug::Clear();
        return;
    }

    // Re-bind backbuffer in case a post-process pass left an off-screen
    // target bound. The depth buffer comes along with it.
    m_device.SetBackBuffer();
    ResetViewport();

    // CRITICAL: by the time we get here the engine's 2D batch has run
    // and cbuffer slot b0 is bound to ScreenConstants (which the 2D
    // shader uses). Our debug shader samples viewProjection from
    // FrameConstants at b0, so we MUST re-upload and re-bind the
    // frame constants here. Without this, every debug vertex projects
    // through screen-pixel-size data instead of the camera matrix and
    // ends up off-screen — invisible.
    FlushFrameConstants();

    // Depth-disabled so debug lines ALWAYS render on top of all
    // engine geometry — terrain, buildings, units. Matches Unity /
    // Unreal "Gizmos always visible" behavior. Alpha blend so the
    // colors composite cleanly over bright HDR scenes.
    Render::Debug::Flush(m_device, m_shaderDebug, m_vbDebug,
                         m_rasterNoCull, m_blendAlpha, m_depthDisabled);

    // Clear the queue so next frame starts empty. Inspector / game
    // code re-submits primitives every frame (Unity Debug.DrawLine
    // semantics).
    Render::Debug::Clear();
}

void Renderer::Resize(int width, int height)
{
    m_device.Resize(width, height);
}

void Renderer::SetCamera(const Render::Float4x4& view, const Render::Float4x4& projection, const Render::Float3& cameraPos)
{
    Float4x4 vp = Float4x4Multiply(view, projection);

    // Validate the viewProjection matrix — detect degenerate/NaN matrices
    // that project everything off-screen (causes dark frames).
    Float4x4 vpTest = vp;
    bool valid = true;
    float* vals = &vpTest._11;
    for (int i = 0; i < 16; ++i)
    {
        if (isnan(vals[i]) || isinf(vals[i]))
        {
            valid = false;
            break;
        }
    }
    // Also check if the matrix is all zeros (degenerate)
    if (valid)
    {
        float sum = 0;
        for (int i = 0; i < 16; ++i) sum += fabsf(vals[i]);
        if (sum < 0.0001f) valid = false;
    }

    if (valid)
    {
        m_lastValidVP = vpTest; // save for fallback
        m_frameData.viewProjection = vp;
        m_frameData.cameraPos = { cameraPos.x, cameraPos.y, cameraPos.z, 1.0f };
        m_lastValidCameraPos = m_frameData.cameraPos;
    }
    else
    {
        // Use last known good matrix to prevent dark frame.
        // NaN should be caught at source by SanitizeMatrix3D; this is a safety net.
        m_frameData.viewProjection = m_lastValidVP;
        m_frameData.cameraPos = m_lastValidCameraPos;
    }
}

void Renderer::SetSunLight(const Render::Float3& direction, const Render::Float4& color)
{
    SetDirectionalLights(&direction, &color, 1);
}

// Fixed "2pm sun" direction: elevation ~60° (sin60 = 0.866), azimuth 225°
// (SW of zenith). +Z is up in the Generals coordinate system; lightDirections[0]
// stores the direction light TRAVELS (from sun to scene), so this vector points
// down-and-northeast. Unit length.
static const Render::Float3 kFixedSunDirection2PM = { 0.354f, 0.354f, -0.866f };

// Global: override the map's lighting[0] direction with kFixedSunDirection2PM so
// the per-pixel lighting comes from a known-good fixed angle regardless of map
// INI. Colors/ambient still come from the map so the time-of-day mood is preserved.
bool g_fixedSunAt2PM = true;

void Renderer::SetDirectionalLights(const Render::Float3* directions, const Render::Float4* colors, uint32_t count)
{
    const uint32_t clampedCount = count < kMaxDirectionalLights ? count : kMaxDirectionalLights;
    for (uint32_t i = 0; i < kMaxDirectionalLights; ++i)
    {
        if (i < clampedCount && directions && colors)
        {
            m_frameData.lightDirections[i] = { directions[i].x, directions[i].y, directions[i].z, 0.0f };
            m_frameData.lightColors[i] = colors[i];
        }
        else
        {
            m_frameData.lightDirections[i] = { 0.0f, -1.0f, 0.0f, 0.0f };
            m_frameData.lightColors[i] = { 0.0f, 0.0f, 0.0f, 0.0f };
        }
    }

    // Force the primary sun direction to the fixed 2pm angle so per-pixel
    // lighting lines up across all maps, even ones whose INI sets a degenerate
    // lightPos (zero or poorly normalized). Color intensity from the map is
    // retained so maps still feel bright/dim as authored.
    if (g_fixedSunAt2PM && clampedCount > 0)
    {
        m_frameData.lightDirections[0] = {
            kFixedSunDirection2PM.x,
            kFixedSunDirection2PM.y,
            kFixedSunDirection2PM.z,
            0.0f
        };
    }

    m_frameData.lightingOptions.x = static_cast<float>(clampedCount);
}

void Renderer::SetAmbientLight(const Render::Float4& color)
{
    m_frameData.ambientColor = color;
}

void Renderer::SetPointLights(const Render::Float4* positions, const Render::Float4* colors, uint32_t count)
{
    const uint32_t clampedCount = count < kMaxPointLights ? count : kMaxPointLights;
    for (uint32_t i = 0; i < kMaxPointLights; ++i)
    {
        if (i < clampedCount && positions && colors)
        {
            m_frameData.pointLightPositions[i] = positions[i];
            m_frameData.pointLightColors[i] = colors[i];
        }
        else
        {
            m_frameData.pointLightPositions[i] = { 0.0f, 0.0f, 0.0f, 0.0f };
            m_frameData.pointLightColors[i] = { 0.0f, 0.0f, 0.0f, 0.0f };
        }
    }
    m_frameData.lightingOptions.z = static_cast<float>(clampedCount);
}

void Renderer::ClearPointLights()
{
    for (uint32_t i = 0; i < kMaxPointLights; ++i)
    {
        m_frameData.pointLightPositions[i] = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_frameData.pointLightColors[i] = { 0.0f, 0.0f, 0.0f, 0.0f };
    }
    m_frameData.lightingOptions.z = 0.0f;
}

void Renderer::SetViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
{
    m_device.SetViewport(x, y, width, height, minDepth, maxDepth);
}

void Renderer::ResetViewport()
{
    SetViewport(0.0f, 0.0f, (float)m_device.GetWidth(), (float)m_device.GetHeight(), 0.0f, 1.0f);
}

void Renderer::FlushFrameConstants()
{
    // Recompute viewProjection from current m_frameData and re-upload
    m_cbFrame.Update(m_device, &m_frameData, sizeof(m_frameData));
    m_cbFrame.BindVS(m_device, 0);
    m_cbFrame.BindPS(m_device, 0);
    // Always bind WRAP sampler at s1 for tiling textures.
    // s0 may be CLAMP (for terrain atlas) or WRAP depending on the pass.
    m_samplerLinear.BindPS(m_device, 1);
}

void Renderer::Restore3DState()
{
    m_shader3D.Bind(m_device);
    m_rasterDefault.Bind(m_device);
    m_blendOpaque.Bind(m_device);
    m_depthDefault.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

void Renderer::SetAlphaBlend3DState()
{
    m_shader3D.Bind(m_device);
    m_rasterDefault.Bind(m_device);
    m_blendAlpha.Bind(m_device);
    m_depthNoWrite.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

void Renderer::SetAdditive3DState()
{
    m_shader3DUnlit.Bind(m_device);
    m_rasterNoCull.Bind(m_device);
    m_blendAdditive.Bind(m_device);
    m_depthNoWrite.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

void Renderer::SetAlphaTest3DState()
{
    m_shader3DAlphaTest.Bind(m_device);
    m_rasterDefault.Bind(m_device);
    m_blendOpaque.Bind(m_device);
    m_depthDefault.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

void Renderer::SetReflectionMesh3DState()
{
    // Same shader and blend as Restore3DState (lit, opaque, depth test+write),
    // but with NoCull so the scene meshes survive the reflected camera's
    // flipped screen-space winding. Used by TerrainRenderer::RenderWater for
    // the water reflection RT pass.
    m_shader3D.Bind(m_device);
    m_rasterNoCull.Bind(m_device);
    m_blendOpaque.Bind(m_device);
    m_depthDefault.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

void Renderer::SetOccludedSilhouette3DState()
{
    // Unlit shader so the per-mesh batch texture * vertex color * objectColor
    // collapses to a clean colored fill (no lighting darkening). Default cull
    // (we want the silhouette of the visible faces, not back faces). Alpha
    // blend so the silhouette can be tinted with reduced opacity. Depth test
    // GREATER + no write means the mesh draws ONLY where the depth buffer is
    // closer to the camera (i.e. something else is in front of it) — exactly
    // the "occluded" pixels. No depth write so we don't pollute the depth
    // buffer for subsequent passes.
    m_shader3DUnlit.Bind(m_device);
    m_rasterDefault.Bind(m_device);
    m_blendAlpha.Bind(m_device);
    m_depthGreaterNoWrite.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

// --- Particle blend states ---
// All three bind m_shader3DUnlit (PSMainUnlit: just texture * vertex color),
// matching the original DX8 _PresetAlphaSpriteShader / _PresetMultiplicativeSpriteShader
// / _PresetATestSpriteShader presets which were unlit. Using the lit shader
// here would darken every smoke plume / dust cloud / scorch sprite via
// ComputeLighting + shroud + atmosphere fog.
void Renderer::SetParticleAlphaBlend3DState()
{
    m_shader3DUnlit.Bind(m_device);
    m_rasterNoCull.Bind(m_device);
    m_blendAlpha.Bind(m_device);
    m_depthNoWrite.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

void Renderer::SetParticleMultiplicative3DState()
{
    m_shader3DUnlit.Bind(m_device);
    m_rasterNoCull.Bind(m_device);
    m_blendMultiplicative.Bind(m_device);
    m_depthNoWrite.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

void Renderer::SetDecalAlphaBlend3DState()
{
    m_shader3DUnlit.Bind(m_device);
    m_rasterNoCull.Bind(m_device);
    m_blendAlpha.Bind(m_device);
    m_depthNoWrite.Bind(m_device);
    m_samplerLinearClamp.BindPS(m_device, 0);
}

void Renderer::SetDecalMultiplicative3DState()
{
    m_shader3DUnlit.Bind(m_device);
    m_rasterNoCull.Bind(m_device);
    m_blendMultiplicative.Bind(m_device);
    m_depthNoWrite.Bind(m_device);
    m_samplerLinearClamp.BindPS(m_device, 0);
}

void Renderer::SetDecalAdditive3DState()
{
    m_shader3DUnlit.Bind(m_device);
    m_rasterNoCull.Bind(m_device);
    m_blendAdditive.Bind(m_device);
    m_depthNoWrite.Bind(m_device);
    m_samplerLinearClamp.BindPS(m_device, 0);
}

void Renderer::SetParticleAlphaTest3DState()
{
    // Alpha test particles: opaque blend, depth write enabled (the cutout
    // hides the sprite where alpha < threshold so depth fragments are valid).
    // Use unlit shader; PSMainUnlit doesn't clip(), so we use the dedicated
    // alpha-test pixel shader is overkill — instead clip in pixel shader is
    // unnecessary if we rely on the GPU's alpha-to-coverage; for simplicity
    // approximate with alpha-blend + depth write off, like the original
    // _PresetATestSpriteShader which used REF=96 alpha test.
    m_shader3DUnlit.Bind(m_device);
    m_rasterNoCull.Bind(m_device);
    m_blendAlpha.Bind(m_device);
    m_depthNoWrite.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

void Renderer::SetSmudge3DState()
{
    // Snapshot the current backbuffer into the scene RT (this is what the
    // smudge shader will sample to produce the heat-haze refraction effect).
    int sceneW = m_device.GetWidth();
    int sceneH = m_device.GetHeight();
    if (sceneW <= 0 || sceneH <= 0)
        return;
    if (!m_sceneRT.IsValid() ||
        m_sceneRT.GetWidth() != (uint32_t)sceneW ||
        m_sceneRT.GetHeight() != (uint32_t)sceneH)
    {
        m_sceneRT.CreateRenderTarget(m_device, sceneW, sceneH);
    }
    CopyBackbufferToSceneRT();

    // Bind the smudge shader and pipeline state. Sample state allows linear
    // filtering of the offset scene-RT lookup so the displacement is smooth.
    m_shader3DSmudge.Bind(m_device);
    m_rasterNoCull.Bind(m_device);
    m_blendAlpha.Bind(m_device);
    m_depthNoWrite.Bind(m_device);
    m_samplerLinearClamp.BindPS(m_device, 0);

    // Bind the scene RT as the bumpTexture (slot 1) — the shader uses
    // diffuseTexture (slot 0, smudge texture set by Draw3D) for the
    // displacement and bumpTexture (slot 1, scene RT) for the source pixels.
    m_sceneRT.BindPS(m_device, 1);

    // Update + bind viewport-size constants (slot b3) so the shader can
    // convert SV_POSITION pixel coords back to UV [0,1].
    struct SmudgeCB
    {
        Render::Float4 viewportSize;
    } cb;
    cb.viewportSize = { (float)sceneW, (float)sceneH,
                        1.0f / (float)sceneW, 1.0f / (float)sceneH };
    // Lazy-init smudge constant buffer on first use. ConstantBuffer doesn't
    // expose IsValid() so we use a static one-shot guard. Multiple calls
    // re-Create — that's safe because Create is idempotent on D3D11.
    static bool s_smudgeCBInit = false;
    if (!s_smudgeCBInit)
    {
        m_cbSmudge.Create(m_device, sizeof(SmudgeCB));
        s_smudgeCBInit = true;
    }
    m_cbSmudge.Update(m_device, &cb, sizeof(SmudgeCB));
    m_cbSmudge.BindPS(m_device, 3);
}

void Renderer::SetMeshDecal3DState()
{
    m_shaderMeshDecal.Bind(m_device);
    m_rasterDecalBias.Bind(m_device);
    m_blendAlpha.Bind(m_device);
    m_depthNoWrite.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

void Renderer::SetGhost3DState()
{
    // Construction-ghost preview: alpha-blended over the world with depth
    // test DISABLED. The ghost is GUI feedback — it must always render on
    // top of whatever's underneath so the red IllegalBuild tint is visible
    // even when the preview overlaps an existing building at the same Z
    // (LessEqual Z-fights, flipping half the ghost fragments off and
    // exposing the existing building's black faction-logo decals instead
    // of the red ghost). Depth-write stays off so subsequent draws see the
    // underlying structure's depth, not the ghost's.
    m_shaderGhost.Bind(m_device);
    m_rasterDefault.Bind(m_device);
    m_blendAlpha.Bind(m_device);
    m_depthDisabled.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

void Renderer::UploadGhostConstants(const GhostConstants& gc)
{
    m_cbGhost.Update(m_device, &gc, sizeof(gc));
    m_cbGhost.BindVS(m_device, 5);
    m_cbGhost.BindPS(m_device, 5);
}

void Renderer::DrawMeshDecal(const VertexBuffer& vb, const IndexBuffer& ib,
                              const Texture* decalTexture,
                              const MeshDecalConstants& decalConst,
                              const Render::Float4x4& world,
                              const Render::Float4& color)
{
    ObjectConstants obj;
    obj.world = world;
    obj.color = color;
    obj.shaderParams = { (float)m_currentShaderId, m_currentIsPlayerDrawable ? 1.0f : 0.0f, m_currentIsAccentMesh ? 1.0f : 0.0f, m_currentIsZhcaTexture ? 1.0f : 0.0f };
    m_cbObject.Update(m_device, &obj, sizeof(obj));
    m_cbObject.BindVS(m_device, 1);
    m_cbObject.BindPS(m_device, 1);

    m_cbMeshDecal.Update(m_device, &decalConst, sizeof(decalConst));
    m_cbMeshDecal.BindPS(m_device, 2);

    if (decalTexture && decalTexture->IsValid())
        decalTexture->BindPS(m_device, 0);

    vb.Bind(m_device);
    ib.Bind(m_device);
    m_device.SetTopology(Topology::TriangleList);
    m_device.DrawIndexed(ib.GetIndexCount(), 0, 0);
}

void Renderer::SetTerrainEdgeBase3DState()
{
    m_shader3DTerrainMask.Bind(m_device);
    m_rasterDefault.Bind(m_device);
    m_blendOpaque.Bind(m_device);
    m_depthDefault.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

void Renderer::SetTerrainEdgeArt3DState()
{
    m_shader3DEdgeAlphaTest.Bind(m_device);
    m_rasterDefault.Bind(m_device);
    m_blendOpaque.Bind(m_device);
    m_depthDefault.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

void Renderer::SetAdditiveAlpha3DState()
{
    m_shader3DUnlit.Bind(m_device);
    m_rasterNoCull.Bind(m_device);
    m_blendAdditive.Bind(m_device);
    m_depthNoWrite.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

void Renderer::SetShroudOverlay3DState()
{
    // Shroud overlay: alpha blend, no depth test, no depth write.
    // The shroud is a flat quad that must overlay the entire terrain
    // regardless of terrain height.
    m_shader3D.Bind(m_device);
    m_rasterNoCull.Bind(m_device);
    m_blendAlpha.Bind(m_device);
    m_depthDisabled.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

void Renderer::SetDepthWriteEnabled(bool enabled)
{
    if (enabled)
        m_depthDefault.Bind(m_device);
    else
        m_depthNoWrite.Bind(m_device);
}

void Renderer::SetDepthDisabled()
{
    m_depthDisabled.Bind(m_device);
}

void Renderer::SetMultiplicative3DState()
{
    // Multiplicative blend: result = dest * src_color.
    // Used for shroud to darken terrain while preserving hue.
    m_shader3D.Bind(m_device);
    m_rasterNoCull.Bind(m_device);
    m_blendMultiplicative.Bind(m_device);
    m_depthNoWrite.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

void Renderer::SetShroudParams(float invWorldW, float invWorldH, float offsetX, float offsetY)
{
    m_frameData.shroudParams = { invWorldW, invWorldH, offsetX, offsetY };
}

void Renderer::BindShroudTexture(const Texture* shroudTex)
{
    if (shroudTex)
        shroudTex->BindPS(m_device, 3);
}

void Renderer::SetSkybox3DState()
{
    m_shader3DSkybox.Bind(m_device);
    m_rasterNoCull.Bind(m_device);
    m_blendOpaque.Bind(m_device);
    m_depthDefault.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

void Renderer::SetWaterBump3DState(const Texture* bumpTexture)
{
    m_shader3DWaterBump.Bind(m_device);
    m_rasterNoCull.Bind(m_device);
    m_blendAlpha.Bind(m_device);
    m_depthNoWrite.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
    if (bumpTexture)
        bumpTexture->BindPS(m_device, 1);
}

// Toggle the "enhanced water" pixel-shader path. Drives the
// atmosphereParams.w flag the water shader reads. 0 = classic look
// (matches original DX8), 1 = enhanced (dual normals + fresnel + foam).
// Default OFF — opt-in via the Inspector "Render Toggles → Visual FX →
// Enhanced Water" checkbox.
void Renderer::SetEnhancedWaterEnabled(bool enabled)
{
    m_frameData.atmosphereParams.w = enabled ? 1.0f : 0.0f;
}

void Renderer::BeginWaterDepthRead()
{
    // Switch to read-only DSV so depth buffer can be simultaneously bound as SRV
    m_device.SetBackBufferReadOnlyDepth();
    // Bind depth buffer as SRV on slot t2
    m_device.BindDepthTexturePS(2);
}

void Renderer::EndWaterDepthRead()
{
    // Unbind depth SRV
    m_device.UnbindPSSRVs(2);
    // Restore normal (writable) DSV
    m_device.SetBackBuffer();
}

void Renderer::BindLinearClampSampler()
{
    m_samplerLinearClamp.BindPS(m_device, 0);
}

void Renderer::SetRenderTarget(Texture& rt, int width, int height)
{
    if (!rt.IsValid()) return;
    m_device.ClearRenderTarget(rt, 0, 0, 0, 1);
    m_device.SetRenderTarget(rt);
    SetViewport(0, 0, (float)width, (float)height, 0, 1);
}

void Renderer::RestoreBackBuffer()
{
    m_device.SetBackBuffer();
    ResetViewport();
}

void Renderer::Draw3D(const VertexBuffer& vb, const IndexBuffer& ib, const Texture* texture, const Render::Float4x4& world, const Render::Float4& color)
{
    // Update per-object constant buffer. The CB is permanently bound to slot 1
    // after the first call, so we skip redundant BindVS/BindPS on subsequent calls.
    ObjectConstants obj;
    obj.world = world;
    obj.color = color;
    obj.shaderParams = { (float)m_currentShaderId, m_currentIsPlayerDrawable ? 1.0f : 0.0f, m_currentIsAccentMesh ? 1.0f : 0.0f, m_currentIsZhcaTexture ? 1.0f : 0.0f };
    m_cbObject.Update(m_device, &obj, sizeof(obj));
    if (!m_objectCBBound)
    {
        m_cbObject.BindVS(m_device, 1);
        m_cbObject.BindPS(m_device, 1);
        m_objectCBBound = true;
    }

    // Only rebind texture if it changed since last draw call
    if (texture && texture->IsValid())
    {
        if (texture != m_lastBoundTexture)
        {
            texture->BindPS(m_device, 0);
            m_lastBoundTexture = texture;
        }
    }
    else if (m_lastBoundTexture != nullptr)
    {
        m_device.UnbindPSSRVs(0);
        m_lastBoundTexture = nullptr;
    }

    vb.Bind(m_device);
    ib.Bind(m_device);

    m_device.SetTopology(Topology::TriangleList);
    m_device.DrawIndexed(ib.GetIndexCount(), 0, 0);

    extern int g_debugFrameDrawCalls;
    ++g_debugFrameDrawCalls;
}

void Renderer::Draw3DIndexed(const VertexBuffer& vb, const IndexBuffer& ib, uint32_t indexCount, const Texture* texture, const Render::Float4x4& world, const Render::Float4& color)
{
    ObjectConstants obj;
    obj.world = world;
    obj.color = color;
    obj.shaderParams = { (float)m_currentShaderId, m_currentIsPlayerDrawable ? 1.0f : 0.0f, m_currentIsAccentMesh ? 1.0f : 0.0f, m_currentIsZhcaTexture ? 1.0f : 0.0f };
    m_cbObject.Update(m_device, &obj, sizeof(obj));
    if (!m_objectCBBound)
    {
        m_cbObject.BindVS(m_device, 1);
        m_cbObject.BindPS(m_device, 1);
        m_objectCBBound = true;
    }

    if (texture && texture->IsValid())
    {
        if (texture != m_lastBoundTexture) { texture->BindPS(m_device, 0); m_lastBoundTexture = texture; }
    }
    else if (m_lastBoundTexture != nullptr)
    {
        m_device.UnbindPSSRVs(0);
        m_lastBoundTexture = nullptr;
    }

    vb.Bind(m_device);
    ib.Bind(m_device);

    m_device.SetTopology(Topology::TriangleList);
    m_device.DrawIndexed(indexCount, 0, 0);
}

void Renderer::Draw3DNoIndex(const VertexBuffer& vb, uint32_t vertexCount, const Texture* texture, const Render::Float4x4& world, const Render::Float4& color)
{
    // Don't rebind shader — caller sets the desired shader via SetAdditive3DState etc.
    // m_shader3D would override the unlit shader needed for additive FX.

    ObjectConstants obj;
    obj.world = world;
    obj.color = color;
    obj.shaderParams = { (float)m_currentShaderId, m_currentIsPlayerDrawable ? 1.0f : 0.0f, m_currentIsAccentMesh ? 1.0f : 0.0f, m_currentIsZhcaTexture ? 1.0f : 0.0f };
    m_cbObject.Update(m_device, &obj, sizeof(obj));
    if (!m_objectCBBound)
    {
        m_cbObject.BindVS(m_device, 1);
        m_cbObject.BindPS(m_device, 1);
        m_objectCBBound = true;
    }

    if (texture && texture->IsValid())
    {
        if (texture != m_lastBoundTexture) { texture->BindPS(m_device, 0); m_lastBoundTexture = texture; }
    }
    else if (m_lastBoundTexture != nullptr)
    {
        m_device.UnbindPSSRVs(0);
        m_lastBoundTexture = nullptr;
    }

    vb.Bind(m_device);

    m_device.SetTopology(Topology::TriangleList);
    m_device.Draw(vertexCount, 0);
}

void Renderer::Draw3DMasked(const VertexBuffer& vb, const IndexBuffer& ib, const Texture* baseTexture, const Texture* maskTexture, const Render::Float4x4& world, const Render::Float4& color)
{
    ObjectConstants obj;
    obj.world = world;
    obj.color = color;
    obj.shaderParams = { (float)m_currentShaderId, m_currentIsPlayerDrawable ? 1.0f : 0.0f, m_currentIsAccentMesh ? 1.0f : 0.0f, m_currentIsZhcaTexture ? 1.0f : 0.0f };
    m_cbObject.Update(m_device, &obj, sizeof(obj));
    m_cbObject.BindVS(m_device, 1);
    m_cbObject.BindPS(m_device, 1);

    const Texture* textures[2] = { baseTexture, maskTexture };
    m_device.BindPSTextures(0, textures, 2);

    vb.Bind(m_device);
    ib.Bind(m_device);

    m_device.SetTopology(Topology::TriangleList);
    m_device.DrawIndexed(ib.GetIndexCount(), 0, 0);
}

// 2D rendering ----------------------------------------------------------------

void Renderer::Begin2D()
{
    m_in2DMode = true;
    m_2DVertexCount = 0;
    // Invalidate 3D draw state when switching to 2D
    m_objectCBBound = false;
    m_lastBoundTexture = nullptr;
    m_current2DTexture = nullptr;

    // Reset viewport to full screen so 2D UI covers the entire window
    ResetViewport();

    // Switch to 2D pipeline state
    m_rasterNoCull.Bind(m_device);
    m_blendAlpha.Bind(m_device);
    m_depthDisabled.Bind(m_device);

    // Update screen size constant buffer
    struct { float w, h, pad1, pad2; } screenSize = { (float)m_device.GetWidth(), (float)m_device.GetHeight(), 0, 0 };
    m_cbFrame.Update(m_device, &screenSize, sizeof(screenSize));
    m_cbFrame.BindVS(m_device, 0);

    m_device.SetTopology(Topology::TriangleList);
}

void Renderer::Flush2DBatch()
{
    if (m_2DVertexCount == 0)
        return;

    m_vb2D.Update(m_device, m_2DVertices, m_2DVertexCount * sizeof(Vertex2D));
    m_vb2D.Bind(m_device);

    if (m_current2DTexture)
    {
        if (m_grayscale2D)
            m_shader2DGrayscale.Bind(m_device);
        else
            m_shader2DTextured.Bind(m_device);
        m_current2DTexture->BindPS(m_device, 0);
    }
    else
    {
        m_shader2D.Bind(m_device);
    }

    m_device.Draw(m_2DVertexCount, 0);
    m_2DVertexCount = 0;
}

void Renderer::DrawRect(float x, float y, float w, float h, uint32_t color)
{
    if (m_current2DTexture != nullptr || m_2DVertexCount + 6 > MAX_2D_VERTICES)
    {
        Flush2DBatch();
        m_current2DTexture = nullptr;
    }

    uint32_t c = argbToAbgr(color);
    Vertex2D* v = &m_2DVertices[m_2DVertexCount];
    v[0] = { {x, y},         {0,0}, c };
    v[1] = { {x + w, y},     {1,0}, c };
    v[2] = { {x, y + h},     {0,1}, c };
    v[3] = { {x + w, y},     {1,0}, c };
    v[4] = { {x + w, y + h}, {1,1}, c };
    v[5] = { {x, y + h},     {0,1}, c };
    m_2DVertexCount += 6;
}

void Renderer::DrawImage(const Texture& texture, float x, float y, float w, float h, uint32_t tint)
{
    if (m_current2DTexture != &texture || m_2DVertexCount + 6 > MAX_2D_VERTICES)
    {
        Flush2DBatch();
        m_current2DTexture = &texture;
    }

    uint32_t c = argbToAbgr(tint);
    Vertex2D* v = &m_2DVertices[m_2DVertexCount];
    v[0] = { {x, y},         {0,0}, c };
    v[1] = { {x + w, y},     {1,0}, c };
    v[2] = { {x, y + h},     {0,1}, c };
    v[3] = { {x + w, y},     {1,0}, c };
    v[4] = { {x + w, y + h}, {1,1}, c };
    v[5] = { {x, y + h},     {0,1}, c };
    m_2DVertexCount += 6;
}

void Renderer::DrawImageUV(const Texture& texture, float x, float y, float w, float h,
                            float u0, float v0, float u1, float v1, uint32_t tint)
{
    if (m_current2DTexture != &texture || m_2DVertexCount + 6 > MAX_2D_VERTICES)
    {
        Flush2DBatch();
        m_current2DTexture = &texture;
    }

    uint32_t c = argbToAbgr(tint);
    Vertex2D* v = &m_2DVertices[m_2DVertexCount];
    v[0] = { {x, y},         {u0, v0}, c };
    v[1] = { {x + w, y},     {u1, v0}, c };
    v[2] = { {x, y + h},     {u0, v1}, c };
    v[3] = { {x + w, y},     {u1, v0}, c };
    v[4] = { {x + w, y + h}, {u1, v1}, c };
    v[5] = { {x, y + h},     {u0, v1}, c };
    m_2DVertexCount += 6;
}

void Renderer::DrawImageUVRotatedCCW90(const Texture& texture, float x, float y, float w, float h,
                                       float u0, float v0, float u1, float v1, uint32_t tint)
{
    if (m_current2DTexture != &texture || m_2DVertexCount + 6 > MAX_2D_VERTICES)
    {
        Flush2DBatch();
        m_current2DTexture = &texture;
    }

    uint32_t c = argbToAbgr(tint);
    Vertex2D* v = &m_2DVertices[m_2DVertexCount];
    // Quad vertices stay axis-aligned to the destination rect; the UVs are
    // rotated 90° CCW so the sampled texel at each corner matches what a
    // counter-clockwise rotation of the source image would produce. Mapping:
    //   top-left  of quad → top-right of texture
    //   top-right of quad → bottom-right of texture
    //   bot-left  of quad → top-left of texture
    //   bot-right of quad → bottom-left of texture
    v[0] = { {x, y},         {u1, v0}, c };
    v[1] = { {x + w, y},     {u1, v1}, c };
    v[2] = { {x, y + h},     {u0, v0}, c };
    v[3] = { {x + w, y},     {u1, v1}, c };
    v[4] = { {x + w, y + h}, {u0, v1}, c };
    v[5] = { {x, y + h},     {u0, v0}, c };
    m_2DVertexCount += 6;
}

void Renderer::DrawTri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color)
{
    if (m_current2DTexture != nullptr || m_2DVertexCount + 3 > MAX_2D_VERTICES)
    {
        Flush2DBatch();
        m_current2DTexture = nullptr;
    }

    uint32_t c = argbToAbgr(color);
    Vertex2D* v = &m_2DVertices[m_2DVertexCount];
    v[0] = { {x0, y0}, {0,0}, c };
    v[1] = { {x1, y1}, {0,0}, c };
    v[2] = { {x2, y2}, {0,0}, c };
    m_2DVertexCount += 3;
}

void Renderer::DrawLine(float x1, float y1, float x2, float y2, float width, uint32_t color)
{
    // Expand line into a quad
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f)
        return;

    float nx = -dy / len * width * 0.5f;
    float ny = dx / len * width * 0.5f;

    if (m_current2DTexture != nullptr || m_2DVertexCount + 6 > MAX_2D_VERTICES)
    {
        Flush2DBatch();
        m_current2DTexture = nullptr;
    }

    uint32_t c = argbToAbgr(color);
    Vertex2D* v = &m_2DVertices[m_2DVertexCount];
    v[0] = { {x1 + nx, y1 + ny}, {0,0}, c };
    v[1] = { {x2 + nx, y2 + ny}, {1,0}, c };
    v[2] = { {x1 - nx, y1 - ny}, {0,1}, c };
    v[3] = { {x2 + nx, y2 + ny}, {1,0}, c };
    v[4] = { {x2 - nx, y2 - ny}, {1,1}, c };
    v[5] = { {x1 - nx, y1 - ny}, {0,1}, c };
    m_2DVertexCount += 6;
}

void Renderer::Set2DGrayscale(bool grayscale)
{
    if (m_grayscale2D != grayscale)
    {
        Flush2DBatch();
        m_grayscale2D = grayscale;
    }
}

void Renderer::DrawImageFSR(const Texture& srcTexture, float dstX, float dstY, float dstW, float dstH)
{
    if (!m_fsrReady)
    {
        DrawImage(srcTexture, dstX, dstY, dstW, dstH);
        return;
    }

    // Flush any pending 2D draws before we change pipeline state
    Flush2DBatch();

    int outW = (int)dstW;
    int outH = (int)dstH;

    // Lazily create/resize the intermediate render target at display resolution
    if (m_fsrRTWidth != outW || m_fsrRTHeight != outH)
    {
        m_fsrRT.CreateRenderTarget(m_device, outW, outH);
        m_fsrRTWidth = outW;
        m_fsrRTHeight = outH;
    }

    m_rasterNoCull.Bind(m_device);
    m_blendOpaque.Bind(m_device);
    m_depthDisabled.Bind(m_device);

    // ── Pass 1: EASU — upscale source texture to intermediate RT ──
    {
        FSRConstants fsr = {};
        fsr.texelSize = { 1.0f / (float)srcTexture.GetWidth(), 1.0f / (float)srcTexture.GetHeight() };
        fsr.sharpness = 0.0f;
        fsr.pad = 0.0f;
        m_cbFSR.Update(m_device, &fsr, sizeof(fsr));
        m_cbFSR.BindPS(m_device, 0);
        m_cbFSR.BindVS(m_device, 0);

        // Render to intermediate RT (full area, no clear needed since we write every pixel)
        m_device.SetRenderTarget(m_fsrRT);
        SetViewport(0, 0, (float)outW, (float)outH);

        m_shaderEASU.Bind(m_device);
        srcTexture.BindPS(m_device, 0);
        m_samplerLinear.BindPS(m_device, 0);
        m_samplerPoint.BindPS(m_device, 1);

        m_fullscreenQuadVB.Bind(m_device);
        m_device.SetTopology(Topology::TriangleStrip);
        m_device.Draw(4, 0);
    }

    // ── Pass 2: RCAS — sharpen the EASU output back to backbuffer ──
    {
        FSRConstants fsr = {};
        fsr.texelSize = { 1.0f / (float)outW, 1.0f / (float)outH };
        fsr.sharpness = 0.2f; // adjustable: 0 = max sharpening, 2 = none
        fsr.pad = 0.0f;
        m_cbFSR.Update(m_device, &fsr, sizeof(fsr));
        m_cbFSR.BindPS(m_device, 0);

        // Restore backbuffer and set viewport to destination rect
        RestoreBackBuffer();
        SetViewport(dstX, dstY, dstW, dstH);

        m_shaderRCAS.Bind(m_device);
        m_fsrRT.BindPS(m_device, 0);
        m_samplerLinear.BindPS(m_device, 0);
        m_samplerPoint.BindPS(m_device, 1);

        m_fullscreenQuadVB.Bind(m_device);
        m_device.SetTopology(Topology::TriangleStrip);
        m_device.Draw(4, 0);

        // Unbind RT as SRV to avoid D3D warnings
        m_device.UnbindPSSRVs(0);
    }

    // Restore 2D state so subsequent draws work correctly
    ResetViewport();
    m_blendAlpha.Bind(m_device);
    m_depthDisabled.Bind(m_device);
    m_rasterNoCull.Bind(m_device);

    // Re-bind 2D screen size constants (FSR overwrote b0)
    struct { float w, h, pad1, pad2; } screenSize = { (float)m_device.GetWidth(), (float)m_device.GetHeight(), 0, 0 };
    m_cbFrame.Update(m_device, &screenSize, sizeof(screenSize));
    m_cbFrame.BindVS(m_device, 0);

    // Reset batch state
    m_current2DTexture = nullptr;
    m_2DVertexCount = 0;
}

void Renderer::End2D()
{
    Flush2DBatch();
    m_in2DMode = false;
    m_grayscale2D = false;

    // Restore 3D state
    m_rasterDefault.Bind(m_device);
    m_blendOpaque.Bind(m_device);
    m_depthDefault.Bind(m_device);

    // Begin2D() overwrites b0 with screen-size data for 2D rendering.
    // Restore the real 3D frame constants so any subsequent 3D draws
    // (e.g. FlushTranslucent, particles, water) use the correct camera.
    extern bool g_debugDisableFlushConstants;
    if (!g_debugDisableFlushConstants)
        FlushFrameConstants();
}

void Renderer::DrawSnowInstanced(uint32_t instanceCount, const Texture* snowTex,
                                  GPUBuffer& noiseBuffer, const SnowConstants& params)
{
    if (instanceCount == 0 || !snowTex)
        return;

    // Upload snow constants
    m_cbSnow.Update(m_device, &params, sizeof(params));
    m_cbSnow.BindVS(m_device, 2);

    // Bind frame constants (viewProjection, cameraPos) at b0
    m_cbFrame.BindVS(m_device, 0);

    // Bind noise table StructuredBuffer at t3 for VS
    noiseBuffer.BindVS(m_device, 3);

    // Bind snow texture at t0 for PS
    snowTex->BindPS(m_device, 0);

    // Set state: alpha blend, no depth write
    m_shaderSnow.Bind(m_device);
    m_rasterNoCull.Bind(m_device);
    m_blendAlpha.Bind(m_device);
    m_depthNoWrite.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);

    // No vertex buffer — VS generates positions from SV_VertexID + SV_InstanceID
    m_device.ClearInputLayout();
    m_device.SetTopology(Topology::TriangleList);
    m_device.DrawInstanced(6, instanceCount, 0, 0);

    // Clean up VS SRV binding
    m_device.UnbindVSSRVs(3);
}

void Renderer::DrawDecalsInstanced(uint32_t instanceCount, const Texture* decalTex,
                                    GPUBuffer& instanceBuffer, const Texture* heightmapTex,
                                    const DecalConstants& params, DecalBlend blend)
{
    if (instanceCount == 0 || !decalTex)
        return;

    // Upload decal constants to b2
    m_cbDecal.Update(m_device, &params, sizeof(params));
    m_cbDecal.BindVS(m_device, 2);

    // Bind frame constants (viewProjection, cameraPos) at b0
    m_cbFrame.BindVS(m_device, 0);

    // Bind instance StructuredBuffer at t3 for VS
    instanceBuffer.BindVS(m_device, 3);

    // Bind heightmap texture at t4 for VS
    if (heightmapTex)
        heightmapTex->BindVS(m_device, 4);

    // Bind decal texture at t0 for PS
    decalTex->BindPS(m_device, 0);

    // Set shader, pipeline state, and blend mode
    m_shaderDecal.Bind(m_device);
    m_rasterNoCull.Bind(m_device);
    m_depthNoWrite.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
    m_samplerLinear.BindVS(m_device, 0); // VS samples heightmap

    switch (blend)
    {
        case DecalBlend::Multiplicative: m_blendMultiplicative.Bind(m_device); break;
        case DecalBlend::Alpha:          m_blendAlpha.Bind(m_device); break;
        case DecalBlend::Additive:       m_blendAdditive.Bind(m_device); break;
    }

    // No vertex buffer — VS generates quad from SV_VertexID + SV_InstanceID
    m_device.ClearInputLayout();
    m_device.SetTopology(Topology::TriangleList);
    m_device.DrawInstanced(6, instanceCount, 0, 0);

    // Clean up all VS state changes to prevent leaking into subsequent passes
    m_device.UnbindVSSRVs(3, 2);
    m_device.UnbindVSSamplers(0);
    m_device.UnbindVSConstantBuffers(2);
}

void Renderer::EnsurePingPongRTs()
{
    int w = m_device.GetWidth();
    int h = m_device.GetHeight();
    if (w <= 0 || h <= 0) return;
    if (m_ppWidth == w && m_ppHeight == h) return;
    m_pingRT.CreateRenderTarget(m_device, w, h);
    m_pongRT.CreateRenderTarget(m_device, w, h);
    m_ppWidth = w;
    m_ppHeight = h;
    m_ppReadIdx = 0;
}

void Renderer::CopyBackbufferToPing()
{
    EnsurePingPongRTs();
    m_device.CopyBackBufferToTexture(m_pingRT);
    m_ppReadIdx = 0; // ping has the data, read from ping first
}

void Renderer::CopyPingToBackbuffer()
{
    m_device.CopyTextureToBackBuffer(GetPPReadRT());
}

void Renderer::EnsureSceneRT()
{
    int sceneW = m_device.GetWidth();
    int sceneH = m_device.GetHeight();
    m_sceneRT.CreateRenderTarget(m_device, sceneW, sceneH);
}

void Renderer::CopyBackbufferToSceneRT()
{
    EnsureSceneRT();
    if (!m_sceneRT.IsValid()) return;
    m_device.CopyBackBufferToTexture(m_sceneRT);
}

void Renderer::EnsurePostChainInput()
{
    if (!m_postChainDirty)
    {
        // First effect in the chain — copy backbuffer to ping and set shared state
        EnsurePingPongRTs();
        CopyBackbufferToPing();
        m_postChainDirty = true;

        m_rasterNoCull.Bind(m_device);
        m_blendOpaque.Bind(m_device);
        m_depthDisabled.Bind(m_device);
        m_samplerLinearClamp.BindPS(m_device, 0);
        m_fullscreenQuadVB.Bind(m_device);
        m_device.SetTopology(Topology::TriangleStrip);
    }
}

void Renderer::BeginPostChain()
{
    m_inPostChain = true;
    m_postChainDirty = false;
}

void Renderer::EndPostChain()
{
    if (!m_inPostChain) return;
    if (m_postChainDirty)
    {
        CopyPingToBackbuffer();
        RestoreBackBuffer();
    }
    m_inPostChain = false;
    m_postChainDirty = false;
}

void Renderer::SetAtmosphereEnabled(bool enabled)
{
    m_atmosphereEnabled = enabled;
    m_frameData.atmosphereParams.x = enabled ? 0.00007f : 0.0f; // gentle fog density
    m_frameData.atmosphereParams.y = enabled ? 2.0f : 0.0f;     // subtle scatter power
}

void Renderer::SetSurfaceSpecularEnabled(bool enabled)
{
    m_surfaceSpecEnabled = enabled;
    m_frameData.atmosphereParams.z = enabled ? 0.08f : 0.0f; // specular intensity
}

void Renderer::UpdateShockwaves()
{
    int numPL = (int)m_frameData.lightingOptions.z;
    Float4x4 vp = m_frameData.viewProjection;

    // Match current point lights to existing shockwaves, or spawn new ones.
    // Threshold raised from 5 → 18: a single tank-shell impact buddy-light
    // routinely sums to 5-8 (per-channel ~2.0), triggering shockwave rings on
    // every shot. Only really-large explosions (nuke, fuel-air, MOAB) clear
    // 18. The original game has no shockwave effect at all, so this is the
    // most conservative threshold that still keeps the cinematic look on the
    // few effects where it actually fits.
    for (int p = 0; p < numPL && p < (int)kMaxPointLights; p++)
    {
        float wx = m_frameData.pointLightPositions[p].x;
        float wy = m_frameData.pointLightPositions[p].y;
        float wz = m_frameData.pointLightPositions[p].z;
        float intensity = m_frameData.pointLightColors[p].x
                        + m_frameData.pointLightColors[p].y
                        + m_frameData.pointLightColors[p].z;
        if (intensity < 18.0f) continue;

        // Project to screen UV
        Float4 worldPos(wx, wy, wz, 1.0f);
        Float4 clip = Float4Transform(worldPos, vp);
        float w = clip.w;
        if (w < 0.1f) continue;
        float sx = clip.x / w * 0.5f + 0.5f;
        float sy = -clip.y / w * 0.5f + 0.5f;
        if (sx < -0.3f || sx > 1.3f || sy < -0.3f || sy > 1.3f) continue;

        // Check if this matches an existing shockwave
        bool found = false;
        for (uint32_t s = 0; s < kMaxShockwaves; s++)
        {
            if (!m_shockwaves[s].active) continue;
            float dx = m_shockwaves[s].worldX - wx;
            float dy = m_shockwaves[s].worldY - wy;
            if (dx * dx + dy * dy < 400.0f)
            {
                m_shockwaves[s].screenX = sx;
                m_shockwaves[s].screenY = sy;
                found = true;
                break;
            }
        }

        if (!found)
        {
            for (uint32_t s = 0; s < kMaxShockwaves; s++)
            {
                if (!m_shockwaves[s].active)
                {
                    m_shockwaves[s] = { sx, sy, 0.0f, 1.0f, wx, wy, true };
                    // Trigger explosion flash (intensity proportional to light brightness)
                    if (m_explosionFlashEnabled && intensity > 1.0f)
                        m_flashIntensity = (intensity > 3.0f) ? 1.0f : intensity / 3.0f;
                    break;
                }
            }
        }
    }

    // Advance all active shockwaves
    for (uint32_t s = 0; s < kMaxShockwaves; s++)
    {
        if (!m_shockwaves[s].active) continue;
        m_shockwaves[s].phase += 0.022f;
        m_shockwaves[s].intensity -= 0.018f;
        if (m_shockwaves[s].intensity <= 0.0f || m_shockwaves[s].phase > 1.2f)
        {
            m_shockwaves[s].active = false;
            m_shockwaves[s].intensity = 0.0f;
        }
    }
}

void Renderer::SetLaserGlow3DState()
{
    m_shader3DLaserGlow.Bind(m_device);
    m_rasterNoCullLaserBias.Bind(m_device);
    m_blendAdditive.Bind(m_device);
    m_depthNoWrite.Bind(m_device);
    m_samplerLinear.BindPS(m_device, 0);
}

void Renderer::ApplyShockwave()
{
    if (!m_shockwaveEnabled)
        return;

    UpdateShockwaves();

    int count = 0;
    for (uint32_t s = 0; s < kMaxShockwaves; s++)
        if (m_shockwaves[s].active) count++;
    if (count == 0)
        return;

    int sceneW = m_device.GetWidth();
    int sceneH = m_device.GetHeight();
    if (sceneW <= 0 || sceneH <= 0)
        return;

    // Get input texture — from ping-pong chain if active, otherwise copy backbuffer
    Texture* inputRT;
    if (m_inPostChain) {
        EnsurePostChainInput();
        inputRT = &GetPPReadRT();
    } else {
        CopyBackbufferToSceneRT();
        inputRT = &m_sceneRT;
    }

    ShockwaveConstants sc = {};
    int idx = 0;
    for (uint32_t s = 0; s < kMaxShockwaves && idx < 8; s++)
    {
        if (m_shockwaves[s].active)
        {
            sc.shockwaves[idx] = { m_shockwaves[s].screenX, m_shockwaves[s].screenY,
                                   m_shockwaves[s].phase, m_shockwaves[s].intensity };
            idx++;
        }
    }
    sc.texelSize = { 1.0f / (float)sceneW, 1.0f / (float)sceneH };
    sc.time = m_frameData.lightingOptions.y * 0.001f;
    sc.shockwaveCount = (float)idx;

    m_cbShockwave.Update(m_device, &sc, sizeof(sc));
    m_cbShockwave.BindPS(m_device, 0);
    m_cbShockwave.BindVS(m_device, 0);

    if (m_inPostChain) {
        SetRenderTarget(GetPPWriteRT(), sceneW, sceneH);
    } else {
        RestoreBackBuffer();
        m_rasterNoCull.Bind(m_device);
        m_blendOpaque.Bind(m_device);
        m_depthDisabled.Bind(m_device);
        m_samplerLinearClamp.BindPS(m_device, 0);
    }
    m_shaderShockwave.Bind(m_device);
    inputRT->BindPS(m_device, 0);
    m_fullscreenQuadVB.Bind(m_device);
    m_device.SetTopology(Topology::TriangleStrip);
    m_device.Draw(4, 0);

    m_device.UnbindPSSRVs(0);
    if (m_inPostChain) SwapPP();
}

void Renderer::ApplyExplosionFlash()
{
    if (!m_explosionFlashEnabled)
    {
        m_flashIntensity = 0.0f;
        return;
    }

    // Ensure shockwave detection runs even if shockwave rendering is disabled,
    // since we piggyback on it for flash triggers
    if (!m_shockwaveEnabled)
        UpdateShockwaves();

    if (m_flashIntensity <= 0.01f)
        return;

    float alpha = m_flashIntensity * 0.25f;
    if (alpha > 0.25f) alpha = 0.25f;

    // White flash as a 2D rect overlay
    Begin2D();
    uint8_t a = (uint8_t)(alpha * 255.0f);
    DrawRect(0.0f, 0.0f, (float)m_device.GetWidth(), (float)m_device.GetHeight(),
             (a << 24) | 0x00FFFFFF);
    End2D();

    // Rapid decay: ~14 frames to fade out
    m_flashIntensity -= 0.07f;
    if (m_flashIntensity < 0.0f)
        m_flashIntensity = 0.0f;
}

void Renderer::ApplyGodRays()
{
    if (!m_godRaysEnabled)
        return;

    int sceneW = m_device.GetWidth();
    int sceneH = m_device.GetHeight();
    if (sceneW <= 0 || sceneH <= 0)
        return;

    int halfW = (sceneW / 2) > 1 ? (sceneW / 2) : 1;
    int halfH = (sceneH / 2) > 1 ? (sceneH / 2) : 1;

    m_godRayExtractRT.CreateRenderTarget(m_device, halfW, halfH);
    m_godRayBlurRT.CreateRenderTarget(m_device, halfW, halfH);
    if (!m_godRayExtractRT.IsValid() || !m_godRayBlurRT.IsValid())
        return;

    // Project sun to screen UV
    Float4 sunDir4 = m_frameData.lightDirections[0];
    Float4 camPos4 = m_frameData.cameraPos;
    Float4 sunWorldPos = Float4Sub(camPos4, Float4Scale(sunDir4, 5000.0f));
    sunWorldPos.w = 1.0f;
    Float4 sunClip = Float4Transform(sunWorldPos, m_frameData.viewProjection);
    float w = sunClip.w;
    float sunU = 0.5f, sunV = 0.5f;
    if (w > 0.1f)
    {
        sunU = sunClip.x / w * 0.5f + 0.5f;
        sunV = -sunClip.y / w * 0.5f + 0.5f;
    }

    // Get input texture
    Texture* inputRT;
    if (m_inPostChain) {
        EnsurePostChainInput();
        inputRT = &GetPPReadRT();
    } else {
        CopyBackbufferToSceneRT();
        inputRT = &m_sceneRT;
    }

    if (!m_inPostChain) {
        m_rasterNoCull.Bind(m_device);
        m_blendOpaque.Bind(m_device);
        m_depthDisabled.Bind(m_device);
        m_samplerLinearClamp.BindPS(m_device, 0);
    }

    GodRayConstants gc = {};
    gc.sunScreenUV = { sunU, sunV };
    gc.density = 0.5f;
    gc.decay = 0.97f;
    gc.weight = 0.25f;
    gc.exposure = 0.15f;
    gc.threshold = 0.75f;
    gc.numSamples = 32.0f;
    gc.texelSize = { 1.0f / (float)sceneW, 1.0f / (float)sceneH };

    // Pass 1: Extract bright pixels at half-res
    {
        m_cbGodRays.Update(m_device, &gc, sizeof(gc));
        m_cbGodRays.BindPS(m_device, 0);
        m_cbGodRays.BindVS(m_device, 0);
        SetRenderTarget(m_godRayExtractRT, halfW, halfH);
        m_shaderGodRayExtract.Bind(m_device);
        inputRT->BindPS(m_device, 0);
        m_fullscreenQuadVB.Bind(m_device);
        m_device.SetTopology(Topology::TriangleStrip);
        m_device.Draw(4, 0);
    }

    // Pass 2: Radial blur at half-res
    {
        gc.texelSize = { 1.0f / (float)halfW, 1.0f / (float)halfH };
        m_cbGodRays.Update(m_device, &gc, sizeof(gc));
        SetRenderTarget(m_godRayBlurRT, halfW, halfH);
        m_shaderGodRayBlur.Bind(m_device);
        m_godRayExtractRT.BindPS(m_device, 0);
        m_fullscreenQuadVB.Bind(m_device);
        m_device.SetTopology(Topology::TriangleStrip);
        m_device.Draw(4, 0);
    }

    // Pass 3: Composite scene + god rays
    {
        if (m_inPostChain) {
            SetRenderTarget(GetPPWriteRT(), sceneW, sceneH);
        } else {
            RestoreBackBuffer();
        }
        m_shaderGodRayComposite.Bind(m_device);
        const Texture* textures[2] = { inputRT, &m_godRayBlurRT };
        m_device.BindPSTextures(0, textures, 2);
        m_fullscreenQuadVB.Bind(m_device);
        m_device.SetTopology(Topology::TriangleStrip);
        m_device.Draw(4, 0);

        m_device.UnbindPSSRVs(0, 2);
    }
    if (m_inPostChain) SwapPP();
}

void Renderer::ApplyCinematic()
{
    if (!m_chromaEnabled && !m_vignetteEnabled && !m_colorGradeEnabled && !m_bwFilterEnabled)
        return;

    int sceneW = m_device.GetWidth();
    int sceneH = m_device.GetHeight();
    if (sceneW <= 0 || sceneH <= 0)
        return;

    Texture* inputRT;
    if (m_inPostChain) {
        EnsurePostChainInput();
        inputRT = &GetPPReadRT();
    } else {
        CopyBackbufferToSceneRT();
        inputRT = &m_sceneRT;
    }

    CinematicConstants cc = {};
    cc.texelSize = { 1.0f / (float)sceneW, 1.0f / (float)sceneH };
    cc.chromaAmount = m_chromaEnabled ? 0.002f : 0.0f;
    cc.vignetteStrength = m_vignetteEnabled ? 0.85f : 0.0f;
    cc.colorGradeIntensity = m_colorGradeEnabled ? 0.2f : 0.0f;
    // BW filter: force saturation to 0 so the cinematic shader's `lerp(luma,
    // color, saturation)` collapses every pixel to grayscale. Wins over the
    // colorGradeEnabled boost because the script-driven BW filter takes
    // precedence (used during fade-to-bw cinematic transitions in campaign).
    if (m_bwFilterEnabled)
        cc.saturation = 0.0f;
    else
        cc.saturation = m_colorGradeEnabled ? 1.06f : 1.0f;
    cc.contrast = m_colorGradeEnabled ? 1.04f : 1.0f;

    m_cbCinematic.Update(m_device, &cc, sizeof(cc));
    m_cbCinematic.BindPS(m_device, 0);
    m_cbCinematic.BindVS(m_device, 0);

    if (m_inPostChain) {
        SetRenderTarget(GetPPWriteRT(), sceneW, sceneH);
    } else {
        RestoreBackBuffer();
        m_rasterNoCull.Bind(m_device);
        m_blendOpaque.Bind(m_device);
        m_depthDisabled.Bind(m_device);
        m_samplerLinearClamp.BindPS(m_device, 0);
    }
    m_shaderCinematic.Bind(m_device);
    inputRT->BindPS(m_device, 0);
    m_fullscreenQuadVB.Bind(m_device);
    m_device.SetTopology(Topology::TriangleStrip);
    m_device.Draw(4, 0);

    m_device.UnbindPSSRVs(0);
    if (m_inPostChain) SwapPP();
}

void Renderer::SetGroundFog(int index, float x, float y, float z, float radius, float r, float g, float b, float density)
{
    if (index < 0 || index >= (int)kMaxGroundFog) return;
    m_groundFog[index] = { x, y, z, radius, r, g, b, density, true };
}

void Renderer::ClearGroundFog()
{
    for (uint32_t i = 0; i < kMaxGroundFog; i++)
        m_groundFog[i].active = false;
}

void Renderer::TrackParticleSystem(uintptr_t id, float x, float y, float z, float r, float g, float b, float size)
{
    // Update existing
    for (uint32_t i = 0; i < kMaxTrackedParticleSys; i++)
    {
        if (m_trackedPS[i].wasAlive && m_trackedPS[i].id == id)
        {
            m_trackedPS[i].x = x; m_trackedPS[i].y = y; m_trackedPS[i].z = z;
            m_trackedPS[i].r = r; m_trackedPS[i].g = g; m_trackedPS[i].b = b;
            m_trackedPS[i].size = size;
            m_trackedPS[i].seenThisFrame = true;
            return;
        }
    }
    // New — find empty slot
    for (uint32_t i = 0; i < kMaxTrackedParticleSys; i++)
    {
        if (!m_trackedPS[i].wasAlive)
        {
            m_trackedPS[i] = { id, x, y, z, r, g, b, size, true, true };
            return;
        }
    }
}

void Renderer::FinishParticleTracking()
{
    // Phase 1: any system that was alive last frame but NOT seen this frame → died
    for (uint32_t i = 0; i < kMaxTrackedParticleSys; i++)
    {
        if (m_trackedPS[i].wasAlive && !m_trackedPS[i].seenThisFrame)
        {
            // System was deleted by game logic — spawn a fade-out cloud
            if (m_trackedPS[i].size > 0.5f)
            {
                for (uint32_t c = 0; c < kMaxVolClouds; c++)
                {
                    if (!m_volClouds[c].active)
                    {
                        m_volClouds[c].worldX = m_trackedPS[i].x;
                        m_volClouds[c].worldY = m_trackedPS[i].y;
                        m_volClouds[c].worldZ = m_trackedPS[i].z;
                        m_volClouds[c].radius = m_trackedPS[i].size * 1.5f;
                        if (m_volClouds[c].radius < 6.0f) m_volClouds[c].radius = 6.0f;
                        m_volClouds[c].r = m_trackedPS[i].r;
                        m_volClouds[c].g = m_trackedPS[i].g;
                        m_volClouds[c].b = m_trackedPS[i].b;
                        m_volClouds[c].density = 0.25f; // low — just a brief visual transition
                        m_volClouds[c].age = 4.0f;     // start aged so it dies faster (max age = 8)
                        m_volClouds[c].active = true;
                        break;
                    }
                }
            }
            m_trackedPS[i].wasAlive = false;
        }
    }

    // Phase 2: reset seenThisFrame for next frame
    for (uint32_t i = 0; i < kMaxTrackedParticleSys; i++)
    {
        if (m_trackedPS[i].seenThisFrame)
        {
            m_trackedPS[i].wasAlive = true;
            m_trackedPS[i].seenThisFrame = false;
        }
    }
}

void Renderer::UpdateVolClouds()
{
    int numPL = (int)m_frameData.lightingOptions.z;

    // Detect new point lights and spawn persistent clouds
    for (int p = 0; p < numPL && p < (int)kMaxPointLights; p++)
    {
        float intensity = m_frameData.pointLightColors[p].x
                        + m_frameData.pointLightColors[p].y
                        + m_frameData.pointLightColors[p].z;
        if (intensity < 0.3f) continue;

        float wx = m_frameData.pointLightPositions[p].x;
        float wy = m_frameData.pointLightPositions[p].y;
        float wz = m_frameData.pointLightPositions[p].z;

        // Check if this matches an existing cloud
        bool found = false;
        for (uint32_t c = 0; c < kMaxVolClouds; c++)
        {
            if (!m_volClouds[c].active) continue;
            float dx = m_volClouds[c].worldX - wx;
            float dy = m_volClouds[c].worldY - wy;
            if (dx * dx + dy * dy < 400.0f) // within 20 units
            {
                // Refresh: keep density high while point light is active
                if (m_volClouds[c].density < 0.8f)
                    m_volClouds[c].density = 0.8f;
                found = true;
                break;
            }
        }

        if (!found)
        {
            // Spawn new cloud — find empty slot, or recycle the most-faded one
            uint32_t bestSlot = kMaxVolClouds;
            float lowestDensity = 2.0f;
            for (uint32_t c = 0; c < kMaxVolClouds; c++)
            {
                if (!m_volClouds[c].active) { bestSlot = c; break; }
                if (m_volClouds[c].density < lowestDensity) { lowestDensity = m_volClouds[c].density; bestSlot = c; }
            }
            if (bestSlot < kMaxVolClouds)
            {
                uint32_t c = bestSlot;
                float radius = m_frameData.pointLightPositions[p].w;
                if (radius < 2.0f) radius = 18.0f;
                m_volClouds[c].worldX = wx;
                m_volClouds[c].worldY = wy;
                m_volClouds[c].worldZ = wz;
                m_volClouds[c].radius = radius * 1.8f;
                if (m_volClouds[c].radius < 15.0f) m_volClouds[c].radius = 15.0f;
                m_volClouds[c].r = m_frameData.pointLightColors[p].x;
                m_volClouds[c].g = m_frameData.pointLightColors[p].y;
                m_volClouds[c].b = m_frameData.pointLightColors[p].z;
                m_volClouds[c].density = 1.0f;
                m_volClouds[c].age = 0.0f;
                m_volClouds[c].active = true;
            }
        }
    }

    // Age all clouds — fade and force-kill after max age
    for (uint32_t c = 0; c < kMaxVolClouds; c++)
    {
        if (!m_volClouds[c].active) continue;
        m_volClouds[c].age += 0.016f;
        // Force kill after 8 seconds regardless of density
        if (m_volClouds[c].age > 8.0f)
        {
            m_volClouds[c].active = false;
            m_volClouds[c].density = 0.0f;
            continue;
        }
        float fadeRate = 0.008f + (1.0f - m_volClouds[c].density) * 0.006f;
        m_volClouds[c].density -= fadeRate;
        m_volClouds[c].radius += 0.3f; // faster physical expansion
        if (m_volClouds[c].density <= 0.0f)
        {
            m_volClouds[c].active = false;
            m_volClouds[c].density = 0.0f;
        }
    }
}

void Renderer::ApplyVolumetricExplosions()
{
    if (!m_volumetricEnabled)
        return;

    UpdateVolClouds();

    // Count active clouds + ground fog
    int count = 0;
    for (uint32_t c = 0; c < kMaxVolClouds; c++)
        if (m_volClouds[c].active) count++;
    int gfActive = 0;
    if (m_modernAOEEnabled)
        for (uint32_t i = 0; i < kMaxGroundFog; i++)
            if (m_groundFog[i].active) gfActive++;
    if (count == 0 && gfActive == 0)
        return;

    int sceneW = m_device.GetWidth();
    int sceneH = m_device.GetHeight();
    if (sceneW <= 0 || sceneH <= 0)
        return;

    // Get input texture
    Texture* inputRT;
    if (m_inPostChain) {
        EnsurePostChainInput();
        inputRT = &GetPPReadRT();
    } else {
        CopyBackbufferToSceneRT();
        inputRT = &m_sceneRT;
    }

    Float4x4 invVPf = Float4x4Inverse(m_frameData.viewProjection);

    // Build constant buffer from persistent cloud tracker
    VolumetricConstants vc = {};
    vc.invViewProjection = invVPf;
    vc.camPos = m_frameData.cameraPos;

    // Upload top 8 densest clouds to GPU (cap for performance)
    // Sort by density to pick the most visible ones
    struct CRef { uint32_t i; float d; };
    CRef crefs[128];
    int nRefs = 0;
    for (uint32_t c = 0; c < kMaxVolClouds; c++)
        if (m_volClouds[c].active)
            crefs[nRefs++] = { c, m_volClouds[c].density };
    for (int i = 0; i < nRefs - 1; i++)
        for (int j = i + 1; j < nRefs; j++)
            if (crefs[j].d > crefs[i].d)
                { CRef t = crefs[i]; crefs[i] = crefs[j]; crefs[j] = t; }

    int numActive = 0;
    for (int ci = 0; ci < nRefs && numActive < (int)kMaxRenderedClouds; ci++)
    {
        uint32_t c = crefs[ci].i;
        vc.clouds[numActive] = { m_volClouds[c].worldX, m_volClouds[c].worldY,
                                 m_volClouds[c].worldZ, m_volClouds[c].radius };
        vc.cloudColors[numActive] = { m_volClouds[c].r, m_volClouds[c].g,
                                      m_volClouds[c].b, m_volClouds[c].density };
        numActive++;
    }
    count = numActive;

    // Ground AOE fog
    int gfCount = 0;
    if (m_modernAOEEnabled)
    {
        for (uint32_t i = 0; i < kMaxGroundFog && gfCount < (int)kMaxRenderedGroundFog; i++)
        {
            if (!m_groundFog[i].active) continue;
            vc.gfClouds[gfCount] = { m_groundFog[i].worldX, m_groundFog[i].worldY,
                                     m_groundFog[i].worldZ, m_groundFog[i].radius };
            vc.gfColors[gfCount] = { m_groundFog[i].r, m_groundFog[i].g,
                                     m_groundFog[i].b, m_groundFog[i].density };
            gfCount++;
        }
    }

    vc.sunDirection = m_frameData.lightDirections[0];
    vc.sunColor = m_frameData.lightColors[0];
    vc.texelSize = { 1.0f / (float)sceneW, 1.0f / (float)sceneH };
    vc.time = m_frameData.lightingOptions.y * 0.001f;
    vc.numClouds = (float)count;
    vc.numGroundFog = (float)gfCount;

    m_cbVolumetric.Update(m_device, &vc, sizeof(vc));
    m_cbVolumetric.BindPS(m_device, 0);
    m_cbVolumetric.BindVS(m_device, 0);

    if (m_inPostChain) {
        SetRenderTarget(GetPPWriteRT(), sceneW, sceneH);
    } else {
        RestoreBackBuffer();
        m_rasterNoCull.Bind(m_device);
        m_blendOpaque.Bind(m_device);
        m_depthDisabled.Bind(m_device);
        m_samplerLinearClamp.BindPS(m_device, 0);
    }
    m_shaderVolumetric.Bind(m_device);
    inputRT->BindPS(m_device, 0);
    m_fullscreenQuadVB.Bind(m_device);
    m_device.SetTopology(Topology::TriangleStrip);
    m_device.Draw(4, 0);

    m_device.UnbindPSSRVs(0);
    if (m_inPostChain) SwapPP();
}

void Renderer::ApplyLensFlare()
{
    if (!m_lensFlareEnabled)
        return;

    int sceneW = m_device.GetWidth();
    int sceneH = m_device.GetHeight();
    if (sceneW <= 0 || sceneH <= 0)
        return;

    // Project sun to screen UV
    Float4 sunDir4 = m_frameData.lightDirections[0];
    Float4 camPos4 = m_frameData.cameraPos;
    Float4 sunWorldPos = Float4Sub(camPos4, Float4Scale(sunDir4, 5000.0f));
    sunWorldPos.w = 1.0f;
    Float4 sunClip = Float4Transform(sunWorldPos, m_frameData.viewProjection);
    float w = sunClip.w;

    float sunU = 0.5f, sunV = 0.5f;
    float onScreen = 0.0f;
    if (w > 0.1f)
    {
        sunU = sunClip.x / w * 0.5f + 0.5f;
        sunV = -sunClip.y / w * 0.5f + 0.5f;
        // Fade as sun approaches screen edge
        float edgeDist = 1.0f - 2.0f * fmaxf(fabsf(sunU - 0.5f), fabsf(sunV - 0.5f));
        onScreen = (edgeDist > 0.0f) ? fminf(edgeDist * 3.0f, 1.0f) : 0.0f;
    }

    if (onScreen < 0.01f)
        return;

    Texture* inputRT;
    if (m_inPostChain) {
        EnsurePostChainInput();
        inputRT = &GetPPReadRT();
    } else {
        CopyBackbufferToSceneRT();
        inputRT = &m_sceneRT;
    }

    LensFlareConstants lf = {};
    lf.sunScreenUV = { sunU, sunV };
    lf.sunOnScreen = onScreen;
    lf.intensity = 0.4f;
    lf.sunColor = m_frameData.lightColors[0];
    lf.texelSize = { 1.0f / (float)sceneW, 1.0f / (float)sceneH };

    m_cbLensFlare.Update(m_device, &lf, sizeof(lf));
    m_cbLensFlare.BindPS(m_device, 0);
    m_cbLensFlare.BindVS(m_device, 0);

    if (m_inPostChain) {
        SetRenderTarget(GetPPWriteRT(), sceneW, sceneH);
    } else {
        RestoreBackBuffer();
        m_rasterNoCull.Bind(m_device);
        m_blendOpaque.Bind(m_device);
        m_depthDisabled.Bind(m_device);
        m_samplerLinearClamp.BindPS(m_device, 0);
    }
    m_shaderLensFlare.Bind(m_device);
    inputRT->BindPS(m_device, 0);
    m_fullscreenQuadVB.Bind(m_device);
    m_device.SetTopology(Topology::TriangleStrip);
    m_device.Draw(4, 0);

    m_device.UnbindPSSRVs(0);
    if (m_inPostChain) SwapPP();
}

void Renderer::ApplyFilmGrain()
{
    if (!m_filmGrainEnabled)
        return;

    int sceneW = m_device.GetWidth();
    int sceneH = m_device.GetHeight();
    if (sceneW <= 0 || sceneH <= 0)
        return;

    Texture* inputRT;
    if (m_inPostChain) {
        EnsurePostChainInput();
        inputRT = &GetPPReadRT();
    } else {
        CopyBackbufferToSceneRT();
        inputRT = &m_sceneRT;
    }

    FilmGrainConstants gc = {};
    gc.texelSize = { 1.0f / (float)sceneW, 1.0f / (float)sceneH };
    gc.grainIntensity = 0.055f;
    gc.time = m_frameData.lightingOptions.y * 0.001f;
    m_cbFilmGrain.Update(m_device, &gc, sizeof(gc));
    m_cbFilmGrain.BindPS(m_device, 0);
    m_cbFilmGrain.BindVS(m_device, 0);

    if (m_inPostChain) {
        SetRenderTarget(GetPPWriteRT(), sceneW, sceneH);
    } else {
        RestoreBackBuffer();
        m_rasterNoCull.Bind(m_device);
        m_blendOpaque.Bind(m_device);
        m_depthDisabled.Bind(m_device);
        m_samplerLinearClamp.BindPS(m_device, 0);
    }
    m_shaderFilmGrain.Bind(m_device);
    inputRT->BindPS(m_device, 0);
    m_fullscreenQuadVB.Bind(m_device);
    m_device.SetTopology(Topology::TriangleStrip);
    m_device.Draw(4, 0);

    m_device.UnbindPSSRVs(0);
    if (m_inPostChain) SwapPP();
}

void Renderer::ApplySharpen()
{
    if (!m_sharpenEnabled)
        return;

    int sceneW = m_device.GetWidth();
    int sceneH = m_device.GetHeight();
    if (sceneW <= 0 || sceneH <= 0)
        return;

    Texture* inputRT;
    if (m_inPostChain) {
        EnsurePostChainInput();
        inputRT = &GetPPReadRT();
    } else {
        CopyBackbufferToSceneRT();
        inputRT = &m_sceneRT;
    }

    SharpenConstants sc = {};
    sc.texelSize = { 1.0f / (float)sceneW, 1.0f / (float)sceneH };
    sc.sharpenAmount = 0.6f;
    m_cbSharpen.Update(m_device, &sc, sizeof(sc));
    m_cbSharpen.BindPS(m_device, 0);
    m_cbSharpen.BindVS(m_device, 0);

    if (m_inPostChain) {
        SetRenderTarget(GetPPWriteRT(), sceneW, sceneH);
    } else {
        RestoreBackBuffer();
        m_rasterNoCull.Bind(m_device);
        m_blendOpaque.Bind(m_device);
        m_depthDisabled.Bind(m_device);
        m_samplerLinearClamp.BindPS(m_device, 0);
    }
    m_samplerPoint.BindPS(m_device, 1);
    m_shaderSharpen.Bind(m_device);
    inputRT->BindPS(m_device, 0);
    m_fullscreenQuadVB.Bind(m_device);
    m_device.SetTopology(Topology::TriangleStrip);
    m_device.Draw(4, 0);

    m_device.UnbindPSSRVs(0);
    if (m_inPostChain) SwapPP();
}

void Renderer::ApplyTiltShift()
{
    if (!m_tiltShiftEnabled)
        return;

    int sceneW = m_device.GetWidth();
    int sceneH = m_device.GetHeight();
    if (sceneW <= 0 || sceneH <= 0)
        return;

    Texture* inputRT;
    if (m_inPostChain) {
        EnsurePostChainInput();
        inputRT = &GetPPReadRT();
    } else {
        CopyBackbufferToSceneRT();
        inputRT = &m_sceneRT;
    }

    TiltShiftConstants tc = {};
    tc.texelSize = { 1.0f / (float)sceneW, 1.0f / (float)sceneH };
    tc.focusCenter = 0.45f;
    tc.focusWidth = 0.15f;
    tc.blurStrength = 3.5f;
    m_cbTiltShift.Update(m_device, &tc, sizeof(tc));
    m_cbTiltShift.BindPS(m_device, 0);
    m_cbTiltShift.BindVS(m_device, 0);

    if (m_inPostChain) {
        SetRenderTarget(GetPPWriteRT(), sceneW, sceneH);
    } else {
        RestoreBackBuffer();
        m_rasterNoCull.Bind(m_device);
        m_blendOpaque.Bind(m_device);
        m_depthDisabled.Bind(m_device);
        m_samplerLinearClamp.BindPS(m_device, 0);
    }
    m_shaderTiltShift.Bind(m_device);
    inputRT->BindPS(m_device, 0);
    m_fullscreenQuadVB.Bind(m_device);
    m_device.SetTopology(Topology::TriangleStrip);
    m_device.Draw(4, 0);

    m_device.UnbindPSSRVs(0);
    if (m_inPostChain) SwapPP();
}

void Renderer::CapturePreParticleScene()
{
    if (!m_particleGlowEnabled && !m_heatDistortEnabled)
        return;

    int sceneW = m_device.GetWidth();
    int sceneH = m_device.GetHeight();
    if (sceneW <= 0 || sceneH <= 0)
        return;

    // Create/resize pre-particle RT on first use or resolution change
    if (!m_particleFxReady || m_particleFxWidth != sceneW || m_particleFxHeight != sceneH)
    {
        int halfW = ((sceneW / 2) > 1 ? (sceneW / 2) : 1);
        int halfH = ((sceneH / 2) > 1 ? (sceneH / 2) : 1);
        m_preParticleRT.CreateRenderTarget(m_device, sceneW, sceneH);
        m_particleExtractRT.CreateRenderTarget(m_device, halfW, halfH);
        m_particleBlurRT.CreateRenderTarget(m_device, halfW, halfH);
        m_particleFxWidth = sceneW;
        m_particleFxHeight = sceneH;
        m_particleFxReady = true;
    }

    if (!m_preParticleRT.IsValid())
        return;

    // Copy current backbuffer to preParticleRT
    m_device.CopyBackBufferToTexture(m_preParticleRT);
}

void Renderer::ApplyParticleFX()
{
    if (!m_particleGlowEnabled && !m_heatDistortEnabled)
        return;

    if (!m_particleFxReady || !m_preParticleRT.IsValid())
        return;

    int sceneW = m_device.GetWidth();
    int sceneH = m_device.GetHeight();
    if (sceneW <= 0 || sceneH <= 0)
        return;

    int halfW = ((sceneW / 2) > 1 ? (sceneW / 2) : 1);
    int halfH = ((sceneH / 2) > 1 ? (sceneH / 2) : 1);

    if (!m_particleExtractRT.IsValid() || !m_particleBlurRT.IsValid())
        return;

    // Ensure m_sceneRT exists at current resolution (shared with bloom).
    // CreateRenderTarget is a no-op if the RT already matches.
    m_sceneRT.CreateRenderTarget(m_device, sceneW, sceneH);

    if (!m_sceneRT.IsValid())
        return;

    // Copy backbuffer → sceneRT (post-particle scene for reading)
    m_device.CopyBackBufferToTexture(m_sceneRT);

    // Set up fullscreen quad rendering state
    m_rasterNoCull.Bind(m_device);
    m_blendOpaque.Bind(m_device);
    m_depthDisabled.Bind(m_device);
    m_samplerLinearClamp.BindPS(m_device, 0);

    // --- Pass 1: Extract particle contribution at half-res ---
    // particleExtractRT = max(0, sceneRT - preParticleRT)
    {
        ParticleFXConstants pc = {};
        pc.texelSize = { 1.0f / (float)sceneW, 1.0f / (float)sceneH };
        m_cbParticleFX.Update(m_device, &pc, sizeof(pc));
        m_cbParticleFX.BindPS(m_device, 0);
        m_cbParticleFX.BindVS(m_device, 0);

        SetRenderTarget(m_particleExtractRT, halfW, halfH);
        m_shaderParticleExtract.Bind(m_device);

        const Texture* textures[2] = { &m_sceneRT, &m_preParticleRT };
        m_device.BindPSTextures(0, textures, 2);

        m_fullscreenQuadVB.Bind(m_device);
        m_device.SetTopology(Topology::TriangleStrip);
        m_device.Draw(4, 0);
    }

    // --- Pass 2+3: Blur particle extract for glow (if glow or both enabled) ---
    // Reuses the bloom blur shader + cbPost, so glow requires bloom shaders to be compiled
    bool doGlow = m_particleGlowEnabled && m_bloomEnabled;
    if (doGlow)
    {
        // Horizontal blur
        {
            PostConstants pc = {};
            pc.texelSize = { 1.0f / (float)halfW, 0.0f };
            pc.bloomThreshold = 0.0f;
            pc.bloomIntensity = 1.0f;
            m_cbPost.Update(m_device, &pc, sizeof(pc));
            m_cbPost.BindPS(m_device, 0);
            m_cbPost.BindVS(m_device, 0);

            SetRenderTarget(m_particleBlurRT, halfW, halfH);
            m_shaderBlur.Bind(m_device);
            m_particleExtractRT.BindPS(m_device, 0);
            m_fullscreenQuadVB.Bind(m_device);
            m_device.SetTopology(Topology::TriangleStrip);
            m_device.Draw(4, 0);
        }

        // Vertical blur
        {
            PostConstants pc = {};
            pc.texelSize = { 0.0f, 1.0f / (float)halfH };
            pc.bloomThreshold = 0.0f;
            pc.bloomIntensity = 1.0f;
            m_cbPost.Update(m_device, &pc, sizeof(pc));

            SetRenderTarget(m_particleExtractRT, halfW, halfH);
            m_shaderBlur.Bind(m_device);
            m_particleBlurRT.BindPS(m_device, 0);
            m_fullscreenQuadVB.Bind(m_device);
            m_device.SetTopology(Topology::TriangleStrip);
            m_device.Draw(4, 0);
        }
        // particleExtractRT now holds blurred particle glow
    }

    // --- Final pass: Composite to backbuffer ---
    RestoreBackBuffer();

    float timeMs = m_frameData.lightingOptions.y;

    if (m_heatDistortEnabled)
    {
        // Heat distortion + optional glow composite in one pass
        ParticleFXConstants pc = {};
        pc.texelSize = { 1.0f / (float)sceneW, 1.0f / (float)sceneH };
        pc.distortionStrength = 0.015f;
        pc.glowIntensity = doGlow ? 0.25f : 0.0f;
        pc.time = timeMs * 0.001f;
        pc.colorAwareFx = m_colorAwareFxEnabled ? 1.0f : 0.0f;
        m_cbParticleFX.Update(m_device, &pc, sizeof(pc));
        m_cbParticleFX.BindPS(m_device, 0);
        m_cbParticleFX.BindVS(m_device, 0);

        m_shaderHeatDistort.Bind(m_device);

        const Texture* textures[2] = { &m_sceneRT, &m_particleExtractRT };
        m_device.BindPSTextures(0, textures, 2);

        m_fullscreenQuadVB.Bind(m_device);
        m_device.SetTopology(Topology::TriangleStrip);
        m_device.Draw(4, 0);
    }
    else if (doGlow)
    {
        // Glow-only composite
        ParticleFXConstants pc = {};
        pc.texelSize = { 1.0f / (float)sceneW, 1.0f / (float)sceneH };
        pc.distortionStrength = 0.0f;
        pc.glowIntensity = 0.25f;
        pc.time = 0.0f;
        pc.colorAwareFx = m_colorAwareFxEnabled ? 1.0f : 0.0f;
        m_cbParticleFX.Update(m_device, &pc, sizeof(pc));
        m_cbParticleFX.BindPS(m_device, 0);
        m_cbParticleFX.BindVS(m_device, 0);

        m_shaderGlowComposite.Bind(m_device);

        const Texture* textures[2] = { &m_sceneRT, &m_particleExtractRT };
        m_device.BindPSTextures(0, textures, 2);

        m_fullscreenQuadVB.Bind(m_device);
        m_device.SetTopology(Topology::TriangleStrip);
        m_device.Draw(4, 0);
    }

    // Unbind SRVs
    m_device.UnbindPSSRVs(0, 2);
}

void Renderer::ApplyPostProcessing()
{
    if (!m_bloomEnabled)
        return;

    int sceneW = m_device.GetWidth();
    int sceneH = m_device.GetHeight();
    if (sceneW <= 0 || sceneH <= 0)
        return;

    int halfW = sceneW / 2;
    int halfH = sceneH / 2;
    if (halfW < 1) halfW = 1;
    if (halfH < 1) halfH = 1;

    m_bloomExtractRT.CreateRenderTarget(m_device, halfW, halfH);
    m_bloomBlurRT.CreateRenderTarget(m_device, halfW, halfH);
    if (!m_bloomExtractRT.IsValid() || !m_bloomBlurRT.IsValid())
        return;

    // Get input texture
    Texture* inputRT;
    if (m_inPostChain) {
        EnsurePostChainInput();
        inputRT = &GetPPReadRT();
    } else {
        CopyBackbufferToSceneRT();
        inputRT = &m_sceneRT;
    }

    if (!m_inPostChain) {
        m_rasterNoCull.Bind(m_device);
        m_blendOpaque.Bind(m_device);
        m_depthDisabled.Bind(m_device);
    }
    m_samplerLinear.BindPS(m_device, 0);

    // Pass 1: Extract bright pixels to half-res RT
    {
        PostConstants pc = {};
        pc.texelSize = { 1.0f / (float)sceneW, 1.0f / (float)sceneH };
        pc.bloomThreshold = 0.50f;
        pc.bloomIntensity = 0.55f;
        m_cbPost.Update(m_device, &pc, sizeof(pc));
        m_cbPost.BindPS(m_device, 0);
        m_cbPost.BindVS(m_device, 0);

        SetRenderTarget(m_bloomExtractRT, halfW, halfH);
        m_shaderBloomExtract.Bind(m_device);
        inputRT->BindPS(m_device, 0);
        m_fullscreenQuadVB.Bind(m_device);
        m_device.SetTopology(Topology::TriangleStrip);
        m_device.Draw(4, 0);
    }

    // Pass 2: Horizontal blur
    {
        PostConstants pc = {};
        pc.texelSize = { 1.0f / (float)halfW, 0.0f };
        pc.bloomThreshold = 0.50f;
        pc.bloomIntensity = 0.55f;
        m_cbPost.Update(m_device, &pc, sizeof(pc));

        SetRenderTarget(m_bloomBlurRT, halfW, halfH);
        m_shaderBlur.Bind(m_device);
        m_bloomExtractRT.BindPS(m_device, 0);
        m_fullscreenQuadVB.Bind(m_device);
        m_device.SetTopology(Topology::TriangleStrip);
        m_device.Draw(4, 0);
    }

    // Pass 3: Vertical blur (back into extract RT)
    {
        PostConstants pc = {};
        pc.texelSize = { 0.0f, 1.0f / (float)halfH };
        pc.bloomThreshold = 0.50f;
        pc.bloomIntensity = 0.55f;
        m_cbPost.Update(m_device, &pc, sizeof(pc));

        SetRenderTarget(m_bloomExtractRT, halfW, halfH);
        m_shaderBlur.Bind(m_device);
        m_bloomBlurRT.BindPS(m_device, 0);
        m_fullscreenQuadVB.Bind(m_device);
        m_device.SetTopology(Topology::TriangleStrip);
        m_device.Draw(4, 0);
    }

    // Pass 4: Composite scene + bloom
    {
        PostConstants pc = {};
        pc.texelSize = { 1.0f / (float)sceneW, 1.0f / (float)sceneH };
        pc.bloomThreshold = 0.50f;
        // Final composite strength: the only pass where bloomIntensity is read
        // (PSComposite: result = scene + bloom * bloomIntensity). The earlier
        // extract/blur passes ignore it. 0.25 keeps the halo visible on bright
        // highlights without washing the HUD / lit skins the way 0.55 did.
        pc.bloomIntensity = 0.25f;
        m_cbPost.Update(m_device, &pc, sizeof(pc));

        if (m_inPostChain) {
            SetRenderTarget(GetPPWriteRT(), sceneW, sceneH);
        } else {
            RestoreBackBuffer();
        }
        m_shaderComposite.Bind(m_device);

        const Texture* textures[2] = { inputRT, &m_bloomExtractRT };
        m_device.BindPSTextures(0, textures, 2);

        m_fullscreenQuadVB.Bind(m_device);
        m_device.SetTopology(Topology::TriangleStrip);
        m_device.Draw(4, 0);

        m_device.UnbindPSSRVs(0, 2);
    }
    if (m_inPostChain) SwapPP();
}

} // namespace Render
