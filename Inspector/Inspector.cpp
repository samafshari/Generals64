// Inspector — in-process Dear ImGui debug overlay implementation.
//
// Phase 1 scope: bring up ImGui, hook input via SDL3 backend, render via
// the D3D11 backend, expose an F10 toggle, and draw a hello-world panel
// plus the ImGui demo window so we can validate that input + rendering
// are wired up correctly. Everything inspector-panel-shaped lives in
// later phases.
//
// Build matrix: under BUILD_WITH_D3D11 the full ImGui pipeline is active.
// Under BUILD_WITH_VULKAN (DebugVK / ReleaseVK configs) every entry
// point is a no-op stub for now — adding the Vulkan backend is a Phase 2
// follow-up so we don't block the Phase 1 hello-world on porting two
// backends at once.

#include "Inspector.h"
#include "Panels.h"
#include "HudPanels.h"
#include "AIPanels.h"

#ifdef BUILD_WITH_D3D11

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_sdl3.h"

// Embedded Roboto-Medium.ttf — this header was generated with `xxd -i`
// from extern/imgui/fonts/Roboto-Medium.ttf and patched to use static
// const linkage so it has internal scope per translation unit.
// Embedding the font means there's nothing to deploy alongside the exe.
#include "fonts/Roboto-Medium.ttf.h"

#include <SDL3/SDL.h>
#include <d3d11.h>

// Pull the renderer in so Render() can rebind the backbuffer before
// ImGui draws. Phase 1 doesn't need any other engine state — that's
// what makes the module easy to evolve later.
#include "Renderer.h"
#include "Core/Device.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

// C-bridges in D3D11Shims.cpp — keeps Inspector.cpp free of W3D /
// CameraClass / Matrix3D headers (and the DX8-era includes they
// transitively pull in). The two functions are:
//
//   InspectorReadGameCamera     → seed the free-fly cam from the engine
//   InspectorApplyCameraOverride → push our transform back into the
//                                  engine's CameraClass each frame
extern "C" bool InspectorReadGameCamera(
    float* outX, float* outY, float* outZ,
    float* outYaw, float* outPitch);
extern "C" void InspectorApplyCameraOverride(
    float posX, float posY, float posZ,
    float yaw, float pitch);

// Pulls the active cursor tooltip from the engine's Mouse singleton. Defined
// in Core/.../Mouse.cpp (same TU as TheMouse). Returns 1 if a tooltip should
// be drawn this frame (and fills `outBuf`, `outX`, `outY`), 0 otherwise.
extern "C" int InspectorQueryGameTooltip(char* outBuf, int bufSize, int* outX, int* outY);

namespace Inspector
{

namespace
{
    bool s_initialized = false;
    bool s_enabled = false;          // start hidden — F10 reveals
    bool s_showDemo = false;         // demo hidden by default; toggle from toolbar
    bool s_showToolbar = true;       // play/pause toolbar visible whenever inspector is on
    SDL_Window* s_window = nullptr;

    // Pause/step state. The simulation loop in GameEngine::update reads
    // these via Inspector::IsPaused() / TryConsumeStep() to gate the
    // logic tick. Both default to "not paused" so toggling the inspector
    // doesn't accidentally freeze gameplay on first run.
    bool s_paused = false;
    int  s_pendingStepFrames = 0;

    // Frame-scoped tooltip — populated by SetFrameTooltip (from Mouse::
    // drawTooltip) and consumed+cleared by Render(). One tooltip per
    // frame is plenty since the game only ever shows one at a time.
    std::string s_tooltipText;
    int         s_tooltipX = 0;
    int         s_tooltipY = 0;
    bool        s_tooltipPending = false;

