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
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Display.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/LANAPICallbacks.h"  // TheLAN

#include <chrono>
#include <shlobj.h>    // SHGetFolderPathA for %LOCALAPPDATA%
#include <cstdio>
#include <cstdarg>
#include <mutex>

extern char g_authGameToken[];  // CommandLine.cpp

// ---------------------------------------------------------------------------
// Triage trace: writes a line to a fixed absolute path whenever a key
// GameTelemetry event fires. DEBUG_LOG is stripped from ReleasePublic so
// this is the only way to see what's going on. Path is
//   %LOCALAPPDATA%\Generals64\telemetry-trace.log
// which is always writable regardless of the engine's CWD / elevation
// state. Handle is opened once at first use and held open with line-
// buffered flushing so a crash still leaves the last few lines on disk.
// ---------------------------------------------------------------------------
static std::mutex  g_traceMutex;
static FILE       *g_traceFile = nullptr;
static bool        g_traceTried = false;

static FILE *telemetryTraceFile()
{
	if (g_traceFile || g_traceTried) return g_traceFile;
	g_traceTried = true;

	char localAppData[MAX_PATH];
	if (FAILED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData)))
		return nullptr;

	char dir[MAX_PATH];
	_snprintf(dir, sizeof(dir), "%s\\Generals64", localAppData);
	dir[sizeof(dir) - 1] = '\0';
	CreateDirectoryA(dir, nullptr);  // harmless if it already exists

	char path[MAX_PATH];
	_snprintf(path, sizeof(path), "%s\\telemetry-trace.log", dir);
	path[sizeof(path) - 1] = '\0';

	FILE *f = nullptr;
	if (fopen_s(&f, path, "a") != 0 || !f) return nullptr;
	// Line-buffered flushing so a crash preserves the last lines.
	setvbuf(f, nullptr, _IOLBF, 1024);

	SYSTEMTIME st;
	GetLocalTime(&st);
	fprintf(f, "==== telemetry trace opened %04d-%02d-%02d %02d:%02d:%02d ====\n",
	        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	fflush(f);
	g_traceFile = f;
	return g_traceFile;
}

static void telemetryTrace(const char *fmt, ...)
{
	std::lock_guard<std::mutex> lk(g_traceMutex);
	FILE *f = telemetryTraceFile();
	if (!f) return;
	SYSTEMTIME st;
	GetLocalTime(&st);
	fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
	        st.wYear, st.wMonth, st.wDay,
	        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fputc('\n', f);
	fflush(f);
}

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

// One-shot flag: whether we've already attached a ThingFactory manifest
// to a GAMERESULT in this engine process. The full mapping is large
// (~500 entries, ~15KB of JSON) but static across the engine's life,
// so we ship it once and let the server catalog persist it across
// sessions and games.
static Bool s_thingManifestShipped = FALSE;

// Append a "<key>":{"thingId":count,...} fragment to <dst>, filtered
// to unit-kind ThingTemplates only (anything isKindOf(KINDOF_STRUCTURE)
// is treated as a building and skipped — buildings are tracked on the
// same page's future Building-Mastery surface, not here). Emits nothing
// — not even the key — when the filtered map is empty, so the caller
// can safely concat it and the comma-preserving writer above stays
// correct. Caller is responsible for the leading comma when appending
// after an existing field.
static void appendUnitThingIdCountMapJson(AsciiString &dst, const char *key,
	const std::map<const ThingTemplate *, Int> &counts)
{
	if (counts.empty()) return;

	Bool first = TRUE;
	char buf[48];
	AsciiString body;
	for (auto it = counts.begin(); it != counts.end(); ++it)
	{
		const ThingTemplate *t = it->first;
		const Int count = it->second;
		if (!t || count <= 0) continue;
		if (t->isKindOf(KINDOF_STRUCTURE)) continue;  // unit-kind only
		const UnsignedShort id = t->getTemplateID();
		if (id == 0) continue;
		if (!first) body.concat(",");
		first = FALSE;
		_snprintf(buf, sizeof(buf), "\"%u\":%d", (unsigned)id, count);
		buf[sizeof(buf) - 1] = '\0';
		body.concat(buf);
	}
	if (first) return;  // every entry filtered out

	dst.concat(",\"");
	dst.concat(key);
	dst.concat("\":{");
	dst.concat(body.str());
	dst.concat("}");
}

// Walk every ThingTemplate the factory knows about and append
//    "ThingManifest":{"1":"AmericaTankCrusader","2":"ChinaTankOverlord",...}
// to <dst>. Caller is responsible for placing the comma that precedes
// the "ThingManifest" key; this helper writes the key and value only.
// Called at most once per engine process (see s_thingManifestShipped).
static void appendThingManifestJson(AsciiString &dst)
{
	if (!TheThingFactory)
		return;
	dst.concat("\"ThingManifest\":{");
	Bool first = TRUE;
	char buf[32];
	for (const ThingTemplate *t = TheThingFactory->firstTemplate();
	     t != nullptr;
	     t = const_cast<ThingTemplate*>(t)->friend_getNextTemplate())
	{
		const UnsignedShort id = t->getTemplateID();
		if (id == 0) continue;          // engine uses 0 as invalid
		const AsciiString &name = t->getName();
		if (name.isEmpty()) continue;
		if (!first) dst.concat(",");
		first = FALSE;
		sprintf(buf, "\"%u\":\"", (unsigned)id);
		dst.concat(buf);
		appendJsonEscaped(dst, name.str());
		dst.concat("\"");
	}
	dst.concat("}");
}

GameTelemetry::GameTelemetry()
: m_active(FALSE)
, m_gameStartFrame(0)
, m_resultSent(FALSE)
, m_lastSnapshotFrame(0)
, m_lastPositionSnapshotBucket(-1)
, m_senderShouldStop(false)
{
}

GameTelemetry::~GameTelemetry()
{
	// Mirror the reset() teardown — if the thread is still up at
	// process shutdown, signal it, drain the queue, and join.
	if (m_senderThread.joinable())
	{
		m_senderShouldStop.store(true);
		m_outboxCV.notify_all();
		m_senderThread.join();
	}
}

void GameTelemetry::init()
{
	m_active = FALSE;
	m_sessionId.clear();
	m_pendingAssignId.clear();
	m_gameStartFrame = 0;
	m_resultSent = FALSE;
	m_lastSnapshotFrame = 0;
	m_lastPositionSnapshotBucket = -1;
	m_fpsMinuteIndex = 0;
	m_fpsSampleCount = 0;
	m_fpsSumX100 = 0;
	m_fpsMinX100 = 0;
	m_fpsMaxX100 = 0;
	// Note: sender thread is NOT started here — onGameStart owns that
	// so a sandbox/campaign run with no auth token never spawns it.
}

