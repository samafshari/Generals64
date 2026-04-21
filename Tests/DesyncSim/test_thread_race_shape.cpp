// test_thread_race_shape.cpp
//
// Demonstrates the shape of the AIPathfindPrecomputed threading risk.
//
// WHAT THE LIVE CODE DOES (AIPathfindPrecomputed.cpp:706-728)
// ============================================================
// PathfindPrecomputed::rebuildAsync() spawns worker threads that build lookup
// tables and then set a "ready" flag (via std::atomic store with
// memory_order_relaxed).  The main logic thread reads the flag and, if set,
// reads the lookup tables.
//
// THE HAZARD
// ==========
// Even with an atomic flag, the *ordering* between the flag write and the
// table writes matters.  A store(relaxed) on the flag does not guarantee that
// previous non-atomic writes to the table are visible to other threads before
// they observe the flag as true.  The correct fix is:
//   - Worker: store table data, then flag.store(true, memory_order_release).
//   - Main:   if (flag.load(memory_order_acquire)) { read table }
//
// Without acquire/release pairing the compiler and CPU are free to reorder
// the flag write before the table writes, so the main thread can see:
//   flag == true   (ready!)
//   table[x]       (not fully written yet — stale or partially written)
//
// WHY THIS IS A DESYNC
// ====================
// In MP lockstep, every client runs identical logic.  If client A rebuilds
// async and the worker "wins" the race (flag published early), client A uses
// the new table.  Client B's worker is slightly slower, so client B still
// uses the old table for the same frame.  Both clients produce different
// pathfind results for the same unit → desync.
//
// THIS TEST
// =========
// Runs the read/write race 1000 times without synchronisation and counts how
// many times the reader observes an inconsistent state (flag=true but table
// value != expected).  On TSan-enabled builds the race is reported directly.
// On release builds the volatile trick may not reliably trigger the race on
// every run — but the test documents the hazard and explains the fix.
//
// The test itself PASSES (exit 0).  Its purpose is to print a warning note
// and, when compiled with -fsanitize=thread, to emit a TSan report.
//
// Build (from Tests/DesyncSim/):
//   cl /std:c++20 /EHsc /nologo test_thread_race_shape.cpp
//                               /Fe:test_thread_race_shape.exe
//
//   # For TSan (requires Clang):
//   clang++ -std=c++20 -fsanitize=thread -g test_thread_race_shape.cpp
//           -o test_thread_race_shape_tsan
//
// Exit code: 0 (always — this is a documentation/demonstration test).

#include "TestFramework.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>

// ---------------------------------------------------------------------------
// Shared state that mimics the AIPathfindPrecomputed layout.
// ---------------------------------------------------------------------------

// Simulated lookup table — in the real code this is an array of precomputed
// path costs, zone distances, etc.  We use a simple array of ints where
// every element should equal TABLE_MAGIC when fully written.
static constexpr int TABLE_SIZE  = 64;
static constexpr int TABLE_MAGIC = 0xABCD1234;

struct SimulatedTable
{
    int data[TABLE_SIZE];
};

// The flag that tells the main thread the table is ready.
// Using plain bool + volatile to maximise the chance of observing the race.
// In the real code this is std::atomic<int> with memory_order_relaxed.
static volatile bool         s_ready      = false;
static SimulatedTable        s_table      = {};
static std::atomic<int>      s_raceCount  { 0 };

// ---------------------------------------------------------------------------
// Worker: write the table, then set the flag.
// No memory fence — this is the BUGGY pattern from AIPathfindPrecomputed.
// ---------------------------------------------------------------------------
static void workerBuggy()
{
    // Simulate "building" the table.
    for (int i = 0; i < TABLE_SIZE; ++i)
        s_table.data[i] = TABLE_MAGIC;

    // Relaxed publish — the compiler / CPU may reorder this before some of
    // the table writes above.
    s_ready = true;   // plain volatile store, no fence
}

