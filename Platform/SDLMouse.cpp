#ifdef USE_SDL

#include "SDLMouse.h"
#include "SDLPlatform.h"
#include "AniCursor.h"
#include "Common/FileSystem.h"
#include "Common/File.h"
#include <SDL3/SDL.h>
#include <vector>
#include <cstdio>
#include <cstring>

SDLMouse::SDLMouse() {}
SDLMouse::~SDLMouse()
{
    // Destroy every SDL_Cursor we created. Each loaded slot owns its
    // frames; system fallback cursors are also stored in frames[0] of
    // their slot, so a single sweep covers everything.
    for (CursorSlot& slot : m_loadedCursors)
    {
        for (SDL_Cursor* c : slot.frames)
            if (c) SDL_DestroyCursor(c);
        slot.frames.clear();
        slot.sequence.clear();
        slot.jiffies.clear();
    }
    m_lastSetSdlCursor = nullptr;
}

void SDLMouse::init()
{
    Mouse::init();
    // SDL mouse events report absolute window-relative positions
    // (event.button.x/y, event.motion.x/y), not deltas — so the base
    // class must treat them as absolute moves. Without this, every
    // click is interpreted as a relative motion and the cursor flies
    // off-screen, so menu UI never sees a click at its real position.
    m_inputMovesAbsolute = TRUE;
    m_nextFreeIndex = 0;
    m_nextGetIndex = 0;
    m_lostFocus = FALSE;
}

void SDLMouse::reset()
{
    Mouse::reset();
    m_nextFreeIndex = 0;
    m_nextGetIndex = 0;
}

void SDLMouse::update()
{
    if (m_lostFocus)
        return;
    Mouse::update();

    // Animated cursor playback. Win32Mouse leaves animation up to the
    // OS (HCURSORs from .ANI files animate themselves), but SDL has no
    // such thing — we have to walk the sequence ourselves.
    //
    // 1 jiffy = 1/60 sec ≈ 16.667 ms. We compare against milliseconds
    // since the current step started; when we exceed the step's jiffy
    // duration we advance and re-apply the new frame's SDL_Cursor.
    if (m_currentCursor < 0 || m_currentCursor >= NUM_MOUSE_CURSORS)
        return;
    const CursorSlot& slot = m_loadedCursors[m_currentCursor];
    if (slot.frames.size() <= 1 || slot.sequence.empty())
        return;

    const UnsignedInt nowMs   = SDL_GetTicks();
    const UnsignedInt stepIdx = m_animStep % slot.sequence.size();
    const uint32_t    jiffies = (stepIdx < slot.jiffies.size())
        ? slot.jiffies[stepIdx]
        : 6u; // ~10 fps default
    const UnsignedInt stepMs  = (jiffies * 1000u + 30u) / 60u; // round

    if (nowMs - m_animStepStartedMs >= stepMs)
    {
        m_animStep = (stepIdx + 1) % slot.sequence.size();
        m_animStepStartedMs = nowMs;
        ApplyCursorFrame(m_currentCursor, m_animStep);
    }
}

// ─── Cursor loading ──────────────────────────────────────────────────
//
// On startup we walk every cursor enum (FIRST_CURSOR..NUM_MOUSE_CURSORS),
// look up its texture name in m_cursorInfo (populated earlier by
// Mouse::parseIni() from Data/INI/Mouse/*.ini), and try to load the
// matching Data/Cursors/<name>.ANI through TheFileSystem so search-path
// resolution works regardless of CWD. The bytes are decoded by
// AniCursor::Parse and each unique frame becomes one SDL_Cursor*.
//
// If the file is missing or unparseable we fall back to the closest
// SDL system cursor (SDL_SYSTEM_CURSOR_DEFAULT et al.). The slot always
// ends up holding at least one frame so setCursor() can never crash on
// an empty array.

