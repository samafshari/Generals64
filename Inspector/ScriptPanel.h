// Mission Script debugger panel for the ImGui Inspector.
//
// Lives in a separate .cpp from Panels.cpp because the volume of
// engine-side ScriptEngine / Scripts.h type knowledge it pulls in is
// large enough to slow down compilation of the rest of the panel
// module noticeably. Also keeps the live state cache (per-script
// fire history, condition results, recent-fires ring buffer) in
// one self-contained translation unit so it's easy to find and
// extend.
//
// The public surface here is intentionally tiny: Init() registers
// the engine-side observer hook (so the panel doesn't miss any
// fires that happen before the panel window is first opened),
// Shutdown() unregisters it, and DrawScriptPanel() is called from
// Panels::DrawAll() when the visibility flag is on.
#pragma once

namespace Inspector
{
namespace Panels
{

// Hook the script engine observer so the panel records every script
// evaluation regardless of whether the window is currently visible.
// Safe to call multiple times — repeated calls re-install the same
// observer. Called from Panels::Init().
void InitScriptPanel();

// Unhook the observer and clear the recorded state. Called from
// Panels::Shutdown().
void ShutdownScriptPanel();

// Draw the panel for this frame. Called from Panels::DrawAll() when
// s_visibility.script is true. Cheap when the panel is hidden — the
// observer keeps recording either way so opening the panel mid-game
// shows real history rather than starting from zero.
void DrawScriptPanel();

} // namespace Panels
} // namespace Inspector
