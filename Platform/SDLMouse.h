#pragma once

#ifdef USE_SDL

#include "GameClient/Mouse.h"

#include <vector>
#include <cstdint>

union SDL_Event;
struct SDL_Cursor;

// SDLMouse - Mouse implementation using SDL3 events.
// Replaces Win32Mouse for cross-platform input.
class SDLMouse : public Mouse
{
public:
    SDLMouse();
    virtual ~SDLMouse();

    virtual void init() override;
    virtual void reset() override;
    virtual void update() override;
    virtual void initCursorResources() override;
    virtual void setCursor(MouseCursor cursor) override;
    virtual void setVisibility(Bool visible) override;

    // Called by SDLPlatform's event callback to feed us SDL events
    void HandleSDLEvent(const SDL_Event& event);

protected:
    virtual void capture() override;
    virtual void releaseCapture() override;
    virtual UnsignedByte getMouseEvent(MouseIO* result, Bool flush) override;

private:
    // One slot per game cursor enum. Either:
    //   - frames.size() == 1 (static cursor — single .CUR-style frame
    //     or an SDL system fallback like SDL_SYSTEM_CURSOR_DEFAULT)
    //   - frames.size() >  1 (animated .ANI cursor with `sequence` and
    //     `jiffies` describing playback order and per-step duration)
    //
    // The frames vector owns the SDL_Cursor* handles — we destroy them
    // in the destructor.
    struct CursorSlot
    {
        std::vector<SDL_Cursor*> frames;
        std::vector<uint32_t>    sequence;  // step -> index into frames[]
        std::vector<uint32_t>    jiffies;   // duration of each step (1/60 s)
    };

    CursorSlot m_loadedCursors[NUM_MOUSE_CURSORS];

    // Animation playback state for the currently-shown cursor.
    UnsignedInt m_animStep = 0;          // current step index in m_loadedCursors[m_currentCursor].sequence
    UnsignedInt m_animStepStartedMs = 0; // SDL_GetTicks() when the current step began
    SDL_Cursor* m_lastSetSdlCursor = nullptr;

    // Try to load Data/Cursors/<textureName>.ANI through TheFileSystem
    // and decode it into a CursorSlot. Falls back to an SDL system
    // cursor (SDL_SYSTEM_CURSOR_DEFAULT et al.) if the file is missing
    // or unparseable. Always leaves a single SDL_Cursor* in the slot.
    void LoadAnimatedCursor(MouseCursor cursor);
    SDL_Cursor* CreateSystemFallback(MouseCursor cursor);
    void ApplyCursorFrame(MouseCursor cursor, UnsignedInt step);

    struct SDLMouseEvent
    {
        UnsignedInt type;    // SDL event type
        Int x, y;            // Position
        Int button;          // Button index
        Int wheelY;          // Wheel delta
        UnsignedInt time;    // Timestamp
    };

    static constexpr int EVENT_BUFFER_SIZE = 256;
    SDLMouseEvent m_eventBuffer[EVENT_BUFFER_SIZE];
    UnsignedInt m_nextFreeIndex = 0;
    UnsignedInt m_nextGetIndex = 0;
    Bool m_lostFocus = FALSE;
};

#endif // USE_SDL
