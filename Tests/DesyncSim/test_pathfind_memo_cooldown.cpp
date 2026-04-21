// test_pathfind_memo_cooldown.cpp
//
// Stub-only re-implementation of the s_findPathMemo cooldown ring from
// AIPathfind.cpp lines 150-188 and verifies its determinism contracts.
//
// WHAT THE RING DOES
// ==================
// When A* expansion exceeds FINDPATH_CELL_EXPANSION_CAP, the (objectID,
// goalCellX, goalCellY) triple is parked in a ring buffer.  Repeat calls for
// the same unit+goal within FINDPATH_MEMO_COOLDOWN_FRAMES frames short-circuit
// to nullptr, preventing retry storms.
//
// DETERMINISM CONTRACT (as documented in AIPathfind.cpp ~line 145-160):
//   - Ring is indexed by GameLogic frame (integer, lockstep-identical).
//   - Keyed on ObjectID + cell coords — no floats, no wall clock.
//   - Every client evaluates findPath in the same order from the same logic
//     queue, so the ring evolves identically and produces identical nullptr
//     decisions.
//
// THIS TEST VERIFIES:
//   (a) Same key sequence on two independent ring instances produces identical
//       hit/miss results.  (If this fails, the ring itself is non-deterministic.)
//   (b) Resetting one ring at a different logical frame while the other keeps
//       the same entries produces different hit/miss results.  This establishes
//       that any client-specific reset (e.g. triggered by a non-lockstep event)
//       is a desync hazard.
//
// Build (from Tests/DesyncSim/):
//   cl /std:c++20 /EHsc /nologo test_pathfind_memo_cooldown.cpp
//                               /Fe:test_pathfind_memo_cooldown.exe

#include "TestFramework.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Re-implementation of the ring from AIPathfind.cpp.
// Names and constants match the live code so any future diff is obvious.
// ---------------------------------------------------------------------------

using ObjectID    = uint32_t;
using Int         = int32_t;
using UnsignedInt = uint32_t;

static constexpr ObjectID    INVALID_ID                   = 0u;
static constexpr Int         FINDPATH_MEMO_RING_SIZE      = 256;
static constexpr UnsignedInt FINDPATH_MEMO_COOLDOWN_FRAMES = 30;

struct FindPathMemoEntry
{
    ObjectID    objID;
    Int         goalCellX;
    Int         goalCellY;
    UnsignedInt expiryFrame;   // frame at which the entry stops short-circuiting
};

// The ring is encapsulated in a class so tests can run multiple independent
// instances without file-scope state collisions.
class PathfindMemoRing
{
public:
    PathfindMemoRing()
    {
        reset();
    }

    void reset()
    {
        std::memset(m_ring, 0, sizeof(m_ring));
        m_nextSlot = 0;
    }

    // Returns true iff a recent-failure entry exists for (obj, goalCell) that
    // hasn't expired yet.
    bool hit(ObjectID objID, Int gx, Int gy, UnsignedInt nowFrame) const
    {
        if (objID == INVALID_ID)
            return false;
        for (Int i = 0; i < FINDPATH_MEMO_RING_SIZE; ++i) {
            const FindPathMemoEntry& e = m_ring[i];
            if (e.objID == objID && e.goalCellX == gx && e.goalCellY == gy) {
                if (nowFrame < e.expiryFrame)
                    return true;
            }
        }
        return false;
    }

    // Insert a new failure entry for (obj, goalCell) at logical frame nowFrame.
    void insert(ObjectID objID, Int gx, Int gy, UnsignedInt nowFrame)
    {
        FindPathMemoEntry& slot = m_ring[m_nextSlot];
        slot.objID       = objID;
        slot.goalCellX   = gx;
        slot.goalCellY   = gy;
        slot.expiryFrame = nowFrame + FINDPATH_MEMO_COOLDOWN_FRAMES;
        m_nextSlot       = (m_nextSlot + 1) % FINDPATH_MEMO_RING_SIZE;
    }

private:
    FindPathMemoEntry m_ring[FINDPATH_MEMO_RING_SIZE];
    Int               m_nextSlot = 0;
};

// ---------------------------------------------------------------------------

