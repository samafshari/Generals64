// LivePerf.h //////////////////////////////////////////////////////////////
//
// Lightweight, always-on, single-threaded perf scope counters for live
// in-game profiling via the Inspector's Perf HUD panel.
//
// Why this exists ---------------------------------------------------------
//
// The legacy Common/PerfTimer.h system (PerfGather + DECLARE_PERF_TIMER /
// USE_PERF_TIMER) is gated on a PERF_TIMERS macro that is never defined
// in this build configuration. PerfTimer.h:32-40 explicitly #defines
// NO_PERF_TIMERS in BOTH the RTS_DEBUG and release branches with a
// comment from the original devs warning that PERF_TIMERS "should never
// be checked in enabled" because of its non-trivial cost. As a result,
// every USE_PERF_TIMER(...) and DECLARE_PERF_TIMER(...) macro call in
// the engine compiles to nothing today, and the legacy PerfGather system
// is unusable for live profiling.
//
// LivePerf is a tiny replacement designed specifically to leave running
// in release builds. Each scope is one std::chrono::steady_clock::now()
// pair (~50-100 ns on modern x86) accumulated into a fixed-size global
// table indexed by call site. The Inspector's "Perf HUD" panel reads
// the table once per frame and displays per-counter ms, call counts,
// EWMA averages, and peaks.
//
// Threading model ---------------------------------------------------------
//
// Single-threaded. The game logic, GameClient, and Inspector all run on
// the main thread, so no atomics or locks are used. Adding a LIVE_PERF
// scope from a worker thread would corrupt the table — don't do it.
//
// Storage model -----------------------------------------------------------
//
// All state lives in static locals inside inline functions, which the
// C++11 standard guarantees are unique per program even across multiple
// translation units that include this header. This avoids needing a
// dedicated .cpp file (and avoids touching the .vcxproj to add one).
// The Inspector module compiles into the same exe as the engine
// (z_generals.vcxproj:1066,1070), so the same array is visible to both.
//
// Usage -------------------------------------------------------------------
//
//     #include "Common/LivePerf.h"
//
//     void Foo()
//     {
//         LIVE_PERF_SCOPE("Foo");
//         ...work...
//     }
//
// LivePerf::EndFrame() must be called once per main-loop iteration so
// the per-frame accumulators roll into the displayed history. It's
// hooked from GameEngine::update().
//
////////////////////////////////////////////////////////////////////////////

#pragma once

#include <chrono>
#include <cmath>
#include <cstring>

// Telemetry bridge: every LIVE_PERF_SCOPE also emits a per-sample row to
// the SQLite telemetry subsystem (see Common/Telemetry.h). The Scope dtor
// calls Telemetry::Record() with category "perf" and name = the scope
// label. When GENERALS_TELEMETRY=0 this include contributes nothing
// (Record becomes an inline no-op). This gives us p50/p95/p99 stats on
// every existing LIVE_PERF_SCOPE site for free, in addition to the HUD
// aggregates LivePerf maintains in-process.
#include "Common/Telemetry.h"

namespace LivePerf
{
    // Hard cap on the number of distinct named scopes the program can
    // register. 64 is plenty for the AI/render profiling we care about
    // and keeps the per-EndFrame cost negligible. Bump if you need more.
    constexpr int MAX_SLOTS = 64;

    struct Slot
    {
        const char* name;          // first-seen string-literal pointer
        long long   accumNs;       // ns accumulated this frame (zeroed by EndFrame)
        int         callCount;     // calls this frame (zeroed by EndFrame)
        float       lastFrameMs;   // last completed frame's total ms
        int         lastCallCount; // last completed frame's call count
        float       avgMs;         // EWMA average across recent frames
        float       peakMs;        // max lastFrameMs since last ResetPeaks()
        float       varEwma;       // EWMA of squared deviation from avgMs (running variance)
        float       stdDevMs;      // sqrt(varEwma) — surfaces which scopes have spiky timings vs steady
        float       minMs;         // min lastFrameMs since last ResetPeaks() (floor; complements peak)
    };

    // Single-source-of-truth storage. Static-local-in-inline-function
    // pattern: C++11 guarantees one instance program-wide regardless of
    // how many translation units include this header.
    inline Slot* GetSlotsArray()
    {
        static Slot s_slots[MAX_SLOTS] = {};
        return s_slots;
    }
    inline int& GetSlotCountRef()
    {
        static int s_count = 0;
        return s_count;
    }