    // Render `text` as a tooltip body, emitting the character immediately
    // after each `&` hotkey marker in a highlight color so the shortcut
    // letter stands out. `&&` is treated as a literal `&`. The text may
    // contain embedded '\n' for multi-line tooltips.
    //
    // Implemented as a sequence of TextUnformatted calls concatenated via
    // SameLine(0, 0). ImGui's text wrapping still works per-segment so
    // long tooltips wrap at the caller's PushTextWrapPos; we only split on
    // '&' markers, which are at most a couple per line in practice.
    void RenderTooltipTextWithHotkeys(const char* text)
    {
        if (!text || !*text) return;
        const ImU32 kHotkeyCol = IM_COL32(255, 212, 64, 255); // warm gold

        const char* p = text;
        bool sameLine = false;

        auto emitNormal = [&](const char* start, const char* end) {
            if (start >= end) return;
            if (sameLine) ImGui::SameLine(0, 0);
            ImGui::TextUnformatted(start, end);
            sameLine = true;
        };
        auto emitHotkey = [&](const char* c) {
            if (sameLine) ImGui::SameLine(0, 0);
            ImGui::PushStyleColor(ImGuiCol_Text, kHotkeyCol);
            ImGui::TextUnformatted(c, c + 1);
            ImGui::PopStyleColor();
            sameLine = true;
        };

        while (*p)
        {
            // Scan a single line; '\n' separates lines and resets the
            // same-line chain so the next segment lands on a new row.
            const char* lineStart = p;
            while (*p && *p != '\n') ++p;
            const char* lineEnd = p;

            if (lineStart == lineEnd)
            {
                // Blank line between paragraphs.
                ImGui::NewLine();
                sameLine = false;
            }
            else
            {
                sameLine = false;
                const char* run = lineStart;
                for (const char* q = lineStart; q < lineEnd; )
                {
                    if (*q == '&' && q + 1 < lineEnd)
                    {
                        if (q[1] == '&')
                        {
                            // Escaped ampersand: emit through the first '&',
                            // skip the second. The engine uses this to show
                            // a literal '&' in labels.
                            emitNormal(run, q + 1);
                            q += 2;
                            run = q;
                        }
                        else if ((unsigned char)q[1] > ' ')
                        {
                            emitNormal(run, q);
                            emitHotkey(q + 1);
                            q += 2;
                            run = q;
                        }
                        else
                        {
                            ++q;
                        }
                    }
                    else
                    {
                        ++q;
                    }
                }
                emitNormal(run, lineEnd);
            }

            if (*p == '\n') ++p;
        }
    }
}

bool Init(SDL_Window* window, ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (s_initialized)
        return true;

    if (!window || !device || !context)
        return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Don't write imgui.ini next to the working directory — it pollutes
    // GameData/ and the file path differs between launch dirs anyway.
    // Phase 2 will route this to a proper user settings location.
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    // Load Roboto-Medium at the target pixel size from embedded TTF
    // bytes. Loading at the final size (rather than scaling the
    // default 13px font 2x) gives crisp glyphs because freetype
    // rasterizes at the requested pixel grid. AddFontFromMemoryTTF
    // takes ownership of the buffer by default — pass FontDataOwnedByAtlas
    // = false in the config so ImGui doesn't try to free our static
    // const array on shutdown.
    constexpr float kFontPixels = 22.0f;  // ~roughly 2x the 13px default
    constexpr float kUIScale = 1.6f;      // matching scale for padding/spacing
    {
        ImFontConfig fontCfg{};
        fontCfg.FontDataOwnedByAtlas = false;
        // The cast to non-const is required because the ImGui API
        // signature predates const-correctness here; the buffer is
        // never written to when FontDataOwnedByAtlas is false.
        io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(RobotoMedium_ttf),
            (int)RobotoMedium_ttf_len,
            kFontPixels,
            &fontCfg);
    }
    ImGui::GetStyle().ScaleAllSizes(kUIScale);

    if (!ImGui_ImplSDL3_InitForD3D(window))
    {
        ImGui::DestroyContext();
        return false;
    }

    if (!ImGui_ImplDX11_Init(device, context))
    {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    s_window = window;
    s_initialized = true;

    // Hook engine integrations (DEBUG_LOG sink, etc.) once the
    // foundation is up. Panels::Init is idempotent so a second
    // Inspector::Init call won't double-register.
    Panels::Init();

    return true;
}

void Shutdown()
{
    if (!s_initialized)
        return;

    // Unhook engine integrations BEFORE the ImGui context goes away —
    // panels can still try to push log lines from background threads
    // briefly during shutdown.
    Panels::Shutdown();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    s_window = nullptr;
    s_initialized = false;
    s_enabled = false;
}

bool IsInitialized()
{
    return s_initialized;
}

bool IsEnabled()
{
    return s_enabled;
}

void SetEnabled(bool enabled)
{
    const bool wasEnabled = s_enabled;

    // Trigger a one-shot panel layout reset on every off→on
    // transition so the panels always come up in a non-overlapping
    // arrangement instead of stacked at ImGui's default position.
    // Once the panels are showing, the user can drag them around
    // freely; the next F10 cycle will re-spread them.
    if (enabled && !s_enabled)
        Panels::RequestLayoutReset();
    s_enabled = enabled;

    // Toggle the renderer's "game viewport" redirect — when the
    // inspector is on, the engine renders into an off-screen texture
    // and the Game window samples it; when off, the engine renders
    // straight to the swap chain backbuffer like normal.
    if (s_initialized)
        Render::Renderer::Instance().EnableGameViewport(enabled);

    // On enabled→disabled, ImGui's SDL3 backend may have left the OS
    // cursor hidden (it tracks cursor visibility based on whether any
    // ImGui window is hovered). The game's SDLMouse::setVisibility
    // only runs on game-side intent changes, so it won't undo the hide
    // until the user happens to interact with menu UI again. Force the
    // cursor visible here so the game cursor is back the instant F10
    // closes the inspector.
    if (wasEnabled && !enabled && s_window)
    {
        SDL_ShowCursor();
        SDL_SetWindowRelativeMouseMode(s_window, false);
    }
}

bool ProcessEvent(const SDL_Event* event)
{
    if (!s_initialized || !event)
        return false;

    // Intercept F10 to toggle the inspector BEFORE handing the event to
    // ImGui — otherwise an open inspector panel could swallow the press
    // and the user couldn't dismiss it. We still pass the event through
    // to ImGui afterwards so it sees the key release. Route through
    // SetEnabled so the off→on transition triggers a panel layout
    // reset (panels spread out instead of overlapping).
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_F10 && !event->key.repeat)
    {
        SetEnabled(!s_enabled);
    }