void GameTelemetry::reset()
{
	m_active = FALSE;
	m_sessionId.clear();
	m_pendingAssignId.clear();
	m_gameStartFrame = 0;
	m_resultSent = FALSE;
	m_lastSnapshotFrame = 0;
	m_lastPositionSnapshotBucket = -1;
	m_fpsMinuteIndex = 0;
	m_fpsSampleCount = 0;
	m_fpsSumX100 = 0;
	m_fpsMinX100 = 0;
	m_fpsMaxX100 = 0;

	// Stop the sender thread cleanly. The worker loop drains any
	// remaining queued packets (waiting on the CV under the same
	// shutdown flag) before it returns.
	if (m_senderThread.joinable())
	{
		m_senderShouldStop.store(true);
		m_outboxCV.notify_all();
		m_senderThread.join();
	}
	m_senderShouldStop.store(false);
	{
		std::lock_guard<std::mutex> lk(m_outboxMutex);
		m_outboxQueue.clear();
	}
}

// ── Sender thread ──────────────────────────────────────────────────
//
// Producer (sim thread): copies the bytes into a vector, pushes onto
// the deque under the mutex, notifies the CV. Returns immediately —
// no network I/O on the sim thread.
void GameTelemetry::enqueuePacket(const UnsignedByte *buf, Int len)
{
	if (!buf || len <= 0)
		return;
	std::vector<UnsignedByte> pkt(buf, buf + len);
	{
		std::lock_guard<std::mutex> lk(m_outboxMutex);
		m_outboxQueue.push_back(std::move(pkt));
	}
	m_outboxCV.notify_one();
}

// Consumer: drain one packet at a time, run the blocking
// relaySendAll() off the sim thread. Loops until shutdown flag is
// set AND the queue is empty so any in-flight terminal packets
// land before we exit.
void GameTelemetry::senderThreadFn()
{
	for (;;)
	{
		std::vector<UnsignedByte> pkt;
		{
			std::unique_lock<std::mutex> lk(m_outboxMutex);
			m_outboxCV.wait(lk, [this] {
				return !m_outboxQueue.empty() || m_senderShouldStop.load();
			});
			if (m_outboxQueue.empty() && m_senderShouldStop.load())
				return;
			pkt = std::move(m_outboxQueue.front());
			m_outboxQueue.pop_front();
		}
		if (TheLAN && !pkt.empty())
		{
			TheLAN->relaySendAll((const char *)pkt.data(), (int)pkt.size());
		}
	}
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
	telemetryTrace("onGameStart: token=%s TheLAN=%s TheGameLogic=%s ThePlayerList=%s",
	               g_authGameToken[0] == '\0' ? "EMPTY" : "present",
	               TheLAN          ? "ok" : "null",
	               TheGameLogic    ? "ok" : "null",
	               ThePlayerList   ? "ok" : "null");

	// No token = no launcher-auth = no relay to ship GAMERESULT to.
	if (g_authGameToken[0] == '\0')
	{
		telemetryTrace("  bail: auth token empty (not launched via launcher, or -auth not parsed)");
		return;
	}

	// Session ID is assigned by the relay, not minted locally. The
	// relay broadcasts RELAY_TYPE_SESSION_ASSIGN to every peer on the
	// filter code right after it handles MSG_GAME_START; that packet
	// lands in onRelayAssignSession which sets m_sessionId. Until
	// then, every sender path short-circuits on m_sessionId.isEmpty()
	// — which means the first ~logic frames after start are silent
	// even if the normal SCORE_EVENTS cadence triggers. That gap is
	// bounded by the arm-timeout in onLogicFrame.
	//
	// Race on the host: MSG_GAME_START is sent from lobby code and
	// the ASSIGN broadcast can round-trip back while the engine is
	// still finishing its lobby→game transition — arriving BEFORE
	// we get here. onRelayAssignSession catches that case by stashing
	// into m_pendingAssignId, and we adopt it below.
	if (!m_pendingAssignId.isEmpty())
	{
		m_sessionId = m_pendingAssignId;
		m_pendingAssignId.clear();
		telemetryTrace("  adopted pre-arrival ASSIGN sid=%s", m_sessionId.str());
	}
	else
	{
		m_sessionId.clear();
	}
	m_active = TRUE;
	m_resultSent = FALSE;
	m_gameStartFrame = TheGameLogic ? (Int)TheGameLogic->getFrame() : 0;
	m_lastSnapshotFrame = 0;
	m_lastPositionSnapshotBucket = -1;
	m_fpsMinuteIndex = 0;
	m_fpsSampleCount = 0;
	m_fpsSumX100 = 0;
	m_fpsMinX100 = 0;
	m_fpsMaxX100 = 0;

	// Spin up the telemetry sender thread now that we're armed and
	// need to ship packets the moment the relay hands us a session
	// ID. Guard against a stale thread from a previous game leaking
	// through (reset() should have torn it down, but be defensive).
	if (!m_senderThread.joinable())
	{
		m_senderShouldStop.store(false);
		m_senderThread = std::thread(&GameTelemetry::senderThreadFn, this);
		telemetryTrace("  armed: m_active=TRUE awaiting RELAY_TYPE_SESSION_ASSIGN; sender thread spawned");
	}
	else
	{
		telemetryTrace("  armed: m_active=TRUE awaiting RELAY_TYPE_SESSION_ASSIGN (sender thread already running)");
	}
}

