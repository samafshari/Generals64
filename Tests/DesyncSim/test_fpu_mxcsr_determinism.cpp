// test_fpu_mxcsr_determinism.cpp
//
// Verifies that the x64 sim's FPU/MXCSR discipline actually produces bit-identical
// SSE2 float results across "dirty" and "clean" initial states — i.e. that pinning
// MXCSR at the start of each sim tick is sufficient to immunise the lockstep sim
// against driver/DLL contamination of the control register.
//
// WHY THIS TEST EXISTS
// --------------------
// On x86, GameLogic::setFPMode() reset the x87 control word every tick. On x64,
// for years it was a NO-OP ("SSE2 always uses fixed precision — nothing to
// configure"). This is wrong: SSE2 honours MXCSR, and MXCSR state bleeds in from
// DirectX / audio / input / media drivers whose internal math paths enable FTZ
// (flush-to-zero) or DAZ (denormals-are-zero) for perf. Two peers that loaded
// slightly different driver versions therefore inherited different MXCSR values
// at sim time, producing bit-divergent SSE arithmetic — the exact signature of
// a single-unit sub-voxel position drift we observed on a Laser Comanche.
//
// We fixed setFPMode() to pin MXCSR to 0x1F80 (IEEE-754 default: round-to-nearest,
// FTZ off, DAZ off, all exceptions masked). This test reproduces the "dirty
// driver" condition by deliberately setting MXCSR to an FTZ/DAZ/round-down state
// BEFORE the "sim tick" begins, invokes a pin-MXCSR shim that mirrors what the
// real setFPMode does, runs a batch of SSE math (dot products, normalisations,
// atan2 — the kind of thing the helicopter locomotor does every frame), and
// asserts the bit-pattern of the result matches a golden value.
//
// If this test fails, either:
//   (a) the pin is wrong (some MXCSR bit isn't being overwritten), or
//   (b) the math is genuinely non-deterministic for reasons beyond MXCSR
//       (a compiler FMA contraction, for instance — in which case the
//       project's /fp: flag is wrong).
//
// Self-contained. No engine linkage. Build:
//   cl /std:c++20 /EHsc /nologo /arch:AVX2 test_fpu_mxcsr_determinism.cpp ...
//   (AVX2 not strictly required; /arch:SSE2 is enough. AVX2 is just the common
//   default for modern MSVC x64 builds and matches the shipping binary.)

#include "TestFramework.h"

#include <cstdint>
#include <cmath>
#include <cstring>

#if defined(_M_IX86)
#error "This test is for x64 only — x86 uses a different FPU control path."
#endif

#include <xmmintrin.h>   // _mm_setcsr, _mm_getcsr
#include <emmintrin.h>   // SSE2 intrinsics used by the sim-like math block


// What GameLogic::setFPMode() pins MXCSR to on x64. IEEE-754 default.
//   bits 0..5  : exception flags (PE UE OE ZE DE IE)  — clear
//   bit 6      : DAZ (denormals-are-zero)             — OFF
//   bits 7..12 : exception masks (PM UM OM ZM DM IM)  — all ON (masked)
//   bits 13..14: rounding mode                        — 00 = round-to-nearest
//   bit 15     : FTZ (flush-to-zero)                  — OFF
static constexpr unsigned int CLEAN_MXCSR = 0x1F80u;

// ALL exception masks on. Any dirty pattern must preserve these or the SSE
// ops in the test harness raise SIGFPE / hang the process. We only want to
// model the perf-flip bits that real-world drivers actually toggle:
//   FTZ (bit 15), DAZ (bit 6), rounding mode (bits 13..14).
static constexpr unsigned int EXC_MASK_BITS = 0x1F80u;

// A representative "dirty" state. Bits that differ from CLEAN:
//   FTZ = 1, DAZ = 1, rounding = 11 (round-toward-zero).
// Exception masks stay on so the test itself doesn't crash; divergence
// comes from the rounding mode and denormal-flush behaviour, which is
// what actual DirectX/audio drivers leave behind.
static constexpr unsigned int DIRTY_MXCSR = EXC_MASK_BITS | 0x8040u | 0x6000u;


// Mirror of the production setFPMode path for the x64 branch. If the real
// function ever changes, update this too.
static inline void pinSimMXCSR()
{
    _mm_setcsr(CLEAN_MXCSR);
}


