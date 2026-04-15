#include "W3DDevice/GameClient/ModelRenderer.h"
#include "W3DDevice/GameClient/TerrainRenderer.h" // for FrustumPlanes, ExtractFrustumFromCamera

#include "Common/GlobalData.h"
#include "W3DDevice/GameClient/ImageCache.h"
#include "WW3D2/camera.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/meshgeometry.h"
#include "WW3D2/hlod.h"
#include "WW3D2/texture.h"
#include <d3d8.h> // for IDirect3DTexture8/Surface8 pixel access in ZHC recoloring
#include "WW3D2/vertmaterial.h"
#include "WW3D2/shader.h"
#include "WW3D2/distlod.h"
#include "WW3D2/part_emt.h"
#include "WW3D2/part_buf.h"
#include "WW3D2/rendobj.h"
#include "WWMath/matrix3d.h"
#include "WWMath/matrix4.h"
#include "RenderUtils.h"
#include "WWMath/sphere.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Implemented in D3D11Shims.cpp. Walks the live drawable list and pushes
// each owner's profile shader id into the corresponding RenderObjClass so
// newly-spawned units pick up the effect within one frame. Declared at
// global scope — extern "C" linkage specs are illegal inside a function.
extern "C" int RefreshAllPlayerShaderIds();

namespace
{

static Render::ModelRenderer::MeshBlendMode ClassifyShaderBlendMode(const ShaderClass& shader)
{
    auto srcBlend = shader.Get_Src_Blend_Func();
    auto dstBlend = shader.Get_Dst_Blend_Func();
    auto alphaTest = shader.Get_Alpha_Test();

    // Additive: src=ONE, dst=ONE (headlights, glows, muzzle flashes)
    if (srcBlend == ShaderClass::SRCBLEND_ONE && dstBlend == ShaderClass::DSTBLEND_ONE)
        return Render::ModelRenderer::BLEND_ADDITIVE;

    // Additive with alpha: src=SRC_ALPHA, dst=ONE
    if (srcBlend == ShaderClass::SRCBLEND_SRC_ALPHA && dstBlend == ShaderClass::DSTBLEND_ONE)
        return Render::ModelRenderer::BLEND_ADDITIVE_ALPHA;

    // Alpha blend: src=SRC_ALPHA, dst=ONE_MINUS_SRC_ALPHA
    if (srcBlend == ShaderClass::SRCBLEND_SRC_ALPHA &&
        dstBlend == ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA)
    {
        // If also alpha testing, treat as alpha test (binary transparency)
        if (alphaTest == ShaderClass::ALPHATEST_ENABLE)
            return Render::ModelRenderer::BLEND_ALPHA_TEST;
        return Render::ModelRenderer::BLEND_ALPHA;
    }

    // Screen blend: src=ONE, dst=ONE_MINUS_SRC_COLOR
    if (srcBlend == ShaderClass::SRCBLEND_ONE &&
        dstBlend == ShaderClass::DSTBLEND_ONE_MINUS_SRC_COLOR)
        return Render::ModelRenderer::BLEND_ADDITIVE;

    // Alpha test with opaque blend
    if (alphaTest == ShaderClass::ALPHATEST_ENABLE)
        return Render::ModelRenderer::BLEND_ALPHA_TEST;

    // Non-zero destination blend means the mesh reads from the framebuffer and needs
    // transparency.  The original WW3D engine (meshmdlio.cpp) uses dstBlend != ZERO
    // as the trigger for sorted/transparent rendering.  Treat unrecognised blend
    // combinations (e.g. multiplicative ZERO/SRC_COLOR) as alpha-blended so they are
    // deferred and rendered back-to-front instead of showing as opaque black.
    if (dstBlend != ShaderClass::DSTBLEND_ZERO)
        return Render::ModelRenderer::BLEND_ALPHA;

    // Default: truly opaque (dstBlend == ZERO, no alpha test)
    return Render::ModelRenderer::BLEND_OPAQUE;
}

static uint32_t argbToAbgr(uint32_t argb)
{
    const uint32_t a = (argb >> 24) & 0xFF;
    const uint32_t r = (argb >> 16) & 0xFF;
    const uint32_t g = (argb >> 8) & 0xFF;
    const uint32_t b = argb & 0xFF;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

static void pushUnique(std::vector<std::string>& candidates, const std::string& value)
{
    if (!value.empty() && std::find(candidates.begin(), candidates.end(), value) == candidates.end())
        candidates.push_back(value);
}

static std::string toLowerStr(const std::string& s)
{
    std::string result = s;
    for (char& c : result)
        c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return result;
}

static std::string stripDirectoryPrefix(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos)
        return path.substr(slash + 1);
    return path;
}

static std::string replaceExtension(const std::string& path, const char* newExt)
{
    size_t slash = path.find_last_of("/\\");
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        return path.substr(0, dot) + newExt;
    return path + newExt;
}

static void appendTextureCandidates(std::vector<std::string>& candidates, const char* value)
{
    if (!value || !*value)
        return;

    const std::string candidate(value);
    pushUnique(candidates, candidate);

    const size_t slash = candidate.find_last_of("/\\");
    const size_t dot = candidate.find_last_of('.');
    const bool hasExtension = dot != std::string::npos && (slash == std::string::npos || dot > slash);

    // Try both extensions regardless
    if (hasExtension)
    {
        pushUnique(candidates, replaceExtension(candidate, ".dds"));
        pushUnique(candidates, replaceExtension(candidate, ".tga"));
    }
    else
    {
        pushUnique(candidates, candidate + ".dds");
        pushUnique(candidates, candidate + ".tga");
    }

    // Strip directory prefixes (e.g. "Art\Textures\foo.tga" -> "foo.tga")
    std::string basename = stripDirectoryPrefix(candidate);
    if (basename != candidate)
    {
        pushUnique(candidates, basename);
        if (hasExtension)
        {
            pushUnique(candidates, replaceExtension(basename, ".dds"));
            pushUnique(candidates, replaceExtension(basename, ".tga"));
        }
        else
        {
            pushUnique(candidates, basename + ".dds");
            pushUnique(candidates, basename + ".tga");
        }
    }

    // Try with "Art/Textures/" prefix
    std::string withPrefix = "Art/Textures/" + basename;
    pushUnique(candidates, withPrefix);
    if (hasExtension)
    {
        pushUnique(candidates, replaceExtension(withPrefix, ".dds"));
        pushUnique(candidates, replaceExtension(withPrefix, ".tga"));
    }
    else
    {
        pushUnique(candidates, withPrefix + ".dds");
        pushUnique(candidates, withPrefix + ".tga");
    }

    // Try lowercase variants of the basename
    std::string lowerBasename = toLowerStr(basename);
    if (lowerBasename != basename)
    {
        pushUnique(candidates, lowerBasename);
        if (hasExtension)
        {
            pushUnique(candidates, replaceExtension(lowerBasename, ".dds"));
            pushUnique(candidates, replaceExtension(lowerBasename, ".tga"));
        }
        else
        {
            pushUnique(candidates, lowerBasename + ".dds");
            pushUnique(candidates, lowerBasename + ".tga");
        }
    }

    // If the name has no extension, also try bare basename without extension variants
    // already covered above, but try uppercase first letter (common naming convention)
    if (!hasExtension && !basename.empty())
    {
        std::string upperFirst = basename;
        upperFirst[0] = static_cast<char>(::toupper(static_cast<unsigned char>(upperFirst[0])));
        if (upperFirst != basename)
        {
            pushUnique(candidates, upperFirst + ".dds");
            pushUnique(candidates, upperFirst + ".tga");
        }
    }
}

static void AppendModelTrace(const char* format, ...)
{
    return; // Debug logging removed
}

static bool IsFiniteMatrix(const Render::Float4x4& matrix)
{
    const float* values = reinterpret_cast<const float*>(&matrix);
    for (int i = 0; i < 16; ++i)
    {
        if (!std::isfinite(values[i]))
            return false;
    }
    return true;
}

static const Render::Float3 kFallbackLightDirection = { -0.3f, -0.2f, -0.8f };
static const Render::Float4 kFallbackLightColor = { 1.0f, 0.95f, 0.85f, 1.0f };
static const Render::Float4 kFallbackAmbientColor = { 0.35f, 0.35f, 0.40f, 1.0f };

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

static void ApplyObjectLighting(Render::Renderer& renderer)
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
    const GlobalData::TerrainLighting* lights = TheGlobalData->m_terrainObjectsLighting[timeOfDay];

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

} // namespace

