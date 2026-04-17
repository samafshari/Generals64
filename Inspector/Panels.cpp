// Inspector panels — log, hierarchy, properties, players.
//
// Implementation notes:
// - Compiled with PrecompiledHeader=NotUsing because we mix engine
//   headers (which expect PreRTS.h) with imgui.h. We include PreRTS.h
//   manually at the top so we still get Bool/Int/Real/etc.
// - The DEBUG_LOG sink is registered at Init time and can fire from
//   any thread the engine logs from. A small mutex protects the ring
//   buffer so log capture is thread-safe even if the engine logs from
//   audio or net threads (the bulk of logging is on the main thread).
// - Selection state is keyed by ObjectID, not Object*, so an object
//   that gets destroyed while selected just appears as "no selection"
//   on the next frame instead of dereferencing a stale pointer.

#include "PreRTS.h"

// Engine surface we want to inspect ----------------------------------
#include "Common/PlayerList.h"
#include "Common/LivePerf.h"
#include "Common/Player.h"
#include "Common/Money.h"
#include "Common/Energy.h"
#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "Common/Geometry.h"
#include "Common/SearchPaths.h"
#ifdef _WIN32
#  include <windows.h>
#endif

// Externs read by the Launch Parameters panel. Declared at file scope
// (NOT inside the anonymous namespace below) so they pick up external
// linkage and resolve to the real definitions in CommandLine.cpp /
// LANAPI.cpp at link time. A block-scope `extern` inside an anonymous
// namespace function would get internal linkage and fail to link.
extern char g_relayServerHost[256];
extern char g_relayFilterCode[24];
extern Bool g_launchToMpMenu;
extern AsciiString g_launchConfigFile;
extern Int g_autotestFrames;
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameClient/View.h"
#include "GameClient/Drawable.h"
#include "W3DDevice/GameClient/W3DView.h"
// Model debugger panel: walks the W3D render-object tree of the
// selected drawable to show meshes, textures, sub-objects, decals.
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/hlod.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/matinfo.h"
#include "WW3D2/texture.h"

// Renderer debug-draw module (3D world-space line/box/sphere/arrow).
#include "DebugDraw.h"
// Renderer.h for the game viewport SRV (used by the Game panel)
#include "Renderer.h"
#include "Core/Texture.h"

// Bridge in D3D11Shims.cpp — reads WorldHeightMap bytes without
// pulling any W3D / DX8-era headers into this translation unit.
extern "C" bool InspectorGetTerrainBytes(
    int* outWidth, int* outHeight, int* outBorder,
    float* outWorldWidth, float* outWorldHeight,
    const unsigned char** outData);

// ----------------------------------------------------------------------------
// Render-pass / post-FX toggles (replacement for the old F9 in-game menu)
// ----------------------------------------------------------------------------
// These bools live in W3DDisplay.cpp where the per-frame render path
// reads them. The old F9 overlay is gone — the inspector's
// "Render Toggles" panel now drives them via these externs.
extern bool g_debugDisableSkyBox;
extern bool g_debugDisableTerrain;
extern bool g_debugDisableRoads;
extern bool g_debugDisableBridges;
extern bool g_debugDisableProps;
extern bool g_debugDisableBibs;
extern bool g_debugDisableScorch;
extern bool g_debugDisableTracks;
extern bool g_debugDisableWaypoints;
extern bool g_debugDisableShroud;
extern bool g_debugDisableCloudShadows;
extern bool g_debugDisableModels;
extern bool g_debugDisableTranslucent;
extern bool g_debugDisableParticles;
extern bool g_debugDisableSnow;
extern bool g_debugDisableShadowDecals;
extern bool g_debugDisableShadowMap;
extern bool g_debugShadowGizmos;
extern int  g_debugBuildingShadowViz;
extern bool g_debugDisableWater;
extern bool g_debugDisableReflection;
extern bool g_debugDisableUI;
extern bool g_debugDisableMouse;
extern bool g_debugDisableLighting;
extern bool g_debugDisableDrawViews;
extern bool g_debugDisableFrustumCull;
extern bool g_debugDisableFSRVideo;
extern bool g_debugDisableLaserGlow;
extern bool g_debugDisableTracerStreak;
extern bool g_debugDisableVolumetric;
extern bool g_debugDisableModernAOE;
extern bool g_useEnhancedWater;
extern bool g_useEnhancedParticles;
extern bool g_useEnhancedSmudges;
extern bool g_debugDisableColorAwareFX;
extern bool g_debugDisableParticleGlow;
extern bool g_debugDisableHeatDistortion;
extern bool g_debugDisableShockwave;
extern bool g_debugDisableDistanceFog;
extern bool g_debugDisableSurfaceSpec;
extern bool g_debugDisableLensFlare;
extern bool g_debugDisableGodRays;
extern bool g_debugDisableChromaAberration;
extern bool g_debugDisableColorGrade;
extern bool g_debugDisableSharpen;
extern bool g_debugDisableSmoothParticleFade;
extern bool g_debugDisableLightPulse;
extern bool g_debugDisableStatusCircle;
extern bool g_debugDisableWW3DSync;
extern bool g_debugDisableTrackUpdate;
extern bool g_debugDisableBloom;
extern bool g_debugDisableVolumetricTrails;
extern bool g_useClassicTrails;
#ifdef BUILD_WITH_D3D11
#include <d3d11.h>
#endif

// Modern HUD panels (Resources / Radar / Selection / Commands / etc.)
#include "HudPanels.h"
// AI debug panels (state machine, pathfinder, kanban, build lists)
#include "AIPanels.h"

// Inspector + ImGui --------------------------------------------------
#include "Panels.h"
#include "Inspector.h"
#include "ScriptPanel.h"
#include "DestructionTimeline.h"
#include "imgui.h"
// imgui_internal.h is needed for the DockBuilder API
// (DockBuilderRemoveNode, DockBuilderSplitNode, DockBuilderDockWindow,
// DockBuilderFinish). The "internal" name is misleading — these are
// the canonical ImGui APIs for programmatic dockspace layout, used
// by every editor that builds on the docking branch.
#include "imgui_internal.h"

#include <deque>
#include <mutex>
#include <string>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <unordered_set>

namespace Inspector
{

// ============================================================================
// Selection
// ============================================================================
namespace Selection
{
namespace
{
    uint32_t s_selectedID  = 0;
    uint32_t s_hoverID     = 0;
    bool     s_pickMode    = false;
    bool     s_tooltipMode = false;

    // Multi-tag set. Independent from s_selectedID — tags persist
    // across normal selection clicks. Built up via Ctrl-click in
    // pick mode and via Hierarchy panel tag buttons.
    std::unordered_set<uint32_t> s_tags;

    // Pending viewport click queued by Inspector::ProcessEvent. We
    // can't perform the engine pickDrawable() call from inside the
    // event handler because that file (Inspector.cpp) doesn't see
    // engine types — so the click coords sit here until DrawAll runs
    // on the same frame and consumes them.
    bool s_pendingClick    = false;
    int  s_pendingClickX   = 0;
    int  s_pendingClickY   = 0;
    int  s_pendingClickKind = 0;
}

uint32_t GetObjectID() { return s_selectedID; }
void SetObjectID(uint32_t id) { s_selectedID = id; }
void Clear() { s_selectedID = 0; s_hoverID = 0; }
bool HasSelection() { return s_selectedID != 0; }

bool IsPickMode() { return s_pickMode; }
void SetPickMode(bool on)
{
    s_pickMode = on;
    if (!on && !s_tooltipMode)
    {
        // Clear hover when no mode is left to drive it, so the
        // yellow highlight doesn't linger on whatever the cursor
        // was last over.
        s_hoverID = 0;
    }
}
void TogglePickMode() { SetPickMode(!s_pickMode); }

bool IsTooltipMode() { return s_tooltipMode; }
void SetTooltipMode(bool on)
{
    s_tooltipMode = on;
    if (!on && !s_pickMode)
        s_hoverID = 0;
}
void ToggleTooltipMode() { SetTooltipMode(!s_tooltipMode); }

uint32_t GetHoverID()        { return s_hoverID; }
void     SetHoverID(uint32_t id) { s_hoverID = id; }

void QueueClick(int x, int y, int kind)
{
    s_pendingClick     = true;
    s_pendingClickX    = x;
    s_pendingClickY    = y;
    s_pendingClickKind = kind;
}

bool ConsumePendingClick(int* outX, int* outY, int* outKind)
{
    if (!s_pendingClick)
        return false;
    if (outX)    *outX    = s_pendingClickX;
    if (outY)    *outY    = s_pendingClickY;
    if (outKind) *outKind = s_pendingClickKind;
    s_pendingClick = false;
    return true;
}

// --- Tag set ---
void TagAdd(uint32_t id)    { if (id) s_tags.insert(id); }
void TagRemove(uint32_t id) { s_tags.erase(id); }
void TagToggle(uint32_t id)
{
    if (!id) return;
    auto it = s_tags.find(id);
    if (it == s_tags.end()) s_tags.insert(id);
    else                    s_tags.erase(it);
}
void TagClear() { s_tags.clear(); }
bool IsTagged(uint32_t id) { return s_tags.find(id) != s_tags.end(); }
size_t TagCount() { return s_tags.size(); }
void TagForEach(void (*fn)(uint32_t, void*), void* user)
{
    if (!fn) return;
    // Snapshot first so the callback can mutate the set safely.
    std::vector<uint32_t> snap(s_tags.begin(), s_tags.end());
    for (uint32_t id : snap)
        fn(id, user);
}

// Internal helper — resolves an ObjectID to a live Object*, returning
// nullptr if nothing is selected or the object has died. Used by
// panels that need to read the selected/hovered object's state.
static Object* Resolve(uint32_t id)
{
    if (id == 0 || TheGameLogic == nullptr)
        return nullptr;
    return TheGameLogic->findObjectByID((ObjectID)id);
}

static Object* ResolveLive()
{
    return Resolve(s_selectedID);
}

} // namespace Selection

// ============================================================================
// Panels
// ============================================================================
namespace Panels
{

namespace
{
    Visibility s_visibility;
    DebugOverlayFlags g_debugFlags;

    // Layout reset state. When true, the next DrawAll snaps every
    // panel to its predetermined non-overlapping layout via
    // ImGuiCond_Always (overrides whatever the user had dragged them
    // to). Cleared at the end of DrawAll so subsequent frames preserve
    // user customization. Set by Panels::RequestLayoutReset().
    bool s_needsLayout = false;

    // Logical panel slots. Used as an index into the per-frame
    // computed layout array so each panel pulls its position+size
    // from the same place. Order is rendering order, not visibility.
    enum LayoutSlot
    {
        SLOT_LOG = 0,
        SLOT_HIERARCHY,
        SLOT_PROPERTIES,
        SLOT_PLAYERS,
        SLOT_MINIMAP,
        SLOT_PERFHUD,
        SLOT_GAME,
        SLOT_MODEL,
        SLOT_COUNT,
    };

    struct PanelRect { ImVec2 pos; ImVec2 size; };
    PanelRect s_layout[SLOT_COUNT];

    // The Game window's content rect (in OS window pixels) — captured
    // each frame after ImGui::Image runs. Used by UpdatePickMode to
    // remap mouse coords from "OS window space" to "engine resolution
    // space" so pickDrawable hits the right object regardless of
    // where the user has dragged or resized the Game window.
    bool   s_gameRectValid = false;
    ImVec2 s_gameRectMin   = ImVec2(0, 0);
    ImVec2 s_gameRectMax   = ImVec2(0, 0);
    int    s_gameRTWidth   = 0;
    int    s_gameRTHeight  = 0;

    // Frame-time history for the Performance HUD sparkline. Ring
    // buffer with the most recent kPerfHistory frames. Sampled once
    // per call to DrawAll. The std::vector index wraps via modulo.
    constexpr int kPerfHistory = 240;  // ~4 seconds at 60fps
    float s_perfFrameMs[kPerfHistory] = {};
    int   s_perfWriteIdx = 0;

    // Hovered Hierarchy row → drives the viewport hover highlight so
    // the user can scrub down a list of objects and see them light up
    // in 3D as the mouse moves over them. Set during DrawHierarchyPanel,
    // consumed by DrawSelectionOverlay via Selection::SetHoverID.
    uint32_t s_hierarchyHoverID = 0;

    // ---- Mini-map state: zoom / pan / terrain backdrop --------------
    //
    // Transform model matches the HUD Radar panel:
    //   centerX/Y  = world coord at the canvas center
    //   pxPerUnit  = zoom (pixels per world unit)
    //
    // Terrain is a 256x256 preview texture baked from the engine's
    // WorldHeightMap (via the InspectorGetTerrainBytes bridge).
    // Cached by source-data pointer so a new map load auto-rebakes.
    struct MinimapState
    {
        bool  initialized    = false;
        float centerX        = 0.0f;
        float centerY        = 0.0f;
        float pxPerUnit      = 0.10f;
        Render::Texture terrainTex;
        const unsigned char* terrainSrcPtr = nullptr;
        float mapWorldW      = 0.0f;
        float mapWorldH      = 0.0f;
        bool  terrainReady   = false;
    };
    MinimapState s_minimap;

    // Bake the terrain preview. 256x256 grayscale with brown tint.
    // Called when the source heightmap data pointer changes.
    void BakeMinimapTerrain()
    {
        int w = 0, h = 0, border = 0;
        float worldW = 0, worldH = 0;
        const unsigned char* data = nullptr;
        if (!InspectorGetTerrainBytes(&w, &h, &border, &worldW, &worldH, &data))
        {
            s_minimap.terrainReady  = false;
            s_minimap.terrainSrcPtr = nullptr;
            return;
        }
        if (!data || w <= 2 * border || h <= 2 * border)
        {
            s_minimap.terrainReady  = false;
            s_minimap.terrainSrcPtr = nullptr;
            return;
        }

        const int innerW = w - 2 * border;
        const int innerH = h - 2 * border;

        int hmin = 255, hmax = 0;
        for (int y = border; y < h - border; ++y)
        {
            const unsigned char* row = data + (size_t)y * w;
            for (int x = border; x < w - border; ++x)
            {
                const int hv = row[x];
                if (hv < hmin) hmin = hv;
                if (hv > hmax) hmax = hv;
            }
        }
        const int hrange = std::max(1, hmax - hmin);

        constexpr int kOutW = 256;
        constexpr int kOutH = 256;
        std::vector<uint32_t> rgba(kOutW * kOutH);
        for (int y = 0; y < kOutH; ++y)
        {
            const int sy = border + (y * innerH) / kOutH;
            const unsigned char* row = data + (size_t)sy * w;
            for (int x = 0; x < kOutW; ++x)
            {
                const int sx = border + (x * innerW) / kOutW;
                const int hv = row[sx];
                const float t = (float)(hv - hmin) / (float)hrange;
                // Shadowed brown → warm highlight — same palette as
                // the HUD Radar so the two panels feel consistent.
                const uint8_t r = (uint8_t)(48  + t * 160);
                const uint8_t g = (uint8_t)(42  + t * 130);
                const uint8_t b = (uint8_t)(30  + t *  90);
                rgba[(size_t)y * kOutW + x] =
                    (0xFFu << 24) | ((uint32_t)b << 16) |
                    ((uint32_t)g <<  8) | (uint32_t)r;
            }
        }

        auto& device = Render::Renderer::Instance().GetDevice();
        s_minimap.terrainTex.Destroy(device);
        if (!s_minimap.terrainTex.CreateFromRGBA(device, rgba.data(),
                kOutW, kOutH, /*generateMips*/ false))
        {
            s_minimap.terrainReady  = false;
            s_minimap.terrainSrcPtr = nullptr;
            return;
        }

        s_minimap.terrainSrcPtr = data;
        s_minimap.mapWorldW     = worldW;
        s_minimap.mapWorldH     = worldH;
        s_minimap.terrainReady  = true;
    }

