// HudPanels — modernized HUD overlay panels (Resources, Radar,
// Selection, Build Queue, General Powers, Team Colors).
//
// These panels supplement the original game UI rather than replace
// it. There is intentionally no Commands panel — the engine's
// ControlBar at the bottom of the screen remains the sole command
// dispatch surface, even when the inspector overlay is active.
//
// Build Queue is interactive: right-clicking a queue entry cancels
// it via MSG_CANCEL_UNIT_CREATE with the entry's real productionID.
// Selection collapses identical templates into "N× TemplateName"
// rows so a 60-unit selection isn't a wall of duplicates.

#include "PreRTS.h"

// Engine surface ------------------------------------------------------
#include "Common/PlayerList.h"
#include "Common/Player.h"
#include "Common/Money.h"
#include "Common/Energy.h"
#include "Common/Radar.h"
#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "Common/GameCommon.h"             // VeterancyLevel
#include "Common/MessageStream.h"          // GameMessage + TheMessageStream
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameClient/Drawable.h"
#include "GameClient/View.h"

// Modern renderer (no DX8 dependency) — for baking the terrain
// preview into an off-screen texture the ImGui radar samples.
#include "Renderer.h"
#include "Core/Texture.h"
#ifdef BUILD_WITH_D3D11
#include <d3d11.h>
#endif

// HudPanels public API + ImGui ----------------------------------------
#include "HudPanels.h"
#include "imgui.h"

// Inspector bridge implemented in D3D11Shims.cpp — lets us read the
// WorldHeightMap's raw bytes without including any W3D / DX8-era
// headers from this file.
extern "C" bool InspectorGetTerrainBytes(
    int* outWidth, int* outHeight, int* outBorder,
    float* outWorldWidth, float* outWorldHeight,
    const unsigned char** outData);

// Team-color refresh: walks every render object owned by the player
// and pushes the new color via Set_ObjectColor. Required because the
// rendobj caches the color at bind time and doesn't auto-update from
// Player::m_color.
extern "C" int InspectorRefreshPlayerHouseColors(int playerIdx, unsigned int newColor);

