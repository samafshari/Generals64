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
 */

#pragma once
#ifndef __GAMETELEMETRY_H_
#define __GAMETELEMETRY_H_

#include "Common/GameCommon.h"
#include "Common/AsciiString.h"
#include "Common/SubsystemInterface.h"

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

	Bool isActive() const { return m_active; }

private:
	/// Build the GAMERESULT JSON and POST it through LANAPI::relaySendGameResult.
	/// localResult is the server's PlayerResult byte for the local player
	/// (1=Win, 2=Loss, 3=Draw, 4=Disconnect); other players' results are
	/// inferred from Player::isPlayerDead() plus the local outcome.
	void sendGameResultToRelay(Int localResult);

	Int  gameDurationSeconds() const;

	Bool         m_active;
	AsciiString  m_sessionId;       ///< stamped at game start — becomes Game.ExternalKey
	Int          m_gameStartFrame;
	Bool         m_resultSent;      ///< guards against double-send (win + exit in the same frame)
};

extern GameTelemetry *TheGameTelemetry;

#endif // __GAMETELEMETRY_H_