// ---------------------------------------------------------------------------
// Reader: spin until flag is set, then read the table.
// ---------------------------------------------------------------------------
static void readerCheck()
{
    // Spin until the worker publishes "ready".  In the real code this is
    // the logic-thread path that queries IsReady() before using the table.
    while (!s_ready) {
        // busy-wait (intentionally no yield, to maximise race window)
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }

    // At this point s_ready == true.  Without the proper release/acquire
    // fence, the table writes may not be visible yet.
    int inconsistent = 0;
    for (int i = 0; i < TABLE_SIZE; ++i) {
        if (s_table.data[i] != TABLE_MAGIC)
            ++inconsistent;
    }
    if (inconsistent > 0)
        s_raceCount.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------

static void TestRaceShape()
{
    Section("Thread race: flag-before-table-write hazard shape");

    // Run 1000 trials.  Each trial spawns one writer and one reader in a race.
    // We reset state between trials.
    constexpr int TRIALS = 1000;
    int racesObserved = 0;

    for (int t = 0; t < TRIALS; ++t) {
        // Reset.
        s_ready = false;
        std::memset(&s_table, 0, sizeof(s_table));
        s_raceCount.store(0, std::memory_order_relaxed);

        // Race: launch writer and reader near-simultaneously.
        std::thread reader(readerCheck);
        std::thread writer(workerBuggy);

        writer.join();
        reader.join();

        if (s_raceCount.load(std::memory_order_relaxed) > 0)
            ++racesObserved;
    }

    std::printf("  Trials: %d\n", TRIALS);
    std::printf("  Races observed (inconsistent reads): %d\n", racesObserved);

    if (racesObserved > 0) {
        std::printf("  NOTE: race was observed on this platform/build.\n");
    } else {
        std::printf("  NOTE: race was NOT observed on this platform/build.\n");
        std::printf("        This does NOT mean the code is safe — the race\n");
        std::printf("        exists in the memory model and will manifest on\n");
        std::printf("        hardware with weaker ordering or under TSan.\n");
    }

    // The test passes regardless of whether the race fired — the hazard is
    // architectural, not dependent on a particular observation in this run.
    CHECK(true);

    std::printf("\n");
    std::printf("  *** This pattern is live in AIPathfindPrecomputed.cpp:706-728 ***\n");
    std::printf("  Any future MP-determinism fix MUST make that section synchronous\n");
    std::printf("  OR replace the relaxed stores/loads with release/acquire pairs:\n");
    std::printf("    Worker:  table[i] = ...; flag.store(READY, memory_order_release);\n");
    std::printf("    Main:    if (flag.load(memory_order_acquire)) { use table; }\n");
    std::printf("  The safest MP fix is to call waitForAsync() before every logic\n");
    std::printf("  tick so async work is always complete before state is consumed.\n");
    std::printf("\n");
}

// ---------------------------------------------------------------------------
// Show the CORRECT synchronisation pattern for comparison.
// ---------------------------------------------------------------------------

static std::atomic<bool> s_correctReady  { false };
static SimulatedTable    s_correctTable  = {};

static void workerCorrect()
{
    for (int i = 0; i < TABLE_SIZE; ++i)
        s_correctTable.data[i] = TABLE_MAGIC;
    // Release store: all writes above are visible to any thread that does
    // an acquire load of s_correctReady and sees true.
    s_correctReady.store(true, std::memory_order_release);
}

static int readerCorrectCheck()
{
    while (!s_correctReady.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    int bad = 0;
    for (int i = 0; i < TABLE_SIZE; ++i)
        if (s_correctTable.data[i] != TABLE_MAGIC)
            ++bad;
    return bad;
}

static void TestCorrectSync()
{
    Section("Correct synchronisation: acquire/release eliminates the race");

    constexpr int TRIALS = 1000;
    int failures = 0;

    for (int t = 0; t < TRIALS; ++t) {
        s_correctReady.store(false, std::memory_order_relaxed);
        std::memset(&s_correctTable, 0, sizeof(s_correctTable));

        int bad = 0;
        std::thread reader([&]{ bad = readerCorrectCheck(); });
        std::thread writer(workerCorrect);
        writer.join();
        reader.join();
        if (bad > 0) ++failures;
    }

    std::printf("  Trials: %d\n", TRIALS);
    std::printf("  Inconsistent reads with correct sync: %d\n", failures);
    CHECK(failures == 0);
}

// ---------------------------------------------------------------------------

int main()
{
    TestRaceShape();
    TestCorrectSync();
    return FinalReport();
}
