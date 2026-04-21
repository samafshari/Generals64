// test_lockstep_baseline.cpp
//
// POSITIVE test: two instances given identical inputs must agree for 1000+ frames.
//
// If this test fails the harness itself is broken — no real desync hazard
// analysis is meaningful until the baseline is green.
//
// Build (from Tests/DesyncSim/):
//   cl /std:c++20 /EHsc /nologo ^
//       test_lockstep_baseline.cpp ^
//       FakeNetwork.cpp CrcCollector.cpp StubEngine.cpp ^
//       GameInstance.cpp LockstepHarness.cpp ^
//       /Fe:test_lockstep_baseline.exe
//
// Exit code: 0 = all assertions passed, N = N assertions failed.

#include "TestFramework.h"
#include "FakeNetwork.h"
#include "GameInstance.h"
#include "LockstepHarness.h"

#include <cstdio>
#include <vector>

// ---------------------------------------------------------------------------
// Scenario helpers
// ---------------------------------------------------------------------------

// Submit a MOVE command for objectId to (tx, ty) from instance 0 (player 0).
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

// Submit an ADD_UNIT command from sender 0.
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

// ---------------------------------------------------------------------------

int main()
{
    Section("Baseline: two instances, same seed, same commands, 1000 frames");

    // Both instances share one network and use the same RNG seed.
    // With latency=2, commands submitted at frame F are delivered at frame F+2.
    FakeNetwork net;
    net.setLatency(2);

    const uint32_t SEED = 0xDEADBEEF;
    std::vector<GameInstance> instances;
    instances.emplace_back(0u, net, SEED);
    instances.emplace_back(1u, net, SEED);

    // Spawn two units at frame 0 (delivered at frame 2).
    submitAddUnit(net, 0, 0, 10, 10);
    submitAddUnit(net, 0, 1, 20, 20);

    // Scripted injector: every few frames issue a MOVE command.
    // ObjectIDs are 1 and 2 (issued in the order ADD_UNIT commands are applied).
    // We use hard-coded IDs here because the toy world issues IDs starting from 1
    // in submission order.
    uint32_t moveCount = 0;
    auto injector = [&](uint32_t frame, std::vector<GameInstance>&) {
        // Issue a new move every 30 frames to keep units busy.
        if (frame > 0 && frame % 30 == 0) {
            int32_t target = static_cast<int32_t>(frame % 200);
            submitMove(net, frame, 1, target,       target);
            submitMove(net, frame, 2, target + 10,  target + 10);
            ++moveCount;
        }
        // Spawn additional units every 100 frames.
        if (frame > 0 && frame % 100 == 0) {
            submitAddUnit(net, frame, 0, static_cast<int32_t>(frame % 50), 0);
        }
    };

    LockstepResult result = runLockstep(instances, net, 1000u, injector);

    CHECK(!result.diverged);

    if (result.diverged) {
        std::printf("  Divergence at frame %u\n", result.divergenceFrame);
        for (const auto& m : result.modulesDiverged)
            std::printf("  Diverged module: %s\n", m.c_str());
    } else {
        std::printf("  1000 frames completed — no divergence detected.\n");
        std::printf("  Move commands issued: %u\n", moveCount);
    }

    // Verify that both instances ended up with the same unit count.
    CHECK_EQ(instances[0].units().size(), instances[1].units().size());

    // Verify the final frame CRCs match directly as a belt-and-suspenders check.
    CHECK_CRC_MATCH(instances[0].crcCollector().frameCrc(),
                    instances[1].crcCollector().frameCrc(),
                    "final frame CRC");

    return FinalReport();
}