    // ApplyLayout pushes a default floating-window size for when a
    // panel is undocked and floats freely. With docking enabled the
    // dock node controls actual position and size — so this is just
    // a hint for the "I tore the panel out and want it floating"
    // case. The previous fixed-coordinate layout was replaced with
    // ImGui::DockBuilder layout (see SetupDockspaceLayout below).
    void ApplyLayout(LayoutSlot slot)
    {
        static const ImVec2 floatingSizes[SLOT_COUNT] = {
            ImVec2(700, 200),  // SLOT_LOG
            ImVec2(340, 480),  // SLOT_HIERARCHY
            ImVec2(340, 360),  // SLOT_PROPERTIES
            ImVec2(420, 200),  // SLOT_PLAYERS
            ImVec2(360, 360),  // SLOT_MINIMAP
            ImVec2(340, 220),  // SLOT_PERFHUD
            ImVec2(960, 540),  // SLOT_GAME
            ImVec2(420, 600),  // SLOT_MODEL
        };
        ImGui::SetNextWindowSize(floatingSizes[slot], ImGuiCond_FirstUseEver);
    }

    // Build a dockspace layout placing every inspector panel at a
    // sensible default location around a central passthru hole that
    // exposes the game viewport. Called once on startup and again
    // whenever Panels::RequestLayoutReset() is invoked.
    //
    // Layout:
    //
    //   +-----------------------------------------------+
    //   |  HIER       |                |   PLAYERS      |
    //   |  ARCHY      |                +----------------+
    //   |             |   game shows   |   MINI-MAP     |
    //   |-------------|   through      +----------------+
    //   |  PROPS      |   here         |   PERF HUD     |
    //   |             |                |                |
    //   +-------------+----------------+----------------+
    //   |                LOG                            |
    //   +-----------------------------------------------+
    //
    // The central node has the PassthruCentralNode flag so it draws
    // nothing — the engine renders directly through. Mouse events in
    // the central area pass through to the game (ImGui doesn't claim
    // them). Panels are docked to leftTop/leftBot/rightTop/rightMid/
    // rightBot/bottom nodes around the central hole.
    void SetupDockspaceLayout(ImGuiID dockspaceID, ImVec2 size)
    {
        ImGui::DockBuilderRemoveNode(dockspaceID);
        // No PassthruCentralNode anymore — the central area is now
        // occupied by the "Game" window which displays the off-screen
        // game render target via ImGui::Image. The user can drag,
        // resize, and undock that window like any other panel.
        ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceID, size);

        ImGuiID center = dockspaceID;

        // Bottom: Log strip first so it spans the FULL width.
        ImGuiID bottomID = ImGui::DockBuilderSplitNode(
            center, ImGuiDir_Down, 0.20f, nullptr, &center);

        // Left column: Hierarchy on top, Properties below it
        ImGuiID leftID = ImGui::DockBuilderSplitNode(
            center, ImGuiDir_Left, 0.18f, nullptr, &center);
        ImGuiID leftBotID = 0;
        ImGuiID leftTopID = ImGui::DockBuilderSplitNode(
            leftID, ImGuiDir_Up, 0.55f, nullptr, &leftBotID);

        // Right column: Players on top, Mini-map middle, Perf HUD bottom
        ImGuiID rightID = ImGui::DockBuilderSplitNode(
            center, ImGuiDir_Right, 0.24f, nullptr, &center);
        ImGuiID rightMidID = 0;
        ImGuiID rightTopID = ImGui::DockBuilderSplitNode(
            rightID, ImGuiDir_Up, 0.40f, nullptr, &rightMidID);
        ImGuiID rightBotID = 0;
        ImGuiID rightMid2ID = ImGui::DockBuilderSplitNode(
            rightMidID, ImGuiDir_Up, 0.55f, nullptr, &rightBotID);

        // Game window goes in the central node (what's left of `center`
        // after all the side splits). This is the area that previously
        // had PassthruCentralNode; now it holds the Game ImGui::Image.
        ImGui::DockBuilderDockWindow("Game",       center);
        ImGui::DockBuilderDockWindow("Hierarchy",  leftTopID);
        ImGui::DockBuilderDockWindow("Properties", leftBotID);
        // Dock Model into the same tab group as Properties so it
        // shares the bottom-left slot. Off by default; user toggles
        // it from the View menu and can drag it elsewhere if they
        // want both Properties and Model visible at once.
        ImGui::DockBuilderDockWindow("Model",      leftBotID);
        ImGui::DockBuilderDockWindow("Players",    rightTopID);
        ImGui::DockBuilderDockWindow("Mini-map",   rightMid2ID);
        ImGui::DockBuilderDockWindow("Perf HUD",   rightBotID);
        ImGui::DockBuilderDockWindow("Log",        bottomID);

        ImGui::DockBuilderFinish(dockspaceID);
    }

    // ---- Log capture ring buffer -----------------------------------
    constexpr size_t kLogRingMax = 5000;
    std::deque<std::string> s_logRing;
    std::mutex s_logMutex;
    bool s_logAutoScroll = true;
    char s_logFilter[128] = "";
    bool s_logHookInstalled = false;

    // DEBUG_LOG sink — runs on whatever thread called DebugLog. Keep
    // the critical section as short as possible: copy the line into
    // the ring under lock, drop oldest entries to bound memory, and
    // get out before any further work.
    void LogSink(const char* line)
    {
        if (!line)
            return;
        std::lock_guard<std::mutex> lock(s_logMutex);
        if (s_logRing.size() >= kLogRingMax)
            s_logRing.pop_front();
        s_logRing.emplace_back(line);
    }

}

// Push for arbitrary string — used by Inspector::Log (defined at the
// Inspector namespace scope below so it's visible outside Panels).
// Lives at Inspector::Panels::LogRingPush so the ring buffer stays
// file-local to Panels.cpp but can still be called from outside the
// anonymous namespace (e.g. from the Inspector::Log forwarder).
void LogRingPush(const char* line)
{
    if (!line)
        return;
    std::lock_guard<std::mutex> lock(s_logMutex);
    if (s_logRing.size() >= kLogRingMax)
        s_logRing.pop_front();
    s_logRing.emplace_back(line);
}

namespace
{

    // ---- Per-panel draw helpers ------------------------------------
    void DrawLogPanel();
    void DrawHierarchyPanel();
    void DrawPropertiesPanel();
    void DrawPlayersPanel();
    void DrawMinimapPanel();
    void DrawPerfHudPanel();
    void DrawGamePanel();
    void DrawModelPanel();
    void DrawRenderTogglesPanel();
    void DrawLightsPanel();
    void DrawLaunchParamsPanel();

    // ---- Pick mode helpers (per-frame picking + overlay rendering) -
    void UpdatePickMode();         // run pick + queue → selection/hover
    void DrawSelectionOverlay();   // foreground draw list highlights
    void DrawHoverTooltip();       // floating context tooltip at cursor

    // ---- Properties content helper ---------------------------------
    // Renders the body of the Properties panel for an Object. Shared
    // by DrawPropertiesPanel and DrawHoverTooltip (when tooltip mode
    // is on) so the floating tooltip looks identical to the docked
    // panel.
    void RenderObjectProperties(Object* obj);

    // Forward declaration — defined further down in the Pick Mode
    // section. Returns a short string label for an Object's KindOf
    // category ("Vehicle" / "Infantry" / "Structure" / etc).
    const char* ContextKindLabel(Object* obj);

    // Format helper for player names — getPlayerDisplayName returns
    // UnicodeString (wchar_t-based) which doesn't drop into ImGui::Text
    // cleanly, so we prefer the AsciiString side identifier instead.
    // The result is one of: "America", "China", "GLA", "Civilian", "".
    const char* SafeSide(Player* p)
    {
        if (!p)
            return "(null)";
        AsciiString side = p->getSide();
        const char* s = side.str();
        return (s && *s) ? s : "(neutral)";
    }

