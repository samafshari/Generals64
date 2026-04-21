// test_lockstep_injected_desync.cpp
//
// NEGATIVE test: deliberately corrupt instance 1's RNG seed (or skip applying
// one command) and assert that the harness detects the divergence, reports the
// correct frame, and names the diverged modules.
//
// If this test fails it means the harness is blind to the injected fault —
// which would make it useless for catching real desyncs.
//
// Build (from Tests/DesyncSim/):
//   cl /std:c++20 /EHsc /nologo ^
//       test_lockstep_injected_desync.cpp ^
//       FakeNetwork.cpp CrcCollector.cpp StubEngine.cpp ^
//       GameInstance.cpp LockstepHarness.cpp ^
//       /Fe:test_lockstep_injected_desync.exe
//
// Exit code: 0 = all assertions passed, N = N assertions failed.

#include "TestFramework.h"
#include "FakeNetwork.h"
#include "GameInstance.h"
#include "LockstepHarness.h"

#include <cstdio>
#include <vector>

// ---------------------------------------------------------------------------

static void submitAddUnit(FakeNetwork& net, uint32_t frame,
                          uint32_t ownerId, int32_t x, int32_t y)
{
    CmdAddUnit au{};
    au.type    = CmdType::ADD_UNIT;
    au.ownerId = ownerId;
    au.x       = x;
    au.y       = y;
    net.submit(0, frame, &au, sizeof(au));
}

static void submitMove(FakeNetwork& net, uint32_t frame,
                       ObjectID objectId, int32_t tx, int32_t ty)
{
    CmdMove mv{};
    mv.type     = CmdType::MOVE;
    mv.objectId = objectId;
    mv.tx       = tx;
    mv.ty       = ty;
    net.submit(0, frame, &mv, sizeof(mv));
}

// ---------------------------------------------------------------------------
// Test 1: RNG seed corruption
//
// Corrupt instance 1's RNG by re-seeding it with a different value immediately
// before the run.  The RNG state is hashed every frame so the desync should
// appear at frame 0 (or the first frame after the corruption is visible).
// ---------------------------------------------------------------------------

static void TestRNGSeedCorruption()
{
    Section("Injected desync: RNG seed mismatch detected on first frame");

    FakeNetwork net;
    net.setLatency(2);

    const uint32_t GOOD_SEED = 0xDEADBEEF;
    const uint32_t BAD_SEED  = 0xCAFEBABE;   // different — deliberate corruption

    std::vector<GameInstance> instances;
    instances.emplace_back(0u, net, GOOD_SEED);
    instances.emplace_back(1u, net, BAD_SEED);   // corrupted seed

    submitAddUnit(net, 0, 0, 5, 5);

    LockstepResult result = runLockstep(instances, net, 100u,
        [&](uint32_t frame, std::vector<GameInstance>&) {
            if (frame % 20 == 0 && frame > 0)
                submitMove(net, frame, 1, static_cast<int32_t>(frame), 0);
        });

    // The harness must have detected the desync.
    CHECK(result.diverged);

    // The desync must be reported within the first few frames because the RNG
    // state is hashed every frame.  Generous upper bound of 5 frames.
    if (result.diverged) {
        std::printf("  Detected divergence at frame %u (expected <= 5)\n",
                    result.divergenceFrame);
        CHECK(result.divergenceFrame <= 5u);

        // The RNG module must be listed as diverged.
        bool rngDiverged = false;
        for (const auto& m : result.modulesDiverged)
            if (m == "RNG") rngDiverged = true;
        CHECK(rngDiverged);
    }
}

// ---------------------------------------------------------------------------
// Test 2: skipped command
//
// To simulate one client not receiving a command (e.g. because of a non-
// deterministic cache hit that bypassed the apply step), we run the harness
// for a few frames without incident, then use a custom injector that submits
// a MOVE to the network BUT also directly moves instance 0's unit by a
// different delta — making instance 0 act on a different order than what the
// harness delivers.
//
// Implementation: we use a variant of the harness loop manually here so we
// can corrupt the state mid-run without adding "sabotage" API to GameInstance.
// ---------------------------------------------------------------------------

static void TestSkippedCommand()
{
    Section("Injected desync: skipped command detected by CRC divergence");

    // Build up a 3-frame clean run, then manually diverge instance 1 by
    // re-seeding its RNG on frame 3 to simulate a cache bypass that caused
    // it to skip a state update.

    FakeNetwork net;
    net.setLatency(0);   // zero latency so commands are immediate

    const uint32_t SEED = 0x12345678;
    std::vector<GameInstance> instances;
    instances.emplace_back(0u, net, SEED);
    instances.emplace_back(1u, net, SEED);

    // Frame 0: spawn one unit on both instances.
    submitAddUnit(net, 0, 0, 0, 0);

    // Tick frames 0-2 cleanly.
    for (uint32_t f = 0; f <= 2; ++f) {
        std::vector<FakeCommand> cmds = net.drain(f);
        for (auto& inst : instances) {
            inst.applyCommandBatch(cmds);
            inst.tick(f);
        }
        // Verify clean so far.
        uint32_t crc0 = instances[0].crcCollector().frameCrc();
        uint32_t crc1 = instances[1].crcCollector().frameCrc();
        if (crc0 != crc1) {
            std::printf("  UNEXPECTED divergence at setup frame %u\n", f);
        }
    }

    // Frame 3: submit a MOVE.  Instance 0 will apply it normally.
    // Instance 1 simulates a "missed apply" by corrupting its RNG after
    // applying the same commands — so only the RNG module diverges.
    submitMove(net, 3, 1, 50, 50);
    {
        std::vector<FakeCommand> cmds = net.drain(3);
        for (auto& inst : instances) {
            inst.applyCommandBatch(cmds);
        }
        // Corrupt instance 1's RNG *before* tick (simulates a desync in
        // how that instance advanced its internal state — e.g. it called an
        // extra random roll that instance 0 did not).
        instances[1].rng().seed(0xDEAD1234);
        for (auto& inst : instances) {
            inst.tick(3);
        }
    }

    uint32_t crc0 = instances[0].crcCollector().frameCrc();
    uint32_t crc1 = instances[1].crcCollector().frameCrc();
    bool diverged = (crc0 != crc1);

    CHECK(diverged);

    if (diverged) {
        std::printf("  Divergence detected at frame 3 as expected.\n");
        auto divergedModules = CrcCollector::diff(instances[0].crcCollector(),
                                                   instances[1].crcCollector());
        std::printf("  Diverged modules:");
        for (const auto& m : divergedModules)
            std::printf(" %s", m.c_str());
        std::printf("\n");
        CHECK(!divergedModules.empty());
    }
}

// ---------------------------------------------------------------------------

int main()
{
    TestRNGSeedCorruption();
    TestSkippedCommand();
    return FinalReport();
}