void GameTelemetry::onRelayAssignSession(const UnsignedByte bytes[16])
{
	if (!bytes)
		return;

	// Hex-encode the 16 bytes in the same order GuidBytesMatchNFormat
	// expects on the way back (textual "N" format — bytes serialised
	// in order, no .NET little-endian reshuffle). The engine then
	// embeds the raw bytes right back into every SCORE_EVENTS payload
	// and the hex form into GAMERESULT's ExternalKey; a round trip
	// through both hex + bytes must match what the relay sent.
	char sid[33];
	static const char kHex[] = "0123456789abcdef";
	for (Int i = 0; i < 16; ++i)
	{
		sid[i * 2]     = kHex[(bytes[i] >> 4) & 0x0F];
		sid[i * 2 + 1] = kHex[ bytes[i]       & 0x0F];
	}
	sid[32] = '\0';

	// Pre-arrival case: ASSIGN beat onGameStart (see the note in
	// onGameStart for the host-side race). Stash in the pending
	// buffer and return — onGameStart will adopt.
	if (!m_active)
	{
		m_pendingAssignId.set(sid);
		telemetryTrace("onRelayAssignSession: pre-arrival; buffered sid=%s (m_active=0)", sid);
		return;
	}

	// Already armed. Idempotent for the same ID; a duplicate ASSIGN
	// (reconnect, relay retry) just no-ops. A different ID arriving
	// while we already have one means the relay moved us to a new
	// match without a clean onGameStart — log + adopt; better to
	// ship under the new ID than to silently keep talking about the
	// old one.
	if (!m_sessionId.isEmpty())
	{
		if (m_sessionId.compare(sid) == 0)
		{
			telemetryTrace("onRelayAssignSession: duplicate assign for %s ignored", sid);
			return;
		}
		telemetryTrace("onRelayAssignSession: session changed %s -> %s; resetting cadence",
		               m_sessionId.str(), sid);
		m_lastSnapshotFrame = 0;
	}
	m_sessionId.set(sid);
	telemetryTrace("onRelayAssignSession: sid=%s m_active=1", sid);
}

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

	// Force a terminal SCORE_EVENTS batch before GAMERESULT ships. The
	// periodic onLogicFrame batch only runs every SCORE_EVENTS_EVERY_FRAMES
	// (~3s), so a game that ends before the first tick (short matches,
	// rage-quits) would otherwise never credit its score totals to the
	// User rollup — the server's StatsService doesn't read score fields
	// out of GAMERESULT, so without this forced send the totals stay
	// zero for short games. Bypasses the normal throttle by zeroing
	// the last-frame sentinel; absolute snapshots are idempotent so a
	// duplicate emission at the same frame is safe.
	m_lastSnapshotFrame = 0;
	onLogicFrame();

	// Terminal position snapshot — captures the final battlefield
	// state for the replay/heatmap surface. Tagged "game_end" so
	// the dashboard can distinguish it from periodic snapshots.
	snapshotPositions("game_end", -1);

	// Also flush the in-flight FPS bucket — same rationale as the
	// SCORE_EVENTS terminal flush above. A game that ends inside its
	// first minute otherwise loses its sole bucket entirely. The
	// onLogicFrame call above already folded the final sample into
	// m_fps*; this just packs + ships what's there.
	shipAndResetFpsBucket();

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

	// Sized to hold the longest sprintf below — the 11-field per-player
	// score block is ~260 chars (184 literal + up to 11 × 11-char ints).
	// Plus headroom for future fields. This buffer is reused inside the
	// player loop; a 128-byte buffer corrupted the stack canary and crashed
	// at function return with __report_gsfailure (observed Apr 2026).
	char buf[512];
	_snprintf(buf, sizeof(buf), ",\"DurationSeconds\":%d,\"Players\":[", gameDurationSeconds());
	buf[sizeof(buf) - 1] = '\0';
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

		// Side — the PlayerTemplate's side string (e.g. "America",
		// "ChinaTankGeneral"). Empty for placeholder/observer templates.
		AsciiString sideStr;
		const PlayerTemplate *tmpl = p->getPlayerTemplate();
		if (tmpl)
			sideStr = tmpl->getSide();

		// Team number from GameInfo when available (lobby slot), else -1
		// (server treats as null). While we're walking the slots we also
		// capture the ORIGINAL template the player picked in the lobby —
		// if it was PLAYERTEMPLATE_RANDOM we preserve that fact as a
		// separate flag on the wire, while Side above keeps the resolved
		// faction the sim actually ran. Without this split the "I rolled
		// China off random" case becomes indistinguishable from "I picked
		// China deliberately" once the report hits the server.
		//
		// Team-aware result resolution downstream needs the local
		// player's team number too — capture it here once and reuse.
		Int teamNum = -1;
		Bool wasRandom = FALSE;
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
					wasRandom = (slot->getOriginalPlayerTemplate() == PLAYERTEMPLATE_RANDOM);
					break;
				}
			}
		}

		// Local player's team number — one-time lookup outside this
		// per-iteration block via a static-style cache. Cheap because
		// MAX_SLOTS is 8.
		Int localTeam = -1;
		if (TheGameInfo && localPlayer)
		{
			AsciiString localName;
			localName.translate(localPlayer->getPlayerDisplayName());
			for (Int s = 0; s < MAX_SLOTS; ++s)
			{
				GameSlot *slot = TheGameInfo->getSlot(s);
				if (!slot) continue;
				AsciiString slotName;
				slotName.translate(slot->getName());
				if (slotName.compare(localName.str()) == 0)
				{
					localTeam = slot->getTeamNumber();
					break;
				}
			}
		}

		// Result resolution (team-aware, per-perspective truth):
		//   Local player: use the argument passed in by the terminal hook.
		//   Dead: LOSS regardless of team — eliminated is eliminated.
		//   Same team as me, I won: teammate also won (team-shared
		//          victory under Generals rules).
		//   Same team as me, I lost / disconnected: teammate's fate is
		//          UNKNOWN at this instant — they may go on to win the
		//          match for the team. The server post-processes
		//          outcomes by priority: WIN > LOSS > DISCONNECT >
		//          UNKNOWN, and any reporter who DID see the teammate
		//          finish the match overrides this UNKNOWN report.
		//   Different team, alive: opposite of local outcome (the
		//          inter-team case unchanged).
		Int playerResult;
		if (i == localIdx)
		{
			playerResult = localResult;
		}
		else if (p->isPlayerDead())
		{
			playerResult = RES_LOSS;
		}
		else if (teamNum >= 0 && teamNum == localTeam)
		{
			if (localResult == RES_WIN)
				playerResult = RES_WIN;       // team won, teammate won
			else
				playerResult = RES_UNKNOWN;   // teammate's fate not yet decided from my POV
		}
		else
		{
			playerResult = (localResult == RES_WIN) ? RES_LOSS
			            : (localResult == RES_LOSS ? RES_WIN : RES_UNKNOWN);
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
			_snprintf(buf, sizeof(buf), ",\"Team\":%d", teamNum);
			buf[sizeof(buf) - 1] = '\0';
			json.concat(buf);
		}
		if (wasRandom)
			json.concat(",\"WasRandom\":true");

		_snprintf(buf, sizeof(buf),
		        ",\"Result\":%d"
		        ",\"UnitsBuilt\":%d,\"UnitsLost\":%d"
		        ",\"UnitsKilledHuman\":%d,\"UnitsKilledAI\":%d"
		        ",\"BuildingsBuilt\":%d,\"BuildingsLost\":%d"
		        ",\"BuildingsKilledHuman\":%d,\"BuildingsKilledAI\":%d"
		        ",\"MoneyEarned\":%d,\"MoneySpent\":%d",
		        playerResult,
		        s.unitsBuilt, s.unitsLost,
		        s.unitsKilledHuman, s.unitsKilledAI,
		        s.buildingsBuilt, s.buildingsLost,
		        s.buildingsKilledHuman, s.buildingsKilledAI,
		        s.moneyEarned, s.moneySpent);
		buf[sizeof(buf) - 1] = '\0';
		json.concat(buf);

		// Per-unit-type kill + loss maps — drives the "top destroyers"
		// and "survival rate" breakdowns on the Mastery/Units page.
		// Only ships when the underlying ScoreKeeper has at least one
		// unit-kind entry; the helper itself emits nothing on empty so
		// short / no-kill games don't bloat the payload.
		if (ScoreKeeper *sk = p->getScoreKeeper())
		{
			// Sum kills across every victim slot into a single map
			// (thingId is identifying the VICTIM's template; which slot
			// owned that victim doesn't matter for mastery stats, only
			// the unit type does). Self-kills already suppressed server-
			// side by ScoreKeeper::addObjectDestroyed's player-index
			// tracking, so we sum every slot unconditionally.
			std::map<const ThingTemplate *, Int> killsByType;
			for (Int v = 0; v < MAX_PLAYER_COUNT; ++v)
			{
				const auto &m = sk->getObjectsDestroyedAgainst(v);
				for (auto it = m.begin(); it != m.end(); ++it)
					killsByType[it->first] += it->second;
			}
			appendUnitThingIdCountMapJson(json, "UnitKills", killsByType);
			appendUnitThingIdCountMapJson(json, "UnitsLostByType", sk->getObjectsLost());
		}

		json.concat("}");
	}

	json.concat("]");

	// First GAMERESULT of this engine process: attach the ThingFactory
	// id → name manifest so the server can resolve wire-level numeric
	// ids (observation builds, favourite-unit dashboard) to template
	// names without the engine ever having to emit names on the hot
	// observation path. Skipped on subsequent results — the server
	// catalogs the manifest once and it remains valid until the engine
	// reloads its INI (which doesn't happen in a normal session).
	if (!s_thingManifestShipped)
	{
		json.concat(",");
		appendThingManifestJson(json);
		s_thingManifestShipped = TRUE;
	}

	json.concat("}");

	// Pack on the sim thread, enqueue for the sender thread to ship.
	// Bypasses TheLAN->relaySendGameResult's wrapper so relaySendAll
	// is only ever invoked on the telemetry thread.
	std::vector<UnsignedByte> packet;
	TheLAN->packGameResultPacket(json.str(), json.getLength(), packet);
	if (!packet.empty())
		enqueuePacket(packet.data(), (Int)packet.size());

	m_resultSent = TRUE;
	m_active = FALSE;
}

