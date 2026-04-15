// Destruction Timeline — see DestructionTimeline.h for the rationale.
//
// This file owns:
//   1. The thread-safe ring buffer of DeathRecord.
//   2. The recorder API (called from engine code).
//   3. The ImGui gantt panel that visualizes the ring.
//
// The panel is a single ImGui::Table with two columns and ScrollFreeze(1,1)
// so the player-name column stays put when the user scrolls horizontally
// and the minute-axis row stays put when the user scrolls vertically.
// The right-hand cell of each player row is a custom-rendered "track"
// drawn via ImDrawList — gridlines every minute, tick-mark per death in
// the player's color, hover tooltip that lists the unit names that died
// inside whichever minute bucket is under the cursor.
//
// Why a custom track and not one cell per minute: the panel needs to feel
// like a continuous gantt (so you can spot the rhythm of a fight at a
// glance) and per-cell tables get expensive once a long match accumulates
// 30+ minutes worth of columns. The custom track also lets us cluster
// dense markers visually (a count badge appears when more than ~6 deaths
// land within 5 px of each other) so a wave of paratroopers doesn't
// degenerate into a single saturated stripe.

#include "DestructionTimeline.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Inspector
{
namespace Destruction
{

namespace
{

// One recorded death. Strings are owned (std::string) because the
// recorder is called from anywhere in the engine and we don't want to
// rely on ThingTemplate strings outliving the record (they normally do,
// but the panel can outlive a map reload).
struct DeathRecord
{
    unsigned int frame        = 0;     // logic frame at time of death
    int          playerIndex  = -1;    // owning player slot
    unsigned int colorARGB    = 0;     // player color, 32-bit ARGB
    bool         selfInflicted = false;
    int          kindMask     = 0;     // 0 unit / 1 vehicle / 2 structure / 3 inf / 4 air
    std::string  templateName;         // e.g. "AmericaTankCrusader"
    std::string  sideName;             // "America" / "China" / "GLA" / ""
};

// Ring buffer + lock. 16k records is roughly 8-10 minutes of an extreme
// many-vs-many late-game with constant carnage; the oldest fall off when
// the ring fills, which matches the user-facing semantic of "the panel
// shows the last several minutes" without unbounded growth.
constexpr size_t kMaxRecords = 16384;
std::mutex                    g_mutex;
std::deque<DeathRecord>       g_records;

// Highest frame we have ever recorded. Used to detect frame regression
// (new game starts at frame 0) so we can auto-clear without an explicit
// "game start" hook in engine code. Reads/writes are inside the lock.
unsigned int g_highWaterFrame = 0;

// Logic frame rate. The engine constant LOGICFRAMES_PER_SECOND is 30
// in vanilla Generals/ZH and we mirror it here so the panel doesn't
// have to drag in any engine headers just to convert frames to seconds.
constexpr float kLogicFramesPerSecond = 30.0f;

// Convert ARGB unsigned int to ImGui's IM_COL32 (which is RGBA in low
// byte order). Player color sometimes leaves alpha at 0 if the entry
// was never explicitly set up — force opaque so markers are visible.
ImU32 ARGBToImCol(unsigned int argb)
{
    unsigned int a = (argb >> 24) & 0xFF;
    unsigned int r = (argb >> 16) & 0xFF;
    unsigned int g = (argb >>  8) & 0xFF;
    unsigned int b = (argb      ) & 0xFF;
    if (a == 0) a = 255;
    // Pure-black player slot (unassigned) → use a neutral gray so the
    // marker is visible against the dark panel background.
    if (r == 0 && g == 0 && b == 0)
    { r = g = b = 180; }
    return IM_COL32(r, g, b, a);
}

// Lighter / darker variants of a color, used for marker outlines and
// row backgrounds. Lerp toward white / black with a fixed factor.
ImU32 LightenARGB(unsigned int argb, float t)
{
    int a = (argb >> 24) & 0xFF; if (a == 0) a = 255;
    int r = (argb >> 16) & 0xFF;
    int g = (argb >>  8) & 0xFF;
    int b = (argb      ) & 0xFF;
    r = (int)(r + (255 - r) * t);
    g = (int)(g + (255 - g) * t);
    b = (int)(b + (255 - b) * t);
    return IM_COL32(r, g, b, a);
}

ImU32 DarkenARGB(unsigned int argb, float t)
{
    int a = (argb >> 24) & 0xFF; if (a == 0) a = 80;
    int r = (int)(((argb >> 16) & 0xFF) * (1.0f - t));
    int g = (int)(((argb >>  8) & 0xFF) * (1.0f - t));
    int b = (int)(((argb      ) & 0xFF) * (1.0f - t));
    return IM_COL32(r, g, b, a);
}

// Per-frame snapshot taken under the lock. The panel iterates this
// instead of the live deque so the lock is held for as little time as
// possible (one std::vector copy of POD-ish records).
struct Snapshot
{
    std::vector<DeathRecord> records;
    unsigned int             highWaterFrame = 0;
};

Snapshot TakeSnapshot()
{
    Snapshot snap;
    std::lock_guard<std::mutex> lock(g_mutex);
    snap.records.assign(g_records.begin(), g_records.end());
    snap.highWaterFrame = g_highWaterFrame;
    return snap;
}

// Panel-local UI state. Persists between frames so the user's chosen
// zoom, filter, and follow-mode survive a map reload.
struct PanelState
{
    float pxPerMin   = 96.0f;  // horizontal zoom
    float rowHeight  = 22.0f;  // per-row height (rows are denser now
                               //   that we have one per (team, unit))
    bool  hideSelfDestruct = false; // hide sold/expired/suicide records
    bool  followLive = true;   // auto-scroll right edge to current frame
    char  searchBuf[64] = {};  // unit-name substring filter (case-insensitive)
    bool  initialized = false;
};
PanelState g_panel;

// Format a frame count as "mm:ss" — used in tooltips for precise time.
void FormatFrameTime(unsigned int frame, char* out, size_t cap)
{
    const unsigned int totalSec = (unsigned int)(frame / kLogicFramesPerSecond);
    const unsigned int mm = totalSec / 60;
    const unsigned int ss = totalSec % 60;
    std::snprintf(out, cap, "%u:%02u", mm, ss);
}

// One row in the gantt table = one (team, unit type) pair. The buckets
// vector is one slot per displayed minute and counts how many of this
// type the team lost during that minute. Adjacent non-zero buckets get
// rendered as a single merged chunk with the total written inside.
struct AggRow
{
    int           playerIndex   = -1;
    unsigned int  colorARGB     = 0;
    std::string   sideName;
    std::string   templateName;
    int           totalDeaths   = 0;
    unsigned int  firstFrame    = ~0u;
    std::vector<int> buckets;          // size = axisMinCount
};

// Case-insensitive substring match. Used by the unit-name search filter
// so the user can type "tank" and only see tank rows. Returns true when
// `needle` is empty so an empty filter is a no-op.
bool ContainsIgnoreCase(const char* hay, const char* needle)
{
    if (!needle || !*needle) return true;
    if (!hay) return false;
    for (; *hay; ++hay)
    {
        const char* h = hay;
        const char* n = needle;
        while (*h && *n)
        {
            char ch = *h, cn = *n;
            if (ch >= 'A' && ch <= 'Z') ch += 32;
            if (cn >= 'A' && cn <= 'Z') cn += 32;
            if (ch != cn) break;
            ++h; ++n;
        }
        if (!*n) return true;
    }
    return false;
}

// Bucket every record into a (player, template) row. The result is
// sorted by player slot, then by total death count descending so the
// most-affected unit type for each team appears at the top of that
// team's group. Stable name-sort breaks count ties for determinism.
void BuildAggRows(const Snapshot& snap,
                  int               axisMinCount,
                  const char*       searchFilter,
                  std::vector<AggRow>& out)
{
    out.clear();
    if (snap.records.empty() || axisMinCount <= 0)
        return;

    // Map key: "P<idx>|<templateName>". Cheap to format and the row
    // count is small even in pathological matches (~few hundred), so
    // a string-keyed unordered_map is plenty for one-shot per-frame
    // aggregation.
    std::unordered_map<std::string, size_t> indexByKey;
    char keybuf[160];

    for (const DeathRecord& r : snap.records)
    {
        if (!ContainsIgnoreCase(r.templateName.c_str(), searchFilter))
            continue;

        std::snprintf(keybuf, sizeof(keybuf), "P%d|%s",
            r.playerIndex, r.templateName.c_str());

        AggRow* row = nullptr;
        auto it = indexByKey.find(keybuf);
        if (it == indexByKey.end())
        {
            AggRow nr;
            nr.playerIndex  = r.playerIndex;
            nr.colorARGB    = r.colorARGB;
            nr.sideName     = r.sideName;
            nr.templateName = r.templateName;
            nr.buckets.assign(axisMinCount, 0);
            indexByKey[keybuf] = out.size();
            out.push_back(std::move(nr));
            row = &out.back();
        }
        else
        {
            row = &out[it->second];
        }

        const float t = r.frame / kLogicFramesPerSecond;
        const int   minBucket = (int)(t / 60.0f);
        if (minBucket >= 0 && minBucket < axisMinCount)
            row->buckets[minBucket]++;
        row->totalDeaths++;
        if (r.frame < row->firstFrame) row->firstFrame = r.frame;
        if (!r.sideName.empty())       row->sideName  = r.sideName;
        row->colorARGB = r.colorARGB;
    }

    std::sort(out.begin(), out.end(),
        [](const AggRow& a, const AggRow& b)
        {
            if (a.playerIndex  != b.playerIndex)  return a.playerIndex  <  b.playerIndex;
            if (a.totalDeaths  != b.totalDeaths)  return a.totalDeaths  >  b.totalDeaths;
            return a.templateName < b.templateName;
        });
}

// Replace the alpha byte of a packed IM_COL32 value, leaving RGB
// untouched. Used to draw merged chunks at fixed semi-transparency
// regardless of whatever alpha the recorder happened to push.
ImU32 WithAlpha(ImU32 col, int alpha)
{
    return (col & 0x00FFFFFFu) | (((unsigned)alpha & 0xFFu) << 24);
}

} // namespace

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------

void RecordDeath(int          playerIndex,
                 const char*  templateName,
                 unsigned int colorARGB,
                 const char*  sideName,
                 unsigned int frame,
                 bool         selfInflicted,
                 int          kindMask)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    // Frame regression → a new game started; drop the old match's
    // records so the panel doesn't conflate two unrelated timelines.
    if (frame + (unsigned)(kLogicFramesPerSecond * 5) < g_highWaterFrame)
    {
        g_records.clear();
        g_highWaterFrame = 0;
    }

    if (g_records.size() >= kMaxRecords)
        g_records.pop_front();

    DeathRecord rec;
    rec.frame        = frame;
    rec.playerIndex  = playerIndex;
    rec.colorARGB    = colorARGB;
    rec.selfInflicted = selfInflicted;
    rec.kindMask     = kindMask;
    rec.templateName = (templateName && *templateName) ? templateName : "<unknown>";
    rec.sideName     = (sideName     && *sideName    ) ? sideName     : "";
    g_records.push_back(std::move(rec));

    if (frame > g_highWaterFrame)
        g_highWaterFrame = frame;
}

void Clear()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_records.clear();
    g_highWaterFrame = 0;
}

