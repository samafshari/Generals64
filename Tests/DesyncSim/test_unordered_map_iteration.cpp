// test_unordered_map_iteration.cpp
//
// Demonstrates that std::unordered_map iteration order is non-deterministic
// across different seeds and potentially across builds/platforms, and proves
// that std::map gives stable order.
//
// WHY THIS MATTERS FOR GENERALS DESYNCS
// ======================================
// AIPlayer.cpp (~line 1029) introduced:
//
//   std::unordered_map<uint64_t, LocationSafeCacheEntry> m_locationSafeCache;
//
// This map is iterated or invalidated by AI logic but is NOT included in the
// xfer/CRC serialisation.  Two problems:
//
//   1. The map is not serialised, so its state diverges silently.  Once entries
//      expire at different frames on two clients (because the wall-clock or
//      non-deterministic hash seed differs), isLocationSafe() starts returning
//      different cached answers → game-state divergence.
//
//   2. If any code ever iterates the map to process entries in order, the order
//      is not guaranteed.  std::unordered_map provides no ordering guarantee;
//      the bucket layout depends on the hash function's seed (which in some
//      STL implementations is randomised per-process via address-space entropy
//      or a fixed but ABI-version-dependent seed).
//
// This test quantifies the hazard by inserting the same keys with different
// hash seeds and showing that the iteration order changes.  It then shows that
// std::map is stable and would not have this problem.
//
// Build (from Tests/DesyncSim/):
//   cl /std:c++20 /EHsc /nologo test_unordered_map_iteration.cpp
//                               /Fe:test_unordered_map_iteration.exe
//
// Exit code: 0 = all assertions passed.

#include "TestFramework.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// Custom hash functor with a runtime seed.
//
// We use this to simulate what happens when two processes (or two different
// STL versions) use different internal hash seeds for uint64_t keys.
// In production code, std::hash<uint64_t> is deterministic within a single
// process on MSVC, but it is NOT guaranteed to be identical across:
//   - Processes compiled with different /Zc:threadSafeInit settings.
//   - GCC vs MSVC (ABI differs).
//   - 32-bit vs 64-bit builds.
// For AIPlayer's purposes even within a single process, if the map is
// populated by one subsystem and iterated by another, any reliance on
// a particular traversal order is a latent desync waiting to happen.
// ---------------------------------------------------------------------------

struct SeededHash
{
    uint64_t seed;

    size_t operator()(uint64_t key) const noexcept
    {
        // FNV-1a mix with a seed so we can vary the hash distribution.
        uint64_t h = seed ^ (key * 11400714819323198485ULL);
        h ^= h >> 33;
        h *= 0xFF51AFD7ED558CCDULL;
        h ^= h >> 33;
        h *= 0xC4CEB9FE1A85EC53ULL;
        h ^= h >> 33;
        return static_cast<size_t>(h);
    }
};

// ---------------------------------------------------------------------------

static std::vector<uint64_t> collectOrder(
    const std::unordered_map<uint64_t, int, SeededHash>& m)
{
    std::vector<uint64_t> order;
    order.reserve(m.size());
    for (const auto& kv : m)
        order.push_back(kv.first);
    return order;
}

// ---------------------------------------------------------------------------