#include <vector>
#include <unordered_map>
#include <string>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace Inspector
{
namespace Hud
{

namespace
{

Visibility s_visibility;

// ============================================================================
// Selection / helpers
// ============================================================================

Player* LocalPlayer()
{
    return ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
}

// Walk the game-logic object list and collect every object whose
// drawable is currently selected by the player.
std::vector<Object*> CollectSelectedObjects(int maxCount = 256)
{
    std::vector<Object*> sel;
    if (!TheGameLogic) return sel;
    for (Object* o = TheGameLogic->getFirstObject();
         o && (int)sel.size() < maxCount;
         o = o->getNextObject())
    {
        Drawable* d = o->getDrawable();
        if (d && d->isSelected())
            sel.push_back(o);
    }
    return sel;
}

const char* VeterancyName(VeterancyLevel l)
{
    switch (l)
    {
    case LEVEL_REGULAR: return "REG";
    case LEVEL_VETERAN: return "VET";
    case LEVEL_ELITE:   return "ELT";
    case LEVEL_HEROIC:  return "HRO";
    default:            return "?";
    }
}

ImVec4 PlayerColorVec(Player* p)
{
    if (!p) return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    Color c = p->getPlayerColor();
    const float r = ((c >> 16) & 0xFF) / 255.0f;
    const float g = ((c >>  8) & 0xFF) / 255.0f;
    const float b = ((c      ) & 0xFF) / 255.0f;
    if (r == 0.0f && g == 0.0f && b == 0.0f)
        return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    return ImVec4(r, g, b, 1.0f);
}

ImVec4 HealthColor(float frac)
{
    if (frac > 0.60f) return ImVec4(0.30f, 0.95f, 0.45f, 1.0f);
    if (frac > 0.25f) return ImVec4(0.95f, 0.85f, 0.20f, 1.0f);
    return ImVec4(0.95f, 0.30f, 0.30f, 1.0f);
}

// Palantir-style window chrome.
void BeginHudWindow(const char* title, bool* visible,
                    ImGuiWindowFlags extra = 0)
{
    ImGui::PushStyleColor(ImGuiCol_WindowBg,      ImVec4(0.04f, 0.06f, 0.10f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.30f, 0.70f, 1.00f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg,       ImVec4(0.06f, 0.10f, 0.16f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.10f, 0.20f, 0.32f, 1.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   5.0f);
    ImGui::Begin(title, visible, extra);
}

void EndHudWindow()
{
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
}

// Cancel a specific production-queue entry by its real ProductionID.
// Mirrors ControlBarCommandProcessing.cpp:497.
void DispatchCancelQueueEntry(Object* source, ProductionID id)
{
    if (!source || !TheMessageStream)
        return;
    GameMessage* msg = TheMessageStream->appendMessage(GameMessage::MSG_CANCEL_UNIT_CREATE);
    msg->appendIntegerArgument((Int)id);
    msg->appendObjectIDArgument(source->getID());
}

// ============================================================================
// HUD: Resources
// ============================================================================
void DrawResources()
{
    ImGui::SetNextWindowSize(ImVec2(340, 150), ImGuiCond_FirstUseEver);
    BeginHudWindow("HUD: Resources", &s_visibility.resources);

    Player* lp = LocalPlayer();
    if (!lp)
    {
        ImGui::TextDisabled("No local player");
        EndHudWindow();
        return;
    }

    // Cash — oversized readout
    UnsignedInt cash = lp->getMoney() ? lp->getMoney()->countMoney() : 0;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.85f, 0.20f, 1.00f));
    ImGui::TextDisabled("CREDITS");
    ImGui::Text("$ %u", (unsigned)cash);
    ImGui::PopStyleColor();

    ImGui::Separator();

    Energy* e = lp->getEnergy();
    if (e)
    {
        const Int prod = e->getProduction();
        const Int cons = e->getConsumption();
        const bool low = (cons > prod);
        const Int net  = prod - cons;

        ImGui::TextDisabled("POWER");
        // Two stacked bars: consumption (solid) inside production (outline)
        char usageLabel[64];
        snprintf(usageLabel, sizeof(usageLabel),
            "%d used / %d produced", (int)cons, (int)prod);

        const float frac = (prod > 0) ? std::min(1.0f, (float)cons / (float)prod) : 1.0f;
        const ImVec4 barCol = low ? ImVec4(0.95f, 0.30f, 0.30f, 1.0f)
                                  : ImVec4(0.30f, 0.95f, 0.50f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barCol);
        ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 20.0f), usageLabel);
        ImGui::PopStyleColor();

        // Net indicator with sign + color
        if (net >= 0)
            ImGui::TextColored(ImVec4(0.45f, 1.00f, 0.55f, 1.0f),
                "  NET  +%d  (surplus)", (int)net);
        else
            ImGui::TextColored(ImVec4(1.00f, 0.40f, 0.40f, 1.0f),
                "  NET  %d  (BROWNOUT)", (int)net);
    }
    else
    {
        ImGui::TextDisabled("(no energy data)");
    }

    EndHudWindow();
}

// ============================================================================
// HUD: Selection — grouped by template with aggregates
// ============================================================================
void DrawSelection()
{
    ImGui::SetNextWindowSize(ImVec2(400, 340), ImGuiCond_FirstUseEver);
    BeginHudWindow("HUD: Selection", &s_visibility.selection);

    auto selected = CollectSelectedObjects();
    if (selected.empty())
    {
        ImGui::TextDisabled("No selection");
        EndHudWindow();
        return;
    }

    // Group by template ID
    struct Group
    {
        const ThingTemplate* tmpl;
        const char* name;
        int   count;
        float hp;
        float maxHp;
        int   bestVet; // max veterancy in the group
    };
    std::vector<Group> groups;
    groups.reserve(8);

    float totalHp = 0, totalMax = 0;
    for (Object* o : selected)
    {
        const ThingTemplate* t = o->getTemplate();
        if (BodyModuleInterface* b = o->getBodyModule())
        {
            totalHp  += b->getHealth();
            totalMax += b->getMaxHealth();
        }
        // Find or create the group
        Group* g = nullptr;
        for (auto& gr : groups)
        {
            if (gr.tmpl == t) { g = &gr; break; }
        }
        if (!g)
        {
            Group ng;
            ng.tmpl   = t;
            ng.name   = t ? t->getName().str() : "Object";
            ng.count  = 0;
            ng.hp     = 0.0f;
            ng.maxHp  = 0.0f;
            ng.bestVet = LEVEL_REGULAR;
            groups.push_back(ng);
            g = &groups.back();
        }
        g->count++;
        if (BodyModuleInterface* b = o->getBodyModule())
        {
            g->hp    += b->getHealth();
            g->maxHp += b->getMaxHealth();
        }
        if (const ExperienceTracker* xp = o->getExperienceTracker())
        {
            if ((int)xp->getVeterancyLevel() > g->bestVet)
                g->bestVet = (int)xp->getVeterancyLevel();
        }
    }

    // Group header
    ImGui::Text("SELECTED");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.30f, 0.85f, 1.0f, 1.0f),
        "%zu unit%s in %zu group%s",
        selected.size(), selected.size() == 1 ? "" : "s",
        groups.size(),   groups.size()   == 1 ? "" : "s");

    if (totalMax > 0.0f)
    {
        const float frac = totalHp / totalMax;
        const ImVec4 col = HealthColor(frac);
        char hpBuf[64];
        snprintf(hpBuf, sizeof(hpBuf), "%.0f / %.0f", totalHp, totalMax);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
        ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 14.0f), hpBuf);
        ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // One row per group
    if (ImGui::BeginChild("##selGroups", ImVec2(0, 0)))
    {
        for (const Group& g : groups)
        {
            ImGui::PushID((const void*)g.tmpl);
            // Count × name
            ImGui::Text("%3d ×", g.count);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.85f, 0.95f, 1.0f, 1.0f), "%s", g.name);
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", VeterancyName((VeterancyLevel)g.bestVet));

            if (g.maxHp > 0.0f)
            {
                const float frac = g.hp / g.maxHp;
                const ImVec4 col = HealthColor(frac);
                char hb[64];
                snprintf(hb, sizeof(hb), "%.0f / %.0f", g.hp, g.maxHp);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
                ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 8.0f), hb);
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    EndHudWindow();
}

