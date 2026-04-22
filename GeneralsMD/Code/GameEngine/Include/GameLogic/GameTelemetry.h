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
// CRC-validated sim every peer's numbers agree, so the first report
// the server receives wins. Reused as the "last sent" watermark so
// the periodic SCORE_EVENTS packet can ship deltas instead of totals.
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

	/// Generates the session GUID used as Game.ExternalKey on the server
	/// and flips the collector into the active state. Called from
	/// GameLogic::startNewGame; no-op without an auth token.
	void onGameStart();

	/// Terminal hooks — each builds and ships a full GAMERESULT through
	/// the relay. Exited reports the local player as Disconnect; the
	/// others report Win / Loss respectively.
	void onGameWon();
	void onGameLost();
	void onGameSurrendered();
	void onGameExited();

	/// Periodic score-events hook. Called from GameLogic::update once
	/// per logic frame; throttles itself to one send every
	/// SCORE_EVENTS_EVERY_FRAMES (~3 s at 30 Hz). Builds a fixed-width
	/// binary RELAY_TYPE_SCORE_EVENTS packet from the local player's
	/// ScoreKeeper carrying per-field DELTAS since the last send, plus
	/// a wall-clock UTC-ms timestamp and the current logic frame so
	/// the server can reconstruct a per-game timeline. Updates
	/// m_lastSent to the new totals after enqueueing. No-ops unless
	/// the session is active, we haven't already shipped a terminal
	/// GAMERESULT, and LAN/GameLogic/PlayerList are live.
	void onLogicFrame();

	Bool isActive() const { return m_active; }

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

	Bool         m_active;
	AsciiString  m_sessionId;       ///< stamped at game start — becomes Game.ExternalKey
	Int          m_gameStartFrame;
	Bool         m_resultSent;      ///< guards against double-send (win + exit in the same frame)
	Int          m_lastSnapshotFrame; ///< frame of most recent onLogicFrame snapshot send; 0 = none yet

	// Watermark: totals the engine has already reported to the relay.
	// Each onLogicFrame ships current - m_lastSent and then assigns
	// current into m_lastSent. Reset to zero in init/onGameStart/reset
	// so a fresh game starts fresh.
	PerPlayerScore m_lastSent;

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
