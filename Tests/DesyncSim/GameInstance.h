// GameInstance.h
//
// Represents one "virtual client" running logic in the lockstep harness.
//
// In a full integration the GameInstance would own or reference a real
// GameLogic, a real Pathfinder, a real AI stack, etc.  At this stage it owns
// a toy world: a flat list of StubUnits whose positions are updated each tick
// in response to MOVE commands and SPAWN commands injected through FakeNetwork.
//
// The tick() method is the heart of the determinism contract:
//   1. Drain the network for this frame (deterministic order from FakeNetwork).
//   2. Apply commands in drain order (order-independent state modifications
//      would be a desync hazard — this is intentionally order-dependent).
//   3. Advance the RNG once per tick so two instances with the same seed stay
//      in step even during idle frames.
//   4. Hash all state into the CrcCollector.
//
// TODO (integration): replace StubUnit with real Object*; replace
// StubGameLogicRandom with GameLogicRandomValue(); replace FakeNetwork drain
// with TheCommandXlat->executeFrame(); call ThePathfinder->update() here.

#pragma once

#include "FakeNetwork.h"
#include "CrcCollector.h"
#include "StubEngine.h"

#include <cstdint>
#include <vector>
#include <string>

// ----------------------------------------------------------------------------
// Command type tags encoded in FakeCommand payloads
// ----------------------------------------------------------------------------

enum class CmdType : uint8_t
{
    MOVE     = 1,   // move an existing unit to (tx, ty)
    ADD_UNIT = 2,   // spawn a new unit at (x, y) for ownerId
};

// Payload layouts — trivially serialisable with memcpy.

struct CmdMove
{
    CmdType  type = CmdType::MOVE;
    uint8_t  _pad[3] = {};
    ObjectID objectId;
    int32_t  tx;    // target cell x
    int32_t  ty;    // target cell y
};

struct CmdAddUnit
{
    CmdType  type = CmdType::ADD_UNIT;
    uint8_t  _pad[3] = {};
    uint32_t ownerId;
    int32_t  x;     // spawn cell x
    int32_t  y;     // spawn cell y
};

// ----------------------------------------------------------------------------
// StubUnit — the minimal game object for the toy world
// ----------------------------------------------------------------------------

struct StubUnit
{
    ObjectID objectId;
    int32_t  x;
    int32_t  y;
    int32_t  vx;            // velocity (cells/tick)
    int32_t  vy;
    uint32_t ownerId;
    uint32_t frameOfLastOrder;  // frame when the last MOVE was applied
};

// ----------------------------------------------------------------------------
// GameInstance
// ----------------------------------------------------------------------------

class GameInstance
{
public:
    // instanceId: which virtual player this instance represents (0-based).
    // net: shared FakeNetwork (all instances share one network).
    // rngSeed: deterministic seed — must be the same for all instances in a
    //   clean run (same seed = same sequence = same game state).
    GameInstance(uint32_t instanceId, FakeNetwork& net, uint32_t rngSeed);

    // Advance the simulation by one frame.
    // frame: the current logical frame number.
    void tick(uint32_t frame);

    // Read access for tests and the harness.
    const CrcCollector&         crcCollector()  const { return m_crc; }
    const std::vector<StubUnit>& units()         const { return m_units; }
    uint32_t                    instanceId()    const { return m_instanceId; }

    // Expose the RNG so tests can deliberately corrupt it to inject a desync.
    StubGameLogicRandom& rng() { return m_rng; }

    // Reset to initial state (used between test cases).
    void reset(uint32_t rngSeed);

    // Apply a batch of pre-sorted commands from the harness.
    // The harness calls this before tick() so every instance processes
    // exactly the same command list in the same order.
    void applyCommandBatch(const std::vector<FakeCommand>& cmds);

private:
    // Apply one FakeCommand payload to this instance's world state.
    void applyCommand(const FakeCommand& cmd);

    // Hash all world state into m_crc for the current frame.
    void hashState(uint32_t frame);

    uint32_t             m_instanceId;
    FakeNetwork&         m_net;
    StubGameLogicRandom  m_rng;
    StubObjectIDIssuer   m_idIssuer;
    CrcCollector         m_crc;
    std::vector<StubUnit> m_units;
};