// ============================================================================
// HUD: Build Queue — real cancel dispatch
// ============================================================================
void DrawBuildQueue()
{
    ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
    BeginHudWindow("HUD: Build Queue", &s_visibility.buildQueue);

    auto selected = CollectSelectedObjects();
    bool any = false;

    for (Object* o : selected)
    {
        ProductionUpdateInterface* prod = o->getProductionUpdateInterface();
        if (!prod || prod->getProductionCount() == 0)
            continue;

        any = true;
        const char* bldName = "Building";
        if (const ThingTemplate* t = o->getTemplate())
            bldName = t->getName().str();

        ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), "%s", bldName);
        ImGui::SameLine();
        ImGui::TextDisabled("  (%u in queue)",
            (unsigned)prod->getProductionCount());
        ImGui::Separator();

        int idx = 0;
        for (const ProductionEntry* e = prod->firstProduction();
             e;
             e = prod->nextProduction(e), ++idx)
        {
            const ThingTemplate* tmpl = e->getProductionObject();
            const char* itemName = tmpl ? tmpl->getName().str() : "?";
            const float pct = std::min(1.0f,
                std::max(0.0f, e->getPercentComplete() * 0.01f));
            const ProductionID pid = e->getProductionID();

            ImGui::PushID(idx);

            // Progress bar + label
            const ImVec4 barCol = (idx == 0)
                ? ImVec4(0.30f, 0.95f, 0.60f, 1.0f)  // currently building → bright green
                : ImVec4(0.30f, 0.60f, 0.90f, 1.0f); // queued → blue
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barCol);
            char label[160];
            snprintf(label, sizeof(label), "%s  %.0f%%", itemName, pct * 100.0f);
            ImGui::ProgressBar(pct, ImVec2(-64.0f, 18.0f), label);
            ImGui::PopStyleColor();

            ImGui::SameLine();
            if (ImGui::SmallButton("CANCEL"))
            {
                // Dispatch the exact same cancel message the original
                // control bar does.
                DispatchCancelQueueEntry(o, pid);
            }

            ImGui::PopID();
        }
        ImGui::Spacing();
    }

    if (!any)
        ImGui::TextDisabled("Select a production building to view its queue");

    EndHudWindow();
}

