/**
 * GameTelemetry
 *
 * Emits the end-of-game GAMERESULT report via LANAPI::relaySendGameResult
 * so the server's consensus engine can finalise the game and credit each
 * player's lifetime stats. In-game events (unit / building production,
 * CRC sync, command stream) are observed by the relay directly from the
 * peer-sync packets — the client doesn't self-report any of that.
 *
 * Active only for sessions launched through the authenticated launcher
 * path (g_authGameToken non-empty). Sandbox / campaign runs are silent.
 *
 * Network I/O lives on a dedicated sender thread (m_senderThread). The
 * sim thread builds packet bytes inline, calls enqueuePacket, and
 * returns immediately — the worker drains the queue and runs the
 * blocking relaySendAll(). That keeps TCP back-pressure from stalling
 * GameLogic::update on a slow relay link.
 */

#pragma once
#ifndef __GAMETELEMETRY_H_
#define __GAMETELEMETRY_H_

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "Common/GameCommon.h"
#include "Common/AsciiString.h"
#include "Common/SubsystemInterface.h"

// Per-player score numbers read from ScoreKeeper. Under lockstep /
// CRC-validated sim every peer's numbers agree, so the server collates
// reports from every peer and trusts the canonical (self) report for
// display while keeping the cross-peer ones for audit. Absolute totals
// only — the watermark/delta machinery is gone (server computes deltas
// at ingest).
struct PerPlayerScore
{
	Int unitsBuilt;
	Int unitsLost;
	Int unitsKilledHuman;
	Int unitsKilledAI;
	Int buildingsBuilt;
	Int buildingsLost;
	Int buildingsKilledHuman;
	Int buildingsKilledAI;
	Int moneyEarned;
	Int moneySpent;
};

class GameTelemetry : public SubsystemInterface
{
public:
	GameTelemetry();
	virtual ~GameTelemetry();

	virtual void init() override;
	virtual void reset() override;
	virtual void update() override {}

	/// Arms the collector and zeroes watermarks; the session GUID is
	/// assigned separately by onRelayAssignSession when the relay's
	/// RELAY_TYPE_SESSION_ASSIGN packet lands (usually within one TCP
	/// round-trip of MSG_GAME_START). Until then m_sessionId is empty
	/// and every sender path short-circuits — that's intentional: the
	/// relay is the sole source of the match ID so every peer agrees.
	/// Called from GameLogic::startNewGame; no-op without an auth token.
	void onGameStart();

	/// Handler for the relay's RELAY_TYPE_SESSION_ASSIGN packet. Takes
	/// the 16 raw GUID bytes (same order GuidBytesMatchNFormat expects
	/// on the way back), hex-encodes to the 32-char form used for
	/// Game.ExternalKey, and stores on m_sessionId. Telemetry begins
	/// firing on the next logic frame. Idempotent for the same GUID;
	/// logs + ignores on mismatch in case of a late / duplicate assign
	/// from a prior game.
	void onRelayAssignSession(const UnsignedByte bytes[16]);

	/// Terminal hooks — each builds and ships a full GAMERESULT through
	/// the relay. Exited reports the local player as Disconnect; the
	/// others report Win / Loss respectively.
	void onGameWon();
	void onGameLost();
	void onGameSurrendered();
	void onGameExited();

	/// Periodic score-events hook. Called from GameLogic::update once
	/// per logic frame; throttles itself to one send every
	/// SCORE_EVENTS_EVERY_FRAMES (~3 s at 30 Hz). Builds a variable-
	/// length RELAY_TYPE_SCORE_EVENTS packet carrying ABSOLUTE totals
	/// for every observable player in the match (lockstep sim → every
	/// peer's ScoreKeeper has authoritative-quality numbers for every
	/// player). Server keeps every report; the canonical (self-)
	/// report is what drives display. No-ops unless the session is
	/// active, we haven't already shipped a terminal GAMERESULT, and
	/// LAN / GameLogic / PlayerList are live.
	void onLogicFrame();

	Bool isActive() const { return m_active; }

