// AIPanels — implementation.
//
// Same compile-pattern as Panels/HudPanels: PCH disabled, manually
// include PreRTS.h, then engine headers, then ImGui. Each panel
// reads engine state through public APIs only.

#include "PreRTS.h"

// Engine surface ------------------------------------------------------
#include "Common/PlayerList.h"
#include "Common/Player.h"
#include "Common/GameCommon.h"
#include "Common/ThingTemplate.h"
#include "Common/ObjectStatusTypes.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/SidesList.h"              // BuildListInfo
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/AIPathfind.h"              // Path, PathNode
#include "GameClient/Drawable.h"

// AIPanels public API + ImGui -----------------------------------------
#include "AIPanels.h"
#include "Panels.h"      // Inspector::Selection (current object ID)
#include "DebugDraw.h"   // Render::Debug for in-world AABB visualization
#include "imgui.h"

// C-bridges in D3D11Shims.cpp — opaque queries into the W3D render
// object hierarchy without dragging WW3D / DX8-era headers in.
extern "C" bool InspectorGetModelInfo(unsigned int objID,
    char* outName, int outNameSize,
    int* outClassID, int* outNumSubObjects,
    float* outCenterX, float* outCenterY, float* outCenterZ,
    float* outExtentX, float* outExtentY, float* outExtentZ);
extern "C" bool InspectorGetModelMeshAt(unsigned int objID, int idx,
    char* outName, int outNameSize,
    int* outClassID,
    float* outCenterX, float* outCenterY, float* outCenterZ,
    float* outExtentX, float* outExtentY, float* outExtentZ);

