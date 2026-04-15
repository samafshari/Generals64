#pragma once

#ifdef USE_SDL

#include "Win32Device/Common/Win32GameEngine.h"

// SDLGameEngine - Extends Win32GameEngine to use SDL for event pumping.
// On Windows, inherits all W3D factories from Win32GameEngine.
// Overrides serviceWindowsOS() to use SDL_PollEvent via SDLPlatform.
//
// For macOS, a fully separate implementation will be needed that doesn't
// inherit from Win32GameEngine (Phase 4).
class SDLGameEngine : public Win32GameEngine
{
public:
    SDLGameEngine();
    virtual ~SDLGameEngine();

    virtual void init() override;
    virtual void serviceWindowsOS() override;
};

#endif // USE_SDL
