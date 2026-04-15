// HudPanels — ImGui-based replacements for the in-game HUD elements
// (resources, radar, selection info, build queue, general powers).
// Each panel mirrors the data the original game UI reads, so the
// in-game state stays 1:1 with the original control bar — disabled
// buttons are disabled, in-progress builds show progress, etc.
//
// Note: there is no ImGui replacement for the command bar. The
// original ControlBar at the bottom of the screen remains the sole
// command-issuing UI, even when the inspector overlay is active.
//
// This is intended as a foundation for a modernized widescreen-friendly
// game UI without the stretched legacy assets — the styling is meant
// to look "Palantir mission control": dark backgrounds, glowy borders,
// monospace numerics, faction colors, color-coded health.
//
// All panels are HIDDEN by default. The user enables them via the
// Inspector toolbar's "Modern HUD" menu.
#pragma once

namespace Inspector
{
namespace Hud
{

// Per-panel visibility flags. Toolbar reads/writes via GetVisibility().
struct Visibility
{
    bool resources  = false;
    bool radar      = false;
    bool selection  = false;
    bool buildQueue = false;
    bool generals   = false;
    bool teamColors = false;  // Color-wheel editor for every player
};

// Lifecycle hooks (kept symmetric with Panels::Init/Shutdown for the
// Inspector to call). Currently no-ops; reserved for future caching
// of icons, command-set lookups, etc.
void Init();
void Shutdown();

// Render every enabled HUD panel. Called from Panels::DrawAll after
// the inspector debug panels render so the modern HUD draws on top
// of the dock layout.
void DrawAll();

// Mutable visibility state — toolbar reads/writes this.
Visibility& GetVisibility();

} // namespace Hud
} // namespace Inspector