#include <vector>
#include <unordered_map>
#include <string>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace Inspector
{
namespace Ai
{

namespace
{

Visibility s_visibility;

// ============================================================================
// Shared helpers
// ============================================================================

// Palantir-style window chrome, same vibe as HudPanels but tinted
// toward magenta instead of cyan so the AI panels are visually
// distinct from the HUD panels.
void BeginAiWindow(const char* title, bool* visible,
                   ImGuiWindowFlags extra = 0)
{
    ImGui::PushStyleColor(ImGuiCol_WindowBg,      ImVec4(0.05f, 0.04f, 0.09f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.80f, 0.35f, 0.95f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg,       ImVec4(0.12f, 0.06f, 0.16f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.20f, 0.10f, 0.26f, 1.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   4.0f);
    ImGui::Begin(title, visible, extra);
}

void EndAiWindow()
{
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
}

ImVec4 PlayerColorVec(Player* p)
{
    if (!p)
        return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    Color c = p->getPlayerColor();
    const float r = ((c >> 16) & 0xFF) / 255.0f;
    const float g = ((c >>  8) & 0xFF) / 255.0f;
    const float b = ((c      ) & 0xFF) / 255.0f;
    if (r == 0.0f && g == 0.0f && b == 0.0f)
        return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    return ImVec4(r, g, b, 1.0f);
}

const char* SafeSide(Player* p)
{
    if (!p) return "(null)";
    const char* s = p->getSide().str();
    return (s && *s) ? s : "(neutral)";
}

// Walk the game-logic object list once and return the first selected
// object with an AIUpdateInterface — the "primary" unit the various
// single-target panels inspect. Null if nothing selected or nothing
// selected has AI.
Object* FirstSelectedWithAI()
{
    if (!TheGameLogic) return nullptr;
    for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
    {
        Drawable* d = o->getDrawable();
        if (d && d->isSelected() && o->getAIUpdateInterface())
            return o;
    }
    return nullptr;
}

// Classify an object into one of the kanban activity buckets by
// inspecting its AIUpdateInterface and object status bits. Returns
// a fixed string lifetime (static constants) so the caller can use
// it as a map key.
enum ActivityBucket
{
    ACT_IDLE = 0,
    ACT_MOVING,
    ACT_ATTACKING,
    ACT_FIRING,
    ACT_CONSTRUCTING,
    ACT_REPAIRING,
    ACT_OTHER,
    ACT_COUNT
};

const char* kActivityNames[ACT_COUNT] = {
    "Idle", "Moving", "Attacking", "Firing",
    "Constructing", "Repairing", "Other"
};

ActivityBucket ClassifyActivity(Object* obj)
{
    if (!obj) return ACT_OTHER;

    const ObjectStatusMaskType& status = obj->getStatusBits();
    // Order matters: test the most "committed" states first so a
    // unit that's both moving AND firing shows up in Firing.
    if (TEST_OBJECT_STATUS_MASK(status, OBJECT_STATUS_IS_FIRING_WEAPON))
        return ACT_FIRING;
    if (TEST_OBJECT_STATUS_MASK(status, OBJECT_STATUS_UNDER_CONSTRUCTION))
        return ACT_CONSTRUCTING;
    if (TEST_OBJECT_STATUS_MASK(status, OBJECT_STATUS_UNDERGOING_REPAIR))
        return ACT_REPAIRING;

    AIUpdateInterface* ai = obj->getAIUpdateInterface();
    if (ai)
    {
        if (ai->isAttacking()) return ACT_ATTACKING;
        if (ai->isMoving())    return ACT_MOVING;
        if (ai->isIdle())      return ACT_IDLE;
    }
    return ACT_OTHER;
}

// ============================================================================
// State Machine panel
// ============================================================================
void DrawStateMachine()
{
    ImGui::SetNextWindowSize(ImVec2(400, 340), ImGuiCond_FirstUseEver);
    BeginAiWindow("AI: State Machine", &s_visibility.stateMachine);

    Object* obj = FirstSelectedWithAI();
    if (!obj)
    {
        ImGui::TextDisabled("Select a unit with an AIUpdate module");
        EndAiWindow();
        return;
    }

    // Header: object identity
    const char* name = "Object";
    if (const ThingTemplate* t = obj->getTemplate())
        name = t->getName().str();
    ImGui::TextColored(ImVec4(0.90f, 0.45f, 1.0f, 1.0f), "%s", name);
    ImGui::TextDisabled("ID %u", (unsigned)obj->getID());
    ImGui::Separator();

    // Current AI state
    AIUpdateInterface* ai = obj->getAIUpdateInterface();
    if (ai)
    {
        AsciiString stateName = ai->getCurrentStateName();
        const char* s = stateName.str();
        ImGui::Text("State:   %s", (s && *s) ? s : "(unknown)");
        ImGui::Text("StateID: %u", (unsigned)(Int)ai->getCurrentStateID());

        // Classifier flags — the high-level "what's this unit doing"
        ImGui::Spacing();
        ImGui::TextDisabled("classifiers");
        auto flagRow = [](const char* label, bool val) {
            ImGui::Text("  %s:", label);
            ImGui::SameLine(140);
            if (val) ImGui::TextColored(ImVec4(0.40f, 1.0f, 0.60f, 1.0f), "yes");
            else     ImGui::TextDisabled("no");
        };
        flagRow("isIdle",     ai->isIdle());
        flagRow("isMoving",   ai->isMoving());
        flagRow("isAttacking",ai->isAttacking());
    }
    else
    {
        ImGui::TextDisabled("(no AI update module)");
    }

    ImGui::Separator();
    ImGui::TextDisabled("object status bits");

    // Status bits — show a small checklist of the most interesting
    // flags so you can eyeball a unit's live state
    const ObjectStatusMaskType& sb = obj->getStatusBits();
    struct Flag { const char* label; ObjectStatusTypes id; };
    static const Flag flags[] = {
        { "UNDER_CONSTRUCTION", OBJECT_STATUS_UNDER_CONSTRUCTION },
        { "IS_ATTACKING",       OBJECT_STATUS_IS_ATTACKING },
        { "IS_FIRING_WEAPON",   OBJECT_STATUS_IS_FIRING_WEAPON },
        { "IS_USING_ABILITY",   OBJECT_STATUS_IS_USING_ABILITY },
        { "IS_AIMING_WEAPON",   OBJECT_STATUS_IS_AIMING_WEAPON },
        { "SOLD",               OBJECT_STATUS_SOLD },
        { "UNDERGOING_REPAIR",  OBJECT_STATUS_UNDERGOING_REPAIR },
    };
    for (const auto& f : flags)
    {
        const bool set = TEST_OBJECT_STATUS_MASK(sb, f.id);
        ImGui::Text("  %s:", f.label);
        ImGui::SameLine(190);
        if (set) ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.30f, 1.0f), "SET");
        else     ImGui::TextDisabled("-");
    }

    EndAiWindow();
}

// ============================================================================
// Pathfinder panel
// ============================================================================
void DrawPathfinder()
{
    ImGui::SetNextWindowSize(ImVec2(400, 340), ImGuiCond_FirstUseEver);
    BeginAiWindow("AI: Pathfinder", &s_visibility.pathfinder);

    Object* obj = FirstSelectedWithAI();
    if (!obj)
    {
        ImGui::TextDisabled("Select a unit with an AIUpdate module");
        EndAiWindow();
        return;
    }

    AIUpdateInterface* ai = obj->getAIUpdateInterface();
    if (!ai)
    {
        ImGui::TextDisabled("(no AI module)");
        EndAiWindow();
        return;
    }

    const Path* path = ai->getPath();
    if (!path)
    {
        ImGui::TextDisabled("No path computed");
        // Still show the waypoint goal path as an alternative view —
        // this is the high-level waypoint queue (different from the
        // low-level pathfinder output).
        const Int wpCount = ai->friend_getWaypointGoalPathSize();
        if (wpCount > 0)
        {
            ImGui::Separator();
            ImGui::Text("Waypoint path: %d nodes", (int)wpCount);
            for (Int i = 0; i < wpCount; ++i)
            {
                const Coord3D* wp = ai->friend_getGoalPathPosition(i);
                if (wp)
                    ImGui::Text("  [%2d]  %8.0f  %8.0f  %8.0f",
                        (int)i, wp->x, wp->y, wp->z);
            }
        }
        EndAiWindow();
        return;
    }

    // Walk the linked list once to count nodes
    int nodeCount = 0;
    for (PathNode* n = const_cast<Path*>(path)->getFirstNode();
         n; n = n->getNext())
        ++nodeCount;

    ImGui::Text("Path nodes: %d", nodeCount);
    ImGui::Text("Optimized length: %.1f",
        const_cast<Path*>(path)->getOptimizedTotalLength());
    ImGui::Text("Path age: %u frames", (unsigned)ai->getPathAge());
    ImGui::Separator();

    if (ImGui::BeginChild("##pathnodes", ImVec2(0, 0)))
    {
        ImGui::TextDisabled("  idx          X          Y          Z");
        int i = 0;
        for (PathNode* n = const_cast<Path*>(path)->getFirstNode();
             n; n = n->getNext(), ++i)
        {
            const Coord3D* p = n->getPosition();
            if (p)
                ImGui::Text("  %3d  %9.0f  %9.0f  %9.0f",
                    i, p->x, p->y, p->z);
        }
    }
    ImGui::EndChild();

    EndAiWindow();
}

// ============================================================================
// Kanban: Units by Activity
// ============================================================================
void DrawKanbanActivity()
{
    ImGui::SetNextWindowSize(ImVec2(820, 420), ImGuiCond_FirstUseEver);
    BeginAiWindow("AI: Kanban — Units by Activity", &s_visibility.kanbanActivity);

    if (!TheGameLogic)
    {
        ImGui::TextDisabled("GameLogic not initialized");
        EndAiWindow();
        return;
    }

    // Walk all objects once, bucket by activity. Cap per-column
    // entries so a skirmish with 500 idle peons doesn't wreck the
    // layout — show a "+N more" line at the end of the column.
    struct Card { ObjectID id; const char* name; Player* owner; };
    std::vector<Card> columns[ACT_COUNT];
    int totalByBucket[ACT_COUNT] = {};

    int considered = 0;
    for (Object* o = TheGameLogic->getFirstObject();
         o; o = o->getNextObject())
    {
        // Skip projectiles / invisible objects that don't have
        // meaningful activity classification
        if (!o->getAIUpdateInterface() &&
            !TEST_OBJECT_STATUS_MASK(o->getStatusBits(), OBJECT_STATUS_UNDER_CONSTRUCTION))
            continue;
        ++considered;
        const ActivityBucket b = ClassifyActivity(o);
        totalByBucket[b]++;
        if (columns[b].size() < 40)
        {
            Card c;
            c.id    = o->getID();
            c.name  = "?";
            if (const ThingTemplate* t = o->getTemplate())
                c.name = t->getName().str();
            c.owner = o->getControllingPlayer();
            columns[b].push_back(c);
        }
    }

    ImGui::TextDisabled("%d units considered", considered);
    ImGui::Separator();

    if (ImGui::BeginTable("##kanbanActivity", ACT_COUNT,
            ImGuiTableFlags_BordersInner | ImGuiTableFlags_SizingStretchSame |
            ImGuiTableFlags_ScrollY,
            ImVec2(0, 0)))
    {
        for (int c = 0; c < ACT_COUNT; ++c)
        {
            char hdr[64];
            snprintf(hdr, sizeof(hdr), "%s (%d)",
                kActivityNames[c], totalByBucket[c]);
            ImGui::TableSetupColumn(hdr);
        }
        ImGui::TableHeadersRow();

        // Render each column by walking the tallest column and
        // placing cards per row. With ScrollY + fixed column count
        // this reads naturally as a kanban board.
        int maxRows = 0;
        for (int c = 0; c < ACT_COUNT; ++c)
            maxRows = std::max(maxRows, (int)columns[c].size());

        for (int row = 0; row < maxRows; ++row)
        {
            ImGui::TableNextRow();
            for (int c = 0; c < ACT_COUNT; ++c)
            {
                ImGui::TableSetColumnIndex(c);
                if (row < (int)columns[c].size())
                {
                    const Card& card = columns[c][row];
                    const ImVec4 col = PlayerColorVec(card.owner);
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(col.x * 0.25f, col.y * 0.25f, col.z * 0.25f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Border,
                        ImVec4(col.x, col.y, col.z, 1.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                    // Per-card ID — without this, multiple units with the
                    // same template name in a column collide and ImGui's
                    // ConfigDebugHighlightIdConflicts pops a tooltip.
                    ImGui::PushID((int)card.id);
                    ImGui::Button(card.name, ImVec2(-FLT_MIN, 0));
                    ImGui::PopID();
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                }
                else if (row == (int)columns[c].size() &&
                         totalByBucket[c] > (int)columns[c].size())
                {
                    ImGui::TextDisabled("+%d more",
                        totalByBucket[c] - (int)columns[c].size());
                }
            }
        }
        ImGui::EndTable();
    }

    EndAiWindow();
}

// ============================================================================
// Kanban: Production (per-building queue columns)
// ============================================================================
void DrawKanbanProduction()
{
    ImGui::SetNextWindowSize(ImVec2(820, 360), ImGuiCond_FirstUseEver);
    BeginAiWindow("AI: Kanban — Production", &s_visibility.kanbanProd);

    if (!TheGameLogic)
    {
        ImGui::TextDisabled("GameLogic not initialized");
        EndAiWindow();
        return;
    }

    // Collect every object with an active production queue.
    struct BldCol
    {
        Object* obj;
        const char* bldName;
        std::vector<std::pair<const char*, float>> queue; // name, pct
    };
    std::vector<BldCol> cols;
    cols.reserve(16);

    for (Object* o = TheGameLogic->getFirstObject();
         o; o = o->getNextObject())
    {
        ProductionUpdateInterface* prod = o->getProductionUpdateInterface();
        if (!prod || prod->getProductionCount() == 0)
            continue;

        BldCol col;
        col.obj = o;
        col.bldName = "Building";
        if (const ThingTemplate* t = o->getTemplate())
            col.bldName = t->getName().str();
        for (const ProductionEntry* e = prod->firstProduction();
             e; e = prod->nextProduction(e))
        {
            const ThingTemplate* tt = e->getProductionObject();
            const char* n = tt ? tt->getName().str() : "?";
            const float pct = std::min(1.0f,
                std::max(0.0f, e->getPercentComplete() * 0.01f));
            col.queue.emplace_back(n, pct);
        }
        cols.push_back(std::move(col));
        if (cols.size() >= 12) break;
    }

    if (cols.empty())
    {
        ImGui::TextDisabled("No active production in any building");
        EndAiWindow();
        return;
    }

    ImGui::TextDisabled("%zu active production buildings", cols.size());
    ImGui::Separator();

    const int colCount = (int)cols.size();
    if (ImGui::BeginTable("##kanbanProd", colCount,
            ImGuiTableFlags_BordersInner | ImGuiTableFlags_SizingStretchSame |
            ImGuiTableFlags_ScrollY))
    {
        for (const auto& col : cols)
        {
            Player* owner = col.obj->getControllingPlayer();
            ImVec4 tint = PlayerColorVec(owner);
            // Suffix with ##objID so two buildings of the same type
            // don't produce duplicate column IDs (the "##..." portion
            // is hidden from the visible header text).
            char header[160];
            snprintf(header, sizeof(header), "%s##%u",
                col.bldName, (unsigned)col.obj->getID());
            ImGui::TableSetupColumn(header);
            (void)tint; // could apply via PushStyleColor if desired
        }
        ImGui::TableHeadersRow();

        int maxRows = 0;
        for (const auto& col : cols)
            maxRows = std::max(maxRows, (int)col.queue.size());

        for (int row = 0; row < maxRows; ++row)
        {
            ImGui::TableNextRow();
            for (int c = 0; c < colCount; ++c)
            {
                ImGui::TableSetColumnIndex(c);
                if (row < (int)cols[c].queue.size())
                {
                    const auto& item = cols[c].queue[row];
                    Player* owner = cols[c].obj->getControllingPlayer();
                    const ImVec4 col = PlayerColorVec(owner);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                        ImVec4(col.x, col.y, col.z, 1.0f));
                    char label[160];
                    snprintf(label, sizeof(label), "%s  %.0f%%",
                        item.first, item.second * 100.0f);
                    ImGui::ProgressBar(item.second,
                        ImVec2(-FLT_MIN, 14.0f), label);
                    ImGui::PopStyleColor();
                }
            }
        }
        ImGui::EndTable();
    }

    EndAiWindow();
}

// ============================================================================
// AI Build Lists (per-AI-player)
// ============================================================================
void DrawBuildLists()
{
    ImGui::SetNextWindowSize(ImVec2(500, 380), ImGuiCond_FirstUseEver);
    BeginAiWindow("AI: Build Lists", &s_visibility.buildLists);

    if (!ThePlayerList)
    {
        ImGui::TextDisabled("PlayerList not initialized");
        EndAiWindow();
        return;
    }

    int aiCount = 0;
    const Int n = ThePlayerList->getPlayerCount();
    for (Int i = 0; i < n; ++i)
    {
        Player* p = ThePlayerList->getNthPlayer(i);
        if (!p) continue;
        if (p->getPlayerType() != PLAYER_COMPUTER)
            continue;

        ++aiCount;

        // Section header per AI player
        const ImVec4 col = PlayerColorVec(p);
        ImGui::TextColored(col, "[%d]  %s", (int)i, SafeSide(p));

        BuildListInfo* list = p->getBuildList();
        if (!list)
        {
            ImGui::TextDisabled("  (empty build list)");
            ImGui::Spacing();
            continue;
        }

        int entryCount = 0;
        int built = 0;
        for (BuildListInfo* e = list; e; e = e->getNext())
        {
            ++entryCount;
            if (e->isInitiallyBuilt()) ++built;
        }
        ImGui::TextDisabled("  %d entries (%d initially built)",
            entryCount, built);
        ImGui::Separator();

        // Child region so long build lists stay inside the panel
        char childID[32];
        snprintf(childID, sizeof(childID), "##buildList%d", (int)i);
        if (ImGui::BeginChild(childID, ImVec2(0, 100), true))
        {
            for (BuildListInfo* e = list; e; e = e->getNext())
            {
                const char* tmpl = e->getTemplateName().str();
                if (!tmpl || !*tmpl) tmpl = "?";
                const Coord3D* loc = e->getLocation();
                ImGui::BulletText("%s  @ (%.0f, %.0f)",
                    tmpl,
                    loc ? loc->x : 0.0f,
                    loc ? loc->y : 0.0f);
                if (e->isInitiallyBuilt())
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.40f, 1.0f, 0.50f, 1.0f), "[built]");
                }
                if (e->getNumRebuilds() > 0)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("x%d", (int)e->getNumRebuilds());
                }
            }
        }
        ImGui::EndChild();
        ImGui::Spacing();
    }

    if (aiCount == 0)
        ImGui::TextDisabled("No AI players in this match");

    EndAiWindow();
}