int Count()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return (int)g_records.size();
}

// ----------------------------------------------------------------------------
// Panel
// ----------------------------------------------------------------------------

void DrawPanel(bool* pOpen)
{
    if (!g_panel.initialized)
    {
        // First-frame default size only — after that the dock layout
        // / user drags own the window rect.
        ImGui::SetNextWindowSize(ImVec2(1100, 420), ImGuiCond_FirstUseEver);
        g_panel.initialized = true;
    }

    if (!ImGui::Begin("Destruction Timeline", pOpen))
    {
        ImGui::End();
        return;
    }

    Snapshot snap = TakeSnapshot();

    // ---- Top toolbar -------------------------------------------------
    {
        const float curMinutes = snap.highWaterFrame / kLogicFramesPerSecond / 60.0f;
        ImGui::Text("Game time: %5.2f min", curMinutes);
        ImGui::SameLine(0, 24);
        ImGui::Text("Records: %d", (int)snap.records.size());

        ImGui::SameLine(0, 24);
        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat("Zoom", &g_panel.pxPerMin, 40.0f, 320.0f, "%.0f px/min");

        ImGui::SameLine(0, 24);
        ImGui::SetNextItemWidth(70);
        ImGui::SliderFloat("Row", &g_panel.rowHeight, 16.0f, 48.0f, "%.0f");

        ImGui::SameLine(0, 24);
        ImGui::Checkbox("Hide self-destruct", &g_panel.hideSelfDestruct);
        ImGui::SameLine();
        ImGui::Checkbox("Follow live", &g_panel.followLive);

        ImGui::SameLine(0, 24);
        ImGui::SetNextItemWidth(160);
        ImGui::InputTextWithHint("##search", "filter unit...",
            g_panel.searchBuf, IM_ARRAYSIZE(g_panel.searchBuf));

        ImGui::SameLine(0, 16);
        if (ImGui::Button("Clear"))
        {
            Clear();
            snap.records.clear();
            snap.highWaterFrame = 0;
        }
    }

    ImGui::Separator();

    // Apply self-destruct filter to the snapshot before aggregation.
    if (g_panel.hideSelfDestruct && !snap.records.empty())
    {
        std::vector<DeathRecord> kept;
        kept.reserve(snap.records.size());
        for (const DeathRecord& r : snap.records)
            if (!r.selfInflicted) kept.push_back(r);
        snap.records.swap(kept);
    }

    // Compute the displayed time extent. We always show at least 5
    // minutes so an early game doesn't have a single-pixel timeline,
    // and add a small lookahead so the live marker isn't pinned to
    // the right edge.
    const float liveSec = snap.highWaterFrame / kLogicFramesPerSecond;
    float displayMin = liveSec / 60.0f + 0.5f;
    if (displayMin < 5.0f) displayMin = 5.0f;
    const int   axisMinCount = (int)std::ceil(displayMin) + 1;
    const float trackPxWidth = axisMinCount * g_panel.pxPerMin;

    // Build aggregated (team, unit) rows from the (filtered) snapshot.
    std::vector<AggRow> rows;
    BuildAggRows(snap, axisMinCount, g_panel.searchBuf, rows);

    if (rows.empty())
    {
        ImGui::TextDisabled(
            "No deaths to show. Start a game (or clear the filter / "
            "self-destruct toggle) and watch units die.");
        ImGui::End();
        return;
    }

    // ---- Main 3-column gantt table ----------------------------------
    //
    // Three columns: Team (sticky), Unit (sticky), Timeline (wide,
    // horizontally scrollable). ScrollFreeze(2,1) keeps the two label
    // columns put when the user pans the timeline left/right and the
    // header+axis row pinned when the user scrolls a long list.
    constexpr ImGuiTableFlags tflags =
        ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_BordersInnerH |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_NoPadInnerX;

    const float kTeamColW = 110.0f;
    const float kUnitColW = 200.0f;

    if (ImGui::BeginTable("##gantt", 3, tflags, ImVec2(0, 0)))
    {
        // Freeze: 2 columns (Team + Unit) horizontally, 2 rows (header
        // titles + per-minute axis) vertically. Both stay put while
        // the user pans the timeline or scrolls a long row list.
        ImGui::TableSetupScrollFreeze(2, 2);
        ImGui::TableSetupColumn("Team",
            ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
            kTeamColW);
        ImGui::TableSetupColumn("Unit",
            ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
            kUnitColW);
        ImGui::TableSetupColumn("Timeline (1-min buckets, merged)",
            ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
            trackPxWidth);
        ImGui::TableHeadersRow();

        // ---- Frozen minute-axis row ----
        // Sits inside the ScrollFreeze region so it stays pinned at
        // the top of the table while body rows scroll vertically. The
        // labels are drawn into the cell via ImDrawList because we
        // need them at exact x = N * pxPerMin pixel offsets, not at
        // ImGui's auto-laid-out cursor positions.
        ImGui::TableNextRow(0, 20.0f);
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("(min)");
        ImGui::TableSetColumnIndex(2);
        {
            ImGui::Dummy(ImVec2(trackPxWidth, 16));
            const ImVec2 a0 = ImGui::GetItemRectMin();
            const ImVec2 a1 = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(a0, a1, IM_COL32(28, 28, 32, 255));
            for (int m = 0; m < axisMinCount; ++m)
            {
                const float x = a0.x + m * g_panel.pxPerMin;
                dl->AddLine(ImVec2(x, a0.y), ImVec2(x, a1.y),
                            IM_COL32(80, 80, 90, 255));
                char lbl[16];
                std::snprintf(lbl, sizeof(lbl), "%d:00", m);
                dl->AddText(ImVec2(x + 4, a0.y + 1),
                            IM_COL32(200, 200, 210, 255), lbl);
            }
            const float liveX = a0.x + (liveSec / 60.0f) * g_panel.pxPerMin;
            dl->AddLine(ImVec2(liveX, a0.y), ImVec2(liveX, a1.y),
                        IM_COL32(255, 160, 60, 255), 2.0f);
        }

        // ---- Body rows: one per (team, unit) pair ----
        //
        // Tracks the previous row's player index so we can suppress
        // the team label on continuation rows of the same team and
        // draw a colored vertical bar instead — keeps the panel feeling
        // grouped without repeating the team name on every row.
        int prevPlayerIdx = INT_MIN;

        for (size_t rowIdx = 0; rowIdx < rows.size(); ++rowIdx)
        {
            const AggRow& row = rows[rowIdx];
            const bool firstOfTeam = (row.playerIndex != prevPlayerIdx);
            prevPlayerIdx = row.playerIndex;

            ImGui::PushID((int)rowIdx);
            ImGui::TableNextRow(0, g_panel.rowHeight);

            // ---- Col 0: Team ----
            ImGui::TableSetColumnIndex(0);
            {
                const ImU32 col = ARGBToImCol(row.colorARGB);
                if (firstOfTeam)
                {
                    ImVec4 v;
                    v.x = ((col      ) & 0xFF) / 255.0f;
                    v.y = ((col >>  8) & 0xFF) / 255.0f;
                    v.z = ((col >> 16) & 0xFF) / 255.0f;
                    v.w = ((col >> 24) & 0xFF) / 255.0f;
                    const char* side = row.sideName.empty()
                        ? "(neutral)" : row.sideName.c_str();
                    ImGui::TextColored(v, "P%d %s", row.playerIndex, side);
                }
                else
                {
                    // Continuation row → small colored bar at the
                    // left edge of the team column so the eye can still
                    // track which team this row belongs to.
                    const ImVec2 c0 = ImGui::GetCursorScreenPos();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(
                        ImVec2(c0.x + 4, c0.y),
                        ImVec2(c0.x + 8, c0.y + g_panel.rowHeight - 4),
                        col);
                    ImGui::Dummy(ImVec2(12, g_panel.rowHeight - 4));
                }
            }

            // ---- Col 1: Unit name + total ----
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(row.templateName.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%d)", row.totalDeaths);

            // ---- Col 2: Merged-chunk timeline ----
            ImGui::TableSetColumnIndex(2);
            {
                const float trackH = g_panel.rowHeight - 4.0f;
                ImGui::InvisibleButton("##track",
                    ImVec2(trackPxWidth, trackH));
                const bool   hovered = ImGui::IsItemHovered();
                const ImVec2 p0 = ImGui::GetItemRectMin();
                const ImVec2 p1 = ImGui::GetItemRectMax();
                ImDrawList* dl  = ImGui::GetWindowDrawList();

                // Background + alternate minute bands + gridlines.
                dl->AddRectFilled(p0, p1, IM_COL32(22, 22, 26, 255));
                for (int m = 0; m < axisMinCount; ++m)
                {
                    if ((m & 1) == 0) continue;
                    const float xa = p0.x + m       * g_panel.pxPerMin;
                    const float xb = p0.x + (m + 1) * g_panel.pxPerMin;
                    dl->AddRectFilled(ImVec2(xa, p0.y), ImVec2(xb, p1.y),
                                      IM_COL32(255, 255, 255, 8));
                }
                for (int m = 0; m <= axisMinCount; ++m)
                {
                    const float x = p0.x + m * g_panel.pxPerMin;
                    const ImU32 c = (m % 5 == 0)
                        ? IM_COL32(110, 110, 130, 200)
                        : IM_COL32(60, 60, 70, 200);
                    dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), c, 1.0f);
                }
                // Live-now marker.
                const float liveX = p0.x + (liveSec / 60.0f) * g_panel.pxPerMin;
                dl->AddLine(ImVec2(liveX, p0.y), ImVec2(liveX, p1.y),
                            IM_COL32(255, 160, 60, 220), 2.0f);

                // ---- Walk runs of consecutive non-zero buckets ----
                //
                // Each run = one merged chunk. We draw a filled rect
                // spanning [start*pxPerMin, end*pxPerMin] in the team
                // color and stamp the total count in the middle. The
                // hover tooltip shows the [start, end) minute range
                // and the per-minute breakdown.
                const ImU32 baseCol = ARGBToImCol(row.colorARGB);
                const ImU32 edgeCol = LightenARGB(row.colorARGB, 0.55f);

                // Highest single-minute count for this row, used to
                // tint the chunk fill so heavy bursts read as bright
                // and lone deaths read as dim. Computed inline so we
                // don't store it on AggRow.
                int rowMaxBucket = 1;
                for (int v : row.buckets)
                    if (v > rowMaxBucket) rowMaxBucket = v;

                int hoverStart   = -1;
                int hoverEnd     = -1;
                int hoverTotal   = 0;
                int hoverMaxBkt  = 0;
                const float mouseX = ImGui::GetIO().MousePos.x;

                int m = 0;
                while (m < axisMinCount)
                {
                    if (row.buckets[m] == 0) { m++; continue; }

                    const int start = m;
                    int total = 0;
                    int maxBkt = 0;
                    while (m < axisMinCount && row.buckets[m] > 0)
                    {
                        total  += row.buckets[m];
                        if (row.buckets[m] > maxBkt) maxBkt = row.buckets[m];
                        m++;
                    }
                    const int end = m;  // exclusive

                    const float xa = p0.x + start * g_panel.pxPerMin + 1.0f;
                    const float xb = p0.x + end   * g_panel.pxPerMin - 1.0f;
                    const float ya = p0.y + 2.0f;
                    const float yb = p1.y - 2.0f;

                    // Brighter chunks for bursts: scale alpha by the
                    // ratio of this chunk's worst minute to the row's
                    // worst minute. Lone deaths still get a faint fill
                    // so the chunk is always visible.
                    const int alpha = 110 + (int)(140.0f * (float)maxBkt
                                                 / (float)rowMaxBucket);
                    const ImU32 chunkFill = WithAlpha(baseCol,
                                                      alpha > 255 ? 255 : alpha);
                    dl->AddRectFilled(ImVec2(xa, ya), ImVec2(xb, yb), chunkFill);
                    dl->AddRect      (ImVec2(xa, ya), ImVec2(xb, yb), edgeCol);

                    // Stamp the total count centered in the chunk.
                    char lbl[16];
                    std::snprintf(lbl, sizeof(lbl), "%d", total);
                    const ImVec2 ts = ImGui::CalcTextSize(lbl);
                    if ((xb - xa) >= ts.x + 4 && (yb - ya) >= ts.y)
                    {
                        const float tx = (xa + xb) * 0.5f - ts.x * 0.5f;
                        const float ty = (ya + yb) * 0.5f - ts.y * 0.5f;
                        // Tiny dark backing rect so the digit reads
                        // even on bright team colors (yellow GLA).
                        dl->AddRectFilled(
                            ImVec2(tx - 1, ty),
                            ImVec2(tx + ts.x + 1, ty + ts.y),
                            IM_COL32(0, 0, 0, 140));
                        dl->AddText(ImVec2(tx, ty),
                            IM_COL32(255, 255, 255, 255), lbl);
                    }

                    // Capture hover info if the mouse is in this chunk.
                    if (hovered && mouseX >= xa && mouseX <= xb)
                    {
                        hoverStart  = start;
                        hoverEnd    = end;
                        hoverTotal  = total;
                        hoverMaxBkt = maxBkt;
                    }
                }

                // ---- Hover tooltip ----
                if (hovered)
                {
                    if (hoverStart >= 0)
                    {
                        ImGui::BeginTooltip();
                        const ImU32 col = ARGBToImCol(row.colorARGB);
                        ImVec4 tv;
                        tv.x = ((col      ) & 0xFF) / 255.0f;
                        tv.y = ((col >>  8) & 0xFF) / 255.0f;
                        tv.z = ((col >> 16) & 0xFF) / 255.0f;
                        tv.w = ((col >> 24) & 0xFF) / 255.0f;
                        ImGui::TextColored(tv, "P%d %s",
                            row.playerIndex,
                            row.sideName.empty() ? "(neutral)" : row.sideName.c_str());
                        ImGui::Text("%s × %d",
                            row.templateName.c_str(), hoverTotal);
                        ImGui::Separator();
                        ImGui::TextDisabled("Minutes %d:00 – %d:00",
                            hoverStart, hoverEnd);
                        ImGui::TextDisabled("Worst minute: %d", hoverMaxBkt);
                        // Per-minute breakdown lines for the chunk.
                        for (int mm = hoverStart; mm < hoverEnd; ++mm)
                        {
                            const int v = row.buckets[mm];
                            if (v > 0)
                                ImGui::Text("  %d:00 — %d", mm, v);
                        }
                        ImGui::EndTooltip();
                    }
                    else
                    {
                        // Empty area → show the minute under cursor.
                        const float relX   = mouseX - p0.x;
                        const float minute = relX / g_panel.pxPerMin;
                        ImGui::SetTooltip("%s — %.2f min (no deaths)",
                            row.templateName.c_str(), minute);
                    }
                }
            }

            ImGui::PopID();
        }

        // ---- Auto-follow live edge ----
        // After laying out the table, scroll the timeline column so
        // the orange "now" marker stays roughly 70% across the visible
        // width. Cheap because BeginTable already did all its layout.
        if (g_panel.followLive && trackPxWidth > 0)
        {
            const float liveLocalX = (liveSec / 60.0f) * g_panel.pxPerMin;
            const float visibleW   = ImGui::GetContentRegionAvail().x;
            const float targetX    = liveLocalX - visibleW * 0.7f;
            if (targetX > 0)
                ImGui::SetScrollX(targetX);
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace Destruction
} // namespace Inspector