// Map a game-side MouseCursor enum to the closest SDL3 system cursor.
// Used as a fallback when the .ANI load fails.
static SDL_SystemCursor MapToSDLSystemCursor(Mouse::MouseCursor cursor)
{
    switch (cursor)
    {
    case Mouse::ARROW:
    case Mouse::NORMAL:                 return SDL_SYSTEM_CURSOR_DEFAULT;
    case Mouse::SCROLL:                 return SDL_SYSTEM_CURSOR_MOVE;
    case Mouse::CROSS:
    case Mouse::ATTACKMOVETO:
    case Mouse::FORCE_ATTACK_OBJECT:
    case Mouse::FORCE_ATTACK_GROUND:
    case Mouse::ATTACK_OBJECT:
    case Mouse::WAYPOINT:
    case Mouse::SET_RALLY_POINT:
    case Mouse::SNIPE_VEHICLE:
    case Mouse::LASER_GUIDED_MISSILES:
    case Mouse::PARTICLE_UPLINK_CANNON: return SDL_SYSTEM_CURSOR_CROSSHAIR;
    case Mouse::SELECTING:
    case Mouse::ENTER_FRIENDLY:
    case Mouse::ENTER_AGGRESSIVELY:
    case Mouse::CAPTUREBUILDING:
    case Mouse::HACK:                   return SDL_SYSTEM_CURSOR_POINTER;
    case Mouse::GENERIC_INVALID:
    case Mouse::INVALID_BUILD_PLACEMENT:
    case Mouse::OUTRANGE:
    case Mouse::STAB_ATTACK_INVALID:
    case Mouse::PLACE_CHARGE_INVALID:   return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    case Mouse::DO_REPAIR:
    case Mouse::GET_REPAIRED:
    case Mouse::GET_HEALED:
    case Mouse::RESUME_CONSTRUCTION:    return SDL_SYSTEM_CURSOR_PROGRESS;
    default:                            return SDL_SYSTEM_CURSOR_DEFAULT;
    }
}

SDL_Cursor* SDLMouse::CreateSystemFallback(MouseCursor cursor)
{
    return SDL_CreateSystemCursor(MapToSDLSystemCursor(cursor));
}

void SDLMouse::LoadAnimatedCursor(MouseCursor cursor)
{
    CursorSlot& slot = m_loadedCursors[cursor];
    if (!slot.frames.empty())
        return; // already loaded

    // No texture name in the INI? Use a system cursor and bail.
    if (m_cursorInfo[cursor].textureName.isEmpty())
    {
        slot.frames.push_back(CreateSystemFallback(cursor));
        return;
    }

    // Build the path the same way Win32Mouse does. Multi-direction
    // cursors get a "0" suffix because the engine stores one .ANI per
    // direction; we only use direction 0 here (the camera-relative
    // rotation logic from W3DMouse isn't wired up to SDL).
    char relPath[256];
    if (m_cursorInfo[cursor].numDirections > 1)
        snprintf(relPath, sizeof(relPath), "Data\\Cursors\\%s0.ANI",
                 m_cursorInfo[cursor].textureName.str());
    else
        snprintf(relPath, sizeof(relPath), "Data\\Cursors\\%s.ANI",
                 m_cursorInfo[cursor].textureName.str());

    // Pull bytes through TheFileSystem so search paths (and BIGs)
    // resolve correctly. LoadImageA / fopen would honour CWD only.
    File* file = TheFileSystem
        ? TheFileSystem->openFile(relPath, File::READ | File::BINARY)
        : nullptr;

    std::vector<uint8_t> bytes;
    if (file)
    {
        file->seek(0, File::END);
        const Int sz = file->position();
        file->seek(0, File::START);
        if (sz > 0)
        {
            bytes.resize(size_t(sz));
            file->read(bytes.data(), sz);
        }
        file->close();
    }

    AniCursor::Animation anim;
    if (bytes.empty() || !AniCursor::Parse(bytes.data(), bytes.size(), anim))
    {
        // Couldn't read or parse — fall back so the user still gets
        // *some* cursor.
        slot.frames.push_back(CreateSystemFallback(cursor));
        return;
    }

    // Convert each unique decoded frame into an SDL_Cursor.
    slot.frames.reserve(anim.frames.size());
    for (const AniCursor::Frame& fr : anim.frames)
    {
        // SDL_PIXELFORMAT_ARGB8888 on little-endian = byte order BGRA,
        // which is exactly what AniCursor produces. The 4*width row
        // stride matches our tightly-packed buffer.
        SDL_Surface* surf = SDL_CreateSurfaceFrom(
            fr.width, fr.height, SDL_PIXELFORMAT_ARGB8888,
            const_cast<uint8_t*>(fr.bgra.data()), fr.width * 4);
        if (!surf)
        {
            slot.frames.push_back(nullptr);
            continue;
        }
        SDL_Cursor* sdlCursor = SDL_CreateColorCursor(surf, fr.hotspotX, fr.hotspotY);
        SDL_DestroySurface(surf);
        slot.frames.push_back(sdlCursor);
    }
    slot.sequence = std::move(anim.sequence);
    slot.jiffies  = std::move(anim.jiffies);

    // If every frame failed to create (out of GPU surfaces, weird
    // surface format), fall back so we still display *something*.
    bool anyFrame = false;
    for (SDL_Cursor* c : slot.frames) if (c) { anyFrame = true; break; }
    if (!anyFrame)
    {
        slot.frames.clear();
        slot.frames.push_back(CreateSystemFallback(cursor));
        slot.sequence.clear();
        slot.jiffies.clear();
    }
}

