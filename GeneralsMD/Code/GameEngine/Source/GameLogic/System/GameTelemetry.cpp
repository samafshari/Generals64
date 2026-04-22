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
, m_senderShouldStop(false)
{
	memset(&m_lastSent, 0, sizeof(m_lastSent));
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
	memset(&m_lastSent, 0, sizeof(m_lastSent));
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
	memset(&m_lastSent, 0, sizeof(m_lastSent));

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
	memset(&m_lastSent, 0, sizeof(m_lastSent));

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
		telemetryTrace("onRelayAssignSession: session changed %s -> %s; resetting watermark",
		               m_sessionId.str(), sid);
		m_lastSnapshotFrame = 0;
		memset(&m_lastSent, 0, sizeof(m_lastSent));
	}
	m_sessionId.set(sid);
	telemetryTrace("onRelayAssignSession: sid=%s m_active=1", sid);
}

// Note: PerPlayerScore lives in GameTelemetry.h so the watermark
// member m_lastSent and this helper can share the layout.

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
	// User rollup — the server's StatsService no longer reads score
	// fields out of GAMERESULT at all, so without this forced send
	// the totals stay zero for short games. Bypasses the normal
	// throttle by zeroing the last-frame sentinel; the delta vs.
	// m_lastSent still covers exactly the unreported tail.
	m_lastSnapshotFrame = 0;
	onLogicFrame();

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
			_snprintf(buf, sizeof(buf), ",\"Team\":%d", teamNum);
			buf[sizeof(buf) - 1] = '\0';
			json.concat(buf);
		}

		_snprintf(buf, sizeof(buf),
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
		buf[sizeof(buf) - 1] = '\0';
		json.concat(buf);
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

void GameTelemetry::onGameWon()         { sendGameResultToRelay(RES_WIN); }
void GameTelemetry::onGameLost()        { sendGameResultToRelay(RES_LOSS); }
void GameTelemetry::onGameSurrendered() { sendGameResultToRelay(RES_LOSS); }
void GameTelemetry::onGameExited()      { sendGameResultToRelay(RES_DISCONNECT); }

// ── Periodic score events ─────────────────────────────────────────
//
// Wire format (69-byte payload behind the standard
// [4:size][4:sessionID][1:type=9] relay header):
//
//    [16] session GUID, raw bytes (not hex) — derived from m_sessionId
//    [ 1] local player slot index
//    [ 4] current logic frame           (Int32 LE)
//    [ 8] wall-clock UTC milliseconds   (Int64 LE) at pack time
//    [ 4] unitsBuilt                    (Int32 LE) — DELTA since last send
//    [ 4] unitsLost                                   DELTA
//    [ 4] unitsKilledHuman                            DELTA
//    [ 4] unitsKilledAI                               DELTA
//    [ 4] buildingsBuilt                              DELTA
//    [ 4] buildingsLost                               DELTA
//    [ 4] buildingsKilledHuman                        DELTA
//    [ 4] buildingsKilledAI                           DELTA
//    [ 4] moneyEarned                                 DELTA
//    [ 4] moneySpent                                  DELTA
//
// Total: 16 + 1 + 4 + 8 + 10*4 = 69 bytes. Must match the
// ScoreEventPayloadSize constant in RelayServer.cs — drift = every
// packet dropped as "bad payload size".
//
// All ten score fields are DELTAS = current ScoreKeeper total minus the
// totals we shipped on the previous send (m_lastSent). The server
// accumulates these onto the User rollup and appends a per-batch row
// to GameScoreEvents so the timeline can be reconstructed by frame /
// wall clock. Fire-and-forget; a dropped packet just means that
// round's delta folds into the next batch.

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

	PerPlayerScore current;
	readScoreForPlayer(localPlayer, current);

	// Per-field delta vs. the last batch we shipped. Negative deltas
	// shouldn't happen in normal play (ScoreKeeper totals are
	// monotonic) but we don't clamp here — the server is responsible
	// for any sanity floors so the wire stays a clean diff.
	PerPlayerScore d;
	d.unitsBuilt           = current.unitsBuilt           - m_lastSent.unitsBuilt;
	d.unitsLost            = current.unitsLost            - m_lastSent.unitsLost;
	d.unitsKilledHuman     = current.unitsKilledHuman     - m_lastSent.unitsKilledHuman;
	d.unitsKilledAI        = current.unitsKilledAI        - m_lastSent.unitsKilledAI;
	d.buildingsBuilt       = current.buildingsBuilt       - m_lastSent.buildingsBuilt;
	d.buildingsLost        = current.buildingsLost        - m_lastSent.buildingsLost;
	d.buildingsKilledHuman = current.buildingsKilledHuman - m_lastSent.buildingsKilledHuman;
	d.buildingsKilledAI    = current.buildingsKilledAI    - m_lastSent.buildingsKilledAI;
	d.moneyEarned          = current.moneyEarned          - m_lastSent.moneyEarned;
	d.moneySpent           = current.moneySpent           - m_lastSent.moneySpent;

	// Wall-clock UTC milliseconds since the Unix epoch. std::chrono
	// system_clock is portable and matches what DateTime.UtcNow on the
	// server reads from System.DateTime — no timezone math needed on
	// either end.
	const Int64 utcMillis =
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();

	// Pack the 69-byte payload. Little-endian on x86/x64 — memcpy of
	// a native Int32/Int64 matches what BitConverter.ToInt32 /
	// BitConverter.ToInt64 read on the other end. Size must match the
	// ScoreEventPayloadSize constant on RelayServer.cs — a mismatch
	// trips the "bad payload size" drop in RelayServer.cs and every
	// batch is silently discarded.
	UnsignedByte buf[69];
	Int off = 0;
	memcpy(buf + off, sidBytes, 16); off += 16;
	buf[off++] = (UnsignedByte)(localPlayer->getPlayerIndex() & 0xFF);
	Int frame = currentFrame;
	memcpy(buf + off, &frame, 4); off += 4;
	memcpy(buf + off, &utcMillis, 8); off += 8;

	#define PUT_I32(v) do { Int _v = (Int)(v); memcpy(buf + off, &_v, 4); off += 4; } while (0)
	PUT_I32(d.unitsBuilt);
	PUT_I32(d.unitsLost);
	PUT_I32(d.unitsKilledHuman);
	PUT_I32(d.unitsKilledAI);
	PUT_I32(d.buildingsBuilt);
	PUT_I32(d.buildingsLost);
	PUT_I32(d.buildingsKilledHuman);
	PUT_I32(d.buildingsKilledAI);
	PUT_I32(d.moneyEarned);
	PUT_I32(d.moneySpent);
	#undef PUT_I32

	// Assert the payload length matches the wire contract. Compile-time
	// check isn't an option because `off` is a runtime accumulator, but
	// if this ever fires at runtime it means the fixed-width layout drifted.
	if (off != sizeof(buf))
		return;

	// Pack the framed bytes on the sim thread, hand them to the
	// telemetry sender thread for the blocking relaySendAll. Bypasses
	// the relaySendScoreEvents wrapper so relaySendAll runs only on
	// the sender thread.
	std::vector<UnsignedByte> packet;
	TheLAN->packScoreEventPacket(buf, (Int)sizeof(buf), packet);
	if (!packet.empty())
	{
		enqueuePacket(packet.data(), (Int)packet.size());
		telemetryTrace("enqueued SCORE_EVENTS frame=%d size=%d built=%d lost=%d killed=%d money=%d/%d",
		               currentFrame, (int)packet.size(),
		               d.unitsBuilt, d.unitsLost,
		               d.unitsKilledHuman + d.unitsKilledAI,
		               d.moneyEarned, d.moneySpent);
	}
	else
	{
		telemetryTrace("pack returned empty packet (relay likely disconnected)");
	}

	// Advance the watermark to the totals we just shipped. Any
	// further changes will surface in the next batch's delta.
	m_lastSent = current;
	m_lastSnapshotFrame = currentFrame;
}
