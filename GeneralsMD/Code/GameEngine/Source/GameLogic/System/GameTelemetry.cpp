/**
 * GameTelemetry — build + ship the end-of-game GAMERESULT JSON through
 * the relay. Server-side consensus + score crediting lives in
 * Data/.../StatsService.TryFinaliseGameAsync.
 */

#include "PreRTS.h"

#include "GameLogic/GameTelemetry.h"

#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/ScoreKeeper.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/LANAPICallbacks.h"  // TheLAN

#include <objbase.h>  // CoCreateGuid

extern char g_authGameToken[];  // CommandLine.cpp

GameTelemetry *TheGameTelemetry = NULL;

// Minimal JSON-escape for the identifiers we emit (side / display name /
// map name). Control chars + quote + backslash only; non-ASCII never
// appears in these fields so we don't need UTF-8 escape handling.
static void appendJsonEscaped(AsciiString &dst, const char *src)
{
	if (!src) return;
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

// Mirrors GeneralsRemastered.Data.Entities.PlayerResult so the byte on
// the wire matches what StatsService expects.
enum : Int { RES_UNKNOWN = 0, RES_WIN = 1, RES_LOSS = 2, RES_DRAW = 3, RES_DISCONNECT = 4 };

GameTelemetry::GameTelemetry()
: m_active(FALSE)
, m_gameStartFrame(0)
, m_resultSent(FALSE)
{
}

GameTelemetry::~GameTelemetry() {}

void GameTelemetry::init()
{
	m_active = FALSE;
	m_sessionId.clear();
	m_gameStartFrame = 0;
	m_resultSent = FALSE;
}

void GameTelemetry::reset()
{
	m_active = FALSE;
	m_sessionId.clear();
	m_gameStartFrame = 0;
	m_resultSent = FALSE;
}

Int GameTelemetry::gameDurationSeconds() const
{
	if (!TheGameLogic) return 0;
	UnsignedInt frame = TheGameLogic->getFrame();
	UnsignedInt startFrame = (UnsignedInt)m_gameStartFrame;
	if (frame < startFrame) return 0;
	Real seconds = (Real)(frame - startFrame) * MSEC_PER_LOGICFRAME_REAL * 0.001f;
	return seconds > 0 ? (Int)seconds : 0;
}

void GameTelemetry::onGameStart()
{
	// No token = no launcher-auth = no relay to ship GAMERESULT to.
	if (g_authGameToken[0] == '\0') return;

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
	m_resultSent = FALSE;
	m_gameStartFrame = TheGameLogic ? (Int)TheGameLogic->getFrame() : 0;
}

// Per-player score numbers read from ScoreKeeper. Under lockstep /
// CRC-validated sim every peer's numbers agree, so the first report the
// server receives wins.
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

static void readScoreForPlayer(Player *subject, PerPlayerScore &out)
{
	memset(&out, 0, sizeof(out));
	if (!subject || !ThePlayerList)
		return;
	ScoreKeeper *sk = subject->getScoreKeeper();
	if (!sk)
		return;

	out.unitsBuilt     = sk->getTotalUnitsBuilt();
	out.unitsLost      = sk->getTotalUnitsLost();
	out.buildingsBuilt = sk->getTotalBuildingsBuilt();
	out.buildingsLost  = sk->getTotalBuildingsLost();
	out.moneyEarned    = sk->getTotalMoneyEarned();
	out.moneySpent     = sk->getTotalMoneySpent();

	// Kill attribution split by opponent type (human vs AI). We walk
	// ThePlayerList and sum getUnitsDestroyedByPlayer / getBuildingsDestroyedByPlayer
	// from the SUBJECT's ScoreKeeper — those accessors return "how many
	// of player i's things did SUBJECT kill".
	const Int subjectIdx = subject->getPlayerIndex();
	const Int playerCount = ThePlayerList->getPlayerCount();
	for (Int i = 1; i < playerCount; ++i)
	{
		if (i == subjectIdx)
			continue;  // Don't credit kills on own units
		Player *other = ThePlayerList->getNthPlayer(i);
		if (!other)
			continue;
		const Int u = sk->getUnitsDestroyedByPlayer(i);
		const Int b = sk->getBuildingsDestroyedByPlayer(i);
		switch (other->getPlayerType())
		{
			case PLAYER_HUMAN:
				out.unitsKilledHuman     += u;
				out.buildingsKilledHuman += b;
				break;
			case PLAYER_COMPUTER:
				out.unitsKilledAI        += u;
				out.buildingsKilledAI    += b;
				break;
			default: break;  // Neutral / civilian — don't count
		}
	}
}

void GameTelemetry::sendGameResultToRelay(Int localResult)
{
	if (!m_active || m_resultSent || m_sessionId.isEmpty() || !ThePlayerList)
		return;
	// Relay must be connected, otherwise the packet won't land anywhere.
	if (!TheLAN)
		return;

	Player *localPlayer = ThePlayerList->getLocalPlayer();
	if (!localPlayer)
		return;
	const Int localIdx = localPlayer->getPlayerIndex();

	AsciiString json;
	json.concat("{\"ExternalKey\":\"");
	appendJsonEscaped(json, m_sessionId.str());
	json.concat("\"");

	if (TheGlobalData && !TheGlobalData->m_mapName.isEmpty())
	{
		json.concat(",\"MapName\":\"");
		appendJsonEscaped(json, TheGlobalData->m_mapName.str());
		json.concat("\"");
	}

	char buf[128];
	sprintf(buf, ",\"DurationSeconds\":%d,\"Players\":[", gameDurationSeconds());
	json.concat(buf);

	// Walk every real player in the match (skip civilian / creeps / observer
	// bookkeeping entries that ThePlayerList includes internally). For each,
	// emit a PlayerReportEntry.
	Bool first = TRUE;
	const Int playerCount = ThePlayerList->getPlayerCount();
	for (Int i = 0; i < playerCount; ++i)
	{
		Player *p = ThePlayerList->getNthPlayer(i);
		if (!p)
			continue;

		// Filter: only real participants. We use the display name being
		// non-empty + non-neutral player type as the signal. Civilian /
		// Neutral types carry an empty or bookkeeping name.
		AsciiString displayName;
		displayName.translate(p->getPlayerDisplayName());
		if (displayName.isEmpty())
			continue;
		const PlayerType pt = p->getPlayerType();
		if (pt != PLAYER_HUMAN && pt != PLAYER_COMPUTER)
			continue;

		// Result resolution:
		//   Local player: use the argument passed in by the terminal hook.
		//   Others: dead/defeated → Loss; alive → opposite of local (Win
		//   if local lost, Loss if local won). This is a best-effort read
		//   — the server's consensus engine collates reports from every
		//   peer and lands on a majority-agreed result even if individual
		//   reports disagree at the edges.
		Int playerResult;
		if (i == localIdx)
		{
			playerResult = localResult;
		}
		else if (p->isPlayerDead())
		{
			playerResult = RES_LOSS;
		}
		else
		{
			playerResult = (localResult == RES_WIN) ? RES_LOSS
			            : (localResult == RES_LOSS ? RES_WIN : RES_UNKNOWN);
		}

		// Side — the PlayerTemplate's side string (e.g. "America",
		// "ChinaTankGeneral"). Empty for placeholder/observer templates.
		AsciiString sideStr;
		const PlayerTemplate *tmpl = p->getPlayerTemplate();
		if (tmpl)
			sideStr = tmpl->getSide();

		// Team number from GameInfo when available (lobby slot), else -1
		// (server treats as null).
		Int teamNum = -1;
		if (TheGameInfo)
		{
			for (Int s = 0; s < MAX_SLOTS; ++s)
			{
				GameSlot *slot = TheGameInfo->getSlot(s);
				if (!slot) continue;
				AsciiString slotName;
				slotName.translate(slot->getName());
				if (slotName.compare(displayName.str()) == 0)
				{
					teamNum = slot->getTeamNumber();
					break;
				}
			}
		}

		PerPlayerScore s;
		readScoreForPlayer(p, s);

		if (!first) json.concat(",");
		first = FALSE;

		json.concat("{\"DisplayName\":\"");
		appendJsonEscaped(json, displayName.str());
		json.concat("\"");

		if (!sideStr.isEmpty())
		{
			json.concat(",\"Side\":\"");
			appendJsonEscaped(json, sideStr.str());
			json.concat("\"");
		}
		if (teamNum >= 0)
		{
			sprintf(buf, ",\"Team\":%d", teamNum);
			json.concat(buf);
		}

		sprintf(buf,
		        ",\"Result\":%d"
		        ",\"UnitsBuilt\":%d,\"UnitsLost\":%d"
		        ",\"UnitsKilledHuman\":%d,\"UnitsKilledAI\":%d"
		        ",\"BuildingsBuilt\":%d,\"BuildingsLost\":%d"
		        ",\"BuildingsKilledHuman\":%d,\"BuildingsKilledAI\":%d"
		        ",\"MoneyEarned\":%d,\"MoneySpent\":%d}",
		        playerResult,
		        s.unitsBuilt, s.unitsLost,
		        s.unitsKilledHuman, s.unitsKilledAI,
		        s.buildingsBuilt, s.buildingsLost,
		        s.buildingsKilledHuman, s.buildingsKilledAI,
		        s.moneyEarned, s.moneySpent);
		json.concat(buf);
	}

	json.concat("]}");

	TheLAN->relaySendGameResult(json.str(), json.getLength());
	m_resultSent = TRUE;
	m_active = FALSE;
}

void GameTelemetry::onGameWon()         { sendGameResultToRelay(RES_WIN); }
void GameTelemetry::onGameLost()        { sendGameResultToRelay(RES_LOSS); }
void GameTelemetry::onGameSurrendered() { sendGameResultToRelay(RES_LOSS); }
void GameTelemetry::onGameExited()      { sendGameResultToRelay(RES_DISCONNECT); }