// Emit a "match_outcome" GameEvent from the local player's
// perspective. Carries the explicit outcome string (win/loss/
// disconnect) in extraJson so the server can drive
// GamePlayer.Result without inferring from inferences. Multi-
// reporter — every peer fires this for their own outcome; the
// server picks the latest report per player as authoritative.
static void emitMatchOutcomeEvent(const char *outcome, const char *reason = nullptr)
{
	if (!TheGameTelemetry || !ThePlayerList) return;
	Player *local = ThePlayerList->getLocalPlayer();
	if (!local) return;

	AsciiString extra;
	extra.concat("{\"outcome\":\"");
	extra.concat(outcome);
	extra.concat("\"");
	if (reason && *reason)
	{
		extra.concat(",\"reason\":\"");
		extra.concat(reason);
		extra.concat("\"");
	}
	extra.concat("}");

	TheGameTelemetry->emitEvent(
		"match_outcome",
		/*actorSlot*/   local->getPlayerIndex(),
		/*targetSlot*/  -1,
		/*tid*/         -1,
		/*x*/           INT_MIN,
		/*y*/           INT_MIN,
		/*cash*/        INT_MIN,
		/*extraJson*/   extra.str());
}

void GameTelemetry::onGameWon()
{
	emitMatchOutcomeEvent("win");
	sendGameResultToRelay(RES_WIN);
}
void GameTelemetry::onGameLost()
{
	emitMatchOutcomeEvent("loss");
	sendGameResultToRelay(RES_LOSS);
}
void GameTelemetry::onGameSurrendered()
{
	emitMatchOutcomeEvent("loss", "surrender");
	sendGameResultToRelay(RES_LOSS);
}
void GameTelemetry::onGameExited()
{
	// Capture the leaving peer's final view BEFORE the GAMERESULT
	// flush so the audit trail records what they saw right before
	// going down. The peers still in the game will independently
	// emit "client_disconnected" snapshots when their netcode
	// timeout fires.
	if (ThePlayerList)
	{
		Player *local = ThePlayerList->getLocalPlayer();
		Int slot = local ? local->getPlayerIndex() : -1;
		snapshotPositions("player_exited", slot);
	}
	emitMatchOutcomeEvent("disconnect", "exit");
	sendGameResultToRelay(RES_DISCONNECT);
}

// ── Periodic score events (multi-reporter, absolute snapshots) ────
//
// Wire format behind the standard [4:size][4:sessionID][1:type=9]
// relay header. Variable length: every peer ships absolute totals for
// every observable player in the match, so under lockstep sim each
// peer's payload is independently authoritative for the whole game.
// The server keeps every reporter's view; the canonical (self-)report
// is what the dashboard displays, with the others retained for cross-
// peer audit / divergence detection (anti-cheat).
//
// Header (30 bytes):
//    [16] session GUID, raw bytes (not hex) — derived from m_sessionId
//    [ 1] reporter slot index (the local player's slot)
//    [ 4] current logic frame           (Int32 LE)
//    [ 8] wall-clock UTC milliseconds   (Int64 LE) at pack time
//    [ 1] block count N (1..MAX_SLOTS)
//
// Then N blocks of 41 bytes each:
//    [ 1] observed slot
//    [ 4] unitsBuilt                    (Int32 LE) ABSOLUTE total
//    [ 4] unitsLost                                ABSOLUTE
//    [ 4] unitsKilledHuman                         ABSOLUTE
//    [ 4] unitsKilledAI                            ABSOLUTE
//    [ 4] buildingsBuilt                           ABSOLUTE
//    [ 4] buildingsLost                            ABSOLUTE
//    [ 4] buildingsKilledHuman                     ABSOLUTE
//    [ 4] buildingsKilledAI                        ABSOLUTE
//    [ 4] moneyEarned                              ABSOLUTE
//    [ 4] moneySpent                               ABSOLUTE
//
// Total: 30 + 41*N bytes. Min 71 (1 observed), max 358 (8 observed).
// Must match the bounds check in RelayServer.cs — drift = packet
// dropped as "bad payload size". Absolutes are idempotent; a retry of
// the same (Reporter, Observed, Frame) overwrites server-side rather
// than double-crediting.

// ~3 s at 30 Hz. Lowered from the old 300-frame (~10 s) snapshot
// cadence: each batch is now small (10 deltas, mostly zero in idle
// stretches) so the dashboard can feel live without flooding the
// relay. ~20 packets/minute per peer, still trivial vs. lockstep
// netcode.
static const Int SCORE_EVENTS_EVERY_FRAMES = 90;

// Parse one hex char 0..9/a..f/A..F. Returns -1 on failure.
static Int hexVal(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
	if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
	return -1;
}

