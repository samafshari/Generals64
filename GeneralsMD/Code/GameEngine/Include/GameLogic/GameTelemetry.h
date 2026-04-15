/**
 * GameTelemetry
 *
 * Collects per-game events (unit create/die, building destroyed, cash
 * snapshots, game start/win/loss/surrender) during a multiplayer match
 * and ships them to the relay server as JSON batches every
 * kTelemetryFlushIntervalMs and at major events. The relay persists them
 * as GameEvent rows against the Game identified by the relay-assigned
 * session GUID (see LANAPI::getCurrentGameSessionId).
 *
 * Only active while LANAPI's relay connection is up AND a game session
 * id has been assigned — skirmish / campaign / pre-lobby never produce
 * telemetry, so the leaderboard stays MP-only per spec.
 */

#pragma once
#ifndef __GAMETELEMETRY_H_
#define __GAMETELEMETRY_H_

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "Common/GameCommon.h"
#include "Common/AsciiString.h"
#include "Common/STLTypedefs.h"
#include "Common/SubsystemInterface.h"

class Object;
class Player;

class GameTelemetry : public SubsystemInterface
{
public:
	GameTelemetry();
	virtual ~GameTelemetry();

	virtual void init() override;
	virtual void reset() override;
	virtual void update() override;

	// ── Hooks (called from GameLogic / VictoryConditions / etc.) ───

	/// Must be called from GameLogic::startNewGame once the player list
	/// exists. No-op outside of MP. Captures humans/ais counts from
	/// ThePlayerList so the server can flag 1-human games for leaderboard
	/// exclusion.
	void onGameStart();

	/// Unit / building / infantry / whatever — fired from ThingFactory::newObject.
	/// Distinguishes buildings (KINDOF_STRUCTURE) in the event type.
	void onObjectCreated(const Object *obj);

	/// Fired from GameLogic::destroyObject. Distinguishes buildings.
	void onObjectDestroyed(const Object *obj);

	// ── Per-owner stat buckets ─────────────────────────────────────
	//
	// Owner-attributed counts that ride alongside the event stream.
	// Each hook bumps a (object, owner, event) triple that we flush as a
	// compact buckets array every 30 s. Event semantics:
	//   built              — owner=builder
	//   lost               — owner=victim (our unit/building died)
	//   destroyed          — owner=attacker (credited the kill)
	//   building_captured  — owner=new controller (we captured an enemy)
	//   building_got_captured — owner=previous controller (lost to capture)
	//   vehicle_stolen     — owner=new controller (we hijacked)
	//   vehicle_got_stolen — owner=previous controller (ours hijacked)
	//   power_used         — owner=activator (object name = power template)

	void onObjectBuiltBy(const Player *owner, const Object *obj);
	void onObjectLostBy(const Player *owner, const Object *obj);
	void onObjectDestroyedBy(const Player *attacker, const Object *victim);
	void onObjectCapturedBy(const Player *newOwner, const Player *oldOwner, const Object *obj);
	void onSpecialPowerUsedBy(const Player *user, const char *powerName);

	/// Victory / defeat / surrender hooks. Each triggers an immediate
	/// flush so the server sees the terminal event even if the process
	/// exits a frame later.
	void onGameWon();
	void onGameLost();
	void onGameSurrendered();

	/// Clean shutdown (menu "Exit Game" etc.). Flushes what we have and
	/// writes a game_exited event.
	void onGameExited();

	/// Forces an immediate flush of the buffered events.
	void flush();

	/// True while the collector is observing an active MP session.
	Bool isActive() const { return m_active; }

private:
	struct Event
	{
		AsciiString type;
		Int         gameTimeMs;
		Int         cash;             ///< -1 if unknown
		AsciiString dataJson;         ///< inline JSON object, empty for none
	};

	/// One (object, owner, event) row in the buckets accumulator. We scan
	/// linearly on bump so the bucket count must stay small — in practice
	/// 30 s of play on a single client yields ~dozens to low-hundreds of
	/// unique triples, well within linear-search territory.
	struct StatBucket
	{
		AsciiString object;   ///< ThingTemplate name, or SpecialPowerTemplate name
		AsciiString owner;    ///< Player display name (ASCII-translated from Unicode)
		AsciiString event;    ///< event name, see onObject*By comment block
		Int         count;
	};

	void pushEvent(const char *type, Int cash, const char *dataJson);
	/// Bump the count on the matching (object, owner, event) triple,
	/// appending a new bucket if none exists. Both strings are copied.
	void bumpBucket(const char *object, const char *owner, const char *event);
	/// Helper: resolve Player → ASCII owner name (translates from the
	/// player's Unicode display name; empty if the player is null).
	AsciiString ownerNameOf(const Player *p) const;
	/// Emits a 'game_score' event carrying the local player's ScoreKeeper
	/// totals at terminal time: built/lost/killed for units and structures
	/// (kills split into vs-human and vs-AI buckets) plus money earned/spent.
	/// Called once per game from each terminal hook.
	void pushScoreSummary();
	Int  localPlayerCash() const;
	Int  gameTimeMs() const;
	Bool buildBatchJson(AsciiString &out) const;
	void clearBatch();

	// Background sender. flush() builds the JSON on the game thread (fast,
	// CPU-only) and hands it off to this worker, which does the blocking
	// TCP send. Prevents a slow network from stalling frames.
	void startSendThread();
	void stopSendThread();
	void enqueueSend(AsciiString &&json);
	void sendWorker();
	void drainSendQueue(UnsignedInt timeoutMs);

	Bool                 m_active;
	AsciiString          m_sessionId;       ///< stamped from LANAPI at game start
	AsciiString          m_localSide;       ///< local player's PlayerTemplate side (e.g. "America")
	Int                  m_humans;
	Int                  m_ais;
	Int                  m_gameStartFrame;  ///< TheGameLogic frame when onGameStart fired
	UnsignedInt          m_lastFlushMs;     ///< system ms of the last successful flush
	std::vector<Event>   m_buffer;          ///< game-thread only
	std::vector<StatBucket> m_buckets;      ///< game-thread only; cleared per-flush

	std::thread                 m_sendThread;
	std::mutex                  m_sendMutex;
	std::condition_variable     m_sendCv;
	std::deque<AsciiString>     m_sendQueue;   ///< guarded by m_sendMutex
	std::atomic<bool>           m_sendStop;    ///< thread shutdown flag
};

extern GameTelemetry *TheGameTelemetry;

#endif // __GAMETELEMETRY_H_