int main()
{
    // -----------------------------------------------------------------------
    // (a) Same key sequence on two independent instances produces same results
    // -----------------------------------------------------------------------
    Section("(a) Two independent rings driven by the same key sequence agree");

    PathfindMemoRing ringA, ringB;

    // Simulate 50 frames of pathfinding activity.
    // Each frame: a unit fails to find a path; insert it; then check whether
    // subsequent queries for the same goal are short-circuited.
    bool allMatch = true;
    for (UnsignedInt frame = 0; frame < 50; ++frame) {
        ObjectID id = (frame % 8) + 1u;   // cycle through 8 unit IDs
        Int gx = static_cast<Int>(frame * 3);
        Int gy = static_cast<Int>(frame * 7);

        // Before insert: both rings should have the same hit/miss result.
        bool hitA_pre = ringA.hit(id, gx, gy, frame);
        bool hitB_pre = ringB.hit(id, gx, gy, frame);
        if (hitA_pre != hitB_pre) { allMatch = false; break; }

        // Insert the failure on both rings.
        ringA.insert(id, gx, gy, frame);
        ringB.insert(id, gx, gy, frame);

        // Immediately after insert: both should report a hit.
        bool hitA_post = ringA.hit(id, gx, gy, frame);
        bool hitB_post = ringB.hit(id, gx, gy, frame);
        if (hitA_post != hitB_post) { allMatch = false; break; }
    }

    // Also verify expiry: after COOLDOWN_FRAMES the entry should be gone.
    ringA.insert(999u, 100, 200, 0u);
    ringB.insert(999u, 100, 200, 0u);
    bool expiredA = !ringA.hit(999u, 100, 200, FINDPATH_MEMO_COOLDOWN_FRAMES);
    bool expiredB = !ringB.hit(999u, 100, 200, FINDPATH_MEMO_COOLDOWN_FRAMES);

    CHECK(allMatch);
    CHECK(expiredA == expiredB);
    CHECK(expiredA);   // entry must expire after cooldown

    std::printf("  All 50-frame hit/miss results matched between ringA and ringB.\n");

    // -----------------------------------------------------------------------
    // (b) Resetting one ring at a different frame produces divergent results
    // -----------------------------------------------------------------------
    Section("(b) Out-of-sync reset produces divergent hit patterns (desync hazard)");

    PathfindMemoRing syncA, syncB;

    // Identical setup: insert a known failure at frame 10.
    syncA.insert(42u, 50, 50, 10u);
    syncB.insert(42u, 50, 50, 10u);

    // Verify both agree at frame 15 (within cooldown window).
    bool hitA15 = syncA.hit(42u, 50, 50, 15u);
    bool hitB15 = syncB.hit(42u, 50, 50, 15u);
    CHECK(hitA15 == hitB15);
    CHECK(hitA15);   // should be a hit — cooldown has not expired

    // Now simulate client B receiving a non-lockstep reset (the hazard).
    // In the live engine this could be triggered by loading a save, a host
    // migration event, or any code path that calls findPathMemoReset() outside
    // the synchronised logic tick.
    syncB.reset();

    // After the desync reset, client B no longer knows about the failure.
    bool hitA20 = syncA.hit(42u, 50, 50, 20u);   // still within cooldown on A
    bool hitB20 = syncB.hit(42u, 50, 50, 20u);   // ring was wiped on B

    std::printf("  Frame 20 hit on ringA (intact): %s\n",  hitA20 ? "true" : "false");
    std::printf("  Frame 20 hit on ringB (reset):  %s\n",  hitB20 ? "true" : "false");

    // The two rings now disagree — this IS the desync.
    CHECK(hitA20 != hitB20);
    std::printf("  Divergence confirmed: ringA=%s, ringB=%s\n",
                hitA20 ? "HIT" : "MISS", hitB20 ? "HIT" : "MISS");

    // -----------------------------------------------------------------------
    // (c) Verify ring capacity: inserting more than RING_SIZE entries wraps
    //     and evicts oldest entries deterministically on both rings.
    // -----------------------------------------------------------------------
    Section("(c) Ring wrap-around is deterministic across instances");

    PathfindMemoRing wrapA, wrapB;
    // Fill the ring completely.
    for (Int i = 0; i < FINDPATH_MEMO_RING_SIZE; ++i) {
        wrapA.insert(static_cast<ObjectID>(i + 1), i, i, 0u);
        wrapB.insert(static_cast<ObjectID>(i + 1), i, i, 0u);
    }

    // Insert one more — should evict slot 0 (the entry with objectID=1, 0,0).
    wrapA.insert(999u, 0, 0, 0u);
    wrapB.insert(999u, 0, 0, 0u);

    // Entry for (1, 0, 0) should have been evicted.
    bool evictedA = !wrapA.hit(1u, 0, 0, 0u);
    bool evictedB = !wrapB.hit(1u, 0, 0, 0u);

    // New entry (999, 0, 0) should still be present.
    bool newHitA = wrapA.hit(999u, 0, 0, 0u);
    bool newHitB = wrapB.hit(999u, 0, 0, 0u);

    CHECK(evictedA == evictedB);
    CHECK(evictedA);    // oldest entry evicted
    CHECK(newHitA == newHitB);
    CHECK(newHitA);     // new entry present

    std::printf("  Ring wrap-around eviction is identical on both instances.\n");

    return FinalReport();
}
