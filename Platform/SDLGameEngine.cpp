#ifdef USE_SDL

#include "SDLGameEngine.h"
#include "SDLPlatform.h"
#include "SDLMouse.h"
#include "SDLKeyboard.h"
#include "GameClient/Display.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/IMEManager.h"
#include "GameClient/InGameUI.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Mouse.h"
#include "GameClient/Shell.h"
#include "GameLogic/GameLogic.h"
#include "Common/GlobalData.h"
#include <SDL3/SDL.h>
#include <cstdio>

// Global SDL input devices for event routing from the SDL event pump.
// Set during init after the game client creates them via the factory.
static SDLMouse* g_sdlMouse = nullptr;
static SDLKeyboard* g_sdlKeyboard = nullptr;

// Event callback from SDLPlatform -> route to SDL input devices
static void SDLEventDispatcher(const SDL_Event& event, void* /*userData*/)
{
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
        if (g_sdlMouse)
            g_sdlMouse->HandleSDLEvent(event);
        break;

    case SDL_EVENT_KEY_DOWN:
    {
        // ALT+ENTER toggles bordered <-> borderless. We swallow it here so
        // the keystroke does not also reach the game's keyboard buffer
        // (where ENTER would otherwise dismiss menus / send chat).
        // Use SDL_GetModState() for the modifier — event.key.mod has been
        // observed to be stale on the down-event for some keyboard layouts.
        // Accept either the main Return key or the keypad Enter.
        const bool isEnter = (event.key.scancode == SDL_SCANCODE_RETURN ||
                              event.key.scancode == SDL_SCANCODE_KP_ENTER);
        const bool altHeld = (SDL_GetModState() & SDL_KMOD_ALT) != 0
                          || (event.key.mod & SDL_KMOD_ALT) != 0;
        if (isEnter && altHeld)
        {
            if (!event.key.repeat)
            {
                fprintf(stderr, "[SDL] ALT+ENTER detected -> ToggleBorderless\n");
                fflush(stderr);
                Platform::SDLPlatform::Instance().ToggleBorderless();
            }
            return;
        }
        if (g_sdlKeyboard)
            g_sdlKeyboard->HandleSDLEvent(event);

        // SDL3's SDL_EVENT_TEXT_INPUT only fires for printable characters —
        // it does NOT fire for Enter/Escape/Tab/etc., which Win32 WM_CHAR
        // delivered as control codes (0x0D, 0x1B, 0x09, ...). Game text
        // widgets expect those codes via GWM_IME_CHAR (e.g. GadgetTextEntry
        // submits chat / closes the edit when it sees VK_RETURN). Synthesize
        // them here so Enter actually sends the chat line. Only fire when an
        // IME-attached text window exists, so menu Enter (which goes through
        // the scancode/GWM_CHAR path) is not double-dispatched.
        if (!event.key.repeat && TheIMEManager && TheWindowManager)
        {
            GameWindow* target = TheIMEManager->getWindow();
            if (target)
            {
                unsigned int cp = 0;
                switch (event.key.scancode)
                {
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_KP_ENTER:
                    cp = 0x0D;  // VK_RETURN
                    break;
                default:
                    break;
                }
                if (cp != 0)
                    TheWindowManager->winSendInputMsg(target, GWM_IME_CHAR,
                                                      (WindowMsgData)cp, 0);
            }
        }
        break;
    }

    case SDL_EVENT_KEY_UP:
    {
        // Match the swallow above so the up-event doesn't desync key state.
        const bool isEnter = (event.key.scancode == SDL_SCANCODE_RETURN ||
                              event.key.scancode == SDL_SCANCODE_KP_ENTER);
        const bool altHeld = (SDL_GetModState() & SDL_KMOD_ALT) != 0
                          || (event.key.mod & SDL_KMOD_ALT) != 0;
        if (isEnter && altHeld)
            return;
        if (g_sdlKeyboard)
            g_sdlKeyboard->HandleSDLEvent(event);
        break;
    }

    case SDL_EVENT_TEXT_INPUT:
    {
        // SDL3's equivalent of Win32 WM_CHAR: an already-composed UTF-8 string
        // representing what the user just typed (with keyboard layout, shift,
        // dead-keys, IME composition all applied). The engine's text widgets
        // (GadgetTextEntry) consume printable characters via GWM_IME_CHAR
        // dispatched to the focused window — historically that path was fed by
        // IMEManager::Service translating WM_CHAR. With SDL we forward the
        // text-input string directly to the same focused window.
        if (!event.text.text || !TheIMEManager || !TheWindowManager)
            break;

        GameWindow* target = TheIMEManager->getWindow();
        if (!target)
            break;

        // event.text.text is UTF-8. Decode each codepoint and dispatch one
        // GWM_IME_CHAR per character. Anything below SP that isn't VK_RETURN
        // is dropped — matching the IMEManager::Service WM_CHAR filter.
        const unsigned char* p = (const unsigned char*)event.text.text;
        while (*p)
        {
            unsigned int cp = 0;
            int extra = 0;
            if (*p < 0x80) {
                cp = *p++;
            } else if ((*p & 0xE0) == 0xC0) {
                cp = *p++ & 0x1F; extra = 1;
            } else if ((*p & 0xF0) == 0xE0) {
                cp = *p++ & 0x0F; extra = 2;
            } else if ((*p & 0xF8) == 0xF0) {
                cp = *p++ & 0x07; extra = 3;
            } else {
                ++p; // invalid UTF-8 lead — skip
                continue;
            }
            while (extra-- > 0 && (*p & 0xC0) == 0x80)
                cp = (cp << 6) | (*p++ & 0x3F);

            // Game text widgets are UCS-2; supplementary plane is unsupported
            // by GadgetTextEntry's WideChar pipeline. Drop anything above U+FFFF.
            if (cp == 0 || cp > 0xFFFF)
                continue;
            // Filter to match WM_CHAR path: printable or VK_RETURN (0x0D).
            if (cp < 0x20 && cp != 0x0D)
                continue;

            TheWindowManager->winSendInputMsg(target, GWM_IME_CHAR, (WindowMsgData)cp, 0);
        }
        break;
    }
    }
}

