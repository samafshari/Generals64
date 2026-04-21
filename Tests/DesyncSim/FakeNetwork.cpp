// FakeNetwork.cpp
//
// Implementation of the in-memory lockstep command queue.
//
// Key invariant: drain() returns commands in (deliveryFrame, senderId,
// sequenceId) order.  Tests rely on this to verify that two instances see
// commands in the same order and produce the same game state.

#include "FakeNetwork.h"

#include <algorithm>
#include <cstring>

// ----------------------------------------------------------------------------

void FakeNetwork::submit(uint32_t senderId, uint32_t currentFrame,
                         const void* data, size_t size)
{
    FakeCommand cmd;
    cmd.senderId       = senderId;
    cmd.frameSubmitted = currentFrame;
    cmd.deliveryFrame  = currentFrame + m_latency;
    cmd.sequenceId     = m_nextSeq[senderId]++;   // map default-inits to 0

    if (size > 0 && data != nullptr) {
        cmd.payload.resize(size);
        std::memcpy(cmd.payload.data(), data, size);
    }

    m_queue.push_back(std::move(cmd));
}

// ----------------------------------------------------------------------------

std::vector<FakeCommand> FakeNetwork::drain(uint32_t deliveryFrame)
{
    std::vector<FakeCommand> out;

    // Partition: collect matching commands into 'out', keep the rest.
    std::vector<FakeCommand> remaining;
    remaining.reserve(m_queue.size());

    for (auto& cmd : m_queue) {
        if (cmd.deliveryFrame == deliveryFrame)
            out.push_back(std::move(cmd));
        else
            remaining.push_back(std::move(cmd));
    }

    m_queue = std::move(remaining);

    // Sort by (senderId asc, sequenceId asc).
    // deliveryFrame is identical for all elements in 'out', so the primary key
    // drops out and we only need the tie-breakers.  This matches the real
    // engine's NetCommandList ordering.
    std::sort(out.begin(), out.end(),
        [](const FakeCommand& a, const FakeCommand& b) {
            if (a.senderId != b.senderId)
                return a.senderId < b.senderId;
            return a.sequenceId < b.sequenceId;
        });

    return out;
}

// ----------------------------------------------------------------------------

std::vector<FakeCommand> FakeNetwork::peek(uint32_t deliveryFrame) const
{
    std::vector<FakeCommand> out;
    for (const auto& cmd : m_queue) {
        if (cmd.deliveryFrame == deliveryFrame)
            out.push_back(cmd);
    }
    std::sort(out.begin(), out.end(),
        [](const FakeCommand& a, const FakeCommand& b) {
            if (a.senderId != b.senderId)
                return a.senderId < b.senderId;
            return a.sequenceId < b.sequenceId;
        });
    return out;
}

// ----------------------------------------------------------------------------

void FakeNetwork::reset()
{
    m_queue.clear();
    m_nextSeq.clear();
    // Keep m_latency — callers usually set it once before the first test.
}