// Convert m_sessionId (32 hex chars) into 16 raw bytes. Returns FALSE
// on malformed input — caller skips this snapshot.
static Bool sessionIdToBytes(const AsciiString &sid, UnsignedByte out[16])
{
	if (sid.getLength() != 32)
		return FALSE;
	const char *p = sid.str();
	for (Int i = 0; i < 16; ++i)
	{
		Int hi = hexVal(p[i * 2]);
		Int lo = hexVal(p[i * 2 + 1]);
		if (hi < 0 || lo < 0)
			return FALSE;
		out[i] = (UnsignedByte)((hi << 4) | lo);
	}
	return TRUE;
}

// ── Sim-object position snapshots ─────────────────────────────────
//
// Walks TheGameLogic's object list, packs an 8-byte record for every
// observable object into a payload buffer, and ships as
// RELAY_TYPE_POSITION_SNAPSHOT. The wire is per-object:
//   ownerSlot:u8 | templateId:u16le | xQ:i16le | yQ:i16le | stateBits:u8
// where x/y are quantized as engineCoord / 16 (~16 world-unit
// resolution — fine for a minimap; the map fits in [-32768,32767]
// after the divide for any plausible Generals map).
//
// Best-effort: per-object reads are wrapped in try/catch so a snapshot
// taken during desync (potentially mid-mutation state) doesn't crash
// the sim. Failures emit nothing; the row is preserved as a partial
// capture (the partial flag is implicit in record count vs. the live
// scene size, which the dashboard doesn't compare).
void GameTelemetry::snapshotPositions(const char *triggerKind, Int triggerSlot)
{
	if (!triggerKind || !*triggerKind)
		return;
	if (!m_active || m_resultSent)
		return;
	if (!TheLAN || !TheGameLogic || m_sessionId.isEmpty())
		return;

	UnsignedByte sidBytes[16];
	if (!sessionIdToBytes(m_sessionId, sidBytes))
		return;
	const Int currentFrame = (Int)TheGameLogic->getFrame();
	const Int64 utcMillis =
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();

	// Map bounds. TerrainLogic's getExtent is a virtual stub on the
	// base class; fall back to a wide default so the decoder still
	// has consistent values to work against. Any real implementation
	// can populate the bounds when it's wired up.
	Int mapMinX = 0, mapMinY = 0, mapMaxX = 16384, mapMaxY = 16384;

	// Quantization scale: divide engine world units by 16 → 1 record
	// dimension fits in i16 for maps up to ~524288 world units, well
	// past every Zero Hour map.
	const Int Q = 16;

	// Build records buffer. Cap at 8192 records (matches relay
	// validation) — at 200 units/player × 8 players = 1600 expected
	// peak, the cap is 5× the steady-state ceiling.
	const Int MAX_RECORDS = 8192;
	std::vector<UnsignedByte> records;
	records.reserve(MAX_RECORDS * 8);

	Int recordCount = 0;
	for (Object *obj = TheGameLogic->getFirstObject();
	     obj != nullptr && recordCount < MAX_RECORDS;
	     obj = obj->getNextObject())
	{
		try
		{
			const ThingTemplate *tmpl = obj->getTemplate();
			if (!tmpl) continue;
			const UnsignedShort tid = tmpl->getTemplateID();
			if (tid == 0) continue;
			Player *owner = obj->getControllingPlayer();
			Int ownerSlot = owner ? owner->getPlayerIndex() : 0;
			if (ownerSlot < 0 || ownerSlot > 255) continue;

			const Coord3D *pos = obj->getPosition();
			if (!pos) continue;
			Int xQ = (Int)(pos->x / Q);
			Int yQ = (Int)(pos->y / Q);
			if (xQ < -32768) xQ = -32768; else if (xQ > 32767) xQ = 32767;
			if (yQ < -32768) yQ = -32768; else if (yQ > 32767) yQ = 32767;

			// stateBits packs object-state hints for the dashboard
			// without needing a separate per-object lookup later.
			//   bit 0: KINDOF_STRUCTURE (vs. unit)
			//   bit 1: under construction
			//   bit 2: dead / dying (caller may snapshot during onDie)
			//   bits 3-7: reserved
			UnsignedByte stateBits = 0;
			if (tmpl->isKindOf(KINDOF_STRUCTURE))            stateBits |= 0x01;
			if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION)) stateBits |= 0x02;
			if (obj->isEffectivelyDead())                    stateBits |= 0x04;

			UnsignedByte rec[8];
			rec[0] = (UnsignedByte)(ownerSlot & 0xFF);
			rec[1] = (UnsignedByte)(tid & 0xFF);
			rec[2] = (UnsignedByte)((tid >> 8) & 0xFF);
			short sx = (short)xQ, sy = (short)yQ;
			memcpy(rec + 3, &sx, 2);
			memcpy(rec + 5, &sy, 2);
			rec[7] = stateBits;

			records.insert(records.end(), rec, rec + 8);
			recordCount++;
		}
		catch (...)
		{
			// Skip this object on any read failure (desync trigger
			// can race state mutations). The remaining objects
			// still contribute to the snapshot.
			continue;
		}
	}

	if (recordCount == 0)
	{
		telemetryTrace("snapshotPositions: empty scene; nothing to ship");
		return;
	}

	// Build the variable-length payload.
	const Int triggerKindLen = (Int)strlen(triggerKind);
	if (triggerKindLen <= 0 || triggerKindLen > 32)
		return;

	const Int headerSize = 16 + 1 + 4 + 8 + 1 + triggerKindLen + 1 + 16 + 4;
	std::vector<UnsignedByte> payload;
	payload.resize((size_t)headerSize + records.size());

	Int off = 0;
	memcpy(payload.data() + off, sidBytes, 16); off += 16;
	payload[off++] = (UnsignedByte)0;  // reporterSlot — server uses AuthUserId
	{
		Int frame = currentFrame;
		memcpy(payload.data() + off, &frame, 4);     off += 4;
		memcpy(payload.data() + off, &utcMillis, 8); off += 8;
	}
	payload[off++] = (UnsignedByte)triggerKindLen;
	memcpy(payload.data() + off, triggerKind, triggerKindLen); off += triggerKindLen;
	payload[off++] = (UnsignedByte)((triggerSlot >= 0 && triggerSlot <= 255) ? triggerSlot : 0xFF);

	memcpy(payload.data() + off, &mapMinX, 4); off += 4;
	memcpy(payload.data() + off, &mapMinY, 4); off += 4;
	memcpy(payload.data() + off, &mapMaxX, 4); off += 4;
	memcpy(payload.data() + off, &mapMaxY, 4); off += 4;

	memcpy(payload.data() + off, &recordCount, 4); off += 4;
	memcpy(payload.data() + off, records.data(), records.size());

	std::vector<UnsignedByte> packet;
	TheLAN->packPositionSnapshotPacket(payload.data(), (Int)payload.size(), packet);
	if (!packet.empty())
	{
		enqueuePacket(packet.data(), (Int)packet.size());
		telemetryTrace("enqueued POSITION_SNAPSHOT frame=%d trigger=%s records=%d size=%d",
		               currentFrame, triggerKind, recordCount, (int)packet.size());
	}
}

