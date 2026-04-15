#ifdef USE_SDL

#include "SDLKeyboard.h"
#include <SDL3/SDL.h>
#include "GameClient/KeyDefs.h"

SDLKeyboard::SDLKeyboard() {}
SDLKeyboard::~SDLKeyboard() {}

void SDLKeyboard::init()
{
    Keyboard::init();
    m_writeIndex = 0;
    m_readIndex = 0;
}

void SDLKeyboard::reset()
{
    Keyboard::reset();
    m_writeIndex = 0;
    m_readIndex = 0;
}

void SDLKeyboard::update()
{
    Keyboard::update();
}

Bool SDLKeyboard::getCapsState()
{
    return (SDL_GetModState() & SDL_KMOD_CAPS) ? TRUE : FALSE;
}

void SDLKeyboard::HandleSDLEvent(const SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
        return;

    UnsignedByte key = TranslateSDLScancode(event.key.scancode);
    if (key == KEY_NONE)
        return;

    UnsignedInt nextWrite = (m_writeIndex + 1) % EVENT_BUFFER_SIZE;
    if (nextWrite == m_readIndex)
        return; // buffer full

    SDLKeyEvent& e = m_eventBuffer[m_writeIndex];
    e.key = key;
    e.time = SDL_GetTicks();

    // Build key state flags
    UnsignedShort state = KEY_STATE_NONE;
    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        state |= KEY_STATE_DOWN;
        if (event.key.repeat)
            state |= KEY_STATE_AUTOREPEAT;
    }
    else
    {
        state |= KEY_STATE_UP;
    }

    // Add modifier flags
    SDL_Keymod mod = SDL_GetModState();
    if (mod & SDL_KMOD_LCTRL)  state |= KEY_STATE_LCONTROL;
    if (mod & SDL_KMOD_RCTRL)  state |= KEY_STATE_RCONTROL;
    if (mod & SDL_KMOD_LSHIFT) state |= KEY_STATE_LSHIFT;
    if (mod & SDL_KMOD_RSHIFT) state |= KEY_STATE_RSHIFT;
    if (mod & SDL_KMOD_LALT)   state |= KEY_STATE_LALT;
    if (mod & SDL_KMOD_RALT)   state |= KEY_STATE_RALT;
    if (mod & SDL_KMOD_CAPS)   state |= KEY_STATE_CAPSLOCK;

    e.state = state;
    m_writeIndex = nextWrite;
}

void SDLKeyboard::getKey(KeyboardIO* key)
{
    key->key = KEY_NONE;

    if (m_readIndex == m_writeIndex)
        return; // no events

    SDLKeyEvent& e = m_eventBuffer[m_readIndex];
    key->key = e.key;
    key->state = e.state;
    key->status = KeyboardIO::STATUS_UNUSED;
    key->keyDownTimeMsec = e.time;

    m_readIndex = (m_readIndex + 1) % EVENT_BUFFER_SIZE;
}