SDLGameEngine::SDLGameEngine()
{
}

SDLGameEngine::~SDLGameEngine()
{
    g_sdlMouse = nullptr;
    g_sdlKeyboard = nullptr;
}

// Forwards SDL window resize events to TheDisplay AND tells the UI layer
// to relayout itself. ALT+ENTER (border toggle) and OS-driven resizes
// (drag, snap, Win+Left/Right, maximize/restore) all reach the engine
// through here.
//
// We do NOT run setDisplayMode synchronously here. SDL_EVENT_WINDOW_RESIZED
// is dispatched from inside SDL_PollEvent (called via PumpEvents), and Win10/
// Win11 snap shortcuts (Win+Left/Right/Up/Down) emit a burst of resize
// events during the snap animation. Tearing down the swap chain, depth
// buffer, and rebuilding the shell / control bar synchronously inside the
// SDL event-dispatch tightloop crashed the game — the swap chain ends up
// reset multiple times in quick succession while bound resources are still
// referenced from the previous frame.
//
// Instead, mirror what the Win32 WndProc does for WM_SIZE: just set
// gPendingResize. Win32GameEngine::update (whose body SDLGameEngine
// inherits unchanged) calls handleDeferredResize() between frames, which
// reads the latest GetClientRect and applies the resize once when no
// rendering is in flight. handleDeferredResize is wrapped in try/catch
// and contains the same shell-rebuild carve-out for in-mission resizes.
extern Bool gPendingResize;
static void OnPlatformResize(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;
    gPendingResize = TRUE;
}

void SDLGameEngine::init()
{
    // Register SDL event callback before engine init (which creates input devices)
    Platform::SDLPlatform::Instance().SetEventCallback(SDLEventDispatcher, nullptr);

    // Call parent init — this creates all subsystems including the game client
    // which creates SDLMouse and SDLKeyboard via the factory methods
    Win32GameEngine::init();

    // Get references to the SDL input devices created by W3DGameClient.
    // TheMouse/TheKeyboard are globals set in GameClient::init().
    extern Mouse* TheMouse;
    extern Keyboard* TheKeyboard;
    g_sdlMouse = dynamic_cast<SDLMouse*>(TheMouse);
    g_sdlKeyboard = dynamic_cast<SDLKeyboard*>(TheKeyboard);

    // Register window callbacks now that TheDisplay exists.
    Platform::PlatformCallbacks cbs;
    cbs.onResize = OnPlatformResize;
    Platform::SDLPlatform::Instance().SetCallbacks(cbs);
}

void SDLGameEngine::serviceWindowsOS()
{
    // Pump SDL events — this dispatches mouse/keyboard events to
    // SDLMouse/SDLKeyboard via the event callback above.
    Platform::SDLPlatform::Instance().PumpEvents();

    if (Platform::SDLPlatform::Instance().ShouldQuit())
        setQuitting(TRUE);

#ifdef _WIN32
    // On Windows, still process Win32 messages for IME, COM, clipboard,
    // and other system services that SDL doesn't fully handle.
    Win32GameEngine::serviceWindowsOS();
#endif
}

#endif // USE_SDL