// ── Detailed in-game event log ────────────────────────────────────
//
// Best-effort hook called from anywhere in the engine where an event
// fires (object created/destroyed, upgrade/science purchased,
// superpower fired, …). The sim thread accumulates events in a small
// buffer; onLogicFrame's tick flushes the buffer as one
// RELAY_TYPE_GAME_EVENTS packet alongside the SCORE_EVENTS packet.
//
// Cap on the in-flight buffer so a runaway emit loop can't OOM the
// engine. Above the cap we silently drop new events — analytics is
// best-effort, and missing one event is preferable to crashing the
// game over telemetry.
static const size_t MAX_PENDING_EVENTS = 512;

void GameTelemetry::emitEvent(const char *eventType,
	Int actorSlot, Int targetSlot, Int thingTemplateId,
	Int x, Int y, Int cash, const char *extraJson)
{
	// Defensive: never propagate failures from telemetry to the
	// caller. The engine's hot paths can call this without any
	// awareness of telemetry state.
	if (!eventType || !*eventType)
		return;
	if (!m_active || m_resultSent)
		return;

	std::lock_guard<std::mutex> lk(m_pendingEventsMutex);
	if (m_pendingEvents.size() >= MAX_PENDING_EVENTS)
		return;  // buffer full — silent drop

	PendingEvent ev;
	ev.frame           = TheGameLogic ? (Int)TheGameLogic->getFrame() : 0;
	ev.type.set(eventType);
	ev.actorSlot       = actorSlot;
	ev.targetSlot      = targetSlot;
	ev.thingTemplateId = thingTemplateId;
	ev.x               = x;
	ev.y               = y;
	ev.cash            = cash;
	if (extraJson && *extraJson)
	{
		// Defensive: extraJson must be a complete JSON value
		// (object or array). emitEvent's contract paste it
		// verbatim after `"extra":` in the wire body, so a
		// key-value fragment like `"x":1` would produce
		// `"extra":"x":1` and the relay's parser would reject
		// the whole batch. Skip the leading whitespace and
		// require '{' or '[' as the first non-space char.
		const char *p = extraJson;
		while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
		if (*p == '{' || *p == '[')
		{
			ev.extraJson.set(extraJson);
		}
		else
		{
			telemetryTrace("emitEvent: dropping malformed extraJson for type=%s "
			               "(must start with '{' or '['); first char=0x%02x",
			               eventType, (unsigned)(unsigned char)*p);
		}
	}
	m_pendingEvents.push_back(std::move(ev));
}

// Pack the in-flight events buffer as a RELAY_TYPE_GAME_EVENTS packet
// and enqueue it on the sender thread. No-op if the buffer is empty.
//
// Wire body (UTF-8 JSON):
//   {"events":[
//     {"frame":1234,"type":"object_created","actor":2,"tid":17},
//     {"frame":1240,"type":"superpower_fired","actor":1,"x":1024,"y":2048},
//     ...
//   ]}
//
// Hard cap on serialised JSON at 16 KB to match the relay's accept
// envelope. If the encoded body would exceed that we ship what we
// have so far and leave the rest in the buffer for the next tick —
// better to land most of the events than to drop the whole batch.
void GameTelemetry::shipPendingEventsBatch()
{
	if (!m_active || m_resultSent)
		return;
	if (!TheLAN || m_sessionId.isEmpty())
		return;

	std::vector<PendingEvent> batch;
	{
		std::lock_guard<std::mutex> lk(m_pendingEventsMutex);
		if (m_pendingEvents.empty())
			return;
		batch.swap(m_pendingEvents);
	}

	UnsignedByte sidBytes[16];
	if (!sessionIdToBytes(m_sessionId, sidBytes))
		return;
	const Int currentFrame = TheGameLogic ? (Int)TheGameLogic->getFrame() : 0;
	const Int64 utcMillis =
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();

	// Build the JSON body. AsciiString concatenation is cheap for
	// the modest sizes we're working with (max 16 KB).
	AsciiString json;
	json.concat("{\"events\":[");

	const size_t MAX_BODY_BYTES = 16 * 1024 - 256;  // headroom for closing braces
	Bool first = TRUE;
	size_t shipped = 0;
	char numBuf[32];
	for (const PendingEvent &e : batch)
	{
		AsciiString one;
		if (!first) one.concat(",");
		one.concat("{\"frame\":");
		_snprintf(numBuf, sizeof(numBuf), "%d", e.frame);
		one.concat(numBuf);
		one.concat(",\"type\":\"");
		appendJsonEscaped(one, e.type.str());
		one.concat("\"");
		if (e.actorSlot       >= 0 && e.actorSlot       < 256) {
			one.concat(",\"actor\":");
			_snprintf(numBuf, sizeof(numBuf), "%d", e.actorSlot);
			one.concat(numBuf);
		}
		if (e.targetSlot      >= 0 && e.targetSlot      < 256) {
			one.concat(",\"target\":");
			_snprintf(numBuf, sizeof(numBuf), "%d", e.targetSlot);
			one.concat(numBuf);
		}
		if (e.thingTemplateId >= 0) {
			one.concat(",\"tid\":");
			_snprintf(numBuf, sizeof(numBuf), "%d", e.thingTemplateId);
			one.concat(numBuf);
		}
		if (e.x != INT_MIN) {
			one.concat(",\"x\":");
			_snprintf(numBuf, sizeof(numBuf), "%d", e.x);
			one.concat(numBuf);
		}
		if (e.y != INT_MIN) {
			one.concat(",\"y\":");
			_snprintf(numBuf, sizeof(numBuf), "%d", e.y);
			one.concat(numBuf);
		}
		if (e.cash != INT_MIN) {
			one.concat(",\"cash\":");
			_snprintf(numBuf, sizeof(numBuf), "%d", e.cash);
			one.concat(numBuf);
		}
		if (!e.extraJson.isEmpty()) {
			one.concat(",\"extra\":");
			one.concat(e.extraJson.str());
		}
		one.concat("}");

		if (shipped > 0
		    && (size_t)json.getLength() + one.getLength() >= MAX_BODY_BYTES)
		{
			// Body would overflow; stop here. Re-buffer the
			// remaining events for the next flush so they're
			// not lost.
			std::lock_guard<std::mutex> lk(m_pendingEventsMutex);
			// Find this event's position in the original batch.
			size_t idx = shipped;
			while (idx < batch.size())
			{
				m_pendingEvents.push_back(std::move(batch[idx]));
				idx++;
			}
			break;
		}
		json.concat(one.str());
		first = FALSE;
		shipped++;
	}
	json.concat("]}");

	if (shipped == 0)
		return;

	// Build the wire payload: 29-byte fixed prefix + 4-byte jsonLen
	// + jsonBytes. Then frame the whole thing as RELAY_TYPE_GAME_EVENTS.
	const Int jsonLen = json.getLength();
	std::vector<UnsignedByte> body;
	body.resize((size_t)(29 + 4 + jsonLen));
	Int off = 0;
	memcpy(body.data() + off, sidBytes, 16); off += 16;
	body[off++] = (UnsignedByte)(0 & 0xFF);  // reporterSlot — server uses AuthUserId, this is informational
	{
		Int frame = currentFrame;
		memcpy(body.data() + off, &frame, 4);     off += 4;
		memcpy(body.data() + off, &utcMillis, 8); off += 8;
		memcpy(body.data() + off, &jsonLen, 4);   off += 4;
	}
	memcpy(body.data() + off, json.str(), jsonLen);

	std::vector<UnsignedByte> packet;
	TheLAN->packGameEventsPacket(body.data(), (Int)body.size(), packet);
	if (!packet.empty())
	{
		enqueuePacket(packet.data(), (Int)packet.size());
		telemetryTrace("enqueued GAME_EVENTS frame=%d events=%u jsonLen=%d",
		               currentFrame, (unsigned)shipped, (int)jsonLen);
	}
}

