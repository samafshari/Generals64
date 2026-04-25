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

// FILE: MainMenu.cpp /////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, October 2001
// Description: Main menu window callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/GameSpy/GameSpySDKStubs.h"

#include "Lib/BaseType.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/RandomValue.h"
#include "Common/OptionPreferences.h"
#include "Common/version.h"
#include "Common/GameLOD.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/ExtendedMessageBox.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Display.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameText.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Shell.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/Mouse.h"
#include "GameClient/WindowVideoManager.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/HotKey.h"
#include "GameClient/GameClient.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/ChallengeGenerals.h"

#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/BuddyThread.h"

#include "GameNetwork/DownloadManager.h"
#include "GameNetwork/GameSpy/MainMenuUtils.h"

#include "GameClient/InGameUI.h"

#ifdef _WIN32
#include <winsock.h>
#endif
extern HWND ApplicationHWnd;

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////

enum
{
	DROPDOWN_NONE = 0,
	DROPDOWN_SINGLE,
	DROPDOWN_MULTIPLAYER,
	DROPDOWN_MAIN,
	DROPDOWN_LOADREPLAY,
	DROPDOWN_DIFFICULTY,

	DROPDOWN_COUNT
};

static Bool raiseMessageBoxes = TRUE;
static Bool campaignSelected = FALSE;
#if defined(RTS_DEBUG) || defined RTS_PROFILE
static NameKeyType campaignID = NAMEKEY_INVALID;
static GameWindow *buttonCampaign = nullptr;
#ifdef TEST_COMPRESSION
static GameWindow *buttonCompressTest = nullptr;
void DoCompressTest();
#endif // TEST_COMPRESSION
#endif


// window ids -------------------------------------------------------------------------------------
static NameKeyType mainMenuID = NAMEKEY_INVALID;
static NameKeyType skirmishID = NAMEKEY_INVALID;
static NameKeyType onlineID = NAMEKEY_INVALID;
static NameKeyType networkID = NAMEKEY_INVALID;
static NameKeyType optionsID = NAMEKEY_INVALID;
static NameKeyType exitID = NAMEKEY_INVALID;
static NameKeyType motdID = NAMEKEY_INVALID;
static NameKeyType worldBuilderID = NAMEKEY_INVALID;
static NameKeyType getUpdateID = NAMEKEY_INVALID;
static NameKeyType buttonTRAININGID = NAMEKEY_INVALID;
static NameKeyType buttonChallengeID = NAMEKEY_INVALID;
static NameKeyType buttonUSAID = NAMEKEY_INVALID;
static NameKeyType buttonGLAID = NAMEKEY_INVALID;
static NameKeyType buttonChinaID = NAMEKEY_INVALID;
static NameKeyType buttonUSARecentSaveID = NAMEKEY_INVALID;
static NameKeyType buttonUSALoadGameID = NAMEKEY_INVALID;
static NameKeyType buttonGLARecentSaveID = NAMEKEY_INVALID;
static NameKeyType buttonGLALoadGameID = NAMEKEY_INVALID;
static NameKeyType buttonChinaRecentSaveID = NAMEKEY_INVALID;
static NameKeyType buttonChinaLoadGameID = NAMEKEY_INVALID;
static NameKeyType buttonSinglePlayerID = NAMEKEY_INVALID;
static NameKeyType buttonMultiPlayerID = NAMEKEY_INVALID;
static NameKeyType buttonMultiBackID = NAMEKEY_INVALID;
static NameKeyType buttonSingleBackID = NAMEKEY_INVALID;
static NameKeyType buttonLoadReplayBackID = NAMEKEY_INVALID;
static NameKeyType buttonReplayID = NAMEKEY_INVALID;
static NameKeyType buttonLoadReplayID = NAMEKEY_INVALID;
static NameKeyType buttonLoadID = NAMEKEY_INVALID;
static NameKeyType buttonCreditsID = NAMEKEY_INVALID;
static NameKeyType buttonEasyID = NAMEKEY_INVALID;
static NameKeyType buttonMediumID = NAMEKEY_INVALID;
static NameKeyType buttonHardID = NAMEKEY_INVALID;
static NameKeyType buttonDiffBackID = NAMEKEY_INVALID;


// window pointers --------------------------------------------------------------------------------
static GameWindow *parentMainMenu = nullptr;
static GameWindow *buttonSinglePlayer = nullptr;
static GameWindow *buttonMultiPlayer = nullptr;
static GameWindow *buttonSkirmish = nullptr;
static GameWindow *buttonOnline = nullptr;
static GameWindow *buttonNetwork = nullptr;
static GameWindow *buttonOptions = nullptr;
static GameWindow *buttonExit = nullptr;
static GameWindow *buttonMOTD = nullptr;
static GameWindow *buttonWorldBuilder = nullptr;
static GameWindow *mainMenuMovie = nullptr;
static GameWindow *getUpdate = nullptr;
static GameWindow *buttonTRAINING = nullptr;
static GameWindow *buttonChallenge = nullptr;
static GameWindow *buttonUSA = nullptr;
static GameWindow *buttonGLA = nullptr;
static GameWindow *buttonChina = nullptr;
static GameWindow *buttonUSARecentSave = nullptr;
static GameWindow *buttonUSALoadGame = nullptr;
static GameWindow *buttonGLARecentSave = nullptr;
static GameWindow *buttonGLALoadGame = nullptr;
static GameWindow *buttonChinaRecentSave = nullptr;
static GameWindow *buttonChinaLoadGame = nullptr;
static GameWindow *buttonReplay = nullptr;
static GameWindow *buttonLoadReplay = nullptr;
static GameWindow *buttonLoad = nullptr;
static GameWindow *buttonCredits = nullptr;
static GameWindow *buttonEasy = nullptr;
static GameWindow *buttonMedium = nullptr;
static GameWindow *buttonHard = nullptr;
static GameWindow *buttonDiffBack = nullptr;
static GameWindow *dropDownWindows[DROPDOWN_COUNT];

static Bool buttonPushed = FALSE;
static Bool isShuttingDown = FALSE;
static Bool startGame = FALSE;
static Int	initialGadgetDelay = 210;

enum
{
	SHOW_NONE = 0,
	SHOW_TRAINING,
	SHOW_USA,
	SHOW_GLA,
	SHOW_CHINA,
	SHOW_SKIRMISH,
	SHOW_FRAMES_LIMIT = 20
};

static Int showFade = FALSE;
static Int dropDown = DROPDOWN_NONE;
static Int pendingDropDown = DROPDOWN_NONE;
static AnimateWindowManager *localAnimateWindowManager = nullptr;
static Bool notShown = TRUE;
static Bool FirstTimeRunningTheGame = TRUE;

static Bool showLogo = FALSE;
static Int  showFrames = 0;
static Int  showSide = SHOW_NONE;
static Bool logoIsShown = FALSE;
static Bool justEntered = FALSE;
static Bool launchChallengeMenu = FALSE;

static Bool dontAllowTransitions = FALSE;

// ── -mpmenu auto-launch state machine ─────────────────────────────────
// When the launcher passes -mpmenu, MainMenuInit hides the entire main
// menu UI and we run a small phased flow instead of the normal menu:
//
//   PENDING_START → first MainMenuUpdate frame: create the in-game
//                   "Connecting to multiplayer server..." popup, advance
//                   to AWAIT_RENDER. We have to defer the popup creation
//                   to Update because TheWindowManager isn't fully ready
//                   to spawn modal dialogs from inside Init.
//   AWAIT_RENDER  → wait one frame so the popup actually paints before
//                   we block the main thread on a 5s TCP probe.
//   RUN_PROBE     → run the synchronous relay pre-flight, destroy the
//                   popup, then either push LanLobbyMenu (success) or
//                   show an error popup whose OK callback quits the game.
//   DONE          → terminal — no more work.
//
// Kept self-contained inside MainMenu.cpp because the only thing that
// needs to know about the auto-launch is the menu's own update loop.
enum MpAutoLaunchPhase
{
	MPAL_INACTIVE = 0,
	MPAL_PENDING_START,
	MPAL_AWAIT_RENDER,
	MPAL_RUN_PROBE,
	MPAL_DONE,
};
static MpAutoLaunchPhase s_mpAutoLaunchPhase = MPAL_INACTIVE;
static GameWindow *s_mpConnectingPopup = nullptr;

// OK callback for the "connection failed" popup. Quits the game so the
// player isn't dumped onto an empty hidden main menu.
static void mpAutoLaunchFailQuit()
{
	TheGameEngine->setQuitting(TRUE);
}

