// AIPanels — debug visualizers for AI decision making, pathfinding,
// state machines, and unit-task kanban boards.
//
// These are developer-oriented (not player-facing) — they expose the
// internal AI bookkeeping the engine normally hides. Useful for:
//   - debugging cutscene scripts (state machine view)
//   - investigating "why isn't this unit moving" (pathfinder view)
//   - understanding AI opponent builds (build list view)
//   - spotting stuck units (kanban activity view)
//
// All panels live behind a new "AI Debug" menu in the Inspector
// toolbar, hidden by default.
#pragma once

namespace Inspector
{
namespace Ai
{

struct Visibility
{
    bool stateMachine   = false;  // Selected unit's AI state + flags
    bool pathfinder     = false;  // Selected unit's computed path + stats
    bool kanbanActivity = false;  // All units grouped by what they're doing
    bool kanbanProd     = false;  // All production buildings as columns
    bool buildLists     = false;  // AI players' build list state
    bool modelDebugger  = false;  // Selected object's render-obj mesh hierarchy + AABBs
    bool entityGizmos   = false;  // In-world per-unit AI/path gizmos (arrows, paths, ranges)
};

void Init();
void Shutdown();
void DrawAll();

Visibility& GetVisibility();

} // namespace Ai
} // namespace Inspector