// ============================================================================
// Model Debugger panel
// ============================================================================
//
// Inspects the W3D render-object hierarchy of the inspector's
// currently-selected object. Pulls data through C bridges in
// D3D11Shims.cpp (InspectorGetModelInfo / InspectorGetModelMeshAt)
// so this file stays free of any WW3D / DX8-era headers.
//
// Two visualizations:
//   1. ImGui tree listing every sub-mesh with name, class ID, and
//      world AABB center+extent
//   2. In-world AABB rendering via DebugDraw, one box per sub-mesh
//      in a cycling palette so individual meshes are easy to pick
//      out — toggleable via the "Visualize in 3D" checkbox

struct ModelDebugState
{
    bool visualize3D = true;
    bool showRootBox = true;
    bool showSubBoxes = true;
};
ModelDebugState s_modelDbg;

void DrawModelDebugger()
{
    ImGui::SetNextWindowSize(ImVec2(460, 420), ImGuiCond_FirstUseEver);
    BeginAiWindow("AI: Model Debugger", &s_visibility.modelDebugger);

    const uint32_t selID = Selection::GetObjectID();
    if (selID == 0)
    {
        ImGui::TextDisabled("Select an object (Pick mode → click) to inspect its model");
        EndAiWindow();
        return;
    }

    // Top-level info via the bridge
    char modelName[64] = {};
    int classID = 0;
    int numSub  = 0;
    float aCx, aCy, aCz, aEx, aEy, aEz;
    if (!InspectorGetModelInfo(selID,
            modelName, (int)sizeof(modelName),
            &classID, &numSub,
            &aCx, &aCy, &aCz, &aEx, &aEy, &aEz))
    {
        ImGui::TextDisabled("(no W3DModelDraw render object on selection)");
        ImGui::TextDisabled("This object may use a different draw module");
        ImGui::TextDisabled("(W3DDefaultDraw, W3DDebrisDraw, particle, ...)");
        EndAiWindow();
        return;
    }

    // Header
    ImGui::TextColored(ImVec4(0.95f, 0.55f, 1.0f, 1.0f), "%s", modelName);
    ImGui::TextDisabled("class %d  ·  %d sub-objects", classID, numSub);
    ImGui::Spacing();
    ImGui::Text("AABB center  %.1f  %.1f  %.1f", aCx, aCy, aCz);
    ImGui::Text("AABB extent  %.1f  %.1f  %.1f", aEx, aEy, aEz);
    ImGui::Text("AABB volume  %.0f",
        8.0f * aEx * aEy * aEz);

    ImGui::Separator();
    ImGui::Checkbox("Visualize in 3D",       &s_modelDbg.visualize3D);
    if (s_modelDbg.visualize3D)
    {
        ImGui::SameLine();
        ImGui::Checkbox("Root",  &s_modelDbg.showRootBox);
        ImGui::SameLine();
        ImGui::Checkbox("Sub",   &s_modelDbg.showSubBoxes);
    }
    ImGui::Separator();

    // Walk sub-meshes — cap to keep the panel responsive on huge LODs.
    constexpr int kMaxMeshes = 128;
    struct Sub { char name[64]; int classID; float cx,cy,cz, ex,ey,ez; };
    Sub subs[kMaxMeshes];
    int subCount = 0;

    const int probeLimit = numSub < kMaxMeshes ? numSub : kMaxMeshes;
    for (int i = 0; i < probeLimit; ++i)
    {
        Sub& s = subs[subCount];
        if (InspectorGetModelMeshAt(selID, i,
                s.name, (int)sizeof(s.name),
                &s.classID,
                &s.cx, &s.cy, &s.cz,
                &s.ex, &s.ey, &s.ez))
        {
            ++subCount;
        }
    }

    // Tree list of sub-meshes
    if (ImGui::BeginChild("##submeshes", ImVec2(0, 0)))
    {
        for (int i = 0; i < subCount; ++i)
        {
            const Sub& s = subs[i];
            ImGui::PushID(i);

            // Color the bullet to match the 3D box color so the user
            // can correlate "this row → that box in the world"
            using namespace Render::Debug;
            const uint32_t palette[8] = {
                kRed, kGreen, kBlue, kYellow,
                kCyan, kMagenta, kOrange, kWhite
            };
            const uint32_t col = palette[i % 8];
            const ImVec4 colVec(
                ((col      ) & 0xFF) / 255.0f,
                ((col >>  8) & 0xFF) / 255.0f,
                ((col >> 16) & 0xFF) / 255.0f,
                1.0f);
            ImGui::TextColored(colVec, "##");
            ImGui::SameLine();
            ImGui::Text("%-24s", s.name[0] ? s.name : "(unnamed)");
            ImGui::SameLine();
            ImGui::TextDisabled("[c%d]", s.classID);
            ImGui::Indent();
            ImGui::TextDisabled("center  %.1f  %.1f  %.1f", s.cx, s.cy, s.cz);
            ImGui::TextDisabled("extent  %.1f  %.1f  %.1f", s.ex, s.ey, s.ez);
            ImGui::Unindent();
            ImGui::PopID();
        }
        if (subCount == 0)
            ImGui::TextDisabled("(no sub-meshes)");
    }
    ImGui::EndChild();

    // ---- 3D visualization via Render::Debug ----
    // Drawn every frame the panel is visible. The user can toggle
    // root vs per-sub independently. Each sub-mesh box is colored
    // from a cycling palette so individual meshes are easy to spot.
    if (s_modelDbg.visualize3D)
    {
        using namespace Render::Debug;
        const uint32_t palette[8] = {
            kRed, kGreen, kBlue, kYellow,
            kCyan, kMagenta, kOrange, kWhite
        };

        if (s_modelDbg.showRootBox)
        {
            const Render::Float3 mn{ aCx - aEx, aCy - aEy, aCz - aEz };
            const Render::Float3 mx{ aCx + aEx, aCy + aEy, aCz + aEz };
            // Bright magenta for the root, distinct from any sub-box color
            AABB(mn, mx, MakeRGBA(255, 80, 240, 255));
        }

        if (s_modelDbg.showSubBoxes)
        {
            for (int i = 0; i < subCount; ++i)
            {
                const Sub& s = subs[i];
                // Skip degenerate / empty boxes (bones with no
                // visual content have zero extent and would just
                // clutter the view)
                if (s.ex < 0.01f && s.ey < 0.01f && s.ez < 0.01f)
                    continue;
                const Render::Float3 mn{ s.cx - s.ex, s.cy - s.ey, s.cz - s.ez };
                const Render::Float3 mx{ s.cx + s.ex, s.cy + s.ey, s.cz + s.ez };
                AABB(mn, mx, palette[i % 8]);
            }
        }
    }

    EndAiWindow();
}

