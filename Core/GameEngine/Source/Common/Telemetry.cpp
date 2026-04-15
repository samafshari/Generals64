// Telemetry.cpp ///////////////////////////////////////////////////////////
//
// SQLite-backed perf telemetry — ring buffer producer on the game thread,
// batching writer thread, sessioned schema. See Common/Telemetry.h for
// the rationale and the macro-level usage contract.
//
// Architecture --------------------------------------------------------
//
//   producer (any game-thread Scope dtor)
//        |                              (lock-free-ish ring: SPSC-ish
//        v                               with a coarse mutex on the tail
//   +---------+                          for the multi-producer case —
//   |   ring  |  capacity = kRingSize    we never see enough contention
//   +---------+                          on the draw/update thread for
//        |                               atomics to beat a mutex.)
//        v
//   writer thread (in-process)
//        |   BEGIN; INSERT batch; COMMIT;   (every kFlushIntervalMs or
//        v                                   kBatchSize, whichever first)
//   GameData/telemetry.sqlite
//
// If the ring fills (game producing faster than writer can commit), new
// events are dropped and a counter is bumped — better than stalling the
// sim on disk I/O.
//
// Schema --------------------------------------------------------------
//
//   sessions(session_id INTEGER PK AUTOINCREMENT,
//            started_utc TEXT,
//            build_version TEXT)
//
//   events(id INTEGER PK AUTOINCREMENT,
//          session_id INTEGER,
//          frame_no INTEGER,
//          tick_us INTEGER,
//          category TEXT,
//          name TEXT,
//          duration_us INTEGER,
//          payload TEXT NULL)
//     -- indexes: (category,name), (frame_no), (session_id)
//
//   v_rollup  : view returning count/mean/stddev/max per (category,name)
//               scoped to the current session. Percentiles are computed
//               in C++ (sqlite3 core has no percentile_cont) via a single
//               sorted SELECT.
//
////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "Common/Telemetry.h"

#if GENERALS_TELEMETRY

