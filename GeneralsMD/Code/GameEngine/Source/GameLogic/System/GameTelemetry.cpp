/**
 * GameTelemetry
 *
 * See GameTelemetry.h for intent. This file implements the event buffer
 * and JSON serialization by hand (no external JSON lib dependency — the
 * event shape is small and fixed).
 */

#include "PreRTS.h"

#include "GameLogic/GameTelemetry.h"

#include "Common/AsciiString.h"
#include "Common/GameCommon.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/KindOf.h"
#include "Common/Money.h"
#include "Common/ScoreKeeper.h"
#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/RestClient.h"

#include <mmsystem.h>
#include <objbase.h>  // CoCreateGuid

extern char g_authGameToken[];  // CommandLine.cpp

GameTelemetry *TheGameTelemetry = NULL;

// Flush cadence: every 30s of wall-clock during an active game. Terminal
// events (win/loss/surrender/exit) also flush immediately.
static const UnsignedInt kTelemetryFlushIntervalMs = 30 * 1000;

// Cap on the buffered events we'll hold before force-flushing early. Keeps
// memory bounded on pathological games (very fast unit spam). Picked high
// enough that a normal 30s window never trips it.
static const Int kMaxBufferedEvents = 4096;

// ── Helpers ─────────────────────────────────────────────────────

/// Append a JSON-escaped copy of \p src onto \p dst. Handles the basic set
/// (quote, backslash, control chars) — we never emit non-ASCII identifiers
/// from the telemetry path so we don't need full unicode escape handling.
static void appendJsonEscaped(AsciiString &dst, const char *src)
{
	if (!src)
		return;
	for (const char *p = src; *p; ++p)
	{
		unsigned char c = (unsigned char)*p;
		switch (c)
		{
			case '"':  dst.concat("\\\""); break;
			case '\\': dst.concat("\\\\"); break;
			case '\b': dst.concat("\\b");  break;
			case '\f': dst.concat("\\f");  break;
			case '\n': dst.concat("\\n");  break;
			case '\r': dst.concat("\\r");  break;
			case '\t': dst.concat("\\t");  break;
			default:
				if (c < 0x20)
				{
					char buf[8];
					sprintf(buf, "\\u%04x", c);
					dst.concat(buf);
				}
				else
				{
					char ch[2] = { (char)c, 0 };
					dst.concat(ch);
				}
				break;
		}
	}
}

GameTelemetry::GameTelemetry()
: m_active(FALSE)
, m_humans(0)
, m_ais(0)
, m_gameStartFrame(0)
, m_lastFlushMs(0)
, m_sendStop(false)
{
}

GameTelemetry::~GameTelemetry()
{
	// Stop the worker so the OS reclaims the thread cleanly. The worker
	// may be mid-send() on the relay socket; the join waits for that one
	// call to return, then we exit.
	stopSendThread();
}

void GameTelemetry::init()
{
	m_active = FALSE;
	m_sessionId.clear();
	m_localSide.clear();
	m_humans = 0;
	m_ais = 0;
	m_gameStartFrame = 0;
	m_lastFlushMs = 0;
	m_buffer.clear();
	m_buckets.clear();
	startSendThread();
}

void GameTelemetry::reset()
{
	// Called between games — drop any buffered events without sending
	// (the previous game was closed out by its terminal event) but keep
	// the sender thread alive for the next game.
	m_active = FALSE;
	m_sessionId.clear();
	m_localSide.clear();
	m_humans = 0;
	m_ais = 0;
	m_gameStartFrame = 0;
	m_buffer.clear();
	m_buckets.clear();
}

void GameTelemetry::update()
{
	if (!m_active)
		return;

	// Time-based flush. Also hard-cap the buffer size.
	UnsignedInt now = timeGetTime();
	Bool overCap = (Int)m_buffer.size() >= kMaxBufferedEvents;
	if (overCap || (now - m_lastFlushMs) >= kTelemetryFlushIntervalMs)
	{
		if (!m_buffer.empty() || !m_buckets.empty())
			flush();
		else
			m_lastFlushMs = now;
	}
}

Int GameTelemetry::localPlayerCash() const
{
	if (!ThePlayerList)
		return -1;
	Player *p = ThePlayerList->getLocalPlayer();
	if (!p)
		return -1;
	const Money *m = p->getMoney();
	return m ? (Int)m->countMoney() : -1;
}