// Spawn a modal popup with no buttons. We piggy-back on the regular
// MessageBox.wnd layout (so we get the styled frame + body text widget
// for free) and then hide the OK button so the player can't dismiss it
// while the connection probe is running. Returns the GameWindow* of the
// popup (the same value gogoMessageBox returns), or nullptr on failure.
static GameWindow *spawnConnectingPopup(UnicodeString title, UnicodeString body)
{
	GameWindow *popup = MessageBoxOk(title, body, nullptr);
	if (!popup)
		return nullptr;

	// MessageBox.wnd layout: parent → ButtonOk. Hide the button so the
	// dialog has no clickable surface. winSetModal (called by gogo) keeps
	// input from leaking through to the (hidden) main menu beneath.
	GameWindow *parent = TheWindowManager->winGetWindowFromId(
		popup, TheNameKeyGenerator->nameToKey("MessageBox.wnd:MessageBoxParent"));
	if (parent)
	{
		GameWindow *okBtn = TheWindowManager->winGetWindowFromId(
			parent, TheNameKeyGenerator->nameToKey("MessageBox.wnd:ButtonOk"));
		if (okBtn)
			okBtn->winHide(TRUE);
	}
	return popup;
}

// Synchronous relay-server pre-flight TCP connect. Reads the host from
// g_relayServerHost (set by the launcher via -relayserver). On success
// returns true; on failure returns false and writes a user-facing error
// message into outErr (which must be at least 320 bytes). Identical
// behavior to the relay probe inside the multiplayer button click — the
// auto-launch path uses this so the two flows can't drift apart.
static Bool relayPreflight(char *outErr, size_t errLen)
{
	extern char g_relayServerHost[256];
#if defined(RELEASE_DEV) && RELEASE_DEV
	static const unsigned short RELAY_PORT = 27910;
#else
	static const unsigned short RELAY_PORT = 28910;
#endif

	if (g_relayServerHost[0] == '\0')
	{
		snprintf(outErr, errLen,
			"No relay server configured.\n\n"
			"Launch the game through the Discombobulator launcher, "
			"or pass -relayserver <host> on the command line.");
		return FALSE;
	}

	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);

	SOCKET testSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	Bool connected = FALSE;

	if (testSock != INVALID_SOCKET)
	{
		u_long nonBlock = 1;
		ioctlsocket(testSock, FIONBIO, &nonBlock);

		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(RELAY_PORT);
		addr.sin_addr.s_addr = inet_addr(g_relayServerHost);

		if (addr.sin_addr.s_addr == INADDR_NONE)
		{
			struct hostent *host = gethostbyname(g_relayServerHost);
			if (host)
				addr.sin_addr = *(struct in_addr *)host->h_addr;
			else
				addr.sin_addr.s_addr = INADDR_NONE;
		}

		if (addr.sin_addr.s_addr != INADDR_NONE)
		{
			connect(testSock, (struct sockaddr *)&addr, sizeof(addr));

			fd_set writefds, exceptfds;
			FD_ZERO(&writefds);
			FD_ZERO(&exceptfds);
			FD_SET(testSock, &writefds);
			FD_SET(testSock, &exceptfds);
			struct timeval tv = { 5, 0 };  // 5 second timeout

			// Failed connect signals via exceptfds on Windows; watch
			// both so we don't sit through the full 5s on a refused
			// connect, and never report success on failure.
			if (select(0, nullptr, &writefds, &exceptfds, &tv) > 0
				&& FD_ISSET(testSock, &writefds)
				&& !FD_ISSET(testSock, &exceptfds))
			{
				connected = TRUE;
			}
		}
		closesocket(testSock);
	}

	if (!connected)
	{
		snprintf(outErr, errLen,
			"Connection to Relay Server failed.\n\n"
			"Could not reach: %s:%u\n"
			"Make sure the relay server is running, then try again.",
			g_relayServerHost, (unsigned)RELAY_PORT);
		return FALSE;
	}

	outErr[0] = '\0';
	return TRUE;
}

// State machine driver for the -mpmenu auto-launch flow. Called once
// per frame from the early-return block at the top of MainMenuUpdate
// while s_mpAutoLaunchPhase is non-INACTIVE/non-DONE. Phases are spread
// across multiple frames so the connecting popup paints before we block
// the main thread on the (synchronous) 5-second TCP probe.
static void runMpAutoLaunchUpdate()
{
	switch (s_mpAutoLaunchPhase)
	{
		case MPAL_INACTIVE:
		case MPAL_DONE:
			break;

		case MPAL_PENDING_START:
		{
			// First Update tick: spawn the modal "connecting" popup.
			// Done here rather than in Init because the window manager
			// is fully ready to host modal dialogs by the first update.
			s_mpConnectingPopup = spawnConnectingPopup(
				UnicodeString(L"Multiplayer"),
				UnicodeString(L"Connecting to multiplayer server..."));
			s_mpAutoLaunchPhase = MPAL_AWAIT_RENDER;
			break;
		}

		case MPAL_AWAIT_RENDER:
			// One frame of grace so the popup actually renders before
			// the synchronous TCP connect freezes the main thread.
			s_mpAutoLaunchPhase = MPAL_RUN_PROBE;
			break;

		case MPAL_RUN_PROBE:
		{
			char errmsg[320];
			Bool ok = relayPreflight(errmsg, sizeof(errmsg));

			// Tear down the "connecting" popup either way.
			if (s_mpConnectingPopup)
			{
				TheWindowManager->winDestroy(s_mpConnectingPopup);
				s_mpConnectingPopup = nullptr;
			}

			s_mpAutoLaunchPhase = MPAL_DONE;

			if (ok)
			{
				// Direct-to-multiplayer launch: SKIP every main menu
				// transition. The layout is already hidden (see the
				// MPAL init path above) and staying hidden is fine —
				// when the user eventually pops back from the lobby,
				// MainMenuInit runs again and the menu reappears with
				// its own clean fade-in.
				//
				// Key subtleties:
				//   - `setGroup("", TRUE)` atomically skips any currently
				//     running or queued transition so the subsequent
				//     push doesn't inherit a half-finished animation.
				//   - `dontAllowTransitions = TRUE` short-circuits the
				//     logo/default-menu re-show paths in MainMenuUpdate.
				//   - We deliberately DO NOT call `reverse(...)` or
				//     `parentMainMenu->winHide(FALSE)` — those were the
				//     flashy parts the user called out as bad UX.
				dontAllowTransitions = TRUE;
				buttonPushed = TRUE;
				TheTransitionHandler->setGroup(AsciiString::TheEmptyString, TRUE);
				TheShell->push("Menus/LanLobbyMenu.wnd");
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_NETWORK_SELECTED]);
			}
			else
			{
				// Failure popup whose OK callback quits the game —
				// the main menu is hidden, so dropping the player back
				// onto an empty shell would be worse than just exiting.
				UnicodeString title;
				title.translate(AsciiString("Connection Failed"));
				UnicodeString body;
				body.translate(AsciiString(errmsg));
				MessageBoxOk(title, body, mpAutoLaunchFailQuit);
			}
			break;
		}
	}
}

const Int /*TIME_OUT = 15,*/ CORNER = 10;
void AcceptResolution();
void DeclineResolution();
GameWindow *resAcceptMenu = nullptr;
extern DisplaySettings oldDispSettings, newDispSettings;
extern Bool dispChanged;
//static time_t timeStarted = 0, currentTime = 0;

void diffReverseSide();
void HandleCanceledDownload( Bool resetDropDown )
{
	buttonPushed = FALSE;
	if (resetDropDown)
	{
		dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
		TheTransitionHandler->setGroup("MainMenuDefaultMenuLogoFade");
	}
}

//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------

static void showSelectiveButtons( Int show )
{
	buttonUSARecentSave->winHide(!(show == SHOW_USA ));
	buttonUSALoadGame->winHide(!(show == SHOW_USA ));
	buttonGLARecentSave->winHide(!(show == SHOW_GLA ));
	buttonGLALoadGame->winHide(!(show == SHOW_GLA ));
	buttonChinaRecentSave->winHide(!(show == SHOW_CHINA ));
	buttonChinaLoadGame->winHide(!(show == SHOW_CHINA ));
}

