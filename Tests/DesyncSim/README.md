# DesyncSim: Multiplayer Desync Detection Test Suite

Standalone desync-detection scaffolding for Command & Conquer Generals Zero Hour (64-bit fork).
No game engine linkage required. Compiles and runs on any Windows box with MSVC or Clang.

---

## Purpose

Lockstep RTS multiplayer works by requiring every client to execute identical logic every frame and
produce an identical CRC of game state. If two clients diverge, the game is out of sync ("desync").

The classic sources of desyncs are:
- Non-deterministic data structures (iteration order depends on memory layout / ASLR).
- State that updates on one client but is never serialised into the CRC (invisible to detection).
- Concurrency: worker threads publishing partial results that the main thread reads mid-update.
- Runtime flags that change code paths asymmetrically across clients.
- Memoisation caches keyed on wall-clock or memory addresses rather than game-logic coordinates.

This suite targets seven specific hazards introduced by the 64-bit fork (see "Hazards covered"
below) without requiring the full engine. The tests run in-process, are deterministic, and exit
with a nonzero count equal to the number of failures.

---

## Architecture

### Tier 1: isolated micro-tests (self-contained, no sim required)

Each file in this tier demonstrates one hazard in isolation. They are the fastest to run and the
most informative when a specific code path is under review.

    test_unordered_map_iteration.cpp   : proves std::unordered_map order is unstable
    test_pathfind_memo_cooldown.cpp    : re-implements s_findPathMemo ring, verifies sync contract
    test_thread_race_shape.cpp         : demonstrates the async-rebuild read/write race shape

### Tier 2: scaffolded multi-instance simulation

A minimal "toy world" (StubUnit list) runs inside N GameInstance objects sharing one FakeNetwork.
Each tick drains the same command set, applies it, and hashes state into a CrcCollector. The
LockstepHarness compares per-frame CRCs across all instances and reports divergence frame + module.

    TestFramework.h                    : CHECK macros, Section(), FinalReport()
    FakeNetwork.h / .cpp               : in-memory lockstep command queue
    CrcCollector.h / .cpp              : per-module CRC capture + diff reporting
    StubEngine.h / .cpp                : deterministic stubs (clock, RNG, ObjectID, Player list)
    GameInstance.h / .cpp              : one virtual client (toy world + tick)
    LockstepHarness.h / .cpp           : runs N instances, compares CRCs, stops on divergence

### Tier 3: integration tests (scaffolded; engine wiring is a follow-up)

    test_lockstep_baseline.cpp         : positive; two identical instances must agree 1000+ frames
    test_lockstep_injected_desync.cpp  : negative; deliberate corruption triggers detection

---

## Hazards covered

| # | File | Hazard |
|---|------|--------|
| 1 | test_thread_race_shape.cpp | std::thread worker in rebuildAsync() (AIPathfindPrecomputed:706) |
| 3 | test_pathfind_memo_cooldown.cpp | s_useFixedPathfinding runtime flag (AIPathfind:1239) |
| 4 | test_pathfind_memo_cooldown.cpp | s_findPathMemo ring (AIPathfind:150-188) |
| 5 | StubEngine / GameInstance | m_cachedMoodTargetID not in xfer (AIUpdate:4695) |
| 6 | test_unordered_map_iteration.cpp | m_locationSafeCache unordered_map (AIPlayer:1029) |
| 7 | (comment only) | _MSC_VER conditional supplyTruck (AIPlayer:1083) |

Hazard 2 (AIPathfindFlowField) is being removed upstream, so no tests are written for it.

---

## Build instructions

Open a Developer Command Prompt (or any shell with `cl` on PATH from a VS install) and cd to the
`Tests/DesyncSim/` directory. Then either run the batch script:

    build.bat

Or compile individual tests manually, e.g.:

    cl /std:c++20 /EHsc /nologo test_lockstep_baseline.cpp FakeNetwork.cpp CrcCollector.cpp StubEngine.cpp GameInstance.cpp LockstepHarness.cpp /Fe:test_lockstep_baseline.exe

The `.cpp` files that implement shared infrastructure (FakeNetwork, CrcCollector, StubEngine,
GameInstance, LockstepHarness) must be listed alongside the test's own `.cpp` when compiling the
Tier-2 tests. The Tier-1 micro-tests are fully self-contained single-file builds.

---

## Running

    run_all.bat

Each test executable exits with 0 (pass) or N > 0 (N assertions failed). `run_all.bat` tallies the
failures and prints a summary. The same convention is used by `Tests/ShadowMathTests.cpp`.

---

## What is wired up now vs. scaffolded for later

### Fully implemented
- TestFramework.h: macro set, Section(), FinalReport()
- FakeNetwork: in-memory queue, deterministic delivery order, configurable latency
- CrcCollector: standalone CRC32, per-module breakdown, diff reporting
- StubEngine: deterministic clock, LCG RNG seeded from known value, ObjectID counter, player list
- GameInstance: toy world (StubUnit), MOVE/ADD_UNIT commands, per-tick hash
- LockstepHarness: N-instance loop, per-frame CRC comparison, divergence detection
- All test files (baseline, injected desync, unordered_map, pathfind memo, thread race)

### Scaffolded / TODO for follow-up integration
- GameInstance::tick() has TODO markers where real Pathfinder / AI / GameLogic calls will hook in
- CrcCollector does not yet call the engine's XferCRC. Replace the standalone CRC32 with a call
  to XferCRC once the engine headers are available.
- StubGameLogicRandom uses a simple LCG. Replace it with the real GameLogicRandomValue once
  headers are available.
- LockstepHarness does not snapshot/restore state. A full regression harness would add that.

---

## Roadmap to CMake promotion

1. Confirm all tests pass ad-hoc with `build.bat`.
2. Add a `Tests/DesyncSim/CMakeLists.txt` that defines one target per test, linking the shared
   infrastructure as a static lib (DesyncSimLib).
3. Wire into the root CMakeLists.txt under an `option(BUILD_DESYNC_TESTS ...)` guard so it does
   not affect the main engine build by default.
4. Add a CTest entry per executable so CI can run `ctest --output-on-failure`.
5. Gradually replace stubs with real engine types behind compile-time switches once the engine's
   own headers can be included without pulling in the full render/network stack.
