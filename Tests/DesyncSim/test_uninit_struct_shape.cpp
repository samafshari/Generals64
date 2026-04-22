// test_uninit_struct_shape.cpp
//
// Proves that adding in-class member initialisers to a sim struct eliminates
// the "uninitialised float accumulator" class of desync bug. Models the exact
// shape of ChinookCombatDropState::RopeInfo before/after the fix.
//
// WHY THIS TEST EXISTS
// --------------------
// The audit turned up RopeInfo declared as a plain aggregate:
//
//     struct RopeInfo {
//         Drawable* ropeDrawable;
//         Real      ropeSpeed;
//         Real      ropeLen;
//         Real      ropeLenMax;
//         ...
//     };
//
// onEnter() allocated `RopeInfo info;` on the stack, then only initialised
// ropeSpeed/ropeLen/ropeLenMax inside an `if (info.ropeDrawable)` guard. When
// the rope drawable failed to create, those floats stayed as stack garbage —
// and stack garbage is process-specific (different ASLR, different allocator
// state, different prior stack use). Next frame's update() read them back and
// did `it->ropeSpeed += fabs(gravity)`, producing a float accumulator that
// diverged between peers from frame 1.
//
// The fix is in-class default initialisers (C++11), which value-initialise
// every RopeInfo instantiation regardless of surrounding control flow.
//
// This test models the bug and fix in a minimal form, and asserts:
//   (a) without in-class defaults, many allocations of the same struct on
//       differently-dirtied stacks produce at least some non-zero garbage
//       floats — i.e. the bug shape exists and is detectable;
//   (b) with in-class defaults, the same pattern always yields zeros — i.e.
//       the fix deterministically initialises every field.
//
// Self-contained. Build:
//   cl /std:c++20 /EHsc /nologo test_uninit_struct_shape.cpp ...

#include "TestFramework.h"

#include <cstdint>
#include <cstdio>
#include <cstring>


// -----------------------------------------------------------------------------
// Shape BEFORE the fix — plain aggregate, no in-class defaults.
// -----------------------------------------------------------------------------
struct RopeInfoOldShape
{
    void*       ropeDrawable;     // pointer (we don't dereference it)
    float       ropeSpeed;        // Real in the engine
    float       ropeLen;
    float       ropeLenMax;
    unsigned    nextDropTime;
};


// -----------------------------------------------------------------------------
// Shape AFTER the fix — in-class initialisers on every field.
// -----------------------------------------------------------------------------
struct RopeInfoNewShape
{
    void*       ropeDrawable   = nullptr;
    float       ropeSpeed      = 0.0f;
    float       ropeLen        = 0.0f;
    float       ropeLenMax     = 0.0f;
    unsigned    nextDropTime   = 0u;
};


// -----------------------------------------------------------------------------
// Simulate the bug: dirty a chunk of stack with known garbage, then allocate
// `RopeInfoOldShape info;` on that same dirtied stack frame. Because the
// compiler does not zero aggregate floats, `info` inherits whatever was
// written there a moment ago.
//
// The function is marked noinline via a volatile trampoline so the optimiser
// doesn't elide the dirtying pass — MSVC is aggressive about SROA'ing these.
// -----------------------------------------------------------------------------
__declspec(noinline)
static void dirtyStack(volatile unsigned* sink, unsigned pattern)
{
    // A block of stack bytes we explicitly stamp with a dirty pattern.
    // Size must be at least as large as RopeInfoOldShape so the subsequent
    // allocation overlaps it.
    volatile unsigned garbage[16];
    for (int i = 0; i < 16; ++i)
        garbage[i] = pattern ^ (unsigned)i * 0x9E3779B9u;

    // Force the writes to retire.
    *sink = garbage[0];
}

__declspec(noinline)
static RopeInfoOldShape allocOldAfterDirty(unsigned pattern)
{
    volatile unsigned sink = 0;
    dirtyStack(&sink, pattern);

    // Reserve stack space with the same layout as the dirtying function so
    // the new RopeInfoOldShape overlaps the garbage. MSVC doesn't guarantee
    // this but in practice at the same optimisation level it does — that's
    // enough to demonstrate the shape.
    RopeInfoOldShape info;

    // Touch sink so the compiler doesn't DCE the dirtying.
    (void)sink;
    return info;
}

__declspec(noinline)
static RopeInfoNewShape allocNewAfterDirty(unsigned pattern)
{
    volatile unsigned sink = 0;
    dirtyStack(&sink, pattern);

    RopeInfoNewShape info;   // in-class defaults run here
    (void)sink;
    return info;
}


// -----------------------------------------------------------------------------
// Reinterpret a float as its raw uint32 bit pattern for exact comparison.
// -----------------------------------------------------------------------------
static uint32_t bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}


int main()
{
    // -------------------------------------------------------------------------
    Section("Pre-fix shape: stack garbage bleeds into uninitialised floats");
    //
    // We don't require a specific garbage value — just that across a bunch of
    // allocations with different dirty patterns, AT LEAST ONE old-shape
    // instance picks up non-zero garbage in a float field. That's the bug
    // shape: the value is non-zero, non-reproducible, and process-specific.
    //
    // If this check ever stops firing it means the compiler is unexpectedly
    // zeroing stack storage (unlikely; or /GS / Spectre mitigations kicking
    // in) and the test's evidence is weakened, but the fix remains valid.

    bool sawGarbage = false;
    for (unsigned trial = 0; trial < 32 && !sawGarbage; ++trial)
    {
        RopeInfoOldShape info = allocOldAfterDirty(0xDEADBEEF ^ trial);
        if (bits(info.ropeSpeed)  != 0 ||
            bits(info.ropeLen)    != 0 ||
            bits(info.ropeLenMax) != 0)
        {
            std::printf("  trial %u: ropeSpeed=0x%08X ropeLen=0x%08X ropeLenMax=0x%08X (garbage observed)\n",
                        trial, bits(info.ropeSpeed), bits(info.ropeLen), bits(info.ropeLenMax));
            sawGarbage = true;
        }
    }
    CHECK(sawGarbage);


    // -------------------------------------------------------------------------
    Section("Post-fix shape: in-class defaults always zero-init the floats");
    //
    // Same pattern, but with the fixed struct. Every single allocation must
    // come back with zeros — that's what in-class `= 0.0f` guarantees.

    for (unsigned trial = 0; trial < 32; ++trial)
    {
        RopeInfoNewShape info = allocNewAfterDirty(0xDEADBEEF ^ trial);

        CHECK_EQ(bits(info.ropeSpeed),    0u);
        CHECK_EQ(bits(info.ropeLen),      0u);
        CHECK_EQ(bits(info.ropeLenMax),   0u);
    }


    // -------------------------------------------------------------------------
    Section("Post-fix shape: pointer and integer fields are also deterministic");
    //
    // The RopeInfo fix covered non-float fields too, so this sweep covers
    // those — they matter for lockstep equality via XferCRC which hashes
    // every field's bytes.

    for (unsigned trial = 0; trial < 8; ++trial)
    {
        RopeInfoNewShape info = allocNewAfterDirty(0xCAFEF00D ^ trial);
        CHECK(info.ropeDrawable == nullptr);
        CHECK_EQ(info.nextDropTime, 0u);
    }


    return FinalReport();
}