// ============================================================================
// Entity Gizmos panel
// ============================================================================
//
// In-world per-unit visualization of AI/pathfinding decisions. Each
// frame the panel walks the object list and submits debug-line
// primitives for whichever layers are enabled. Designed to be
// readable with hundreds of units active — line budget is comfortably
// inside Render::Debug::kMaxLineVertices.

enum GizmoScope
{
    SCOPE_SELECTED = 0,
    SCOPE_FRIENDLY,
    SCOPE_ALL,
    SCOPE_COUNT
};
const char* kScopeNames[SCOPE_COUNT] = { "Selected only", "Local player", "All units" };

enum GizmoColorMode
{
    COLOR_BY_PLAYER = 0,
    COLOR_BY_STATE,
    COLOR_MODE_COUNT
};
const char* kColorModeNames[COLOR_MODE_COUNT] = { "By player", "By state" };

struct EntityGizmoState
{
    int  scope         = SCOPE_ALL;
    int  colorMode     = COLOR_BY_STATE;
    bool showPath      = true;   // low-level pathfinder linked list
    bool showWaypoints = true;   // high-level goal-path waypoints
    bool showGoal      = true;   // arrow from current pos to goal pos
    bool showTarget    = true;   // arrow to attack target object
    bool showVision    = false;  // ground circle = vision range
    bool showFacing    = true;   // short arrow showing orientation
    bool showSelfMark  = true;   // small cross at unit position
};
EntityGizmoState s_giz;