    ImGui_ImplSDL3_ProcessEvent(event);

    if (!s_enabled)
        return false;

    // Decision tree for routing each event:
    //
    //   * Mouse / wheel events:
    //       - cursor INSIDE the Game window image:
    //           - in pick mode: intercept left-click for picking
    //             (return true), let other buttons pass through
    //             remapped to engine
    //           - otherwise: REMAP coords to engine resolution and
    //             forward to engine (return false)
    //       - cursor OUTSIDE the Game window: respect WantCaptureMouse
    //         so panels work normally
    //
    //   * Keyboard events: respect WantCaptureKeyboard so ImGui text
    //     fields can swallow input but otherwise pass through to the
    //     engine for in-game hotkeys.
    //
    // We modify event coords in place via const_cast — SDL is done
    // with the event after PollEvent returns from the dispatcher,
    // and the engine input layer reads coords from the same struct.
    const ImGuiIO& io = ImGui::GetIO();
    SDL_Event* mevt = const_cast<SDL_Event*>(event);

    switch (event->type)
    {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    {
        const int cx = (int)event->button.x;
        const int cy = (int)event->button.y;
        const Panels::GameViewportTransform t =
            Panels::RemapPointToGameViewport(cx, cy);

        if (t.valid)
        {
            // Cursor is inside the game image rect.
            // Pick mode left-click → intercept (don't pass to engine).
            // Ctrl+left-click → toggle tag.
            if (event->button.button == SDL_BUTTON_LEFT && Selection::IsPickMode())
            {
                const SDL_Keymod mods = SDL_GetModState();
                const bool ctrlDown = (mods & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL)) != 0;
                const int kind = ctrlDown ? Selection::CLICK_TOGGLE : Selection::CLICK_NORMAL;
                Selection::QueueClick(cx, cy, kind);
                return true;
            }
            // Otherwise: remap and forward to engine
            mevt->button.x = (float)t.outX;
            mevt->button.y = (float)t.outY;
            return false;
        }
        // Outside the game viewport — let WantCaptureMouse decide
        return io.WantCaptureMouse;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        const int cx = (int)event->button.x;
        const int cy = (int)event->button.y;
        const Panels::GameViewportTransform t =
            Panels::RemapPointToGameViewport(cx, cy);

        if (t.valid)
        {
            // Pick mode swallows the matching release for left button
            if (event->button.button == SDL_BUTTON_LEFT && Selection::IsPickMode())
                return true;
            mevt->button.x = (float)t.outX;
            mevt->button.y = (float)t.outY;
            return false;
        }
        return io.WantCaptureMouse;
    }
    case SDL_EVENT_MOUSE_MOTION:
    {
        const int mx = (int)event->motion.x;
        const int my = (int)event->motion.y;
        const Panels::GameViewportTransform t =
            Panels::RemapPointToGameViewport(mx, my);

        if (t.valid)
        {
            // Remap absolute coords AND scale relative deltas so the
            // engine's drag detection sees consistent units.
            mevt->motion.x = (float)t.outX;
            mevt->motion.y = (float)t.outY;
            mevt->motion.xrel *= t.scaleX;
            mevt->motion.yrel *= t.scaleY;
            return false;
        }
        return io.WantCaptureMouse;
    }
    case SDL_EVENT_MOUSE_WHEEL:
    {
        // SDL3 wheel events carry mouse_x / mouse_y as the cursor
        // position at the time of the wheel event. Use those for
        // the remap test.
        const int wx = (int)event->wheel.mouse_x;
        const int wy = (int)event->wheel.mouse_y;
        const Panels::GameViewportTransform t =
            Panels::RemapPointToGameViewport(wx, wy);
        if (t.valid)
        {
            // Wheel events don't carry absolute pixel coords that
            // need remapping (the engine reads the wheel delta), so
            // just forward as-is.
            return false;
        }
        return io.WantCaptureMouse;
    }