    // Engine Color is a 32-bit ARGB int (A in the high byte). ImGui's
    // TextColored takes an ImVec4 in linear RGBA floats — convert
    // here so we can tint the side label per player. We also boost
    // very dark / desaturated colors slightly so they remain readable
    // against the dark ImGui theme: pure 0x000000 backgrounds for
    // unassigned players just become a neutral gray.
    ImVec4 ColorToImVec4(Color c)
    {
        const float a = ((c >> 24) & 0xFF) / 255.0f;
        const float r = ((c >> 16) & 0xFF) / 255.0f;
        const float g = ((c >>  8) & 0xFF) / 255.0f;
        const float b = ((c      ) & 0xFF) / 255.0f;
        // If the player has no color set (all zero), fall back to a
        // neutral gray instead of invisible black-on-black text.
        if (r == 0.0f && g == 0.0f && b == 0.0f)
            return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        // Engine Color sometimes leaves alpha at 0 for entries that
        // never had alpha explicitly set; force opaque so text isn't
        // invisible.
        return ImVec4(r, g, b, a > 0.0f ? a : 1.0f);
    }
}

// ----------------------------------------------------------------------------
// Init / Shutdown
// ----------------------------------------------------------------------------
void Init()
{
#ifdef DEBUG_LOGGING
    if (!s_logHookInstalled)
    {
        DebugSetLogSink(&LogSink);
        s_logHookInstalled = true;
    }
#endif
    InitScriptPanel();
}

void Shutdown()
{
#ifdef DEBUG_LOGGING
    if (s_logHookInstalled)
    {
        DebugSetLogSink(nullptr);
        s_logHookInstalled = false;
    }
#endif
    ShutdownScriptPanel();
    {
        std::lock_guard<std::mutex> lock(s_logMutex);
        s_logRing.clear();
    }
    Selection::Clear();
}

Visibility& GetVisibility() { return s_visibility; }

DebugOverlayFlags& GetDebugFlags() { return g_debugFlags; }

void RequestLayoutReset() { s_needsLayout = true; }

bool IsPointInGameViewport(int x, int y)
{
    if (!s_gameRectValid)
        return false;
    return (float)x >= s_gameRectMin.x && (float)x <= s_gameRectMax.x &&
           (float)y >= s_gameRectMin.y && (float)y <= s_gameRectMax.y;
}

GameViewportTransform RemapPointToGameViewport(int osX, int osY)
{
    GameViewportTransform t;
    if (!s_gameRectValid)
        return t;
    if ((float)osX < s_gameRectMin.x || (float)osX > s_gameRectMax.x ||
        (float)osY < s_gameRectMin.y || (float)osY > s_gameRectMax.y)
        return t;

    const float w = s_gameRectMax.x - s_gameRectMin.x;
    const float h = s_gameRectMax.y - s_gameRectMin.y;
    if (w < 1.0f || h < 1.0f)
        return t;

    t.valid  = true;
    t.scaleX = (float)s_gameRTWidth  / w;
    t.scaleY = (float)s_gameRTHeight / h;
    t.outX   = (int)(((float)osX - s_gameRectMin.x) * t.scaleX);
    t.outY   = (int)(((float)osY - s_gameRectMin.y) * t.scaleY);
    return t;
}

// ----------------------------------------------------------------------------
// DrawAll — dispatch
// ----------------------------------------------------------------------------
void DrawAll()
{
    // Set up the editor-style dockspace. PassthruCentralNode leaves
    // the middle of the screen transparent, so the engine renders
    // through it AND mouse events in the central area pass through
    // to the game (the standard Unity/Unreal editor pattern). Panels
    // dock to the edges around the hole.
    const ImGuiID dockspaceID = ImGui::DockSpaceOverViewport(0,
        ImGui::GetMainViewport(),
        ImGuiDockNodeFlags_PassthruCentralNode);

    // First-time / reset layout: programmatically dock every panel
    // to its default location. After this runs, the user can drag
    // tabs / split panes / undock floating windows freely and the
    // arrangement persists until the next RequestLayoutReset.
    if (s_needsLayout)
    {
        SetupDockspaceLayout(dockspaceID, ImGui::GetMainViewport()->WorkSize);
    }

    // Sample frame time into the perf HUD ring buffer once per draw,
    // before any panels run, so the graph captures real per-frame
    // intervals (not the time it takes to draw the inspector itself).
    {
        const float ms = 1000.0f / ImGui::GetIO().Framerate;
        s_perfFrameMs[s_perfWriteIdx % kPerfHistory] = ms;
        s_perfWriteIdx = (s_perfWriteIdx + 1) % kPerfHistory;
    }

    // Reset Hierarchy hover ID — it'll be set this frame if the
    // mouse is over a hierarchy row, otherwise it stays 0 and we
    // fall back to the viewport pick result.
    s_hierarchyHoverID = 0;

    // Pick mode runs first so the panels see freshly-updated hover/
    // selection state for the same frame the click happened on.
    UpdatePickMode();

    // Game panel runs FIRST so its content rect is captured before
    // UpdatePickMode reads it for mouse remap (UpdatePickMode runs
    // earlier in this same DrawAll, but it uses the LAST frame's
    // rect, which is fine because the user typically can't drag the
    // window faster than the inspector picks up).
    if (s_visibility.game)       DrawGamePanel();
    if (s_visibility.log)        DrawLogPanel();
    if (s_visibility.hierarchy)  DrawHierarchyPanel();
    if (s_visibility.properties) DrawPropertiesPanel();
    if (s_visibility.model)      DrawModelPanel();
    if (s_visibility.players)    DrawPlayersPanel();
    if (s_visibility.minimap)    DrawMinimapPanel();
    if (s_visibility.perfHud)    DrawPerfHudPanel();
    if (s_visibility.script)     DrawScriptPanel();
    if (s_visibility.renderToggles) DrawRenderTogglesPanel();
    if (s_visibility.destruction)
        Destruction::DrawPanel(&s_visibility.destruction);
    if (s_visibility.lights)     DrawLightsPanel();
    if (s_visibility.launchParams) DrawLaunchParamsPanel();

    // After hierarchy renders, if the user was hovering a row in it
    // we override the viewport hover so the gizmo lights up that
    // exact object. Hierarchy hover takes priority over viewport
    // pick because it's a deliberate panel interaction.
    if (s_hierarchyHoverID != 0)
        Selection::SetHoverID(s_hierarchyHoverID);

    // Modern HUD panels (Resources, Radar, Selection, Commands, Build
    // Queue, General Powers). These read game state 1:1 and are
    // visually distinct from the debug panels above. Drawn after the
    // debug panels but before the foreground overlays so the world-
    // space gizmos still composite on top.
    Hud::DrawAll();

    // AI debug panels (state machine, pathfinder, kanban boards,
    // build lists). Magenta-tinted chrome to distinguish from the
    // cyan Modern HUD panels and the gray debug panels.
    Ai::DrawAll();

    // Overlay highlights and the context tooltip render LAST so they
    // sit on top of every panel — drawn into the foreground draw
    // list which composites above all ImGui windows.
    DrawSelectionOverlay();
    DrawHoverTooltip();

    // Layout reset is a one-shot request — clear the flag now so the
    // next frame uses FirstUseEver and respects any dragging the user
    // does between now and the next reset request.
    s_needsLayout = false;
}

// ----------------------------------------------------------------------------
// Log panel
// ----------------------------------------------------------------------------
namespace
{
void DrawLogPanel()
{
    ApplyLayout(SLOT_LOG);
    if (!ImGui::Begin("Log", &s_visibility.log))
    {
        ImGui::End();
        return;
    }

    // Top toolbar: filter, autoscroll, clear, copy.
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##logfilter", "filter (substring)", s_logFilter, sizeof(s_logFilter));
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &s_logAutoScroll);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear"))
    {
        std::lock_guard<std::mutex> lock(s_logMutex);
        s_logRing.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy all"))
    {
        std::string blob;
        {
            std::lock_guard<std::mutex> lock(s_logMutex);
            for (const auto& line : s_logRing)
            {
                blob.append(line);
                blob.push_back('\n');
            }
        }
        ImGui::SetClipboardText(blob.c_str());
    }
    ImGui::SameLine();
    {
        std::lock_guard<std::mutex> lock(s_logMutex);
        ImGui::TextDisabled("(%zu lines)", s_logRing.size());
    }

    ImGui::Separator();

    // Build a single text buffer from the (filtered) ring contents.
    // We use ImGui::InputTextMultiline (read-only) so the user can
    // mouse-drag to select a range and Ctrl-C to copy it. The buffer
    // is rebuilt each frame from the ring snapshot under the mutex.
    static std::string s_logBufferText;
    s_logBufferText.clear();
    {
        std::lock_guard<std::mutex> lock(s_logMutex);
        s_logBufferText.reserve(s_logRing.size() * 80);
        const bool filtering = (s_logFilter[0] != 0);
        for (const auto& line : s_logRing)
        {
            if (filtering && strstr(line.c_str(), s_logFilter) == nullptr)
                continue;
            s_logBufferText.append(line);
            s_logBufferText.push_back('\n');
        }
    }

    // Detect buffer growth → trigger one-shot auto-scroll. The
    // callback below moves the cursor to the end which causes ImGui
    // to scroll the text view to follow. We only do this when the
    // user has Auto-scroll enabled AND new content arrived since
    // last frame, so manual selection survives between log writes.
    static size_t s_lastLogBufLen = 0;
    static bool   s_jumpToBottom  = false;
    if (s_logAutoScroll && s_logBufferText.size() > s_lastLogBufLen)
        s_jumpToBottom = true;
    s_lastLogBufLen = s_logBufferText.size();

    auto callback = [](ImGuiInputTextCallbackData* data) -> int
    {
        bool* jump = (bool*)data->UserData;
        if (jump && *jump)
        {
            data->CursorPos = data->BufTextLen;
            data->SelectionStart = data->SelectionEnd = data->CursorPos;
            *jump = false;
        }
        return 0;
    };

    // ImGui::InputTextMultiline wants char* + capacity. Read-only
    // means it won't actually write to the buffer; we can hand it
    // std::string::data() (writable in C++17+) and the size + 1.
    char dummy[1] = { 0 };
    char*  buf     = s_logBufferText.empty() ? dummy : s_logBufferText.data();
    size_t bufSize = s_logBufferText.empty() ? 1 : (s_logBufferText.size() + 1);

    const ImGuiInputTextFlags flags =
        ImGuiInputTextFlags_ReadOnly |
        ImGuiInputTextFlags_NoHorizontalScroll |
        ImGuiInputTextFlags_CallbackAlways;

    ImGui::InputTextMultiline("##loglines",
        buf, bufSize,
        ImVec2(-FLT_MIN, -FLT_MIN),
        flags, callback, &s_jumpToBottom);

    ImGui::End();
}

// ----------------------------------------------------------------------------
// Hierarchy panel
// ----------------------------------------------------------------------------
char s_hierarchyFilter[128] = "";

void DrawHierarchyPanel()
{
    ApplyLayout(SLOT_HIERARCHY);
    if (!ImGui::Begin("Hierarchy", &s_visibility.hierarchy))
    {
        ImGui::End();
        return;
    }

    if (TheGameLogic == nullptr)
    {
        ImGui::TextDisabled("GameLogic not initialized");
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##hierfilter", "filter by template name",
        s_hierarchyFilter, sizeof(s_hierarchyFilter));

    // Snapshot the live object list. Walking the linked list while
    // ImGui draws is fine because we're called from the main thread
    // and the engine logic is paused (or about to step) — but we want
    // a stable index for the clipper, so we collect into a vector
    // first. Each vector entry is a (id, templateName) pair so the
    // hierarchy doesn't dereference Object* across frames.
    struct Row
    {
        ObjectID id;
        const char* templateName;
        Player* owner;
    };
    std::vector<Row> rows;
    {
        const bool filtering = (s_hierarchyFilter[0] != 0);
        for (Object* obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
        {
            const ThingTemplate* tmpl = obj->getTemplate();
            const char* name = tmpl ? tmpl->getName().str() : "(null)";
            if (filtering && strstr(name, s_hierarchyFilter) == nullptr)
                continue;
            rows.push_back({ obj->getID(), name, obj->getControllingPlayer() });
        }
    }

    ImGui::TextDisabled("%zu objects (%u total)", rows.size(),
        (unsigned)TheGameLogic->getObjectCount());
    ImGui::Separator();

    if (ImGui::BeginChild("##hierscroll", ImVec2(0, 0)))
    {
        ImGuiListClipper clipper;
        clipper.Begin((int)rows.size());
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const Row& r = rows[i];
                char label[256];
                snprintf(label, sizeof(label), "%u  %s##obj%u",
                    (unsigned)r.id, r.templateName, (unsigned)r.id);

                const bool selected = (Selection::GetObjectID() == (uint32_t)r.id);
                if (ImGui::Selectable(label, selected,
                        ImGuiSelectableFlags_AllowDoubleClick))
                {
                    Selection::SetObjectID((uint32_t)r.id);
                }

                // Hovering a row in the hierarchy lights up the
                // matching object in the 3D viewport with the hover
                // gizmo, so the user can scrub the list and visually
                // identify what's where on the map. The override is
                // applied at the end of DrawAll after all panels have
                // had a chance to set their hover state.
                if (ImGui::IsItemHovered())
                {
                    s_hierarchyHoverID = (uint32_t)r.id;
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", r.templateName);
                    ImGui::TextDisabled("ID %u", (unsigned)r.id);
                    if (r.owner)
                    {
                        const ImVec4 sideColor = ColorToImVec4(r.owner->getPlayerColor());
                        ImGui::Text("Owner: ");
                        ImGui::SameLine();
                        ImGui::TextColored(sideColor, "%s", SafeSide(r.owner));
                    }
                    ImGui::EndTooltip();
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

// Render the body content for an Object's properties (shared between
// the Properties panel and the floating tooltip in tooltip mode).
// Caller is responsible for the surrounding ImGui::Begin/End or
// ImGui::BeginTooltip/EndTooltip wrapper.
void RenderObjectProperties(Object* obj)
{
    if (!obj)
    {
        ImGui::TextDisabled("No selection.");
        ImGui::TextDisabled("Click an entry in Hierarchy to inspect.");
        return;
    }

    // ---- Identity header (template name as the title)
    if (const ThingTemplate* tmpl = obj->getTemplate())
    {
        ImGui::PushFont(nullptr);  // default font, just want a header feel
        ImGui::TextUnformatted(tmpl->getName().str());
        ImGui::PopFont();
    }
    ImGui::TextDisabled("ID %u  ·  %s",
        (unsigned)obj->getID(),
        ContextKindLabel(obj));

    if (Player* p = obj->getControllingPlayer())
    {
        const ImVec4 sideColor = ColorToImVec4(p->getPlayerColor());
        ImGui::Text("Owner: ");
        ImGui::SameLine();
        ImGui::TextColored(sideColor, "%s", SafeSide(p));
    }

    ImGui::Separator();

    // ---- Transform
    if (const Coord3D* pos = obj->getPosition())
    {
        ImGui::Text("Position");
        ImGui::Text("  X: %8.2f", pos->x);
        ImGui::Text("  Y: %8.2f", pos->y);
        ImGui::Text("  Z: %8.2f", pos->z);
    }
    ImGui::Text("Orientation: %.3f rad (%.1f deg)",
        obj->getOrientation(), obj->getOrientation() * (180.0f / 3.14159265f));

    ImGui::Separator();

    // ---- Health
    if (BodyModuleInterface* body = obj->getBodyModule())
    {
        const float hp  = body->getHealth();
        const float max = body->getMaxHealth();
        ImGui::Text("Health: %.0f / %.0f", hp, max);
        if (max > 0.0f)
        {
            const float frac = hp / max;
            // Color the bar by health level: green > 60%, yellow >
            // 25%, red below. Information density per Palantir taste.
            ImVec4 c;
            if      (frac > 0.6f) c = ImVec4(0.30f, 0.90f, 0.40f, 1.0f);
            else if (frac > 0.25f) c = ImVec4(0.95f, 0.85f, 0.20f, 1.0f);
            else                   c = ImVec4(0.95f, 0.30f, 0.30f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, c);
            ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0));
            ImGui::PopStyleColor();
        }
    }
    else
    {
        ImGui::TextDisabled("(no body module)");
    }

    ImGui::Separator();

    // ---- AI state
    if (AIUpdateInterface* ai = obj->getAIUpdateInterface())
    {
        AsciiString stateName = ai->getCurrentStateName();
        const char* s = stateName.str();
        ImGui::Text("AI state: %s", (s && *s) ? s : "(unknown)");
    }
    else
    {
        ImGui::TextDisabled("(no AI module)");
    }

    // ---- Geometry
    const GeometryInfo& geom = obj->getGeometryInfo();
    ImGui::Separator();
    ImGui::Text("Geometry");
    const char* geomKind = "?";
    switch (geom.getGeomType())
    {
    case GEOMETRY_SPHERE:   geomKind = "sphere"; break;
    case GEOMETRY_CYLINDER: geomKind = "cylinder"; break;
    case GEOMETRY_BOX:      geomKind = "box"; break;
    default: break;
    }
    ImGui::Text("  Type:   %s", geomKind);
    ImGui::Text("  Major:  %.1f", geom.getMajorRadius());
    ImGui::Text("  Minor:  %.1f", geom.getMinorRadius());
    ImGui::Text("  Height: %.1f", geom.getMaxHeightAbovePosition());
}

// ----------------------------------------------------------------------------
// Properties panel
// ----------------------------------------------------------------------------
void DrawPropertiesPanel()
{
    ApplyLayout(SLOT_PROPERTIES);
    if (!ImGui::Begin("Properties", &s_visibility.properties))
    {
        ImGui::End();
        return;
    }
    RenderObjectProperties(Selection::ResolveLive());
    ImGui::End();
}

// ----------------------------------------------------------------------------
// Model debugger panel — walks the W3D render-object hierarchy of the
// currently selected drawable so we can see exactly what the renderer
// sees: per-mesh names (HOUSECOLOR meshes show up here), texture
// references, vertex/poly counts, blend modes, and the resolved house
// color used for shader-side tinting. Designed to make it obvious why
// a faction logo or unit color is rendering wrong.
// ----------------------------------------------------------------------------

// Format a packed 32-bit ARGB color as text + a tiny color swatch.
static void DrawColorSwatch(const char* label, unsigned int argb)
{
    const float a = ((argb >> 24) & 0xFF) / 255.0f;
    const float r = ((argb >> 16) & 0xFF) / 255.0f;
    const float g = ((argb >>  8) & 0xFF) / 255.0f;
    const float b = ((argb      ) & 0xFF) / 255.0f;
    ImGui::Text("%s 0x%08X (R=%u G=%u B=%u A=%u)",
        label, argb,
        (unsigned)((argb >> 16) & 0xFF),
        (unsigned)((argb >>  8) & 0xFF),
        (unsigned)( argb        & 0xFF),
        (unsigned)((argb >> 24) & 0xFF));
    ImGui::SameLine();
    ImGui::ColorButton(label,
        ImVec4(r, g, b, a > 0.0f ? a : 1.0f),
        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
        ImVec2(18, 18));
}

// Recursively walk a RenderObjClass tree, drawing collapsing-header tree
// nodes with mesh details inline. Returns the total number of meshes
// encountered so the panel header can show a summary.
static int DrawRenderObjTree(RenderObjClass* robj, int depth = 0)
{
    if (!robj || depth > 16) // depth guard against pathological cycles
        return 0;

    const char* name = robj->Get_Name();
    const int classId = robj->Class_ID();
    const char* classLabel = "?";
    switch (classId)
    {
    case RenderObjClass::CLASSID_MESH:           classLabel = "Mesh";           break;
    case RenderObjClass::CLASSID_HLOD:           classLabel = "HLOD";           break;
    case RenderObjClass::CLASSID_DISTLOD:        classLabel = "DistLOD";        break;
    case RenderObjClass::CLASSID_SEGLINE:        classLabel = "SegLine";        break;
    case RenderObjClass::CLASSID_LINE3D:         classLabel = "Line3D";         break;
    case RenderObjClass::CLASSID_DAZZLE:         classLabel = "Dazzle";         break;
    case RenderObjClass::CLASSID_PARTICLEEMITTER: classLabel = "ParticleEmitter"; break;
    case RenderObjClass::CLASSID_PARTICLEBUFFER:  classLabel = "ParticleBuffer";  break;
    case RenderObjClass::CLASSID_COLLECTION:     classLabel = "Collection";     break;
    default: break;
    }

    char header[256];
    snprintf(header, sizeof(header), "[%s] %s##robj%d_%p",
        classLabel, name ? name : "(null)", depth, (void*)robj);

    // Default-open at top level so the user immediately sees the structure.
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (depth == 0)
        nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;

    int meshCount = (classId == RenderObjClass::CLASSID_MESH) ? 1 : 0;

    if (!ImGui::TreeNodeEx(header, nodeFlags))
    {
        // Still walk children to count meshes for the parent's summary,
        // but don't display anything (the user collapsed this node).
        const int n = robj->Get_Num_Sub_Objects();
        for (int i = 0; i < n; ++i)
        {
            RenderObjClass* sub = robj->Get_Sub_Object(i);
            if (sub)
            {
                // Recurse without drawing — we just want the count.
                // (Skipping draw means the parent header is collapsed,
                // so users don't see anything we'd be redundantly
                // recursing into.)
                meshCount += (sub->Class_ID() == RenderObjClass::CLASSID_MESH) ? 1 : 0;
                sub->Release_Ref();
            }
        }
        return meshCount;
    }

    // ---- Mesh details inline ----
    if (classId == RenderObjClass::CLASSID_MESH)
    {
        MeshClass* mesh = static_cast<MeshClass*>(robj);
        if (MeshModelClass* model = mesh->Peek_Model())
        {
            ImGui::Text("Vertices:  %d", model->Get_Vertex_Count());
            ImGui::Text("Polygons:  %u", (unsigned)model->Get_Polygon_Count());

            // Detect HOUSECOLOR sub-meshes (the tinted-via-shader path
            // we already log via [HOUSECOLOR] traces). The substring
            // after the dot in the mesh name is what ComputeMeshColor
            // checks against "HOUSECOLOR".
            if (name)
            {
                const char* dotPos = strchr(name, '.');
                const char* subName = dotPos ? dotPos + 1 : name;
                if (subName && _strnicmp(subName, "HOUSECOLOR", 10) == 0)
                {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                        "  HOUSECOLOR mesh — tinted by shader");
                }
            }

            // Per-pass texture list: each pass = one diffuse texture
            // stage in the original DX8 fixed-function pipeline.
            const int passCount = model->Get_Pass_Count();
            for (int pass = 0; pass < passCount; ++pass)
            {
                if (TextureClass* tex = model->Peek_Texture(0, pass, 0))
                {
                    const char* texName = tex->Get_Texture_Name().str();
                    ImGui::Text("Pass %d tex: %s", pass, texName ? texName : "(null)");
                    if (texName && _strnicmp(texName, "ZHC", 3) == 0)
                    {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                            "[ZHC = team-color palette]");
                    }
                }
            }

            // Mesh-stored ObjectColor (only set for top-level robjs but
            // useful to dump as a sanity check on tinting flow).
            const unsigned int meshObjColor = mesh->Get_ObjectColor();
            if (meshObjColor != 0)
                DrawColorSwatch("Mesh.ObjectColor:", meshObjColor);
        }
        else
        {
            ImGui::TextDisabled("(no MeshModel — not initialized?)");
        }
    }

    // ---- Recurse into sub-objects ----
    const int n = robj->Get_Num_Sub_Objects();
    if (n > 0)
        ImGui::TextDisabled("Sub-objects: %d", n);
    for (int i = 0; i < n; ++i)
    {
        RenderObjClass* sub = robj->Get_Sub_Object(i);
        if (sub)
        {
            meshCount += DrawRenderObjTree(sub, depth + 1);
            sub->Release_Ref(); // Get_Sub_Object Add_Refs the result
        }
    }

    ImGui::TreePop();
    return meshCount;
}

void DrawModelPanel()
{
    ApplyLayout(SLOT_MODEL);
    if (!ImGui::Begin("Model", &s_visibility.model))
    {
        ImGui::End();
        return;
    }

    Object* obj = Selection::ResolveLive();
    if (!obj)
    {
        ImGui::TextDisabled("No selection.");
        ImGui::TextDisabled("Pick an object in the Hierarchy or click");
        ImGui::TextDisabled("one in the viewport with pick mode on.");
        ImGui::End();
        return;
    }

    // ---- Header: template / owner / KindOf ----
    const ThingTemplate* tmpl = obj->getTemplate();
    ImGui::TextUnformatted(tmpl ? tmpl->getName().str() : "(null template)");
    ImGui::TextDisabled("ID %u  ·  %s", (unsigned)obj->getID(), ContextKindLabel(obj));
    if (Player* p = obj->getControllingPlayer())
    {
        const ImVec4 sideColor = ColorToImVec4(p->getPlayerColor());
        ImGui::Text("Owner: ");
        ImGui::SameLine();
        ImGui::TextColored(sideColor, "%s", SafeSide(p));
        DrawColorSwatch("Player color:", p->getPlayerColor());
    }
    DrawColorSwatch("IndicatorColor:", obj->getIndicatorColor());
    ImGui::Separator();

    // ---- Walk every W3DModelDraw module on the drawable ----
    Drawable* draw = obj->getDrawable();
    if (!draw)
    {
        ImGui::TextDisabled("No drawable.");
        ImGui::End();
        return;
    }

    int totalMeshes = 0;
    int moduleIdx = 0;
    for (DrawModule** dm = draw->getDrawModules(); *dm; ++dm)
    {
        ObjectDrawInterface* odi = (*dm)->getObjectDrawInterface();
        if (!odi)
            continue;
        // Same static_cast as the renderer uses (the hot path doesn't
        // do RTTI, and only W3DModelDraw implements ObjectDrawInterface
        // in this codebase).
        W3DModelDraw* w3dDraw = static_cast<W3DModelDraw*>(odi);
        RenderObjClass* robj = w3dDraw->getRenderObject();

        char header[128];
        snprintf(header, sizeof(header), "DrawModule %d##dm%d",
            moduleIdx, moduleIdx);
        if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (!robj)
            {
                ImGui::TextDisabled("(no render object)");
            }
            else
            {
                // Top-level metadata that drives shader-side tinting.
                DrawColorSwatch("ObjectColor (m_currentObjectColor):",
                    robj->Get_ObjectColor());
                ImGui::Text("Top-level: %s", robj->Get_Name());
                ImGui::Spacing();

                totalMeshes += DrawRenderObjTree(robj);
            }
        }
        ++moduleIdx;
    }

