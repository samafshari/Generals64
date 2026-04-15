#pragma once

#ifdef USE_SDL

#include "GameClient/Keyboard.h"

union SDL_Event;

// SDLKeyboard - Keyboard implementation using SDL3 events.
// Replaces DirectInputKeyboard for cross-platform input.
// Translates SDL scancodes to the game's DirectInput-based KeyDefType values.
class SDLKeyboard : public Keyboard
{
public:
    SDLKeyboard();
    virtual ~SDLKeyboard();

    virtual void init() override;
    virtual void reset() override;
    virtual void update() override;
    virtual Bool getCapsState() override;

    // Called by SDLPlatform's event callback to feed us SDL events
    void HandleSDLEvent(const SDL_Event& event);

protected:
    virtual void getKey(KeyboardIO* key) override;

private:
    // Translate SDL scancode to game's KeyDefType (DirectInput scancode)
    static UnsignedByte TranslateSDLScancode(int sdlScancode);

    struct SDLKeyEvent
    {
        UnsignedByte key;    // Translated KeyDefType
        UnsignedShort state; // KEY_STATE_* flags
        UnsignedInt time;    // Timestamp
    };

    static constexpr int EVENT_BUFFER_SIZE = 256;
    SDLKeyEvent m_eventBuffer[EVENT_BUFFER_SIZE];
    UnsignedInt m_writeIndex = 0;
    UnsignedInt m_readIndex = 0;
};

#endif // USE_SDL
