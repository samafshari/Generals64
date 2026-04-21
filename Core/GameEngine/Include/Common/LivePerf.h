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

// Telemetry bridge: LivePerf emits ONE row per slot per frame to the SQLite
// telemetry subsystem (see Common/Telemetry.h). The emission happens from
// EndFrame(), not from the Scope dtor — per-sample writes flooded the ring
// buffer on high-frequency sites (PhysicsBehavior at 200k calls / session,
// W3DModelDraw at 400k) causing 40%+ drop rates and multi-GB DBs. The
// per-frame aggregate gives the same actionable signal (the HUD shows
// lastFrameMs too) at ~100× lower storage cost. When GENERALS_TELEMETRY=0
// Record becomes a no-op and this bridge contributes nothing.
#include "Common/Telemetry.h"

namespace LivePerf
{
    // Hard cap on the number of distinct named scopes the program can
    // register. Bumped from 64 → 256 because the dynamic typeid-based
    // per-module-class instrumentation registers one slot per unique
    // UpdateModule subclass (there are 60+ of them). Beyond the cap,
    // FindOrCreateSlot folds every subsequent registration into slot 0,
    // which silently pollutes the first-registered scope with orphan
    // ns. Per-EndFrame cost at 256 is still < 3 microsec.
    constexpr int MAX_SLOTS = 256;

    // Number of trailing one-minute buckets each slot keeps. Index 0 is the
    // minute currently being accumulated; indices 1..14 are the N-minutes-ago
    // TOTALS of wall-clock seconds spent inside that slot. Total-seconds (not
    // per-frame avg) is what surfaces drift: a 1 ms/call × 100 calls minute
    // reads as 0.10 s, and "gets worse over time" as steadily larger numbers.
    constexpr int MINUTE_BUCKETS = 15;

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

        // Rolling one-minute buckets. minuteSec[0] is the *running total* of
        // seconds spent inside this scope during the in-progress minute — so
        // it grows from 0 to ~60 over that minute. minuteSec[1] is the sealed
        // total for the minute that just ended, ..., minuteSec[14] is the
        // total from 14 minutes ago. Index 0 is updated every frame from the
        // per-frame accumNs; indices 1+ are stable snapshots shifted once per
        // wall-clock minute boundary by EndFrame().
        float       minuteSec[MINUTE_BUCKETS];
        long long   minuteAccumNs;   // ns accumulated in the current minute
        int         minuteFrames;    // frames counted toward the current minute
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
    // No per-sample telemetry emission — that work happens once per frame
    // in EndFrame(). Keeping the dtor as a simple accumulator preserves the
    // sub-100ns cost per scope that lets us leave LIVE_PERF_SCOPE on in
    // release builds.
    class Scope
    {
    public:
        explicit Scope(Slot* slot) : m_slot(slot), m_start(NowNs()) {}
        ~Scope()
        {
            const long long delta = NowNs() - m_start;
            m_slot->accumNs   += delta;
            m_slot->callCount += 1;
        }
    private:
        Slot*     m_slot;
        long long m_start;
    };

    // Wall-clock timestamp of the current minute-bucket's start. Used by
    // EndFrame() to decide when to shift the per-slot minute history. File-
    // scope static via function-local-static so there is one instance across
    // all TUs that include this header (same idiom as the slot table).
    inline long long& MinuteBucketStartNsRef()
    {
        static long long s_startNs = 0;
        return s_startNs;
    }

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

        // Decide whether this EndFrame crosses a one-minute wall-clock boundary.
        // The minute counter is driven off steady_clock so it's unaffected by
        // game pause/unpause — pausing the game doesn't rewind the buckets.
        constexpr long long kOneMinuteNs = 60LL * 1000LL * 1000LL * 1000LL;
        long long& bucketStartNs = MinuteBucketStartNsRef();
        const long long nowNs = NowNs();
        if (bucketStartNs == 0)
            bucketStartNs = nowNs;
        const bool rollBucket = (nowNs - bucketStartNs) >= kOneMinuteNs;
        if (rollBucket)
            bucketStartNs = nowNs;

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

            // Accumulate toward the in-progress minute bucket as a running
            // TOTAL. This is the sum of wall-clock seconds spent in this scope
            // across all frames of the current minute — not an average. A
            // scope running 1 ms per call × 100 calls in a minute reads as
            // 0.100 s; a 57 ms/frame scope at 70 Hz reads as ~4.0 s.
            s.minuteAccumNs += s.accumNs;
            s.minuteFrames  += 1;
            // Convert ns → s once per frame so the live row displays the
            // partial total as it grows through the minute.
            s.minuteSec[0] = (float)((double)s.minuteAccumNs * 1e-9);

            if (rollBucket)
            {
                // Shift: minuteSec[1] becomes what was minuteSec[0] (the total
                // for the minute we just finished). Indices 2..14 slide right;
                // the oldest drops off the end.
                for (int j = MINUTE_BUCKETS - 1; j >= 2; --j)
                    s.minuteSec[j] = s.minuteSec[j - 1];
                s.minuteSec[1]  = s.minuteSec[0];
                s.minuteSec[0]  = 0.0f;
                s.minuteAccumNs = 0;
                s.minuteFrames  = 0;
            }

            // Bridge to telemetry: one row per slot per frame that had calls.
            // Row semantics are "total cost of N invocations within frame X"
            // — p50/p95/p99 then become per-frame percentiles, matching how
            // the HUD itself summarizes each slot (lastFrameMs). Dormant
            // slots are skipped so the DB isn't padded with zero rows.
            if (s.callCount > 0)
            {
                ::Telemetry::Record("perf", s.name, s.accumNs);
            }

            s.accumNs   = 0;
            s.callCount = 0;
        }
    }

    // Reset the running peak/min-ms columns + variance. Bound to a button in the
    // panel so the user can clear stats after a perf experiment. Also wipes the
    // 15-minute history so a "drift over time" test can be run repeatedly.
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
            for (int j = 0; j < MINUTE_BUCKETS; ++j)
                slots[i].minuteSec[j] = 0.0f;
            slots[i].minuteAccumNs = 0;
            slots[i].minuteFrames  = 0;
        }
        MinuteBucketStartNsRef() = 0;
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

// Dynamic-name variant: for dispatch sites where the scope name is chosen
// at runtime (e.g. iterating a heterogeneous list of polymorphic modules
// and grouping timings by module class). The name pointer MUST have
// program-lifetime storage — typeid(x).name() and interned string-table
// pointers qualify; stack strings do NOT. Cost per call is a string
// compare walk over the slot table, so use sparingly (not in 10k-calls-
// per-frame hot paths).
#define LIVE_PERF_SCOPE_DYNAMIC(namePtr)                                   \
    ::LivePerf::Scope LIVE_PERF_CONCAT(_lpScopeDyn_, __LINE__)(            \
        ::LivePerf::FindOrCreateSlot(namePtr))