static void quitCallback()
{
	buttonPushed = TRUE;
	TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_EXIT_SELECTED]);
	TheShell->pop();
	TheGameEngine->setQuitting( TRUE );



	//if (!TheGameLODManager->didMemPass())
	{	//GIANT CRAPTACULAR HACK ALERT!!!!  On sytems with little memory, we skip all normal exit code
//		//and let Windows clean up the mess.  This reduces exit times from minutes to seconds.
//		//8-19-03. MW
//		delete TheGameClient;
//		_exit(EXIT_SUCCESS);

//  THE CRAP IS NOW EVEN MORE TACULAR
//  NOW WE PERSUADE THE MEMORYPOOLMANAGER TO RETURN STUPID FROM ITS FREE()
//    if (TheMemoryPoolFactory) TheMemoryPoolFactory->prepareForMinSpecShutDown();

	}
	if (TheGameLogic->isInGame())
		TheGameLogic->exitGame();
}


void setupGameStart(AsciiString mapName, GameDifficulty diff)
{
	TheCampaignManager->setGameDifficulty(diff);

	if (launchChallengeMenu)
	{
		if (TheChallengeGenerals)
			TheChallengeGenerals->setCurrentDifficulty(diff);

		campaignSelected = TRUE;
		TheShell->push( "Menus/ChallengeMenu.wnd" );
		TheTransitionHandler->reverse("MainMenuDifficultyMenuTraining");
	}
	else
	{
		startGame = TRUE;
		TheWritableGlobalData->m_pendingFile = mapName;
		TheShell->reverseAnimatewindow();
		TheTransitionHandler->setGroup("FadeWholeScreen");
	}
}

void prepareCampaignGame(GameDifficulty diff)
{
	dontAllowTransitions = TRUE;
	OptionPreferences pref;
	pref.setCampaignDifficulty(diff);
	pref.write();
	TheScriptEngine->setGlobalDifficulty(diff);

	buttonPushed = FALSE;
	TheTransitionHandler->reverse("MainMenuDifficultyMenuBack");
	setupGameStart(TheCampaignManager->getCurrentMap(), diff );
}

static void doGameStart()
{
	startGame = FALSE;

	if (TheGameLogic->isInGame())
		TheGameLogic->clearGameData();

	// send a message to the logic for a new game
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
	msg->appendIntegerArgument(TheCampaignManager->getGameDifficulty());
	msg->appendIntegerArgument(TheCampaignManager->getRankPoints());
	InitRandom(0);

	isShuttingDown = TRUE;
}

static void shutdownComplete( WindowLayout *layout )
{
	isShuttingDown = FALSE;

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout );

}



/*
static void TimetToFileTime( time_t t, LPFILETIME pft )
{
	LONGLONG ll = Int32x32To64(t, 10000000) + 116444736000000000;
	pft->dwLowDateTime = (DWORD) ll;
	pft->dwHighDateTime = ll >>32;
}
*/

void initialHide()
{
GameWindow *win = nullptr;
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionGLA"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionChina"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionUS"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinGrowMarker"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionTraining"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionTrainingSmall"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionTrainingMedium"));
	if(win)
		win->winHide(TRUE);

	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionSkirmish"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionSkirmishSmall"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionSkirmishMedium"));
	if(win)
		win->winHide(TRUE);

	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionUS"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionUSSmall"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionUSMedium"));
	if(win)
		win->winHide(TRUE);

	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionGLA"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionGLASmall"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionGLAMedium"));
	if(win)
		win->winHide(TRUE);

	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionChina"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionChinaSmall"));
	if(win)
		win->winHide(TRUE);
	win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionChinaMedium"));
	if(win)
		win->winHide(TRUE);

}

// Originally this label does not exist in the Main Menu. It can be copied from the Options Menu.
static void initLabelVersion()
{
	NameKeyType versionID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:LabelVersion" );
	GameWindow *labelVersion = TheWindowManager->winGetWindowFromId( nullptr, versionID );

	if (labelVersion)
	{
		if (TheVersion && TheGlobalData)
		{
			UnicodeString text = TheVersion->getUnicodeProductVersionHashString();
			GadgetStaticTextSetText( labelVersion, text );
		}
		else
		{
			labelVersion->winHide( TRUE );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Initialize the main menu */
//-------------------------------------------------------------------------------------------------
void MainMenuInit( WindowLayout *layout, void *userData )
{
	TheWritableGlobalData->m_breakTheMovie = FALSE;

	TheShell->showShellMap(TRUE);
	TheMouse->setVisibility(TRUE);
	//winVidManager = NEW WindowVideoManager;
	buttonPushed = FALSE;
	isShuttingDown = FALSE;
	startGame = FALSE;
	dropDown = DROPDOWN_NONE;
	pendingDropDown = DROPDOWN_NONE;
	// Reset the -mpmenu auto-launch state. Init runs once at boot AND
	// every time the player backs out of the lobby into the main menu;
	// after the first run g_launchToMpMenu has been consumed so the
	// flag-check below is a no-op on subsequent re-entries.
	s_mpAutoLaunchPhase = MPAL_INACTIVE;
	s_mpConnectingPopup = nullptr;
	Int i = 0;
	for(; i < DROPDOWN_COUNT; ++i)
		dropDownWindows[i] = nullptr;

	// get ids for our windows
	mainMenuID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:MainMenuParent" );
//	campaignID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonCampaign" );
	skirmishID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonSkirmish" );
	onlineID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonOnline" );
	networkID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonNetwork" );
	optionsID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonOptions" );
	exitID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonExit" );
	motdID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonMOTD" );
	worldBuilderID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonWorldBuilder" );
	getUpdateID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonGetUpdate" );
//	buttonTRAININGID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonTRAINING" );
	buttonChallengeID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonChallenge" );
	buttonUSAID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonUSA" );
	buttonGLAID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonGLA" );
	buttonChinaID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonChina" );
	buttonUSARecentSaveID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonUSARecentSave" );
	buttonUSALoadGameID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonUSALoadGame" );
	buttonGLARecentSaveID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonGLARecentSave" );
	buttonGLALoadGameID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonGLALoadGame" );
	buttonChinaRecentSaveID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonChinaRecentSave" );
	buttonChinaLoadGameID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonChinaLoadGame" );
	buttonSinglePlayerID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonSinglePlayer" );
	buttonMultiPlayerID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonMultiplayer" );
	buttonMultiBackID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonMultiBack" );
	buttonSingleBackID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonSingleBack" );
	buttonLoadReplayBackID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonLoadReplayBack" );
	buttonReplayID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonReplay" );
	buttonLoadReplayID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonLoadReplay" );
	buttonLoadID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonLoadGame" );
	buttonCreditsID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonCredits" );

	buttonEasyID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonEasy" );
	buttonMediumID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonMedium" );
	buttonHardID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonHard" );
	buttonDiffBackID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonDiffBack" );

	// get pointers to the window buttons
	parentMainMenu = TheWindowManager->winGetWindowFromId( nullptr, mainMenuID );
	//buttonCampaign = TheWindowManager->winGetWindowFromId( parentMainMenu, campaignID );
	buttonSinglePlayer = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonSinglePlayerID );
	buttonMultiPlayer = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonMultiPlayerID );
	buttonSkirmish = TheWindowManager->winGetWindowFromId( parentMainMenu, skirmishID );
	buttonOnline = TheWindowManager->winGetWindowFromId( parentMainMenu, onlineID );
	buttonNetwork = TheWindowManager->winGetWindowFromId( parentMainMenu, networkID );

	// Hide the legacy Online button - all multiplayer goes through our relay server
	if (buttonOnline)
		buttonOnline->winHide(TRUE);

	// Discombobulator: hide the top-level MULTIPLAYER button on the main
	// menu. The intended entry point into multiplayer is the launcher
	// (which uses -mpmenu to drop straight into the LAN lobby) — leaving
	// a Multiplayer button on the main menu invites players to bypass
	// the relay-server pre-configuration the launcher does for them.
	if (buttonMultiPlayer)
		buttonMultiPlayer->winHide(TRUE);
	buttonOptions = TheWindowManager->winGetWindowFromId( parentMainMenu, optionsID );
	buttonExit = TheWindowManager->winGetWindowFromId( parentMainMenu, exitID );
	buttonMOTD = TheWindowManager->winGetWindowFromId( parentMainMenu, motdID );
	buttonWorldBuilder = TheWindowManager->winGetWindowFromId( parentMainMenu, worldBuilderID );
	buttonReplay = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonReplayID );
	buttonLoadReplay = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonLoadReplayID );
	buttonLoad = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonLoadID );
	buttonCredits = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonCreditsID );

	buttonEasy = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonEasyID );
	buttonMedium = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonMediumID );
	buttonHard = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonHardID );
	buttonDiffBack = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonDiffBackID );

	getUpdate = TheWindowManager->winGetWindowFromId( parentMainMenu, getUpdateID );
