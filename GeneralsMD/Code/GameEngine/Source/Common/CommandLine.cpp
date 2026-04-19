/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////


#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ArchiveFileSystem.h"
#include "Common/CommandLine.h"
#include "Common/CRCDebug.h"
#include "Common/LocalFileSystem.h"
#include "Common/Recorder.h"
#include "Common/SearchPaths.h"
#include "Common/version.h"
#include "GameClient/ClientInstance.h"
#include "GameClient/TerrainVisual.h" // for TERRAIN_LOD_MIN definition
#include "GameClient/GameText.h"
#include "GameNetwork/NetworkDefs.h"
#include "trim.h"




Bool TheDebugIgnoreSyncErrors = FALSE;
extern Int DX8Wrapper_PreserveFPU;

// Autotest mode: after this many rendered frames, screenshot + diagnostics + exit.
// 0 = disabled (normal mode). Set via -autotest <frames> command line.
Int g_autotestFrames = 0;

#ifdef DEBUG_CRC
Int TheCRCFirstFrameToLog = -1;
UnsignedInt TheCRCLastFrameToLog = 0xffffffff;
Bool g_keepCRCSaves = FALSE;
Bool g_saveDebugCRCPerFrame = FALSE;
AsciiString g_saveDebugCRCPerFrameDir;
Bool g_crcModuleDataFromLogic = FALSE;
Bool g_crcModuleDataFromClient = FALSE;
Bool g_verifyClientCRC = FALSE; // verify that GameLogic CRC doesn't change from client
Bool g_clientDeepCRC = FALSE;
Bool g_logObjectCRCs = FALSE;
#endif

#if defined(RTS_DEBUG)
extern Bool g_useStringFile;
#endif

// Retval is number of cmd-line args eaten
typedef Int (*FuncPtr)( char *args[], int num );

static const UnsignedByte F_NOCASE = 1; // Case-insensitive

struct CommandLineParam
{
	const char *name;
	FuncPtr func;
};

static void ConvertShortMapPathToLongMapPath(AsciiString &mapName)
{
	AsciiString path = mapName;
	AsciiString token;
	AsciiString parentDir;  // last non-.map token we saw (immediate parent)
	AsciiString actualpath;

	if ((path.find('\\') == nullptr) && (path.find('/') == nullptr))
	{
		DEBUG_CRASH(("Invalid map name %s", mapName.str()));
		return;
	}
	path.nextToken(&token, "\\/");
	while (!token.endsWithNoCase(".map") && (!token.isEmpty()))
	{
		actualpath.concat(token);
		actualpath.concat('\\');
		parentDir = token;
		path.nextToken(&token, "\\/");
	}

	if (!token.endsWithNoCase(".map"))
	{
		DEBUG_CRASH(("Invalid map name %s", mapName.str()));
	}
	// remove the .map from the end.
	token.truncateBy(4);

	// If the immediate parent directory already matches the map base name,
	// the input was already a long-form path (e.g. Maps\Foo\Foo.map).  In
	// that case actualpath already ends with "Foo\" and we only need to
	// append "Foo.map".  Otherwise (short-form, e.g. Maps\Foo.map) we also
	// need to add the intermediate "Foo\" folder.
	if (parentDir.compareNoCase(token) != 0)
	{
		actualpath.concat(token);
		actualpath.concat('\\');
	}
	actualpath.concat(token);
	actualpath.concat(".map");

	mapName = actualpath;
}

//=============================================================================
//=============================================================================
Int parseNoLogOrCrash(char *args[], int)
{
	DEBUG_CRASH(("-NoLogOrCrash not supported in this build"));
	return 1;
}