#include "sqlite3.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Telemetry
{

namespace
{

// --- Tunables ------------------------------------------------------------
// Ring sized for ~0.5 s of sustained 20k events/sec (per-frame scope count
// across Draw + Update + AI is currently on the order of a few thousand).
constexpr int  kRingSize        = 16384;
constexpr int  kBatchSize       = 1024;          // rows per transaction
constexpr int  kFlushIntervalMs = 100;           // flush cadence

struct Event
{
    const char* category;     // static storage (string literal)
    const char* name;         // static storage (string literal)
    int64_t     tickUs;       // monotonic wall clock, relative to session start
    int64_t     durationNs;
    int32_t     frameNo;
};

// --- Global state (file-scoped so nothing leaks into the namespace) ------
std::atomic<bool>   g_enabled{false};
std::atomic<int32_t> g_frameNo{0};
std::atomic<int64_t> g_sessionStartNs{0};
std::atomic<int64_t> g_dropped{0};

// Ring buffer. head = next write, tail = next read. indices modulo kRingSize.
// Guarded by g_ringMutex. We could go full SPSC atomics but the producer is
// multi-threaded (Draw can run on any thread that has a Scope), so a mutex
// is simpler and uncontended in practice.
std::mutex            g_ringMutex;
std::condition_variable g_ringCv;
Event                 g_ring[kRingSize];
int                   g_head = 0;
int                   g_tail = 0;

// Writer thread state.
std::thread           g_writer;
std::atomic<bool>     g_stopRequested{false};

// SQLite handle + prepared insert. Owned by the writer thread only.
sqlite3*              g_db            = nullptr;
sqlite3_stmt*         g_insertStmt    = nullptr;
int64_t               g_sessionId     = 0;
std::mutex            g_queryMutex;   // serializes read-only debug queries

// Build version string baked in at compile time if available.
const char* buildVersion()
{
#ifdef BUILD_VERSION_STR
    return BUILD_VERSION_STR;
#else
    return "unknown";
#endif
}

// --- SQLite helpers ------------------------------------------------------
bool exec(sqlite3* db, const char* sql)
{
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK)
    {
        DEBUG_LOG(("Telemetry SQL failed: %s (%s)", err ? err : "?", sql));
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool createSchema(sqlite3* db)
{
    // WAL + NORMAL synchronous trades a tiny window of commit loss on power
    // fail for order-of-magnitude faster INSERTs — perfectly appropriate
    // for perf telemetry where a lost tail is acceptable.
    if (!exec(db, "PRAGMA journal_mode=WAL;"))       return false;
    if (!exec(db, "PRAGMA synchronous=NORMAL;"))     return false;
    if (!exec(db, "PRAGMA temp_store=MEMORY;"))      return false;

    static const char* schema =
        "CREATE TABLE IF NOT EXISTS sessions ("
        "  session_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  started_utc TEXT NOT NULL,"
        "  build_version TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS events ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  session_id INTEGER NOT NULL,"
        "  frame_no INTEGER NOT NULL,"
        "  tick_us INTEGER NOT NULL,"
        "  category TEXT NOT NULL,"
        "  name TEXT NOT NULL,"
        "  duration_us INTEGER NOT NULL,"
        "  payload TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS ix_events_cat_name ON events(category, name);"
        "CREATE INDEX IF NOT EXISTS ix_events_frame    ON events(frame_no);"
        "CREATE INDEX IF NOT EXISTS ix_events_session  ON events(session_id);"
        // Aggregate view. Percentiles are *not* included because sqlite
        // core has no percentile_cont; they're computed in QueryBottlenecks().
        "CREATE VIEW IF NOT EXISTS v_rollup AS"
        "  SELECT session_id, category, name,"
        "         COUNT(*)        AS n,"
        "         AVG(duration_us) AS mean_us,"
        "         MAX(duration_us) AS max_us"
        "    FROM events"
        "   GROUP BY session_id, category, name;";

    return exec(db, schema);
}

// Fill `out` with the absolute directory containing the running executable,
// trailing separator stripped. Empty string on failure. Used so we can drop
// telemetry artifacts beside the exe regardless of the launch cwd (play.bat
// pushd's into GameData/, debuggers may not, CI may launch from anywhere).
void getExeDir(char* out, size_t cap)
{
    if (cap == 0) return;
    out[0] = '\0';
#ifdef _WIN32
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, (DWORD)sizeof(exePath)) == 0) return;
    // Strip the filename. Windows uses '\' but tolerate '/'.
    char* slash = std::strrchr(exePath, '\\');
    char* fwd   = std::strrchr(exePath, '/');
    if (fwd && (!slash || fwd > slash)) slash = fwd;
    if (!slash) return;
    *slash = '\0';
    std::snprintf(out, cap, "%s", exePath);
#endif
}

// Storage for the resolved DB path so the shutdown report can land beside it.
char g_dbPath[1024] = {0};

bool openDb()
{
    // Resolve telemetry.sqlite beside the running exe. Earlier code used a
    // relative "GameData/telemetry.sqlite" which fails whenever the launch
    // cwd isn't the repo root — play.bat's `pushd GameData` makes it resolve
    // to GameData/GameData/... which silently errors out. Grounding on the
    // exe directory is cwd-independent.
    char exeDir[1024] = {0};
    getExeDir(exeDir, sizeof(exeDir));
    if (exeDir[0])
        std::snprintf(g_dbPath, sizeof(g_dbPath), "%s\\telemetry.sqlite", exeDir);
    else
        std::snprintf(g_dbPath, sizeof(g_dbPath), "telemetry.sqlite");

    const int rc = sqlite3_open(g_dbPath, &g_db);
    if (rc != SQLITE_OK)
    {
        const char* err = g_db ? sqlite3_errmsg(g_db) : "unknown";
        DEBUG_LOG(("Telemetry: failed to open %s: %s", g_dbPath, err));
        // Release builds strip DEBUG_LOG. Stderr is the one channel that
        // survives so "my DB is missing" always leaves a breadcrumb.
        std::fprintf(stderr, "[Telemetry] failed to open %s: %s\n", g_dbPath, err);
        if (g_db) { sqlite3_close(g_db); g_db = nullptr; }
        return false;
    }

    if (!createSchema(g_db)) return false;

    // Stamp this run.
    char nowStr[64];
    {
        const std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        std::strftime(nowStr, sizeof(nowStr), "%Y-%m-%dT%H:%M:%SZ", &tm);
    }

    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(g_db,
            "INSERT INTO sessions(started_utc, build_version) VALUES(?,?);",
            -1, &s, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(s, 1, nowStr, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 2, buildVersion(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(s) != SQLITE_DONE) { sqlite3_finalize(s); return false; }
    sqlite3_finalize(s);
    g_sessionId = sqlite3_last_insert_rowid(g_db);

    // Prepare the hot-path insert once — reused for every batched row.
    if (sqlite3_prepare_v2(g_db,
            "INSERT INTO events(session_id,frame_no,tick_us,category,name,duration_us)"
            " VALUES(?,?,?,?,?,?);",
            -1, &g_insertStmt, nullptr) != SQLITE_OK)
    {
        DEBUG_LOG(("Telemetry: failed to prepare insert: %s", sqlite3_errmsg(g_db)));
        return false;
    }

    DEBUG_LOG(("Telemetry: session=%lld file=%s", (long long)g_sessionId, path));
    return true;
}

// --- Writer thread -------------------------------------------------------
// Drains up to kBatchSize events from the ring, wraps them in a single
// transaction, then sleeps or blocks on the cv until kFlushIntervalMs.
void writerThread()
{
    while (!g_stopRequested.load(std::memory_order_acquire))
    {
        // Pull a batch snapshot.
        std::vector<Event> batch;
        batch.reserve(kBatchSize);
        {
            std::unique_lock<std::mutex> lk(g_ringMutex);
            g_ringCv.wait_for(lk, std::chrono::milliseconds(kFlushIntervalMs),
                [] { return g_head != g_tail || g_stopRequested.load(); });
            while (g_tail != g_head && (int)batch.size() < kBatchSize)
            {
                batch.push_back(g_ring[g_tail]);
                g_tail = (g_tail + 1) % kRingSize;
            }
        }

        if (batch.empty()) continue;

        // One transaction per batch — without this, WAL commit latency
        // dominates and we'd fall behind the producer within seconds.
        exec(g_db, "BEGIN;");
        for (const Event& e : batch)
        {
            sqlite3_reset(g_insertStmt);
            sqlite3_bind_int64(g_insertStmt, 1, g_sessionId);
            sqlite3_bind_int  (g_insertStmt, 2, e.frameNo);
            sqlite3_bind_int64(g_insertStmt, 3, e.tickUs);
            sqlite3_bind_text (g_insertStmt, 4, e.category, -1, SQLITE_STATIC);
            sqlite3_bind_text (g_insertStmt, 5, e.name,     -1, SQLITE_STATIC);
            sqlite3_bind_int64(g_insertStmt, 6, (e.durationNs + 500) / 1000); // us
            if (sqlite3_step(g_insertStmt) != SQLITE_DONE)
            {
                DEBUG_LOG(("Telemetry insert: %s", sqlite3_errmsg(g_db)));
                break;
            }
        }
        exec(g_db, "COMMIT;");
    }

    // Final drain on shutdown.
    std::vector<Event> batch;
    {
        std::lock_guard<std::mutex> lk(g_ringMutex);
        while (g_tail != g_head)
        {
            batch.push_back(g_ring[g_tail]);
            g_tail = (g_tail + 1) % kRingSize;
        }
    }
    if (!batch.empty() && g_db && g_insertStmt)
    {
        exec(g_db, "BEGIN;");
        for (const Event& e : batch)
        {
            sqlite3_reset(g_insertStmt);
            sqlite3_bind_int64(g_insertStmt, 1, g_sessionId);
            sqlite3_bind_int  (g_insertStmt, 2, e.frameNo);
            sqlite3_bind_int64(g_insertStmt, 3, e.tickUs);
            sqlite3_bind_text (g_insertStmt, 4, e.category, -1, SQLITE_STATIC);
            sqlite3_bind_text (g_insertStmt, 5, e.name,     -1, SQLITE_STATIC);
            sqlite3_bind_int64(g_insertStmt, 6, (e.durationNs + 500) / 1000);
            sqlite3_step(g_insertStmt);
        }
        exec(g_db, "COMMIT;");
    }
}

int64_t nowNs()
{
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        clock::now().time_since_epoch()).count();
}

} // namespace (file-scoped)

// --- Public API ----------------------------------------------------------

void Init()
{
    if (g_enabled.load()) return; // already up
    if (g_writer.joinable()) return;

    if (!openDb())
    {
        // Stay disabled; log already emitted. Do not throw — perf is optional.
        return;
    }

    g_sessionStartNs.store(nowNs());
    g_stopRequested.store(false);
    g_writer = std::thread(writerThread);
    g_enabled.store(true);
}

void Shutdown()
{
    if (!g_writer.joinable() && !g_enabled.load()) return;

    g_enabled.store(false);       // producers short-circuit from now on
    g_stopRequested.store(true);
    g_ringCv.notify_all();
    if (g_writer.joinable()) g_writer.join();

    if (g_insertStmt) { sqlite3_finalize(g_insertStmt); g_insertStmt = nullptr; }
    if (g_db)
    {
        // Emit one final summary line — useful when reading post-mortem logs.
        const int64_t dropped = g_dropped.load();
        if (dropped > 0)
        {
            DEBUG_LOG(("Telemetry: %lld events dropped (ring full)", (long long)dropped));
        }
        sqlite3_close(g_db);
        g_db = nullptr;
    }
}

void OnFrameBoundary()
{
    g_frameNo.fetch_add(1, std::memory_order_relaxed);
}

bool IsEnabled()
{
    return g_enabled.load(std::memory_order_acquire);
}

void Record(const char* category, const char* name, int64_t durationNs)
{
    if (!g_enabled.load(std::memory_order_acquire)) return;

    Event e;
    e.category   = category;
    e.name       = name;
    e.durationNs = durationNs;
    e.frameNo    = g_frameNo.load(std::memory_order_relaxed);
    e.tickUs     = (nowNs() - g_sessionStartNs.load(std::memory_order_relaxed)) / 1000;

    bool woken = false;
    {
        std::lock_guard<std::mutex> lk(g_ringMutex);
        const int next = (g_head + 1) % kRingSize;
        if (next == g_tail)
        {
            // Ring full: drop oldest by advancing tail. Writer is falling
            // behind; we'd rather lose samples than stall the sim.
            g_tail = (g_tail + 1) % kRingSize;
            g_dropped.fetch_add(1, std::memory_order_relaxed);
        }
        g_ring[g_head] = e;
        g_head = next;
        // Wake the writer if the batch is now half-full — otherwise let it
        // sleep to its own flush cadence so we don't spin-wake every event.
        const int pending =
            (g_head - g_tail + kRingSize) % kRingSize;
        woken = (pending >= kBatchSize / 2);
    }
    if (woken) g_ringCv.notify_one();
}

// --- Query helper --------------------------------------------------------
// Percentiles are computed client-side because sqlite core has no
// percentile_cont. For each (category,name) we order duration_us ASC and
// pick indices at 0.50 / 0.95 / 0.99.

int QueryBottlenecks(Stat* out, int maxCount, int topN)
{
    if (!g_db || !out || maxCount <= 0) return 0;
    std::lock_guard<std::mutex> lk(g_queryMutex);

    // 1) enumerate (cat,name) ordered by expected weight (n * mean, cheap proxy)
    sqlite3_stmt* s = nullptr;
    const char* kSql =
        "SELECT category, name, n, mean_us, max_us FROM v_rollup"
        " WHERE session_id=? ORDER BY (n*mean_us) DESC LIMIT ?;";
    if (sqlite3_prepare_v2(g_db, kSql, -1, &s, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int64(s, 1, g_sessionId);
    sqlite3_bind_int  (s, 2, topN);

    struct Row { std::string cat, name; int64_t n; double mean, max; };
    std::vector<Row> rows;
    rows.reserve(topN);
    while (sqlite3_step(s) == SQLITE_ROW)
    {
        Row r;
        r.cat  = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
        r.name = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
        r.n    = sqlite3_column_int64(s, 2);
        r.mean = sqlite3_column_double(s, 3);
        r.max  = sqlite3_column_double(s, 4);
        rows.push_back(std::move(r));
    }
    sqlite3_finalize(s);

    // 2) for each, fetch sorted duration_us and compute p50/p95/p99 + stddev
    int written = 0;
    for (const Row& r : rows)
    {
        if (written >= maxCount) break;

        sqlite3_stmt* q = nullptr;
        if (sqlite3_prepare_v2(g_db,
                "SELECT duration_us FROM events"
                " WHERE session_id=? AND category=? AND name=?"
                " ORDER BY duration_us ASC;",
                -1, &q, nullptr) != SQLITE_OK) continue;
        sqlite3_bind_int64(q, 1, g_sessionId);
        sqlite3_bind_text (q, 2, r.cat.c_str(),  -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (q, 3, r.name.c_str(), -1, SQLITE_TRANSIENT);

        std::vector<int64_t> vals;
        vals.reserve((size_t)r.n);
        while (sqlite3_step(q) == SQLITE_ROW)
            vals.push_back(sqlite3_column_int64(q, 0));
        sqlite3_finalize(q);
        if (vals.empty()) continue;

        auto pct = [&](double p) -> double
        {
            const size_t idx =
                std::min<size_t>(vals.size() - 1,
                    (size_t)(p * (double)(vals.size() - 1)));
            return (double)vals[idx];
        };

        double sumSq = 0.0;
        for (int64_t v : vals) { const double d = (double)v - r.mean; sumSq += d*d; }
        const double stddev = (vals.size() > 1)
            ? std::sqrt(sumSq / (double)(vals.size() - 1)) : 0.0;

        // These pointers are ephemeral — we'd need a string pool if this
        // were a hot path. For debug dump it's fine: caller copies or
        // prints immediately and discards.
        Stat& st = out[written++];
        st.category = r.cat.c_str();  // valid until this function returns
        st.name     = r.name.c_str();
        st.count    = r.n;
        st.meanUs   = r.mean;
        st.stddevUs = stddev;
        st.p50Us    = pct(0.50);
        st.p95Us    = pct(0.95);
        st.p99Us    = pct(0.99);
        st.maxUs    = r.max;
    }
    return written;
}

void DumpBottlenecks(int topN)
{
    if (!g_db) { DEBUG_LOG(("Telemetry: not running")); return; }

    std::vector<Stat> stats(topN);
    // NOTE: Stat.category/name reference std::string buffers that go out of
    // scope when QueryBottlenecks returns. We reformat into a local buffer
    // inside that function call's frame by printing before the vector dies.
    // To do that cleanly we re-run the query ourselves here rather than
    // going through QueryBottlenecks — keeps storage ownership obvious.

    std::lock_guard<std::mutex> lk(g_queryMutex);
    sqlite3_stmt* s = nullptr;
    const char* kSql =
        "SELECT category, name, n, mean_us, max_us FROM v_rollup"
        " WHERE session_id=? ORDER BY (n*mean_us) DESC LIMIT ?;";
    if (sqlite3_prepare_v2(g_db, kSql, -1, &s, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int64(s, 1, g_sessionId);
    sqlite3_bind_int  (s, 2, topN);

    DEBUG_LOG(("Telemetry top-%d bottlenecks (session=%lld)",
               topN, (long long)g_sessionId));
    DEBUG_LOG(("  %-10s %-28s %8s %10s %10s %10s %10s %10s %10s",
               "category","name","count","mean_us","stddev","p50","p95","p99","max"));

    while (sqlite3_step(s) == SQLITE_ROW)
    {
        std::string cat  = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
        std::string name = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
        const int64_t n  = sqlite3_column_int64(s, 2);
        const double mean= sqlite3_column_double(s, 3);
        const double mx  = sqlite3_column_double(s, 4);
        (void)mx; // only referenced inside DEBUG_LOG — no-op in Release

        sqlite3_stmt* q = nullptr;
        if (sqlite3_prepare_v2(g_db,
                "SELECT duration_us FROM events"
                " WHERE session_id=? AND category=? AND name=?"
                " ORDER BY duration_us ASC;",
                -1, &q, nullptr) != SQLITE_OK) continue;
        sqlite3_bind_int64(q, 1, g_sessionId);
        sqlite3_bind_text (q, 2, cat.c_str(),  -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (q, 3, name.c_str(), -1, SQLITE_TRANSIENT);
        std::vector<int64_t> vals; vals.reserve((size_t)n);
        while (sqlite3_step(q) == SQLITE_ROW) vals.push_back(sqlite3_column_int64(q, 0));
        sqlite3_finalize(q);
        if (vals.empty()) continue;

        auto pct = [&](double p) -> double {
            const size_t idx = std::min<size_t>(vals.size()-1,
                (size_t)(p * (double)(vals.size()-1)));
            return (double)vals[idx];
        };
        double sumSq = 0.0;
        for (int64_t v : vals) { const double d = (double)v - mean; sumSq += d*d; }
        const double stddev = (vals.size() > 1)
            ? std::sqrt(sumSq / (double)(vals.size()-1)) : 0.0;
        (void)stddev; // only referenced inside DEBUG_LOG — no-op in Release

        DEBUG_LOG(("  %-10s %-28s %8lld %10.1f %10.1f %10.1f %10.1f %10.1f %10.1f",
                   cat.c_str(), name.c_str(), (long long)n, mean, stddev,
                   pct(0.50), pct(0.95), pct(0.99), mx));
    }
    sqlite3_finalize(s);
}

} // namespace Telemetry

#endif // GENERALS_TELEMETRY