void GameTelemetry::onLogicFrame()
{
	// First-time trace to confirm the hook is firing. Reset each
	// onGameStart via s_tracedOnce = FALSE so every match logs once.
	static Bool s_tracedOnce = FALSE;
	if (!s_tracedOnce)
	{
		telemetryTrace("onLogicFrame first tick; m_active=%d m_resultSent=%d sid=%s",
		               (int)m_active, (int)m_resultSent,
		               m_sessionId.isEmpty() ? "empty" : m_sessionId.str());
		s_tracedOnce = TRUE;
	}

	if (!m_active || m_resultSent)
		return;
	if (!TheLAN || !TheGameLogic || !ThePlayerList)
		return;

	const Int currentFrame = (Int)TheGameLogic->getFrame();

	// Arm-timeout: if we've been waiting for the relay's
	// RELAY_TYPE_SESSION_ASSIGN for more than 10 seconds of logic
	// frames, give up rather than sit armed-but-silent for the rest
	// of the match. Happens when the relay is down or the ASSIGN
	// broadcast was dropped at the TCP layer. Clearing m_active stops
	// sendGameResultToRelay too — without a session ID there's
	// nothing the server can do with a GAMERESULT anyway.
	static const Int ASSIGN_TIMEOUT_FRAMES = 300; // ~10 s at 30 Hz
	if (m_sessionId.isEmpty())
	{
		if ((currentFrame - m_gameStartFrame) > ASSIGN_TIMEOUT_FRAMES)
		{
			telemetryTrace("onLogicFrame: RELAY_TYPE_SESSION_ASSIGN never arrived "
			               "(%d frames since start); disarming telemetry",
			               currentFrame - m_gameStartFrame);
			m_active = FALSE;
		}
		return;
	}

	if (m_lastSnapshotFrame != 0
	    && (currentFrame - m_lastSnapshotFrame) < SCORE_EVENTS_EVERY_FRAMES)
		return;

	Player *localPlayer = ThePlayerList->getLocalPlayer();
	if (!localPlayer)
		return;

	UnsignedByte sidBytes[16];
	if (!sessionIdToBytes(m_sessionId, sidBytes))
		return;

	// Wall-clock UTC milliseconds since the Unix epoch. std::chrono
	// system_clock is portable and matches what DateTime.UtcNow on the
	// server reads from System.DateTime — no timezone math needed.
	const Int64 utcMillis =
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();

	// Walk every real participant in the match (skip civilian /
	// neutral / observer bookkeeping entries that ThePlayerList
	// includes internally). For each, read the absolute totals from
	// their ScoreKeeper. Under lockstep sim every peer's read of every
	// player's totals is authoritative.
	//
	// The wire packet's max payload is 358 bytes (8 observed). MAX_SLOTS
	// stays well under that; we cap defensively at 8 observed even if
	// ThePlayerList grew (e.g. observers).
	const Int playerCount = ThePlayerList->getPlayerCount();
	const Int MAX_OBSERVED = 8;

	// Stack-local buffer sized for the worst case (header + MAX_OBSERVED
	// blocks). Avoids a heap alloc on the sim thread's hot path.
	const Int HEADER_SIZE = 30;
	const Int BLOCK_SIZE  = 41;
	UnsignedByte buf[HEADER_SIZE + BLOCK_SIZE * MAX_OBSERVED];
	Int off = HEADER_SIZE;  // body starts after header; we'll write count later
	UnsignedByte blockCount = 0;

	#define PUT_I32(v) do { Int _v = (Int)(v); memcpy(buf + off, &_v, 4); off += 4; } while (0)

	for (Int i = 1; i < playerCount && blockCount < MAX_OBSERVED; ++i)
	{
		Player *p = ThePlayerList->getNthPlayer(i);
		if (!p)
			continue;
		// Filter to real participants. Same predicate
		// sendGameResultToRelay uses for the GAMERESULT JSON: human
		// or AI player with a non-empty display name. Civilian /
		// neutral / observer bookkeeping rows are skipped — they
		// have no meaningful ScoreKeeper data.
		AsciiString displayName;
		displayName.translate(p->getPlayerDisplayName());
		if (displayName.isEmpty())
			continue;
		const PlayerType pt = p->getPlayerType();
		if (pt != PLAYER_HUMAN && pt != PLAYER_COMPUTER)
			continue;

		PerPlayerScore observed;
		readScoreForPlayer(p, observed);

		const UnsignedByte observedSlot = (UnsignedByte)(p->getPlayerIndex() & 0xFF);
		buf[off++] = observedSlot;

		PUT_I32(observed.unitsBuilt);
		PUT_I32(observed.unitsLost);
		PUT_I32(observed.unitsKilledHuman);
		PUT_I32(observed.unitsKilledAI);
		PUT_I32(observed.buildingsBuilt);
		PUT_I32(observed.buildingsLost);
		PUT_I32(observed.buildingsKilledHuman);
		PUT_I32(observed.buildingsKilledAI);
		PUT_I32(observed.moneyEarned);
		PUT_I32(observed.moneySpent);

		blockCount++;
	}

	#undef PUT_I32

	// No observable players — should never happen in a real match
	// (the local player is always observable) but be defensive: a
	// zero-block packet would trip the relay's min-size check.
	if (blockCount == 0)
		return;

	// Now backfill the header.
	{
		Int hoff = 0;
		memcpy(buf + hoff, sidBytes, 16); hoff += 16;
		buf[hoff++] = (UnsignedByte)(localPlayer->getPlayerIndex() & 0xFF);
		Int frame = currentFrame;
		memcpy(buf + hoff, &frame, 4); hoff += 4;
		memcpy(buf + hoff, &utcMillis, 8); hoff += 8;
		buf[hoff++] = blockCount;
	}

	const Int totalLen = HEADER_SIZE + BLOCK_SIZE * (Int)blockCount;

	// Pack the framed bytes on the sim thread, hand them to the
	// telemetry sender thread for the blocking relaySendAll. Bypasses
	// the relaySendScoreEvents wrapper so relaySendAll runs only on
	// the sender thread.
	std::vector<UnsignedByte> packet;
	TheLAN->packScoreEventPacket(buf, totalLen, packet);
	if (!packet.empty())
	{
		enqueuePacket(packet.data(), (Int)packet.size());
		telemetryTrace("enqueued SCORE_EVENTS frame=%d observed=%u size=%d",
		               currentFrame, (unsigned)blockCount, (int)packet.size());
	}
	else
	{
		telemetryTrace("pack returned empty packet (relay likely disconnected)");
	}

	m_lastSnapshotFrame = currentFrame;

	// Flush any in-flight detailed events on the same cadence —
	// keeps the per-tick traffic batched into one SCORE_EVENTS +
	// (optional) GAME_EVENTS pair instead of sprinkling small
	// per-event packets across the wire.
	shipPendingEventsBatch();

	// ── Periodic position snapshot ──────────────────────────────
	//
	// Every 1800 frames (~60s at 30Hz) take a full sim-object
	// snapshot. Game-time-based, not wall-clock — same rationale
	// as the FPS minute bucket above. A paused game doesn't tick
	// the snapshot bucket forward.
	const Int SNAPSHOT_EVERY_FRAMES = 1800;
	const Int elapsedFrames = currentFrame - m_gameStartFrame;
	const Int currentSnapBucket = elapsedFrames / SNAPSHOT_EVERY_FRAMES;
	if (currentSnapBucket > m_lastPositionSnapshotBucket)
	{
		snapshotPositions("periodic", -1);
		m_lastPositionSnapshotBucket = currentSnapBucket;
	}

	// ── Per-minute FPS bucketing ──────────────────────────────
	//
	// Sample once per score-event tick (~3s cadence). Enough
	// density for a minute-level aggregate without paying the
	// cost of a TheDisplay call every single logic frame. A
	// 60s minute collects ~20 samples, more than enough for the
	// avg/min/max triple to be meaningful.
	if (TheDisplay)
	{
		const Real nowFps = TheDisplay->getCurrentFPS();
		const Int  nowX100 = (Int)(nowFps * 100.0f + 0.5f);
		// Minute index is game-time-based, not wall-clock — a
		// paused game doesn't roll the bucket forward, which
		// matches how every other per-match aggregate on this
		// class is framed (SCORE_EVENTS carries the logic
		// frame, not UTC ms, for the same reason).
		const Int elapsedFrames = currentFrame - m_gameStartFrame;
		const Int minuteIndex   = elapsedFrames / (30 * 60);

		if (m_fpsSampleCount == 0)
		{
			// First sample of a fresh bucket.
			m_fpsMinuteIndex = minuteIndex;
			m_fpsMinX100 = nowX100;
			m_fpsMaxX100 = nowX100;
		}
		else if (minuteIndex != m_fpsMinuteIndex)
		{
			// Minute rolled. Ship the closed bucket, then seed
			// the new one with this tick's sample so the first
			// 60s are never lost.
			shipAndResetFpsBucket();
			m_fpsMinuteIndex = minuteIndex;
			m_fpsMinX100 = nowX100;
			m_fpsMaxX100 = nowX100;
		}

		m_fpsSumX100    += nowX100;
		m_fpsSampleCount++;
		if (nowX100 < m_fpsMinX100) m_fpsMinX100 = nowX100;
		if (nowX100 > m_fpsMaxX100) m_fpsMaxX100 = nowX100;
	}
}