// ============================================================================
// HUD: Radar — terrain backdrop + zoom/pan
// ============================================================================
//
// Coordinate model:
//   * World-space: the engine's (x, y) units. The map origin is at
//     (0, 0) and extends to (mapWorldW, mapWorldH) for the playable
//     area. Y is "up" on the ground plane (engine convention).
//   * Canvas-space: ImGui screen pixels, relative to the panel.
//
// The transform is parameterized by two state variables:
//   * centerX/Y  — world coord that sits at the canvas center
//   * pxPerUnit  — zoom (pixels per world unit)
//
// Forward transform:
//     sx = canvasCenterX + (wx - centerX) * pxPerUnit
//     sy = canvasCenterY - (wy - centerY) * pxPerUnit    (y flipped)
//
// Inverse (for cursor-anchored zoom):
//     wx = centerX + (sx - canvasCenterX) / pxPerUnit
//     wy = centerY - (sy - canvasCenterY) / pxPerUnit
//
// Mouse wheel zooms pxPerUnit around the world point under the
// cursor so the point stays stationary while zooming.
// Middle-click drag translates centerX/Y by the mouse delta.

struct RadarState
{
    bool  initialized    = false;
    float centerX        = 0.0f;
    float centerY        = 0.0f;
    float pxPerUnit      = 0.10f;
    // Baked terrain backdrop. We cache by WorldHeightMap data pointer
    // so a new map load auto-rebakes. The texture is a 256x256 RGBA8
    // grayscale-with-brown-tint preview of the heightmap.
    Render::Texture terrainTex;
    const unsigned char* terrainSrcPtr = nullptr;
    float mapWorldW = 0.0f;
    float mapWorldH = 0.0f;
    bool  terrainReady = false;
};
RadarState s_radar;

// Rebake the terrain preview texture from the current heightmap
// (256x256 grayscale-with-brown-tint). Called when the source
// heightmap pointer changes or on first use. Silently no-ops if
// no heightmap is loaded (e.g., shell map).
void BakeRadarTerrain()
{
    int w = 0, h = 0, border = 0;
    float worldW = 0, worldH = 0;
    const unsigned char* data = nullptr;
    if (!InspectorGetTerrainBytes(&w, &h, &border, &worldW, &worldH, &data))
    {
        s_radar.terrainReady  = false;
        s_radar.terrainSrcPtr = nullptr;
        return;
    }
    if (!data || w <= 2 * border || h <= 2 * border)
    {
        s_radar.terrainReady  = false;
        s_radar.terrainSrcPtr = nullptr;
        return;
    }

    const int innerW = w - 2 * border;
    const int innerH = h - 2 * border;

    // First pass: find height min/max in the playable area for
    // contrast normalization. Border cells often have clamped
    // values that would wreck the histogram.
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
            const float t = (float)(hv - hmin) / (float)hrange; // 0..1
            // Terrain palette: shadowed brown → warm highlight.
            // IM_COL32 packs as R,G,B,A in memory order.
            const uint8_t r = (uint8_t)(48  + t * 160);
            const uint8_t g = (uint8_t)(42  + t * 130);
            const uint8_t b = (uint8_t)(30  + t *  90);
            rgba[(size_t)y * kOutW + x] =
                (0xFFu << 24) | ((uint32_t)b << 16) |
                ((uint32_t)g <<  8) | (uint32_t)r;
        }
    }

    auto& device = Render::Renderer::Instance().GetDevice();
    s_radar.terrainTex.Destroy(device);
    if (!s_radar.terrainTex.CreateFromRGBA(device, rgba.data(),
            kOutW, kOutH, /*generateMips*/ false))
    {
        s_radar.terrainReady  = false;
        s_radar.terrainSrcPtr = nullptr;
        return;
    }

    s_radar.terrainSrcPtr = data;
    s_radar.mapWorldW     = worldW;
    s_radar.mapWorldH     = worldH;
    s_radar.terrainReady  = true;
}

