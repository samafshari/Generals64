// Inspector panels — log, hierarchy, properties, players.
//
// All panels live in this module so the main Inspector.cpp stays small
// and only deals with init/shutdown/event/render plumbing. The public
// API here is intentionally engine-type-free (ObjectID is exposed as
// uint32_t) so that Inspector.h doesn't have to drag in GameLogic.h.
#pragma once

#include <cstdint>

namespace Inspector
{
namespace Panels
{

// Per-panel visibility flags. The main Inspector toolbar mutates these
// via a "View" menu so the user can show/hide individual panels.
struct Visibility
{
    bool log           = true;
    bool hierarchy     = true;
    bool properties    = true;
    bool players       = true;
    bool minimap       = true;
    bool perfHud       = true;
    bool script        = false; // Mission script debugger (off by default)
    bool game          = true;  // The "game viewport" ImGui window
    bool model         = false; // Model debugger (mesh tree + textures + decals)
    bool renderToggles = false; // Render-pass / post-fx toggles (replaces old F9 menu)
    bool destruction   = false; // Destruction Timeline (gantt of unit losses per player per minute)
    bool lights        = false; // Lights debugger (sun direction + color sliders, sun gizmo)
    bool launchParams  = false; // Launch parameters debugger (raw command line + parsed flags)
    bool shadows       = false; // Sun shadow-map tuning (darkness, bias, debug modes, tooltip, map preview)
};

// Per-frame debug-draw overlay toggles. The toolbar mutates these via
// a "Debug Draw" menu. Each flag controls a global visualization that
// runs every frame regardless of the current selection.
struct DebugOverlayFlags
{
    bool showWorldOrigin = false;  // RGB axis gizmo at world (0,0,0)
    bool showAllBounds   = false;  // faint wireframe around every object
    bool showAllHeadings = false;  // short heading line on every object
};

// One-time hookup of engine integration points. Currently registers
// the DEBUG_LOG sink so the log panel captures every line. Safe to
// call multiple times — repeated calls re-register the same sink.
void Init();

// Tear down engine integrations (clears the DEBUG_LOG sink). Idempotent.
void Shutdown();

// Draw every enabled panel for this frame. Called from
// Inspector::BeginFrame after ImGui::NewFrame has run, while ImGui is
// in a state that accepts Begin/End window calls.
void DrawAll();

// Mutable visibility state — toolbar reads/writes this.
Visibility& GetVisibility();

// Mutable debug overlay flags — toolbar reads/writes this.
DebugOverlayFlags& GetDebugFlags();

// Force every panel to snap to its predetermined non-overlapping
// layout on the next frame. Called automatically by Inspector::Init
// the first time the inspector becomes visible (so users see a clean
// layout instead of all panels stacked at ImGui's default 60,60).
// Also exposed via the toolbar's View menu so users can reset after
// dragging windows around.
void RequestLayoutReset();

// True if the given OS-window pixel coordinate is inside the visible
// content area of the "Game" ImGui window (i.e., the rectangle where
// the off-screen game render target is drawn). Inspector::ProcessEvent
// calls this to decide whether a left-click in pick mode should be
// intercepted as a pick (cursor over game pixels) or passed through
// to ImGui (cursor over a docked panel). Returns false if the Game
// window is hidden, undocked, or the cursor is outside its image.
bool IsPointInGameViewport(int x, int y);

// Remap an OS-window pixel coordinate to engine resolution coords
// for forwarding mouse events to the game when the cursor is inside
// the Game window. Used by Inspector::ProcessEvent to translate
// SDL mouse events so the engine's input handlers see consistent
// "where in the game image did this happen" coordinates regardless
// of how the user has dragged or resized the Game panel.
//
// Returns a struct with valid=true if the source point was inside
// the displayed game image, and valid=false otherwise. When valid,
// outX/outY are the remapped absolute coords and scaleX/scaleY are
// the multiplier you should apply to delta values (xrel/yrel) so
// they match the same coordinate space.
struct GameViewportTransform
{
    bool  valid   = false;
    int   outX    = 0;
    int   outY    = 0;
    float scaleX  = 1.0f;
    float scaleY  = 1.0f;
};
GameViewportTransform RemapPointToGameViewport(int osX, int osY);

} // namespace Panels

namespace Selection
{

// Currently selected ObjectID, or 0 if nothing is selected. Exposed as
// uint32_t to keep the public header free of engine types — the impl
// casts to/from the engine's ObjectID typedef internally.
uint32_t GetObjectID();
void SetObjectID(uint32_t id);
void Clear();
bool HasSelection();

// --- Pick mode ---
//
// When pick mode is on, mouse clicks in the viewport (not over an
// ImGui panel) update the selection instead of dispatching to the
// engine's RTS click handler. Hover state is also updated each frame
// while pick mode is on so the user gets a yellow highlight + tooltip
// showing what's under the cursor.
bool IsPickMode();
void SetPickMode(bool on);
void TogglePickMode();

// --- Tooltip mode ---
//
// When tooltip mode is on, hovering any object in the viewport pops
// up a large floating tooltip with the full Properties panel content
// (name, ID, owner, position, orientation, health, AI state, etc.).
// Independent from pick mode — clicks pass through to the engine, so
// you can browse object info without committing a selection.
bool IsTooltipMode();
void SetTooltipMode(bool on);
void ToggleTooltipMode();

// Object the cursor is currently over (set by Panels.cpp from a
// per-frame pick at the mouse position). Zero if nothing is hovered
// or pick mode is off. Independent from selection so the user can
// see "what would I click" before committing.
uint32_t GetHoverID();
void SetHoverID(uint32_t id);

// --- Multi-tag selection ---
//
// Tags are a SET of object IDs that get persistent highlights every
// frame, separately from the single "selection" pointer. The user
// builds tag sets by Ctrl-clicking objects in pick mode (which adds
// or removes the tag instead of replacing the selection). Useful for
// watching multiple units at once during AI debugging — e.g. tag a
// builder, a tank, and a base, then watch the AI cycle between them.
void TagAdd(uint32_t id);
void TagRemove(uint32_t id);
void TagToggle(uint32_t id);
void TagClear();
bool IsTagged(uint32_t id);
size_t TagCount();
// Iterate. Pass a callback that takes a uint32_t. Implemented as a
// snapshot so it's safe for the callback to mutate the tag set.
void TagForEach(void (*fn)(uint32_t id, void* user), void* user);

// Pending viewport-click queue. Inspector::ProcessEvent stuffs the
// click coords here when in pick mode and the user left-clicks the
// viewport; Panels::DrawAll consumes the queue and performs the
// engine pickDrawable() call (which can't be done from ProcessEvent
// because that file doesn't see engine types).
//
// Click variant: NORMAL replaces selection; TOGGLE adds/removes a
// tag (Ctrl-click semantics).
enum ClickKind { CLICK_NORMAL = 0, CLICK_TOGGLE = 1 };
void QueueClick(int x, int y, int kind);
bool ConsumePendingClick(int* outX, int* outY, int* outKind);

} // namespace Selection
} // namespace Inspector