    case SDL_EVENT_KEY_UP:
        // KEY_UP events must ALWAYS reach the engine — even when
        // ImGui is consuming the press stream — so the engine's
        // `Keyboard::m_keyStatus` gets the release and stops
        // synthesising autorepeat events for that key. Without this
        // asymmetric carve-out, the sequence "user presses backspace
        // while game has focus → user focuses an ImGui text field →
        // user releases backspace" swallows the UP event and leaves
        // `m_keyStatus[KEY_BACKSPACE].state` stuck in KEY_STATE_DOWN
        // forever. `Keyboard::checkKeyRepeat()` then fires one
        // synthetic MSG_RAW_KEY_DOWN + KEY_STATE_AUTOREPEAT per
        // frame, manifesting as "textboxes erase text as if
        // someone's holding backspace" — which was exactly the bug
        // this fix addresses.
        return false;

    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_TEXT_EDITING:
        // Route ALL keys to the engine by default — including ESC
        // (skip videos), hotkeys, and game commands. The only time
        // we block is when an ImGui text field is actively focused
        // for typing (e.g., the log filter or hierarchy filter
        // input boxes), so typing letters there doesn't double-fire
        // game hotkeys.
        //
        // io.WantTextInput is the precise "I'm typing in a text
        // field" flag. io.WantCaptureKeyboard is much broader (true
        // whenever any ImGui window has nav focus, which is always
        // when the dockspace is up) and was previously eating ESC.
        return io.WantTextInput;

    default:
        return false;
    }
}

bool WantCaptureMouse()
{
    return s_initialized && s_enabled && ImGui::GetIO().WantCaptureMouse;
}

bool WantCaptureKeyboard()
{
    return s_initialized && s_enabled && ImGui::GetIO().WantCaptureKeyboard;
}

// --- Pause / step ---
//
// These are unconditional reads/writes — GameEngine consults them every
// tick regardless of whether the inspector window is visible, so a user
// can pause from a hidden inspector and still freeze the world. The
// step counter decrements on each consume so multi-frame steps work
// transparently (RequestStep(5) advances 5 logic ticks then re-pauses).

bool IsPaused()
{
    return s_paused;
}

void SetPaused(bool paused)
{
    s_paused = paused;
    if (!paused)
        s_pendingStepFrames = 0; // resuming play discards any pending step
}

void RequestStep(int frames)
{
    if (frames > 0)
        s_pendingStepFrames += frames;
}

bool TryConsumeStep()
{
    if (s_pendingStepFrames > 0)
    {
        --s_pendingStepFrames;
        return true;
    }
    return false;
}

// ============================================================================
// Free-fly editor camera
// ============================================================================
namespace Camera
{
namespace
{
    bool   s_active     = false;
    bool   s_seeded     = false;
    float  s_posX       = 0.0f;
    float  s_posY       = 0.0f;
    float  s_posZ       = 200.0f;
    float  s_yaw        = 0.0f;    // radians, atan2(forward.y, forward.x)
    float  s_pitch      = -0.4f;   // radians, look slightly down
    float  s_moveSpeed  = 250.0f;  // world units per second baseline
    float  s_mouseSens  = 0.004f;  // radians per pixel of mouse delta
}

bool   IsActive()           { return s_active; }
float& MoveSpeed()          { return s_moveSpeed; }
float& MouseSensitivity()   { return s_mouseSens; }

void SetActive(bool on)
{
    if (on && !s_active)
    {
        // Re-seed from the current engine camera on each fresh
        // activation so the user picks up where the game was looking.
        s_seeded = false;
    }
    s_active = on;
}

void Toggle() { SetActive(!s_active); }

void Update(float dt)
{
    if (!s_active)
        return;

    // First-frame seed from the live engine camera
    if (!s_seeded)
    {
        float ex, ey, ez, eyaw, epitch;
        if (InspectorReadGameCamera(&ex, &ey, &ez, &eyaw, &epitch))
        {
            s_posX  = ex;
            s_posY  = ey;
            s_posZ  = ez;
            s_yaw   = eyaw;
            s_pitch = epitch;
        }
        s_seeded = true;
    }

    // ---- Keyboard input via SDL polling -----------------------------
    // Bypass the event stream entirely so we don't have to fight
    // ImGui consumption or worry about the engine misinterpreting
    // movement keys as game hotkeys. ProcessEvent still swallows
    // these same keys to keep them out of the engine's command path
    // while free-fly is active.
    int numKeys = 0;
    const bool* keys = SDL_GetKeyboardState(&numKeys);
    if (!keys) return;

    const bool sprint =
        keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    const float baseSpeed = s_moveSpeed * (sprint ? 4.0f : 1.0f);
    const float move = baseSpeed * dt;

    // Forward / right vectors derived from yaw + pitch.
    // Engine convention: Z is up, yaw=0 looks +X.
    const float cp = std::cos(s_pitch);
    const float fx = std::cos(s_yaw) * cp;
    const float fy = std::sin(s_yaw) * cp;
    const float fz = std::sin(s_pitch);
    // Right is yaw-only (so strafing stays horizontal regardless of pitch)
    const float rx = std::sin(s_yaw);
    const float ry = -std::cos(s_yaw);

    if (keys[SDL_SCANCODE_W]) { s_posX += fx * move; s_posY += fy * move; s_posZ += fz * move; }
    if (keys[SDL_SCANCODE_S]) { s_posX -= fx * move; s_posY -= fy * move; s_posZ -= fz * move; }
    if (keys[SDL_SCANCODE_A]) { s_posX -= rx * move; s_posY -= ry * move; }
    if (keys[SDL_SCANCODE_D]) { s_posX += rx * move; s_posY += ry * move; }
    if (keys[SDL_SCANCODE_SPACE])  { s_posZ += move; }
    if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) { s_posZ -= move; }

