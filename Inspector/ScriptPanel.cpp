// Mission Script debugger panel for the ImGui Inspector.
//
// Goal: debug USA01 and similar campaign missions, catch broken
// scripts at the moment they fail, and fix them quickly. The panel
// has four main features:
//
//   1. **Live tree** of every Side → Group → Script with a coloured
//      status badge (recently fired / always-true / disabled / never
//      evaluated) so you can scan the whole mission at a glance.
//   2. **Per-script details** showing conditions, true/false action
//      lists, and per-script controls (Force Fire, Toggle Active,
//      Reset Eval Frame).
//   3. **Recent fires log** — a ring buffer of the last 256 script
//      events (true-fire / false-fire / force-fire / toggle) so you
//      can scrub back through what just happened during a cinematic
//      glitch without restarting the mission.
//   4. **Counters & flags** table — live values, edit-in-place, so
//      you can poke a counter to satisfy a stuck "if Counter X >= 5"
//      condition and continue past a broken trigger.
//
// The panel relies on a tiny set of inspector hooks added to
// ScriptEngine: a function-pointer observer fired from inside
// executeScript() (so we capture every eval whether the panel is
// open or not), plus public force-fire / toggle / counter-edit
// helpers. See ScriptEngine.h `inspectorScriptObserverFn` and the
// related `inspector*` methods.

#include "Panels.h"
#include "ScriptPanel.h"

#include "imgui.h"

// --- Engine includes -----------------------------------------------
#include "Common/AsciiString.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Scripts.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/SidesList.h"

