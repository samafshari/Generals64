#pragma once

#ifdef USE_SDL

#include <cstdint>

struct SDL_Window;
union SDL_Event;

namespace Platform
{

// Callback for window events (resize, focus, close, etc.)
struct PlatformCallbacks
{
    void (*onResize)(int width, int height) = nullptr;
    void (*onFocusGained)() = nullptr;
    void (*onFocusLost)() = nullptr;
    void (*onQuitRequested)() = nullptr;
    void (*onActivated)() = nullptr;
    void (*onDeactivated)() = nullptr;
};

class SDLPlatform
{
public:
    static SDLPlatform& Instance();

    // Initialize SDL and create a window.
    // backendFlags: SDL_WINDOW_VULKAN or 0 (for D3D11 which uses the native HWND).
    // windowed=true  -> bordered (windowed mode); maximized iff startMaximized
    // windowed=false -> borderless (fullscreen / borderless windowed); maximized iff startMaximized
    // startMaximized=false opens the window at exactly width x height instead of
    // covering the work area / full screen. Used when the caller has an
    // explicit resolution (e.g. -xres on the command line).
    bool Init(int width, int height, bool windowed, const char* title, uint32_t backendFlags = 0, bool startMaximized = true);
    void Shutdown();

    // Toggle between bordered (windowed) and borderless (fullscreen) modes,
    // keeping the window maximized in both. Driven by ALT+ENTER.
    void ToggleBorderless();

    // Poll and dispatch all pending SDL events. Call once per frame.
    void PumpEvents();

    // Set callbacks for window events.
    void SetCallbacks(const PlatformCallbacks& callbacks) { m_callbacks = callbacks; }

    // --- Window accessors ---

    SDL_Window* GetWindow() const { return m_window; }

    // Returns the native window handle: HWND on Windows, NSWindow* on macOS.
    void* GetNativeWindowHandle() const;

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    bool IsWindowed() const { return m_windowed; }
    bool ShouldQuit() const { return m_shouldQuit; }
    bool HasFocus() const { return m_hasFocus; }

    // --- Cursor control ---

    void SetCursorVisible(bool visible);
    void SetCursorCapture(bool captured);
    void WarpCursorTo(int x, int y);

    // --- Mouse/Keyboard event access ---
    // Raw SDL events are dispatched via callbacks registered by the input layer.

    using EventCallback = void(*)(const SDL_Event& event, void* userData);
    void SetEventCallback(EventCallback cb, void* userData) { m_eventCallback = cb; m_eventUserData = userData; }

private:
    SDLPlatform() = default;

    SDL_Window* m_window = nullptr;
    PlatformCallbacks m_callbacks;
    EventCallback m_eventCallback = nullptr;
    void* m_eventUserData = nullptr;

    int m_width = 0;
    int m_height = 0;
    bool m_windowed = true;
    bool m_shouldQuit = false;
    bool m_hasFocus = true;
};

} // namespace Platform

#endif // USE_SDL