    // Q / E — yaw rotation at a fixed angular rate (radians/sec) so it's
    // predictable for the "does the shadow stay put when I rotate?" test.
    const float yawRate = 1.5f; // ~86 deg/sec
    if (keys[SDL_SCANCODE_Q]) s_yaw += yawRate * dt;
    if (keys[SDL_SCANCODE_E]) s_yaw -= yawRate * dt;

    // ---- Mouse look while RMB held ----------------------------------
    // Per-frame mouse delta from ImGui (which the SDL3 backend feeds
    // from the same SDL_EVENT_MOUSE_MOTION events). We don't gate on
    // hover — once the user starts dragging RMB the look continues
    // until release, matching Unreal's editor behavior.
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        s_yaw   -= d.x * s_mouseSens;
        s_pitch -= d.y * s_mouseSens;
        // Clamp pitch just shy of straight up/down so we never reach
        // the singularity (forward becoming parallel to world up).
        const float maxPitch = 1.55f; // ~88.8 degrees
        if (s_pitch >  maxPitch) s_pitch =  maxPitch;
        if (s_pitch < -maxPitch) s_pitch = -maxPitch;
    }
}

void ApplyToEngineCamera()
{
    if (!s_active) return;
    InspectorApplyCameraOverride(s_posX, s_posY, s_posZ, s_yaw, s_pitch);
}

} // namespace Camera