    if (moduleIdx == 0)
        ImGui::TextDisabled("(no W3DModelDraw modules on this drawable)");
    else
        ImGui::TextDisabled("Total meshes: %d", totalMeshes);

    ImGui::End();
}

// ----------------------------------------------------------------------------
// Players panel
// ----------------------------------------------------------------------------
void DrawPlayersPanel()
{
    ApplyLayout(SLOT_PLAYERS);
    if (!ImGui::Begin("Players", &s_visibility.players))
    {
        ImGui::End();
        return;
    }

    if (ThePlayerList == nullptr)
    {
        ImGui::TextDisabled("PlayerList not initialized");
        ImGui::End();
        return;
    }

    if (ImGui::BeginTable("##players", 5,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("#",     ImGuiTableColumnFlags_WidthFixed, 28);
        ImGui::TableSetupColumn("Side",  ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Cash",  ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Power", ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();

        const Int n = ThePlayerList->getPlayerCount();
        for (Int i = 0; i < n; ++i)
        {
            Player* p = ThePlayerList->getNthPlayer(i);
            if (!p)
                continue;

            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            // Row-spanning invisible Selectable for hover detection.
            // SpanAllColumns makes the hit region cover every cell
            // in this row; AllowOverlap lets cell content still
            // intercept clicks on top of it.
            bool rowSelected = false;
            ImGui::Selectable("##row", &rowSelected,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
            const bool rowHovered = ImGui::IsItemHovered();

            ImGui::SameLine(0, 0);
            ImGui::Text("%d", (int)i);

            ImGui::TableSetColumnIndex(1);
            // Color the side label with the player's actual unit
            // color so the table doubles as a faction-color legend.
            // This matches what the user sees in-game on minimap
            // and unit hulls.
            const ImVec4 sideColor = ColorToImVec4(p->getPlayerColor());
            ImGui::TextColored(sideColor, "%s", SafeSide(p));

            ImGui::TableSetColumnIndex(2);
            if (Money* m = p->getMoney())
                ImGui::Text("%u", (unsigned)m->countMoney());
            else
                ImGui::TextDisabled("-");

            ImGui::TableSetColumnIndex(3);
            if (Energy* e = p->getEnergy())
            {
                const Int prod = e->getProduction();
                const Int cons = e->getConsumption();
                const ImVec4 col = (cons > prod)
                    ? ImVec4(1.0f, 0.5f, 0.4f, 1.0f)
                    : ImVec4(0.7f, 1.0f, 0.7f, 1.0f);
                ImGui::TextColored(col, "%d / %d", (int)cons, (int)prod);
            }
            else
            {
                ImGui::TextDisabled("-");
            }

            ImGui::TableSetColumnIndex(4);
            ImGui::TextDisabled("active");

            // Hover detection from the row-spanning Selectable above
            // — show a richer tooltip with money + power balance
            // breakdown. Palantir density: every visible row is an
            // inspectable surface.
            if (rowHovered)
            {
                ImGui::BeginTooltip();
                const ImVec4 col = ColorToImVec4(p->getPlayerColor());
                ImGui::TextColored(col, "%s", SafeSide(p));
                ImGui::TextDisabled("Player slot %d", (int)i);
                ImGui::Separator();
                if (Money* m = p->getMoney())
                    ImGui::Text("Cash:    $%u", (unsigned)m->countMoney());
                if (Energy* e = p->getEnergy())
                {
                    const Int prod = e->getProduction();
                    const Int cons = e->getConsumption();
                    const Int net  = prod - cons;
                    ImGui::Text("Power:   %d / %d", (int)cons, (int)prod);
                    if (net >= 0)
                        ImGui::TextColored(ImVec4(0.4f,1.0f,0.5f,1.0f), "  surplus +%d", (int)net);
                    else
                        ImGui::TextColored(ImVec4(1.0f,0.4f,0.4f,1.0f), "  deficit %d",  (int)net);
                }
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

// ----------------------------------------------------------------------------
// Mini-map panel — top-down faction-colored radar of every object.
// ----------------------------------------------------------------------------
//
// Strategy: walk every live Object once per frame, find the world XY
// bounds, then map each object position into the panel's local rect
// and draw a colored dot via the panel's draw list. The bounds are
// recomputed every frame so the map auto-fits as units move around
// — no static map size assumption needed. Selected and hovered
// objects get a brighter outline so they pop out of the dot field.
void DrawMinimapPanel()
{
    ApplyLayout(SLOT_MINIMAP);
    if (!ImGui::Begin("Mini-map", &s_visibility.minimap))
    {
        ImGui::End();
        return;
    }

    if (TheGameLogic == nullptr)
    {
        ImGui::TextDisabled("GameLogic not initialized");
        ImGui::End();
        return;
    }

    // Re-bake the terrain preview if the underlying heightmap data
    // pointer changed (new map load). Cheap to check each frame.
    {
        int tw = 0, th = 0, tb = 0;
        float tww = 0, twh = 0;
        const unsigned char* tdata = nullptr;
        if (InspectorGetTerrainBytes(&tw, &th, &tb, &tww, &twh, &tdata))
        {
            if (tdata != s_minimap.terrainSrcPtr)
                BakeMinimapTerrain();
        }
    }

    // Snapshot objects + compute auto-fit bounds (only used on first
    // frame to set the initial zoom).
    struct Dot { float wx, wy; uint32_t color; ObjectID id; };
    std::vector<Dot> dots;
    dots.reserve(512);
    float minX =  1e30f, minY =  1e30f;
    float maxX = -1e30f, maxY = -1e30f;
    int objCount = 0;
    for (Object* obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
    {
        const Coord3D* p = obj->getPosition();
        if (!p) continue;
        ++objCount;
        if (p->x < minX) minX = p->x;
        if (p->y < minY) minY = p->y;
        if (p->x > maxX) maxX = p->x;
        if (p->y > maxY) maxY = p->y;

        Player* owner = obj->getControllingPlayer();
        const uint32_t col = owner
            ? Render::Debug::MakeRGBA(
                (owner->getPlayerColor() >> 16) & 0xFF,
                (owner->getPlayerColor() >>  8) & 0xFF,
                (owner->getPlayerColor()      ) & 0xFF, 220)
            : Render::Debug::MakeRGBA(160, 160, 160, 200);
        dots.push_back({ p->x, p->y, col, obj->getID() });
    }

    ImGui::TextDisabled("%d objects  |  %.2fx zoom", objCount,
        s_minimap.pxPerUnit * 100.0f);

    // Canvas rect
    ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 32 || canvasSize.y < 32) { ImGui::End(); return; }
    const float side = std::min(canvasSize.x, canvasSize.y);
    canvasSize = ImVec2(side, side);
    const ImVec2 c0(canvasPos.x, canvasPos.y);
    const ImVec2 c1(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
    const ImVec2 cc(canvasPos.x + canvasSize.x * 0.5f,
                    canvasPos.y + canvasSize.y * 0.5f);

    // First-use auto-fit
    if (!s_minimap.initialized)
    {
        float fitW, fitH, cX, cY;
        if (s_minimap.terrainReady)
        {
            cX   = s_minimap.mapWorldW * 0.5f;
            cY   = s_minimap.mapWorldH * 0.5f;
            fitW = s_minimap.mapWorldW;
            fitH = s_minimap.mapWorldH;
        }
        else if (objCount > 0)
        {
            cX   = (minX + maxX) * 0.5f;
            cY   = (minY + maxY) * 0.5f;
            fitW = std::max(1.0f, maxX - minX);
            fitH = std::max(1.0f, maxY - minY);
        }
        else
        {
            cX = 0; cY = 0; fitW = 1000; fitH = 1000;
        }
        s_minimap.centerX = cX;
        s_minimap.centerY = cY;
        const float pad = 20.0f;
        const float px = (canvasSize.x - pad * 2) / fitW;
        const float py = (canvasSize.y - pad * 2) / fitH;
        s_minimap.pxPerUnit = std::min(px, py);
        if (s_minimap.pxPerUnit < 0.001f) s_minimap.pxPerUnit = 0.01f;
        s_minimap.initialized = true;
    }

    // Forward + inverse transform (zoom + pan aware)
    auto WorldToCanvas = [&](float wx, float wy) -> ImVec2 {
        return ImVec2(
            cc.x + (wx - s_minimap.centerX) * s_minimap.pxPerUnit,
            cc.y - (wy - s_minimap.centerY) * s_minimap.pxPerUnit);
    };
    auto CanvasToWorld = [&](ImVec2 sp) -> ImVec2 {
        return ImVec2(
            s_minimap.centerX + (sp.x - cc.x) / s_minimap.pxPerUnit,
            s_minimap.centerY - (sp.y - cc.y) / s_minimap.pxPerUnit);
    };

    // Claim the canvas as an interactive widget so zoom/pan events
    // target it specifically (not the whole panel).
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("##minimapCanvas", canvasSize,
        ImGuiButtonFlags_MouseButtonLeft |
        ImGuiButtonFlags_MouseButtonMiddle);
    const bool canvasHovered = ImGui::IsItemHovered();

    // ---- Pan: middle-click drag ----
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
    {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        if (d.x != 0.0f || d.y != 0.0f)
        {
            s_minimap.centerX -= d.x / s_minimap.pxPerUnit;
            s_minimap.centerY += d.y / s_minimap.pxPerUnit;
        }
    }

    // ---- Zoom: mouse wheel, anchored on cursor ----
    if (canvasHovered && ImGui::GetIO().MouseWheel != 0.0f)
    {
        const float wheel  = ImGui::GetIO().MouseWheel;
        const float factor = (wheel > 0.0f) ? 1.18f : (1.0f / 1.18f);
        const ImVec2 mp = ImGui::GetMousePos();
        const ImVec2 wBefore = CanvasToWorld(mp);
        s_minimap.pxPerUnit *= factor;
        s_minimap.pxPerUnit = std::clamp(s_minimap.pxPerUnit, 0.005f, 50.0f);
        s_minimap.centerX = wBefore.x - (mp.x - cc.x) / s_minimap.pxPerUnit;
        s_minimap.centerY = wBefore.y + (mp.y - cc.y) / s_minimap.pxPerUnit;
    }

    // ---- Background + terrain image + grid ----
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 bgCol     = IM_COL32(20, 28, 40, 230);
    const ImU32 borderCol = IM_COL32(80, 130, 180, 255);
    const ImU32 gridCol   = IM_COL32(40, 80, 120, 70);

    dl->AddRectFilled(c0, c1, bgCol, 6.0f);
    dl->PushClipRect(c0, c1, true);

    // Terrain backdrop (if loaded)
#ifdef BUILD_WITH_D3D11
    if (s_minimap.terrainReady && s_minimap.terrainTex.GetSRV())
    {
        const ImVec2 imgTL = WorldToCanvas(0.0f,              s_minimap.mapWorldH);
        const ImVec2 imgBR = WorldToCanvas(s_minimap.mapWorldW, 0.0f);
        const ImTextureID tex = (ImTextureID)(uintptr_t)s_minimap.terrainTex.GetSRV();
        dl->AddImage(tex, imgTL, imgBR);
    }
#endif

    // Subtle grid
    for (int i = 1; i < 8; ++i)
    {
        const float t = (float)i / 8.0f;
        dl->AddLine(ImVec2(c0.x + t * canvasSize.x, c0.y),
                    ImVec2(c0.x + t * canvasSize.x, c1.y), gridCol);
        dl->AddLine(ImVec2(c0.x, c0.y + t * canvasSize.y),
                    ImVec2(c1.x, c0.y + t * canvasSize.y), gridCol);
    }

    // Camera frustum overlay
    if (TheTacticalView)
    {
        Coord3D cTL{}, cTR{}, cBR{}, cBL{};
        TheTacticalView->getScreenCornerWorldPointsAtZ(&cTL, &cTR, &cBR, &cBL, 0.0f);
        const ImVec2 pTL = WorldToCanvas(cTL.x, cTL.y);
        const ImVec2 pTR = WorldToCanvas(cTR.x, cTR.y);
        const ImVec2 pBR = WorldToCanvas(cBR.x, cBR.y);
        const ImVec2 pBL = WorldToCanvas(cBL.x, cBL.y);
        dl->AddQuadFilled(pTL, pTR, pBR, pBL, IM_COL32(120, 200, 255,  35));
        dl->AddQuad(      pTL, pTR, pBR, pBL, IM_COL32(120, 200, 255, 200), 1.5f);

        // Camera world-position dot
        const Coord3D& camWorld = TheTacticalView->get3DCameraPosition();
        const ImVec2 camP = WorldToCanvas(camWorld.x, camWorld.y);
        dl->AddCircleFilled(camP, 4.0f, IM_COL32(255, 255, 255, 255));
        dl->AddCircle(camP, 6.0f, IM_COL32(120, 200, 255, 255), 16, 1.5f);
    }

    // Dots with selection / hover / tag highlights
    const uint32_t selID = Selection::GetObjectID();
    const uint32_t hovID = Selection::GetHoverID();
    for (const Dot& d : dots)
    {
        const ImVec2 sp = WorldToCanvas(d.wx, d.wy);
        const bool tagged = Selection::IsTagged((uint32_t)d.id);
        const float radius = ((uint32_t)d.id == selID) ? 5.0f
                            : ((uint32_t)d.id == hovID) ? 4.0f
                            : tagged ? 4.0f : 2.5f;
        dl->AddCircleFilled(sp, radius, d.color);
        if ((uint32_t)d.id == selID)
            dl->AddCircle(sp, radius + 2.0f, IM_COL32(255, 255, 255, 255), 12, 1.5f);
        else if ((uint32_t)d.id == hovID)
            dl->AddCircle(sp, radius + 2.0f, IM_COL32(255, 220,  60, 255), 12, 1.5f);
        else if (tagged)
            dl->AddCircle(sp, radius + 2.0f, IM_COL32(255, 255, 255, 200), 12, 1.0f);
    }

    dl->PopClipRect();
    dl->AddRect(c0, c1, borderCol, 6.0f, 0, 1.5f);

    // N compass (still fixed to top-right corner of canvas)
    {
        const ImVec2 nPos(c1.x - 18, c0.y + 18);
        dl->AddCircleFilled(nPos, 9.0f, IM_COL32(0, 0, 0, 180));
        dl->AddCircle(nPos, 9.0f, IM_COL32(180, 200, 220, 220), 16, 1.0f);
        const char* nLabel = "N";
        const ImVec2 nSize = ImGui::CalcTextSize(nLabel);
        dl->AddText(ImVec2(nPos.x - nSize.x * 0.5f, nPos.y - nSize.y * 0.5f),
            IM_COL32(220, 230, 240, 255), nLabel);
    }

    // Control hint
    dl->AddText(ImVec2(c0.x + 8, c1.y - 18),
        IM_COL32(120, 160, 200, 180),
        "wheel: zoom   mmb-drag: pan   lmb: select");

    // Hover tooltip + left-click nearest-dot select
    if (canvasHovered)
    {
        const ImVec2 mp = ImGui::GetMousePos();

        // Nearest dot under cursor
        float bestDist = 1e30f;
        ObjectID bestID = (ObjectID)0;
        const Dot* bestDot = nullptr;
        for (const Dot& d : dots)
        {
            const ImVec2 sp = WorldToCanvas(d.wx, d.wy);
            const float dx = sp.x - mp.x;
            const float dy = sp.y - mp.y;
            const float dist = dx * dx + dy * dy;
            if (dist < bestDist) { bestDist = dist; bestID = d.id; bestDot = &d; }
        }

        // Tooltip
        const ImVec2 wp = CanvasToWorld(mp);
        ImGui::BeginTooltip();
        ImGui::Text("World: %.0f, %.0f", wp.x, wp.y);
        if (bestDot && bestDist < 1600.0f)
        {
            Object* obj = TheGameLogic->findObjectByID(bestID);
            if (obj && obj->getTemplate())
            {
                ImGui::Separator();
                ImGui::TextDisabled("Nearest:");
                ImGui::Text("  %s", obj->getTemplate()->getName().str());
                if (Player* owner = obj->getControllingPlayer())
                {
                    const ImVec4 sideCol = ColorToImVec4(owner->getPlayerColor());
                    ImGui::Text("  Owner: ");
                    ImGui::SameLine();
                    ImGui::TextColored(sideCol, "%s", SafeSide(owner));
                }
                ImGui::TextDisabled("  ID %u", (unsigned)bestID);
            }
        }
        ImGui::EndTooltip();

        // Left-click commits the nearest dot as selection
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && bestDist < 400.0f)
            Selection::SetObjectID((uint32_t)bestID);
    }

    ImGui::End();
}

// ----------------------------------------------------------------------------
// Game panel — displays the off-screen render target inside an ImGui
// window. The renderer's "game viewport" mode redirects all engine
// rendering into a texture, which we sample here via ImGui::Image so
// the user can drag/resize/dock the game like any other panel and
// have side panels NOT occlude the visible game pixels.
// ----------------------------------------------------------------------------
void DrawGamePanel()
{
    ApplyLayout(SLOT_GAME);
    // No padding so the game image fills the entire window content
    // area edge-to-edge — no awkward gray border around the game.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (!ImGui::Begin("Game", &s_visibility.game,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::PopStyleVar();
        ImGui::End();
        s_gameRectValid = false;
        return;
    }

    // Use ::Render to escape Inspector::Render (the function declared in
    // Inspector.h) which would otherwise shadow the global Render namespace
    // for unqualified name lookup inside `namespace Inspector { ... }`.
    auto& renderer = ::Render::Renderer::Instance();
#ifdef BUILD_WITH_D3D11
    ID3D11ShaderResourceView* srv = renderer.GetGameViewportSRV();
#else
    void* srv = nullptr;
#endif
    const ImVec2 region = ImGui::GetContentRegionAvail();
    if (srv && region.x > 4 && region.y > 4)
    {
        // Render-target aspect ratio
        const float rtW = (float)renderer.GetGameViewportWidth();
        const float rtH = (float)renderer.GetGameViewportHeight();
        const float rtAR  = rtW / rtH;
        const float winAR = region.x / region.y;

        // Letterbox: scale the texture to fit while preserving the
        // engine's aspect ratio. The game looks correct regardless
        // of how the user resizes the panel.
        ImVec2 imgSize;
        if (winAR > rtAR)
        {
            imgSize.y = region.y;
            imgSize.x = region.y * rtAR;
        }
        else
        {
            imgSize.x = region.x;
            imgSize.y = region.x / rtAR;
        }
        // Center the image
        const ImVec2 cursor(
            ImGui::GetCursorPosX() + (region.x - imgSize.x) * 0.5f,
            ImGui::GetCursorPosY() + (region.y - imgSize.y) * 0.5f);
        ImGui::SetCursorPos(cursor);

        // Capture the IMAGE rect (in screen coords) BEFORE drawing —
        // ImGui::Image advances the cursor and we want the rect of
        // the actual game pixels, not the whole panel.
        const ImVec2 imgScreenMin = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(uintptr_t)srv, imgSize);
        const ImVec2 imgScreenMax(imgScreenMin.x + imgSize.x,
                                  imgScreenMin.y + imgSize.y);

        // Stash for the mouse-remap path in UpdatePickMode
        s_gameRectValid = true;
        s_gameRectMin   = imgScreenMin;
        s_gameRectMax   = imgScreenMax;
        s_gameRTWidth   = (int)rtW;
        s_gameRTHeight  = (int)rtH;
    }
    else
    {
        ImGui::TextDisabled("Game viewport not ready.");
        s_gameRectValid = false;
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

// ----------------------------------------------------------------------------
// Performance HUD — live frame-time graph + scoped counters
// ----------------------------------------------------------------------------
//
// Top half: wall-clock FPS / frame-ms readouts + sparkline.
//
// Bottom half: a sortable table of LIVE_PERF_SCOPE counters, populated
// by every Common/LivePerf.h scope in the engine. Reads the global
// LivePerf::GetSlots() table directly each frame — no copying, no locks
// (the engine is single-threaded). Sorting is in-place on a small index
// array so the underlying slot order stays stable across frames.
//
// Why a custom collector and not Common/PerfTimer.h? PerfTimer.h is
// gated on PERF_TIMERS which is never defined in this build (see the
// big comment at the top of LivePerf.h). LivePerf is the always-on
// replacement designed specifically to ship in release builds.
void DrawPerfHudPanel()
{
    ApplyLayout(SLOT_PERFHUD);
    if (!ImGui::Begin("Perf HUD", &s_visibility.perfHud))
    {
        ImGui::End();
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const float curMs = 1000.0f / io.Framerate;

    // Big numerical readouts
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.95f, 0.50f, 1.0f));
    ImGui::Text("%5.1f FPS", io.Framerate);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("%5.2f ms", curMs);

    // Frame-time history sparkline. ImGui::PlotLines draws a strip
    // chart from the float array. We feed it the ring buffer with
    // the write index as offset so the most recent sample is on the
    // right edge.
    ImGui::PlotLines("##frametime", s_perfFrameMs, kPerfHistory, s_perfWriteIdx,
        nullptr, 0.0f, 33.3f, ImVec2(-FLT_MIN, 60.0f));
    ImGui::TextDisabled("frame time (ms)  · 33ms cap");

    ImGui::Separator();

    // Counts
    if (TheGameLogic)
    {
        ImGui::Text("Objects:    %u", (unsigned)TheGameLogic->getObjectCount());
        ImGui::Text("GameFrame:  %u", (unsigned)TheGameLogic->getFrame());
    }
    ImGui::Text("Debug lines: %zu", Render::Debug::QueuedLineCount());

    ImGui::Separator();

    // ---- LivePerf scope counters table ---------------------------------
    //
    // Each LIVE_PERF_SCOPE("name") in the engine registers a slot in
    // the global LivePerf table; GameEngine::update() rolls per-frame
    // accumulators into displayed history once per main-loop iteration.
    // We display the snapshot here. Sorting is by an index array so the
    // underlying slot positions never move (which would break the
    // per-call-site cached pointers in the LIVE_PERF_SCOPE macro).

    // Pause / snapshot state. When paused, we display a frozen copy of
    // the slot table taken at the moment the user clicked Pause. The
    // live counters keep updating behind the scenes; the snapshot just
    // gives the user a stable view to read during a spike.
    static bool                s_paused        = false;
    static ::LivePerf::Slot    s_snapshot[::LivePerf::MAX_SLOTS];
    static int                 s_snapshotCount = 0;
    static char                s_filter[64]    = "";

    const int liveCount = ::LivePerf::GetSlotCount();
    const ::LivePerf::Slot* slots =
        s_paused ? s_snapshot : ::LivePerf::GetSlots();
    const int slotCount =
        s_paused ? s_snapshotCount : liveCount;

    if (ImGui::CollapsingHeader("Scope counters", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Top-of-section frame-budget summary. We hard-code references
        // to a few "category root" scope names (GameLogic, GameClient,
        // W3DDisplay) and show their last-frame ms inline so the user
        // gets a quick "frame budget burndown" without having to scroll
        // the table or do mental arithmetic. 33ms is the 30 Hz logic
        // tick budget — anything over that is a stall on a slow client.
        auto findSlot = [&](const char* name) -> const ::LivePerf::Slot* {
            for (int i = 0; i < liveCount; ++i) {
                const ::LivePerf::Slot* live = ::LivePerf::GetSlots() + i;
                if (std::strcmp(live->name, name) == 0) return live;
            }
            return nullptr;
        };
        const ::LivePerf::Slot* gl = findSlot("GameLogic::update");
        const ::LivePerf::Slot* gc = findSlot("GameClient::update");
        const ::LivePerf::Slot* dd = findSlot("W3DDisplay::draw");
        if (gl || gc || dd)
        {
            ImGui::TextDisabled("Frame budget (30Hz logic = 33.3 ms):");
            if (gl) {
                ImVec4 col(0.55f, 0.95f, 0.55f, 1.0f);
                if      (gl->lastFrameMs >= 25.0f) col = ImVec4(1.00f, 0.40f, 0.40f, 1.0f);
                else if (gl->lastFrameMs >= 15.0f) col = ImVec4(1.00f, 0.85f, 0.30f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::Text("  GameLogic::update  %6.2f ms  (%4.1f%% of 33ms)",
                    gl->lastFrameMs, gl->lastFrameMs * (100.0f / 33.3f));
                ImGui::PopStyleColor();
            }
            if (gc) {
                ImGui::Text("  GameClient::update %6.2f ms", gc->lastFrameMs);
            }
            if (dd) {
                ImGui::Text("  W3DDisplay::draw   %6.2f ms", dd->lastFrameMs);
            }
            ImGui::Spacing();
        }

        // Pause + Reset peaks + Snapshot now buttons + filter input.
        // Pause freezes the displayed values to the snapshot taken at the
        // moment of pause; Reset peaks zeros the running peak column;
        // the filter narrows visible rows by case-insensitive substring.
        bool prevPaused = s_paused;
        ImGui::Checkbox("Pause", &s_paused);
        if (s_paused && !prevPaused) {
            // Transition to paused — capture a snapshot of the current
            // slot table. The display will read from s_snapshot until
            // the user unchecks Pause.
            s_snapshotCount = liveCount;
            std::memcpy(s_snapshot, ::LivePerf::GetSlots(),
                (size_t)s_snapshotCount * sizeof(::LivePerf::Slot));
        }
        ImGui::SameLine();
        if (ImGui::Button("Snapshot")) {
            // Re-capture the snapshot in place (only meaningful while paused).
            s_paused = true;
            s_snapshotCount = liveCount;
            std::memcpy(s_snapshot, ::LivePerf::GetSlots(),
                (size_t)s_snapshotCount * sizeof(::LivePerf::Slot));
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset peaks"))
            ::LivePerf::ResetPeaks();
        ImGui::SameLine();
        ImGui::TextDisabled("(%d active%s)",
            liveCount, s_paused ? ", PAUSED" : "");

        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##perffilter", "filter (substring)",
            s_filter, sizeof(s_filter));

        // Sort key persists across frames so the column header sticks.
        // 0=Name, 1=Calls, 2=Last ms, 3=Avg ms, 4=Peak ms. Default is
        // Last ms descending — the user wants to see the current
        // worst offender at the top.
        static int  s_sortKey  = 2;
        static bool s_sortDesc = true;

        constexpr ImGuiTableFlags kTableFlags =
            ImGuiTableFlags_RowBg       |
            ImGuiTableFlags_Borders     |
            ImGuiTableFlags_Sortable    |
            ImGuiTableFlags_Resizable   |
            ImGuiTableFlags_ScrollY     |
            ImGuiTableFlags_SizingStretchProp;

        if (ImGui::BeginTable("##liveperf", 6, kTableFlags, ImVec2(0, 220)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Scope",
                ImGuiTableColumnFlags_WidthStretch, 3.0f, 0);
            ImGui::TableSetupColumn("Calls",
                ImGuiTableColumnFlags_WidthStretch, 1.0f, 1);
            ImGui::TableSetupColumn("Last ms",
                ImGuiTableColumnFlags_WidthStretch |
                ImGuiTableColumnFlags_DefaultSort, 1.2f, 2);
            ImGui::TableSetupColumn("Avg ms",
                ImGuiTableColumnFlags_WidthStretch, 1.2f, 3);
            // σ identifies spiky scopes — high σ relative to avg means the
            // scope is inconsistent (zone recalc, combat pathfinding, etc.).
            // Raw UTF-8 bytes for the lowercase sigma (U+03C3) so MSVC c++20's
            // char8_t-strict u8"" literals don't force a cast here.
            ImGui::TableSetupColumn("\xCF\x83 ms",
                ImGuiTableColumnFlags_WidthStretch, 1.0f, 5);
            ImGui::TableSetupColumn("Peak ms",
                ImGuiTableColumnFlags_WidthStretch, 1.2f, 4);
            ImGui::TableHeadersRow();

            // Pull the live sort spec out of ImGui — clicking a header
            // updates ColumnUserID + SortDirection here for the next frame.
            if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs())
            {
                if (specs->SpecsDirty && specs->SpecsCount > 0)
                {
                    s_sortKey  = (int)specs->Specs[0].ColumnUserID;
                    s_sortDesc = (specs->Specs[0].SortDirection == ImGuiSortDirection_Descending);
                    specs->SpecsDirty = false;
                }
            }

            // Build a sorted index array. We sort indices instead of
            // the slots themselves so the LIVE_PERF_SCOPE macro's
            // cached slot pointers stay valid forever.
            int order[::LivePerf::MAX_SLOTS];
            for (int i = 0; i < slotCount; ++i) order[i] = i;

            auto cmp = [&](int a, int b) -> bool {
                const ::LivePerf::Slot& sa = slots[a];
                const ::LivePerf::Slot& sb = slots[b];
                bool less = false;
                switch (s_sortKey) {
                    case 0: less = std::strcmp(sa.name, sb.name) < 0; break;
                    case 1: less = sa.lastCallCount < sb.lastCallCount; break;
                    case 2: less = sa.lastFrameMs   < sb.lastFrameMs;   break;
                    case 3: less = sa.avgMs         < sb.avgMs;         break;
                    case 4: less = sa.peakMs        < sb.peakMs;        break;
                    case 5: less = sa.stdDevMs      < sb.stdDevMs;      break;
                }
                return s_sortDesc ? !less : less;
            };
            // Tiny insertion sort — at most MAX_SLOTS (64). O(N²) is
            // irrelevant at this size and avoids dragging in <algorithm>.
            for (int i = 1; i < slotCount; ++i) {
                int key = order[i];
                int j = i - 1;
                while (j >= 0 && cmp(key, order[j])) {
                    order[j + 1] = order[j];
                    --j;
                }
                order[j + 1] = key;
            }

            // Helper for the substring filter — case-insensitive find.
            auto matchesFilter = [&](const char* name) -> bool {
                if (s_filter[0] == '\0') return true;
                for (const char* a = name; *a; ++a) {
                    const char* p = a;
                    const char* q = s_filter;
                    while (*p && *q &&
                           (*p | 0x20) == (*q | 0x20)) { ++p; ++q; }
                    if (*q == '\0') return true;
                }
                return false;
            };

            for (int i = 0; i < slotCount; ++i)
            {
                const ::LivePerf::Slot& s = slots[order[i]];
                if (!matchesFilter(s.name)) continue;
                ImGui::TableNextRow();

                // Color-code rows so the eye finds the hot ones fast.
                // 1ms+ = green, 3ms+ = yellow, 8ms+ = red. Sub-1ms rows
                // get the default text color so they fade to background.
                ImVec4 col(0.85f, 0.85f, 0.85f, 1.0f);
                if      (s.lastFrameMs >= 8.0f) col = ImVec4(1.00f, 0.40f, 0.40f, 1.0f);
                else if (s.lastFrameMs >= 3.0f) col = ImVec4(1.00f, 0.85f, 0.30f, 1.0f);
                else if (s.lastFrameMs >= 1.0f) col = ImVec4(0.55f, 0.95f, 0.55f, 1.0f);
                else if (s.lastFrameMs <  0.01f) col = ImVec4(0.45f, 0.45f, 0.45f, 1.0f);

                ImGui::TableSetColumnIndex(0);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(s.name);
                ImGui::PopStyleColor();

                // Per-row hover tooltip — surfaces drill-down info that
                // doesn't fit in columns. The big question for any hot
                // row is "is this slow because of high call count or
                // high per-call cost?" — show both.
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", s.name);
                    ImGui::Separator();
                    const float perCallUs =
                        (s.lastCallCount > 0)
                            ? (s.lastFrameMs * 1000.0f / (float)s.lastCallCount)
                            : 0.0f;
                    ImGui::Text("Last frame:   %6.3f ms across %d call(s)",
                        s.lastFrameMs, s.lastCallCount);
                    ImGui::Text("Per-call avg: %6.2f us",
                        perCallUs);
                    ImGui::Text("EWMA average: %6.3f ms",
                        s.avgMs);
                    ImGui::Text("EWMA stddev:  %6.3f ms",
                        s.stdDevMs);
                    // Coefficient of variation: σ/μ. CV > ~0.5 = spiky;
                    // CV near 0 = steady. Useful for ranking unstable scopes.
                    const float cv = (s.avgMs > 0.001f)
                        ? (s.stdDevMs / s.avgMs)
                        : 0.0f;
                    ImGui::Text("CV (sigma/mu): %6.3f %s",
                        cv,
                        cv >= 1.0f ? "(very spiky)"
                            : cv >= 0.5f ? "(spiky)"
                            : cv >= 0.25f ? "(variable)"
                            : "(steady)");
                    ImGui::Text("Min ever:     %6.3f ms",
                        s.minMs);
                    ImGui::Text("Peak ever:    %6.3f ms",
                        s.peakMs);
                    ImGui::Spacing();
                    ImGui::TextDisabled(
                        "ms is wall-clock for the scope, including any\n"
                        "nested LIVE_PERF_SCOPEs called from inside it.");
                    ImGui::EndTooltip();
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", s.lastCallCount);

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%6.3f", s.lastFrameMs);

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%6.3f", s.avgMs);

                // σ column — tint when CV is high so spiky scopes jump out.
                // Using column index 4 here because the table setup inserted
                // σ ms as the 5th column (user id 5) between Avg and Peak.
                ImGui::TableSetColumnIndex(4);
                {
                    const float cv = (s.avgMs > 0.001f)
                        ? (s.stdDevMs / s.avgMs)
                        : 0.0f;
                    ImVec4 sigmaCol(0.75f, 0.75f, 0.75f, 1.0f);
                    if      (cv >= 1.0f)  sigmaCol = ImVec4(1.00f, 0.40f, 0.40f, 1.0f);
                    else if (cv >= 0.5f)  sigmaCol = ImVec4(1.00f, 0.85f, 0.30f, 1.0f);
                    else if (cv >= 0.25f) sigmaCol = ImVec4(0.55f, 0.95f, 0.55f, 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, sigmaCol);
                    ImGui::Text("%6.3f", s.stdDevMs);
                    ImGui::PopStyleColor();
                }

                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%6.3f", s.peakMs);
            }

            ImGui::EndTable();
        }

        if (slotCount == 0)
        {
            ImGui::TextDisabled("No LIVE_PERF_SCOPE counters registered yet.");
            ImGui::TextDisabled("(They register on first call — load a map.)");
        }
    }

    ImGui::End();
}

// ============================================================================
// Lights debugger panel — directional light direction + color sliders
// ============================================================================
//
// Lets the user mutate the engine's directional lights at runtime, paired
// with the freecam mode for shadow / lighting parity work. Reads the live
// FrameConstants from the renderer, applies edits via SetDirectionalLights
// / SetAmbientLight, and overlays a sun-direction gizmo (a colored line
// from the camera target along -lightDir) so the freecam user can see
// where the sun is shining at any moment.
//
// Edits persist across frames because TheGameClient's setTimeOfDay
// re-applies the GlobalData INI lighting on every TOD switch — so if
// you mute the engine's per-frame TOD reset (toggle in this panel), your
// edits stick. Without the toggle, the next animation tick stomps your
// values back to whatever the INI says.
void DrawLightsPanel()
{
    ImGui::SetNextWindowSize(ImVec2(380, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Lights", &s_visibility.lights))
    {
        ImGui::End();
        return;
    }

    auto& renderer = ::Render::Renderer::Instance();
    const ::Render::FrameConstants& frame = renderer.GetFrameData();

    ImGui::TextWrapped("Edit directional sun + ambient at runtime. Pair "
        "with freecam (View → Game) for shadow debugging.");
    ImGui::Separator();

    // --- Override toggle ---
    // When off, TerrainRenderer / ModelRenderer / setTimeOfDay reapply
    // the INI-loaded lighting every frame and edits below snap back
    // instantly. Flipping this on makes edits stick. Any edit below
    // also auto-enables the override so users don't have to find this
    // first.
    bool overridden = renderer.LightsOverridden();
    if (ImGui::Checkbox("Override TOD lighting (keep my edits)", &overridden))
        renderer.SetLightsOverridden(overridden);
    ImGui::TextDisabled("Uncheck to resume engine-driven lighting.");
    ImGui::Separator();

    // --- Ambient ---
    ImGui::Text("Ambient");
    static float ambient[4] = { 0, 0, 0, 1 };
    ambient[0] = frame.ambientColor.x;
    ambient[1] = frame.ambientColor.y;
    ambient[2] = frame.ambientColor.z;
    ambient[3] = frame.ambientColor.w;
    if (ImGui::ColorEdit3("Ambient color##amb", ambient,
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
    {
        renderer.SetLightsOverridden(true);
        renderer.SetAmbientLight({ ambient[0], ambient[1], ambient[2], ambient[3] });
    }
    ImGui::Spacing();

    // --- Directional lights ---
    int dirCount = (int)frame.lightingOptions.x;
    if (dirCount < 1) dirCount = 1;
    if (dirCount > (int)::Render::kMaxDirectionalLights)
        dirCount = (int)::Render::kMaxDirectionalLights;

    ImGui::Text("Directional Lights (%d active)", dirCount);
    ImGui::Separator();

    // Local edit buffers re-synced from the renderer each frame so
    // external code (TOD changes) shows up live in the panel.
    static float dirVecs[::Render::kMaxDirectionalLights][3];
    static float dirColors[::Render::kMaxDirectionalLights][4];
    for (int i = 0; i < (int)::Render::kMaxDirectionalLights; ++i)
    {
        dirVecs[i][0] = frame.lightDirections[i].x;
        dirVecs[i][1] = frame.lightDirections[i].y;
        dirVecs[i][2] = frame.lightDirections[i].z;
        dirColors[i][0] = frame.lightColors[i].x;
        dirColors[i][1] = frame.lightColors[i].y;
        dirColors[i][2] = frame.lightColors[i].z;
        dirColors[i][3] = frame.lightColors[i].w;
    }

    bool dirty = false;
    for (int i = 0; i < dirCount; ++i)
    {
        char header[32];
        snprintf(header, sizeof(header), "Light %d", i);
        if (ImGui::TreeNodeEx(header, ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Direction sliders are degrees of azimuth + elevation,
            // converted to a unit vector. Easier to grok than raw xyz.
            float dx = dirVecs[i][0], dy = dirVecs[i][1], dz = dirVecs[i][2];
            float len = sqrtf(dx*dx + dy*dy + dz*dz);
            if (len > 1e-6f) { dx /= len; dy /= len; dz /= len; }
            float elev = asinf(-dz) * 57.29578f;       // negative because lightDir points DOWN at noon
            float azim = atan2f(dy, dx) * 57.29578f;
            bool changed = false;
            changed |= ImGui::SliderFloat(("Azimuth##" + std::to_string(i)).c_str(), &azim, -180.0f, 180.0f, "%.1f°");
            changed |= ImGui::SliderFloat(("Elevation##" + std::to_string(i)).c_str(), &elev, -90.0f, 90.0f, "%.1f°");
            if (changed)
            {
                float ce = cosf(elev * 0.01745329f);
                float se = sinf(elev * 0.01745329f);
                float ca = cosf(azim * 0.01745329f);
                float sa = sinf(azim * 0.01745329f);
                dirVecs[i][0] = ce * ca;
                dirVecs[i][1] = ce * sa;
                dirVecs[i][2] = -se;
                dirty = true;
            }
            ImGui::Text("Vector: (%.2f, %.2f, %.2f)", dirVecs[i][0], dirVecs[i][1], dirVecs[i][2]);

            if (ImGui::ColorEdit4(("Color##" + std::to_string(i)).c_str(),
                dirColors[i], ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
            {
                dirty = true;
            }
            ImGui::TreePop();
        }
    }

    if (dirty)
    {
        ::Render::Float3 dirsArr[::Render::kMaxDirectionalLights];
        ::Render::Float4 colsArr[::Render::kMaxDirectionalLights];
        for (int i = 0; i < dirCount; ++i)
        {
            dirsArr[i] = { dirVecs[i][0], dirVecs[i][1], dirVecs[i][2] };
            colsArr[i] = { dirColors[i][0], dirColors[i][1], dirColors[i][2], dirColors[i][3] };
        }
        renderer.SetLightsOverridden(true);
        renderer.SetDirectionalLights(dirsArr, colsArr, (uint32_t)dirCount);
    }

    ImGui::Separator();
    ImGui::TextWrapped("Tip: edit elevation to ~30°-60° for a typical RTS sun. "
        "Lower values stretch shadows; higher compresses them.");

    ImGui::End();
}

// ============================================================================
// Launch Parameters panel — read-only debug view of how the game booted
// ============================================================================
//
// Shows the raw OS command line, the per-flag globals the engine parsed
// out of it, and the SearchPaths state (both the raw `-path` arguments
// the launcher passed and the resolved priority order they expanded
// into). Strictly informational — no buttons, no toggles. Useful when
// the launcher hands the game a bad combination of args and you need
// to see what actually landed on the command line vs. what you thought
// you were passing.
void DrawLaunchParamsPanel()
{
    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Launch Parameters", &s_visibility.launchParams))
    {
        ImGui::End();
        return;
    }

    // ── Raw command line ──────────────────────────────────────
    if (ImGui::CollapsingHeader("Raw command line", ImGuiTreeNodeFlags_DefaultOpen))
    {
#ifdef _WIN32
        const char* cmdLine = ::GetCommandLineA();
        ImGui::TextWrapped("%s", cmdLine ? cmdLine : "(unavailable)");
        if (ImGui::SmallButton("Copy##cmdline"))
            ImGui::SetClipboardText(cmdLine ? cmdLine : "");
#else
        ImGui::TextDisabled("(only available on Windows builds)");
#endif
    }

    // ── Parsed flags / globals ────────────────────────────────
    if (ImGui::CollapsingHeader("Parsed globals", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::BeginTable("##globals", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Flag");
            ImGui::TableSetupColumn("Value");

            auto row = [](const char* k, const char* v) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(k);
                ImGui::TableNextColumn();
                if (v && *v)
                    ImGui::TextUnformatted(v);
                else
                    ImGui::TextDisabled("(unset)");
            };

            row("-relayserver", g_relayServerHost);
            row("-filtercode",  g_relayFilterCode);
            row("-mpmenu",      g_launchToMpMenu ? "TRUE (pending)" : "FALSE / consumed");
            row("-launchconfig", g_launchConfigFile.isEmpty() ? "" : g_launchConfigFile.str());
            {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", g_autotestFrames);
                row("-autotest frames", g_autotestFrames > 0 ? buf : "");
            }

            // A few GlobalData mirrors so the panel covers the
            // most-asked-about boot toggles in one place.
            if (TheGlobalData)
            {
                row("windowed",  TheGlobalData->m_windowed ? "yes" : "no");
                row("headless",  TheGlobalData->m_headless ? "yes" : "no");
                static char xbuf[16], ybuf[16];
                snprintf(xbuf, sizeof(xbuf), "%d", TheGlobalData->m_xResolution);
                snprintf(ybuf, sizeof(ybuf), "%d", TheGlobalData->m_yResolution);
                row("xres", xbuf);
                row("yres", ybuf);
            }
            ImGui::EndTable();
        }
    }

    // ── -path / paths.txt resolved entries ────────────────────
    if (ImGui::CollapsingHeader("Search paths", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto& cmdPaths  = ::SearchPaths::GetCommandLinePaths();
        const auto& resolved  = ::SearchPaths::Get();

        ImGui::Text("Source: %s",
            cmdPaths.empty() ? "paths.txt (no -path args)" : "command line (-path)");
        ImGui::Spacing();

        if (!cmdPaths.empty())
        {
            ImGui::TextDisabled("Raw -path args (%zu)", cmdPaths.size());
            for (size_t i = 0; i < cmdPaths.size(); ++i)
                ImGui::BulletText("%zu. %s", i, cmdPaths[i].c_str());
            ImGui::Spacing();
        }

        ImGui::TextDisabled("Resolved priority order (%zu)", resolved.size());
        for (size_t i = 0; i < resolved.size(); ++i)
        {
            ImGui::PushID((int)i);
            ImGui::BulletText("#%zu %s%s",
                i,
                i == 0 ? "[hi] " : "",
                resolved[i].c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("copy"))
                ImGui::SetClipboardText(resolved[i].c_str());
            ImGui::PopID();
        }
    }

    ImGui::End();
}

// ============================================================================
// Render Toggles panel — replacement for the old F9 in-game menu
// ============================================================================
//
// Drives the same g_debugDisable* globals the engine's render path
// reads each frame to gate individual passes / effects. The bool's
// semantics are "disabled when true" — we present them as positive
// "Enabled" checkboxes by inverting the meaning at the UI layer so
// the user sees a checkmark for "this thing is on", which is what
// they expect.
//
// Two tabs (Render Passes / Visual Effects) match the layout the
// old F9 menu used. A search box at the top filters by label so the
// 50+ toggles stay manageable.
void DrawRenderTogglesPanel()
{
    ImGui::SetNextWindowSize(ImVec2(380, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Render Toggles", &s_visibility.renderToggles))
    {
        ImGui::End();
        return;
    }

    // Static filter buffer — survives across frames
    static char s_filter[64] = "";
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "filter (substring)",
        s_filter, sizeof(s_filter));
    ImGui::Separator();

    // Helper that draws one toggle row. The bool stored in the engine
    // is "disabled when true" — we flip it for display so the user
    // sees "Enabled" semantics.
    struct Item { const char* label; bool* disable; };
    auto DrawItem = [](const Item& item, const char* filter) -> void {
        if (filter && filter[0] != 0)
        {
            // Case-insensitive substring filter
            const char* a = item.label;
            bool match = false;
            for (; *a; ++a)
            {
                const char* p = a;
                const char* q = filter;
                while (*p && *q &&
                       (*p | 0x20) == (*q | 0x20))
                { ++p; ++q; }
                if (*q == 0) { match = true; break; }
            }
            if (!match) return;
        }
        ImGui::PushID(item.label);
        bool enabled = !*item.disable;
        if (ImGui::Checkbox(item.label, &enabled))
            *item.disable = !enabled;
        ImGui::PopID();
    };

    if (ImGui::BeginTabBar("##renderTogglesTabs"))
    {
        // Enhancements tab FIRST so users landing on the Render Toggles
        // panel see the classic-vs-enhanced opt-in toggles before the
        // dev render-pass kill switches.
        if (ImGui::BeginTabItem("Enhancements"))
        {
            static const Item enhancements[] = {
                {"Enhanced Water (dual normals + fresnel + foam + reflection terrain)", &g_useEnhancedWater},
                {"Enhanced Particles (4-way blend + unlit shader)", &g_useEnhancedParticles},
                {"Enhanced Smudges (heat-haze refraction on explosions)", &g_useEnhancedSmudges},
            };
            ImGui::TextWrapped(
                "Opt-in visual upgrades. ALL default OFF — the renderer "
                "matches the original DX8 look with everything unchecked. "
                "Toggle individual ones to compare against classic.");
            ImGui::Separator();
            for (const Item& it : enhancements)
            {
                if (s_filter[0] != 0)
                {
                    const char* a = it.label;
                    bool match = false;
                    for (; *a; ++a)
                    {
                        const char* p = a; const char* q = s_filter;
                        while (*p && *q && (*p | 0x20) == (*q | 0x20)) { ++p; ++q; }
                        if (*q == 0) { match = true; break; }
                    }
                    if (!match) continue;
                }
                ImGui::PushID(it.label);
                ImGui::Checkbox(it.label, it.disable); // direct, not inverted
                ImGui::PopID();
            }
            ImGui::Separator();
            if (ImGui::Button("All ON"))
            {
                for (const Item& it : enhancements) *it.disable = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("All OFF (classic)"))
            {
                for (const Item& it : enhancements) *it.disable = false;
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Render Passes"))
        {
            // Mirrors the left column of the old F9 menu
            static const Item passes[] = {
                {"SkyBox",              &g_debugDisableSkyBox},
                {"Terrain",             &g_debugDisableTerrain},
                {"Roads",               &g_debugDisableRoads},
                {"Bridges",             &g_debugDisableBridges},
                {"Props (trees/rocks)", &g_debugDisableProps},
                {"Bibs (bases)",        &g_debugDisableBibs},
                {"Scorch marks",        &g_debugDisableScorch},
                {"Vehicle tracks",      &g_debugDisableTracks},
                {"Waypoints",           &g_debugDisableWaypoints},
                {"Shroud (fog of war)", &g_debugDisableShroud},
                {"Cloud shadows",       &g_debugDisableCloudShadows},
                {"Models / Units",      &g_debugDisableModels},
                {"Translucent pass",    &g_debugDisableTranslucent},
                {"Particles",           &g_debugDisableParticles},
                {"Snow",                &g_debugDisableSnow},
                {"Shadow decals",       &g_debugDisableShadowDecals},
                {"GPU shadow map",      &g_debugDisableShadowMap},
                {"Water",               &g_debugDisableWater},
                {"Water reflections",   &g_debugDisableReflection},
                {"UI (windows + HUD)",  &g_debugDisableUI},
                {"Mouse cursor",        &g_debugDisableMouse},
                {"Lighting",            &g_debugDisableLighting},
                {"drawViews()",         &g_debugDisableDrawViews},
                {"Frustum culling",     &g_debugDisableFrustumCull},
                {"FSR video upscale",   &g_debugDisableFSRVideo},
            };
            for (const Item& it : passes) DrawItem(it, s_filter);
            ImGui::Separator();
            ImGui::Checkbox("Shadow decal gizmos (green=with robj, yellow=scripted)",
                            &g_debugShadowGizmos);
            ImGui::TextDisabled("Draws a ground rectangle + X at each caster's shadow stamp site.");

            ImGui::Separator();
            ImGui::TextDisabled("Building-shadow shader debug:");
            ImGui::RadioButton("Normal",    &g_debugBuildingShadowViz, 0); ImGui::SameLine();
            ImGui::RadioButton("Cyan fill", &g_debugBuildingShadowViz, 1); ImGui::SameLine();
            ImGui::RadioButton("Magenta rings", &g_debugBuildingShadowViz, 2); ImGui::SameLine();
            ImGui::RadioButton("Red rects", &g_debugBuildingShadowViz, 3);
            ImGui::TextDisabled(
                "Cyan fill: if entire terrain turns cyan, cbuffer upload works.\n"
                "Magenta rings: each rect center gets a 50-unit circle.\n"
                "Red rects: fills each rotated rect bright red.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Visual FX"))
        {
            // Mirrors the right column of the old F9 menu
            static const Item fx[] = {
                {"Laser / Stream Glow",  &g_debugDisableLaserGlow},
                {"Tracer Streak",        &g_debugDisableTracerStreak},
                {"Volumetric Boom",      &g_debugDisableVolumetric},
                {"Modern AOE Fog",       &g_debugDisableModernAOE},
                {"Color-Aware FX",       &g_debugDisableColorAwareFX},
                {"Particle Glow",        &g_debugDisableParticleGlow},
                {"Heat Distortion",      &g_debugDisableHeatDistortion},
                {"Shockwave",            &g_debugDisableShockwave},
                {"Distance Fog",         &g_debugDisableDistanceFog},
                {"Surface Specular",     &g_debugDisableSurfaceSpec},
                {"Lens Flare",           &g_debugDisableLensFlare},
                {"God Rays",             &g_debugDisableGodRays},
                {"Chromatic Aberr.",     &g_debugDisableChromaAberration},
                {"Color Grading",        &g_debugDisableColorGrade},
                {"Sharpen",              &g_debugDisableSharpen},
                {"Smooth Particle Fade", &g_debugDisableSmoothParticleFade},
                {"Light pulses",         &g_debugDisableLightPulse},
                {"Status Circle / Fades",&g_debugDisableStatusCircle},
                {"WW3D anim clock",      &g_debugDisableWW3DSync},
                {"Track System update",  &g_debugDisableTrackUpdate},
                {"Bloom",                &g_debugDisableBloom},
                {"Volumetric Trails",    &g_debugDisableVolumetricTrails},
            };
            for (const Item& it : fx) DrawItem(it, s_filter);
            // Classic Trails uses positive semantics (g_useClassicTrails = true
            // means the classic D3D8 StreakRenderer ribbon path is active).
            // The list above inverts via `disable` semantics, so render this
            // entry directly so the checkbox state matches the label.
            {
                const char* label = "Classic Trails";
                bool show = true;
                if (s_filter[0] != 0)
                {
                    show = false;
                    for (const char* a = label; *a; ++a)
                    {
                        const char* p = a; const char* q = s_filter;
                        while (*p && *q && (*p | 0x20) == (*q | 0x20)) { ++p; ++q; }
                        if (*q == 0) { show = true; break; }
                    }
                }
                if (show)
                {
                    ImGui::PushID(label);
                    ImGui::Checkbox(label, &g_useClassicTrails);
                    ImGui::PopID();
                }
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Replaces the legacy F9 in-game menu");

    ImGui::End();
}

// ============================================================================
// Pick mode — per-frame picking + overlay rendering
// ============================================================================

// Tiny helper: try to project a world position to screen coords using
// the active tactical view. Returns false if there's no view yet
// (e.g. main menu) or the point is off-screen / behind the camera.
static bool ProjectWorldToScreen(const Coord3D& world, ImVec2* outScreen)
{
    if (!TheTacticalView)
        return false;
    ICoord2D screen = {0, 0};
    if (!TheTacticalView->worldToScreen(&world, &screen))
        return false;
    if (outScreen)
        *outScreen = ImVec2((float)screen.x, (float)screen.y);
    return true;
}

// One-shot pick at the given screen pixel. Returns the picked Object*
// (NOT Drawable*), or nullptr if nothing selectable was hit. Wraps the
// engine's RTS click-to-select machinery so we get exactly the same
// hit semantics players see in normal gameplay.
//
// Uses pickDrawableIgnoreUI rather than the regular pickDrawable so
// that the inspector can pick objects that would otherwise be
// blocked by an opaque UI window — most importantly the shell map
// (main menu) where the entire background is covered by menu UI
// windows, but also any in-game pause/options dialog. The
// "ignore UI" path runs the same ray-cast against the 3D scene as
// the normal path; it just skips the "is the cursor under a non-
// see-through window" early-out.
static Object* PickObjectAt(int screenX, int screenY)
{
    if (!TheTacticalView)
        return nullptr;
    ICoord2D pt = { screenX, screenY };
    // TheTacticalView is statically a View*, but the only concrete
    // implementation in this engine is W3DView, so the cast is safe.
    // The IgnoreUI variant is non-virtual so it requires the derived
    // type to call directly.
    W3DView* w3dView = static_cast<W3DView*>(TheTacticalView);
    Drawable* d = w3dView->pickDrawableIgnoreUI(&pt, FALSE, PICK_TYPE_SELECTABLE);
    return d ? d->getObject() : nullptr;
}

// Helper: remap an OS-window-pixel coordinate to engine-resolution
// coords when the Game window is active and the point is inside it.
// Returns true if the remap happened (point was inside the Game
// window's image rect); false if the point should be considered
// "not over the game" — in which case the caller skips picking.
static bool RemapMouseToGame(int osX, int osY, int* outX, int* outY)
{
    if (!s_gameRectValid)
    {
        // No Game window in play — pass through (legacy fullscreen
        // path). Mouse coords are already in engine resolution.
        if (outX) *outX = osX;
        if (outY) *outY = osY;
        return true;
    }
    if (osX < s_gameRectMin.x || osX > s_gameRectMax.x ||
        osY < s_gameRectMin.y || osY > s_gameRectMax.y)
    {
        // Cursor is outside the displayed game rect — this is panel
        // chrome, dock space, or empty area. Don't pick.
        return false;
    }
    const float relX = (float)osX - s_gameRectMin.x;
    const float relY = (float)osY - s_gameRectMin.y;
    const float w    = s_gameRectMax.x - s_gameRectMin.x;
    const float h    = s_gameRectMax.y - s_gameRectMin.y;
    if (w < 1.0f || h < 1.0f)
        return false;
    if (outX) *outX = (int)((relX / w) * (float)s_gameRTWidth);
    if (outY) *outY = (int)((relY / h) * (float)s_gameRTHeight);
    return true;
}

void UpdatePickMode()
{
    // Hover updates run when EITHER pick mode or tooltip mode is on
    // (and ImGui isn't grabbing the mouse). Both modes need a fresh
    // hovered object each frame — pick mode for click-to-commit,
    // tooltip mode for the floating properties popup.
    const ImGuiIO& io = ImGui::GetIO();
    const bool wantsHover = (Selection::IsPickMode() || Selection::IsTooltipMode());

    if (wantsHover && TheTacticalView)
    {
        // Only update hover when the mouse is actually inside the
        // window — ImGui reports MousePos = (-FLT_MAX, -FLT_MAX) when
        // the cursor leaves and we don't want to pick at garbage.
        // We DON'T early-out on WantCaptureMouse anymore because the
        // cursor is INSIDE the Game window which is itself an ImGui
        // panel — so WantCaptureMouse is always true. Instead we
        // gate on the Game window's content rect via RemapMouseToGame.
        if (io.MousePos.x > -1e6f && io.MousePos.y > -1e6f)
        {
            int gx = 0, gy = 0;
            if (RemapMouseToGame((int)io.MousePos.x, (int)io.MousePos.y, &gx, &gy))
            {
                Object* hovered = PickObjectAt(gx, gy);
                Selection::SetHoverID(hovered ? (uint32_t)hovered->getID() : 0);
            }
            else
            {
                Selection::SetHoverID(0);
            }
        }
        else
        {
            Selection::SetHoverID(0);
        }
    }
    else if (!wantsHover)
    {
        Selection::SetHoverID(0);
    }

    // Consume any click queued from Inspector::ProcessEvent. Two
    // flavors: NORMAL replaces the single selection, TOGGLE adds or
    // removes the picked object from the persistent tag set
    // (Ctrl-click semantics).
    int clickX = 0, clickY = 0, clickKind = 0;
    if (Selection::ConsumePendingClick(&clickX, &clickY, &clickKind))
    {
        int gx = 0, gy = 0;
        if (RemapMouseToGame(clickX, clickY, &gx, &gy))
        {
            Object* picked = PickObjectAt(gx, gy);
            const uint32_t pickedID = picked ? (uint32_t)picked->getID() : 0;
            if (clickKind == Selection::CLICK_TOGGLE)
            {
                if (pickedID != 0)
                    Selection::TagToggle(pickedID);
            }
            else
            {
                Selection::SetObjectID(pickedID);
            }
        }
    }
}

// Choose a context label for an Object based on its template flags.
// Picks one of: "Vehicle", "Infantry", "Aircraft", "Structure",
// "Projectile", "Object". Used in the hover tooltip header.
static const char* ContextKindLabel(Object* obj)
{
    if (!obj)
        return "Object";
    const ThingTemplate* t = obj->getTemplate();
    if (!t)
        return "Object";
    if (t->isKindOf(KINDOF_STRUCTURE))     return "Structure";
    if (t->isKindOf(KINDOF_INFANTRY))      return "Infantry";
    if (t->isKindOf(KINDOF_VEHICLE))       return "Vehicle";
    if (t->isKindOf(KINDOF_AIRCRAFT))      return "Aircraft";
    if (t->isKindOf(KINDOF_PROJECTILE))    return "Projectile";
    return "Object";
}

// Build a tight oriented bounding box for an Object from its
// GeometryInfo (sphere/cylinder/box) and orientation. Sphere/cylinder
// kinds become a square footprint with the major radius as the
// half-extent on X/Y. Box kinds use the actual major+minor extents.
// Height is the geometry height, with a small floor so flat sprites
// like shroud markers still get a visible box.
struct ObjectBox
{
    Render::Float3 center;
    Render::Float3 halfExtents;
    float yawZ;
};

static ObjectBox ComputeObjectBox(Object* obj)
{
    ObjectBox out{ {0,0,0}, {1,1,1}, 0.0f };
    if (!obj)
        return out;

    const Coord3D* pos = obj->getPosition();
    if (!pos)
        return out;

    const GeometryInfo& geom = obj->getGeometryInfo();
    const float major = geom.getMajorRadius();
    const float minor = geom.getMinorRadius();
    const float height = std::max(geom.getMaxHeightAbovePosition(), 8.0f);

    float hx = std::max(major, 4.0f);
    float hy = std::max(major, 4.0f);
    if (geom.getGeomType() == GEOMETRY_BOX)
    {
        hx = std::max(major, 4.0f);
        hy = std::max(minor, 4.0f);
    }
    const float hz = height * 0.5f;

    out.center      = Render::Float3{ pos->x, pos->y, pos->z + hz };
    out.halfExtents = Render::Float3{ hx, hy, hz };
    out.yawZ        = obj->getOrientation();
    return out;
}

// Convert an ARGB engine Color to the RGBA-in-memory-order uint32 the
// debug draw module wants. Falls back to a neutral white when the
// color has zero alpha (engine default for unassigned objects).
static uint32_t EngineColorToDebugRGBA(Color c, uint8_t alphaOverride = 0)
{
    const uint8_t a = alphaOverride ? alphaOverride : ((c >> 24) & 0xFF);
    const uint8_t r = (c >> 16) & 0xFF;
    const uint8_t g = (c >>  8) & 0xFF;
    const uint8_t b = (c      ) & 0xFF;
    if (r == 0 && g == 0 && b == 0)
        return Render::Debug::MakeRGBA(180, 180, 180, a ? a : 200);
    return Render::Debug::MakeRGBA(r, g, b, a ? a : 220);
}

// Push the full inspector visualization for an object into the debug
// queue: oriented bounding box, RTS-style corner brackets in a tinted
// faction color, height pole from the terrain to the box top, a
// heading arrow showing the unit's facing, and a ground crosshair at
// the object's foot. The "highlight" param controls intensity:
// 1.0 = bright (selection), 0.6 = dim (hover), 0.25 = faint
// (show-all-bounds debug overlay).
static void PushObjectVisualization(Object* obj, uint32_t accentColor,
                                    uint32_t fillColor, float highlight)
{
    if (!obj)
        return;
    const ObjectBox box = ComputeObjectBox(obj);

    using namespace Render::Debug;

    // Selection-only pulse animation. Modulates the accent alpha
    // with a slow sine so the bright wireframe gently breathes —
    // Unity-style live-selection feedback. Only applied at full
    // highlight (selection); hover and faint overlays stay constant.
    float pulseScale = 1.0f;
    if (highlight >= 0.99f)
    {
        const float t = (float)ImGui::GetTime();
        pulseScale = 0.825f + 0.175f * std::sin(t * 4.5f);
    }

    // Faded fill alpha for the full wire box (so it doesn't fight the
    // brighter corner brackets visually). Falls off with highlight.
    const uint8_t boxAlpha = (uint8_t)(160 * highlight * pulseScale);
    const uint32_t boxCol = (fillColor & 0x00FFFFFFu) | (uint32_t(boxAlpha) << 24);
    OBB(box.center, box.halfExtents, box.yawZ, boxCol);

    // RTS-style corner brackets — bright accent. Drawn at all 8
    // corners so the selection reads from any camera angle.
    const uint8_t accentAlpha = (uint8_t)(255 * highlight * pulseScale);
    const uint32_t cornerCol = (accentColor & 0x00FFFFFFu) | (uint32_t(accentAlpha) << 24);
    OBBCorners(box.center, box.halfExtents, box.yawZ, cornerCol, 0.32f);

    // Vertical "marker pole" from the object's foot up through the
    // box top — makes the selection findable on hilly terrain when
    // the box itself is hidden behind a building or hill.
    if (highlight >= 0.5f && obj->getPosition())
    {
        const Coord3D* p = obj->getPosition();
        const Render::Float3 footPos{ p->x, p->y, p->z };
        const Render::Float3 polePos{ p->x, p->y, p->z + box.halfExtents.z * 2.5f };
        Line(footPos, polePos, cornerCol);
    }

    // Selection sky beam: tall vertical pillar from high in the sky
    // straight down to the object's foot, in faction color. Makes a
    // selected object findable from any zoom level even when fully
    // off-screen — you can pan the camera around and still see the
    // beam pointing at the unit. Only at full highlight (selection),
    // not on hover or faded overlays.
    if (highlight >= 0.99f && obj->getPosition())
    {
        const Coord3D* p = obj->getPosition();
        // Faded version of the accent color so the beam doesn't
        // visually overpower other geometry.
        const uint8_t beamAlpha = (uint8_t)(140 * pulseScale);
        const uint32_t beamCol  = (accentColor & 0x00FFFFFFu) | (uint32_t(beamAlpha) << 24);
        const Render::Float3 sky { p->x, p->y, p->z + 2000.0f };
        const Render::Float3 foot{ p->x, p->y, p->z };
        Line(sky, foot, beamCol);
    }

    // Heading arrow on the ground, length scaled to the box footprint
    // so big units get long arrows and small units get short ones.
    if (highlight >= 0.5f && obj->getPosition())
    {
        const Coord3D* p = obj->getPosition();
        const float arrowLen = std::max(box.halfExtents.x, box.halfExtents.y) * 1.6f;
        const float yaw = obj->getOrientation();
        const Render::Float3 from{ p->x, p->y, p->z + 2.0f };
        const Render::Float3 to{
            p->x + std::cos(yaw) * arrowLen,
            p->y + std::sin(yaw) * arrowLen,
            p->z + 2.0f
        };
        Arrow(from, to, cornerCol);
    }

    // Ground crosshair at the object's foot — small +sign, helps
    // pinpoint exact position when terrain is rocky.
    if (highlight >= 0.5f && obj->getPosition())
    {
        const Coord3D* p = obj->getPosition();
        const float armLen = std::max(box.halfExtents.x, box.halfExtents.y) * 0.7f;
        Line({p->x - armLen, p->y, p->z + 1.0f}, {p->x + armLen, p->y, p->z + 1.0f}, cornerCol);
        Line({p->x, p->y - armLen, p->z + 1.0f}, {p->x, p->y + armLen, p->z + 1.0f}, cornerCol);
    }
}

void DrawSelectionOverlay()
{
    using namespace Render::Debug;

    // ---- World origin axis gizmo (XYZ in RGB) ----
    // Drawn first so a faint version of it sits behind everything else.
    if (g_debugFlags.showWorldOrigin)
    {
        AxisGizmo({0, 0, 0}, 200.0f);
    }

    // ---- "Show all" debug overlays ----
    // Walk every live object and draw a faint wireframe + faction-
    // tinted faded box. Bounded to a few hundred objects so a giant
    // skirmish doesn't tank the framerate from line count alone.
    if ((g_debugFlags.showAllBounds || g_debugFlags.showAllHeadings)
        && TheGameLogic != nullptr)
    {
        int budget = 600;
        for (Object* obj = TheGameLogic->getFirstObject();
             obj && budget > 0;
             obj = obj->getNextObject(), --budget)
        {
            if (g_debugFlags.showAllBounds)
            {
                Player* owner = obj->getControllingPlayer();
                const uint32_t accent = owner
                    ? EngineColorToDebugRGBA(owner->getPlayerColor(), 200)
                    : MakeRGBA(160, 160, 160, 200);
                PushObjectVisualization(obj, accent, accent, 0.25f);
            }
            if (g_debugFlags.showAllHeadings && obj->getPosition())
            {
                const Coord3D* p = obj->getPosition();
                const float yaw = obj->getOrientation();
                const float len = 12.0f;
                Render::Float3 a{ p->x, p->y, p->z + 2.0f };
                Render::Float3 b{
                    p->x + std::cos(yaw) * len,
                    p->y + std::sin(yaw) * len,
                    p->z + 2.0f
                };
                Line(a, b, MakeRGBA(255, 255, 255, 160));
            }
        }
    }

    // ---- Multi-tag set (faded faction color, persistent) ----
    // Drawn before hover/selection so brighter primary highlights
    // layer on top. Tagged objects render at 0.55 highlight — bright
    // enough to read but visually subordinate to "the" selection.
    if (TheGameLogic != nullptr)
    {
        // Snapshot tags before iterating so the loop is safe even
        // if a tagged object got destroyed this frame.
        std::vector<uint32_t> tagSnapshot;
        tagSnapshot.reserve(Selection::TagCount());
        struct CollectCtx { std::vector<uint32_t>* out; };
        CollectCtx ctx{ &tagSnapshot };
        Selection::TagForEach([](uint32_t id, void* user)
        {
            ((CollectCtx*)user)->out->push_back(id);
        }, &ctx);

        for (uint32_t tagID : tagSnapshot)
        {
            // Skip if the tag is also the current selection (so we
            // don't double-draw and clutter alpha).
            if (tagID == Selection::GetObjectID()) continue;
            Object* t = Selection::Resolve(tagID);
            if (!t) continue;
            Player* owner = t->getControllingPlayer();
            const uint32_t accent = owner
                ? EngineColorToDebugRGBA(owner->getPlayerColor(), 255)
                : MakeRGBA(80, 255, 220, 255);
            PushObjectVisualization(t, accent, accent, 0.55f);
        }
    }

    // ---- Hover (yellow) ----
    // Drawn before selection so selection lines layer on top.
    if (Object* hov = Selection::Resolve(Selection::GetHoverID()))
    {
        if (Selection::GetHoverID() != Selection::GetObjectID())
        {
            const uint32_t hoverAccent = MakeRGBA(255, 220, 60, 255);
            const uint32_t hoverFill   = MakeRGBA(255, 220, 60, 255);
            PushObjectVisualization(hov, hoverAccent, hoverFill, 0.6f);
        }
    }

    // ---- Selection (faction color, bright) ----
    if (Object* sel = Selection::ResolveLive())
    {
        Player* owner = sel->getControllingPlayer();
        // Use the player's actual color so the selection visually
        // matches the unit hull tint and minimap dot. Civilian /
        // unowned objects get a bright cyan accent instead.
        uint32_t accent;
        if (owner)
            accent = EngineColorToDebugRGBA(owner->getPlayerColor(), 255);
        else
            accent = MakeRGBA(80, 255, 220, 255);
        PushObjectVisualization(sel, accent, accent, 1.0f);

        // ---- AI target / destination lines ----
        // For units with an AIUpdateInterface, draw a red arrow to
        // the current attack target (if any) and a green arrow to
        // the current move destination. Lets you see what a unit is
        // doing without paging through state machine names.
        if (AIUpdateInterface* ai = sel->getAIUpdateInterface())
        {
            const Coord3D* selPos = sel->getPosition();
            if (selPos)
            {
                const Render::Float3 from{ selPos->x, selPos->y, selPos->z + 6.0f };

                // Attack target — red arrow + line to victim
                if (Object* victim = ai->getCurrentVictim())
                {
                    if (const Coord3D* vp = victim->getPosition())
                    {
                        const Render::Float3 to{ vp->x, vp->y, vp->z + 6.0f };
                        Arrow(from, to, MakeRGBA(255, 80, 80, 230));
                    }
                }
                else if (const Coord3D* vp = ai->getCurrentVictimPos())
                {
                    // Ground attack — line to the position
                    const Render::Float3 to{ vp->x, vp->y, vp->z + 6.0f };
                    Arrow(from, to, MakeRGBA(255, 80, 80, 230));
                }

                // Move destination — green arrow
                if (const Coord3D* dest = ai->getGoalPosition())
                {
                    // Skip if the destination is essentially the
                    // current position (idle units have a near-zero
                    // delta which would otherwise litter the scene
                    // with degenerate arrows).
                    const float dx = dest->x - selPos->x;
                    const float dy = dest->y - selPos->y;
                    if (dx*dx + dy*dy > 4.0f)
                    {
                        const Render::Float3 to{ dest->x, dest->y, dest->z + 6.0f };
                        Arrow(from, to, MakeRGBA(60, 255, 120, 230));
                    }
                }

                // Waypoint path — connected line strip showing the
                // unit's planned route through the path-finder. Same
                // green color family as the destination arrow but a
                // bit lighter so the line strip is distinguishable.
                // Each waypoint also gets a small upright tick marker
                // so individual nodes are visible against long line
                // strips.
                const Int waypointCount = ai->friend_getWaypointGoalPathSize();
                if (waypointCount > 1)
                {
                    const uint32_t pathCol = MakeRGBA(120, 255, 180, 200);
                    Render::Float3 prev{ selPos->x, selPos->y, selPos->z + 4.0f };
                    for (Int i = 0; i < waypointCount; ++i)
                    {
                        const Coord3D* wp = ai->friend_getGoalPathPosition(i);
                        if (!wp) continue;
                        const Render::Float3 cur{ wp->x, wp->y, wp->z + 4.0f };
                        Line(prev, cur, pathCol);
                        Line(cur, { cur.x, cur.y, cur.z + 8.0f }, pathCol);
                        prev = cur;
                    }
                }
            }
        }
    }

    // ---- Floating world-space labels for selection / hover / tags ---
    // Project each object's box-top to screen and draw a small text
    // chip with template name + HP bar. Drawn into the ImGui
    // foreground draw list so they sit above the engine but below
    // ImGui windows. Only when in pick or tooltip mode (so normal
    // play doesn't get cluttered with name tags everywhere).
    if (Selection::IsPickMode() || Selection::IsTooltipMode())
    {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        if (fg)
        {
            auto DrawLabelFor = [fg](Object* o, uint32_t color, bool bright)
            {
                if (!o) return;
                const ObjectBox box = ComputeObjectBox(o);
                Coord3D top{ box.center.x, box.center.y, box.center.z + box.halfExtents.z };
                ImVec2 sp;
                if (!ProjectWorldToScreen(top, &sp))
                    return;
                const char* name = "Object";
                if (const ThingTemplate* t = o->getTemplate())
                    name = t->getName().str();
                // Compose label: name and HP fraction if known.
                char buf[192];
                if (BodyModuleInterface* body = o->getBodyModule())
                {
                    const float hp  = body->getHealth();
                    const float max = body->getMaxHealth();
                    if (max > 0.0f)
                        snprintf(buf, sizeof(buf), "%s  %d%%", name, (int)(100.0f * hp / max));
                    else
                        snprintf(buf, sizeof(buf), "%s", name);
                }
                else
                {
                    snprintf(buf, sizeof(buf), "%s", name);
                }

                const ImVec2 ts = ImGui::CalcTextSize(buf);
                const ImVec2 lp(sp.x - ts.x * 0.5f, sp.y - 26.0f);
                const float padX = 5.0f, padY = 2.0f;
                const ImU32 bg = IM_COL32(0, 0, 0, bright ? 200 : 130);
                const ImU32 fg2 = IM_COL32(
                    (color      ) & 0xFF,
                    (color >>  8) & 0xFF,
                    (color >> 16) & 0xFF,
                    bright ? 255 : 200);
                fg->AddRectFilled(
                    ImVec2(lp.x - padX, lp.y - padY),
                    ImVec2(lp.x + ts.x + padX, lp.y + ts.y + padY),
                    bg, 3.0f);
                fg->AddText(lp, fg2, buf);
            };

            // Tag labels — dimmer than selection
            std::vector<uint32_t> tagSnap;
            struct LblCtx { std::vector<uint32_t>* out; };
            LblCtx lctx{ &tagSnap };
            Selection::TagForEach([](uint32_t id, void* user)
            {
                ((LblCtx*)user)->out->push_back(id);
            }, &lctx);
            for (uint32_t id : tagSnap)
            {
                if (id == Selection::GetObjectID()) continue;
                Object* o = Selection::Resolve(id);
                if (!o) continue;
                Player* owner = o->getControllingPlayer();
                const uint32_t col = owner
                    ? EngineColorToDebugRGBA(owner->getPlayerColor(), 255)
                    : MakeRGBA(80, 255, 220, 255);
                DrawLabelFor(o, col, false);
            }

            // Hover label
            if (Object* hov = Selection::Resolve(Selection::GetHoverID()))
            {
                if (Selection::GetHoverID() != Selection::GetObjectID())
                    DrawLabelFor(hov, MakeRGBA(255, 220, 60, 255), false);
            }

            // Selection label — bright, with full template name + ID
            if (Object* sel2 = Selection::ResolveLive())
            {
                Player* owner = sel2->getControllingPlayer();
                const uint32_t col = owner
                    ? EngineColorToDebugRGBA(owner->getPlayerColor(), 255)
                    : MakeRGBA(80, 255, 220, 255);
                DrawLabelFor(sel2, col, true);
            }
        }
    }
}

void DrawHoverTooltip()
{
    if (!Selection::IsPickMode() && !Selection::IsTooltipMode())
        return;
    Object* hov = Selection::Resolve(Selection::GetHoverID());
    if (!hov)
        return;

    // Tooltip mode = render the FULL Properties panel content as a
    // floating tooltip following the cursor. Pick mode without
    // tooltip mode = the smaller "click to select" header tooltip.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(280, 0), ImVec2(420, FLT_MAX));
    ImGui::BeginTooltip();

    if (Selection::IsTooltipMode())
    {
        RenderObjectProperties(hov);
        if (Selection::IsPickMode())
            ImGui::TextDisabled("Click to select");
    }
    else
    {
        // Pick-mode-only abbreviated tooltip
        const char* name = "Object";
        if (const ThingTemplate* t = hov->getTemplate())
            name = t->getName().str();
        ImGui::TextUnformatted(name);
        ImGui::TextDisabled("ID %u  ·  %s", (unsigned)hov->getID(), ContextKindLabel(hov));
        ImGui::Separator();

        if (Player* owner = hov->getControllingPlayer())
        {
            const ImVec4 sideColor = ColorToImVec4(owner->getPlayerColor());
            ImGui::Text("Owner: ");
            ImGui::SameLine();
            ImGui::TextColored(sideColor, "%s", SafeSide(owner));
        }
        if (const Coord3D* pos = hov->getPosition())
            ImGui::Text("Pos: %.0f, %.0f, %.0f", pos->x, pos->y, pos->z);
        if (BodyModuleInterface* body = hov->getBodyModule())
        {
            const float hp  = body->getHealth();
            const float max = body->getMaxHealth();
            if (max > 0.0f)
            {
                char hpLabel[64];
                snprintf(hpLabel, sizeof(hpLabel), "%.0f / %.0f", hp, max);
                ImGui::ProgressBar(hp / max, ImVec2(180, 0), hpLabel);
            }
        }
        ImGui::TextDisabled("Click to select");
    }

    ImGui::EndTooltip();
    ImGui::PopStyleVar();
}

} // anonymous namespace
} // namespace Panels

// ----------------------------------------------------------------------------
// Inspector::Log — Release-safe diagnostic push
// ----------------------------------------------------------------------------
// Format a line, push it into the Log panel ring via the Panels-scope
// LogRingPush helper, AND mirror to stderr so the same text lands in
// game_stderr.txt for out-of-process post-mortem. Unlike LogSink (which
// is the DEBUG_LOG hook and gets compiled out in Release builds), this
// function is always active so search-path / asset-resolution failures
// are visible even in a shipped build.
//
// Thread-safe: the underlying LogRingPush takes s_logMutex. Use sparingly
// from hot paths — the ring has a bounded size so spam pushes older
// lines out.
void Log(const char* fmt, ...)
{
    if (!fmt)
        return;

    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n >= (int)sizeof(buf))
        n = (int)sizeof(buf) - 1;
    // Strip a single trailing newline if the caller included one so the
    // ImGui panel doesn't render blank lines between entries.
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = '\0';

    Panels::LogRingPush(buf);
    // stderr mirror — ensures lines land in game_stderr.txt even if the
    // inspector panel isn't open. Keep the newline here only.
    fprintf(stderr, "%s\n", buf);
}

} // namespace Inspector