//	buttonTRAINING = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonTRAININGID );
	buttonChallenge = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonChallengeID );

	// Discombobulator: hide the legacy "TRAINING" button that lives in
	// MainMenu.wnd next to Challenge inside the Solo Play dropdown.
	// The original click handler was commented out years ago ("removed
	// for the mission disk -June 2003" — see further down in this file)
	// so the widget renders but does nothing when clicked. Hide it via
	// the lookup so we don't have to ship a custom MainMenu.wnd.
	{
		GameWindow *buttonTraining = TheWindowManager->winGetWindowFromId(
			parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:ButtonTRAINING"));
		if (buttonTraining)
			buttonTraining->winHide(TRUE);
	}
	buttonUSA = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonUSAID );
	buttonGLA = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonGLAID );
	buttonChina = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonChinaID );
	buttonUSARecentSave = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonUSARecentSaveID );
	buttonUSALoadGame = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonUSALoadGameID );
	buttonGLARecentSave = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonGLARecentSaveID );
	buttonGLALoadGame = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonGLALoadGameID );
	buttonChinaRecentSave = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonChinaRecentSaveID );
	buttonChinaLoadGame = TheWindowManager->winGetWindowFromId( parentMainMenu, buttonChinaLoadGameID );

	dropDownWindows[DROPDOWN_SINGLE] = TheWindowManager->winGetWindowFromId( parentMainMenu, TheNameKeyGenerator->nameToKey( "MainMenu.wnd:MapBorder" ));
	dropDownWindows[DROPDOWN_MULTIPLAYER] = TheWindowManager->winGetWindowFromId( parentMainMenu, TheNameKeyGenerator->nameToKey( "MainMenu.wnd:MapBorder1" ) );
	dropDownWindows[DROPDOWN_MAIN] = TheWindowManager->winGetWindowFromId( parentMainMenu, TheNameKeyGenerator->nameToKey( "MainMenu.wnd:MapBorder2" ) );
	dropDownWindows[DROPDOWN_LOADREPLAY] = TheWindowManager->winGetWindowFromId( parentMainMenu, TheNameKeyGenerator->nameToKey( "MainMenu.wnd:MapBorder3" ) );
	dropDownWindows[DROPDOWN_DIFFICULTY] = TheWindowManager->winGetWindowFromId( parentMainMenu, TheNameKeyGenerator->nameToKey( "MainMenu.wnd:MapBorder4" ) );
	for(i = 1; i < DROPDOWN_COUNT; ++i)
		dropDownWindows[i]->winHide(TRUE);

	initialHide();

	showSelectiveButtons(SHOW_NONE);
	// Set up the version number
#if defined(RTS_DEBUG) || defined RTS_PROFILE
	WinInstanceData instData;