void BeginFrame()
{
    if (!s_initialized)
        return;

    // Cursor management while the inspector is open:
    //
    //   * Always force OUT of relative-mouse-mode (RTS camera grab,
    //     cutscene cursor warping) so ImGui can track the cursor.
    //
    //   * OS cursor visibility depends on where the cursor is:
    //       - Over the Game viewport: HIDE the OS cursor so the
    //         engine's own 2D sprite cursor (drawn into the off-
    //         screen RT) is visible without an overlapping Windows
    //         arrow. This restores the in-game custom cursors.
    //       - Anywhere else (panels, dock chrome, empty area): SHOW
    //         the OS cursor so the user can interact with ImGui.
    //
    //   * The check runs every frame, so moving the mouse between
    //     the game viewport and a panel swaps cursor visibility
    //     instantly with no perceptible lag.
    if (s_enabled && s_window)
    {
        SDL_SetWindowRelativeMouseMode(s_window, false);

        float mx = 0.0f, my = 0.0f;
        SDL_GetMouseState(&mx, &my);
        const bool overGame =
            Panels::IsPointInGameViewport((int)mx, (int)my);
        if (overGame)
            SDL_HideCursor();
        else
            SDL_ShowCursor();
    }

    // ImGui's per-frame begin must run regardless of s_enabled, so the
    // event-capture flags (WantCaptureMouse/Keyboard) stay accurate and
    // so the F10 toggle survives even when the inspector is hidden.
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Free-fly editor camera per-frame update. Runs regardless of
    // s_enabled so the camera can stay active even when the user
    // hides the panels with F10. Uses ImGui's frame delta which is
    // valid right after NewFrame.
    Camera::Update(ImGui::GetIO().DeltaTime);

    if (!s_enabled)
        return;

    // Top toolbar: play/pause/step + status + view menu. Anchored to
    // the top-left so it doesn't move around as panels open and close.
    if (s_showToolbar)
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        // Anchor the floating toolbar at the bottom-right corner of
        // the viewport. The (1,1) pivot means the window's own
        // bottom-right corner lands on the given point — no need to
        // know the toolbar's auto-resized dimensions in advance.
        // Uses Always when a layout reset is pending (so Reset Layout
        // snaps the toolbar back) and FirstUseEver otherwise (so the
        // user's drag-positioned toolbar sticks across frames).
        const ImGuiCond toolbarPosCond = Panels::IsLayoutResetPending()
            ? ImGuiCond_Always
            : ImGuiCond_FirstUseEver;
        ImGui::SetNextWindowPos(
            ImVec2(vp->WorkPos.x + vp->WorkSize.x - 16,
                   vp->WorkPos.y + vp->WorkSize.y - 16),
            toolbarPosCond,
            ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.85f);

        ImGuiWindowFlags toolbarFlags =
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_MenuBar |
            ImGuiWindowFlags_NoDocking;  // floats above the dockspace

        if (ImGui::Begin("Inspector", &s_showToolbar, toolbarFlags))
        {
            // ---- Menu bar with View + Debug Draw toggles ----
            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("View"))
                {
                    Panels::Visibility& v = Panels::GetVisibility();
                    ImGui::MenuItem("Game",       nullptr, &v.game);
                    ImGui::Separator();
                    ImGui::MenuItem("Log",        nullptr, &v.log);
                    ImGui::MenuItem("Hierarchy",  nullptr, &v.hierarchy);
                    ImGui::MenuItem("Properties", nullptr, &v.properties);
                    ImGui::MenuItem("Model",      nullptr, &v.model);
                    ImGui::MenuItem("Players",    nullptr, &v.players);
                    ImGui::MenuItem("Mini-map",   nullptr, &v.minimap);
                    ImGui::MenuItem("Perf HUD",   nullptr, &v.perfHud);
                    ImGui::MenuItem("Render Toggles", nullptr, &v.renderToggles);
                    ImGui::MenuItem("Mission Script", nullptr, &v.script);
                    ImGui::MenuItem("Destruction Timeline", nullptr, &v.destruction);
                    ImGui::MenuItem("Lights",       nullptr, &v.lights);
                    ImGui::MenuItem("Shadows",      nullptr, &v.shadows);
                    ImGui::MenuItem("Launch Parameters", nullptr, &v.launchParams);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Reset Layout"))
                        Panels::RequestLayoutReset();
                    ImGui::Separator();
                    ImGui::MenuItem("ImGui Demo", nullptr, &s_showDemo);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Debug Draw"))
                {
                    Panels::DebugOverlayFlags& d = Panels::GetDebugFlags();
                    ImGui::MenuItem("World Origin Axis", nullptr, &d.showWorldOrigin);
                    ImGui::Separator();
                    ImGui::MenuItem("Show All Bounds",   nullptr, &d.showAllBounds);
                    ImGui::MenuItem("Show All Headings", nullptr, &d.showAllHeadings);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Modern HUD"))
                {
                    Hud::Visibility& h = Hud::GetVisibility();
                    ImGui::TextDisabled("In-game UI replacements");
                    ImGui::Separator();
                    ImGui::MenuItem("Resources",     nullptr, &h.resources);
                    ImGui::MenuItem("Radar",         nullptr, &h.radar);
                    ImGui::MenuItem("Selection",     nullptr, &h.selection);
                    // No ImGui Commands panel — the original ControlBar
                    // at the bottom of the screen is the only command UI,
                    // even when the inspector overlay is active.
                    ImGui::MenuItem("Build Queue",   nullptr, &h.buildQueue);
                    ImGui::MenuItem("General Powers", nullptr, &h.generals);
                    ImGui::Separator();
                    ImGui::MenuItem("Team Colors",   nullptr, &h.teamColors);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Show All"))
                    {
                        h.resources = h.radar = h.selection =
                            h.buildQueue = h.generals =
                            h.teamColors = true;
                    }
                    if (ImGui::MenuItem("Hide All"))
                    {
                        h.resources = h.radar = h.selection =
                            h.buildQueue = h.generals =
                            h.teamColors = false;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("AI Debug"))
                {
                    Ai::Visibility& a = Ai::GetVisibility();
                    ImGui::TextDisabled("AI / pathfinding visualizers");
                    ImGui::Separator();
                    ImGui::MenuItem("State Machine",      nullptr, &a.stateMachine);
                    ImGui::MenuItem("Pathfinder",         nullptr, &a.pathfinder);
                    ImGui::MenuItem("Entity Gizmos",      nullptr, &a.entityGizmos);
                    ImGui::Separator();
                    ImGui::MenuItem("Kanban: Activity",   nullptr, &a.kanbanActivity);
                    ImGui::MenuItem("Kanban: Production", nullptr, &a.kanbanProd);
                    ImGui::Separator();
                    ImGui::MenuItem("AI Build Lists",     nullptr, &a.buildLists);
                    ImGui::Separator();
                    ImGui::MenuItem("Model Debugger",     nullptr, &a.modelDebugger);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Show All##ai"))
                    {
                        a.stateMachine = a.pathfinder = a.kanbanActivity =
                            a.kanbanProd = a.buildLists = a.modelDebugger =
                            a.entityGizmos = true;
                    }
                    if (ImGui::MenuItem("Hide All##ai"))
                    {
                        a.stateMachine = a.pathfinder = a.kanbanActivity =
                            a.kanbanProd = a.buildLists = a.modelDebugger =
                            a.entityGizmos = false;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            // ---- Play / Pause / Step ----
            if (s_paused)
            {
                if (ImGui::Button("Play", ImVec2(80, 0)))
                    SetPaused(false);
            }
            else
            {
                if (ImGui::Button("Pause", ImVec2(80, 0)))
                    SetPaused(true);
            }

            ImGui::SameLine();
            // Step is only meaningful while paused — disable it
            // otherwise so users don't queue ghost frames during play.
            ImGui::BeginDisabled(!s_paused);
            if (ImGui::Button("Step", ImVec2(80, 0)))
                RequestStep(1);
            ImGui::SameLine();
            if (ImGui::Button("+10", ImVec2(60, 0)))
                RequestStep(10);
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            // ---- Free-fly editor camera toggle ----
            // Unreal-style WASD + QE + RMB-look override of the engine's
            // tactical camera. Tinted purple when active so it's
            // unmistakable. Hint shown in the tooltip.
            const bool freeCamOn = Camera::IsActive();
            if (freeCamOn)
            {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.20f, 0.85f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.30f, 0.95f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.50f, 0.15f, 0.80f, 1.0f));
            }
            if (ImGui::Button(freeCamOn ? "Free Cam: ON" : "Free Cam: OFF", ImVec2(140, 0)))
                Camera::Toggle();
            if (freeCamOn)
                ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Unreal-style free-fly editor camera");
                ImGui::Separator();
                ImGui::TextDisabled("W / A / S / D    move");
                ImGui::TextDisabled("Q / E            down / up");
                ImGui::TextDisabled("Shift            sprint (4x)");
                ImGui::TextDisabled("RMB drag         look around");
                ImGui::Separator();
                ImGui::SetNextItemWidth(120);
                ImGui::SliderFloat("speed##cam", &Camera::MoveSpeed(), 25.0f, 2000.0f, "%.0f");
                ImGui::SetNextItemWidth(120);
                ImGui::SliderFloat("sens##cam", &Camera::MouseSensitivity(), 0.0005f, 0.02f, "%.4f");
                ImGui::EndTooltip();
            }

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            // ---- Pick Mode toggle ----
            // When ON, left-clicks in the viewport select objects for
            // the inspector instead of triggering RTS commands. Tinted
            // when active so the user always knows whether the mouse
            // belongs to the inspector or the game.
            const bool pickOn = Selection::IsPickMode();
            if (pickOn)
            {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.55f, 0.85f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.65f, 0.95f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.50f, 0.80f, 1.0f));
            }
            if (ImGui::Button(pickOn ? "Pick: ON" : "Pick: OFF", ImVec2(110, 0)))
                Selection::TogglePickMode();
            if (pickOn)
                ImGui::PopStyleColor(3);

            ImGui::SameLine();

            // ---- Tooltip Mode toggle ----
            // When ON, hovering any object in the viewport pops up a
            // floating tooltip with the full Properties panel content.
            // Independent from pick mode — clicks pass through to the
            // engine, so it's a "look but don't touch" inspection
            // mode for cutscenes and scripted sequences.
            const bool tipOn = Selection::IsTooltipMode();
            if (tipOn)
            {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.85f, 0.55f, 0.20f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.65f, 0.30f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.80f, 0.50f, 0.15f, 1.0f));
            }
            if (ImGui::Button(tipOn ? "Tooltip: ON" : "Tooltip: OFF", ImVec2(130, 0)))
                Selection::ToggleTooltipMode();
            if (tipOn)
                ImGui::PopStyleColor(3);

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            const ImGuiIO& io = ImGui::GetIO();
            ImGui::Text("%.1f FPS  (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);

            if (s_paused)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "[PAUSED]");
            }

            // ---- Tag set indicator + clear ----
            // Shows when the user has built a multi-tag set via
            // Ctrl-click. The clear button lets them dump it without
            // having to find every tagged object on the map.
            const size_t tagCount = Selection::TagCount();
            if (tagCount > 0)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f),
                    "Tags: %zu", tagCount);
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear##tags"))
                    Selection::TagClear();
            }
        }
        ImGui::End();
    }

    // Render all enabled panels (log, hierarchy, properties, players).
    Panels::DrawAll();

    if (s_showDemo)
        ImGui::ShowDemoWindow(&s_showDemo);
}

