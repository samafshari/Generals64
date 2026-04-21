// FakeNetwork.h
//
// Pure in-memory lockstep command queue — no sockets, no threads.
//
// In a real Generals lockstep session every client submits its commands for
// frame F and they are delivered to all clients for execution at frame F +
// latency.  The FakeNetwork replicates that contract exactly so tests can
// exercise the same ordering guarantees without spinning up the real network
// stack.
//
// Delivery order is deterministic: (deliveryFrame asc, senderId asc,
// sequenceId asc).  This is the same total order the real FrameDataManager
// imposes so any test that passes here should also pass in a live game with
// respect to command ordering.

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <map>

// ----------------------------------------------------------------------------
// FakeCommand — one command in the in-memory queue
// ----------------------------------------------------------------------------

struct FakeCommand
{
    uint32_t              senderId;       // which virtual client submitted this
    uint32_t              frameSubmitted; // game frame when submit() was called
    uint32_t              sequenceId;     // monotonically increasing per sender
    uint32_t              deliveryFrame;  // frameSubmitted + latency
    std::vector<uint8_t>  payload;        // opaque command bytes (encoded by caller)
};

// ----------------------------------------------------------------------------
// FakeNetwork
// ----------------------------------------------------------------------------

class FakeNetwork
{
public:
    FakeNetwork() = default;

    // Set the number of frames between submission and delivery.
    // Default is 2 (matches the Generals engine default command latency).
    void setLatency(uint32_t frames) { m_latency = frames; }
    uint32_t latency() const         { return m_latency; }

    // Submit a command from senderId at game frame currentFrame.
    // data/size are copied into the payload.  deliveryFrame = currentFrame + latency.
    void submit(uint32_t senderId, uint32_t currentFrame,
                const void* data, size_t size);

    // Return and remove all commands whose deliveryFrame == the given frame,
    // sorted deterministically by (deliveryFrame asc, senderId asc, sequenceId asc).
    // Callers must pass frames in non-decreasing order; the queue does not
    // garbage-collect old frames automatically.
    std::vector<FakeCommand> drain(uint32_t deliveryFrame);

    // Peek without removing (useful for inspection in tests).
    std::vector<FakeCommand> peek(uint32_t deliveryFrame) const;

    // Total commands still waiting in the queue.
    size_t pendingCount() const { return m_queue.size(); }

    // Reset everything — used between test cases.
    void reset();

private:
    std::vector<FakeCommand>       m_queue;
    std::map<uint32_t, uint32_t>   m_nextSeq;   // per-sender monotonic sequence counter
    uint32_t                       m_latency = 2;
};