Int GameTelemetry::gameTimeMs() const
{
	if (!TheGameLogic)
		return 0;
	UnsignedInt frame = TheGameLogic->getFrame();
	UnsignedInt startFrame = (UnsignedInt)m_gameStartFrame;
	if (frame < startFrame)
		return 0;
	UnsignedInt dFrames = frame - startFrame;
	return (Int)((Real)dFrames * MSEC_PER_LOGICFRAME_REAL);
}

void GameTelemetry::pushEvent(const char *type, Int cash, const char *dataJson)
{
	if (!m_active)
		return;
	Event e;
	e.type = type;
	e.gameTimeMs = gameTimeMs();
	e.cash = cash;
	if (dataJson && *dataJson)
		e.dataJson = dataJson;
	m_buffer.push_back(e);
}

AsciiString GameTelemetry::ownerNameOf(const Player *p) const
{
	AsciiString out;
	if (!p)
		return out;
	// Use the mutable accessor reference — getPlayerDisplayName() is not
	// const in Player.h, so we can't invoke it through a const Player*.
	// Const-cast here is intentional: we only read the name, and the game
	// engine treats display names as read-only during play.
	Player *pw = const_cast<Player *>(p);
	out.translate(pw->getPlayerDisplayName());
	return out;
}

void GameTelemetry::bumpBucket(const char *object, const char *owner, const char *event)
{
	if (!m_active)
		return;
	if (!object || !*object || !owner || !*owner || !event || !*event)
		return;
	for (size_t i = 0; i < m_buckets.size(); ++i)
	{
		StatBucket &b = m_buckets[i];
		if (b.event.compare(event) == 0
		 && b.object.compare(object) == 0
		 && b.owner.compare(owner) == 0)
		{
			++b.count;
			return;
		}
	}
	StatBucket nb;
	nb.object.set(object);
	nb.owner.set(owner);
	nb.event.set(event);
	nb.count = 1;
	m_buckets.push_back(nb);
}

void GameTelemetry::onObjectBuiltBy(const Player *owner, const Object *obj)
{
	if (!m_active || !owner || !obj || !obj->getTemplate())
		return;
	AsciiString o = ownerNameOf(owner);
	if (o.isEmpty())
		return;
	bumpBucket(obj->getTemplate()->getName().str(), o.str(), "built");
}

void GameTelemetry::onObjectLostBy(const Player *owner, const Object *obj)
{
	if (!m_active || !owner || !obj || !obj->getTemplate())
		return;
	AsciiString o = ownerNameOf(owner);
	if (o.isEmpty())
		return;
	bumpBucket(obj->getTemplate()->getName().str(), o.str(), "lost");
}

void GameTelemetry::onObjectDestroyedBy(const Player *attacker, const Object *victim)
{
	if (!m_active || !attacker || !victim || !victim->getTemplate())
		return;
	AsciiString o = ownerNameOf(attacker);
	if (o.isEmpty())
		return;
	bumpBucket(victim->getTemplate()->getName().str(), o.str(), "destroyed");
}

void GameTelemetry::onObjectCapturedBy(const Player *newOwner, const Player *oldOwner, const Object *obj)
{
	if (!m_active || !obj || !obj->getTemplate())
		return;
	const bool isStructure = obj->isKindOf(KINDOF_STRUCTURE);
	const char *capturedEvt = isStructure ? "building_captured"     : "vehicle_stolen";
	const char *gotEvt      = isStructure ? "building_got_captured" : "vehicle_got_stolen";
	// Hold the ThingTemplate name in a named local so the AsciiString
	// returned by getName() survives both bumpBucket() calls below — the
	// temporary would otherwise die at the end of a single statement.
	AsciiString objName = obj->getTemplate()->getName();
	if (newOwner)
	{
		AsciiString o = ownerNameOf(newOwner);
		if (!o.isEmpty())
			bumpBucket(objName.str(), o.str(), capturedEvt);
	}
	if (oldOwner)
	{
		AsciiString o = ownerNameOf(oldOwner);
		if (!o.isEmpty())
			bumpBucket(objName.str(), o.str(), gotEvt);
	}
}

void GameTelemetry::onSpecialPowerUsedBy(const Player *user, const char *powerName)
{
	if (!m_active || !user || !powerName || !*powerName)
		return;
	AsciiString o = ownerNameOf(user);
	if (o.isEmpty())
		return;
	bumpBucket(powerName, o.str(), "power_used");
}