// Pack + ship the in-flight FPS bucket and zero the accumulator.
// Called on every minute roll from onLogicFrame, and on the terminal
// flush path from sendGameResultToRelay so a short game that ends
// inside its first minute still credits a bucket.
void GameTelemetry::shipAndResetFpsBucket()
{
	if (m_fpsSampleCount <= 0 || m_sessionId.isEmpty() || !TheLAN)
	{
		// Zero state — nothing to send; reset anyway.
		m_fpsSampleCount = 0;
		m_fpsSumX100 = 0;
		m_fpsMinX100 = 0;
		m_fpsMaxX100 = 0;
		return;
	}

	UnsignedByte sidBytes[16];
	if (!sessionIdToBytes(m_sessionId, sidBytes))
	{
		m_fpsSampleCount = 0;
		m_fpsSumX100 = 0;
		m_fpsMinX100 = 0;
		m_fpsMaxX100 = 0;
		return;
	}

	const Int avgX100 = m_fpsSumX100 / m_fpsSampleCount;

	// Fixed 37-byte payload: matches RELAY_TYPE_FPS_BUCKET handler
	// in RelayServer.cs (16 GUID + 1 slot + 4 minute + 4 count +
	// 4 avg + 4 min + 4 max).
	UnsignedByte buf[37];
	Int off = 0;
	memcpy(buf + off, sidBytes, 16); off += 16;
	Player *localPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	buf[off++] = localPlayer ? (UnsignedByte)(localPlayer->getPlayerIndex() & 0xFF) : 0;
	Int minute = m_fpsMinuteIndex;
	memcpy(buf + off, &minute,          4); off += 4;
	memcpy(buf + off, &m_fpsSampleCount,4); off += 4;
	memcpy(buf + off, &avgX100,         4); off += 4;
	memcpy(buf + off, &m_fpsMinX100,    4); off += 4;
	memcpy(buf + off, &m_fpsMaxX100,    4); off += 4;

	std::vector<UnsignedByte> packet;
	TheLAN->packFpsBucketPacket(buf, (Int)sizeof(buf), packet);
	if (!packet.empty())
	{
		enqueuePacket(packet.data(), (Int)packet.size());
		telemetryTrace("enqueued FPS_BUCKET minute=%d n=%d avg=%d min=%d max=%d (x100)",
		               m_fpsMinuteIndex, m_fpsSampleCount,
		               avgX100, m_fpsMinX100, m_fpsMaxX100);
	}

	m_fpsSampleCount = 0;
	m_fpsSumX100 = 0;
	m_fpsMinX100 = 0;
	m_fpsMaxX100 = 0;
}
