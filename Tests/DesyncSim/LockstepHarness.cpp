// LockstepHarness.cpp
//
// N-instance lockstep runner.
//
// The design mirrors how the real Generals engine processes a frame:
//   a) All commands for the frame are collected from the network.
//   b) Commands are applied to the game world in a single deterministic pass.
//   c) The world state is hashed (CRC).
//   d) The CRC is compared with the CRC received from every other peer.
//      Mismatch → desync.
//
// Here "receive CRC from peer" means "read the CrcCollector of a different
// GameInstance", which is only possible because we are in-process.  In the
// real engine the CRC comparison happens via FrameDataManager after each
// peer's CRC has been transmitted over the network.

#include "LockstepHarness.h"
#include "CrcCollector.h"

#include <cstdio>

// ----------------------------------------------------------------------------

LockstepResult runLockstep(std::vector<GameInstance>& instances,
                           FakeNetwork&               net,
                           uint32_t                   maxFrames,
                           InjectorFn                 scriptedInjector)
{
    LockstepResult result;

    const size_t n = instances.size();
    if (n == 0) return result;

    for (uint32_t frame = 0; frame < maxFrames; ++frame) {

        // ------------------------------------------------------------------
        // 1. Let the test script inject commands for this frame.
        //    Commands are submitted into FakeNetwork with delivery at
        //    frame + latency, so they won't be consumed until later.
        // ------------------------------------------------------------------
        if (scriptedInjector)
            scriptedInjector(frame, instances);

        // ------------------------------------------------------------------
        // 2. Drain the network once to get the authoritative command list.
        //    All instances must apply exactly these commands, in this order.
        //    If each instance called drain() independently they would each
        //    receive the commands but the FakeNetwork queue would be emptied
        //    by the first caller — so we drain once and replay to all.
        // ------------------------------------------------------------------
        std::vector<FakeCommand> cmds = net.drain(frame);

        // ------------------------------------------------------------------
        // 3. Apply commands to every instance in identical order, then tick.
        //    The tick() advances physics and hashes state.
        // ------------------------------------------------------------------
        for (auto& inst : instances) {
            // Apply the authoritative command list to this instance before
            // ticking.  tick() only advances physics, RNG, and hashing — it
            // does not drain the network.  The split is intentional: the
            // harness is responsible for ensuring every instance receives
            // exactly the same command list in the same order.
            inst.applyCommandBatch(cmds);
            inst.tick(frame);
        }

        // ------------------------------------------------------------------
        // 4. Compare frame CRCs across all instances.
        //    Instance 0 is the reference; any deviation is a desync.
        // ------------------------------------------------------------------
        uint32_t refCrc = instances[0].crcCollector().frameCrc();
        bool diverged = false;

        for (size_t i = 1; i < n; ++i) {
            if (instances[i].crcCollector().frameCrc() != refCrc) {
                diverged = true;
                break;
            }
        }

        if (diverged) {
            result.diverged        = true;
            result.divergenceFrame = frame;

            // Collect per-instance CRCs.
            for (auto& inst : instances)
                result.instanceCrcs.push_back(inst.crcCollector().frameCrc());

            // Find which modules diverged (compare every instance against [0]).
            std::vector<std::string> allDiverged;
            for (size_t i = 1; i < n; ++i) {
                auto mods = CrcCollector::diff(instances[0].crcCollector(),
                                               instances[i].crcCollector());
                for (const auto& m : mods) {
                    // Deduplicate.
                    bool found = false;
                    for (const auto& e : allDiverged)
                        if (e == m) { found = true; break; }
                    if (!found)
                        allDiverged.push_back(m);
                }
            }
            result.modulesDiverged = allDiverged;

            std::printf("  [LockstepHarness] Desync detected at frame %u\n", frame);
            std::printf("  Instance CRCs:");
            for (uint32_t crc : result.instanceCrcs)
                std::printf(" 0x%08X", crc);
            std::printf("\n");
            if (!allDiverged.empty()) {
                std::printf("  Diverged modules:");
                for (const auto& m : allDiverged)
                    std::printf(" %s", m.c_str());
                std::printf("\n");
            }
            return result;
        }
    }

    return result;   // no divergence detected
}
