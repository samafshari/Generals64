// LockstepHarness.h
//
// Runs N GameInstance objects in lockstep inside one process.
//
// Each frame the harness:
//   1. Calls the scripted injector to submit any test-specific commands.
//   2. Drains the shared FakeNetwork ONCE to get the canonical command list.
//   3. Applies that list to every instance in the same order.
//   4. Ticks each instance (physics + RNG advance + CRC hash).
//   5. Compares per-instance frame CRCs.  First disagreement → stop and report.
//
// The injector callback is how tests inject desyncs or specific scenarios:
//   - Baseline test: injector spawns units and issues moves; all instances
//     receive the same commands so they must agree.
//   - Injected-desync test: injector or setup code corrupts one instance's
//     RNG or skips a command; harness must catch it.

#pragma once

#include "GameInstance.h"
#include "FakeNetwork.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------

struct LockstepResult
{
    bool                     diverged        = false;
    uint32_t                 divergenceFrame = 0;
    std::vector<std::string> modulesDiverged;        // from CrcCollector::diff()
    std::vector<uint32_t>    instanceCrcs;            // per-instance at divergence frame
};

// injector signature: called once per frame before ticking.
// Typical use: submit move/spawn commands from instance 0 at the right frame.
using InjectorFn = std::function<void(uint32_t frame,
                                      std::vector<GameInstance>&)>;

// ----------------------------------------------------------------------------

LockstepResult runLockstep(std::vector<GameInstance>& instances,
                           FakeNetwork&               net,
                           uint32_t                   maxFrames,
                           InjectorFn                 scriptedInjector);