void SDLMouse::initCursorResources()
{
    // Make sure the OS cursor is showing from the moment the window opens.
    Platform::SDLPlatform::Instance().SetCursorVisible(true);

    // Pre-load every cursor up front so the first setCursor() call in
    // game code doesn't pay the file-IO cost (and so we don't see a
    // one-frame fallback flicker).
    for (int c = FIRST_CURSOR; c < NUM_MOUSE_CURSORS; ++c)
        LoadAnimatedCursor(MouseCursor(c));
}

// Push frame[step] to SDL, but only if it's actually changed since
// the last call — SDL_SetCursor is cheap but not free, and the menu
// reveal animation calls update() at video frame rate (~60 Hz).
void SDLMouse::ApplyCursorFrame(MouseCursor cursor, UnsignedInt step)
{
    if (cursor < 0 || cursor >= NUM_MOUSE_CURSORS) return;
    const CursorSlot& slot = m_loadedCursors[cursor];
    if (slot.frames.empty()) return;

    UnsignedInt frameIdx = 0;
    if (!slot.sequence.empty())
        frameIdx = slot.sequence[step % slot.sequence.size()];
    if (frameIdx >= slot.frames.size()) frameIdx = 0;

    SDL_Cursor* c = slot.frames[frameIdx];
    if (c && c != m_lastSetSdlCursor)
    {
        SDL_SetCursor(c);
        m_lastSetSdlCursor = c;
    }
}

void SDLMouse::setCursor(MouseCursor cursor)
{
    if (cursor < 0 || cursor >= NUM_MOUSE_CURSORS)
        return;

    // Lazy-load if initCursorResources hasn't run for some reason.
    if (m_loadedCursors[cursor].frames.empty())
        LoadAnimatedCursor(cursor);

    // Reset animation timer whenever the cursor identity changes so a
    // multi-frame cursor always starts from step 0.
    if (m_currentCursor != cursor)
    {
        m_animStep = 0;
        m_animStepStartedMs = SDL_GetTicks();
    }
    m_currentCursor = cursor;

    ApplyCursorFrame(cursor, m_animStep);
}

void SDLMouse::setVisibility(Bool visible)
{
    // Track the game's intent on the base class…
    m_visible = visible;
    // …but always keep the OS cursor showing. The game requests visibility=
    // false in-game expecting to draw its own cursor (the W3D textured-quad
    // cursor system from the original DX8 build), which we don't implement.
    // If we honored the hide request, the user would have no cursor at all
    // during gameplay. Showing the OS arrow is a much better fallback.
    Platform::SDLPlatform::Instance().SetCursorVisible(true);
}

void SDLMouse::capture()
{
    // Match Win32Mouse::capture(): in windowed mode, do not physically
    // confine the cursor — the user must be able to drag it onto the
    // title bar / minimize / close buttons and out of the window onto
    // other monitors. Only fullscreen actually clips. The base
    // Mouse::isCursorCaptured flag is updated by Mouse::capture() callers
    // independently, so it stays consistent either way.
    if (Platform::SDLPlatform::Instance().IsWindowed())
        return;
    Platform::SDLPlatform::Instance().SetCursorCapture(true);
}

