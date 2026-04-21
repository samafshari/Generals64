// GameInstance.cpp
//
// One virtual lockstep client.
//
// The toy world physics are intentionally trivial: units move one step toward
// their target each tick and stop when they arrive.  The important property is
// that two instances with the same RNG seed and the same command stream always
// produce the same sequence of CRCs.  Any divergence in the test results is a
// bug in the harness or, if deliberately injected, the test payload.

#include "GameInstance.h"

#include <cstring>
#include <algorithm>
#include <cstdio>

// ----------------------------------------------------------------------------

GameInstance::GameInstance(uint32_t instanceId, FakeNetwork& net, uint32_t rngSeed)
    : m_instanceId(instanceId)
    , m_net(net)
    , m_rng(rngSeed)
    , m_idIssuer(1u)
{
}

// ----------------------------------------------------------------------------

void GameInstance::reset(uint32_t rngSeed)
{
    m_rng.seed(rngSeed);
    m_idIssuer.reset(1u);
    m_units.clear();
    // m_crc state is reset on the next beginFrame() call in tick().
}

// ----------------------------------------------------------------------------

void GameInstance::tick(uint32_t frame)
{
    m_crc.beginFrame(frame);

    // --- 1. Drain network -------------------------------------------------------
    // All instances share one FakeNetwork, but each calls drain() for its own
    // frame.  Because FakeNetwork::drain() is deterministic (sorted order) and
    // removes the commands it returns, only the first instance to call drain()
    // for a given frame will actually receive commands; subsequent instances get
    // an empty list.
    //
    // TODO (integration): in a real multi-process setup each client has its own
    // copy of the command buffer delivered by the network layer.  Here we
    // replicate the buffer to all instances by having LockstepHarness call
    // drain() once and pass the result to each instance's applyCommands().
    // For now, to keep GameInstance self-contained, the harness pre-populates
    // a per-instance copy.  See LockstepHarness.cpp for how this is handled.
    //
    // NOTE: the actual drain() call is performed by LockstepHarness which then
    // calls applyCommand() on each instance.  tick() here only advances physics
    // and hashes state.  The split is intentional — see LockstepHarness.cpp.

    // --- 2. Advance physics -----------------------------------------------------
    // Move each unit one step toward its target.  vx/vy encode the remaining
    // distance (signed): positive = move in + direction, negative = move in -
    // direction.  Each frame we move one cell and decrement the magnitude.
    // When vx/vy reach zero the unit has arrived.
    for (auto& u : m_units) {
        if (u.vx != 0) {
            u.x += (u.vx > 0) ? 1 : -1;
            if (u.vx > 0) --u.vx; else ++u.vx;
        }
        if (u.vy != 0) {
            u.y += (u.vy > 0) ? 1 : -1;
            if (u.vy > 0) --u.vy; else ++u.vy;
        }
    }

    // --- 3. Advance RNG ---------------------------------------------------------
    // Consume one RNG value every frame so both instances stay in lockstep with
    // the generator even during frames with no units (the RNG must advance
    // identically regardless of world content).
    (void)m_rng.next();

    // TODO (integration): call TheAI->update(frame) here; the AI internally
    // calls GameLogicRandomValue() which must stay synchronised.

    // --- 4. Hash state ----------------------------------------------------------
    hashState(frame);

    m_crc.endFrame();
}

// ----------------------------------------------------------------------------

void GameInstance::applyCommandBatch(const std::vector<FakeCommand>& cmds)
{
    for (const auto& cmd : cmds)
        applyCommand(cmd);
}

// ----------------------------------------------------------------------------

void GameInstance::applyCommand(const FakeCommand& cmd)
{
    if (cmd.payload.empty()) return;

    const uint8_t* p = cmd.payload.data();
    CmdType type = static_cast<CmdType>(p[0]);

    switch (type) {
    case CmdType::MOVE:
    {
        if (cmd.payload.size() < sizeof(CmdMove)) return;
        CmdMove mv;
        std::memcpy(&mv, p, sizeof(CmdMove));

        // Find the unit and set its velocity toward the target.
        for (auto& u : m_units) {
            if (u.objectId == mv.objectId) {
                u.vx = mv.tx - u.x;
                u.vy = mv.ty - u.y;
                // TODO (integration): call PathFinder::findPath() here.
                break;
            }
        }
        break;
    }
    case CmdType::ADD_UNIT:
    {
        if (cmd.payload.size() < sizeof(CmdAddUnit)) return;
        CmdAddUnit au;
        std::memcpy(&au, p, sizeof(CmdAddUnit));

        StubUnit unit{};
        unit.objectId = m_idIssuer.issue();
        unit.x        = au.x;
        unit.y        = au.y;
        unit.vx       = 0;
        unit.vy       = 0;
        unit.ownerId  = au.ownerId;
        unit.frameOfLastOrder = 0;
        m_units.push_back(unit);

        // TODO (integration): call TheGameLogic->addObject() here.
        break;
    }
    default:
        // Unknown command — ignore silently.  In a real engine this would be
        // a DEBUG_ASSERTCRASH.
        break;
    }
}

// ----------------------------------------------------------------------------

void GameInstance::hashState(uint32_t frame)
{
    // Hash in a deterministic order: units sorted by objectId ascending.
    // Sorting is O(n log n) but n is small in the toy world.  In the real
    // engine the object list is already ordered by ID so this sort is free.
    //
    // TODO (integration): replace this with TheGameLogic->getCRC(CRC_RECALC)
    // which serialises the full game state through XferCRC.

    std::vector<const StubUnit*> sorted;
    sorted.reserve(m_units.size());
    for (const auto& u : m_units)
        sorted.push_back(&u);
    std::sort(sorted.begin(), sorted.end(),
        [](const StubUnit* a, const StubUnit* b) {
            return a->objectId < b->objectId;
        });

    for (const auto* u : sorted) {
        // Each unit contributes to the "Objects" module.
        m_crc.addModule("Objects", u, sizeof(StubUnit));
    }

    // Hash the RNG state into a separate module so divergent RNG seeds are
    // detectable even if the object list happens to look identical.
    uint32_t rngState = m_rng.state();
    m_crc.addModule("RNG", &rngState, sizeof(rngState));

    // Hash the unit count so ADD_UNIT divergence shows up in "ObjectCount"
    // before it ripples into "Objects".
    uint32_t count = static_cast<uint32_t>(m_units.size());
    m_crc.addModule("ObjectCount", &count, sizeof(count));

    // TODO (integration): add "AI", "Pathfinder", "Player" modules by calling
    // the corresponding subsystem xfer methods.
    (void)frame;
}