#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace Inspector
{
namespace Panels
{

// ----------------------------------------------------------------------------
// Per-script live state cache
// ----------------------------------------------------------------------------
//
// Keyed by script name (AsciiString contents). Names are unique within a
// loaded map and survive map reloads as fresh entries, which is exactly
// the lifetime we want here. The map gets cleared whenever we detect a
// new map load (the engine bumps a "newMap" generation we approximate
// by watching for the script tree to disappear and reappear with a
// different head pointer — see RebuildSidesIndex).
namespace
{
    struct ScriptState
    {
        UnsignedInt lastEvalFrame   = 0;     // last frame any branch ran
        UnsignedInt lastFireFrame   = 0;     // last frame the true-branch fired
        UnsignedInt lastFalseFrame  = 0;     // last frame the false-branch fired
        UnsignedInt forceFireFrame  = 0;     // last frame Force Fire was pressed
        UnsignedInt toggledFrame    = 0;     // last frame the active flag was poked from the panel
        Int         totalTrueFires  = 0;
        Int         totalFalseFires = 0;
        Int         totalForceFires = 0;
        Bool        lastResult      = FALSE;
    };

    // Unified timeline event kind. Script-eval events come from the
    // script observer (one entry per executeScript pass that resolved a
    // condition); action events come from the action observer (one
    // entry per dispatched action, including the inline special-cases
    // handled directly by ScriptEngine::executeActions).
    enum TimelineKind
    {
        TL_SCRIPT_TRUE  = 0,   // ScriptEngine::INSPECTOR_SCRIPT_EVAL_TRUE
        TL_SCRIPT_FALSE = 1,   // ScriptEngine::INSPECTOR_SCRIPT_EVAL_FALSE
        TL_SCRIPT_FORCE = 2,   // ScriptEngine::INSPECTOR_SCRIPT_FORCE_FIRE
        TL_SCRIPT_TOGGLE= 3,   // ScriptEngine::INSPECTOR_SCRIPT_TOGGLED
        TL_ACTION       = 4,   // ScriptEngine::executeActions dispatched an action
        TL_KIND_COUNT
    };

    struct TimelineEntry
    {
        UnsignedInt frame;     // game frame this happened on
        std::string label;     // short header (script name or action type)
        std::string detail;    // long form (action getUiText, or empty for scripts)
        int         kind;      // TimelineKind
    };

    // Bigger ring than the previous "recent fires" buffer because we now
    // also include every action dispatch, which can run 30-100 events
    // per frame on a busy mission. 2048 entries ≈ 20s of dense action.
    constexpr size_t kTimelineMax = 2048;

    // Cache state. All access happens on the main game thread, so no
    // mutex is needed — the script engine update and the ImGui draw
    // both run from the same loop.
    std::unordered_map<std::string, ScriptState> g_state;
    std::deque<TimelineEntry>                    g_timeline;

    // Pause / step controls. When paused, we still record fires but
    // mark them so the user can tell something happened while frozen.
    // (Engine pause itself is the user's responsibility — this is just
    // a panel filter.)
    bool s_pauseRecording = false;

    // The script the user has selected for the details pane. Stored as
    // a name string so it survives a map reload.
    std::string s_selectedScript;

    // Filter strings for the tree and the timeline table.
    char s_treeFilter[128]      = "";
    char s_timelineFilter[128]  = "";

    // Per-kind visibility for the timeline. By default ALL on so the
    // user sees a complete picture. Toggle the noisy ones (e.g. tree
    // bounces, ambient particle FXLists) off to focus on briefings.
    bool s_showKind[TL_KIND_COUNT] = { true, true, true, true, true };

    // Auto-scroll the timeline to newest entries. Off when the user
    // scrolls back manually so they can study an event without it
    // jumping under their cursor.
    bool s_timelineAutoScroll = true;

    // Highlight window — scripts whose lastFireFrame is within this
    // many frames of "now" get a green tint. Tunable from the toolbar.
    int s_highlightFrames = 60;

    // Push a new entry onto the timeline ring buffer, dropping the
    // oldest entry if we're at capacity.
    void PushTimeline(UnsignedInt frame, int kind, std::string label, std::string detail)
    {
        if (g_timeline.size() >= kTimelineMax)
            g_timeline.pop_front();
        TimelineEntry e;
        e.frame  = frame;
        e.kind   = kind;
        e.label  = std::move(label);
        e.detail = std::move(detail);
        g_timeline.push_back(std::move(e));
    }

    // ----------------------------------------------------------------
    // Script observer — runs from inside ScriptEngine::executeScript
    // every time a script's conditions resolve to true or false, plus
    // the debugger force-fire / toggle paths. Cheap: a single map
    // insert + a deque push.
    void OnScriptEvent(const Script* script, ScriptEngine::InspectorScriptEvent event, UnsignedInt frame)
    {
        if (s_pauseRecording)
            return;
        if (script == nullptr)
            return;

        const std::string name = script->getName().str();
        ScriptState& state = g_state[name];
        state.lastEvalFrame = frame;

        int tlKind = TL_SCRIPT_TRUE;
        switch (event)
        {
            case ScriptEngine::INSPECTOR_SCRIPT_EVAL_TRUE:
                state.lastFireFrame = frame;
                state.totalTrueFires++;
                state.lastResult = TRUE;
                tlKind = TL_SCRIPT_TRUE;
                break;
            case ScriptEngine::INSPECTOR_SCRIPT_EVAL_FALSE:
                state.lastFalseFrame = frame;
                state.totalFalseFires++;
                state.lastResult = FALSE;
                tlKind = TL_SCRIPT_FALSE;
                break;
            case ScriptEngine::INSPECTOR_SCRIPT_FORCE_FIRE:
                state.forceFireFrame = frame;
                state.lastFireFrame  = frame;
                state.totalForceFires++;
                tlKind = TL_SCRIPT_FORCE;
                break;
            case ScriptEngine::INSPECTOR_SCRIPT_TOGGLED:
                state.toggledFrame = frame;
                tlKind = TL_SCRIPT_TOGGLE;
                break;
        }

        PushTimeline(frame, tlKind, name, std::string());
    }

    // ----------------------------------------------------------------
    // Action observer — runs from inside ScriptEngine::executeActions
    // for every action the engine dispatches. We grab the action's
    // type-template name (cheap) plus its UI text (a one-shot string
    // formatter that includes parameter values). The UI text is what
    // makes scripted-voice events readable: SPEECH_PLAY shows the
    // sound name and unit, MOVE_CAMERA_TO shows the waypoint, etc.
    void OnActionEvent(const ScriptAction* action, UnsignedInt frame)
    {
        if (s_pauseRecording)
            return;
        if (action == nullptr)
            return;

        const ScriptAction::ScriptActionType t = const_cast<ScriptAction*>(action)->getActionType();
        const ActionTemplate* tpl = TheScriptEngine ? TheScriptEngine->getActionTemplate((Int)t) : nullptr;
        const char* label = (tpl && !tpl->m_internalName.isEmpty()) ? tpl->m_internalName.str() : "(unknown)";

        // getUiText is non-const on the engine type, even though it
        // doesn't actually mutate anything. Cast away constness so the
        // observer can stay clean.
        AsciiString ui = const_cast<ScriptAction*>(action)->getUiText();
        std::string detail = ui.isNotEmpty() ? ui.str() : std::string();

        PushTimeline(frame, TL_ACTION, label, std::move(detail));
    }

    // ----------------------------------------------------------------
    // ConditionType / ActionType → friendly name. We ask ScriptEngine
    // for the template (which is what the worldbuilder uses) and fall
    // back to the numeric type if the template doesn't exist (e.g.
    // for the OBSOLETE_SCRIPT_* enum values).
    const char* ActionTypeName(ScriptAction::ScriptActionType t)
    {
        if (TheScriptEngine == nullptr)
            return "(no engine)";
        const ActionTemplate* tpl = TheScriptEngine->getActionTemplate((Int)t);
        if (tpl == nullptr)
            return "(unknown action)";
        const char* s = tpl->m_internalName.str();
        return (s && *s) ? s : "(unnamed action)";
    }

    const char* ConditionTypeName(Condition::ConditionType t)
    {
        if (TheScriptEngine == nullptr)
            return "(no engine)";
        const ConditionTemplate* tpl = TheScriptEngine->getConditionTemplate((Int)t);
        if (tpl == nullptr)
            return "(unknown condition)";
        const char* s = tpl->m_internalName.str();
        return (s && *s) ? s : "(unnamed condition)";
    }

    // Status colour: green = recently fired (true), red = recently
    // fired (false), gray = disabled, white = idle.
    ImVec4 StatusColor(const Script* script, const ScriptState* state, UnsignedInt now)
    {
        if (!script->isActive())
            return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
        if (state == nullptr)
            return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);

        const UnsignedInt lastTrue  = state->lastFireFrame;
        const UnsignedInt lastFalse = state->lastFalseFrame;
        const UnsignedInt window    = (UnsignedInt)(s_highlightFrames > 0 ? s_highlightFrames : 1);
        if (lastTrue != 0 && now - lastTrue < window)
            return ImVec4(0.30f, 1.00f, 0.30f, 1.0f);   // bright green
        if (lastFalse != 0 && now - lastFalse < window)
            return ImVec4(1.00f, 0.55f, 0.30f, 1.0f);   // amber-orange
        if (lastTrue != 0)
            return ImVec4(0.55f, 0.85f, 0.55f, 1.0f);   // dim green (fired earlier)
        return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);       // never fired
    }

    bool MatchesFilter(const char* name, const char* filter)
    {
        if (filter == nullptr || filter[0] == 0)
            return true;
        if (name == nullptr)
            return false;
        // Case-insensitive substring search.
        const size_t nlen = std::strlen(name);
        const size_t flen = std::strlen(filter);
        if (flen > nlen) return false;
        for (size_t i = 0; i + flen <= nlen; ++i)
        {
            bool ok = true;
            for (size_t j = 0; j < flen; ++j)
            {
                char a = name[i + j];
                char b = filter[j];
                if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
                if (a != b) { ok = false; break; }
            }
            if (ok) return true;
        }
        return false;
    }

    // ----------------------------------------------------------------
    // Tree row drawing helpers
    void DrawScriptRow(Script* script, UnsignedInt now)
    {
        if (script == nullptr)
            return;

        const char* name = script->getName().str();
        if (!MatchesFilter(name, s_treeFilter))
            return;

        const std::string nameStr = name ? name : "";
        ScriptState* state = nullptr;
        auto it = g_state.find(nameStr);
        if (it != g_state.end())
            state = &it->second;

        // Push a unique ID per script to keep buttons distinct.
        ImGui::PushID(name ? name : "anon");

        // Status colour for the leading dot + name.
        ImVec4 col = StatusColor(script, state, now);
        ImGui::TextColored(col, script->isActive() ? "*" : "-");
        ImGui::SameLine();

        // Selectable label spanning the rest of the row.
        const bool selected = (s_selectedScript == nameStr);
        if (ImGui::Selectable(name ? name : "(anon)", selected,
                              ImGuiSelectableFlags_AllowDoubleClick))
        {
            s_selectedScript = nameStr;
        }

        // Inline status snippet (eval count, last fire frame).
        if (state)
        {
            ImGui::SameLine();
            const Int evalCount = script->getConditionCount();
            if (state->lastFireFrame != 0)
                ImGui::TextDisabled("  fires=%d  last=#%u  evals=%d",
                                    state->totalTrueFires,
                                    (unsigned)state->lastFireFrame,
                                    (int)evalCount);
            else
                ImGui::TextDisabled("  evals=%d", (int)evalCount);
        }

        ImGui::PopID();
    }

    // ----------------------------------------------------------------
    // Details pane — shown for the currently selected script.
    void DrawScriptDetails(Script* script, UnsignedInt now)
    {
        if (script == nullptr)
        {
            ImGui::TextDisabled("Select a script in the tree to see details.");
            return;
        }

        const char* name = script->getName().str();
        ImGui::Text("Name: %s", name ? name : "(anon)");
        if (script->getComment().isNotEmpty())
            ImGui::TextDisabled("Comment: %s", script->getComment().str());

        ImGui::Separator();

        // Header row with state + control buttons.
        bool active = script->isActive() != FALSE;
        if (ImGui::Checkbox("Active", &active))
        {
            if (TheScriptEngine)
                TheScriptEngine->inspectorSetScriptEnabled(AsciiString(name), active ? TRUE : FALSE);
        }
        ImGui::SameLine();
        ImGui::Text(script->isOneShot()    ? "[one-shot]"    : "[reusable]");
        ImGui::SameLine();
        ImGui::Text(script->isSubroutine() ? "[subroutine]" : "");
        ImGui::SameLine();
        if (script->getDelayEvalSeconds() > 0)
            ImGui::Text("[every %ds]", script->getDelayEvalSeconds());

        // Difficulty filter visualisation.
        ImGui::Text("Difficulty:  E:%s  N:%s  H:%s",
                    script->isEasy()   ? "yes" : "no",
                    script->isNormal() ? "yes" : "no",
                    script->isHard()   ? "yes" : "no");

        // Live counters from the cache.
        ScriptState* state = nullptr;
        auto it = g_state.find(name ? name : "");
        if (it != g_state.end())
            state = &it->second;
        if (state)
        {
            ImGui::Text("Fires (true):    %d", state->totalTrueFires);
            ImGui::Text("Fires (false):   %d", state->totalFalseFires);
            ImGui::Text("Force fires:     %d", state->totalForceFires);
            if (state->lastFireFrame)
                ImGui::Text("Last true:       frame %u  (%u frames ago)",
                            (unsigned)state->lastFireFrame, (unsigned)(now - state->lastFireFrame));
            if (state->lastFalseFrame)
                ImGui::Text("Last false:      frame %u  (%u frames ago)",
                            (unsigned)state->lastFalseFrame, (unsigned)(now - state->lastFalseFrame));
        }
        else
        {
            ImGui::TextDisabled("Not yet evaluated this session.");
        }

        // Action buttons.
        ImGui::Separator();
        if (ImGui::Button("Force Fire"))
        {
            if (TheScriptEngine)
                TheScriptEngine->inspectorForceFireScript(AsciiString(name));
        }
        ImGui::SameLine();
        if (ImGui::Button(script->isActive() ? "Disable" : "Enable"))
        {
            if (TheScriptEngine)
                TheScriptEngine->inspectorSetScriptEnabled(AsciiString(name), script->isActive() ? FALSE : TRUE);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Eval Frame"))
        {
            // Drag the next-eval frame back to "now" so periodic
            // scripts re-evaluate immediately on the next tick.
            script->setFrameToEvaluate(now);
        }
        ImGui::SameLine();
        if (ImGui::Button("Re-arm One-Shot"))
        {
            // Re-enabling a one-shot that already fired and was set
            // inactive lets you replay it without restarting the map.
            script->setActive(TRUE);
        }

        // Conditions list.
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Conditions", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int orIndex = 0;
            for (OrCondition* pOr = script->getOrCondition(); pOr; pOr = pOr->getNextOrCondition())
            {
                ImGui::PushID(orIndex);
                ImGui::TextDisabled(orIndex == 0 ? "IF" : "OR");
                ImGui::Indent();
                for (Condition* pCond = pOr->getFirstAndCondition(); pCond; pCond = pCond->getNext())
                {
                    AsciiString uiText = pCond->getUiText();
                    ImGui::BulletText("%s", ConditionTypeName(pCond->getConditionType()));
                    if (uiText.isNotEmpty())
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled(" %s", uiText.str());
                    }
                }
                ImGui::Unindent();
                ImGui::PopID();
                ++orIndex;
            }
            if (orIndex == 0)
                ImGui::TextDisabled("(no conditions)");
        }

        if (ImGui::CollapsingHeader("True actions", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int idx = 0;
            for (ScriptAction* pAct = script->getAction(); pAct; pAct = pAct->getNext())
            {
                AsciiString uiText = pAct->getUiText();
                ImGui::BulletText("%s", ActionTypeName(pAct->getActionType()));
                if (uiText.isNotEmpty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled(" %s", uiText.str());
                }
                ++idx;
            }
            if (idx == 0)
                ImGui::TextDisabled("(no true actions)");
        }

        if (ImGui::CollapsingHeader("False actions"))
        {
            int idx = 0;
            for (ScriptAction* pAct = script->getFalseAction(); pAct; pAct = pAct->getNext())
            {
                AsciiString uiText = pAct->getUiText();
                ImGui::BulletText("%s", ActionTypeName(pAct->getActionType()));
                if (uiText.isNotEmpty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled(" %s", uiText.str());
                }
                ++idx;
            }
            if (idx == 0)
                ImGui::TextDisabled("(no false actions)");
        }
    }

    // Look up a Script by name from any side / group.
    Script* FindScriptByNameSlow(const std::string& name)
    {
        if (TheSidesList == nullptr || name.empty())
            return nullptr;
        const AsciiString needle(name.c_str());
        for (Int i = 0; i < TheSidesList->getNumSides(); ++i)
        {
            ScriptList* sl = TheSidesList->getSideInfo(i)->getScriptList();
            if (sl == nullptr) continue;
            for (Script* s = sl->getScript(); s; s = s->getNext())
                if (s->getName() == needle) return s;
            for (ScriptGroup* g = sl->getScriptGroup(); g; g = g->getNext())
                for (Script* s = g->getScript(); s; s = s->getNext())
                    if (s->getName() == needle) return s;
        }
        return nullptr;
    }
}