void SDLMouse::releaseCapture()
{
    // Always release — even in windowed mode, in case the window flipped
    // from fullscreen to windowed while a grab was active.
    Platform::SDLPlatform::Instance().SetCursorCapture(false);
}

void SDLMouse::HandleSDLEvent(const SDL_Event& event)
{
    UnsignedInt nextFree = (m_nextFreeIndex + 1) % EVENT_BUFFER_SIZE;
    if (nextFree == m_nextGetIndex)
        return; // buffer full

    SDLMouseEvent& e = m_eventBuffer[m_nextFreeIndex];
    e.type = event.type;
    e.x = 0;
    e.y = 0;
    e.button = 0;
    e.wheelY = 0;
    e.time = SDL_GetTicks();

    switch (event.type)
    {
    case SDL_EVENT_MOUSE_MOTION:
        e.x = (Int)event.motion.x;
        e.y = (Int)event.motion.y;
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        e.x = (Int)event.button.x;
        e.y = (Int)event.button.y;
        e.button = event.button.button;
        // Detect double-click via SDL's click count
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.clicks >= 2)
            e.type = 0xFFFF; // sentinel for double-click, handled in getMouseEvent
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        // SDL3 reports wheel.y in notches (1.0 per detent), but the engine's
        // Mouse::createStreamMessages divides wheelPos by 120 (Win32 WHEEL_DELTA)
        // before sending the zoom amount to LookAtXlat. Without this scale,
        // 1/120 = 0 and the camera never zooms. Multiply here so the engine
        // sees Win32-style deltas.
        e.wheelY = (Int)(event.wheel.y * 120.0f);
        {
            float mx, my;
            SDL_GetMouseState(&mx, &my);
            e.x = (Int)mx;
            e.y = (Int)my;
        }
        break;

    default:
        return;
    }

    m_nextFreeIndex = nextFree;
}

UnsignedByte SDLMouse::getMouseEvent(MouseIO* result, Bool flush)
{
    if (m_nextGetIndex == m_nextFreeIndex)
        return MOUSE_NONE;

    SDLMouseEvent& e = m_eventBuffer[m_nextGetIndex];

    // Match Win32Mouse::translateEvent defaults: all states MBS_None,
    // all positions zero. MBS_None means "no change for this button".
    result->pos.x = e.x;
    result->pos.y = e.y;
    result->time = e.time;
    result->wheelPos = 0;
    result->deltaPos.x = 0;
    result->deltaPos.y = 0;
    result->leftState = MBS_None;
    result->leftEvent = 0;
    result->rightState = MBS_None;
    result->rightEvent = 0;
    result->middleState = MBS_None;
    result->middleEvent = 0;

    switch (e.type)
    {
    case SDL_EVENT_MOUSE_MOTION:
        // Position only, no button state change
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button == SDL_BUTTON_LEFT)        result->leftState = MBS_Down;
        else if (e.button == SDL_BUTTON_RIGHT)  result->rightState = MBS_Down;
        else if (e.button == SDL_BUTTON_MIDDLE) result->middleState = MBS_Down;
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button == SDL_BUTTON_LEFT)        result->leftState = MBS_Up;
        else if (e.button == SDL_BUTTON_RIGHT)  result->rightState = MBS_Up;
        else if (e.button == SDL_BUTTON_MIDDLE) result->middleState = MBS_Up;
        break;

    case 0xFFFF: // Double-click sentinel
        if (e.button == SDL_BUTTON_LEFT)        result->leftState = MBS_DoubleClick;
        else if (e.button == SDL_BUTTON_RIGHT)  result->rightState = MBS_DoubleClick;
        else if (e.button == SDL_BUTTON_MIDDLE) result->middleState = MBS_DoubleClick;
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        result->wheelPos = e.wheelY;
        break;
    }

    if (flush)
        m_nextGetIndex = (m_nextGetIndex + 1) % EVENT_BUFFER_SIZE;

    return MOUSE_OK;
}

#endif // USE_SDL