void SetFrameTooltip(const char* utf8Text, int anchorX, int anchorY)
{
    if (!utf8Text || utf8Text[0] == '\0')
    {
        s_tooltipPending = false;
        s_tooltipText.clear();
        return;
    }
    s_tooltipText.assign(utf8Text);
    s_tooltipX = anchorX;
    s_tooltipY = anchorY;
    s_tooltipPending = true;
}

void Render()
{
    if (!s_initialized)
        return;

    // Game tooltip overlay: render regardless of s_enabled so the
    // in-game tooltip behaviour doesn't depend on the inspector being
    // visible. Offset the anchor so the tooltip sits to the lower-right
    // of the cursor (matching the original Mouse::drawTooltip layout),
    // and clamp against the display so it never spills off-screen.
    //
    // Poll the engine's Mouse state directly each frame. This bypasses
    // the legacy drawTooltip push-path (which only ran while the 2D mouse
    // was being drawn and silently lost updates across redraw modes) and
    // gives the ImGui overlay a single authoritative source: whatever the
    // engine thinks the cursor tooltip is right now.
    {
        char pollBuf[1024] = {0};
        int  pollX = 0, pollY = 0;
        if (InspectorQueryGameTooltip(pollBuf, (int)sizeof(pollBuf), &pollX, &pollY))
        {
            s_tooltipText.assign(pollBuf);
            s_tooltipX = pollX;
            s_tooltipY = pollY;
            s_tooltipPending = true;
        }
        else
        {
            s_tooltipPending = false;
            s_tooltipText.clear();
        }
    }

    const bool hadTooltip = s_tooltipPending && !s_tooltipText.empty();
    if (hadTooltip)
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImVec2 pos(
            static_cast<float>(s_tooltipX) + 20.0f,
            static_cast<float>(s_tooltipY));

        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_AlwaysAutoResize;

        if (ImGui::Begin("##GameTooltip", nullptr, flags))
        {
            // Use the platform viewport width as the wrap budget. A long
            // tooltip that would otherwise disappear off the right edge
            // wraps onto another line instead of being clipped.
            const float wrap = vp->Size.x * 0.35f;
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
            RenderTooltipTextWithHotkeys(s_tooltipText.c_str());
            ImGui::PopTextWrapPos();

            // After the window measures itself, nudge it back on-screen
            // if the auto-resize pushed it past the right/bottom edges.
            const ImVec2 winSize = ImGui::GetWindowSize();
            const ImVec2 winPos  = ImGui::GetWindowPos();
            float clampedX = winPos.x;
            float clampedY = winPos.y;
            if (winPos.x + winSize.x + 4.0f > vp->Size.x)
                clampedX = static_cast<float>(s_tooltipX) - winSize.x - 4.0f;
            if (winPos.y + winSize.y + 4.0f > vp->Size.y)
                clampedY = static_cast<float>(s_tooltipY) - winSize.y;
            if (clampedX != winPos.x || clampedY != winPos.y)
                ImGui::SetWindowPos(ImVec2(clampedX, clampedY));
        }
        ImGui::End();

        s_tooltipPending = false;
    }

    // Always end the frame and submit draw data — even when the
    // inspector is hidden — because BeginFrame already called NewFrame
    // and ImGui requires a balanced Render() call.
    ImGui::Render();

    if (!s_enabled && !hadTooltip)
    {
        // No visible windows means no draw calls, but ImGui still
        // expects EndFrame bookkeeping via Render(). Skip the actual
        // RenderDrawData call to save a few microseconds.
        return;
    }

    // CRITICAL: bind the REAL swap-chain backbuffer (not the
    // redirect target). When the game-viewport redirect is active
    // the engine has been rendering into an off-screen texture, but
    // ImGui must draw on the actual visible backbuffer or the user
    // sees nothing. SetBackBufferDirect bypasses the redirect.
    auto& device = Render::Renderer::Instance().GetDevice();
    device.SetBackBufferDirect();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

} // namespace Inspector