inline Render::Float3 ToFloat3(const Coord3D& c)
{
    return Render::Float3{ c.x, c.y, c.z };
}

// Engine player color → packed RGBA for Render::Debug. Mirrors
// PlayerColorVec but returns the format DebugDraw wants.
uint32_t PlayerColorRGBA(Player* p)
{
    if (!p) return Render::Debug::MakeRGBA(180, 180, 180);
    const Color c = p->getPlayerColor();
    const uint8_t r = (c >> 16) & 0xFF;
    const uint8_t g = (c >>  8) & 0xFF;
    const uint8_t b = (c      ) & 0xFF;
    if (r == 0 && g == 0 && b == 0)
        return Render::Debug::MakeRGBA(180, 180, 180);
    return Render::Debug::MakeRGBA(r, g, b);
}

uint32_t StateColor(ActivityBucket b)
{
    using namespace Render::Debug;
    switch (b)
    {
        case ACT_IDLE:         return MakeRGBA(150, 150, 150);
        case ACT_MOVING:       return kCyan;
        case ACT_ATTACKING:    return kRed;
        case ACT_FIRING:       return kOrange;
        case ACT_CONSTRUCTING: return kYellow;
        case ACT_REPAIRING:    return kGreen;
        default:               return kWhite;
    }
}