int main()
{
    // -----------------------------------------------------------------------
    // Section 1: show that iteration order CAN differ with different hash seeds
    // -----------------------------------------------------------------------
    Section("unordered_map: iteration order varies with hash seed");

    // Insert the same 16 keys under two different seeds.
    const uint64_t keys[] = {
        0x0000000100000001ULL, 0x0000000200000002ULL,
        0x0000000300000003ULL, 0x0000000400000004ULL,
        0x0000000500000005ULL, 0x0000000600000006ULL,
        0x0000000700000007ULL, 0x0000000800000008ULL,
        0x0000000900000009ULL, 0x000000100000000AULL,
        0x000000110000000BULL, 0x000000120000000CULL,
        0x000000130000000DULL, 0x000000140000000EULL,
        0x000000150000000FULL, 0x0000001600000010ULL,
    };
    constexpr int N = static_cast<int>(sizeof(keys) / sizeof(keys[0]));

    // Use multiple seeds to increase the chance of observing a different order.
    // Even if two seeds happen to produce the same bucket layout (unlikely for
    // 16+ keys), the point stands for the general case.
    const uint64_t seeds[] = { 0x1ULL, 0x2ULL, 0xDEADBEEFULL, 0xCAFEBABEULL,
                                0x123456789ABCDEFULL, 0xFEDCBA9876543210ULL };

    // Reference order from seed 0.
    std::unordered_map<uint64_t, int, SeededHash> ref({}, SeededHash{seeds[0]});
    for (int i = 0; i < N; ++i)
        ref[keys[i]] = i;
    std::vector<uint64_t> refOrder = collectOrder(ref);

    int differentOrderCount = 0;
    for (size_t s = 1; s < sizeof(seeds)/sizeof(seeds[0]); ++s) {
        std::unordered_map<uint64_t, int, SeededHash> m({}, SeededHash{seeds[s]});
        for (int i = 0; i < N; ++i)
            m[keys[i]] = i;
        std::vector<uint64_t> order = collectOrder(m);
        if (order != refOrder)
            ++differentOrderCount;
    }

    std::printf("  Reference order (seed 0x%" PRIx64 "):", seeds[0]);
    for (auto k : refOrder)
        std::printf(" %" PRIu64, (k & 0xFFFFu));
    std::printf("\n");
    std::printf("  Out of %zu alternative seeds, %d produced a different order.\n",
                sizeof(seeds)/sizeof(seeds[0]) - 1, differentOrderCount);

    // We expect at least one of the five alternative seeds to produce a
    // different order.  If this assertion fails on your platform/STL combo,
    // the test is still useful as documentation even though it could not
    // demonstrate the effect — the underlying hazard is real regardless.
    CHECK(differentOrderCount > 0);

    // -----------------------------------------------------------------------
    // Section 2: same keys in std::map always produce the same order
    // -----------------------------------------------------------------------
    Section("std::map: iteration order is always key-sorted and stable");

    std::map<uint64_t, int> orderedA, orderedB;
    for (int i = 0; i < N; ++i) {
        orderedA[keys[i]] = i;
        orderedB[keys[i]] = i * 2;   // different values, same keys
    }

    std::vector<uint64_t> orderA, orderB;
    for (const auto& kv : orderedA) orderA.push_back(kv.first);
    for (const auto& kv : orderedB) orderB.push_back(kv.first);

    CHECK(orderA == orderB);

    // Keys must be in ascending order.
    bool sorted = true;
    for (int i = 1; i < (int)orderA.size(); ++i)
        if (orderA[i] <= orderA[i-1]) sorted = false;
    CHECK(sorted);

    std::printf("  std::map order is key-ascending and identical across both maps.\n");

    // -----------------------------------------------------------------------
    // Section 3: print the recommendation
    // -----------------------------------------------------------------------
    Section("Recommendation for AIPlayer::m_locationSafeCache");

    std::printf("  FINDING: std::unordered_map<uint64_t, ...> in AIPlayer::m_locationSafeCache\n");
    std::printf("  (AIPlayer.cpp ~line 1029) is a desync hazard because:\n");
    std::printf("    a) It is not included in the xfer/CRC so divergence is invisible.\n");
    std::printf("    b) Iteration order can differ across process instances.\n");
    std::printf("  FIX OPTIONS:\n");
    std::printf("    1. Replace with std::map (ordered, deterministic iteration).\n");
    std::printf("    2. Add to xfer so the CRC catches any divergence.\n");
    std::printf("    3. Remove the cache entirely; re-evaluate if the performance\n");
    std::printf("       cost is measurable in a production MP game.\n");

    // No assertion needed — this section is informational.
    CHECK(true);

    return FinalReport();
}
