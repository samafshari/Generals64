// StubEngine.h
//
// Self-contained stubs for the engine singletons that logic code references.
//
// Real engine code calls TheGameLogic->getFrame(), GameLogicRandomValue(), etc.
// through global singleton pointers.  Linking those singletons would drag in
// the entire engine.  Instead this header provides minimal replacements that
// are deterministic, seedable, and free of external dependencies so the
// DesyncSim suite can run standalone.
//
// IMPORTANT: Do not include PreRTS.h or any real engine header from this file.
// The stubs are intentionally not API-compatible with the real engine — they
// are just enough for the toy world in GameInstance to compile and behave
// deterministically.
//
// When integrating with the real engine in a future pass:
//   - Replace StubGameLogicRandom with the real GameLogicRandomValue() call.
//   - Replace TheStubClock.frame() with TheGameLogic->getFrame().
//   - Replace StubObjectIDIssuer with the real ObjectID allocation.
//   - Keep StubPlayerList or map it to the real Player* array.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------
// ObjectID — a plain integer, like the engine's ObjectID typedef
// ----------------------------------------------------------------------------

using ObjectID = uint32_t;
static constexpr ObjectID INVALID_OBJECT_ID = 0u;

// ----------------------------------------------------------------------------
// TheStubClock — deterministic game frame counter
// ----------------------------------------------------------------------------

class StubClock
{
public:
    void        reset()               { m_frame = 0; }
    void        advance()             { ++m_frame; }
    uint32_t    frame()     const     { return m_frame; }
    void        setFrame(uint32_t f)  { m_frame = f; }

private:
    uint32_t m_frame = 0;
};

// One global instance per translation unit that includes this header.
// For multi-TU builds, declare extern in all but the defining TU, or
// route through a singleton accessor — fine for the single-file tests here.
extern StubClock TheStubClock;

// ----------------------------------------------------------------------------
// StubGameLogicRandom — deterministic LCG
//
// The real engine uses GameLogicRandomValue() which is seeded from the game
// setup and driven lockstep-identically on every client.  We mimic that with
// a standard LCG (same constants as glibc / many textbook examples).
// The important property: given the same seed and the same sequence of calls,
// every instance produces the same sequence of values.
// ----------------------------------------------------------------------------

class StubGameLogicRandom
{
public:
    explicit StubGameLogicRandom(uint32_t seed = 12345u) : m_state(seed) {}

    // Seed (or re-seed) the generator.  All instances given the same seed
    // will produce the same sequence — this is the determinism invariant.
    void seed(uint32_t s) { m_state = s; }

    // Return the next value in [0, 2^31).
    uint32_t next()
    {
        // LCG parameters from Numerical Recipes.
        m_state = 1664525u * m_state + 1013904223u;
        return m_state >> 1;   // strip sign bit for [0, 2^31)
    }

    // Return the next value in [0, range).
    uint32_t nextInRange(uint32_t range)
    {
        if (range == 0) return 0;
        return next() % range;
    }

    uint32_t state() const { return m_state; }

private:
    uint32_t m_state;
};

// ----------------------------------------------------------------------------
// StubObjectIDIssuer — monotonically increasing ObjectID counter
//
// Each GameInstance should own one issuer, seeded from a known base so IDs
// are predictable across instances (they all start from the same base and
// issue IDs in the same order since they process the same commands).
// ----------------------------------------------------------------------------

class StubObjectIDIssuer
{
public:
    explicit StubObjectIDIssuer(uint32_t firstID = 1u) : m_next(firstID) {}

    void     reset(uint32_t firstID = 1u) { m_next = firstID; }
    ObjectID issue()                      { return m_next++; }
    uint32_t peekNext()         const     { return m_next; }

private:
    uint32_t m_next;
};

// ----------------------------------------------------------------------------
// StubPlayer — minimal player record
// ----------------------------------------------------------------------------

struct StubPlayer
{
    uint32_t    index;      // 0-based player slot
    std::string name;
    bool        isHuman;    // false = AI

    StubPlayer(uint32_t idx, const char* n, bool human)
        : index(idx), name(n), isHuman(human) {}
};

// ----------------------------------------------------------------------------
// StubPlayerList — fixed set of players created at scenario setup time
// ----------------------------------------------------------------------------

class StubPlayerList
{
public:
    void addPlayer(uint32_t index, const char* name, bool isHuman)
    {
        m_players.emplace_back(index, name, isHuman);
    }

    const StubPlayer* findByIndex(uint32_t index) const
    {
        for (const auto& p : m_players)
            if (p.index == index) return &p;
        return nullptr;
    }

    size_t count() const { return m_players.size(); }

    const StubPlayer& operator[](size_t i) const { return m_players[i]; }

    void clear() { m_players.clear(); }

private:
    std::vector<StubPlayer> m_players;
};

// One global instance — tests populate it before running.
extern StubPlayerList TheStubPlayerList;