void DrawRadar()
{
    ImGui::SetNextWindowSize(ImVec2(360, 380), ImGuiCond_FirstUseEver);
    BeginHudWindow("HUD: Radar", &s_visibility.radar);

    if (!TheGameLogic)
    {
        ImGui::TextDisabled("GameLogic not initialized");
        EndHudWindow();
        return;
    }

    // Rebake the terrain preview if the source heightmap changed
    // (new map load / campaign transition). Cheap — we compare a
    // single pointer and the bake itself is ~256KB of CPU work.
    {
        int tw = 0, th = 0, tb = 0;
        float tww = 0, twh = 0;
        const unsigned char* tdata = nullptr;
        if (InspectorGetTerrainBytes(&tw, &th, &tb, &tww, &twh, &tdata))
        {
            if (tdata != s_radar.terrainSrcPtr)
                BakeRadarTerrain();
        }
    }

    // Snapshot objects into dots + compute world-bounds for auto-fit
    struct Dot { float wx, wy; ImU32 color; bool isLocal; };
    std::vector<Dot> dots;
    dots.reserve(512);

    Player* lp = LocalPlayer();
    float minX =  1e30f, minY =  1e30f;
    float maxX = -1e30f, maxY = -1e30f;
    int counted = 0;

    for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
    {
        const Coord3D* p = o->getPosition();
        if (!p) continue;
        ++counted;
        if (p->x < minX) minX = p->x;
        if (p->y < minY) minY = p->y;
        if (p->x > maxX) maxX = p->x;
        if (p->y > maxY) maxY = p->y;

        Player* owner = o->getControllingPlayer();
        ImU32 col;
        if (owner)
        {
            const Color c = owner->getPlayerColor();
            const uint8_t r = (c >> 16) & 0xFF;
            const uint8_t g = (c >>  8) & 0xFF;
            const uint8_t b = (c      ) & 0xFF;
            col = (r == 0 && g == 0 && b == 0)
                ? IM_COL32(160, 160, 160, 220)
                : IM_COL32(r, g, b, 230);
        }
        else
        {
            col = IM_COL32(160, 160, 160, 200);
        }
        dots.push_back({ p->x, p->y, col, owner == lp });
    }

    // Canvas rect
    ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 32 || canvasSize.y < 32)
    {
        EndHudWindow();
        return;
    }
    const float side = std::min(canvasSize.x, canvasSize.y);
    canvasSize = ImVec2(side, side);
    const ImVec2 c0(canvasPos.x, canvasPos.y);
    const ImVec2 c1(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
    const ImVec2 cc(canvasPos.x + canvasSize.x * 0.5f,
                    canvasPos.y + canvasSize.y * 0.5f);

    // First-use auto-fit: center on the map (or object cloud if we
    // don't have a heightmap), set zoom so the whole thing fits the
    // canvas with a small margin.
    if (!s_radar.initialized)
    {
        float fitW, fitH, cX, cY;
        if (s_radar.terrainReady)
        {
            cX   = s_radar.mapWorldW * 0.5f;
            cY   = s_radar.mapWorldH * 0.5f;
            fitW = s_radar.mapWorldW;
            fitH = s_radar.mapWorldH;
        }
        else if (counted > 0)
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
        s_radar.centerX = cX;
        s_radar.centerY = cY;
        const float pad = 24.0f;
        const float px = (canvasSize.x - pad * 2) / fitW;
        const float py = (canvasSize.y - pad * 2) / fitH;
        s_radar.pxPerUnit = std::min(px, py);
        if (s_radar.pxPerUnit < 0.001f) s_radar.pxPerUnit = 0.01f;
        s_radar.initialized = true;
    }

    // Forward + inverse transform lambdas (zoom + pan aware)
    auto WorldToCanvas = [&](float wx, float wy) -> ImVec2 {
        return ImVec2(
            cc.x + (wx - s_radar.centerX) * s_radar.pxPerUnit,
            cc.y - (wy - s_radar.centerY) * s_radar.pxPerUnit);
    };
    auto CanvasToWorld = [&](ImVec2 sp) -> ImVec2 {
        return ImVec2(
            s_radar.centerX + (sp.x - cc.x) / s_radar.pxPerUnit,
            s_radar.centerY - (sp.y - cc.y) / s_radar.pxPerUnit);
    };

    // Reserve the canvas region and use InvisibleButton to own the
    // mouse hover / drag / wheel events for it. This lets IsItemHovered
    // and IsMouseDragging target the canvas specifically rather than
    // the entire panel window.
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("##radarcanvas", canvasSize,
        ImGuiButtonFlags_MouseButtonLeft |
        ImGuiButtonFlags_MouseButtonMiddle);
    const bool canvasHovered = ImGui::IsItemHovered();

    // ---- Pan: middle-click drag ----
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
    {
        // IO.MouseDelta is the per-frame movement. We only act on
        // it when the drag is underway on this widget, so check
        // that the drag started while we were hovered.
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        if (d.x != 0.0f || d.y != 0.0f)
        {
            s_radar.centerX -= d.x / s_radar.pxPerUnit;
            s_radar.centerY += d.y / s_radar.pxPerUnit;
        }
    }

    // ---- Zoom: wheel (cursor-anchored) ----
    if (canvasHovered && ImGui::GetIO().MouseWheel != 0.0f)
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        const float factor = (wheel > 0.0f) ? 1.18f : (1.0f / 1.18f);

        // World position under cursor BEFORE zoom
        const ImVec2 mp = ImGui::GetMousePos();
        const ImVec2 wBefore = CanvasToWorld(mp);

        s_radar.pxPerUnit *= factor;
        // Clamp so the user can't zoom to 0 or to infinity
        s_radar.pxPerUnit = std::clamp(s_radar.pxPerUnit, 0.005f, 50.0f);

        // Recompute center so the same world point stays under cursor
        // after zoom (reverse-solve the forward transform).
        s_radar.centerX = wBefore.x - (mp.x - cc.x) / s_radar.pxPerUnit;
        s_radar.centerY = wBefore.y + (mp.y - cc.y) / s_radar.pxPerUnit;
    }

    // ---- Background + terrain image ----
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 bgCol     = IM_COL32(8, 14, 22, 240);
    const ImU32 borderCol = IM_COL32(80, 180, 255, 220);
    const ImU32 gridCol   = IM_COL32(40, 80, 120, 70);

    dl->AddRectFilled(c0, c1, bgCol, 6.0f);
    dl->PushClipRect(c0, c1, true);

    // Draw the baked terrain backdrop. The image corners come from
    // projecting the map's world corners through WorldToCanvas, so
    // the terrain scrolls/zooms with pan and wheel automatically.
