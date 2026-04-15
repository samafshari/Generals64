#ifdef USE_SDL

#include "SDLPlatform.h"
#include "Inspector/Inspector.h"
#include <SDL3/SDL.h>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Platform
{

SDLPlatform& SDLPlatform::Instance()
{
    static SDLPlatform s_instance;
    return s_instance;
}

bool SDLPlatform::Init(int width, int height, bool windowed, const char* title, uint32_t backendFlags, bool startMaximized)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    // Default behavior: maximize on startup. Borderless+maximized acts as
    // fullscreen-borderless-windowed; bordered+maximized is the conventional
    // windowed-maximized look. We deliberately do NOT use SDL_WINDOW_FULLSCREEN
    // (exclusive fullscreen) — borderless maximized is what users actually
    // want for alt-tab friendliness.
    //
    // When startMaximized is false (e.g. caller passed -xres on the command
    // line) we instead open the window at exactly width x height in normal
    // (non-maximized) state so the requested resolution actually applies.
    uint32_t windowFlags = SDL_WINDOW_RESIZABLE | backendFlags;
    if (startMaximized)
        windowFlags |= SDL_WINDOW_MAXIMIZED;
    if (!windowed)
        windowFlags |= SDL_WINDOW_BORDERLESS;

    m_window = SDL_CreateWindow(title, width, height, windowFlags);
    if (!m_window)
    {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    if (startMaximized)
    {
        // Belt-and-braces: some platforms ignore SDL_WINDOW_MAXIMIZED at
        // create time (Wayland in particular), so request maximize
        // explicitly after creation.
        SDL_MaximizeWindow(m_window);
    }

    // Enable SDL3 text input so we receive SDL_EVENT_TEXT_INPUT events with
    // already-composed characters (the equivalent of Win32 WM_CHAR). Without
    // this call SDL3 only delivers SDL_EVENT_KEY_DOWN scancodes and game text
    // widgets never receive printable characters because the engine's text
    // entry path expects pre-composed characters via GWM_IME_CHAR — see
    // SDLEventDispatcher in SDLGameEngine.cpp for the routing.
    SDL_StartTextInput(m_window);

    // Cache the actual maximized client size so the renderer sees correct
    // dimensions even though we passed the unmaximized hint to CreateWindow.
    int actualW = width, actualH = height;
    SDL_GetWindowSize(m_window, &actualW, &actualH);

    m_width = actualW;
    m_height = actualH;
    m_windowed = windowed;
    m_shouldQuit = false;
    m_hasFocus = true;

    return true;
}

void SDLPlatform::ToggleBorderless()
{
    if (!m_window)
        return;

    // Use the tracked m_windowed state rather than re-reading
    // SDL_GetWindowFlags — SDL_WINDOW_BORDERLESS doesn't always update
    // synchronously after SDL_SetWindowBordered, so a fast double-toggle
    // could read stale flags and not actually flip.
    const bool wantBordered = !m_windowed;  // true => bordered+maximized

    SDL_DisplayID displayId = SDL_GetDisplayForWindow(m_window);
    if (displayId == 0)
        displayId = SDL_GetPrimaryDisplay();

    SDL_Rect fullBounds = {0, 0, 0, 0};
    SDL_Rect usableBounds = {0, 0, 0, 0};
    SDL_GetDisplayBounds(displayId, &fullBounds);
    SDL_GetDisplayUsableBounds(displayId, &usableBounds);

    fprintf(stderr, "[SDL] ToggleBorderless: wantBordered=%d display=%d "
                    "full=%dx%d@%d,%d usable=%dx%d@%d,%d\n",
            wantBordered ? 1 : 0, (int)displayId,
            fullBounds.w, fullBounds.h, fullBounds.x, fullBounds.y,
            usableBounds.w, usableBounds.h, usableBounds.x, usableBounds.y);
    fflush(stderr);

    // Always exit any maximized state first — toggling border on a
    // maximized window leaves the window in a wedged state on Windows.
    SDL_RestoreWindow(m_window);

    // Flip the border. In SDL3, true=bordered, false=borderless.
    SDL_SetWindowBordered(m_window, wantBordered);

    if (wantBordered)
    {
        // Bordered windowed-maximized: cover the work area (screen minus
        // taskbar) and let SDL_MaximizeWindow set the proper Win32 state
        // so the title bar's maximize/restore button shows the right icon.
        if (usableBounds.w > 0 && usableBounds.h > 0)
        {
            SDL_SetWindowPosition(m_window, usableBounds.x, usableBounds.y);
            SDL_SetWindowSize(m_window, usableBounds.w, usableBounds.h);
        }
        SDL_MaximizeWindow(m_window);
    }
    else
    {
        // Borderless covers the FULL display, including the taskbar area.
        // We can't use SDL_MaximizeWindow here — Windows clips maximized
        // popup windows to the work area, leaving a gap at the bottom.
        if (fullBounds.w > 0 && fullBounds.h > 0)
        {
            SDL_SetWindowPosition(m_window, fullBounds.x, fullBounds.y);
            SDL_SetWindowSize(m_window, fullBounds.w, fullBounds.h);
        }
    }

    m_windowed = wantBordered;

    // Push the new size into the renderer immediately. SDL will also fire
    // SDL_EVENT_WINDOW_RESIZED in the next pump, but the renderer needs the
    // size NOW or the next Present uses the wrong swap chain dimensions.
    int w = 0, h = 0;
    SDL_GetWindowSize(m_window, &w, &h);
    if (w > 0 && h > 0)
    {
        m_width = w;
        m_height = h;
        if (m_callbacks.onResize)
            m_callbacks.onResize(m_width, m_height);
    }

    fprintf(stderr, "[SDL] ToggleBorderless: now %dx%d, windowed=%d\n",
            m_width, m_height, m_windowed ? 1 : 0);
    fflush(stderr);
}

void SDLPlatform::Shutdown()
{
    if (m_window)
    {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
}

void* SDLPlatform::GetNativeWindowHandle() const
{
    if (!m_window)
        return nullptr;

#ifdef _WIN32
    // SDL3: get HWND via properties
    return (void*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(m_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
    return (void*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(m_window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#else
    return nullptr;
#endif
}

void SDLPlatform::PumpEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        // Inspector gets first crack at every event so it can intercept
        // F10 (toggle) and consume mouse/keyboard input destined for an
        // open panel. When ProcessEvent returns true the inspector has
        // claimed this event and the engine input layer must not see it,
        // otherwise clicks on a panel would also click into the game.
        const bool inspectorConsumed = Inspector::ProcessEvent(&event);

        // Forward to registered input callback unless the inspector
        // consumed it. Window/quit events still propagate so the engine
        // can react to resize and shutdown regardless of the overlay.
        if (m_eventCallback && !inspectorConsumed)
            m_eventCallback(event, m_eventUserData);

        switch (event.type)
        {
        case SDL_EVENT_QUIT:
            m_shouldQuit = true;
            if (m_callbacks.onQuitRequested)
                m_callbacks.onQuitRequested();
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            m_width = event.window.data1;
            m_height = event.window.data2;
            if (m_callbacks.onResize)
                m_callbacks.onResize(m_width, m_height);
            break;

        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            m_hasFocus = true;
            if (m_callbacks.onFocusGained)
                m_callbacks.onFocusGained();
            break;

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            m_hasFocus = false;
            if (m_callbacks.onFocusLost)
                m_callbacks.onFocusLost();
            break;

        case SDL_EVENT_WINDOW_MINIMIZED:
            if (m_callbacks.onDeactivated)
                m_callbacks.onDeactivated();
            break;

        case SDL_EVENT_WINDOW_RESTORED:
            if (m_callbacks.onActivated)
                m_callbacks.onActivated();
            break;
        }
    }
}

void SDLPlatform::SetCursorVisible(bool visible)
{
    if (visible)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
}

void SDLPlatform::SetCursorCapture(bool captured)
{
    // Use SDL_SetWindowMouseGrab — it constrains the cursor to the window
    // without hiding it, matching Win32's ClipCursor() that the original
    // Win32Mouse::capture() used. SDL_SetWindowRelativeMouseMode is the
    // FPS-mouselook equivalent: it HIDES the cursor and switches the window
    // to relative-delta motion. The engine's cursor-capture toggle (used
    // throughout gameplay) just wants to keep the OS pointer inside the
    // game window, so relative mode was wrong and made the in-game cursor
    // disappear whenever a multiplayer match was started — once relative
    // mode is on, SDL_ShowCursor() can't bring the cursor back, only an
    // explicit SDL_SetWindowRelativeMouseMode(false) does. The Inspector
    // happened to call that every frame while open, which is why the
    // cursor only went missing when F10 was off.
    SDL_SetWindowMouseGrab(m_window, captured);
}

void SDLPlatform::WarpCursorTo(int x, int y)
{
    SDL_WarpMouseInWindow(m_window, (float)x, (float)y);
}

} // namespace Platform

#endif // USE_SDL