// A workload that *would* diverge under different MXCSR states if setFPMode
// didn't reset it first. Specifically:
//
//   - Normalisation of a tiny vector: without DAZ the 1/sqrt(x) of a denormal
//     produces a real finite result; with DAZ the denormal is read as zero,
//     1/0 = inf, and the result is garbage. So this step differs between
//     dirty and clean MXCSR.
//   - atan2 on a near-zero y with a positive x: can underflow internal
//     intermediates, which FTZ mode silences to 0 and changes the reported
//     angle by ~1 ULP. Sim-visible over a few frames.
//   - A rounding-sensitive step: (a+b) - (a-b) with a >> b. Under round-down
//     it rounds differently than under round-nearest.
//
// This is intentionally one helicopter-locomotor-flavoured tick of work:
// normalise a facing, compute an angle to a goal, integrate forward one
// frame. Matches the call shape of the code that produced our observed
// drift.
struct SimTickResult
{
    float dirX, dirY;        // normalised facing
    float angleToGoal;       // atan2 result
    float integratedX;       // position + vel*dt after rounding-sensitive step
};

static SimTickResult runSimTick(float startX, float startY,
                                float goalX,  float goalY,
                                float velMag, float dt)
{
    SimTickResult r{};

    // Facing vector = (goal - start), normalised. Small goals (within one
    // frame of travel) stress the denormal/DAZ path.
    float dx = goalX - startX;
    float dy = goalY - startY;
    float lenSq = dx * dx + dy * dy;
    float invLen = 1.0f / std::sqrtf(lenSq);   // MXCSR-sensitive under DAZ
    r.dirX = dx * invLen;
    r.dirY = dy * invLen;

    // Angle to goal. atan2 underflow behaviour changes under FTZ.
    r.angleToGoal = std::atan2f(dy, dx);

    // Integrate one step. The (a+b) - (a-b) idiom is rounding-sensitive:
    // under round-down, the two subexpressions round differently than under
    // round-nearest, and the result differs by the last-bit rounding error.
    float vel = velMag;
    float a = startX + vel * dt;
    float b = startX - vel * dt;
    float combined = (a + b) - (a - b);        // algebraically = 2*vel*dt
    r.integratedX = startX + combined * 0.5f;  // = startX + vel*dt

    return r;
}


// Helpers to compare two results bit-for-bit. We use integer reinterpretation
// so NaN compares as itself and -0 != +0 etc — matches the XferCRC semantics
// the engine uses (memcpy + CRC32).
static bool bitwiseEqual(float a, float b)
{
    uint32_t ua, ub;
    std::memcpy(&ua, &a, 4);
    std::memcpy(&ub, &b, 4);
    return ua == ub;
}

static bool bitwiseEqual(const SimTickResult& a, const SimTickResult& b)
{
    return bitwiseEqual(a.dirX,         b.dirX)
        && bitwiseEqual(a.dirY,         b.dirY)
        && bitwiseEqual(a.angleToGoal,  b.angleToGoal)
        && bitwiseEqual(a.integratedX,  b.integratedX);
}