//=============================================================================
//=============================================================================
Int parseWin(char *args[], int)
{
	TheWritableGlobalData->m_windowed = true;

	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoMusic(char *args[], int)
{
	TheWritableGlobalData->m_musicOn = false;

	return 1;
}


//=============================================================================
//=============================================================================
Int parseNoVideo(char *args[], int)
{
	TheWritableGlobalData->m_videoOn = false;

	return 1;
}

//=============================================================================
//=============================================================================
Int parseFPUPreserve(char *args[], int argc)
{
	if (argc > 1)
	{
		DX8Wrapper_PreserveFPU = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseUseWaveEditor(char *args[], int num)
{
	TheWritableGlobalData->m_usingWaterTrackEditor = TRUE;

	return 1;
}

//=============================================================================
//=============================================================================
Int parseFullViewport(char *args[], int num)
{
	TheWritableGlobalData->m_viewportHeightScale = 1.0f;

	return 1;
}

#if defined(RTS_DEBUG)

//=============================================================================
//=============================================================================
Int parseUseCSF(char *args[], int)
{
	g_useStringFile = FALSE;
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoInputDisable(char *args[], int)
{
	TheWritableGlobalData->m_disableScriptedInputDisabling = true;

	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoFade(char *args[], int)
{
	TheWritableGlobalData->m_disableCameraFade = true;

	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoMilCap(char *args[], int)
{
	TheWritableGlobalData->m_disableMilitaryCaption = true;

	return 1;
}
#endif // RTS_DEBUG

//=============================================================================
//=============================================================================
Int parseDebugCRCFromFrame(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		TheCRCFirstFrameToLog = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseDebugCRCUntilFrame(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		TheCRCLastFrameToLog = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseKeepCRCSave(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_keepCRCSaves = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseSaveDebugCRCPerFrame(char* args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		g_saveDebugCRCPerFrame = TRUE;
		g_saveDebugCRCPerFrameDir = args[1];
		if (TheCRCFirstFrameToLog == -1)
			TheCRCFirstFrameToLog = 0;
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseCRCLogicModuleData(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_crcModuleDataFromLogic = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseCRCClientModuleData(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_crcModuleDataFromClient = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseClientDeepCRC(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_clientDeepCRC = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseVerifyClientCRC(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_verifyClientCRC = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseLogObjectCRCs(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_logObjectCRCs = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNetCRCInterval(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		NET_CRC_INTERVAL = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseReplayCRCInterval(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		REPLAY_CRC_INTERVAL = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseNoDraw(char *args[], int argc)
{
#ifdef DEBUG_CRC
	TheWritableGlobalData->m_noDraw = TRUE;
#endif
	return 1;
}

#if defined(RTS_DEBUG)

//=============================================================================
//=============================================================================
Int parseLogToConsole(char *args[], int)
{
#ifdef ALLOW_DEBUG_UTILS
	DebugSetFlags(DebugGetFlags() | DEBUG_FLAG_LOG_TO_CONSOLE);
#endif
	return 1;
}

#endif // RTS_DEBUG

//=============================================================================
//=============================================================================
Int parseNoAudio(char *args[], int)
{
	TheWritableGlobalData->m_audioOn = false;
	TheWritableGlobalData->m_speechOn = false;
	TheWritableGlobalData->m_soundsOn = false;
	TheWritableGlobalData->m_musicOn = false;

	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoWin(char *args[], int)
{
	TheWritableGlobalData->m_windowed = false;

	return 1;
}

Int parseFullVersion(char *args[], int num)
{
	if (TheVersion && num > 1)
	{
		TheVersion->setShowFullVersion(atoi(args[1]) != 0);
	}
	return 1;
}

Int parseNoShadows(char *args[], int)
{
	TheWritableGlobalData->m_useShadowVolumes = false;
	TheWritableGlobalData->m_useShadowDecals = false;

	return 1;
}

Int parseMapName(char *args[], int num)
{
	if (num == 2)
	{
		TheWritableGlobalData->m_mapName.set( args[ 1 ] );
		ConvertShortMapPathToLongMapPath(TheWritableGlobalData->m_mapName);
	}
	return 1;
}

Int parseHeadless(char *args[], int num)
{
	TheWritableGlobalData->m_headless = TRUE;
	TheWritableGlobalData->m_playIntro = FALSE;
	TheWritableGlobalData->m_afterIntro = TRUE;
	TheWritableGlobalData->m_playSizzle = FALSE;
	return 1;
}

Int parseClientServer(char *args[], int num)
{
	TheWritableGlobalData->m_clientServerMode = TRUE;
	return 1;
}

extern Int g_autotestFrames;

Int parseAutotest(char *args[], int num)
{
	if (num > 1)
	{
		g_autotestFrames = atoi(args[1]);
		if (g_autotestFrames <= 0)
			g_autotestFrames = 120;
	}
	else
	{
		g_autotestFrames = 120;
	}
	// Autotest implies: skip intro, skip shell anim, windowed
	TheWritableGlobalData->m_playIntro = FALSE;
	TheWritableGlobalData->m_afterIntro = TRUE;
	TheWritableGlobalData->m_playSizzle = FALSE;
	TheWritableGlobalData->m_animateWindows = FALSE;
	TheWritableGlobalData->m_windowed = true;
	return 2;
}

Int parseReplay(char *args[], int num)
{
	if (num > 1)
	{
		AsciiString filename = args[1];
		if (!filename.endsWithNoCase(RecorderClass::getReplayExtention()))
		{
			printf("Invalid replay name \"%s\"\n", filename.str());
			exit(1);
		}
		TheWritableGlobalData->m_simulateReplays.push_back(filename);

		TheWritableGlobalData->m_playIntro = FALSE;
		TheWritableGlobalData->m_afterIntro = TRUE;
		TheWritableGlobalData->m_playSizzle = FALSE;
		TheWritableGlobalData->m_shellMapOn = FALSE;

		// Make replay playback possible while other clients (possible retail) are running
		rts::ClientInstance::setMultiInstance(TRUE);
		rts::ClientInstance::skipPrimaryInstance();

		return 2;
	}
	return 1;
}

Int parseJobs(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_simulateReplayJobs = atoi(args[1]);
		if (TheGlobalData->m_simulateReplayJobs < SIMULATE_REPLAYS_SEQUENTIAL || TheGlobalData->m_simulateReplayJobs == 0)
		{
			printf("Invalid number of jobs: %d\n", TheGlobalData->m_simulateReplayJobs);
			exit(1);
		}
		return 2;
	}
	return 1;
}

Int parseXRes(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_xResolution = atoi(args[1]);
		// Mark the resolution as explicitly user-requested so the SDL window
		// opens at this exact size instead of starting maximized. Without
		// this, SDLPlatform::Init defaults to SDL_WINDOW_MAXIMIZED and the
		// requested resolution is ignored — the window covers the work area
		// (bordered) or the full screen (borderless) regardless of -xres.
		TheWritableGlobalData->m_explicitDisplayResolution = TRUE;
		return 2;
	}
	return 1;
}

Int parseYRes(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_yResolution = atoi(args[1]);
		TheWritableGlobalData->m_explicitDisplayResolution = TRUE;
		return 2;
	}
	return 1;
}

#if defined(RTS_DEBUG)
//=============================================================================
//=============================================================================
Int parseLatencyAverage(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_latencyAverage = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseLatencyAmplitude(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_latencyAmplitude = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseLatencyPeriod(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_latencyPeriod = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseLatencyNoise(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_latencyNoise = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parsePacketLoss(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_packetLoss = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseLowDetail(char *args[], int num)
{
	TheWritableGlobalData->m_terrainLOD = TERRAIN_LOD_MIN;

	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoDynamicLOD(char *args[], int num)
{
	TheWritableGlobalData->m_enableDynamicLOD = FALSE;

	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoStaticLOD(char *args[], int num)
{
	TheWritableGlobalData->m_enableStaticLOD = FALSE;

	return 1;
}

//=============================================================================
//=============================================================================
Int parseFPSLimit(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_framesPerSecondLimit = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
Int parseNoViewLimit(char *args[], int)
{
	TheWritableGlobalData->m_useCameraConstraints = FALSE;

	return 1;
}

Int parseWireframe(char *args[], int)
{
	TheWritableGlobalData->m_wireframe = TRUE;

	return 1;
}

Int parseShowCollision(char *args[], int)
{
	TheWritableGlobalData->m_showCollisionExtents = TRUE;

	return 1;
}

Int parseNoShowClientPhysics(char *args[], int)
{
	TheWritableGlobalData->m_showClientPhysics = FALSE;

	return 1;
}

Int parseShowTerrainNormals(char *args[], int)
{
	TheWritableGlobalData->m_showTerrainNormals = TRUE;

	return 1;
}

Int parseStateMachineDebug(char *args[], int)
{
	TheWritableGlobalData->m_stateMachineDebug = TRUE;

	return 1;
}

Int parseJabber(char *args[], int)
{
	TheWritableGlobalData->m_jabberOn = TRUE;

	return 1;
}

Int parseMunkee(char *args[], int)
{
	TheWritableGlobalData->m_munkeeOn = TRUE;

	return 1;
}
#endif // defined(RTS_DEBUG)

Int parseScriptDebug(char *args[], int)
{
	TheWritableGlobalData->m_scriptDebug = TRUE;
	TheWritableGlobalData->m_winCursors = TRUE;

	return 1;
}

Int parseParticleEdit(char *args[], int)
{
	TheWritableGlobalData->m_particleEdit = TRUE;
	TheWritableGlobalData->m_winCursors = TRUE;
	TheWritableGlobalData->m_windowed = TRUE;

	return 1;
}


Int parseBuildMapCache(char *args[], int)
{
	TheWritableGlobalData->m_buildMapCache = true;

	return 1;
}


#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
Int parsePreload( char *args[], int num )
{
	TheWritableGlobalData->m_preloadAssets = TRUE;

	return 1;
}
#endif


#if defined(RTS_DEBUG)
Int parseDisplayDebug(char *args[], int)
{
	TheWritableGlobalData->m_displayDebug = TRUE;

	return 1;
}

Int parsePreloadEverything( char *args[], int num )
{
	TheWritableGlobalData->m_preloadAssets = TRUE;
	TheWritableGlobalData->m_preloadEverything = TRUE;

	return 1;
}

Int parseLogAssets( char *args[], int num )
{
	FILE *logfile=fopen("PreloadedAssets.txt","w");
	if (logfile)	//clear the file
		fclose(logfile);
	TheWritableGlobalData->m_preloadReport = TRUE;

	return 1;
}

/// begin stuff for VTUNE
Int parseVTune ( char *args[], int num )
{
	TheWritableGlobalData->m_vTune = TRUE;

	return 1;
}
/// end stuff for VTUNE

#endif // defined(RTS_DEBUG)

//=============================================================================
//=============================================================================

Int parseNoFX(char *args[], int)
{
	TheWritableGlobalData->m_useFX = FALSE;

	return 1;
}

// Search-path override (set via `-path "<absolute dir>"`, repeatable).
// Each occurrence appends to SearchPaths' command-line override list.
// Once any -path is supplied, SearchPaths skips paths.txt entirely and
// uses only the command-line entries (in the order they were passed).
// Parsed at startup so it lands before any file system touches assets.
Int parsePath(char *args[], int num)
{
	if (num > 1 && args[1] && args[1][0])
	{
		SearchPaths::AddCommandLinePath(args[1]);
		return 2;
	}
	return 1;
}

// FilterCode for relay server private lobby isolation (set via -filtercode).
// Defined in LANAPI.cpp; extern here so the command line parser can write it.
extern char g_relayFilterCode[24];

// Relay server hostname or IP (set via -relayserver). Defined in LANAPI.cpp.
// Empty by default — the launcher passes it on the command line, so running
// the exe directly without -relayserver will keep multiplayer disabled.
extern char g_relayServerHost[256];

Int parseFilterCode(char *args[], int num)
{
	if (num > 1)
	{
		// Trim, uppercase, and validate length (6-20 chars).
		const char *src = args[1];
		// Skip leading whitespace
		while (*src == ' ' || *src == '\t') src++;

		char buf[24];
		int len = 0;
		for (; src[len] && len < 20; ++len)
		{
			char c = src[len];
			if (c >= 'a' && c <= 'z')
				buf[len] = c - 32; // toupper
			else
				buf[len] = c;
		}
		// Trim trailing whitespace
		while (len > 0 && (buf[len-1] == ' ' || buf[len-1] == '\t')) len--;
		buf[len] = '\0';

		if (len >= 6 && len <= 20)
			memcpy(g_relayFilterCode, buf, len + 1);
		// else: keep build default ("ZEROHOUR" for ZH, "GENERALS" for original)
	}
	return 2;
}

// Accept a hostname or IP for the relay / lobby server. No validation
// beyond length — Winsock resolves DNS in LANAPI::relayConnect and the
// pre-flight ping in MainMenu.cpp. Value stays in g_relayServerHost
// for the entire engine lifetime.
Int parseRelayServer(char *args[], int num)
{
	if (num > 1)
	{
		const char *src = args[1];
		// Skip leading whitespace
		while (*src == ' ' || *src == '\t') src++;

		int len = 0;
		for (; src[len] && len < 255; ++len)
			g_relayServerHost[len] = src[len];
		// Trim trailing whitespace
		while (len > 0 && (g_relayServerHost[len-1] == ' ' || g_relayServerHost[len-1] == '\t')) len--;
		g_relayServerHost[len] = '\0';
	}
	return 2;
}

// When TRUE, the main menu auto-pushes the LAN lobby on its first idle frame
// so the player lands directly in the multiplayer menu. Set via -mpmenu.
// One-shot: cleared by MainMenuUpdate after the push fires.
Bool g_launchToMpMenu = FALSE;

// Suppress the EA/Westwood/sizzle intros for any launch path that's
// heading straight into the multiplayer lobby. Matches what -win/-quickstart
// style flags already do above — the player asked for MP, don't make them
// sit through three logos first. Single-player launches (no -mpmenu /
// -joingame / -hostgame) fall through unchanged and still play the intros.
static void suppressIntrosForMultiplayer()
{
	TheWritableGlobalData->m_playIntro = FALSE;
	TheWritableGlobalData->m_afterIntro = TRUE;
	TheWritableGlobalData->m_playSizzle = FALSE;
}

Int parseMpMenu(char *args[], int)
{
	g_launchToMpMenu = TRUE;
	suppressIntrosForMultiplayer();
	return 1;
}

// Name of a relay-listed game the player wants to auto-join after the
// LAN lobby finishes loading. Set via `-joingame "GameName"`. The lobby
// menu's update loop polls TheLAN->LookupGame() each frame for a few
// seconds, then calls RequestGameJoin() if found. On any failure
// (timeout, RET_GAME_FULL, RET_CRC_MISMATCH, etc.) the existing
// OnGameJoin error path leaves the player sitting in the populated
// lobby — exactly the "fall back to lobby on failure" UX we want.
//
// Stored as UnicodeString because LANGameInfo::getName is unicode and
// LookupGame compares with == on UnicodeString. Empty = no auto-join.
UnicodeString g_joinGameName;

Int parseJoinGame(char *args[], int num)
{
	if (num > 1 && args[1] && args[1][0])
	{
		// Engine launches with -mpmenu when this is set, so the
		// command-line tokenizer has already unquoted the value;
		// args[1] is the raw game name as the host published it.
		g_joinGameName.translate(AsciiString(args[1]));
		// Implicit: -joingame is meaningless without entering the
		// multiplayer lobby, so flip the same flag -mpmenu sets.
		// Saves the launcher having to pass both.
		g_launchToMpMenu = TRUE;
		suppressIntrosForMultiplayer();
		return 2;
	}
	return 1;
}

// When TRUE, the lobby's auto-launch state machine synthesizes a
// click on the Host button on first idle frame, dropping the player
// directly into the LanGameOptionsMenu (the create-game / lobby
// setup screen) instead of leaving them in the games-list view.
// Set via -hostgame. Implicitly sets -mpmenu so the launcher only
// has to pass one flag.
Bool g_launcherHostGame = FALSE;

Int parseHostGame(char *args[], int)
{
	g_launcherHostGame = TRUE;
	g_launchToMpMenu = TRUE;
	suppressIntrosForMultiplayer();
	return 1;
}

// Player name override from the launcher (-playername "Name").
// When set, the LAN lobby uses this verbatim instead of the
// LANPreferences saved-name fallback, AND disables the editable
// text entry so the user can't change it from inside the game —
// the launcher is the canonical source. Empty = no override.
UnicodeString g_launcherPlayerName;

Int parsePlayerName(char *args[], int num)
{
	if (num > 1 && args[1] && args[1][0])
	{
		g_launcherPlayerName.translate(AsciiString(args[1]));
		return 2;
	}
	return 1;
}

// Auth token from the launcher (-authtoken "l_..."). A 1-minute
// single-use launch token exchanged with the server for a 24-hour
// game token. Sent in the relay TCP handshake as RELAY_TYPE_AUTH.
char g_authLaunchToken[128] = {0};
char g_authGameToken[128]   = {0};
char g_authDisplayName[16]  = {0};

Int parseAuthToken(char *args[], int num)
{
	if (num > 1 && args[1] && args[1][0])
	{
		const char *src = args[1];
		while (*src == ' ' || *src == '\t') src++;
		int len = 0;
		for (; src[len] && len < 127; ++len)
			g_authLaunchToken[len] = src[len];
		while (len > 0 && (g_authLaunchToken[len-1] == ' ' || g_authLaunchToken[len-1] == '\t')) len--;
		g_authLaunchToken[len] = '\0';
		return 2;
	}
	return 1;
}

// Custom house color override from the launcher (-color "#RRGGBB").
// Stored as a 0x00RRGGBB UnsignedInt. Slot colors are now raw 24-bit
// RGB ints (no palette indices), so this value lands directly in the
// slot's color field at lobby init and flows through the standard
// slot wire encoding to remote peers unchanged. Zero RGB is a valid
// color, hence the separate g_launcherPlayerColorSet flag.
UnsignedInt g_launcherPlayerColor = 0;
Bool g_launcherPlayerColorSet = FALSE;

// Cosmetic team-color shader id from the launcher (-shader N).
// 0 = stock house-color shader (the engine's default replacement
// pass). Non-zero = a player-activated cosmetic override. The
// render path is the eventual consumer; for now we just round-trip
// the value through the wire format and the player dictionary so
// the future shader marketplace UX can be built on top.
Int g_launcherPlayerShaderId = 0;

Int parseShaderId(char *args[], int num)
{
	if (num > 1 && args[1] && args[1][0])
	{
		g_launcherPlayerShaderId = atoi(args[1]);
		return 2;
	}
	return 1;
}

Int parseColor(char *args[], int num)
{
	if (num > 1 && args[1] && args[1][0])
	{
		// Accept "#RRGGBB" or "RRGGBB" — strip a leading # if present.
		const char *src = args[1];
		if (*src == '#') ++src;

		// Parse 6 hex chars. Anything malformed silently leaves the
		// override unset; the engine will fall back to the standard
		// palette picker for that player.
		UnsignedInt rgb = 0;
		Int n = 0;
		for (; n < 6 && src[n]; ++n)
		{
			char c = src[n];
			UnsignedInt v;
			if (c >= '0' && c <= '9')      v = c - '0';
			else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
			else { n = 0; break; } // invalidate
			rgb = (rgb << 4) | v;
		}
		if (n == 6)
		{
			g_launcherPlayerColor = rgb & 0x00FFFFFFu;
			g_launcherPlayerColorSet = TRUE;
		}
		return 2;
	}
	return 1;
}

// Path to a JSON launch config written by the external launcher.
// When set, the game skips the shell menu and starts directly into
// the configured game mode (skirmish, LAN, etc.).
AsciiString g_launchConfigFile;

Int parseLaunchConfig(char *args[], int num)
{
	if (num > 1)
	{
		g_launchConfigFile = args[1];
		TheWritableGlobalData->m_playIntro = FALSE;
		TheWritableGlobalData->m_playSizzle = FALSE;
		TheWritableGlobalData->m_shellMapOn = FALSE;
		TheWritableGlobalData->m_animateWindows = FALSE;
	}
	return 2;
}

// Load a specific map from the command line. Historically this was guarded
// by RTS_DEBUG, but it is useful for QA, profiling, and map authors to run
// in Release builds, so it is available unconditionally.
Int parseFile(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_initialFile = args[1];
		ConvertShortMapPathToLongMapPath(TheWritableGlobalData->m_initialFile);
	}
	return 2;
}

// -ff / -fastforward — turn on TiVO fast-forward mode at boot so the
// simulation runs uncapped from the first frame. Useful for rapidly
// reaching crash repro points in cinematics / scripted missions without
// sitting through the real-time cutscene. GameLogic::startNewGame forces
// m_TiVOFastMode back to FALSE on every new game start, so we can't just
// set it once in the INI — we re-assert it post-init via a deferred flag.
// See Win32GameEngine::init for the handoff.
Bool g_cliFastForwardRequested = FALSE;
Int parseFastForward(char *[], int)
{
	g_cliFastForwardRequested = TRUE;
	return 1;
}

#if defined(RTS_DEBUG) && ENABLE_CONFIGURABLE_SHROUD
Int parseNoShroud(char *args[], int)
{
	TheWritableGlobalData->m_shroudOn = FALSE;

	return 1;
}
#endif

Int parseForceBenchmark(char *args[], int)
{
	TheWritableGlobalData->m_forceBenchmark = TRUE;

	return 1;
}

Int parseNoMoveCamera(char *args[], int)
{
	TheWritableGlobalData->m_disableCameraMovement = true;

	return 1;
}

#if defined(RTS_DEBUG)
Int parseNoCinematic(char *args[], int)
{
	TheWritableGlobalData->m_disableCameraMovement = true;
	TheWritableGlobalData->m_disableMilitaryCaption = true;
	TheWritableGlobalData->m_disableCameraFade = true;
	TheWritableGlobalData->m_disableScriptedInputDisabling = true;

	return 1;
}
#endif

Int parseSync(char *args[], int)
{
	TheDebugIgnoreSyncErrors = true;

	return 1;
}

Int parseNoShellMap(char *args[], int)
{
	TheWritableGlobalData->m_shellMapOn = FALSE;

	return 1;
}

Int parseNoShaders(char *args[], int)
{
	TheWritableGlobalData->m_chipSetType = 1;	//force to a voodoo card which uses least amount of features.

	return 1;
}

Int parseNoLogo(char *args[], int)
{
	TheWritableGlobalData->m_playIntro = FALSE;
	TheWritableGlobalData->m_afterIntro = TRUE;
	TheWritableGlobalData->m_playSizzle = FALSE;

	return 1;
}

Int parseShellMap(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_shellMapName = args[1];
	}
	return 2;
}

Int parseNoWindowAnimation(char *args[], int num)
{
	TheWritableGlobalData->m_animateWindows = FALSE;

	return 1;
}

Int parseWinCursors(char *args[], int num)
{
	TheWritableGlobalData->m_winCursors = TRUE;

	return 1;
}

Int parseQuickStart( char *args[], int num )
{
	parseNoLogo( args, num );
	parseNoShellMap( args, num );
	parseNoWindowAnimation( args, num );
	return 1;
}

Int parseConstantDebug( char *args[], int num )
{
	TheWritableGlobalData->m_constantDebugUpdate = TRUE;

	return 1;
}

#if defined(RTS_DEBUG)
Int parseExtraLogging( char *args[], int num )
{
	TheWritableGlobalData->m_extraLogging = TRUE;

	return 1;
}
#endif

//-allAdvice feature
/*
Int parseAllAdvice( char *args[], int num )
{
	TheWritableGlobalData->m_allAdvice = TRUE;

	return 1;
}
*/

Int parseShowTeamDot( char *args[], int num )
{
	TheWritableGlobalData->m_showTeamDot = TRUE;

	return 1;
}


#if defined(RTS_DEBUG)
Int parseSelectAll( char *args[], int num )
{
	TheWritableGlobalData->m_allowUnselectableSelection = TRUE;

	return 1;
}

Int parseRunAhead( char *args[], Int num )
{
	if (num > 2)
	{
		MIN_RUNAHEAD = atoi(args[1]);
		MAX_FRAMES_AHEAD = atoi(args[2]);
		FRAME_DATA_LENGTH = (MAX_FRAMES_AHEAD + 1)*2;
	}
	return 3;
}
#endif


Int parseSeed(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_fixedSeed = atoi(args[1]);
	}
	return 2;
}

Int parseIncrAGPBuf(char *args[], int num)
{
	TheWritableGlobalData->m_incrementalAGPBuf = TRUE;

	return 1;
}

Int parseNetMinPlayers(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_netMinPlayers = atoi(args[1]);
	}
	return 2;
}

Int parsePlayStats(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_playStats  = atoi(args[1]);
	}
	return 2;
}

Int parseDemoLoadScreen(char *args[], int num)
{
	TheWritableGlobalData->m_loadScreenDemo = TRUE;

	return 1;
}

#if defined(RTS_DEBUG)
Int parseSaveStats(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_saveStats = TRUE;
		TheWritableGlobalData->m_baseStatsDir = args[1];
	}
	return 2;
}
#endif

#if defined(RTS_DEBUG)
Int parseSaveAllStats(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_saveStats = TRUE;
		TheWritableGlobalData->m_baseStatsDir = args[1];
		TheWritableGlobalData->m_saveAllStats = TRUE;
	}
	return 2;
}
#endif

#if defined(RTS_DEBUG)
Int parseLocalMOTD(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_useLocalMOTD = TRUE;
		TheWritableGlobalData->m_MOTDPath = args[1];
	}
	return 2;
}
#endif

#if defined(RTS_DEBUG)
Int parseCameraDebug(char *args[], int num)
{
	TheWritableGlobalData->m_debugCamera = TRUE;

	return 1;
}
#endif

#if defined(RTS_DEBUG)
Int parseBenchmark(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_benchmarkTimer = atoi(args[1]);
		TheWritableGlobalData->m_playStats  = atoi(args[1]);
	}
	return 2;
}
#endif

#if defined(RTS_DEBUG)
#ifdef DUMP_PERF_STATS
Int parseStats(char *args[], int num)
{
	if (num > 1)
	{
		TheWritableGlobalData->m_dumpStatsAtInterval = TRUE;
		TheWritableGlobalData->m_statsInterval  = atoi(args[1]);
	}
	return 2;
}
#endif
#endif

#ifdef DEBUG_CRASHING
Int parseIgnoreAsserts(char *args[], int num)
{
	if (num > 0)
	{
		TheWritableGlobalData->m_debugIgnoreAsserts = true;
	}
	return 1;
}
#endif

#ifdef DEBUG_STACKTRACE
Int parseIgnoreStackTrace(char *args[], int num)
{
	if (num > 0)
	{
		TheWritableGlobalData->m_debugIgnoreStackTrace = true;
	}
	return 1;
}
#endif

Int parseNoFPSLimit(char *args[], int num)
{
	TheWritableGlobalData->m_useFpsLimit = false;
	TheWritableGlobalData->m_framesPerSecondLimit = 30000;

	return 1;
}

Int parseDumpAssetUsage(char *args[], int num)
{
	TheWritableGlobalData->m_dumpAssetUsage = true;

	return 1;
}

Int parseJumpToFrame(char *args[], int num)
{
	if (num > 1)
	{
		parseNoFPSLimit(args, num);
		TheWritableGlobalData->m_noDraw = atoi(args[1]);
		return 2;
	}
	return 1;
}

Int parseUpdateImages(char *args[], int num)
{
	TheWritableGlobalData->m_shouldUpdateTGAToDDS = TRUE;

	return 1;
}


#ifdef DEBUG_LOGGING
Int parseSetDebugLevel(char *args[], int num)
{
	if (num > 1)
	{
		AsciiString val = args[1];
		for (Int i=0; i<DEBUG_LEVEL_MAX; ++i)
		{
			if (val == TheDebugLevels[i])
			{
				DebugLevelMask |= 1<<i;
				break;
			}
		}
	}
	return 2;
}

Int parseClearDebugLevel(char *args[], int num)
{
	if (num > 1)
	{
		AsciiString val = args[1];
		for (Int i=0; i<DEBUG_LEVEL_MAX; ++i)
		{
			if (val == TheDebugLevels[i])
			{
				DebugLevelMask &= ~(1<<i);
				break;
			}
		}
	}
	return 2;
}
#endif

// Initial Params are parsed before Windows Creation.
// Note that except for TheGlobalData, no other global objects exist yet when these are parsed.
static CommandLineParam paramsForStartup[] =
{
	{ "-win", parseWin },
	{ "-fullscreen", parseNoWin },

	// This runs the game without a window, graphics, input and audio. You can combine this with -replay
	{ "-headless", parseHeadless },

	// Use client-server networking mode instead of P2P lockstep
	{ "-clientserver", parseClientServer },

	// Play back a replay. Pass the filename including .rep afterwards.
	// You can pass this multiple times to play back multiple replays.
	// You can also include wildcards. The file must be in the replay folder or in a subfolder.
	{ "-replay", parseReplay },

	// Simulate each replay in a separate process and use 1..N processes at the same time.
	// (If you have 4 cores, call it with -jobs 4)
	// If you do not call this, all replays will be simulated in sequence in the same process.
	{ "-jobs", parseJobs },

	// Autotest mode: render N frames, screenshot, write diagnostics, exit.
	// Usage: -autotest 120
	{ "-autotest", parseAutotest },

	// Resolution - parsed at startup so window is created at the right size
	{ "-xres", parseXRes },
	{ "-yres", parseYRes },

	// Search-path override. Repeatable. Must run at startup so the entries
	// land before the BIG and loose-file systems mount during engine init.
	// When supplied, SearchPaths ignores paths.txt entirely and uses only
	// the order given on the command line.
	{ "-path", parsePath },
};

// These Params are parsed during Engine Init AFTER INI/Options loading.
// Entries here override Options.ini settings.
static CommandLineParam paramsForEngineInit[] =
{
	{ "-nologo", parseNoLogo },
	{ "-noshellmap", parseNoShellMap },
	{ "-noShellAnim", parseNoWindowAnimation },
	{ "-win", parseWin },           // Override Options.ini windowed preference
	{ "-fullscreen", parseNoWin },  // Override Options.ini fullscreen preference
	{ "-autotest", parseAutotest }, // Re-apply autotest flags after Options.ini
	{ "-xres", parseXRes },
	{ "-yres", parseYRes },
	{ "-fullVersion", parseFullVersion },
	{ "-particleEdit", parseParticleEdit },
	{ "-scriptDebug", parseScriptDebug },
	{ "-playStats", parsePlayStats },
	{ "-noshaders", parseNoShaders },
	{ "-quickstart", parseQuickStart },
	{ "-useWaveEditor", parseUseWaveEditor },

	{ "-forcefullviewport", parseFullViewport },

	// Load a specific map from the command line. Available in all builds so
	// that QA and map authors can repro issues directly in shipping builds.
	{ "-file", parseFile },

	// Fast-forward: enable TiVO-mode from the first frame (no render-fps
	// cap). Useful for racing through scripted mission intros.
	{ "-ff",           parseFastForward },
	{ "-fastforward",  parseFastForward },

	// Launch config from external launcher (JSON file with game setup).
	{ "-launchconfig", parseLaunchConfig },

	// Boot straight to the LAN/relay multiplayer lobby menu.
	{ "-mpmenu", parseMpMenu },

	// Auto-join a specific relay-listed game once the lobby finishes
	// loading. Argument is the game name string the host published.
	// Implicitly sets -mpmenu so the launcher only has to pass one flag.
	{ "-joingame", parseJoinGame },

	// Auto-host a new game once the lobby finishes loading: the
	// engine synthesizes a click on the in-game Host button and
	// pushes LanGameOptionsMenu directly. Implicitly sets -mpmenu.
	{ "-hostgame", parseHostGame },

	// Local player name override (the launcher's canonical source).
	// Disables the in-game lobby's editable text entry.
	{ "-playername", parsePlayerName },

	// Auth launch token from the launcher. Exchanged at startup for
	// a 24-hour game token used in the relay TCP handshake.
	{ "-authtoken", parseAuthToken },

	// Custom house color as #RRGGBB. Slot color is a raw 24-bit
	// RGB int now, so this value lands directly in the local slot
	// at lobby init and propagates to peers via the standard slot
	// wire encoding.
	{ "-color", parseColor },

	// Cosmetic team-color shader id (default 0 = stock shader).
	// Non-zero values represent player-activated cosmetic shaders;
	// the render path will eventually swap the house-color pass
	// based on this id. Wire format already carries it.
	{ "-shader", parseShaderId },

	// Private lobby FilterCode for relay server isolation.
	{ "-filtercode", parseFilterCode },

	// Relay server hostname / IP. Required for multiplayer — the
	// launcher passes it; running the exe directly leaves it empty
	// and LANAPI::relayConnect fails fast with a clear log line.
	{ "-relayserver", parseRelayServer },

#if defined(RTS_DEBUG)
	{ "-noaudio", parseNoAudio },
	{ "-map", parseMapName },
	{ "-nomusic", parseNoMusic },
	{ "-novideo", parseNoVideo },
	{ "-noLogOrCrash", parseNoLogOrCrash },
	{ "-FPUPreserve", parseFPUPreserve },
	{ "-benchmark", parseBenchmark },
#ifdef DUMP_PERF_STATS
	{ "-stats", parseStats },
#endif
	{ "-saveStats", parseSaveStats },
	{ "-localMOTD", parseLocalMOTD },
	{ "-UseCSF", parseUseCSF },
	{ "-NoInputDisable", parseNoInputDisable },
#endif
#ifdef DEBUG_CRC
	// The following arguments are useful for CRC debugging.
	// Note that you need to have a debug or internal configuration build in order to use this.
	// Release configuration also works if RELEASE_DEBUG_LOGGING is defined in Debug.h
	// Also note that all players need to play in the same configuration, otherwise mismatch will
	// occur almost immediately.
	// Try this if you want to play the game and have useful debug information in case mismatch occurs:
	// -ignoreAsserts -DebugCRCFromFrame 0 -VerifyClientCRC -LogObjectCRCs -NetCRCInterval 1
	// After mismatch occurs, you can examine the logfile and also reproduce the crc from the replay with this (and diff that with the log):
	// -ignoreAsserts -DebugCRCFromFrame xxx -LogObjectCRCs -SaveDebugCRCPerFrame crc

	// After which frame to log crc logging. Call with 0 to log all frames and with -1 to log none (default).
	{ "-DebugCRCFromFrame", parseDebugCRCFromFrame },

	// Last frame to log
	{ "-DebugCRCUntilFrame", parseDebugCRCUntilFrame },

	// Save data involving CRC calculation to a binary file. (This isn't that useful.)
	{ "-KeepCRCSaves", parseKeepCRCSave },

	// Store CRC Debug Logging into a separate file for each frame.
	// Pass the foldername after this where those files are to be stored.
	// This is useful for replay analysis.
	// Note that the passed folder is deleted if it already exists for every started game.
	{ "-SaveDebugCRCPerFrame", parseSaveDebugCRCPerFrame },

	{ "-CRCLogicModuleData", parseCRCLogicModuleData },
	{ "-CRCClientModuleData", parseCRCClientModuleData },

	// Verify that Game Logic CRC doesn't change during client update.
	// Client update is only for visuals and not supposed to change the crc.
	// (This is implemented using CRCVerification class in GameEngine::update)
	{ "-VerifyClientCRC", parseVerifyClientCRC },

	// Write out binary crc data pre and post client update to "clientPre.crc" and "clientPost.crc"
	{ "-ClientDeepCRC", parseClientDeepCRC },

	// Log CRC of Objects and Weapons (See Object::crc and Weapon::crc)
	{ "-LogObjectCRCs", parseLogObjectCRCs },

	// Number of frames between each CRC check between all players in multiplayer games
	// (if not all crcs are equal, mismatch occurs).
	{ "-NetCRCInterval", parseNetCRCInterval },

	// Number of frames between each CRC that is written to replay files in singleplayer games.
	{ "-ReplayCRCInterval", parseReplayCRCInterval },
#endif
#if defined(RTS_DEBUG)
	{ "-saveAllStats", parseSaveAllStats },
	{ "-noDraw", parseNoDraw },
	{ "-nomilcap", parseNoMilCap },
	{ "-nofade", parseNoFade },
	{ "-nomovecamera", parseNoMoveCamera },
	{ "-nocinematic", parseNoCinematic },
	{ "-packetloss", parsePacketLoss },
	{ "-latAvg", parseLatencyAverage },
	{ "-latAmp", parseLatencyAmplitude },
	{ "-latPeriod", parseLatencyPeriod },
	{ "-latNoise", parseLatencyNoise },
	{ "-noViewLimit", parseNoViewLimit },
	{ "-lowDetail", parseLowDetail },
	{ "-noDynamicLOD", parseNoDynamicLOD },
	{ "-noStaticLOD", parseNoStaticLOD },
	{ "-fps", parseFPSLimit },
	{ "-wireframe", parseWireframe },
	{ "-showCollision", parseShowCollision },
	{ "-noShowClientPhysics", parseNoShowClientPhysics },
	{ "-showTerrainNormals", parseShowTerrainNormals },
	{ "-stateMachineDebug", parseStateMachineDebug },
	{ "-jabber", parseJabber },
	{ "-munkee", parseMunkee },
	{ "-displayDebug", parseDisplayDebug },

//	{ "-preload", parsePreload },

	{ "-preloadEverything", parsePreloadEverything },
	{ "-logAssets", parseLogAssets },
	{ "-netMinPlayers", parseNetMinPlayers },
	{ "-DemoLoadScreen", parseDemoLoadScreen },
	{ "-cameraDebug", parseCameraDebug },
	{ "-logToCon", parseLogToConsole },
	{ "-vTune", parseVTune },
	{ "-selectTheUnselectable", parseSelectAll },
	{ "-RunAhead", parseRunAhead },
#if ENABLE_CONFIGURABLE_SHROUD
	{ "-noshroud", parseNoShroud },
#endif
	{ "-forceBenchmark", parseForceBenchmark },
	{ "-buildmapcache", parseBuildMapCache },
	{ "-noshadowvolumes", parseNoShadows },
	{ "-nofx", parseNoFX },
	{ "-ignoresync", parseSync },
	{ "-shellmap", parseShellMap },
	{ "-winCursors", parseWinCursors },
	{ "-constantDebug", parseConstantDebug },
	{ "-seed", parseSeed },
	{ "-noagpfix", parseIncrAGPBuf },
	{ "-noFPSLimit", parseNoFPSLimit },
	{ "-dumpAssetUsage", parseDumpAssetUsage },
	{ "-jumpToFrame", parseJumpToFrame },
	{ "-updateImages", parseUpdateImages },
	{ "-showTeamDot", parseShowTeamDot },
	{ "-extraLogging", parseExtraLogging },
#endif

#ifdef DEBUG_LOGGING
	{ "-setDebugLevel", parseSetDebugLevel },
	{ "-clearDebugLevel", parseClearDebugLevel },
#endif

#ifdef DEBUG_CRASHING
	{ "-ignoreAsserts", parseIgnoreAsserts },
#endif

#ifdef DEBUG_STACKTRACE
	{ "-ignoreStackTrace", parseIgnoreStackTrace },
#endif

	//-allAdvice feature
	//{ "-allAdvice", parseAllAdvice },

#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	{ "-preload", parsePreload },
#endif


};

char *nextParam(char *newSource, const char *seps)
{
	static char *source = nullptr;
	if (newSource)
	{
		source = newSource;
	}
	if (!source)
	{
		return nullptr;
	}

	// find first separator
	char *first = source;//strpbrk(source, seps);
	if (first)
	{
		// go past separator
		char *firstSep = strpbrk(first, seps);
		char firstChar[2] = {0,0};
		if (firstSep == first)
		{
			firstChar[0] = *first;
			while (*first == firstChar[0]) first++;
		}

		// find end
		char *end;
		if (firstChar[0])
			end = strpbrk(first, firstChar);
		else
			end = strpbrk(first, seps);

		// trim string & save next start pos
		if (end)
		{
			source = end+1;
			*end = 0;

			if (!*source)
				source = nullptr;
		}
		else
		{
			source = nullptr;
		}

		if (first && !*first)
			first = nullptr;
	}

	return first;
}

static void parseCommandLine(const CommandLineParam* params, int numParams)
{
	std::vector<char*> argv;

	std::string cmdLine = GetCommandLineA();
	char *token = nextParam(&cmdLine[0], "\" ");
	while (token != nullptr)
	{
		argv.push_back(strtrim(token));
		token = nextParam(nullptr, "\" ");
	}
	int argc = argv.size();

	int arg = 1;

#ifdef DEBUG_LOGGING
	DEBUG_LOG(("Command-line args:"));
	int debugFlags = DebugGetFlags();
	DebugSetFlags(debugFlags & ~DEBUG_FLAG_PREPEND_TIME); // turn off timestamps
	for (arg=1; arg<argc; arg++)
	{
		DEBUG_LOG((" %s", argv[arg]));
	}
	DEBUG_LOG_RAW(("\n"));
	DebugSetFlags(debugFlags); // turn timestamps back on iff they were on before
	arg = 1;
#endif // DEBUG_LOGGING

	// To parse command-line parameters, we loop through a table holding arguments
	// and functions to handle them.  Comparisons can be case-(in)sensitive, and
	// can check the entire string (for testing the presence of a flag) or check
	// just the start (for a key=val argument).  The handling function can also
	// look at the next argument(s), to accommodate multi-arg parameters, e.g. "-p 1234".
	while (arg<argc)
	{
		// Look at arg #i
		Bool found = false;
		for (int param=0; !found && param<numParams; ++param)
		{
			int len = strlen(params[param].name);
			int len2 = strlen(argv[arg]);
			if (len2 != len)
				continue;
			if (strnicmp(argv[arg], params[param].name, len) == 0)
			{
				arg += params[param].func(&argv[0]+arg, argc-arg);
				found = true;
				break;
			}
		}
		if (!found)
		{
			arg++;
		}
	}
}

void createGlobalData()
{
	if (TheGlobalData == nullptr)
		TheWritableGlobalData = NEW GlobalData;
}

void CommandLine::parseCommandLineForStartup()
{
	// We need the GlobalData initialized before parsing the command line.
	// Note that this function is potentially called multiple times and only initializes the first time.
	createGlobalData();

	if (TheGlobalData->m_commandLineData.m_hasParsedCommandLineForStartup)
		return;
	TheWritableGlobalData->m_commandLineData.m_hasParsedCommandLineForStartup = true;

	parseCommandLine(paramsForStartup, ARRAY_SIZE(paramsForStartup));
}

void CommandLine::parseCommandLineForEngineInit()
{
	createGlobalData();

	DEBUG_ASSERTCRASH(TheGlobalData->m_commandLineData.m_hasParsedCommandLineForStartup,
		("parseCommandLineForStartup is expected to be called before parseCommandLineForEngineInit\n"));
	DEBUG_ASSERTCRASH(!TheGlobalData->m_commandLineData.m_hasParsedCommandLineForEngineInit,
		("parseCommandLineForEngineInit is expected to be called once only\n"));
	TheWritableGlobalData->m_commandLineData.m_hasParsedCommandLineForEngineInit = true;

	parseCommandLine(paramsForEngineInit, ARRAY_SIZE(paramsForEngineInit));
}