#else // !BUILD_WITH_D3D11 — Vulkan or other backends: stub everything.

namespace Inspector
{
void Shutdown() {}
bool IsInitialized() { return false; }
bool IsEnabled() { return false; }
void SetEnabled(bool) {}
bool ProcessEvent(const SDL_Event*) { return false; }
bool WantCaptureMouse() { return false; }
bool WantCaptureKeyboard() { return false; }
bool IsPaused() { return false; }
void SetPaused(bool) {}
void RequestStep(int) {}
bool TryConsumeStep() { return false; }
void BeginFrame() {}
void Render() {}
void SetFrameTooltip(const char*, int, int) {}
// Log() is provided by Panels.cpp in D3D11 configurations. In VK configs
// Panels.cpp is excluded from the build (it unconditionally #includes
// imgui.h which isn't on the include path when Inspector is stubbed),
// so we need a stub here or the linker drops Inspector::Log symbols.
void Log(const char*, ...) {}

namespace Camera {
    static float s_dummySpeed = 250.0f;
    static float s_dummySens  = 0.004f;
    bool   IsActive()         { return false; }
    void   SetActive(bool)    {}
    void   Toggle()           {}
    void   Update(float)      {}
    void   ApplyToEngineCamera() {}
    float& MoveSpeed()        { return s_dummySpeed; }
    float& MouseSensitivity() { return s_dummySens; }
}
// DestructionTimeline.cpp is excluded from VK builds (it #includes imgui.h
// at the top and its panel draw is all ImGui). Engine code in Object::onDie
// still calls the recorder API, so provide no-op stubs here.
namespace Destruction {
    void RecordDeath(int, const char*, unsigned int, const char*, unsigned int, bool, int) {}
    void Clear() {}
    int  Count() { return 0; }
}
}

#endif // BUILD_WITH_D3D11