namespace Render
{

ModelRenderer& ModelRenderer::Instance()
{
    static ModelRenderer s_instance;
    return s_instance;
}

ModelRenderer::StaticMeshCacheEntry::StaticMeshCacheEntry() = default;

ModelRenderer::StaticMeshCacheEntry::~StaticMeshCacheEntry()
{
    if (model != nullptr)
    {
        model->Release_Ref();
        model = nullptr;
    }
}

ModelRenderer::StaticMeshCacheEntry::StaticMeshCacheEntry(StaticMeshCacheEntry&& that) noexcept :
    model(that.model),
    batches(std::move(that.batches))
{
    that.model = nullptr;
}

ModelRenderer::StaticMeshCacheEntry& ModelRenderer::StaticMeshCacheEntry::operator=(StaticMeshCacheEntry&& that) noexcept
{
    if (this != &that)
    {
        if (model != nullptr)
            model->Release_Ref();

        model = that.model;
        batches = std::move(that.batches);
        that.model = nullptr;
    }

    return *this;
}

void ModelRenderer::BeginFrame(CameraClass* camera)
{
    m_camera = camera;
    m_frustumValid = false;
    m_currentObjectColor = 0;
    m_currentObjectShaderId = 0;

    // Refresh per-render-object shader effect ids so newly-spawned units
    // pick up their owner's profile shader within one frame. The function
    // is a no-op for objects whose Get_ObjectShaderId() already matches.
    RefreshAllPlayerShaderIds();
    m_fogDarkening = 1.0f;
    m_tintR = 0; m_tintG = 0; m_tintB = 0;
    m_ghostMode = false;
    m_ghostTintR = m_ghostTintG = m_ghostTintB = 0.0f;
    m_ghostTintIntensity = 0.0f;
    m_ghostOpacity = 1.0f;
    m_currentBlendMode = BLEND_OPAQUE;
    m_deferredTranslucent.clear();
    m_pendingParticleBuffers.clear();
    if (!m_camera)
        return;

    // Cache camera position for distance sorting
    const Vector3 cp = m_camera->Get_Position();
    m_frameData_camX = cp.X; m_frameData_camY = cp.Y; m_frameData_camZ = cp.Z;

    auto& renderer = Renderer::Instance();

    Matrix3D viewMatrix3D;
    m_camera->Get_View_Matrix(&viewMatrix3D);
    RenderUtils::SanitizeMatrix3D(viewMatrix3D);

    Matrix4x4 projMatrix4x4;
    m_camera->Get_D3D_Projection_Matrix(&projMatrix4x4);

    Render::Float4x4 viewMatrix = RenderUtils::Matrix3DToFloat4x4(viewMatrix3D);
    Render::Float4x4 projMatrix = RenderUtils::Matrix4x4ToFloat4x4(projMatrix4x4);

    const Vector3 camPos = m_camera->Get_Position();
    const Render::Float3 cameraPos = { camPos.X, camPos.Y, camPos.Z };

    renderer.Restore3DState();
    renderer.SetCamera(viewMatrix, projMatrix, cameraPos);
    ApplyObjectLighting(renderer);
    renderer.FlushFrameConstants();

    // Apply the camera's viewport as a D3D11 viewport.
    // The W3D camera stores viewport bounds as normalized (0-1) coordinates.
    // The original D3D8 CameraClass::Apply() converted these to pixel coords.
    {
        Vector2 vpMin, vpMax;
        m_camera->Get_Viewport(vpMin, vpMax);
        float zMin = 0.0f, zMax = 1.0f;
        m_camera->Get_Depth_Range(&zMin, &zMax);

        float rtW = (float)renderer.GetWidth();
        float rtH = (float)renderer.GetHeight();

        float vpX = vpMin.X * rtW;
        float vpY = vpMin.Y * rtH;
        float vpW = (vpMax.X - vpMin.X) * rtW;
        float vpH = (vpMax.Y - vpMin.Y) * rtH;

        renderer.SetViewport(vpX, vpY, vpW, vpH, zMin, zMax);

        // viewport_trace.log writing removed
    }

    ExtractFrustumFromCamera(m_camera, m_frustum);
    m_frustumValid = true;
}

void ModelRenderer::RenderRenderObject(RenderObjClass* renderObject, bool isSubObject)
{
    static int s_loggedRenderObjects = 0;

    if (!m_camera || !renderObject)
        return;

    // Frustum cull using the render object's bounding sphere. Skip in
    // shadow caster mode — the camera frustum doesn't match the light
    // frustum, so we'd cull casters that ARE in the light's view but
    // outside the camera's. The shadow map's tight-fit ortho already
    // limits the relevant set, and the GPU clip will discard out-of-
    // bounds verts anyway.
    extern bool g_debugDisableFrustumCull;
    if (m_frustumValid && !g_debugDisableFrustumCull && !m_shadowCasterMode)
    {
        const SphereClass& sphere = renderObject->Get_Bounding_Sphere();
        Render::Float3 center = { sphere.Center.X, sphere.Center.Y, sphere.Center.Z };
        if (!m_frustum.TestSphere(center, sphere.Radius))
            return;
    }

    if (s_loggedRenderObjects < 32)
    {
        AppendModelTrace(
            "ModelRenderer::RenderRenderObject classId=%d name=%s subObjects=%d\n",
            renderObject->Class_ID(),
            renderObject->Get_Name(),
            renderObject->Get_Num_Sub_Objects());
        ++s_loggedRenderObjects;
    }

    // Capture team/house color and cosmetic shader id from the render object.
    //   * Top-level objects: ALWAYS overwrite (even with 0). A neutral or
    //     civilian object has no team color / shader effect, and we must
    //     reset here so its meshes don't inherit the previous player-owned
    //     object's values from earlier in the frame.
    //   * Sub-objects: only overwrite when non-zero, so child meshes inherit
    //     the parent's values when their own per-mesh fields are unset.
    unsigned int objColor = renderObject->Get_ObjectColor();
    int          objShaderId = renderObject->Get_ObjectShaderId();
    if (!isSubObject)
    {
        m_currentObjectColor    = objColor;
        m_currentObjectShaderId = objShaderId;
    }
    else
    {
        if (objColor    != 0) m_currentObjectColor    = objColor;
        if (objShaderId != 0) m_currentObjectShaderId = objShaderId;
    }

    // Only compute LOD and update sub-object transforms for top-level objects.
    // Sub-objects (children of HLod/DistLod/etc.) already have their transforms
    // updated by the parent's Update_Sub_Object_Transforms() call.
    //
    // The shadow caster pass runs FIRST in W3DDisplay::draw (before the
    // regular drawables iteration), so we DO need to compute LOD/transforms
    // here if we're the first pass to touch this object this frame. Both
    // passes call this with the same camera, and the transforms are stable
    // for the duration of the frame, so doing it twice is harmless (the
    // second call is a no-op or tiny refresh).
    if (!isSubObject)
    {
        renderObject->Prepare_LOD(*m_camera);
        renderObject->Update_Sub_Object_Transforms();
    }

    switch (renderObject->Class_ID())
    {
    case RenderObjClass::CLASSID_MESH:
        RenderMesh(static_cast<MeshClass*>(renderObject));
        break;
    case RenderObjClass::CLASSID_HLOD:
        RenderHLod(static_cast<HLodClass*>(renderObject));
        break;
    case RenderObjClass::CLASSID_DISTLOD:
        RenderDistLod(static_cast<DistLODClass*>(renderObject));
        break;
    case RenderObjClass::CLASSID_PARTICLEEMITTER:
    {
        // In shadow caster mode, skip particle emitter side-effects entirely.
        // Spawning particles twice per frame would double the emission rate
        // and breaking the lifetime accounting; m_pendingParticleBuffers is
        // populated by the regular render pass.
        if (m_shadowCasterMode)
            break;
        // Particle emitters on models (e.g. missile exhaust) need their
        // On_Frame_Update called so they generate particles. We also
        // collect their ParticleBuffer for rendering in the scene pass.
        renderObject->On_Frame_Update();
        auto* emitter = static_cast<ParticleEmitterClass*>(renderObject);
        ParticleBufferClass* buf = emitter->Peek_Buffer();
        if (buf)
        {
            buf->On_Frame_Update();
            m_pendingParticleBuffers.push_back(buf);
        }
        break;
    }
    default:
        RenderGenericChildren(renderObject);
        break;
    }
}

void ModelRenderer::Shutdown()
{
    m_staticMeshes.clear();
    m_textureFromW3D.clear();
    m_deferredTranslucent.clear();
    m_camera = nullptr;
}

void ModelRenderer::FlushTranslucent()
{
    if (m_deferredTranslucent.empty())
        return;

    // Sort back-to-front (farthest first) for correct alpha blending
    std::sort(m_deferredTranslucent.begin(), m_deferredTranslucent.end(),
        [](const DeferredDraw& a, const DeferredDraw& b) { return a.distSq > b.distSq; });

    auto& renderer = Renderer::Instance();

    for (const DeferredDraw& dd : m_deferredTranslucent)
    {
        switch (dd.batch->blendMode)
        {
        case BLEND_ALPHA:
            renderer.SetAlphaBlend3DState();
            break;
        case BLEND_ADDITIVE:
        case BLEND_ADDITIVE_ALPHA:
            renderer.SetAdditive3DState();
            break;
        default:
            renderer.SetAlphaBlend3DState();
            break;
        }

        Texture* tex = dd.batch->texture ? dd.batch->texture : GetWhiteTexture();

        // ZHCA "house icon" batches on translucent materials (Command Center
        // emblem, HQ faction logo) need the dark-key + house-color treatment.
        // The mesh-path opaque loop doesn't touch them because they defer to
        // this queue; set the flag here and override the vertex color to the
        // owning object's house color so the PS branch has it in input.color.
        Render::Float4 drawColor = dd.color;
        if (dd.batch->isZhcaTexture && dd.objectColor != 0)
        {
            drawColor.x = ((dd.objectColor >> 16) & 0xFF) / 255.0f;
            drawColor.y = ((dd.objectColor >>  8) & 0xFF) / 255.0f;
            drawColor.z = ( dd.objectColor        & 0xFF) / 255.0f;
        }
        renderer.SetCurrentIsZhcaTexture(dd.batch->isZhcaTexture);
        // Rebind the cosmetic shader effect captured at queue time so the
        // icon animates with the player's selected variant (pulse / rainbow
        // / shimmer / etc.). The player-owned flag (y) is driven off the
        // shader id; the accent-mesh gate (z) is forced on for ZHCA icons so
        // the shader effect applies even though they're not HOUSECOLOR meshes.
        renderer.SetCurrentShaderEffect(dd.shaderId, dd.shaderId != 0);
        renderer.SetCurrentIsAccentMesh(dd.batch->isZhcaTexture);

        renderer.Draw3D(
            dd.batch->vertexBuffer,
            dd.batch->indexBuffer,
            tex,
            dd.worldMatrix,
            drawColor);
    }
    renderer.SetCurrentIsZhcaTexture(false);
    renderer.SetCurrentShaderEffect(0, false);
    renderer.SetCurrentIsAccentMesh(false);

    m_deferredTranslucent.clear();
    renderer.Restore3DState();
}

void ModelRenderer::RenderMesh(MeshClass* mesh)
{
    static int s_loggedMeshes = 0;

    if (!mesh)
        return;

    // Skip collision/bounding box meshes — these are used for picking and selection
    // only, not for visual rendering. They have names like "BOX", "BOUNDING", etc.
    // The original renderer skipped these via the W3D mesh visibility system.
    if (mesh->Is_Hidden())
        return;
    const char* meshName = mesh->Get_Name();
    if (meshName)
    {
        // Check for collision box meshes (e.g., "UIWRKR_SKN.BOX", "MODELNAME.BOUNDINGBOX")
        const char* dot = strrchr(meshName, '.');
        const char* suffix = dot ? dot + 1 : meshName;
        if (_stricmp(suffix, "BOX") == 0 ||
            _stricmp(suffix, "BOUNDINGBOX") == 0 ||
            _stricmp(suffix, "COLLISION") == 0 ||
            _stricmp(suffix, "PICK") == 0)
            return;
    }

    MeshModelClass* model = mesh->Peek_Model();
    if (!model)
        return;

    // Check if this mesh uses vertex skinning (bone-weighted deformation).
    // Skinned meshes store vertices in bind pose — they MUST be deformed by the
    // skeleton each frame or they render as tiny shapes at the origin.
    const bool isSkinned = model->Get_Flag(MeshGeometryClass::SKIN) != 0;

    auto cacheIt = m_staticMeshes.find(model);
    if (cacheIt == m_staticMeshes.end())
    {
        StaticMeshCacheEntry cacheEntry;
        cacheEntry.model = model;
        cacheEntry.model->Add_Ref();
        if (!BuildStaticMeshCache(mesh, cacheEntry))
            return;

        cacheIt = m_staticMeshes.emplace(model, std::move(cacheEntry)).first;
    }

    // For skinned meshes, deform vertices using the skeleton's bone transforms.
    // The deformed positions are in world space, so we use an identity world matrix.
    // Uses static buffers to avoid per-frame heap allocations.
    Render::Float4x4 worldMatrix;
    if (isSkinned && mesh->Get_Container() != nullptr)
    {
        const int vertexCount = model->Get_Vertex_Count();
        if (vertexCount > 0 && !cacheIt->second.batches.empty())
        {
            // Static scratch buffers - avoid heap allocation every frame.
            // Sized to handle the largest skinned mesh (typically <2000 verts).
            static std::vector<Vector3> defPos;
            static std::vector<Vector3> defNorm;
            static std::vector<Vertex3D> verts;

            defPos.resize(vertexCount);
            defNorm.resize(vertexCount);

            // Initialize to bind pose in case deformation partially fails
            const Vector3* bindPos = model->Get_Vertex_Array();
            const Vector3* bindNorm = model->Get_Vertex_Normal_Array();
            if (bindPos) memcpy(defPos.data(), bindPos, vertexCount * sizeof(Vector3));
            if (bindNorm) memcpy(defNorm.data(), bindNorm, vertexCount * sizeof(Vector3));

            mesh->Get_Deformed_Vertices(defPos.data(), defNorm.data());

            // Rebuild vertex data for each pass/batch
            const int passCount = std::max(1, model->Get_Pass_Count());
            auto& device = Renderer::Instance().GetDevice();
            for (int pass = 0; pass < passCount; ++pass)
            {
                const Vector2* texcoords = model->Get_UV_Array(pass, 0);
                const unsigned* diffuseColors = model->Get_DCG_Array(pass);
                const bool useVtxColors = diffuseColors &&
                    model->Get_DCG_Source(pass) != VertexMaterialClass::MATERIAL;

                verts.resize(vertexCount);
                for (int i = 0; i < vertexCount; ++i)
                {
                    verts[i].position = { defPos[i].X, defPos[i].Y, defPos[i].Z };
                    verts[i].normal = { defNorm[i].X, defNorm[i].Y, defNorm[i].Z };
                    verts[i].texcoord = texcoords
                        ? Render::Float2{ texcoords[i].X, texcoords[i].Y }
                        : Render::Float2{ 0, 0 };
                    verts[i].color = useVtxColors ? argbToAbgr(diffuseColors[i]) : 0xFFFFFFFF;
                }

                // Update the existing VB with deformed data. On first skin update,
                // recreate as dynamic buffer so Update() works on subsequent frames.
                for (StaticMeshBatch& batch : cacheIt->second.batches)
                {
                    if (batch.pass != pass) continue;
                    if (!batch.isDynamic)
                    {
                        VertexBuffer dynVB;
                        if (dynVB.Create(device, verts.data(), (uint32_t)verts.size(), sizeof(Vertex3D), true))
                        {
                            batch.vertexBuffer = std::move(dynVB);
                            batch.isDynamic = true;
                        }
                    }
                    else
                    {
                        batch.vertexBuffer.Update(device, verts.data(),
                            (uint32_t)(verts.size() * sizeof(Vertex3D)));
                    }
                    break;
                }
            }
        }
        // Deformed verts are in world space — use identity transform
        DirectX::XMStoreFloat4x4(&ToXM(worldMatrix), DirectX::XMMatrixIdentity());
    }
    else
    {
        worldMatrix = ToWorldMatrix(mesh->Get_Transform());
    }
    if (!IsFiniteMatrix(worldMatrix))
    {
        static int s_loggedInvalidMeshes = 0;
        if (s_loggedInvalidMeshes < 32)
        {
            AppendModelTrace(
                "ModelRenderer::RenderMesh skipping invalid matrix mesh=%s worldPos=(%.2f,%.2f,%.2f)\n",
                mesh->Get_Name(),
                worldMatrix._41,
                worldMatrix._42,
                worldMatrix._43);
            ++s_loggedInvalidMeshes;
        }
        return;
    }
    if (s_loggedMeshes < 24)
    {
        AppendModelTrace(
            "ModelRenderer::RenderMesh mesh=%s worldPos=(%.2f,%.2f,%.2f) row0=(%.2f,%.2f,%.2f,%.2f)\n",
            mesh->Get_Name(),
            worldMatrix._41,
            worldMatrix._42,
            worldMatrix._43,
            worldMatrix._11,
            worldMatrix._12,
            worldMatrix._13,
            worldMatrix._14);
        ++s_loggedMeshes;
    }

    auto& renderer = Renderer::Instance();

    // Construction-ghost path: bind the ghost shader + alpha-blend pipeline
    // once, draw every batch (opaque/alpha-test/alpha alike) through it, skip
    // decals (faction logos on a translucent preview read as noise). Bypasses
    // the normal opaque/alpha-test/translucent batching since the ghost is
    // always alpha-blended and never depth-writes.
    if (m_ghostMode)
    {
        renderer.SetGhost3DState();
        GhostConstants gc;
        gc.ghostTint   = { m_ghostTintR, m_ghostTintG, m_ghostTintB, m_ghostTintIntensity };
        gc.ghostParams = { m_ghostOpacity, 0.0f, 0.0f, 0.0f };
        renderer.UploadGhostConstants(gc);
        m_currentBlendMode = BLEND_ALPHA; // re-sync for the next non-ghost mesh

        for (const StaticMeshBatch& batch : cacheIt->second.batches)
        {
            const Render::Float4 color = ComputeMeshColor(mesh, batch.pass);
            Texture* effectiveTex = batch.texture ? batch.texture : GetWhiteTexture();
            renderer.Draw3D(
                batch.vertexBuffer,
                batch.indexBuffer,
                effectiveTex,
                worldMatrix,
                color);
        }
        return;
    }

    // Apply the cached cosmetic shader effect for this render object so
    // every Draw3D below picks it up via the ObjectConstants cbuf. The
    // isPlayerDrawable flag gates the PS effect — terrain/props/particles
    // never reach this code path so they won't be affected.
    renderer.SetCurrentShaderEffect(m_currentObjectShaderId, m_currentObjectShaderId != 0);

    // Detect HOUSECOLOR / accent meshes once for this mesh — the PS effect
    // is gated so only the team-colored accent trim animates, not the
    // entire building shell or the whole unit body.
    bool isAccentMesh = false;
    if (mesh)
    {
        const char* meshName = mesh->Get_Name();
        const char* dotPos = meshName ? strchr(meshName, '.') : nullptr;
        const char* subName = dotPos ? dotPos + 1 : meshName;
        isAccentMesh = subName && _strnicmp(subName, "HOUSECOLOR", 10) == 0;
    }
    renderer.SetCurrentIsAccentMesh(isAccentMesh);

    // Render opaque and alpha-test batches immediately.
    // Defer transparent batches for sorted rendering later via FlushTranslucent().
    for (const StaticMeshBatch& batch : cacheIt->second.batches)
    {
        bool isTransparent = (batch.blendMode == BLEND_ALPHA ||
                              batch.blendMode == BLEND_ADDITIVE ||
                              batch.blendMode == BLEND_ADDITIVE_ALPHA);

        if (isTransparent)
        {
            // In shadow caster mode, skip translucent batches entirely —
            // they don't cast solid shadows. (Original DX8 W3DVolumetric
            // shadow path also skipped them.)
            if (m_shadowCasterMode)
                continue;
            // Defer for back-to-front sorted rendering
            float dx = worldMatrix._41 - m_frameData_camX;
            float dy = worldMatrix._42 - m_frameData_camY;
            float dz = worldMatrix._43 - m_frameData_camZ;
            m_deferredTranslucent.push_back({
                &batch, worldMatrix, ComputeMeshColor(mesh, batch.pass),
                dx*dx + dy*dy + dz*dz,
                m_currentObjectColor,
                m_currentObjectShaderId
            });
            continue;
        }

        // Track blend state to avoid redundant D3D state changes.
        // Most meshes are opaque, so this skips Restore3DState for consecutive opaques.
        // Skip state changes entirely in shadow caster mode — the externally-
        // bound shadow depth shader and shadow render-target must stay put.
        if (!m_shadowCasterMode && batch.blendMode != m_currentBlendMode)
        {
            switch (batch.blendMode)
            {
            case BLEND_OPAQUE:
                renderer.Restore3DState();
                break;
            case BLEND_ALPHA_TEST:
                renderer.SetAlphaTest3DState();
                break;
            default:
                break;
            }
            m_currentBlendMode = batch.blendMode;
        }

        const Render::Float4 color = ComputeMeshColor(mesh, batch.pass);

        // Silhouette mode: force the white texture so the per-pixel result
        // is just the silhouette color (not modulated by the mesh's diffuse
        // texture). The shader does texColor * input.color * objectColor;
        // with white texture and white vertex color, the output is the
        // objectColor we just computed.
        Texture* effectiveTex = m_silhouetteMode
            ? GetWhiteTexture()
            : (batch.texture ? batch.texture : GetWhiteTexture());
        renderer.Draw3D(
            batch.vertexBuffer,
            batch.indexBuffer,
            effectiveTex,
            worldMatrix,
            color);
    }

    // Render projected mesh decals (faction logos) as a second pass.
    // Decal art is grayscale; the player color comes straight from the owning
    // render object's house color (m_currentObjectColor) at draw time. The
    // shader multiplies decal texture by this color. The cosmetic shader
    // effect stays bound through the decal pass so the player-color region
    // of the logo animates with the same effect as the mesh.
    const auto& decals = cacheIt->second.decals;
    if (!decals.empty())
    {
        unsigned int houseColorPacked = m_currentObjectColor;
        if (houseColorPacked == 0 && mesh)
            houseColorPacked = mesh->Get_ObjectColor();

        Render::Float4 houseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        if (houseColorPacked != 0)
        {
            houseColor.x = ((houseColorPacked >> 16) & 0xFF) / 255.0f;
            houseColor.y = ((houseColorPacked >>  8) & 0xFF) / 255.0f;
            houseColor.z = ( houseColorPacked        & 0xFF) / 255.0f;
        }

        renderer.SetMeshDecal3DState();
        m_currentBlendMode = BLEND_ALPHA; // SetMeshDecal3DState binds alpha blend

        for (const MeshDecalInfo& decal : decals)
        {
            if (!decal.decalTexture)
                continue;

            MeshDecalConstants dc;
            dc.decalProjection = decal.projMatrix;
            dc.decalParams = { decal.backfaceThreshold, decal.opacity, 0.0f, 0.0f };

            // Project the decal over every opaque batch of this mesh.
            for (const StaticMeshBatch& batch : cacheIt->second.batches)
            {
                bool isTransparent = (batch.blendMode == BLEND_ALPHA ||
                                      batch.blendMode == BLEND_ADDITIVE ||
                                      batch.blendMode == BLEND_ADDITIVE_ALPHA);
                if (isTransparent)
                    continue;

                renderer.DrawMeshDecal(
                    batch.vertexBuffer,
                    batch.indexBuffer,
                    decal.decalTexture,
                    dc,
                    worldMatrix,
                    houseColor);
            }
        }
    }

    // Reset the cosmetic shader effect and accent flag so the next render
    // object's mesh / terrain / particle draws don't inherit this mesh's.
    renderer.SetCurrentShaderEffect(0, false);
    renderer.SetCurrentIsAccentMesh(false);

    // Restore to default opaque state and sync the tracking variable so the
    // next mesh correctly detects a state change if it needs alpha test/blend.
    renderer.Restore3DState();
    m_currentBlendMode = BLEND_OPAQUE;
}

void ModelRenderer::RenderHLod(HLodClass* hlod)
{
    if (!hlod)
        return;

    // In WW3D, LOD 0 = lowest detail, LodCount-1 = highest detail.
    // Prepare_LOD should set CurLod based on camera distance, but if it
    // defaults to 0 we'd get box-shaped infantry. Use highest available LOD.
    int lodLevel = hlod->Get_LOD_Level();
    const int lodCount = hlod->Get_Lod_Count();
    if (lodLevel <= 0 && lodCount > 1)
        lodLevel = lodCount - 1; // force highest detail
    const int lodModelCount = hlod->Get_Lod_Model_Count(lodLevel);
    for (int i = 0; i < lodModelCount; ++i)
    {
        RenderObjClass* lodModel = hlod->Peek_Lod_Model(lodLevel, i);
        if (lodModel && !lodModel->Is_Hidden() && !lodModel->Is_Animation_Hidden())
            RenderRenderObject(lodModel, true);
    }

    const int additionalModelCount = hlod->Get_Additional_Model_Count();
    for (int i = 0; i < additionalModelCount; ++i)
    {
        RenderObjClass* additionalModel = hlod->Peek_Additional_Model(i);
        if (additionalModel && !additionalModel->Is_Hidden() && !additionalModel->Is_Animation_Hidden())
            RenderRenderObject(additionalModel, true);
    }
}

void ModelRenderer::RenderDistLod(DistLODClass* distLod)
{
    if (!distLod)
        return;

    RenderObjClass* highestLod = distLod->Get_Sub_Object(0);
    if (!highestLod)
        return;

    RenderRenderObject(highestLod, true);
    highestLod->Release_Ref();
}

void ModelRenderer::RenderGenericChildren(RenderObjClass* renderObject)
{
    const int subObjectCount = renderObject ? renderObject->Get_Num_Sub_Objects() : 0;
    for (int i = 0; i < subObjectCount; ++i)
    {
        RenderObjClass* child = renderObject->Get_Sub_Object(i);
        if (!child)
            continue;

        RenderRenderObject(child, true);
        child->Release_Ref();
    }
}

bool ModelRenderer::BuildStaticMeshCache(MeshClass* mesh, StaticMeshCacheEntry& cacheEntry)
{
    MeshModelClass* model = mesh ? mesh->Peek_Model() : nullptr;
    if (!model)
        return false;

    const int vertexCount = model->Get_Vertex_Count();
    const int polygonCount = model->Get_Polygon_Count();
    if (vertexCount <= 0 || polygonCount <= 0)
        return false;

    const Vector3* positions = model->Get_Vertex_Array();
    const Vector3* normals = model->Get_Vertex_Normal_Array();
    const TriIndex* polygons = model->Get_Polygon_Array();
    if (!positions || !polygons)
        return false;

    auto& device = Renderer::Instance().GetDevice();
    const int passCount = std::max(1, model->Get_Pass_Count());

    for (int pass = 0; pass < passCount; ++pass)
    {
        const Vector2* texcoords = model->Get_UV_Array(pass, 0);
        const unsigned* diffuseColors = model->Get_DCG_Array(pass);
        const bool useVertexColors =
            diffuseColors != nullptr &&
            model->Get_DCG_Source(pass) != VertexMaterialClass::MATERIAL;

        std::vector<Vertex3D> vertices(vertexCount);
        for (int i = 0; i < vertexCount; ++i)
        {
            Vertex3D& vertex = vertices[i];
            vertex.position = { positions[i].X, positions[i].Y, positions[i].Z };
            if (normals)
            {
                vertex.normal = { normals[i].X, normals[i].Y, normals[i].Z };
            }
            else
            {
                vertex.normal = { 0.0f, 0.0f, 1.0f };
            }

            if (texcoords)
            {
                vertex.texcoord = { texcoords[i].X, texcoords[i].Y };
            }
            else
            {
                vertex.texcoord = { 0.0f, 0.0f };
            }

            vertex.color = useVertexColors ? argbToAbgr(diffuseColors[i]) : 0xFFFFFFFF;
        }

        VertexBuffer passVertexBuffer;
        if (!passVertexBuffer.Create(device, vertices.data(), static_cast<uint32_t>(vertices.size()), sizeof(Vertex3D), false))
            continue;

        // Determine blend mode from the mesh shader
        ShaderClass passShader = model->Get_Single_Shader(pass);
        MeshBlendMode passBlendMode = ClassifyShaderBlendMode(passShader);
        bool passDepthWrite = passShader.Get_Depth_Mask() != ShaderClass::DEPTH_WRITE_DISABLE;

        if (model->Has_Texture_Array(pass, 0))
        {
            struct TextureBlendGroup
            {
                Texture* texture = nullptr;
                MeshBlendMode blendMode = BLEND_OPAQUE;
                bool depthWrite = true;
                std::vector<uint16_t> indices;
            };

            // Group by (texture, blendMode) pair
            struct GroupKey { Texture* tex; MeshBlendMode blend; bool operator==(const GroupKey& o) const { return tex == o.tex && blend == o.blend; } };
            struct GroupKeyHash { size_t operator()(const GroupKey& k) const { return std::hash<const void*>()(k.tex) ^ (size_t)k.blend; } };

            std::vector<TextureBlendGroup> groups;
            std::unordered_map<GroupKey, size_t, GroupKeyHash> groupLookup;

            // Parallel array of "is this a house-icon texture?" flags, keyed
            // the same way `groups` is below. Two source patterns to catch:
            //   * ZHCA_* — ZH custom-art recolored faction icons (unit team
            //     color, CC emblem on the general-specific HQ variants).
            //   * AG*   — "Air General" / general-specific landing-pad icons
            //     (AGAirFrc, AGSpec, AGTank, ...) bolted onto the main HQ
            //     via the _FA/_FS/_FT sub-models. These are NOT recolored;
            //     the shader still wants the dark-key / player-color
            //     treatment.
            // W3D names are checked BEFORE ResolveTexture because the
            // recolor-munged "#<colorInt>#<lower>" name lives there; the
            // D3D11 texture doesn't carry it.
            auto stripMunge = [](const char* name) -> const char* {
                if (name && name[0] == '#') {
                    const char* second = strchr(name + 1, '#');
                    if (second) return second + 1;
                }
                return name;
            };
            auto looksLikeHouseIcon = [&](const char* name) -> bool {
                if (!name) return false;
                const char* core = stripMunge(name);
                if (!core) return false;
                if (_strnicmp(core, "ZHCA", 4) == 0) return true;
                if (_strnicmp(core, "AG",   2) == 0) return true;
                // Substring fallback — catches paths like "Art/Textures/ZHCA_*.tga".
                return (strstr(name, "zhca") != nullptr) ||
                       (strstr(name, "ZHCA") != nullptr);
            };
            std::vector<bool> groupIsZhca;
            for (int polygonIndex = 0; polygonIndex < polygonCount; ++polygonIndex)
            {
                TextureClass* rawTex = model->Peek_Texture(polygonIndex, pass, 0);
                const char* rawName = rawTex ? rawTex->Get_Texture_Name().str() : nullptr;
                const char* rawPath = rawTex ? rawTex->Get_Full_Path().str() : nullptr;
                bool isZhca = looksLikeHouseIcon(rawName) || looksLikeHouseIcon(rawPath);
                Texture* texture = ResolveTexture(rawTex);

                // Per-polygon shader if available, otherwise use pass shader
                MeshBlendMode polyBlend = passBlendMode;
                bool polyDepthWrite = passDepthWrite;
                if (model->Has_Shader_Array(pass))
                {
                    ShaderClass polyShader = model->Get_Shader(polygonIndex, pass);
                    polyBlend = ClassifyShaderBlendMode(polyShader);
                    polyDepthWrite = polyShader.Get_Depth_Mask() != ShaderClass::DEPTH_WRITE_DISABLE;
                }

                GroupKey key{texture, polyBlend};
                auto lookup = groupLookup.find(key);
                if (lookup == groupLookup.end())
                {
                    const size_t newIndex = groups.size();
                    lookup = groupLookup.emplace(key, newIndex).first;
                    groups.push_back({texture, polyBlend, polyDepthWrite, {}});
                    groupIsZhca.push_back(isZhca);
                }

                std::vector<uint16_t>& groupedIndices = groups[lookup->second].indices;
                const TriIndex& triangle = polygons[polygonIndex];
                groupedIndices.push_back(triangle.I);
                groupedIndices.push_back(triangle.J);
                groupedIndices.push_back(triangle.K);
            }

            for (size_t gi = 0; gi < groups.size(); ++gi)
            {
                TextureBlendGroup& group = groups[gi];
                if (group.indices.empty())
                    continue;

                StaticMeshBatch batch;
                batch.pass = pass;
                batch.texture = group.texture;
                batch.blendMode = group.blendMode;
                batch.depthWrite = group.depthWrite;
                batch.vertexBuffer = passVertexBuffer;
                batch.isZhcaTexture = gi < groupIsZhca.size() ? groupIsZhca[gi] : false;

                // DXT1 textures have alpha derived from brightness at load time.
                // Promote opaque batches to alpha-test so black pixels get clipped.
                if (batch.texture && !batch.texture->HasAlpha() && batch.blendMode == BLEND_OPAQUE)
                    batch.blendMode = BLEND_ALPHA_TEST;

                if (!batch.indexBuffer.Create(device, group.indices.data(), static_cast<uint32_t>(group.indices.size()), false))
                    continue;

                cacheEntry.batches.push_back(std::move(batch));
            }
            continue;
        }

        std::vector<uint16_t> indices;
        indices.reserve(static_cast<size_t>(polygonCount) * 3);
        for (int polygonIndex = 0; polygonIndex < polygonCount; ++polygonIndex)
        {
            const TriIndex& triangle = polygons[polygonIndex];
            indices.push_back(triangle.I);
            indices.push_back(triangle.J);
            indices.push_back(triangle.K);
        }

        if (indices.empty())
            continue;

        StaticMeshBatch batch;
        batch.pass = pass;
        batch.blendMode = passBlendMode;
        batch.depthWrite = passDepthWrite;
        TextureClass* rawTex = model->Peek_Single_Texture(pass, 0);
        const char* rawName = rawTex ? rawTex->Get_Texture_Name().str() : nullptr;
        const char* rawPath = rawTex ? rawTex->Get_Full_Path().str() : nullptr;
        auto stripMungeSingle = [](const char* n) -> const char* {
            if (n && n[0] == '#') { const char* s = strchr(n + 1, '#'); if (s) return s + 1; }
            return n;
        };
        auto houseIcon = [&](const char* n) {
            if (!n) return false;
            const char* core = stripMungeSingle(n);
            if (!core) return false;
            if (_strnicmp(core, "ZHCA", 4) == 0) return true;
            if (_strnicmp(core, "AG",   2) == 0) return true;
            return (strstr(n, "zhca") != nullptr) || (strstr(n, "ZHCA") != nullptr);
        };
        batch.isZhcaTexture = houseIcon(rawName) || houseIcon(rawPath);
        batch.texture = ResolveTexture(rawTex);
        batch.vertexBuffer = passVertexBuffer;

        // DXT1 textures have alpha derived from brightness at load time.
        // Promote opaque batches to alpha-test so black pixels get clipped.
        if (batch.texture && !batch.texture->HasAlpha() && batch.blendMode == BLEND_OPAQUE)
            batch.blendMode = BLEND_ALPHA_TEST;

        if (!batch.indexBuffer.Create(device, indices.data(), static_cast<uint32_t>(indices.size()), false))
            continue;

        cacheEntry.batches.push_back(std::move(batch));
    }

    return !cacheEntry.batches.empty();
}

Texture* ModelRenderer::ResolveTexture(TextureClass* texture)
{
    static int s_loggedMisses = 0;

    if (!texture)
        return GetWhiteTexture();

    // Check if we've already resolved this exact TextureClass pointer
    // (handles recolored/house-color textures which are unique instances)
    auto cachedIt = m_textureFromW3D.find(texture);
    if (cachedIt != m_textureFromW3D.end())
        return &cachedIt->second;

    std::vector<std::string> candidates;
    appendTextureCandidates(candidates, texture->Get_Full_Path().str());
    appendTextureCandidates(candidates, texture->Get_Texture_Name().str());

    auto& imageCache = ImageCache::Instance();
    auto& device = Renderer::Instance().GetDevice();

    for (const std::string& candidate : candidates)
    {
        if (Texture* resolved = imageCache.GetTexture(device, candidate.c_str()))
        {
            // Cache by pointer for fast future lookups
            m_textureFromW3D[texture] = *resolved;
            return &m_textureFromW3D[texture];
        }
    }

    // For recolored textures (ZHC house-color), try to extract the recolored
    // pixel data from the W3D TextureClass. The asset manager already did the
    // palette remap into system memory via the D3D8 surface stubs.
    // Access the D3D8 stub surface directly (SurfaceClass isn't linked).
    {
        IDirect3DTexture8* d3dTex = texture->Peek_D3D_Texture();
        if (d3dTex && d3dTex->m_surface && d3dTex->m_surface->m_data)
        {
            IDirect3DSurface8* surf = d3dTex->m_surface;
            unsigned w = surf->m_width;
            unsigned h = surf->m_height;
            D3DFORMAT fmt = surf->m_format;
            unsigned bpp = (fmt == D3DFMT_R5G6B5 || fmt == D3DFMT_A4R4G4B4) ? 2 : 4;

            if (w > 0 && h > 0)
            {
                std::vector<uint32_t> rgba(w * h);
                const uint8_t* src = surf->m_data;
                for (unsigned y = 0; y < h; ++y)
                {
                    for (unsigned x = 0; x < w; ++x)
                    {
                        if (bpp == 4)
                        {
                            uint32_t argb = ((const uint32_t*)src)[y * w + x];
                            uint32_t a = (argb >> 24) & 0xFF;
                            uint32_t r = (argb >> 16) & 0xFF;
                            uint32_t g = (argb >> 8) & 0xFF;
                            uint32_t b = argb & 0xFF;
                            rgba[y * w + x] = (a << 24) | (b << 16) | (g << 8) | r;
                        }
                        else
                        {
                            uint16_t pixel = ((const uint16_t*)src)[y * w + x];
                            uint32_t r = ((pixel >> 11) & 0x1F) * 255 / 31;
                            uint32_t g = ((pixel >> 5) & 0x3F) * 255 / 63;
                            uint32_t b = (pixel & 0x1F) * 255 / 31;
                            rgba[y * w + x] = 0xFF000000 | (b << 16) | (g << 8) | r;
                        }
                    }
                }

                Texture recoloredTex;
                if (recoloredTex.CreateFromRGBA(device, rgba.data(), w, h, true))
                {
                    m_textureFromW3D[texture] = std::move(recoloredTex);
                    return &m_textureFromW3D[texture];
                }
            }
        }
    }

    // Fallback: for munged ZHC names, strip color suffix and load base texture
    const char* texName = texture->Get_Texture_Name().str();
    if (texName && (_strnicmp(texName, "ZHC", 3) == 0 || strstr(texName, "_HC_") != nullptr || (texName[0] == '#')))
    {
        std::string baseName = texName;
        // Munged names: "#colorInt#basename" or "ZHCA_texname_COLORHASH"
        if (texName[0] == '#')
        {
            // Extract basename from "#colorInt#basename"
            const char* secondHash = strchr(texName + 1, '#');
            if (secondHash) baseName = secondHash + 1;
        }
        else
        {
            size_t lastUnderscore = baseName.rfind('_');
            if (lastUnderscore != std::string::npos && lastUnderscore > 4)
                baseName = baseName.substr(0, lastUnderscore);
        }
        std::vector<std::string> baseCandidates;
        appendTextureCandidates(baseCandidates, baseName.c_str());
        for (const std::string& candidate : baseCandidates)
        {
            if (Texture* resolved = imageCache.GetTexture(device, candidate.c_str()))
            {
                m_textureFromW3D[texture] = *resolved;
                return &m_textureFromW3D[texture];
            }
        }
    }

    // Last resort: try stripping everything to just the base filename without extension,
    // then search with both extensions
    {
        const char* fullPath = texture->Get_Full_Path().str();
        const char* texName2 = texture->Get_Texture_Name().str();
        // Collect both source strings, strip to basename, remove extension
        const char* sources[] = { fullPath, texName2 };
        for (const char* src : sources)
        {
            if (!src || !*src)
                continue;
            std::string s(src);
            // Strip all directory prefixes
            s = stripDirectoryPrefix(s);
            // Strip extension
            size_t dotPos = s.find_last_of('.');
            if (dotPos != std::string::npos)
                s = s.substr(0, dotPos);
            if (s.empty())
                continue;

            // Try lowercase and original case with both extensions
            std::string lower = toLowerStr(s);
            const char* exts[] = { ".dds", ".tga" };
            const std::string* names[] = { &s, &lower };
            for (const std::string* name : names)
            {
                for (const char* ext : exts)
                {
                    std::string attempt = *name + ext;
                    if (Texture* resolved = imageCache.GetTexture(device, attempt.c_str()))
                    {
                        m_textureFromW3D[texture] = *resolved;
                        return &m_textureFromW3D[texture];
                    }
                }
            }
        }
    }

    if (s_loggedMisses < 128)
    {
        // Log candidates tried so we can debug which textures are still missing
        AppendModelTrace(
            "ModelRenderer::ResolveTexture MISS fullPath='%s' name='%s' tried %d candidates\n",
            texture->Get_Full_Path().str(),
            texture->Get_Texture_Name().str(),
            (int)candidates.size());
        ++s_loggedMisses;
    }

    return GetWhiteTexture();
}

Texture* ModelRenderer::GetWhiteTexture()
{
    if (!m_whiteTextureReady)
    {
        const uint32_t whitePixel = 0xFFFFFFFF;
        m_whiteTexture.CreateFromRGBA(Renderer::Instance().GetDevice(), &whitePixel, 1, 1, false);
        m_whiteTextureReady = true;
    }

    return &m_whiteTexture;
}

Render::Float4x4 ModelRenderer::ToWorldMatrix(const Matrix3D& matrix)
{
    return RenderUtils::Matrix3DToFloat4x4(matrix);
}

Render::Float4 ModelRenderer::ComputeMeshColor(MeshClass* mesh, int pass) const
{
    (void)pass;

    // Silhouette mode short-circuits to a flat constant color, ignoring
    // the mesh's HOUSECOLOR/material/tint computation. Used by the
    // occluded-unit silhouette pass to draw every mesh as a flat fill.
    if (m_silhouetteMode)
        return m_silhouetteColor;

    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
    float alpha = mesh ? mesh->Get_Alpha_Override() : 1.0f;

    if (mesh)
    {
        // Apply objectColor tint to HOUSECOLOR meshes.
        // The color comes from the top-level render object (m_currentObjectColor)
        // since Set_ObjectColor doesn't propagate to child meshes.
        const char* meshName = mesh->Get_Name();
        const char* dotPos = meshName ? strchr(meshName, '.') : nullptr;
        const char* subName = dotPos ? dotPos + 1 : meshName;
        if (subName && _strnicmp(subName, "HOUSECOLOR", 10) == 0)
        {
            unsigned int objectColor = m_currentObjectColor;
            if (objectColor == 0)
                objectColor = mesh->Get_ObjectColor();
            if (objectColor != 0)
            {
                red = ((objectColor >> 16) & 0xFF) / 255.0f;
                green = ((objectColor >> 8) & 0xFF) / 255.0f;
                blue = (objectColor & 0xFF) / 255.0f;
            }

#ifdef DEBUG_LOGGING
            // DIAG: log the first ~30 unique HOUSECOLOR mesh tints we see so
            // we can confirm which color (and which byte order) is actually
            // reaching the renderer for GLA / China / USA units.
            static std::unordered_set<uint64_t> s_seenHouseTints;
            uint64_t key = ((uint64_t)objectColor << 32) | (uintptr_t)(meshName ? meshName : "?");
            if (s_seenHouseTints.size() < 30 && s_seenHouseTints.insert(key).second)
            {
                fprintf(stderr,
                    "[HOUSECOLOR] mesh='%s' objectColor=0x%08X -> rgb=(%.2f,%.2f,%.2f)\n",
                    meshName ? meshName : "(null)",
                    objectColor, red, green, blue);
                fflush(stderr);
            }
#endif
        }
    }

    // Apply additive tint from Drawable flash/selection envelopes (selection
    // glow, capture-building flash, low-health damage pulse). Placement-ghost
    // tinting is handled by the ghost shader path, not here.
    red += m_tintR;
    green += m_tintG;
    blue += m_tintB;

    // Apply fog darkening for objects in fog-of-war
    red *= m_fogDarkening;
    green *= m_fogDarkening;
    blue *= m_fogDarkening;

    alpha = std::clamp(alpha, 0.0f, 1.0f);
    return { red, green, blue, alpha };
}

void ModelRenderer::AddMeshDecal(MeshModelClass* model, uint32_t decalID,
                                  const Render::Float4x4& projMatrix,
                                  TextureClass* decalTexture, float backfaceThreshold,
                                  float opacity)
{
    if (!model)
        return;

    auto cacheIt = m_staticMeshes.find(model);
    if (cacheIt == m_staticMeshes.end())
        return;

    MeshDecalInfo info;
    info.decalID = decalID;
    info.projMatrix = projMatrix;
    info.backfaceThreshold = backfaceThreshold;
    info.decalTexture = decalTexture ? ResolveTexture(decalTexture) : nullptr;
    info.opacity = opacity;

    cacheIt->second.decals.push_back(info);
}

void ModelRenderer::SetGhostMode(float tintR, float tintG, float tintB,
                                  float tintIntensity, float opacity)
{
    m_ghostMode = true;
    m_ghostTintR = tintR;
    m_ghostTintG = tintG;
    m_ghostTintB = tintB;
    m_ghostTintIntensity = std::clamp(tintIntensity, 0.0f, 1.0f);
    m_ghostOpacity = std::clamp(opacity, 0.0f, 1.0f);
}

void ModelRenderer::ClearGhostMode()
{
    m_ghostMode = false;
    m_ghostTintR = m_ghostTintG = m_ghostTintB = 0.0f;
    m_ghostTintIntensity = 0.0f;
    m_ghostOpacity = 1.0f;
}

void ModelRenderer::RemoveMeshDecal(MeshModelClass* model, uint32_t decalID)
{
    if (!model)
        return;

    auto cacheIt = m_staticMeshes.find(model);
    if (cacheIt == m_staticMeshes.end())
        return;

    auto& decals = cacheIt->second.decals;
    decals.erase(
        std::remove_if(decals.begin(), decals.end(),
            [decalID](const MeshDecalInfo& d) { return d.decalID == decalID; }),
        decals.end());
}

} // namespace Render