// SDL scancode to DirectInput scancode (KeyDefType) translation table.
// The game's KeyDefType enum values are DirectInput DIK_* scancode values.
// SDL uses USB HID scancodes; we map them to the equivalent DIK values.
UnsignedByte SDLKeyboard::TranslateSDLScancode(int sc)
{
    switch (sc)
    {
    case SDL_SCANCODE_ESCAPE:       return 0x01; // DIK_ESCAPE
    case SDL_SCANCODE_1:            return 0x02; // DIK_1
    case SDL_SCANCODE_2:            return 0x03; // DIK_2
    case SDL_SCANCODE_3:            return 0x04; // DIK_3
    case SDL_SCANCODE_4:            return 0x05; // DIK_4
    case SDL_SCANCODE_5:            return 0x06; // DIK_5
    case SDL_SCANCODE_6:            return 0x07; // DIK_6
    case SDL_SCANCODE_7:            return 0x08; // DIK_7
    case SDL_SCANCODE_8:            return 0x09; // DIK_8
    case SDL_SCANCODE_9:            return 0x0A; // DIK_9
    case SDL_SCANCODE_0:            return 0x0B; // DIK_0
    case SDL_SCANCODE_MINUS:        return 0x0C; // DIK_MINUS
    case SDL_SCANCODE_EQUALS:       return 0x0D; // DIK_EQUALS
    case SDL_SCANCODE_BACKSPACE:    return 0x0E; // DIK_BACK
    case SDL_SCANCODE_TAB:          return 0x0F; // DIK_TAB
    case SDL_SCANCODE_Q:            return 0x10; // DIK_Q
    case SDL_SCANCODE_W:            return 0x11; // DIK_W
    case SDL_SCANCODE_E:            return 0x12; // DIK_E
    case SDL_SCANCODE_R:            return 0x13; // DIK_R
    case SDL_SCANCODE_T:            return 0x14; // DIK_T
    case SDL_SCANCODE_Y:            return 0x15; // DIK_Y
    case SDL_SCANCODE_U:            return 0x16; // DIK_U
    case SDL_SCANCODE_I:            return 0x17; // DIK_I
    case SDL_SCANCODE_O:            return 0x18; // DIK_O
    case SDL_SCANCODE_P:            return 0x19; // DIK_P
    case SDL_SCANCODE_LEFTBRACKET:  return 0x1A; // DIK_LBRACKET
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B; // DIK_RBRACKET
    case SDL_SCANCODE_RETURN:       return 0x1C; // DIK_RETURN
    case SDL_SCANCODE_LCTRL:        return 0x1D; // DIK_LCONTROL
    case SDL_SCANCODE_A:            return 0x1E; // DIK_A
    case SDL_SCANCODE_S:            return 0x1F; // DIK_S
    case SDL_SCANCODE_D:            return 0x20; // DIK_D
    case SDL_SCANCODE_F:            return 0x21; // DIK_F
    case SDL_SCANCODE_G:            return 0x22; // DIK_G
    case SDL_SCANCODE_H:            return 0x23; // DIK_H
    case SDL_SCANCODE_J:            return 0x24; // DIK_J
    case SDL_SCANCODE_K:            return 0x25; // DIK_K
    case SDL_SCANCODE_L:            return 0x26; // DIK_L
    case SDL_SCANCODE_SEMICOLON:    return 0x27; // DIK_SEMICOLON
    case SDL_SCANCODE_APOSTROPHE:   return 0x28; // DIK_APOSTROPHE
    case SDL_SCANCODE_GRAVE:        return 0x29; // DIK_GRAVE
    case SDL_SCANCODE_LSHIFT:       return 0x2A; // DIK_LSHIFT
    case SDL_SCANCODE_BACKSLASH:    return 0x2B; // DIK_BACKSLASH
    case SDL_SCANCODE_Z:            return 0x2C; // DIK_Z
    case SDL_SCANCODE_X:            return 0x2D; // DIK_X
    case SDL_SCANCODE_C:            return 0x2E; // DIK_C
    case SDL_SCANCODE_V:            return 0x2F; // DIK_V
    case SDL_SCANCODE_B:            return 0x30; // DIK_B
    case SDL_SCANCODE_N:            return 0x31; // DIK_N
    case SDL_SCANCODE_M:            return 0x32; // DIK_M
    case SDL_SCANCODE_COMMA:        return 0x33; // DIK_COMMA
    case SDL_SCANCODE_PERIOD:       return 0x34; // DIK_PERIOD
    case SDL_SCANCODE_SLASH:        return 0x35; // DIK_SLASH
    case SDL_SCANCODE_RSHIFT:       return 0x36; // DIK_RSHIFT
    case SDL_SCANCODE_KP_MULTIPLY:  return 0x37; // DIK_NUMPADSTAR
    case SDL_SCANCODE_LALT:         return 0x38; // DIK_LALT
    case SDL_SCANCODE_SPACE:        return 0x39; // DIK_SPACE
    case SDL_SCANCODE_CAPSLOCK:     return 0x3A; // DIK_CAPSLOCK
    case SDL_SCANCODE_F1:           return 0x3B; // DIK_F1
    case SDL_SCANCODE_F2:           return 0x3C; // DIK_F2
    case SDL_SCANCODE_F3:           return 0x3D; // DIK_F3
    case SDL_SCANCODE_F4:           return 0x3E; // DIK_F4
    case SDL_SCANCODE_F5:           return 0x3F; // DIK_F5
    case SDL_SCANCODE_F6:           return 0x40; // DIK_F6
    case SDL_SCANCODE_F7:           return 0x41; // DIK_F7
    case SDL_SCANCODE_F8:           return 0x42; // DIK_F8
    case SDL_SCANCODE_F9:           return 0x43; // DIK_F9
    case SDL_SCANCODE_F10:          return 0x44; // DIK_F10
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45; // DIK_NUMLOCK
    case SDL_SCANCODE_SCROLLLOCK:   return 0x46; // DIK_SCROLL
    case SDL_SCANCODE_KP_7:         return 0x47; // DIK_NUMPAD7
    case SDL_SCANCODE_KP_8:         return 0x48; // DIK_NUMPAD8
    case SDL_SCANCODE_KP_9:         return 0x49; // DIK_NUMPAD9
    case SDL_SCANCODE_KP_MINUS:     return 0x4A; // DIK_NUMPADMINUS
    case SDL_SCANCODE_KP_4:         return 0x4B; // DIK_NUMPAD4
    case SDL_SCANCODE_KP_5:         return 0x4C; // DIK_NUMPAD5
    case SDL_SCANCODE_KP_6:         return 0x4D; // DIK_NUMPAD6
    case SDL_SCANCODE_KP_PLUS:      return 0x4E; // DIK_NUMPADPLUS
    case SDL_SCANCODE_KP_1:         return 0x4F; // DIK_NUMPAD1
    case SDL_SCANCODE_KP_2:         return 0x50; // DIK_NUMPAD2
    case SDL_SCANCODE_KP_3:         return 0x51; // DIK_NUMPAD3
    case SDL_SCANCODE_KP_0:         return 0x52; // DIK_NUMPAD0
    case SDL_SCANCODE_KP_PERIOD:    return 0x53; // DIK_NUMPADPERIOD
    case SDL_SCANCODE_NONUSBACKSLASH: return 0x56; // DIK_OEM_102
    case SDL_SCANCODE_F11:          return 0x57; // DIK_F11
    case SDL_SCANCODE_F12:          return 0x58; // DIK_F12
    case SDL_SCANCODE_KP_ENTER:     return 0x9C; // DIK_NUMPADENTER
    case SDL_SCANCODE_RCTRL:        return 0x9D; // DIK_RCONTROL
    case SDL_SCANCODE_KP_DIVIDE:    return 0xB5; // DIK_NUMPADSLASH
    case SDL_SCANCODE_PRINTSCREEN:  return 0xB7; // DIK_SYSRQ
    case SDL_SCANCODE_RALT:         return 0xB8; // DIK_RALT
    case SDL_SCANCODE_HOME:         return 0xC7; // DIK_HOME
    case SDL_SCANCODE_UP:           return 0xC8; // DIK_UPARROW
    case SDL_SCANCODE_PAGEUP:       return 0xC9; // DIK_PGUP
    case SDL_SCANCODE_LEFT:         return 0xCB; // DIK_LEFTARROW
    case SDL_SCANCODE_RIGHT:        return 0xCD; // DIK_RIGHTARROW
    case SDL_SCANCODE_END:          return 0xCF; // DIK_END
    case SDL_SCANCODE_DOWN:         return 0xD0; // DIK_DOWNARROW
    case SDL_SCANCODE_PAGEDOWN:     return 0xD1; // DIK_PGDN
    case SDL_SCANCODE_INSERT:       return 0xD2; // DIK_INSERT
    case SDL_SCANCODE_DELETE:        return 0xD3; // DIK_DELETE
    default:                        return 0x00; // KEY_NONE
    }
}

#endif // USE_SDL
