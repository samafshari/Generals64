// CrcCollector.h
//
// Layered CRC capture for per-frame, per-module state hashing.
//
// When two instances diverge we need to know *which module* first produced
// a different CRC, not just that the overall frame hash mismatched.  That
// narrows the search from "somewhere in GameLogic" to "in the AI subsystem"
// or "in the object list".
//
// The standalone CRC32 here is intentionally not tied to the engine's XferCRC
// so this file builds without any engine headers.  When the engine is available
// the caller can swap in XferCRC values by treating the bytes fed to addModule()
// as the serialised xfer output — the CrcCollector does not care what the bytes
// mean.
//
// Frame CRC is a XOR-fold over all module CRCs.  XOR is order-independent so
// it does not matter which order addModule() is called in — the fold is the
// same.  Individual module CRCs ARE keyed by name so two calls with different
// names are separate even if the data is identical.

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <map>

// ----------------------------------------------------------------------------
// Standalone CRC32 (IEEE 802.3 polynomial)
// Provided here so the file compiles without engine headers.
// To migrate to the engine's own CRC, replace calls to Crc32() with
// calls to XferCRC or whatever the engine exposes.
// ----------------------------------------------------------------------------

uint32_t Crc32(const void* data, size_t size, uint32_t initial = 0);

// ----------------------------------------------------------------------------
// CrcCollector
// ----------------------------------------------------------------------------

class CrcCollector
{
public:
    // Call once at the start of each frame before any addModule() calls.
    // Clears all module data from the previous frame.
    void beginFrame(uint32_t frame);

    // Hash 'size' bytes from 'data' into the named module slot.
    // Multiple calls for the same module name within one frame are chained
    // (the running CRC is continued, not reset), so you can call addModule()
    // incrementally as each object is serialised.
    void addModule(const char* moduleName, const void* data, size_t size);

    // Convenience overload for a single uint32_t value (very common).
    void addModuleU32(const char* moduleName, uint32_t value);

    // XOR of all per-module CRCs for the current frame.
    // Order-independent: two collectors with the same modules in different
    // order produce the same frameCrc().
    uint32_t frameCrc() const;

    // Per-module CRC for the current frame.  Returns 0 if moduleName was
    // not added this frame.
    uint32_t moduleCrc(const char* moduleName) const;

    // Which frame is currently being collected.
    uint32_t currentFrame() const { return m_frame; }

    // Call once all modules have been added.  Currently a no-op but
    // provides a hook for future validation (e.g. assert that every
    // expected module was added).
    void endFrame();

    // Compare two collectors.  Both must be in the endFrame state (or at
    // least have called beginFrame for the same logical frame).
    // Returns a list of module names whose CRCs differ.
    // If a module exists in one collector but not the other it is included.
    static std::vector<std::string> diff(const CrcCollector& a,
                                         const CrcCollector& b);

private:
    uint32_t                    m_frame = 0;
    std::map<std::string, uint32_t> m_modules;  // std::map for stable iteration order
};