int main()
{
    // ------------------------------------------------------------------------
    Section("MXCSR is writable and readable");

    _mm_setcsr(CLEAN_MXCSR);
    CHECK_EQ(_mm_getcsr(), CLEAN_MXCSR);

    _mm_setcsr(DIRTY_MXCSR);
    CHECK_EQ(_mm_getcsr(), DIRTY_MXCSR);

    // Pin restores it.
    pinSimMXCSR();
    CHECK_EQ(_mm_getcsr(), CLEAN_MXCSR);


    // ------------------------------------------------------------------------
    Section("Dirty MXCSR produces observable math divergence (sanity)");
    //
    // This section's purpose is to prove the workload above is *actually*
    // MXCSR-sensitive. If this ever stops failing, the test block is too
    // weak to protect against the real bug and should be strengthened.

    // First, compute the golden (clean) result.
    _mm_setcsr(CLEAN_MXCSR);
    SimTickResult golden = runSimTick(
        /* start  */ 2685.83f, 4304.28f,
        /* goal   */ 2686.68f, 4303.94f,   // numbers taken from the real
                                           // desync.log manifest
        /* velMag */ 50.0f,
        /* dt     */ 1.0f / 70.0f);        // 70 Hz game logic

    // Now dirty MXCSR and re-run the same inputs. The results should differ —
    // that's what makes MXCSR matter for lockstep.
    _mm_setcsr(DIRTY_MXCSR);
    SimTickResult dirty = runSimTick(2685.83f, 4304.28f,
                                     2686.68f, 4303.94f,
                                     50.0f, 1.0f / 70.0f);

    // At least one field should differ bit-for-bit, proving MXCSR is being
    // honoured. If this CHECK *passes*, the test is doing its job. If it
    // fails (the two results match), the workload isn't probing MXCSR
    // effectively and we need a harsher stimulus.
    bool anyDiffers =
        !bitwiseEqual(golden.dirX,        dirty.dirX) ||
        !bitwiseEqual(golden.dirY,        dirty.dirY) ||
        !bitwiseEqual(golden.angleToGoal, dirty.angleToGoal) ||
        !bitwiseEqual(golden.integratedX, dirty.integratedX);
    CHECK(anyDiffers);


    // ------------------------------------------------------------------------
    Section("Pinning MXCSR restores bit-identical math after contamination");
    //
    // This is the actual proof that setFPMode's x64 fix works.
    //
    // Simulate the real scenario: a driver/DLL has left MXCSR dirty; the sim
    // tick calls pinSimMXCSR() at entry; the subsequent math must match the
    // golden (clean) result exactly.

    for (int trial = 0; trial < 8; ++trial)
    {
        // Every trial uses a differently-dirty starting state, but keeps all
        // exception masks ON (EXC_MASK_BITS) so the test harness itself
        // doesn't take a SIGFPE on denormal/underflow math. What varies is
        // the perf-flip bits that real drivers actually toggle: FTZ (bit 15),
        // DAZ (bit 6), rounding mode (bits 13-14).
        //
        //  bit 15 = FTZ     bit 6 = DAZ     bits 13..14 = rounding
        //                                    00=nearest 01=down 10=up 11=zero
        const unsigned int dirtySet[8] = {
            EXC_MASK_BITS | 0x8000u,               // FTZ only
            EXC_MASK_BITS | 0x0040u,               // DAZ only
            EXC_MASK_BITS | 0x8040u,               // FTZ + DAZ
            EXC_MASK_BITS | 0x2000u,               // round-down
            EXC_MASK_BITS | 0x4000u,               // round-up
            EXC_MASK_BITS | 0x6000u,               // round-toward-zero
            EXC_MASK_BITS | 0x8040u | 0x6000u,     // FTZ + DAZ + round-zero
            EXC_MASK_BITS | 0x8040u | 0x2000u,     // FTZ + DAZ + round-down
        };
        _mm_setcsr(dirtySet[trial]);

        // Sim tick entry — exactly what GameLogic::update does every frame.
        pinSimMXCSR();

        SimTickResult r = runSimTick(2685.83f, 4304.28f,
                                     2686.68f, 4303.94f,
                                     50.0f, 1.0f / 70.0f);

        // Must match golden bit-for-bit, or lockstep is broken.
        char label[64];
        std::snprintf(label, sizeof(label), "trial %d dirty=0x%04X", trial, dirtySet[trial]);
        CHECK_CRC_MATCH(
            /* packed golden */
            ((uint32_t&)golden.dirX)        ^ ((uint32_t&)golden.dirY) ^
            ((uint32_t&)golden.angleToGoal) ^ ((uint32_t&)golden.integratedX),
            /* packed result */
            ((uint32_t&)r.dirX)             ^ ((uint32_t&)r.dirY) ^
            ((uint32_t&)r.angleToGoal)      ^ ((uint32_t&)r.integratedX),
            label);

        // And full struct bitwise equality — stricter than the CRC XOR above
        // because XOR can mask bit flips that cancel across fields.
        CHECK(bitwiseEqual(r, golden));
    }


    // ------------------------------------------------------------------------
    Section("MXCSR is not disturbed by a typical SSE workload");
    //
    // If setFPMode is called once at tick entry, the rest of the tick must
    // not raise sticky exception flags into MXCSR in a way that affects
    // subsequent math. (The status flags can be set — that's expected — but
    // the *control* bits should remain at 0x1F80 values.)

    pinSimMXCSR();
    (void)runSimTick(2685.83f, 4304.28f, 2686.68f, 4303.94f, 50.0f, 1.0f / 70.0f);

    // Mask off the status flag bits; check the control bits are still clean.
    constexpr unsigned int MXCSR_CONTROL_MASK = 0xFFC0u;  // everything above bit 5
    CHECK_EQ(_mm_getcsr() & MXCSR_CONTROL_MASK, CLEAN_MXCSR & MXCSR_CONTROL_MASK);


    return FinalReport();
}