#ifdef TEST_COMPRESSION
	instData.init();
	BitSet( instData.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	instData.m_textLabelString = "Debug: Compress/Decompress Maps";
	instData.setTooltipText(L"Only Used in Debug and Internal!");
	buttonCompressTest = TheWindowManager->gogoGadgetPushButton( parentMainMenu,
																									 WIN_STATUS_ENABLED | WIN_STATUS_IMAGE,
																									 25, 175,
																									 400, 400,
																									 &instData, nullptr, TRUE );
#endif // TEST_COMPRESSION

	instData.init();
	BitSet( instData.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	instData.m_textLabelString = "Debug: Load Map";

	instData.setTooltipText(L"Only Used in Debug and Internal!");
	buttonCampaign = TheWindowManager->gogoGadgetPushButton( parentMainMenu,
																									 WIN_STATUS_ENABLED,
																									 25, 54,
																									 180, 26,
																									 &instData, nullptr, TRUE );
#endif

	initLabelVersion();

	//TheShell->registerWithAnimateManager(buttonCampaign, WIN_ANIMATION_SLIDE_LEFT, TRUE, 800);
	//TheShell->registerWithAnimateManager(buttonSkirmish, WIN_ANIMATION_SLIDE_LEFT, TRUE, 600);
//	TheShell->registerWithAnimateManager(buttonSinglePlayer, WIN_ANIMATION_SLIDE_LEFT, TRUE, 400);
//	TheShell->registerWithAnimateManager(buttonMultiPlayer, WIN_ANIMATION_SLIDE_LEFT, TRUE, 200);
//	TheShell->registerWithAnimateManager(buttonOptions, WIN_ANIMATION_SLIDE_LEFT, TRUE, 1);
//	TheShell->registerWithAnimateManager(buttonExit, WIN_ANIMATION_SLIDE_RIGHT, TRUE, 1);
//
	layout->hide( FALSE );

	/*
	if (!checkedForUpdate)
	{
		DWORD state = 0;
		Bool isConnected = InternetGetConnectedState(&state, 0);
		if (isConnected && !(state & INTERNET_CONNECTION_MODEM_BUSY))
		{
			// wohoo - we're connected!  fire off a check for updates
			checkedForUpdate = TRUE;
			DEBUG_LOG(("Looking for a patch for productID=%d, versionStr=%s, distribution=%d",
				gameProductID, gameVersionUniqueIDStr, gameDistributionID));
			ptCheckForPatch( gameProductID, gameVersionUniqueIDStr, gameDistributionID, patchAvailableCallback, PTFalse, nullptr );
			//ptCheckForPatch( productID, versionUniqueIDStr, distributionID, mapPackAvailableCallback, PTFalse, nullptr );
		}
	}
	if (getUpdate != nullptr)
	{
		getUpdate->winHide( TRUE );
		//getUpdate->winEnable( FALSE );
	}
	/**/

	if (TheGameSpyPeerMessageQueue && !TheGameSpyPeerMessageQueue->isConnected())
	{
		DEBUG_LOG(("Tearing down GameSpy from MainMenuInit()"));
		TearDownGameSpy();
	}
	if (TheMapCache)
		TheMapCache->updateCache();

	/*
	if (MOTDBuffer && buttonMOTD)
	{
		buttonMOTD->winHide(FALSE);
	}
	*/

	TheShell->loadScheme("MainMenu");
	raiseMessageBoxes = TRUE;

//	if(!localAnimateWindowManager)
//		localAnimateWindowManager = NEW AnimateWindowManager;

	//pendingDropDown =DROPDOWN_MAIN;


	GameWindow *rule = TheWindowManager->winGetWindowFromId( parentMainMenu, TheNameKeyGenerator->nameToKey( "MainMenu.wnd:MainMenuRuler" ) );
	if(rule)
		rule->winHide(TRUE);
	campaignSelected = FALSE;
//	dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
	if(FirstTimeRunningTheGame)
	{
		TheMouse->setVisibility(FALSE);

		TheTransitionHandler->reverse("FadeWholeScreen");
		FirstTimeRunningTheGame  = FALSE;
	}
	else
	{
		showFade = TRUE;
		justEntered = TRUE;
		initialGadgetDelay = 2;
		if(rule)
		rule->winHide(FALSE);
	}

	layout->bringForward();
	// set keyboard focus to main parent
	TheWindowManager->winSetFocus( parentMainMenu );

	// -mpmenu auto-launch: hide the entire main menu so the player never
	// sees a half-rendered shell. The state machine in MainMenuUpdate
	// will spawn a "Connecting to multiplayer server..." popup on the
	// first update, then run the relay pre-flight, then either push the
	// LAN lobby or quit with an error popup.
	{
		extern Bool g_launchToMpMenu;
		if (g_launchToMpMenu)
		{
			// Hide the entire layout. The MessageBox.wnd popup we'll
			// spawn next is a top-level window from winCreateFromScript,
			// so it isn't affected by hiding parentMainMenu.
			if (parentMainMenu)
				parentMainMenu->winHide(TRUE);
			for (Int j = 1; j < DROPDOWN_COUNT; ++j)
			{
				if (dropDownWindows[j])
					dropDownWindows[j]->winHide(TRUE);
			}
			s_mpAutoLaunchPhase = MPAL_PENDING_START;
			g_launchToMpMenu = FALSE; // consume the request
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Main menu shutdown method */
//-------------------------------------------------------------------------------------------------
void MainMenuShutdown( WindowLayout *layout, void *userData )
{
	if (!startGame)
		isShuttingDown = TRUE;

	CancelPatchCheckCallback();

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;

//	if(winVidManager)
	//		delete winVidManager;
	//	winVidManager = nullptr;


	if( popImmediate )
	{
//		if(localAnimateWindowManager)
//		{
//			delete localAnimateWindowManager;
//			localAnimateWindowManager = nullptr;
//		}
		shutdownComplete( layout );
		return;

	}

	if (!startGame)
		TheShell->reverseAnimatewindow();
	//TheShell->reverseAnimatewindow();
//	if(localAnimateWindowManager && dropDown != DROPDOWN_NONE)
//		localAnimateWindowManager->reverseAnimateWindow();
}

extern Bool DontShowMainMenu;

////////////////////////////////////////////////////////////////////////////
//Allows the user to confirm the change, goes back to the previous mode
//if the time to change expires.
////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
// Accept Resolution callback method
//-------------------------------------------------------------------------------------------------
void AcceptResolution()
{
	//Keep new settings and bail with setting the display changed flag
	//set to off
	oldDispSettings = newDispSettings;
	dispChanged = FALSE;
}

//-------------------------------------------------------------------------------------------------
// Decline Resolution callback method
//-------------------------------------------------------------------------------------------------
void DeclineResolution()
{
	//Revert back to old resolution and reset all necessary
	//parts of the shell

	if (TheDisplay->setDisplayMode(oldDispSettings.xRes, oldDispSettings.yRes,
										oldDispSettings.bitDepth, oldDispSettings.windowed))
	{
		dispChanged = FALSE;
		newDispSettings = oldDispSettings;

		TheWritableGlobalData->m_xResolution = newDispSettings.xRes;
		TheWritableGlobalData->m_yResolution = newDispSettings.yRes;

		TheHeaderTemplateManager->onResolutionChanged();
		TheMouse->onResolutionChanged();

		AsciiString prefString;
		prefString.format("%d %d", newDispSettings.xRes, newDispSettings.yRes);

		OptionPreferences optionPref;
		optionPref["Resolution"] = prefString;
		optionPref.write();

		TheShell->recreateWindowLayouts();

		TheInGameUI->recreateControlBar();
	}
}

//-------------------------------------------------------------------------------------------------
// Accept/Decline Resolution dialog method
//-------------------------------------------------------------------------------------------------
void DoResolutionDialog()
{
	//Bring up a dialog to accept the resolution chosen in the options menu
	UnicodeString resolutionNew;

	UnicodeString resTimerString = TheGameText->fetch("GUI:Resolution");

	resolutionNew.format(L": %dx%d\n", newDispSettings.xRes , newDispSettings.yRes);

	resTimerString.concat(resolutionNew);


	resAcceptMenu = TheWindowManager->gogoMessageBox( CORNER, CORNER, -1, -1,MSG_BOX_OK | MSG_BOX_CANCEL ,
																									 TheGameText->fetch("GUI:Resolution"),
																									 resTimerString, nullptr, nullptr, AcceptResolution,
																									 DeclineResolution);
}

/* This function is not being currently used because we do not need a timer on the
// dialog box.
//-------------------------------------------------------------------------------------------------
//ResolutionDialogUpdate() - if resolution dialog box is shown, this must count 10 seconds for
//	accepting resolution changes otherwise we go back to previous display settings
//-------------------------------------------------------------------------------------------------
void ResolutionDialogUpdate()
{
	if (timeStarted == 0 && currentTime == 0)
	{
		timeStarted = currentTime = time(nullptr);
	}
	else
	{
		currentTime = time(nullptr);
	}

	if ( ( currentTime - timeStarted ) >= TIME_OUT)
	{
		currentTime = timeStarted = 0;
		DeclineResolution();
	}
	//------------------------------------------------------------------------------------------------------
	// Used for debugging purposes
	//------------------------------------------------------------------------------------------------------
	DEBUG_LOG(("Resolution Timer :  started at %d,  current time at %d, frameTicker is %d", timeStarted,
							time(nullptr) , currentTime));
}
*/

//-------------------------------------------------------------------------------------------------
/** Main menu update method */
//-------------------------------------------------------------------------------------------------
void DownloadMenuUpdate( WindowLayout *layout, void *userData );
// runMpAutoLaunchUpdate is defined further up in this file (alongside
// the other -mpmenu helpers). The early-return path at the top of
// MainMenuUpdate just dispatches into it.

void MainMenuUpdate( WindowLayout *layout, void *userData )
{
	if( TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() )
	{
		return;
	}

	// -mpmenu auto-launch path. While the state machine is running we
	// keep the main menu hidden and skip every transition / fade /
	// gadget-delay tick — we don't want the standard logo fade-in to
	// flash a half-rendered shell behind the connecting popup.
	if (s_mpAutoLaunchPhase != MPAL_INACTIVE && s_mpAutoLaunchPhase != MPAL_DONE)
	{
		if (parentMainMenu && !parentMainMenu->winIsHidden())
			parentMainMenu->winHide(TRUE);
		runMpAutoLaunchUpdate();
		return;
	}

	if(DontShowMainMenu && justEntered)
		justEntered = FALSE;

	if (TheDownloadManager && !TheDownloadManager->isDone())
	{
		TheDownloadManager->update();
		DownloadMenuUpdate(layout, userData);
	}

	/* This is also commented for the same reason as the top
	if (dispChanged)
	{
		ResolutionDialogUpdate();
		return;
	}
	*/

	if(justEntered)
	{
		if(initialGadgetDelay == 1)
		{
			TheTransitionHandler->setGroup("MainMenuDefaultMenuLogoFade");
			TheWindowManager->winSetFocus( parentMainMenu );
			initialGadgetDelay = 2;
			justEntered = FALSE;
		}
		else
			initialGadgetDelay--;
	}

	if(dontAllowTransitions && TheTransitionHandler->isFinished())
		dontAllowTransitions = FALSE;

	if(showLogo && dontAllowTransitions == FALSE)
	{
//		if(showFrames == SHOW_FRAMES_LIMIT)
//		{
//			TheTransitionHandler->remove("MainMenuSinglePlayerMenu");
			switch (showSide) {
			case SHOW_TRAINING:
				TheTransitionHandler->setGroup("MainMenuFactionTraining");
				break;
			case SHOW_CHINA:
				TheTransitionHandler->setGroup("MainMenuFactionChina");
				break;
			case SHOW_GLA:
				TheTransitionHandler->setGroup("MainMenuFactionGLA");
				break;
			case SHOW_USA:
				TheTransitionHandler->setGroup("MainMenuFactionUS");
				break;
			case SHOW_SKIRMISH:
				TheTransitionHandler->setGroup("MainMenuFactionSkirmish");
				break;
			}
			showLogo = FALSE;
//			showFrames = 0;
//			logoIsShown = TRUE;
//		}
//		else
//			showFrames++;
	}

//	if(showFade)
//	{
//		showFade = FALSE;
//		TheTransitionHandler->reverse("FadeWholeScreen");
//	}
////
//	if (notShown)
//	{
//		if(initialGadgetDelay == 1)
//		{
//			dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
//			TheTransitionHandler->setGroup("MainMenuFade", TRUE);
//			TheTransitionHandler->setGroup("MainMenuDefaultMenu");
//			TheMouse->setVisibility(TRUE);
//			initialGadgetDelay = 2;
//			notShown = FALSE;
//		}
//		else
//			initialGadgetDelay--;
//	}

	if (raiseMessageBoxes)
	{
		RaiseGSMessageBox();
		raiseMessageBoxes = FALSE;
	}

	HTTPThinkWrapper();
	GameSpyUpdateOverlays();
//	if(localAnimateWindowManager)
//		localAnimateWindowManager->update();
//	if(localAnimateWindowManager && pendingDropDown != DROPDOWN_NONE && localAnimateWindowManager->isFinished())
//	{
//		localAnimateWindowManager->reset();
//		if(dropDown != DROPDOWN_NONE)
//			dropDownWindows[dropDown]->winHide(TRUE);
//		dropDown = pendingDropDown;
//		dropDownWindows[dropDown]->winHide(FALSE);
//		localAnimateWindowManager->registerGameWindow(dropDownWindows[dropDown],WIN_ANIMATION_SLIDE_TOP_FAST,TRUE,1,1);
//		//buttonPushed = FALSE;
//		pendingDropDown = DROPDOWN_NONE;
//	}
//	else if(localAnimateWindowManager && dropDown == DROPDOWN_NONE && pendingDropDown == DROPDOWN_NONE && localAnimateWindowManager->isReversed() && localAnimateWindowManager->isFinished())
//	{
//		localAnimateWindowManager->reset();
//		for(Int i = 1; i < DROPDOWN_COUNT; ++i)
//			dropDownWindows[i]->winHide(TRUE);
//	}





	if (startGame && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
	{
		doGameStart();
	}

	// (auto-launch state machine moved to runMpAutoLaunchUpdate, called
	//  from the early-return block at the top of MainMenuUpdate.)

	// We'll only be successful if we've requested to
	if(isShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
	{
		shutdownComplete(layout);
	}


	// We'll only be successful if we've requested to
//	if(TheShell->isAnimReversed() && TheShell->isAnimFinished())
//		shutdownComplete( layout );

//	if(winVidManager)
//		winVidManager->update();

}

//-------------------------------------------------------------------------------------------------
/** Main menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType MainMenuInput( GameWindow *window, UnsignedInt msg,
																		WindowMsgData mData1, WindowMsgData mData2 )
{
	if(!notShown)
		return MSG_IGNORED;

	switch( msg )
	{
		// --------------------------------------------------------------------------------------------
		case GWM_MOUSE_POS:
		{
			Bool doShow = !TheGlobalData->m_shellMapOn;

			if (!doShow)
			{
				ICoord2D mouse;
				mouse.x = mData1 & 0xFFFF;
				mouse.y = mData1 >> 16;

				static Int mousePosX = mouse.x;
				static Int mousePosY = mouse.y;

				doShow = abs(mouse.x - mousePosX) > 20 || abs(mouse.y - mousePosY) > 20;
			}

			if (doShow)
			{
				initialGadgetDelay = 1;
				dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
				TheTransitionHandler->setGroup("MainMenuFade", TRUE);
				TheTransitionHandler->setGroup("MainMenuDefaultMenu");
				TheMouse->setVisibility(TRUE);
				notShown = FALSE;
				return MSG_HANDLED;
			}

			break;
		}

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			initialGadgetDelay = 1;
			dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
			TheTransitionHandler->setGroup("MainMenuFade", TRUE);
			TheTransitionHandler->setGroup("MainMenuDefaultMenu");
			TheMouse->setVisibility(TRUE);
			notShown = FALSE;
			return MSG_HANDLED;
		}
	}

	return MSG_IGNORED;
}

void PrintOffsetsFromControlBarParent();
//-------------------------------------------------------------------------------------------------
/** Main menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType MainMenuSystem( GameWindow *window, UnsignedInt msg,
										 WindowMsgData mData1, WindowMsgData mData2 )
{
	static Bool triedToInitWOLAPI = FALSE;
	static Bool canInitWOLAPI = FALSE;

	switch( msg )
	{

		//---------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{
			ghttpStartup();
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{
			ghttpCleanup();
			DEBUG_LOG(("Tearing down GameSpy from MainMenuSystem(GWM_DESTROY)"));
			TearDownGameSpy();
			StopAsyncDNSCheck(); // kill off the async DNS check thread in case it is still running
			break;

		}

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			break;

		}
		//---------------------------------------------------------------------------------------------
		case GBM_MOUSE_ENTERING:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
			if(controlID == onlineID)
			{
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_ONLINE_HIGHLIGHTED]);
			}
			else if(controlID == networkID)
			{
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_NETWORK_HIGHLIGHTED]);
			}
			else if(controlID == optionsID)
			{
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_OPTIONS_HIGHLIGHTED]);
			}
			else if(controlID == exitID)
			{
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_EXIT_HIGHLIGHTED]);
			}
			else if(controlID == buttonChallengeID)
			{
				if(dontAllowTransitions && !campaignSelected)
				{
					showLogo = TRUE;
					showSide = SHOW_TRAINING;
				}

				if(campaignSelected || dontAllowTransitions)
					break;

				TheTransitionHandler->setGroup("MainMenuFactionTraining");
			}
/*			else if(controlID == buttonTRAININGID)
			{
				if(dontAllowTransitions && !campaignSelected)
				{
					showLogo = TRUE;
					showSide = SHOW_TRAINING;
				}

				if(campaignSelected || dontAllowTransitions)
					break;
				//TheTransitionHandler->remove("MainMenuSinglePlayerMenu");

				TheTransitionHandler->setGroup("MainMenuFactionTraining");

				//showSelectiveButtons(SHOW_NONE);
			}
*/			else if(controlID == skirmishID)
			{
				if(dontAllowTransitions && !campaignSelected)
				{
					showLogo = TRUE;
					showSide = SHOW_SKIRMISH;
				}

				if(campaignSelected || dontAllowTransitions)
					break;
				//TheTransitionHandler->remove("MainMenuSinglePlayerMenu");

				TheTransitionHandler->setGroup("MainMenuFactionSkirmish");
				//showSelectiveButtons(SHOW_NONE);
			}

			else if(controlID == buttonUSAID)
			{
				if(dontAllowTransitions && !campaignSelected)
				{
					showLogo = TRUE;
					showSide = SHOW_USA;
				}

				if(campaignSelected || dontAllowTransitions)
					break;
				//TheTransitionHandler->remove("MainMenuSinglePlayerMenu");

				TheTransitionHandler->setGroup("MainMenuFactionUS");
//				showLogo = TRUE;
//				showFrames = 0;
//				showSide = SHOW_USA;

			}
			else if(controlID == buttonGLAID)
			{
				if(dontAllowTransitions && !campaignSelected)
				{
					showLogo = TRUE;
					showSide = SHOW_GLA;
				}

				if(campaignSelected || dontAllowTransitions)
					break;
				//TheTransitionHandler->remove("MainMenuSinglePlayerMenu");
				TheTransitionHandler->setGroup("MainMenuFactionGLA");
//				showLogo = TRUE;
//				showFrames = 0;
//				showSide = SHOW_GLA;
			}
			else if(controlID == buttonChinaID)
			{
				if(dontAllowTransitions && !campaignSelected)
				{
					showLogo = TRUE;
					showSide = SHOW_CHINA;
				}
				if(campaignSelected || dontAllowTransitions)
					break;
				//TheTransitionHandler->remove("MainMenuSinglePlayerMenu");
				TheTransitionHandler->setGroup("MainMenuFactionChina");
//				showLogo = TRUE;
//				showFrames = 0;
//				showSide = SHOW_CHINA;
			}

		break;
		}
		//---------------------------------------------------------------------------------------------
		case GBM_MOUSE_LEAVING:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			if(controlID == onlineID)
			{
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_ONLINE_UNHIGHLIGHTED]);
			}
			else if(controlID == networkID)
			{
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_NETWORK_UNHIGHLIGHTED]);
			}
			else if(controlID == optionsID)
			{
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_OPTIONS_UNHIGHLIGHTED]);
			}
			else if(controlID == exitID)
			{
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_EXIT_UNHIGHLIGHTED]);
			}
			else if(controlID == buttonChallengeID)
			{
				if(dontAllowTransitions && !campaignSelected && showLogo)
				{
					showLogo = FALSE;
					showSide = SHOW_NONE;
				}

				if(campaignSelected || dontAllowTransitions)
					break;

				// we'll just use the training logo anim for now
				TheTransitionHandler->reverse("MainMenuFactionTraining");
			}