void DrawEntityGizmos()
{
    ImGui::SetNextWindowSize(ImVec2(300, 360), ImGuiCond_FirstUseEver);
    BeginAiWindow("AI: Entity Gizmos", &s_visibility.entityGizmos);

    ImGui::TextDisabled("Per-unit in-world visualizers");
    ImGui::Separator();

    // Scope and color mode -----------------------------------------------
    ImGui::TextDisabled("scope");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##scope", &s_giz.scope, kScopeNames, SCOPE_COUNT);

    ImGui::TextDisabled("color mode");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##colmode", &s_giz.colorMode, kColorModeNames, COLOR_MODE_COUNT);

    ImGui::Separator();
    ImGui::TextDisabled("layers");
    ImGui::Checkbox("Self marker",       &s_giz.showSelfMark);
    ImGui::Checkbox("Facing direction",  &s_giz.showFacing);
    ImGui::Checkbox("Pathfinder path",   &s_giz.showPath);
    ImGui::Checkbox("Waypoints",         &s_giz.showWaypoints);
    ImGui::Checkbox("Goal arrow",        &s_giz.showGoal);
    ImGui::Checkbox("Attack target",     &s_giz.showTarget);
    ImGui::Checkbox("Vision range",      &s_giz.showVision);

    // Resolve local player once per frame, *without* tripping the
    // DEBUG_ASSERTCRASH inside getLocalPlayer() when invoked from the
    // main menu (m_local is nullptr there). We walk the list manually
    // and pick the first PLAYER_HUMAN we find.
    Player* localPlayer = nullptr;
    if (s_giz.scope == SCOPE_FRIENDLY && ThePlayerList)
    {
        const Int n = ThePlayerList->getPlayerCount();
        for (Int i = 0; i < n; ++i)
        {
            Player* p = ThePlayerList->getNthPlayer(i);
            if (p && p->getPlayerType() == PLAYER_HUMAN)
            {
                localPlayer = p;
                break;
            }
        }
    }

    // Scope filter helper ------------------------------------------------
    auto inScope = [&](Object* o) -> bool {
        if (!o) return false;
        if (s_giz.scope == SCOPE_SELECTED)
        {
            Drawable* d = o->getDrawable();
            return d && d->isSelected();
        }
        if (s_giz.scope == SCOPE_FRIENDLY)
        {
            Player* p = o->getControllingPlayer();
            return p && localPlayer && p == localPlayer;
        }
        return true; // SCOPE_ALL
    };

    // ---- World draw ----------------------------------------------------
    if (!TheGameLogic)
    {
        ImGui::Separator();
        ImGui::TextDisabled("(GameLogic not initialized)");
        EndAiWindow();
        return;
    }

    using namespace Render::Debug;

    int drawn = 0;
    for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
    {
        AIUpdateInterface* ai = o->getAIUpdateInterface();
        if (!ai) continue;
        if (!inScope(o)) continue;

        const Coord3D* posC = o->getPosition();
        if (!posC) continue;
        const Render::Float3 pos = ToFloat3(*posC);

        // Pick a color for this entity
        const ActivityBucket bucket = ClassifyActivity(o);
        const uint32_t color = (s_giz.colorMode == COLOR_BY_PLAYER)
            ? PlayerColorRGBA(o->getControllingPlayer())
            : StateColor(bucket);

        ++drawn;

        // Self marker — small ground cross so even idle units are
        // visible at a glance
        if (s_giz.showSelfMark)
            Cross(pos, 4.0f, color);

        // Facing direction — short arrow in the unit's heading
        if (s_giz.showFacing)
        {
            const float a = o->getOrientation();
            const float len = 18.0f;
            const Render::Float3 tip{
                pos.x + std::cos(a) * len,
                pos.y + std::sin(a) * len,
                pos.z + 1.0f };
            Arrow(pos, tip, color);
        }

        // Vision range ground circle
        if (s_giz.showVision)
        {
            const float vr = o->getVisionRange();
            if (vr > 1.0f)
                GroundCircle(Render::Float3{ pos.x, pos.y, pos.z + 0.5f },
                             vr, MakeRGBA(80, 200, 255, 120), 28);
        }

        // Pathfinder linked-list path — straight segments through
        // every PathNode the low-level pathfinder produced
        if (s_giz.showPath)
        {
            if (Path* path = const_cast<Path*>(ai->getPath()))
            {
                const Render::Float3 lift{ 0, 0, 2.0f };
                Render::Float3 prev = pos;
                bool havePrev = true;
                for (PathNode* n = path->getFirstNode(); n; n = n->getNext())
                {
                    const Coord3D* p = n->getPosition();
                    if (!p) continue;
                    const Render::Float3 cur{
                        p->x + lift.x, p->y + lift.y, p->z + lift.z };
                    if (havePrev)
                        Line(prev, cur, color);
                    prev = cur;
                    havePrev = true;
                }
            }
        }

        // High-level waypoint goal-path — bigger picture queue, drawn
        // as a chain of crosses connected by yellow lines so it reads
        // distinct from the low-level pathfinder path
        if (s_giz.showWaypoints)
        {
            const Int wpCount = ai->friend_getWaypointGoalPathSize();
            const uint32_t wpColor = MakeRGBA(255, 240, 80);
            const Render::Float3 lift{ 0, 0, 4.0f };
            Render::Float3 prev = pos;
            for (Int i = 0; i < wpCount; ++i)
            {
                const Coord3D* wp = ai->friend_getGoalPathPosition(i);
                if (!wp) continue;
                const Render::Float3 cur{
                    wp->x + lift.x, wp->y + lift.y, wp->z + lift.z };
                Line(prev, cur, wpColor);
                Cross(cur, 5.0f, wpColor);
                prev = cur;
            }
        }

        // Goal arrow — current position to whatever the state machine
        // considers "where this unit is trying to be"
        if (s_giz.showGoal)
        {
            if (const Coord3D* g = ai->getGoalPosition())
            {
                // Skip degenerate (uninitialized) goals at origin
                if (g->x != 0.0f || g->y != 0.0f)
                {
                    const Render::Float3 to{ g->x, g->y, g->z + 3.0f };
                    Arrow(pos, to, MakeRGBA(120, 255, 120));
                }
            }
        }

        // Attack target arrow — red, only when actively engaging
        if (s_giz.showTarget)
        {
            if (Object* tgt = ai->getGoalObject())
            {
                if (const Coord3D* tp = tgt->getPosition())
                {
                    const Render::Float3 to{ tp->x, tp->y, tp->z + 6.0f };
                    Arrow(pos, to, kRed);
                }
            }
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Drawing gizmos for %d unit%s",
        drawn, drawn == 1 ? "" : "s");

    EndAiWindow();
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
    if (s_visibility.stateMachine)   DrawStateMachine();
    if (s_visibility.pathfinder)     DrawPathfinder();
    if (s_visibility.kanbanActivity) DrawKanbanActivity();
    if (s_visibility.kanbanProd)     DrawKanbanProduction();
    if (s_visibility.buildLists)     DrawBuildLists();
    if (s_visibility.modelDebugger)  DrawModelDebugger();
    if (s_visibility.entityGizmos)   DrawEntityGizmos();
}

} // namespace Ai
} // namespace Inspector
