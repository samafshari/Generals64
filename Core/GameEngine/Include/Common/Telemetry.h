// Telemetry.h /////////////////////////////////////////////////////////////
//
// SQLite-backed perf telemetry subsystem. Records per-scope timing events
// to a sessioned SQLite database so bottlenecks can be mined offline with
// p50 / p95 / p99 / mean / stddev / count queries.
//
// Relationship to LivePerf ------------------------------------------------
//
// LivePerf (Common/LivePerf.h) is the in-process, single-threaded "HUD"
// perf layer — it maintains EWMA averages in a 64-slot array read by the
// Inspector's F11 Perf HUD panel. It stays live.
//
// Telemetry is additive: it records every individual sample (not just
// per-frame aggregates) to persistent storage so we can answer questions
// like "what was the 99th-percentile cost of W3DDisplay::draw across the
// whole replay?". Durations are pushed into a ring buffer from the game
// thread; a dedicated writer thread batches them into SQLite transactions
// so the simulation never blocks on disk I/O.
//
// If the DB fails to open, recording is disabled and a single DEBUG_LOG
// line is emitted — nothing crashes and gameplay is unaffected.
//
// Master switch -----------------------------------------------------------
//
// Define GENERALS_TELEMETRY=0 at the top of this header (or via a
// compiler flag) to compile the macros to nothing. When disabled, the
// scopes expand to an empty statement and the writer thread is never
// started — truly zero cost.
//
// Usage -------------------------------------------------------------------
//
//     #include "Common/Telemetry.h"
//
//     void Draw()
//     {
//         TELEMETRY_SCOPE("Draw", "W3DTerrain");
//         ...
//     }
//
// Instrumentation convention: first arg is a broad category ("Draw",
// "Update", "AI", "Pathfind"); second arg is a specific site name that
// groups all samples from this call site together for stats. Both should
// be string literals (we take their raw pointer for fast dedup).
//
// Query API ---------------------------------------------------------------
//
// Telemetry::QueryBottlenecks(N) returns the top-N (category,name)
// combinations ranked by p99*count, suitable for a debug-console dump.
// See tools/telemetry_report.sql for richer offline queries.
//
////////////////////////////////////////////////////////////////////////////

#pragma once

// --- Master compile switch ----------------------------------------------
// Default ON. Override with /D GENERALS_TELEMETRY=0 to compile it out
// everywhere (all TELEMETRY_SCOPE calls become no-ops, no DB, no thread).
#ifndef GENERALS_TELEMETRY
#define GENERALS_TELEMETRY 1
#endif

#include <chrono>
#include <cstdint>

namespace Telemetry
{

#if GENERALS_TELEMETRY

    // --- Lifecycle --------------------------------------------------------
    // Called once from GameEngine::init() / main after TheFileSystem exists
    // (so we can resolve GameData/). Opens telemetry.sqlite, creates the
    // schema if missing, stamps a new session row, and launches the writer
    // thread. Safe to call multiple times — subsequent calls no-op. Safe to
    // fail: if open fails, all subsequent Record() calls short-circuit.
    void Init();

    // Flush any pending ring buffer entries and stop the writer thread.
    // Called from GameEngine::shutdown(). Blocks briefly while the last
    // batch commits. Safe to call even if Init() failed.
    void Shutdown();

    // Advance the frame counter used to stamp events. Hooked from the same
    // site that already calls LivePerf::EndFrame() (GameEngine::update)
    // right after input/render so the per-frame bucket is coherent.
    void OnFrameBoundary();

    // --- Event recording --------------------------------------------------
    // Push a single scope timing into the ring buffer. Called from the
    // Scope dtor — do not call directly, use TELEMETRY_SCOPE. Both strings
    // must have static (program-lifetime) storage; we store raw pointers
    // and only copy at DB insert time.
    void Record(const char* category, const char* name, int64_t durationNs);

    // Is the subsystem live? Mostly used by the Scope type to skip the
    // chrono call and ring-buffer push when Init() failed or was never
    // invoked. Atomic-load underneath.
    bool IsEnabled();

    // --- Scope RAII -------------------------------------------------------
    // Times the enclosing scope and emits to the ring buffer on dtor. When
    // telemetry is disabled at runtime (Init failed or not yet called),
    // the dtor early-outs before the chrono::now() so cost is ~0 ns past
    // the one atomic load.
    class Scope
    {
    public:
        Scope(const char* category, const char* name)
            : m_category(category), m_name(name), m_startNs(nowNs()) {}
        ~Scope()
        {
            if (!IsEnabled()) return;
            const int64_t elapsed = nowNs() - m_startNs;
            Record(m_category, m_name, elapsed);
        }
    private:
        static int64_t nowNs()
        {
            using clock = std::chrono::steady_clock;
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                clock::now().time_since_epoch()).count();
        }
        const char* m_category;
        const char* m_name;
        int64_t     m_startNs;
    };

    // --- Query API (live; runs against the active session) ---------------
    struct Stat
    {
        const char* category;
        const char* name;
        int64_t     count;
        double      meanUs;
        double      stddevUs;
        double      p50Us;
        double      p95Us;
        double      p99Us;
        double      maxUs;
    };

    // Return the top-N (category,name) pairs ranked by p99*count across
    // the *current session*. Results point into a caller-borrowed vector
    // passed by reference so we don't bake STL into the header's ABI.
    // Returns the number actually written. May run a synchronous SQL query
    // on the calling thread; use sparingly (debug commands, not every frame).
    int QueryBottlenecks(Stat* out, int maxCount, int topN);

    // Dump a formatted text table of QueryBottlenecks to DEBUG_LOG. Handy
    // single-liner for a console binding.
    void DumpBottlenecks(int topN);

#endif // GENERALS_TELEMETRY

} // namespace Telemetry


// --- Macros --------------------------------------------------------------
// When GENERALS_TELEMETRY == 0 the scope expands to nothing and
// Init()/Shutdown()/OnFrameBoundary() become empty inline shims.
#if GENERALS_TELEMETRY

#define TELEMETRY_CONCAT_(a, b) a##b
#define TELEMETRY_CONCAT(a, b)  TELEMETRY_CONCAT_(a, b)
#define TELEMETRY_SCOPE(category, name)                                     \
    ::Telemetry::Scope TELEMETRY_CONCAT(_tlmScope_, __LINE__)((category), (name))

#else

#define TELEMETRY_SCOPE(category, name) ((void)0)

namespace Telemetry {
    inline void Init() {}
    inline void Shutdown() {}
    inline void OnFrameBoundary() {}
    inline void Record(const char*, const char*, int64_t) {}
    inline bool IsEnabled() { return false; }
    inline void DumpBottlenecks(int) {}
}

#endif // GENERALS_TELEMETRY