// ============================================================================
// Public API
// ============================================================================
void InitScriptPanel()
{
    if (TheScriptEngine)
    {
        TheScriptEngine->setInspectorScriptObserver(&OnScriptEvent);
        TheScriptEngine->setInspectorActionObserver(&OnActionEvent);
    }
}

void ShutdownScriptPanel()
{
    if (TheScriptEngine)
    {
        TheScriptEngine->setInspectorScriptObserver(nullptr);
        TheScriptEngine->setInspectorActionObserver(nullptr);
    }
    g_state.clear();
    g_timeline.clear();
    s_selectedScript.clear();
}

void DrawScriptPanel()
{
    ImGui::SetNextWindowSize(ImVec2(720, 520), ImGuiCond_FirstUseEver);

    // Self-heal observer install. InitScriptPanel runs at Inspector
    // startup which is BEFORE TheScriptEngine exists in some build
    // configurations, so the if-guard there is no-op'd and the panel
    // would be empty forever. Reinstall here on every draw — it's a
    // single function-pointer assignment, idempotent, and survives
    // map reloads / engine resets that might null out the observer.
    if (TheScriptEngine)
    {
        if (TheScriptEngine->getInspectorScriptObserver() != &OnScriptEvent)
            TheScriptEngine->setInspectorScriptObserver(&OnScriptEvent);
        if (TheScriptEngine->getInspectorActionObserver() != &OnActionEvent)
            TheScriptEngine->setInspectorActionObserver(&OnActionEvent);
    }

    Visibility& vis = GetVisibility();
    if (!ImGui::Begin("Mission Script", &vis.script))
    {
        ImGui::End();
        return;
    }

    const UnsignedInt now = TheGameLogic ? (UnsignedInt)TheGameLogic->getFrame() : 0u;

    // -------- Top status bar --------
    ImGui::Text("Frame %u", (unsigned)now);
    ImGui::SameLine();
    if (TheScriptEngine)
    {
        // Frozen-time indicators are critical for diagnosing briefing
        // freezes — when a script calls CAMERA_MOD_FREEZE_TIME the
        // engine stops advancing logic until the camera move ends, but
        // rendering should still happen. If you see "TIME FROZEN" stuck
        // on for the duration of a black-screen briefing, the script
        // (not rendering) is the cause.
        const bool frozenScript = TheScriptEngine->isTimeFrozenScript() != FALSE;
        const bool frozenDebug  = TheScriptEngine->isTimeFrozenDebug()  != FALSE;
        if (frozenScript || frozenDebug)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                "[%s%s%s]",
                frozenScript ? "TIME FROZEN (script)" : "",
                (frozenScript && frozenDebug) ? " + " : "",
                frozenDebug ? "TIME FROZEN (debug)" : "");
            ImGui::SameLine();
        }
    }
    ImGui::Checkbox("Pause recording", &s_pauseRecording);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::DragInt("Highlight frames", &s_highlightFrames, 1, 1, 600);
    ImGui::SameLine();
    if (ImGui::Button("Clear stats"))
    {
        g_state.clear();
        g_timeline.clear();
    }

    if (TheSidesList == nullptr)
    {
        ImGui::TextDisabled("No game in progress (TheSidesList is null).");
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("##scripttabs"))
    {
        // ============== TAB 1: scripts tree + details ==============
        if (ImGui::BeginTabItem("Scripts"))
        {
            ImGui::SetNextItemWidth(220);
            ImGui::InputTextWithHint("##treefilter", "filter by name", s_treeFilter, IM_ARRAYSIZE(s_treeFilter));

            // Two-column layout: tree on the left, details on the right.
            ImGui::Columns(2, "##scriptcols", true);
            if (ImGui::GetColumnWidth(0) < 320.0f && ImGui::GetColumnOffset(1) == 0.0f)
                ImGui::SetColumnWidth(0, 320.0f);

            ImGui::BeginChild("##scripttree", ImVec2(0, 0), false);

            // Walk every side → top-level scripts → groups → group scripts.
            for (Int sideIdx = 0; sideIdx < TheSidesList->getNumSides(); ++sideIdx)
            {
                SidesInfo* side = TheSidesList->getSideInfo(sideIdx);
                if (side == nullptr) continue;
                ScriptList* sl = side->getScriptList();
                if (sl == nullptr) continue;

                // Side label — use the player's display name if we can match it.
                const char* sideLabel = "Side";
                Player* p = ThePlayerList ? ThePlayerList->getNthPlayer(sideIdx) : nullptr;
                if (p)
                {
                    AsciiString nm = p->getSide();
                    if (nm.isNotEmpty())
                        sideLabel = nm.str();
                }

                ImGui::PushID(sideIdx);
                if (ImGui::TreeNodeEx(sideLabel, ImGuiTreeNodeFlags_DefaultOpen, "%s (#%d)", sideLabel, sideIdx))
                {
                    // Top-level scripts (not in any group).
                    for (Script* s = sl->getScript(); s; s = s->getNext())
                        DrawScriptRow(s, now);

                    // Groups, each containing zero or more scripts.
                    int groupIdx = 0;
                    for (ScriptGroup* g = sl->getScriptGroup(); g; g = g->getNext(), ++groupIdx)
                    {
                        ImGui::PushID(groupIdx);
                        ImVec4 gcol = g->isActive()
                                          ? ImVec4(0.85f, 0.85f, 0.85f, 1.0f)
                                          : ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
                        ImGui::PushStyleColor(ImGuiCol_Text, gcol);
                        bool open = ImGui::TreeNodeEx(g->getName().str(),
                            ImGuiTreeNodeFlags_DefaultOpen,
                            "%s%s",
                            g->getName().str(),
                            g->isSubroutine() ? "  [sub]" : "");
                        ImGui::PopStyleColor();
                        if (open)
                        {
                            for (Script* s = g->getScript(); s; s = s->getNext())
                                DrawScriptRow(s, now);
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            ImGui::EndChild();

            // ---- Right column: details ----
            ImGui::NextColumn();
            ImGui::BeginChild("##scriptdetails", ImVec2(0, 0), false);
            Script* selected = FindScriptByNameSlow(s_selectedScript);
            DrawScriptDetails(selected, now);
            ImGui::EndChild();

            ImGui::Columns(1);
            ImGui::EndTabItem();
        }

        // ============== TAB 2: live timeline ==============
        if (ImGui::BeginTabItem("Timeline"))
        {
            // ---- Diagnostic line: observer status + event counts ----
            // If "engine: NO" the script engine isn't constructed yet
            // (likely you opened the panel before loading a mission).
            // If "script obs: NO" or "action obs: NO" the observer
            // pointer doesn't match ours — the self-heal at the top of
            // DrawScriptPanel should fix that on the next frame.
            const bool engineUp = (TheScriptEngine != nullptr);
            const bool scriptObsOk = engineUp &&
                (TheScriptEngine->getInspectorScriptObserver() == &OnScriptEvent);
            const bool actionObsOk = engineUp &&
                (TheScriptEngine->getInspectorActionObserver() == &OnActionEvent);
            ImGui::TextDisabled("engine: %s   script obs: %s   action obs: %s   events: %zu",
                engineUp ? "OK" : "NO",
                scriptObsOk ? "OK" : "NO",
                actionObsOk ? "OK" : "NO",
                g_timeline.size());
            ImGui::SameLine();
            // Synthetic test event so the user can verify the table
            // renders independently of whether the engine is producing
            // anything.
            if (ImGui::SmallButton("Test push"))
            {
                PushTimeline(now, TL_ACTION,
                    "TEST_EVENT",
                    "synthetic timeline entry from the panel");
            }

            // ---- Top filter bar ----
            ImGui::SetNextItemWidth(240);
            ImGui::InputTextWithHint("##timelinefilter", "filter by name (substring)",
                                     s_timelineFilter, IM_ARRAYSIZE(s_timelineFilter));
            ImGui::SameLine();
            ImGui::TextDisabled("(%zu / %zu)", g_timeline.size(), kTimelineMax);
            ImGui::SameLine();
            ImGui::Checkbox("Auto-scroll", &s_timelineAutoScroll);

            // Per-kind toggles. Defaults to all on; turn TRUE off if a
            // periodic eval is spamming the timeline and you only care
            // about actions, etc.
            ImGui::Checkbox("TRUE",   &s_showKind[TL_SCRIPT_TRUE]);   ImGui::SameLine();
            ImGui::Checkbox("FALSE",  &s_showKind[TL_SCRIPT_FALSE]);  ImGui::SameLine();
            ImGui::Checkbox("FORCE",  &s_showKind[TL_SCRIPT_FORCE]);  ImGui::SameLine();
            ImGui::Checkbox("TOGGLE", &s_showKind[TL_SCRIPT_TOGGLE]); ImGui::SameLine();
            ImGui::Checkbox("ACTION", &s_showKind[TL_ACTION]);

            // Quick preset buttons for the briefing-debugging workflow.
            // "Audio/Cinema" hides everything except the kinds of
            // actions you care about during a briefing freeze:
            // SPEECH_PLAY, SOUND_PLAY_NAMED, MOVIE_PLAY_*, CAMERA_*,
            // FADE, LETTERBOX, SET_INPUT, etc. Implementation: just
            // use the substring filter to match on common prefixes.
            if (ImGui::Button("Preset: All"))
            {
                for (int i = 0; i < TL_KIND_COUNT; ++i) s_showKind[i] = true;
                s_timelineFilter[0] = 0;
            }
            ImGui::SameLine();
            if (ImGui::Button("Preset: Actions only"))
            {
                for (int i = 0; i < TL_KIND_COUNT; ++i) s_showKind[i] = false;
                s_showKind[TL_ACTION] = true;
                s_timelineFilter[0] = 0;
            }
            ImGui::SameLine();
            if (ImGui::Button("Preset: Audio / Cinematic"))
            {
                for (int i = 0; i < TL_KIND_COUNT; ++i) s_showKind[i] = false;
                s_showKind[TL_ACTION] = true;
                // Empty filter; user can append a substring like
                // "CAMERA" or "SPEECH" if they want to narrow further.
                std::strncpy(s_timelineFilter, "", IM_ARRAYSIZE(s_timelineFilter));
            }
            ImGui::SameLine();
            if (ImGui::Button("Preset: Briefing freeze"))
            {
                // Show every action AND eval result so the user can see
                // what's running during a frozen briefing.
                for (int i = 0; i < TL_KIND_COUNT; ++i) s_showKind[i] = true;
                s_timelineFilter[0] = 0;
            }

            // ---- Timeline table ----
            // Use BeginTable with a sized scroll region so the table
            // fills the remaining tab body. Newest entry at the top so
            // the user always sees what just happened without scrolling.
            const ImVec2 tableSize = ImVec2(0, 0); // fill remaining
            if (ImGui::BeginTable("##timelinetbl", 4,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit |
                ImGuiTableFlags_Resizable, tableSize))
            {
                ImGui::TableSetupScrollFreeze(0, 1); // pin header
                ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Kind",  ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthFixed, 220.0f);
                ImGui::TableSetupColumn("Detail", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                // Iterate newest-first so the most recent event is at the top.
                for (auto it = g_timeline.rbegin(); it != g_timeline.rend(); ++it)
                {
                    if (it->kind < 0 || it->kind >= TL_KIND_COUNT)
                        continue;
                    if (!s_showKind[it->kind])
                        continue;
                    if (!MatchesFilter(it->label.c_str(), s_timelineFilter) &&
                        !MatchesFilter(it->detail.c_str(), s_timelineFilter))
                        continue;

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%u", (unsigned)it->frame);

                    ImGui::TableSetColumnIndex(1);
                    const char* kindStr = "?";
                    ImVec4 col(1, 1, 1, 1);
                    switch (it->kind)
                    {
                        case TL_SCRIPT_TRUE:   kindStr = "TRUE";    col = ImVec4(0.30f, 1.00f, 0.30f, 1.0f); break;
                        case TL_SCRIPT_FALSE:  kindStr = "FALSE";   col = ImVec4(1.00f, 0.55f, 0.30f, 1.0f); break;
                        case TL_SCRIPT_FORCE:  kindStr = "FORCE";   col = ImVec4(0.40f, 0.70f, 1.00f, 1.0f); break;
                        case TL_SCRIPT_TOGGLE: kindStr = "TOGGLED"; col = ImVec4(0.85f, 0.85f, 0.85f, 1.0f); break;
                        case TL_ACTION:        kindStr = "ACTION";  col = ImVec4(0.55f, 0.80f, 1.00f, 1.0f); break;
                    }
                    ImGui::TextColored(col, "%s", kindStr);

                    ImGui::TableSetColumnIndex(2);
                    // Clicking a script row jumps to the Scripts tab
                    // selection. Action rows can't jump (no parent
                    // script association captured), so they're just
                    // text.
                    if (it->kind == TL_ACTION)
                    {
                        ImGui::TextUnformatted(it->label.c_str());
                    }
                    else
                    {
                        if (ImGui::Selectable(it->label.c_str(), s_selectedScript == it->label,
                                              ImGuiSelectableFlags_SpanAllColumns))
                            s_selectedScript = it->label;
                    }

                    ImGui::TableSetColumnIndex(3);
                    if (!it->detail.empty())
                        ImGui::TextDisabled("%s", it->detail.c_str());
                    else
                        ImGui::TextDisabled("(%u frames ago)", (unsigned)(now - it->frame));
                }

                // Auto-scroll to top (which holds the newest entry on
                // each frame because we just iterated rbegin → rend).
                // ImGui's auto-scroll only checks scroll Y vs max so
                // setting Y=0 sticks the scroller to the freshest row.
                if (s_timelineAutoScroll)
                    ImGui::SetScrollY(0.0f);

                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        // ============== TAB 3: counters & flags ==============
        if (ImGui::BeginTabItem("Counters / Flags"))
        {
            if (TheScriptEngine == nullptr)
            {
                ImGui::TextDisabled("Script engine not available.");
            }
            else
            {
                // Two side-by-side tables for counters and flags. Layout
                // uses BeginChild so each table can scroll independently
                // when a mission has lots of script vars.
                const float halfW = ImGui::GetContentRegionAvail().x * 0.5f - 4.0f;

                ImGui::BeginChild("##counters_child", ImVec2(halfW, 0), true);
                ImGui::Text("Counters (%d)", (int)TheScriptEngine->inspectorGetNumCounters() - 1);
                ImGui::Separator();
                if (ImGui::BeginTable("##counters", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
                {
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableHeadersRow();
                    // Index 0 is the sentinel; the engine starts at 1.
                    for (Int i = 1; i < TheScriptEngine->inspectorGetNumCounters(); ++i)
                    {
                        const TCounter* c = TheScriptEngine->inspectorGetCounterByIndex(i);
                        if (!c) continue;
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%s%s", c->name.str(), c->isCountdownTimer ? " (timer)" : "");
                        ImGui::TableSetColumnIndex(1);
                        ImGui::PushID(i);
                        Int val = c->value;
                        if (ImGui::InputInt("##val", (int*)&val, 1, 10,
                                            ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            TheScriptEngine->inspectorSetCounterValue(c->name, val);
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("##flags_child", ImVec2(halfW, 0), true);
                ImGui::Text("Flags (%d)", (int)TheScriptEngine->inspectorGetNumFlags() - 1);
                ImGui::Separator();
                if (ImGui::BeginTable("##flags", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
                {
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableHeadersRow();
                    for (Int i = 1; i < TheScriptEngine->inspectorGetNumFlags(); ++i)
                    {
                        const TFlag* f = TheScriptEngine->inspectorGetFlagByIndex(i);
                        if (!f) continue;
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%s", f->name.str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::PushID(i);
                        bool v = f->value != FALSE;
                        if (ImGui::Checkbox("##val", &v))
                            TheScriptEngine->inspectorSetFlagValue(f->name, v ? TRUE : FALSE);
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace Panels
} // namespace Inspector