/*			else if(controlID == buttonTRAININGID)
			{
				if(dontAllowTransitions && !campaignSelected && showLogo)
				{
					showLogo = FALSE;
					showSide = SHOW_NONE;
				}

				if(campaignSelected || dontAllowTransitions)
					break;
				TheTransitionHandler->reverse("MainMenuFactionTraining");

				//showSelectiveButtons(SHOW_NONE);
			}
*/			else if(controlID == skirmishID)
			{
				if(dontAllowTransitions && !campaignSelected && showLogo)
				{
					showLogo = FALSE;
					showSide = SHOW_NONE;
				}
				if(campaignSelected || dontAllowTransitions)
					break;
				TheTransitionHandler->reverse("MainMenuFactionSkirmish");
				//showSelectiveButtons(SHOW_NONE);
			}
			else if(controlID == buttonUSAID)
			{
				if(dontAllowTransitions && !campaignSelected && showLogo)
				{
					showLogo = FALSE;
					showSide = SHOW_NONE;
				}
				if(campaignSelected || dontAllowTransitions)
					break;
				TheTransitionHandler->reverse("MainMenuFactionUS");

				//showSelectiveButtons(SHOW_NONE);
			}
			else if(controlID == buttonGLAID)
			{
				if(dontAllowTransitions && !campaignSelected && showLogo)
				{
					showLogo = FALSE;
					showSide = SHOW_NONE;
				}
				if(campaignSelected || dontAllowTransitions)
					break;
				TheTransitionHandler->reverse("MainMenuFactionGLA");
				//showSelectiveButtons(SHOW_NONE);
			}
			else if(controlID == buttonChinaID)
			{
				if(dontAllowTransitions && !campaignSelected && showLogo)
				{
					showLogo = FALSE;
					showSide = SHOW_NONE;
				}
				if(campaignSelected || dontAllowTransitions)
					break;
				TheTransitionHandler->reverse("MainMenuFactionChina");
				//showSelectiveButtons(SHOW_NONE);
			}
		break;
		}
		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{

			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			if(buttonPushed)
				break;
#if defined(RTS_DEBUG) || defined RTS_PROFILE
			if( control == buttonCampaign )
			{
				buttonPushed = TRUE;
				TheShell->push("Menus/MapSelectMenu.wnd");
				// As soon as we have a campaign, add it in here!;
			}
#ifdef TEST_COMPRESSION
			else if( control == buttonCompressTest )
			{
				DoCompressTest();
			}
#endif // TEST_COMPRESSION
			else
#endif

			// don't allow mouse click slop that occurs during transitions to unset this flag
			if (TheTransitionHandler->isFinished()
				&& controlID != buttonEasyID && controlID != buttonMediumID && controlID != buttonHardID)
			{
				// this toggle must only be reset if one of these buttons have not been pressed
				// ...the difficulty selection behavior must have a chance to act upon this toggle
				launchChallengeMenu = FALSE;
			}


			if( controlID == buttonSinglePlayerID )
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				//buttonPushed = TRUE;
				buttonPushed = FALSE;
				dropDownWindows[DROPDOWN_SINGLE]->winHide(FALSE);
				TheTransitionHandler->remove("MainMenuDefaultMenu");
				TheTransitionHandler->reverse("MainMenuDefaultMenuBack");
				TheTransitionHandler->setGroup("MainMenuSinglePlayerMenu");
			}
			else if( controlID == buttonSingleBackID )
			{
				if(campaignSelected || dontAllowTransitions)
					break;
				buttonPushed = FALSE;
				dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
				TheTransitionHandler->remove("MainMenuSinglePlayerMenu");
				TheTransitionHandler->reverse("MainMenuSinglePlayerMenuBack");
				TheTransitionHandler->setGroup("MainMenuDefaultMenu");
				dontAllowTransitions = TRUE;
			}
			else if( controlID == buttonMultiBackID )
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				buttonPushed = FALSE;
				dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
				TheTransitionHandler->remove("MainMenuMultiPlayerMenu");
				TheTransitionHandler->reverse("MainMenuMultiPlayerMenuReverse");
				TheTransitionHandler->setGroup("MainMenuDefaultMenu");
			}
			else if( controlID == buttonLoadReplayBackID )
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				buttonPushed = FALSE;
				dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
				TheTransitionHandler->remove("MainMenuLoadReplayMenu");
				TheTransitionHandler->reverse("MainMenuLoadReplayMenuBack");
				TheTransitionHandler->setGroup("MainMenuDefaultMenu");
			}

			else if( control == buttonCredits )
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				buttonPushed = TRUE;
				TheShell->push("Menus/CreditsMenu.wnd" );
				dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
				TheTransitionHandler->reverse("MainMenuDefaultMenu");
			}
			else if( controlID == buttonMultiPlayerID)
			{
				if(dontAllowTransitions)
					break;

				// Run the relay pre-flight (host present + TCP connect).
				// On failure, surface the message via the OS dialog and
				// stay on the main menu — the player can fix and retry.
				// Same probe the -mpmenu auto-launch path uses.
				{
					extern char g_relayServerHost[256];
					char errmsg[320];
					if (!relayPreflight(errmsg, sizeof(errmsg)))
					{
						const char *title = (g_relayServerHost[0] == '\0')
							? "Multiplayer Unavailable" : "Connection Failed";
						MessageBoxA(ApplicationHWnd, errmsg, title, MB_OK | MB_ICONERROR);
						dontAllowTransitions = FALSE;
						break;
					}
				}

				dontAllowTransitions = TRUE;
				buttonPushed = TRUE;
				dropDownWindows[DROPDOWN_MAIN]->winHide(FALSE);
				TheTransitionHandler->reverse("MainMenuDefaultMenu");
				TheShell->push( "Menus/LanLobbyMenu.wnd" );
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_NETWORK_SELECTED]);
			}
			else if( controlID == buttonLoadReplayID)
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				//buttonPushed = TRUE;
				buttonPushed = FALSE;
				dropDownWindows[DROPDOWN_LOADREPLAY]->winHide(FALSE);
				TheTransitionHandler->remove("MainMenuDefaultMenu");
				TheTransitionHandler->reverse("MainMenuDefaultMenuBack");
				TheTransitionHandler->setGroup("MainMenuLoadReplayMenu");
			}
			else if( controlID == buttonLoadID )
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
//				SaveLoadLayoutType layoutType = SLLT_LOAD_ONLY;
//        WindowLayout *saveLoadMenuLayout = TheShell->getSaveLoadMenuLayout();
//				DEBUG_ASSERTCRASH( saveLoadMenuLayout, ("Unable to get save load menu layout.") );
//				saveLoadMenuLayout->runInit( &layoutType );
//				saveLoadMenuLayout->hide( FALSE );
//				saveLoadMenuLayout->bringForward();
				buttonPushed = TRUE;
				dropDownWindows[DROPDOWN_LOADREPLAY]->winHide(FALSE);
				TheTransitionHandler->reverse("MainMenuLoadReplayMenuBackTransition");
				TheShell->push("Menus/SaveLoad.wnd");

			}
			else if( controlID == buttonReplayID )
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				buttonPushed = TRUE;
				dropDownWindows[DROPDOWN_LOADREPLAY]->winHide(FALSE);
				TheTransitionHandler->reverse("MainMenuLoadReplayMenuBackTransition");
				TheShell->push("Menus/ReplayMenu.wnd");
			}
			else if( controlID == skirmishID )
			{
				if(campaignSelected || dontAllowTransitions)
					break;
				buttonPushed = TRUE;
				campaignSelected = TRUE;
				dropDownWindows[DROPDOWN_SINGLE]->winHide(FALSE);
				TheTransitionHandler->remove("MainMenuFactionSkirmish");

				TheTransitionHandler->reverse("MainMenuSinglePlayerMenuBackSkirmish");
#ifdef _CAMPEA_DEMO
				TheCampaignManager->setCampaign( "MD_CAMPEA_DEMO" );
/*
				TheTransitionHandler->setGroup("MainMenuDifficultyMenuUS");
				logoIsShown = FALSE;
				showLogo = FALSE;
				showSide = SHOW_USA;
*/
				prepareCampaignGame(DIFFICULTY_NORMAL);
				break;
#endif
				TheShell->push( "Menus/SkirmishGameOptionsMenu.wnd" );
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_SKIRMISH_SELECTED]);
			}
			else if( controlID == onlineID )
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				buttonPushed = TRUE;
				dropDownWindows[DROPDOWN_MULTIPLAYER]->winHide(FALSE);
				TheTransitionHandler->reverse("MainMenuMultiPlayerMenuTransitionToNext");

				StartPatchCheck();