    // Find an existing slot by name, or create one. Called once per
    // call site over the lifetime of the program — the LIVE_PERF_SCOPE
    // macro caches the resulting pointer in a static local so subsequent
    // calls from the same line never re-walk this list.
    inline Slot* FindOrCreateSlot(const char* name)
    {
        Slot* slots = GetSlotsArray();
        int& count = GetSlotCountRef();

        // Pointer-equal fast path: within a single translation unit the
        // compiler merges identical string literals to one address, so
        // repeated calls from the same source location hit immediately.
        for (int i = 0; i < count; ++i)
            if (slots[i].name == name) return &slots[i];

        // Cross-TU merge: the same scope name used in two different .cpp
        // files would otherwise create two slots with identical labels
        // and split counts. Compare strings to merge them. Only runs on
        // the very first call from each call site (the macro caches the
        // result), so amortized cost is zero.
        for (int i = 0; i < count; ++i)
            if (std::strcmp(slots[i].name, name) == 0) return &slots[i];

        // New slot. If we ran out, fold the overflow into slot 0 so we
        // don't crash and the user notices "slot 0 is huge" in the panel.
        if (count >= MAX_SLOTS)
            return &slots[0];

        Slot& s = slots[count++];
        s.name = name;
        return &s;
    }

    // Wall-clock nanoseconds. steady_clock so the values are monotonic
    // and immune to wall-clock jumps (NTP, DST, suspend/resume).
    inline long long NowNs()
    {
        using clock = std::chrono::steady_clock;
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            clock::now().time_since_epoch()).count();
    }

    // RAII timer. Accumulates elapsed ns into the slot in the dtor.
    class Scope
    {
    public:
        explicit Scope(Slot* slot) : m_slot(slot), m_start(NowNs()) {}
        ~Scope()
        {
            const long long delta = NowNs() - m_start;
            m_slot->accumNs   += delta;
            m_slot->callCount += 1;
            // Bridge: also record into SQLite telemetry. Category is fixed
            // to "perf" so a single predicate selects all LivePerf samples;
            // the scope label lives in name. Telemetry::Record short-circuits
            // on a single atomic load when the subsystem is disabled.
            ::Telemetry::Record("perf", m_slot->name, (int64_t)delta);
        }
    private:
        Slot*     m_slot;
        long long m_start;
    };

    // Roll the per-frame accumulators into displayed history. Hooked
    // from GameEngine::update() once per main-loop iteration. Cost is
    // ~64 multiplications + ~64 EWMA updates per call (~1-2 microsec).
    inline void EndFrame()
    {
        Slot* slots = GetSlotsArray();
        const int count = GetSlotCountRef();

        // EWMA smoothing factor. 0.10 means a single noisy frame moves
        // the average by 10%; the average converges to a steady-state
        // change in ~30 frames (~1 second of game time at 30 Hz logic).
        constexpr float kEwmaAlpha = 0.10f;

        for (int i = 0; i < count; ++i)
        {
            Slot& s = slots[i];
            const float ms = (float)((double)s.accumNs * 1e-6);

            // Compute deviation against the *previous* avg so variance reflects
            // how far this frame's cost deviated from the running expectation,
            // not from the value it just biased. This is the standard EWMVar form.
            const float diff = ms - s.avgMs;
            s.varEwma = (1.0f - kEwmaAlpha) * s.varEwma + kEwmaAlpha * (diff * diff);
            s.stdDevMs = std::sqrt(s.varEwma);

            s.lastFrameMs   = ms;
            s.lastCallCount = s.callCount;
            s.avgMs         = (1.0f - kEwmaAlpha) * s.avgMs + kEwmaAlpha * ms;
            if (ms > s.peakMs) s.peakMs = ms;
            // Track floor too. First-sample fence via minMs==0 sentinel, which
            // is fine since a scope that truly took 0 ns won't have a useful min.
            if (s.minMs == 0.0f || ms < s.minMs) s.minMs = ms;
            s.accumNs   = 0;
            s.callCount = 0;
        }
    }

    // Reset the running peak/min-ms columns + variance. Bound to a button in the
    // panel so the user can clear stats after a perf experiment.
    inline void ResetPeaks()
    {
        Slot* slots = GetSlotsArray();
        const int count = GetSlotCountRef();
        for (int i = 0; i < count; ++i)
        {
            slots[i].peakMs = 0.0f;
            slots[i].minMs = 0.0f;
            slots[i].varEwma = 0.0f;
            slots[i].stdDevMs = 0.0f;
        }
    }

    // Read-only accessors used by the Inspector's Perf HUD panel.
    inline int          GetSlotCount() { return GetSlotCountRef(); }
    inline const Slot*  GetSlots()     { return GetSlotsArray(); }

} // namespace LivePerf

// Per-call-site macro: the static local caches the slot pointer so
// the FindOrCreateSlot walk only runs on the very first invocation
// from this source line. After that, the only per-call cost is the
// Scope ctor/dtor (one chrono::now() pair).
#define LIVE_PERF_CONCAT_(a, b) a##b
#define LIVE_PERF_CONCAT(a, b)  LIVE_PERF_CONCAT_(a, b)
#define LIVE_PERF_SCOPE(name)                                              \
    static ::LivePerf::Slot* LIVE_PERF_CONCAT(_lpSlot_, __LINE__) =        \
        ::LivePerf::FindOrCreateSlot(name);                                \
    ::LivePerf::Scope LIVE_PERF_CONCAT(_lpScope_, __LINE__)(               \
        LIVE_PERF_CONCAT(_lpSlot_, __LINE__))
