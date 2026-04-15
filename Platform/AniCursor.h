// Self-contained Windows .ANI animated cursor parser.
//
// Used by SDLMouse to load the original Generals/Zero Hour cursor art
// (Data/Cursors/*.ANI) without depending on Win32 LoadImageA — which
// resolves relative paths against the process CWD instead of the
// engine's TheFileSystem search paths and silently fails when the game
// is launched from anywhere other than the install directory.
//
// We parse RIFF/ACON ourselves, decode each embedded ICO/CUR frame
// (32-bit, 24-bit, 8-bit and 4-bit indexed all supported), apply the
// AND mask, and return BGRA pixel buffers ready to be handed to
// SDL_CreateColorCursor as SDL_PIXELFORMAT_ARGB8888 (which on
// little-endian byte-orders the int as B,G,R,A in memory — exactly
// what the decoded buffers contain).
//
// Animation playback is the caller's job: we expose the unique frame
// list, the per-step sequence (which frame to show on each step), and
// the per-step jiffies (1 jiffy = 1/60 sec). The caller advances the
// step index based on elapsed wall-clock time and swaps SDL cursors.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace AniCursor
{

struct Frame
{
    int width = 0;
    int height = 0;
    int hotspotX = 0;
    int hotspotY = 0;
    // Tightly packed BGRA (no row stride padding), top-down. Size is
    // exactly width*height*4 bytes.
    std::vector<uint8_t> bgra;
};

struct Animation
{
    // Unique decoded frames (anih.nFrames icon chunks).
    std::vector<Frame> frames;

    // Per-step sequence: which frame index in `frames` plays at each
    // animation step. If the .ANI omits the "seq " chunk this is
    // [0,1,2,...,frames.size()-1] (default identity sequence).
    std::vector<uint32_t> sequence;

    // Per-step duration in jiffies (1 jiffy = 1/60 sec). If the .ANI
    // omits the "rate" chunk every entry is anih.iDispRate.
    std::vector<uint32_t> jiffies;

    bool empty() const { return frames.empty(); }
};

// Parse a .ANI file from a raw byte buffer. Returns true on success.
// On failure `out` is left in whatever partial state we got to.
bool Parse(const uint8_t* data, size_t size, Animation& out);

} // namespace AniCursor
