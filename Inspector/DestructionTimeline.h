// Destruction Timeline — records every unit/structure death and exposes
// a wide ImGui Gantt-style panel that shows who lost what when, bucketed
// by 1-minute chunks of game time.
//
// The push API is intentionally engine-type-free so engine code (currently
// Object::onDie()) can include this header without dragging in any
// inspector / ImGui / D3D11 dependencies. Implementation lives entirely
// in DestructionTimeline.cpp and is compiled in both D3D11 and Vulkan
// builds (the panel draw is gated by the panel visibility flag like the
// rest of the inspector — it's a no-op when the window is hidden).
//
// Storage is a fixed-size mutex-protected ring buffer so the recorder
// is safe to call from any thread, never allocates after warm-up, and
// never blocks the game logic for more than a single CAS-grade lock.
#pragma once

namespace Inspector
{
namespace Destruction
{

// Push a death record into the ring. Called from Object::onDie() once
// per dying object regardless of cause (combat, self-destruct, expire).
//
// playerIndex   — owning player slot index, or -1 if no controlling
//                 player (e.g. neutral/civilian objects).
// templateName  — ThingTemplate name like "AmericaTankCrusader". Pointer
//                 must be valid for the duration of the call; we copy
//                 internally because templates can outlive a game but
//                 strings are cheap to dup.
// colorARGB     — owning player's color as a 32-bit ARGB value (matches
//                 Player::getPlayerColor()'s wire format). Used for the
//                 marker color so the panel doubles as a faction legend.
// sideName      — short side string like "America" / "China" / "GLA",
//                 or "" for neutral. Pointer lifetime same as templateName.
// frame         — logic frame number when the death occurred. We bucket
//                 by minute as `frame / LOGICFRAMES_PER_SECOND / 60`.
// selfInflicted — true when the killer is the same object as the victim
//                 (sold building, expire-on-timer, suicide infantry).
// kindMask      — coarse classification used to color/icon the marker.
//                 0 = unit, 1 = vehicle, 2 = structure, 3 = infantry,
//                 4 = aircraft. Free-form; only used for tooltip text.
void RecordDeath(int          playerIndex,
                 const char*  templateName,
                 unsigned int colorARGB,
                 const char*  sideName,
                 unsigned int frame,
                 bool         selfInflicted,
                 int          kindMask);

// Drop every recorded death. Called automatically by the panel when it
// detects a frame regression (new game / map reload starts at frame 0
// while we still have records from the previous match), and exposed for
// the in-panel "Clear" button.
void Clear();

// Number of records currently held. Cheap; intended for the panel
// header line ("123 deaths recorded since game start").
int Count();

// Draw the ImGui Destruction Timeline panel for this frame. Pulls a
// snapshot of the ring buffer under the lock, then renders a horizontal
// gantt chart with one row per player, time on the X axis bucketed by
// minute. Called from Panels::DrawAll() when the visibility flag is on.
//
// Implemented in DestructionTimeline.cpp so the heavy ImGui surface
// stays out of Panels.cpp's translation unit (compile-time win).
void DrawPanel(bool* pOpen);

} // namespace Destruction
} // namespace Inspector