//				localAnimateWindowManager->reverseAnimateWindow();
				dropDown = DROPDOWN_NONE;

			}
			else if( controlID == networkID )
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				buttonPushed = TRUE;
				dropDownWindows[DROPDOWN_MULTIPLAYER]->winHide(FALSE);
				TheTransitionHandler->reverse("MainMenuMultiPlayerMenuTransitionToNext");
				TheShell->push( "Menus/LanLobbyMenu.wnd" );

				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_NETWORK_SELECTED]);
			}
			else if( controlID == optionsID )
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				//buttonPushed = TRUE;
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_OPTIONS_SELECTED]);

				// load the options menu
				WindowLayout *optLayout = TheShell->getOptionsLayout(TRUE);
				DEBUG_ASSERTCRASH(optLayout != nullptr, ("unable to get options menu layout"));
				optLayout->runInit();
				optLayout->hide(FALSE);
				optLayout->bringForward();
			}
			else if( controlID == worldBuilderID )
			{
#if defined RTS_DEBUG
				if(_spawnl(_P_NOWAIT,"WorldBuilderD.exe","WorldBuilderD.exe", nullptr) < 0)
					MessageBoxOk(TheGameText->fetch("GUI:WorldBuilder"), TheGameText->fetch("GUI:WorldBuilderLoadFailed"),nullptr);
#else
				if(_spawnl(_P_NOWAIT,"WorldBuilder.exe","WorldBuilder.exe", nullptr) < 0)
					MessageBoxOk(TheGameText->fetch("GUI:WorldBuilder"), TheGameText->fetch("GUI:WorldBuilderLoadFailed"),nullptr);
#endif
			}
			else if( controlID == getUpdateID )
			{
				StartDownloadingPatches();
			}
			else if( controlID == exitID )
			{
				// If we ever want to add a dialog before we exit out of the game, uncomment this line and kill the quitCallback() line below.
//#if defined(RTS_DEBUG)
				if (TheGlobalData->m_windowed)
				{
					quitCallback();
//#else
				}
				else
				{
					QuitMessageBoxYesNo(TheGameText->fetch("GUI:QuitPopupTitle"), TheGameText->fetch("GUI:QuitPopupMessage"),quitCallback,nullptr);
				}
//#endif

			}
			else if(controlID == buttonChallengeID)
			{
				if(campaignSelected || dontAllowTransitions)
					break;

				// set up for the difficulty select into challenge menu
				TheTransitionHandler->setGroup("MainMenuFactionTraining");
				GameWindow *win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionTraining"));
				if(win)
					win->winHide(TRUE);
				TheTransitionHandler->reverse("MainMenuSinglePlayerMenuBackTraining");
				TheTransitionHandler->setGroup("MainMenuDifficultyMenuTraining");
				campaignSelected = TRUE;
				showLogo = FALSE;
				showSide = SHOW_TRAINING;
				launchChallengeMenu = TRUE;
			}