void GameTelemetry::onGameStart()
{
	// Only collect telemetry when we have a game token (so the REST calls
	// can authenticate). No token = the user didn't launch via the
	// launcher, or the launch-token exchange failed — either way there's
	// no authenticated user to attribute events to.
	if (g_authGameToken[0] == '\0')
		return;

	// Generate a fresh client-side GUID for this game session. The server
	// uses it as the Game row's ExternalKey — duplicate batches for the
	// same session converge on one row thanks to the unique index.
	GUID guid{};
	if (FAILED(CoCreateGuid(&guid)))
		return;
	char sid[33];
	_snprintf(sid, sizeof(sid),
	          "%08x%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
	          guid.Data1, guid.Data2, guid.Data3,
	          guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
	          guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
	sid[32] = '\0';
	m_sessionId.set(sid);
	m_active = TRUE;
	m_gameStartFrame = TheGameLogic ? (Int)TheGameLogic->getFrame() : 0;
	m_lastFlushMs = timeGetTime();
	m_buffer.clear();
	m_buckets.clear();

	// Capture the local player's faction (PlayerTemplate side, e.g. "America",
	// "ChinaTankGeneral") so the server can drive a "favorite faction" stat.
	// Snapshotting at game-start ties the value to a frozen point — campaign
	// scripts can swap a player's template mid-match, but for leaderboard
	// purposes the start-of-game faction is what the player picked.
	m_localSide.clear();
	if (ThePlayerList)
	{
		Player *me = ThePlayerList->getLocalPlayer();
		if (me)
		{
			const PlayerTemplate *pt = me->getPlayerTemplate();
			if (pt)
				m_localSide = pt->getSide();
		}
	}

	// Count humans vs AIs from the lobby slot list. ThePlayerList includes
	// engine bookkeeping entries (PlyrCivilian, PlyrCreeps, observer) that
	// are typed as PLAYER_HUMAN but aren't real participants, so walking it
	// overcounted. TheGameInfo's slot list is the authoritative source on
	// who actually joined the match.
	m_humans = 0;
	m_ais = 0;
	if (TheGameInfo)
	{
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			GameSlot *slot = TheGameInfo->getSlot(i);
			if (!slot)
				continue;
			if (slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
				continue;
			if (slot->isHuman())      ++m_humans;
			else if (slot->isAI())    ++m_ais;
		}
	}

	pushEvent("game_started", localPlayerCash(), NULL);
	flush();
}

void GameTelemetry::onObjectCreated(const Object *obj)
{
	if (!m_active || !obj)
		return;
	const ThingTemplate *tmpl = obj->getTemplate();
	if (!tmpl)
		return;

	AsciiString data;
	data.concat("{\"unit\":\"");
	appendJsonEscaped(data, tmpl->getName().str());
	data.concat("\"}");

	const char *type = obj->isKindOf(KINDOF_STRUCTURE) ? "building_created" : "unit_created";
	pushEvent(type, localPlayerCash(), data.str());
}

void GameTelemetry::onObjectDestroyed(const Object *obj)
{
	if (!m_active || !obj)
		return;
	const ThingTemplate *tmpl = obj->getTemplate();
	if (!tmpl)
		return;

	AsciiString data;
	data.concat("{\"unit\":\"");
	appendJsonEscaped(data, tmpl->getName().str());
	data.concat("\"}");

	const char *type = obj->isKindOf(KINDOF_STRUCTURE) ? "building_destroyed" : "unit_died";
	pushEvent(type, localPlayerCash(), data.str());
}

void GameTelemetry::pushScoreSummary()
{
	if (!m_active || !ThePlayerList)
		return;

	Player *me = ThePlayerList->getLocalPlayer();
	if (!me)
		return;
	ScoreKeeper *sk = me->getScoreKeeper();
	if (!sk)
		return;

	const Int myIdx = me->getPlayerIndex();
	Int killedHumanUnits = 0, killedAIUnits = 0;
	Int killedHumanBuildings = 0, killedAIBuildings = 0;

	const Int playerCount = ThePlayerList->getPlayerCount();
	for (Int i = 1; i < playerCount; ++i)
	{
		if (i == myIdx)
			continue;  // Don't credit kills on our own units as "enemies defeated"
		Player *other = ThePlayerList->getNthPlayer(i);
		if (!other)
			continue;
		const Int u = sk->getUnitsDestroyedByPlayer(i);
		const Int b = sk->getBuildingsDestroyedByPlayer(i);
		switch (other->getPlayerType())
		{
			case PLAYER_HUMAN:
				killedHumanUnits += u;
				killedHumanBuildings += b;
				break;
			case PLAYER_COMPUTER:
				killedAIUnits += u;
				killedAIBuildings += b;
				break;
			default: break;  // Neutral / civilian — don't count
		}
	}

	// Buffer needs to hold ~180 chars of labels + 10 ints up to 11 chars each.
	// 512 gives comfortable headroom for any realistic ScoreKeeper totals.
	AsciiString data;
	char buf[512];
	_snprintf(buf, sizeof(buf) - 1,
	        "{\"unitsBuilt\":%d,\"unitsLost\":%d,"
	        "\"unitsKilledHuman\":%d,\"unitsKilledAI\":%d,"
	        "\"buildingsBuilt\":%d,\"buildingsLost\":%d,"
	        "\"buildingsKilledHuman\":%d,\"buildingsKilledAI\":%d,"
	        "\"moneyEarned\":%d,\"moneySpent\":%d}",
	        sk->getTotalUnitsBuilt(), sk->getTotalUnitsLost(),
	        killedHumanUnits, killedAIUnits,
	        sk->getTotalBuildingsBuilt(), sk->getTotalBuildingsLost(),
	        killedHumanBuildings, killedAIBuildings,
	        sk->getTotalMoneyEarned(), sk->getTotalMoneySpent());
	buf[sizeof(buf) - 1] = '\0';
	data.set(buf);

	pushEvent("game_score", localPlayerCash(), data.str());
}

void GameTelemetry::onGameWon()
{
	if (!m_active) return;
	pushScoreSummary();
	pushEvent("game_won", localPlayerCash(), NULL);
	flush();
	m_active = FALSE;
}

void GameTelemetry::onGameLost()
{
	if (!m_active) return;
	pushScoreSummary();
	pushEvent("game_lost", localPlayerCash(), NULL);
	flush();
	m_active = FALSE;
}

void GameTelemetry::onGameSurrendered()
{
	if (!m_active) return;
	pushScoreSummary();
	pushEvent("game_surrendered", localPlayerCash(), NULL);
	flush();
	m_active = FALSE;
}

void GameTelemetry::onGameExited()
{
	if (!m_active) return;
	pushScoreSummary();
	pushEvent("game_exited", localPlayerCash(), NULL);
	flush();
	m_active = FALSE;
	// User hit Exit — the engine may tear down to shell or even quit to
	// desktop soon. Give the worker a couple of seconds to land the final
	// batch before we allow that. On win/loss/surrender we skip this wait
	// because gameplay continues briefly (victory screen etc.) and the
	// worker has plenty of runway.
	drainSendQueue(2000);
}

Bool GameTelemetry::buildBatchJson(AsciiString &out) const
{
	out.clear();
	if (m_sessionId.isEmpty() || (m_buffer.empty() && m_buckets.empty()))
		return FALSE;

	// Envelope: sessionId + humans + ais + mapName + events array.
	// Counts/mapName are only meaningful on the first batch; we emit them
	// every time (idempotent on the server — it only writes them once per
	// Game row).
	out.concat("{\"sessionId\":\"");
	appendJsonEscaped(out, m_sessionId.str());
	char buf[64];
	sprintf(buf, "\",\"humans\":%d,\"ais\":%d", m_humans, m_ais);
	out.concat(buf);
	if (TheGlobalData && !TheGlobalData->m_mapName.isEmpty())
	{
		out.concat(",\"mapName\":\"");
		appendJsonEscaped(out, TheGlobalData->m_mapName.str());
		out.concat("\"");
	}
	if (!m_localSide.isEmpty())
	{
		// Reporter's faction this match. Server treats the first non-empty
		// value as authoritative (later batches are idempotent) — a player
		// can't change side mid-match, so there's nothing to reconcile.
		out.concat(",\"side\":\"");
		appendJsonEscaped(out, m_localSide.str());
		out.concat("\"");
	}
	out.concat(",\"events\":[");

	for (size_t i = 0; i < m_buffer.size(); ++i)
	{
		const Event &e = m_buffer[i];
		if (i > 0) out.concat(",");
		out.concat("{\"type\":\"");
		appendJsonEscaped(out, e.type.str());
		sprintf(buf, "\",\"gameTimeMs\":%d", e.gameTimeMs);
		out.concat(buf);
		if (e.cash >= 0)
		{
			sprintf(buf, ",\"cash\":%d", e.cash);
			out.concat(buf);
		}
		if (!e.dataJson.isEmpty())
		{
			out.concat(",\"data\":");
			out.concat(e.dataJson.str());
		}
		out.concat("}");
	}
	out.concat("]");

	// Buckets: per-owner (object, event) counts accumulated since the last
	// flush. Emitted only when we have any to report; keeps the envelope
	// small for quiet periods.
	if (!m_buckets.empty())
	{
		out.concat(",\"buckets\":[");
		for (size_t i = 0; i < m_buckets.size(); ++i)
		{
			const StatBucket &b = m_buckets[i];
			if (i > 0) out.concat(",");
			out.concat("{\"object\":\"");
			appendJsonEscaped(out, b.object.str());
			out.concat("\",\"owner\":\"");
			appendJsonEscaped(out, b.owner.str());
			out.concat("\",\"event\":\"");
			appendJsonEscaped(out, b.event.str());
			sprintf(buf, "\",\"count\":%d}", b.count);
			out.concat(buf);
		}
		out.concat("]");
	}

	out.concat("}");
	return TRUE;
}

void GameTelemetry::clearBatch()
{
	m_buffer.clear();
	m_buckets.clear();
}

void GameTelemetry::flush()
{
	if (m_sessionId.isEmpty() || (m_buffer.empty() && m_buckets.empty()))
		return;

	AsciiString json;
	if (!buildBatchJson(json))
		return;

	// Hand off to the worker thread — the actual TCP send can block on a
	// slow network, and blocking the game thread in mid-frame would stall
	// the whole simulation. Buffer-build (synchronous, CPU-only) is the
	// game thread's only cost.
	enqueueSend(std::move(json));
	m_lastFlushMs = timeGetTime();
	clearBatch();
}

// ── Background sender ──────────────────────────────────────────

void GameTelemetry::startSendThread()
{
	if (m_sendThread.joinable())
		return;
	m_sendStop.store(false);
	m_sendThread = std::thread(&GameTelemetry::sendWorker, this);
}

void GameTelemetry::stopSendThread()
{
	if (!m_sendThread.joinable())
		return;
	{
		std::lock_guard<std::mutex> lk(m_sendMutex);
		m_sendStop.store(true);
	}
	m_sendCv.notify_all();
	m_sendThread.join();
}

void GameTelemetry::enqueueSend(AsciiString &&json)
{
	{
		std::lock_guard<std::mutex> lk(m_sendMutex);
		m_sendQueue.push_back(std::move(json));
	}
	m_sendCv.notify_one();
}

void GameTelemetry::sendWorker()
{
	for (;;)
	{
		AsciiString payload;
		{
			std::unique_lock<std::mutex> lk(m_sendMutex);
			m_sendCv.wait(lk, [this]() {
				return m_sendStop.load() || !m_sendQueue.empty();
			});
			if (m_sendQueue.empty())
			{
				if (m_sendStop.load())
					return;
				continue;
			}
			payload = std::move(m_sendQueue.front());
			m_sendQueue.pop_front();
		}

		// Send outside the lock — HTTP POST may block on a slow network,
		// and we don't want to hold up flush() enqueues on the game thread.
		// Each POST opens its own WinHTTP session so this is thread-safe
		// regardless of what other subsystems are doing.
		if (!payload.isEmpty() && g_authGameToken[0] != '\0')
		{
			AsciiString base  = RestClient::defaultBaseUrl();
			AsciiString token(g_authGameToken);
			RestClient::Response r = RestClient::post(
				base, AsciiString("/api/telemetry"), payload, token);
			if (!r.ok())
			{
				DEBUG_LOG(("GameTelemetry: POST /api/telemetry failed status=%d", r.httpStatus));
				// Per spec: drop the batch on failure. The worker just
				// loops back and waits for the next enqueue.
			}
		}
	}
}

void GameTelemetry::drainSendQueue(UnsignedInt timeoutMs)
{
	UnsignedInt deadline = timeGetTime() + timeoutMs;
	for (;;)
	{
		{
			std::lock_guard<std::mutex> lk(m_sendMutex);
			if (m_sendQueue.empty())
				return;
		}
		if (timeGetTime() >= deadline)
			return;
		// Short spin — the worker consumes queue items without prompting,
		// we're just waiting for them to drain. Anything smarter would
		// require another CV and isn't worth it for a 2-second worst case.
		Sleep(5);
	}
}