	/// 32-char hex session GUID assigned by the relay's
	/// RELAY_TYPE_SESSION_ASSIGN packet (the <c>Game.ExternalKey</c> on
	/// the server). Empty until telemetry is armed AND the relay's
	/// assign packet has landed. RecorderClass uses this as the
	/// replay filename so each game's <c>.rep</c> file is uniquely
	/// addressable on disk and the launcher's uploader can correlate
	/// the file straight to the server-side game row.
	const AsciiString &getSessionId() const { return m_sessionId; }

	/// Detailed in-game event hook. Called from anywhere in the engine
	/// where an interesting event fires (object created / destroyed,
	/// upgrade complete, science purchased, superpower fired, …).
	/// Best-effort: failures never propagate to the caller, the sim
	/// thread is never blocked, and the event is silently dropped if
	/// the per-tick batch is full or the session isn't armed.
	///
	/// <paramref name="eventType"/> is an open string interned at the
	/// server (no enum). Up to 32 chars; longer strings are clipped.
	/// Slot params are 0xFF when "no actor / target". <paramref name="thingTemplateId"/>
	/// is -1 when not bound to a thing template. <paramref name="x"/>
	/// / <paramref name="y"/> are engine world units (INT_MIN = none).
	/// <paramref name="extraJson"/> is an optional JSON OBJECT
	/// (with surrounding braces — the helper pastes it verbatim
	/// after <c>"extra":</c>, so a key-value fragment would produce
	/// malformed JSON and the relay would drop the whole batch).
	/// Pass nullptr / empty when there's nothing to attach. The
	/// helper validates the first non-whitespace char is <c>{</c>
	/// and silently strips the field if not — defensive backstop
	/// against a future hook author mis-formatting.
	void emitEvent(const char *eventType,
		Int actorSlot, Int targetSlot, Int thingTemplateId,
		Int x, Int y, Int cash, const char *extraJson);

	/// Capture a sim-object position snapshot and ship it through the
	/// telemetry sender. Called from <see cref="onLogicFrame"/> on the
	/// per-minute periodic cadence and from event-trigger sites
	/// (<c>onGameWon/Lost/Surrendered/Exited</c>, victory-conditions
	/// player-defeated detection, netcode timeout handler, CRC-mismatch
	/// detection).
	///
	/// <paramref name="triggerKind"/> is an open string identifying the
	/// trigger ("periodic", "player_defeated", "player_exited",
	/// "client_disconnected", "desync", "game_end"). New values surface
	/// new analytics with no schema change. <paramref name="triggerSlot"/>
	/// is the slot whose state caused the trigger when applicable
	/// (-1 = none — periodic / desync / game_end).
	///
	/// Best-effort: failures don't propagate. Wraps per-object reads
	/// in try/catch (the desync trigger may capture mid-mutation
	/// state).
	void snapshotPositions(const char *triggerKind, Int triggerSlot);

private:
	/// Build the GAMERESULT JSON and POST it through LANAPI::relaySendGameResult.
	/// localResult is the server's PlayerResult byte for the local player
	/// (1=Win, 2=Loss, 3=Draw, 4=Disconnect); other players' results are
	/// inferred from Player::isPlayerDead() plus the local outcome.
	void sendGameResultToRelay(Int localResult);

	Int  gameDurationSeconds() const;

	// ── Sender thread ────────────────────────────────────────────
	//
	// Producer (sim thread): build packet bytes inline, then call
	// enqueuePacket — copies the buffer into m_outboxQueue under
	// m_outboxMutex and notifies m_outboxCV. Returns immediately.
	//
	// Consumer (m_senderThread): senderThreadFn drains one packet at
	// a time and calls TheLAN->relaySendAll. relaySendAll already
	// serialises against lobby/game writers via LANAPI::m_relaySendMutex,
	// so telemetry packets can't interleave bytes with anything else.
	//
	// Approach taken (judgment call): we keep relaySendGameResult /
	// relaySendScoreEvents as wrappers and split out
	// packGameResultPacket / packScoreEventPacket helpers in LANAPI;
	// GameTelemetry calls the pack helpers and enqueues the bytes,
	// so relaySendAll is only ever invoked from the telemetry thread
	// while non-telemetry callers of the wrappers behave exactly as
	// before.
	void enqueuePacket(const UnsignedByte *buf, Int len);
	void senderThreadFn();