// This button has been removed for the mission disk -June 2003
/*			else if(controlID == buttonTRAININGID)
			{
				if(campaignSelected || dontAllowTransitions)
					break;
				TheCampaignManager->setCampaign( "TRAINING" );
				TheTransitionHandler->setGroup("MainMenuFactionTraining");
				TheTransitionHandler->remove("MainMenuFactionTraining", TRUE);
				GameWindow *win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionTraining"));
				if(win)
					win->winHide(TRUE);
				TheTransitionHandler->reverse("MainMenuSinglePlayerMenuBackTraining");
				TheTransitionHandler->setGroup("MainMenuDifficultyMenuTraining");
				campaignSelected = TRUE;
				showLogo = FALSE;
				showSide = SHOW_TRAINING;

//				setupGameStart(TheCampaignManager->getCurrentMap());
			}
*/			else if(controlID == buttonUSAID)
			{
				if(campaignSelected || dontAllowTransitions)
					break;
				TheCampaignManager->setCampaign( "USA" );
#ifdef _CAMPEA_DEMO
				TheCampaignManager->setCampaign( "MD_USA_1_DEMO" );
#endif
				TheTransitionHandler->setGroup("MainMenuFactionUS");
				TheTransitionHandler->remove("MainMenuFactionUS", TRUE);
				GameWindow *win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionUS"));
				if(win)
					win->winHide(TRUE);
				TheTransitionHandler->reverse("MainMenuSinglePlayerMenuBackUS");
				TheTransitionHandler->setGroup("MainMenuDifficultyMenuUS");
				campaignSelected = TRUE;
				logoIsShown = FALSE;
				showLogo = FALSE;
				showSide = SHOW_USA;
//				launchChallengeMenu = FALSE;
//				WindowLayout *layout = nullptr;
//				layout = TheWindowManager->winCreateLayout( "Menus/DifficultySelect.wnd" );
//				layout->runInit();
//				layout->hide( FALSE );
//				layout->bringForward();

//				setupGameStart(TheCampaignManager->getCurrentMap());
			}
			else if(controlID == buttonGLAID)
			{
				if(campaignSelected || dontAllowTransitions)
					break;
				TheCampaignManager->setCampaign( "GLA" );
#ifdef _CAMPEA_DEMO
				TheCampaignManager->setCampaign( "MD_USA_2_DEMO" );
#endif
				TheTransitionHandler->setGroup("MainMenuFactionGLA");
				TheTransitionHandler->remove("MainMenuFactionGLA", TRUE);
				GameWindow *win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionGLA"));
				if(win)
					win->winHide(TRUE);
				TheTransitionHandler->reverse("MainMenuSinglePlayerMenuBackGLA");
				TheTransitionHandler->setGroup("MainMenuDifficultyMenuGLA");
				campaignSelected = TRUE;
				logoIsShown = FALSE;
				showLogo = FALSE;
				showSide = SHOW_GLA;
//				launchChallengeMenu = FALSE;
//				WindowLayout *layout = nullptr;
//				layout = TheWindowManager->winCreateLayout( "Menus/DifficultySelect.wnd" );
//				layout->runInit();
//				layout->hide( FALSE );
//				layout->bringForward();

//				setupGameStart(TheCampaignManager->getCurrentMap());
			}
			else if(controlID == buttonChinaID)
			{
				if(campaignSelected || dontAllowTransitions)
					break;
				TheCampaignManager->setCampaign( "China" );
#ifdef _CAMPEA_DEMO
				TheCampaignManager->setCampaign( "MD_GLA_3_DEMO" );
#endif
				TheTransitionHandler->setGroup("MainMenuFactionChina");
				TheTransitionHandler->remove("MainMenuFactionChina", TRUE);
				GameWindow *win = TheWindowManager->winGetWindowFromId(parentMainMenu, TheNameKeyGenerator->nameToKey("MainMenu.wnd:WinFactionChina"));
				if(win)
					win->winHide(TRUE);
				TheTransitionHandler->reverse("MainMenuSinglePlayerMenuBackChina");
				TheTransitionHandler->setGroup("MainMenuDifficultyMenuChina");
				campaignSelected = TRUE;
				logoIsShown = FALSE;
				showLogo = FALSE;
				showSide = SHOW_CHINA;
//				launchChallengeMenu = FALSE;
//				WindowLayout *layout = nullptr;
//				layout = TheWindowManager->winCreateLayout( "Menus/DifficultySelect.wnd" );
//				layout->runInit();
//				layout->hide( FALSE );
//				layout->bringForward();

//				setupGameStart(TheCampaignManager->getCurrentMap());
			}
			else if(controlID == buttonEasyID)
			{
				if(dontAllowTransitions)
					break;

				prepareCampaignGame(DIFFICULTY_EASY);
			}
			else if(controlID == buttonMediumID)
			{
				if(dontAllowTransitions)
					break;

				prepareCampaignGame(DIFFICULTY_NORMAL);
			}
			else if(controlID == buttonHardID)
			{
				if(dontAllowTransitions)
					break;

				prepareCampaignGame(DIFFICULTY_HARD);
			}
			else if(controlID == buttonDiffBackID)
			{
				if(dontAllowTransitions)
					break;
				dontAllowTransitions = TRUE;
				TheCampaignManager->setCampaign( AsciiString::TheEmptyString );
				diffReverseSide();
				campaignSelected = FALSE;
			}


			break;

		}

		//---------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;

	}

	return MSG_HANDLED;

}

void diffReverseSide()
{
	switch (showSide) {
	case SHOW_TRAINING:
		TheTransitionHandler->reverse("MainMenuDifficultyMenuTrainingBack");
		TheTransitionHandler->setGroup("MainMenuSinglePlayerTrainingMenuFromDiff");
		break;
	case SHOW_USA:
		TheTransitionHandler->reverse("MainMenuDifficultyMenuUSBack");
		TheTransitionHandler->setGroup("MainMenuSinglePlayerUSAMenuFromDiff");
		break;
	case SHOW_GLA:
		TheTransitionHandler->reverse("MainMenuDifficultyMenuGLABack");
		TheTransitionHandler->setGroup("MainMenuSinglePlayerGLAMenuFromDiff");
		break;
	case SHOW_CHINA:
		TheTransitionHandler->reverse("MainMenuDifficultyMenuChinaBack");
		TheTransitionHandler->setGroup("MainMenuSinglePlayerChinaMenuFromDiff");

		break;
	}
}