#ifdef BUILD_WITH_D3D11
    if (s_radar.terrainReady && s_radar.terrainTex.GetSRV())
    {
        const ImVec2 imgTL = WorldToCanvas(0.0f,               s_radar.mapWorldH);
        const ImVec2 imgBR = WorldToCanvas(s_radar.mapWorldW,  0.0f);
        const ImTextureID tex = (ImTextureID)(uintptr_t)s_radar.terrainTex.GetSRV();
        dl->AddImage(tex, imgTL, imgBR);
    }
#endif

    // Grid overlay (8x8 subdivisions, just a subtle reference)
    for (int i = 1; i < 8; ++i)
    {
        const float t = (float)i / 8.0f;
        dl->AddLine(ImVec2(c0.x + t * canvasSize.x, c0.y),
                    ImVec2(c0.x + t * canvasSize.x, c1.y), gridCol);
        dl->AddLine(ImVec2(c0.x, c0.y + t * canvasSize.y),
                    ImVec2(c1.x, c0.y + t * canvasSize.y), gridCol);
    }

    // Camera frustum
    if (TheTacticalView)
    {
        Coord3D cTL{}, cTR{}, cBR{}, cBL{};
        TheTacticalView->getScreenCornerWorldPointsAtZ(&cTL, &cTR, &cBR, &cBL, 0.0f);
        const ImVec2 pTL = WorldToCanvas(cTL.x, cTL.y);
        const ImVec2 pTR = WorldToCanvas(cTR.x, cTR.y);
        const ImVec2 pBR = WorldToCanvas(cBR.x, cBR.y);
        const ImVec2 pBL = WorldToCanvas(cBL.x, cBL.y);
        dl->AddQuadFilled(pTL, pTR, pBR, pBL, IM_COL32(120, 200, 255,  45));
        dl->AddQuad(      pTL, pTR, pBR, pBL, IM_COL32(120, 200, 255, 220), 1.5f);
    }

    // Dots
    for (const Dot& d : dots)
    {
        const ImVec2 sp = WorldToCanvas(d.wx, d.wy);
        const float radius = d.isLocal ? 3.5f : 2.5f;
        dl->AddCircleFilled(sp, radius, d.color);
        if (d.isLocal)
            dl->AddCircle(sp, radius + 1.5f, d.color, 8, 1.0f);
    }

    dl->PopClipRect();
    dl->AddRect(c0, c1, borderCol, 6.0f, 0, 1.5f);

    // Corner HUD readout: zoom + contact count + control hint
    char hud[128];
    snprintf(hud, sizeof(hud), "%d contacts  |  %.2fx", counted,
        s_radar.pxPerUnit * 100.0f);
    dl->AddText(ImVec2(c0.x + 8, c0.y + 4),
        IM_COL32(180, 220, 255, 220), hud);
    dl->AddText(ImVec2(c0.x + 8, c1.y - 18),
        IM_COL32(120, 160, 200, 180),
        "wheel: zoom   mmb-drag: pan");

    EndHudWindow();
}