	// Pack the in-flight event batch as RELAY_TYPE_GAME_EVENTS and
	// hand it to the sender thread. Called from onLogicFrame on the
	// SCORE_EVENTS cadence so events ride the same packet rhythm.
	void shipPendingEventsBatch();

	// Pending event batch buffer — events accumulated since the last
	// shipPendingEventsBatch flush. Lives on the sim thread; not
	// shared with the sender thread (the flush copies into a new
	// vector before enqueueing).
	struct PendingEvent
	{
		Int frame;
		AsciiString type;          // open string, ≤32 chars
		Int actorSlot;             // 0xFF = none
		Int targetSlot;            // 0xFF = none
		Int thingTemplateId;       // -1 = none
		Int x;                     // INT_MIN = none
		Int y;                     // INT_MIN = none
		Int cash;                  // INT_MIN = none
		AsciiString extraJson;     // optional inner-JSON body
	};
	std::vector<PendingEvent> m_pendingEvents;
	std::mutex                m_pendingEventsMutex;

	Bool         m_active;
	AsciiString  m_sessionId;       ///< stamped at game start — becomes Game.ExternalKey

	// Pre-arrival buffer for RELAY_TYPE_SESSION_ASSIGN. On the host,
	// MSG_GAME_START is sent from lobby code and the relay's ASSIGN
	// broadcast can round-trip back through LANAPI::update's 200 ms
	// poll while the engine is still finishing its lobby→game
	// transition — before GameLogic::startNewGame calls onGameStart.
	// If we wrote straight into m_sessionId on receipt, onGameStart's
	// clear() would then discard the very ID we just stashed.
	// Instead, onRelayAssignSession deposits the hex form here (and
	// into m_sessionId if we're already mid-game); onGameStart adopts
	// from here and clears the pending buffer.
	AsciiString  m_pendingAssignId;
	Int          m_gameStartFrame;
	Bool         m_resultSent;      ///< guards against double-send (win + exit in the same frame)
	Int          m_lastSnapshotFrame; ///< frame of most recent onLogicFrame snapshot send; 0 = none yet

	// Periodic position-snapshot bucket. Increments every
	// SNAPSHOT_EVERY_FRAMES of game time; a snapshot is taken when
	// the bucket index advances, so paused games don't roll the
	// cadence forward.
	Int          m_lastPositionSnapshotBucket;

	// ── Per-minute FPS bucketing ──────────────────────────────────
	//
	// Each onLogicFrame samples TheDisplay->getCurrentFPS() and rolls
	// the value into the in-flight bucket (sum / min / max / count).
	// When the minute index (frame since match start / logic Hz / 60)
	// advances, the previous bucket is packed + shipped through the
	// relay's RELAY_TYPE_FPS_BUCKET channel and the accumulator
	// resets. Feeds the per-map playability benchmarking query.
	//
	// Fixed-point FPS × 100 on the wire keeps the packet integer
	// while preserving two decimals (58.73 fps → 5873).
	void shipAndResetFpsBucket();
	Int  m_fpsMinuteIndex;   ///< 0-based minute currently being aggregated
	Int  m_fpsSampleCount;   ///< samples folded into the in-flight bucket
	Int  m_fpsSumX100;       ///< running sum of FPS×100 across the minute
	Int  m_fpsMinX100;       ///< running min across the minute
	Int  m_fpsMaxX100;       ///< running max across the minute

	// Sender-thread state. m_outboxQueue is owned by the mutex; the
	// CV is notified on every enqueue and on shutdown. The atomic
	// flag is checked from the worker loop so shutdown can wake the
	// CV and exit cleanly after the queue drains.
	std::deque<std::vector<UnsignedByte> > m_outboxQueue;
	std::mutex                              m_outboxMutex;
	std::condition_variable                 m_outboxCV;
	std::thread                             m_senderThread;
	std::atomic<bool>                       m_senderShouldStop;
};

extern GameTelemetry *TheGameTelemetry;

#endif // __GAMETELEMETRY_H_
