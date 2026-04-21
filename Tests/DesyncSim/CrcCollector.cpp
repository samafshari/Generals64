// CrcCollector.cpp
//
// Implementation of the layered CRC collector plus a standalone CRC32.
//
// CRC32 polynomial: 0xEDB88320 (reflected IEEE 802.3, same as zlib/Ethernet).
// This matches what most C++ CRC32 implementations produce so results are
// cross-comparable with external tools.  The engine's own XferCRC uses a
// similar table-driven approach; if/when the headers become available, the
// call to Crc32() inside addModule() can be replaced with a call into
// XferCRC without changing the CrcCollector interface.

#include "CrcCollector.h"

#include <cstring>
#include <algorithm>

// ----------------------------------------------------------------------------
// CRC32 lookup table (generated at startup, stored in BSS)
// ----------------------------------------------------------------------------

static uint32_t s_crcTable[256];
static bool     s_crcTableInit = false;

static void initCrcTable()
{
    if (s_crcTableInit) return;
    // Standard reflected CRC32 table generation.
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t v = i;
        for (int bit = 0; bit < 8; ++bit)
            v = (v & 1) ? (0xEDB88320u ^ (v >> 1)) : (v >> 1);
        s_crcTable[i] = v;
    }
    s_crcTableInit = true;
}

uint32_t Crc32(const void* data, size_t size, uint32_t initial)
{
    initCrcTable();
    uint32_t crc = ~initial;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
        crc = s_crcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

// ----------------------------------------------------------------------------
// CrcCollector
// ----------------------------------------------------------------------------

void CrcCollector::beginFrame(uint32_t frame)
{
    m_frame = frame;
    m_modules.clear();
}

void CrcCollector::addModule(const char* moduleName, const void* data, size_t size)
{
    if (moduleName == nullptr || size == 0 || data == nullptr)
        return;

    // Chain: if the module already has a running CRC from an earlier call
    // this frame, continue from it.  This lets callers stream data in chunks.
    auto it = m_modules.find(moduleName);
    uint32_t running = (it != m_modules.end()) ? it->second : 0;
    m_modules[moduleName] = Crc32(data, size, running);
}

void CrcCollector::addModuleU32(const char* moduleName, uint32_t value)
{
    addModule(moduleName, &value, sizeof(value));
}

uint32_t CrcCollector::frameCrc() const
{
    // XOR-fold over all module CRCs.  XOR is commutative and associative so
    // the order modules were added does not matter — two collectors with the
    // same set of (name, data) pairs always produce the same frameCrc even if
    // addModule() was called in a different sequence.  This is important
    // because some module iteration orders are implementation-defined.
    uint32_t result = 0;
    for (const auto& kv : m_modules)
        result ^= kv.second;
    return result;
}

uint32_t CrcCollector::moduleCrc(const char* moduleName) const
{
    if (moduleName == nullptr) return 0;
    auto it = m_modules.find(moduleName);
    return (it != m_modules.end()) ? it->second : 0;
}

void CrcCollector::endFrame()
{
    // Currently a no-op.  Future: assert that every expected module was added,
    // or snapshot the frame CRC into a history ring for post-hoc comparison.
}

std::vector<std::string> CrcCollector::diff(const CrcCollector& a,
                                             const CrcCollector& b)
{
    std::vector<std::string> diverged;

    // Collect all module names from both sides.
    // Use a sorted set so the output is deterministic regardless of map
    // internals (though std::map is already sorted, belt-and-suspenders).
    std::vector<std::string> allNames;
    for (const auto& kv : a.m_modules)
        allNames.push_back(kv.first);
    for (const auto& kv : b.m_modules) {
        if (a.m_modules.find(kv.first) == a.m_modules.end())
            allNames.push_back(kv.first);
    }
    std::sort(allNames.begin(), allNames.end());
    allNames.erase(std::unique(allNames.begin(), allNames.end()), allNames.end());

    for (const auto& name : allNames) {
        uint32_t ca = a.moduleCrc(name.c_str());
        uint32_t cb = b.moduleCrc(name.c_str());
        if (ca != cb)
            diverged.push_back(name);
    }

    return diverged;
}