// ============================================================================
// HUD: Team Colors — live color-wheel editor for every player
// ============================================================================
//
// Changes the target Player's m_color via the new public setter.
// The engine's render path reads getPlayerColor() per frame when
// tinting HOUSECOLOR meshes (W3DScene.cpp line 1425 and
// ModelRenderer.cpp line 1227), so the moment the user drags the
// color wheel, every owned unit is re-tinted on the next frame.
// No manual rebinding of materials or cached tints needed.
void DrawTeamColors()
{
    ImGui::SetNextWindowSize(ImVec2(420, 380), ImGuiCond_FirstUseEver);
    BeginHudWindow("HUD: Team Colors", &s_visibility.teamColors);

    if (!ThePlayerList)
    {
        ImGui::TextDisabled("PlayerList not initialized");
        EndHudWindow();
        return;
    }

    // Snapshot the "original" color for each player index the first
    // time we see it this session, so we can offer a Reset button
    // that restores the INI-defined team color. The cache is a
    // static std::unordered_map keyed by player index.
    static std::unordered_map<int, uint32_t> s_originalColors;

    ImGui::TextDisabled("Drag the color to re-tint every owned unit live.");
    ImGui::TextDisabled("Updates all HOUSECOLOR meshes on the next frame.");
    ImGui::Separator();

    const Int n = ThePlayerList->getPlayerCount();
    for (Int i = 0; i < n; ++i)
    {
        Player* p = ThePlayerList->getNthPlayer(i);
        if (!p) continue;

        // Skip completely-unassigned players (no side set) to keep
        // the list short. Civilian / neutral / observer are shown
        // so the user can experiment.
        AsciiString sideA = p->getSide();
        const char* side = sideA.str();

        // Cache original color on first visit
        if (s_originalColors.find((int)i) == s_originalColors.end())
            s_originalColors[(int)i] = (uint32_t)p->getPlayerColor();

        ImGui::PushID((int)i);

        // Row: index + side + color picker + reset button
        ImGui::Text("[%d]", (int)i);
        ImGui::SameLine(50);

        const ImVec4 cur = PlayerColorVec(p);
        ImGui::TextColored(cur, "%s", (side && *side) ? side : "(neutral)");

        // Convert current engine Color → float[3] RGB for ImGui
        Color engineC = p->getPlayerColor();
        float rgb[3] = {
            ((engineC >> 16) & 0xFF) / 255.0f,
            ((engineC >>  8) & 0xFF) / 255.0f,
            ((engineC      ) & 0xFF) / 255.0f,
        };

        // Inline color editor. ImGui opens a popup with the full
        // hue wheel picker when the user clicks the swatch; the
        // PickerHueWheel flag is the magic bit that makes the
        // popup use the "color wheel" style instead of the boring
        // HSV bars. NoInputs hides the RGB/Hex text entries so the
        // row stays compact — the popup has them if needed.
        ImGui::SameLine(160);
        ImGui::SetNextItemWidth(160);
        const ImGuiColorEditFlags flags =
            ImGuiColorEditFlags_NoInputs |
            ImGuiColorEditFlags_NoLabel  |
            ImGuiColorEditFlags_PickerHueWheel |
            ImGuiColorEditFlags_DisplayRGB;
        if (ImGui::ColorEdit3("##pcol", rgb, flags))
        {
            // Color changed → write it back to the Player AND walk
            // every owned object's render-object cache via the
            // C-bridge. The W3DScene per-player render group reads
            // getPlayerColor() per frame, but ModelRenderer's
            // HOUSECOLOR mesh path reads renderObj->Get_ObjectColor()
            // which is set ONCE at bind time — without the refresh
            // call most units don't actually re-tint.
            const uint32_t nr = (uint32_t)(rgb[0] * 255.0f) & 0xFF;
            const uint32_t ng = (uint32_t)(rgb[1] * 255.0f) & 0xFF;
            const uint32_t nb = (uint32_t)(rgb[2] * 255.0f) & 0xFF;
            const Color newColor = (Color)((nr << 16) | (ng << 8) | nb);
            p->setPlayerColor(newColor);
            // Force-refresh the render-object cached color so meshes
            // that go through the HOUSECOLOR fallback path also pick
            // up the new tint.
            const uint32_t argb = 0xFF000000u | (nr << 16) | (ng << 8) | nb;
            InspectorRefreshPlayerHouseColors((int)i, argb);
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Reset"))
        {
            const Color orig = (Color)s_originalColors[(int)i];
            p->setPlayerColor(orig);
            InspectorRefreshPlayerHouseColors((int)i, (uint32_t)orig | 0xFF000000u);
        }

        // Count owned units so the user can see which players
        // actually have stuff to re-tint
        int owned = 0;
        if (TheGameLogic)
        {
            for (Object* o = TheGameLogic->getFirstObject();
                 o && owned < 1000; o = o->getNextObject())
            {
                if (o->getControllingPlayer() == p)
                    ++owned;
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%d units", owned);

        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("Reset All", ImVec2(100, 0)))
    {
        for (Int i = 0; i < n; ++i)
        {
            Player* p = ThePlayerList->getNthPlayer(i);
            if (!p) continue;
            auto it = s_originalColors.find((int)i);
            if (it != s_originalColors.end())
            {
                p->setPlayerColor((Color)it->second);
                InspectorRefreshPlayerHouseColors((int)i, it->second | 0xFF000000u);
            }
        }
    }

    EndHudWindow();
}

// ============================================================================
// HUD: Generals Powers (stub — pending engine enumeration)
// ============================================================================
void DrawGenerals()
{
    ImGui::SetNextWindowSize(ImVec2(320, 200), ImGuiCond_FirstUseEver);
    BeginHudWindow("HUD: General Powers", &s_visibility.generals);

    Player* lp = LocalPlayer();
    if (!lp)
    {
        ImGui::TextDisabled("No local player");
        EndHudWindow();
        return;
    }

    ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), "GENERAL POWERS");
    ImGui::Separator();
    ImGui::TextDisabled("(special-power enumeration is a Phase 2 feature)");
    ImGui::Spacing();
    ImGui::Bullet(); ImGui::TextUnformatted("Walk owned buildings to find SpecialPowerModule");
    ImGui::Bullet(); ImGui::TextUnformatted("Display recharge bars per power");
    ImGui::Bullet(); ImGui::TextUnformatted("Click to invoke");

    EndHudWindow();
}

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================
void Init() {}
void Shutdown() {}
Visibility& GetVisibility() { return s_visibility; }

void DrawAll()
{
    if (s_visibility.resources)  DrawResources();
    if (s_visibility.radar)      DrawRadar();
    if (s_visibility.selection)  DrawSelection();
    if (s_visibility.buildQueue) DrawBuildQueue();
    if (s_visibility.generals)   DrawGenerals();
    if (s_visibility.teamColors) DrawTeamColors();
}

} // namespace Hud
} // namespace Inspector
