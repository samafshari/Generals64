// Inspector — in-process Dear ImGui debug overlay for Generals Remastered.
//
// Phase 1: minimal ImGui bring-up, F10 toggle, demo window. The Inspector
// module is intentionally tiny and self-contained: W3DDisplay only needs
// to call five functions (Init / Shutdown / BeginFrame / Render / handle
// events) and the SDL pump only calls ProcessEvent.
//
// Future phases will add docking layouts, scene picking, gizmos, free-fly
// camera, pause/step, and the actual inspector panels (objects, scripts,
// players, particles, log).
#pragma once

#include <cstdint>

struct SDL_Window;
union SDL_Event;

#ifdef BUILD_WITH_D3D11
struct ID3D11Device;
struct ID3D11DeviceContext;
#endif

namespace Inspector
{

#ifdef BUILD_WITH_D3D11
// Bring up ImGui + the SDL3 input backend + the D3D11 render backend.
// Returns false if any step fails. Safe to call multiple times — Init is
// a no-op once initialized.
bool Init(SDL_Window* window, ID3D11Device* device, ID3D11DeviceContext* context);
#endif

// Tear down ImGui and its backends. Safe to call when not initialized.
void Shutdown();

// Returns true once Init has succeeded.
bool IsInitialized();

// Master visibility toggle (F10). When disabled, the inspector skips all
// drawing and BeginFrame/Render become cheap no-ops, but ImGui still
// processes events so the toggle key remains responsive.
bool IsEnabled();
void SetEnabled(bool enabled);

// Forward an SDL event to ImGui. Returns true if the inspector consumed
// the event and the engine should ignore it (e.g. mouse click on a panel).
// Always called from SDLPlatform::PumpEvents before the engine event
// callback runs, so ImGui has first crack at input.
bool ProcessEvent(const SDL_Event* event);

// Convenience accessors used by the SDL pump to gate input forwarding
// while a panel has focus. WantCaptureMouse/Keyboard track ImGui's
// per-frame intent — when true, the engine should not see that input.
bool WantCaptureMouse();
bool WantCaptureKeyboard();

// --- Pause / step control ---
//
// The inspector owns a global pause flag that GameEngine::update()
// consults to gate logic ticks. Rendering keeps running while paused
// so the user can fly the camera around and inspect a frozen game.
//
// IsPaused returns whether the user has pressed Pause.
// TryConsumeStep returns true exactly once after a step is requested
// (and only when paused) — that single tick is allowed through, then
// pause snaps back on. This is how Unity/Unreal step buttons work.
bool IsPaused();
void SetPaused(bool paused);
void RequestStep(int frames = 1);
bool TryConsumeStep();

// Per-frame entry point. Calls ImGui_ImplDX11_NewFrame +
// ImGui_ImplSDL3_NewFrame + ImGui::NewFrame, then submits whatever
// inspector windows are currently visible. Must be called once per
// rendered frame, paired with Render() at the end of the frame.
void BeginFrame();

// Push a printf-style line into the Inspector's Log panel AND mirror
// it to stderr. Works in Release builds (the regular DEBUG_LOG macro
// is compiled out there). Thread-safe. Use sparingly from hot paths —
// the ring has a bounded size so spam pushes older lines out.
//
// Intended for diagnostics that need to surface in the in-game
// overlay, e.g. asset-resolution failures from the search-path system.
void Log(const char* fmt, ...);

// Submit the current ImGui frame to the swap chain. Must be called
// AFTER all engine rendering for the frame and BEFORE Present.
// Internally re-binds the backbuffer in case the engine left a
// post-process render target bound.
void Render();

// --- Free-fly editor camera ---
//
// Unreal-style WASD + QE + RMB-look override of the engine's
// tactical camera. While active, the engine still updates its own
// view (so its camera-area constraints / cutscenes / etc don't
// crash) but the inspector overwrites the actual camera transform
// each frame after updateViews().
namespace Camera
{
    bool IsActive();
    void SetActive(bool on);
    void Toggle();

    // Called once per frame from Inspector::BeginFrame to read input
    // (SDL keyboard state, ImGui mouse state) and advance the camera.
    void Update(float dtSeconds);

    // Called from W3DDisplay::draw immediately after updateViews()
    // so the override clobbers the engine's freshly-recomputed camera
    // transform before any 3D rendering uses it.
    void ApplyToEngineCamera();

    // Tunable settings — exposed so the toolbar/panel can edit them.
    float& MoveSpeed();      // world units per second at 1x sprint
    float& MouseSensitivity();
}

} // namespace Inspector
