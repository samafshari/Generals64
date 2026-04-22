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

///////////////////////////////////////////////////////////////////////////////////////
// FILE: LanLobbyMenu.cpp
// Author: Chris Huybregts, October 2001
// Description: Lan Lobby Menu
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Lib/BaseType.h"
#include "Common/crc.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/MultiplayerSettings.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/QuotedPrintable.h"
#include "Common/OptionPreferences.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/ClientInstance.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameInfoWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/MessageBox.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/IPEnumeration.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/LANGameInfo.h"

Bool LANisShuttingDown = false;
Bool LANbuttonPushed = false;
Bool LANSocketErrorDetected = FALSE;
char *LANnextScreen = nullptr;

static Int	initialGadgetDelay = 2;
static Bool justEntered = FALSE;

// ── Lobby auto-launch state machine ──────────────────────────────────
// When the launcher passes either `-joingame "GameName"` or `-hostgame`,
// the lobby init arms a small state machine that runs after the lobby
// has settled. Two modes:
//
//   AJ_JOINING: poll TheLAN->LookupGame() each Update tick. When a
//     matching MSG_GAME_ANNOUNCE arrives, synthesize a click on the
//     in-game Join button by calling RequestGameJoin(). Failure
//     (timeout / RET_GAME_FULL / etc.) leaves the player in the
//     populated lobby via the existing OnGameJoin error path.
//
//   AJ_HOSTING: synthesize a click on the in-game Host button by
//     calling RequestGameCreate. The engine pushes the
//     LanGameOptionsMenu (the create-game / lobby-setup screen)
//     immediately after, so the player lands directly in the game
//     options dialog ready to pick a map and start the match.
extern UnicodeString g_joinGameName;
extern Bool          g_launcherHostGame;
enum AutoJoinPhase
{
	AJ_NONE = 0,    // not auto-launching
	AJ_JOINING,     // looking up game name in TheLAN->LookupGame each frame
	AJ_HOSTING,     // hosting a game on first idle frame
	AJ_DONE,        // already attempted (success path lives in OnGameJoin / LanGameOptionsMenuInit)
};
static AutoJoinPhase s_autoJoinPhase = AJ_NONE;
static UnicodeString s_autoJoinTarget;
static UnsignedInt   s_autoJoinDeadline = 0;
// How long we wait for the relay to surface the requested game before
// giving up. Three seconds is enough for a couple of MSG_GAME_ANNOUNCE
// retransmissions (the relay's resend cadence is ~10s but a freshly-
// connected client typically gets a snapshot within the first second).
static const UnsignedInt AUTO_JOIN_TIMEOUT_MS = 3000;



LANPreferences::LANPreferences()
{
	loadFromIniFile();
}

LANPreferences::~LANPreferences()
{
}

Bool LANPreferences::loadFromIniFile()
{
	if (rts::ClientInstance::getInstanceId() > 1u)
	{
		AsciiString fname;
		fname.format("Network_Instance%.2u.ini", rts::ClientInstance::getInstanceId());
		return load(fname);
	}

	return load("Network.ini");
}

UnicodeString LANPreferences::getUserName()
{
	UnicodeString ret;

	LANPreferences::const_iterator it = find("UserName");
	if (it != end())
	{
		// Found an user name. Use it if valid.
		ret = QuotedPrintableToUnicodeString(it->second);
		ret.trim();
		if (!ret.isEmpty())
		{
			return ret;
		}
	}

	if (rts::ClientInstance::getInstanceId() > 1u)
	{
		// to avoid duplicate names for multiple client instances.
		ret.format(L"Instance%.2d", rts::ClientInstance::getInstanceId());
		return ret;
	}

	// Use machine name as default user name.
	IPEnumeration IPs;
	ret.translate(IPs.getMachineName());
	return ret;
}

Int LANPreferences::getPreferredColor()
{
	// Returns the saved house color. With the launcher revamp this
	// is now a raw 0x00RRGGBB int (was a palette index in legacy
	// builds), or -1 for "random — pick at game start". Reject any
	// stored value outside [-1, 0xFFFFFF] in case Network.ini still
	// has an old palette index from before the cutover, in which
	// case we treat it as random and let populateRandomSideAndColor
	// pick a real RGB.
	Int ret;
	LANPreferences::const_iterator it = find("Color");
	if (it == end())
	{
		return -1;
	}

	ret = atoi(it->second.str());
	if (ret < -1 || ret > 0xFFFFFF)
		ret = -1;

	return ret;
}

Int LANPreferences::getPreferredFaction()
{
	Int ret;
	LANPreferences::const_iterator it = find("PlayerTemplate");
	if (it == end())
	{
		return PLAYERTEMPLATE_RANDOM;
	}

	ret = atoi(it->second.str());
	if (ret == PLAYERTEMPLATE_OBSERVER || ret < PLAYERTEMPLATE_MIN || ret >= ThePlayerTemplateStore->getPlayerTemplateCount())
		ret = PLAYERTEMPLATE_RANDOM;

	if (ret >= 0)
	{
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(ret);
		if (!fac)
			ret = PLAYERTEMPLATE_RANDOM;
		else if (fac->getStartingBuilding().isEmpty())
			ret = PLAYERTEMPLATE_RANDOM;
	}

	return ret;
}

Bool LANPreferences::usesSystemMapDir()
{
	OptionPreferences::const_iterator it = find("UseSystemMapDir");
	if (it == end())
		return TRUE;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

AsciiString LANPreferences::getPreferredMap()
{
	AsciiString ret;
	LANPreferences::const_iterator it = find("Map");
	if (it == end())
	{
		ret = getDefaultMap(TRUE);
		return ret;
	}

	ret = QuotedPrintableToAsciiString(it->second);
	ret.trim();
	if (ret.isEmpty() || !isValidMap(ret, TRUE))
	{
		ret = getDefaultMap(TRUE);
		return ret;
	}

	return ret;
}

Int LANPreferences::getNumRemoteIPs()
{
	Int ret;
	LANPreferences::const_iterator it = find("NumRemoteIPs");
	if (it == end())
	{
		ret = 0;
		return ret;
	}

	ret = atoi(it->second.str());
	return ret;
}

UnicodeString LANPreferences::getRemoteIPEntry(Int i)
{
	UnicodeString ret;
	AsciiString key;
	key.format("RemoteIP%d", i);

	AsciiString ipstr;
	AsciiString asciientry;

	LANPreferences::const_iterator it = find(key.str());
	if (it == end())
	{
		asciientry = "";
		return ret;
	}

	asciientry = it->second;

	asciientry.nextToken(&ipstr, ":");
	asciientry.set(asciientry.str() + 1); // skip the ':'

	ret.translate(ipstr);
	if (!asciientry.isEmpty())
	{
		ret.concat(L"(");
		ret.concat(QuotedPrintableToUnicodeString(asciientry));
		ret.concat(L")");
	}

	return ret;
}

static const char superweaponRestrictionKey[] = "SuperweaponRestrict";

Bool LANPreferences::getSuperweaponRestricted() const
{
  LANPreferences::const_iterator it = find(superweaponRestrictionKey);
  if (it == end())
  {
    return false;
  }

  return ( it->second.compareNoCase( "yes" ) == 0 );
}

void LANPreferences::setSuperweaponRestricted( Bool superweaponRestricted )
{
  (*this)[superweaponRestrictionKey] = superweaponRestricted ? "Yes" : "No";
}

static const char startingCashKey[] = "StartingCash";
Money LANPreferences::getStartingCash() const
{
  LANPreferences::const_iterator it = find(startingCashKey);
  if (it == end())
  {
    return TheMultiplayerSettings->getDefaultStartingMoney();
  }

  Money money;
  money.deposit( strtoul( it->second.str(), nullptr, 10 ), FALSE, FALSE );

  return money;
}

void LANPreferences::setStartingCash( const Money & startingCash )
{
  AsciiString option;

  option.format( "%d", startingCash.countMoney() );

  (*this)[startingCashKey] = option;
}

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////


// window ids ------------------------------------------------------------------------------
static NameKeyType parentLanLobbyID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType buttonClearID = NAMEKEY_INVALID;
static NameKeyType buttonHostID = NAMEKEY_INVALID;
static NameKeyType buttonJoinID = NAMEKEY_INVALID;
static NameKeyType buttonDirectConnectID = NAMEKEY_INVALID;
static NameKeyType buttonEmoteID = NAMEKEY_INVALID;
static NameKeyType staticToolTipID = NAMEKEY_INVALID;
static NameKeyType textEntryPlayerNameID = NAMEKEY_INVALID;
static NameKeyType textEntryChatID = NAMEKEY_INVALID;
static NameKeyType listboxPlayersID = NAMEKEY_INVALID;
static NameKeyType staticTextGameInfoID = NAMEKEY_INVALID;


// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentLanLobby = nullptr;
static GameWindow *buttonBack = nullptr;
static GameWindow *buttonClear = nullptr;
static GameWindow *buttonHost = nullptr;
static GameWindow *buttonJoin = nullptr;
static GameWindow *buttonDirectConnect = nullptr;
static GameWindow *buttonEmote = nullptr;
static GameWindow *staticToolTip = nullptr;
static GameWindow *textEntryPlayerName = nullptr;
static GameWindow *textEntryChat = nullptr;
static GameWindow *staticTextGameInfo = nullptr;

//external declarations of the Gadgets the callbacks can use
NameKeyType listboxChatWindowID = NAMEKEY_INVALID;
GameWindow *listboxChatWindow = nullptr;
GameWindow *listboxPlayers = nullptr;
NameKeyType listboxGamesID = NAMEKEY_INVALID;
GameWindow *listboxGames = nullptr;

// hack to disable framerate limiter in LAN games
//static Bool shellmapOn;
static Bool useFpsLimit;
static UnicodeString defaultName;

static void playerTooltip(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	Int x, y, row, col;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);

	GadgetListBoxGetEntryBasedOnXY(window, x, y, row, col);

	if (row == -1 || col == -1)
	{
		//TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:LobbyPlayers") );
		return;
	}

	UnsignedInt playerIP = static_cast<UnsignedInt>(reinterpret_cast<uintptr_t>(GadgetListBoxGetItemData( window, row, col )));
	LANPlayer *player = TheLAN->LookupPlayer(playerIP);
	if (!player)
	{
		DEBUG_CRASH(("No player info in listbox!"));
		//TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:LobbyPlayers") );
		return;
	}

	setLANPlayerTooltip(player);
}


//-------------------------------------------------------------------------------------------------
/** Initialize the Lan Lobby Menu */
//-------------------------------------------------------------------------------------------------
void LanLobbyMenuInit( WindowLayout *layout, void *userData )
{
	LANnextScreen = nullptr;
	LANbuttonPushed = false;
	LANisShuttingDown = false;

	// Pick up any pending -joingame request from the command line.
	// Consume the launcher's auto-launch globals so a back-and-forth
	// navigation through the lobby (e.g. user gets bounced out, comes
	// back) doesn't re-fire either action. -joingame takes precedence
	// over -hostgame if both happen to be set.
	if (!g_joinGameName.isEmpty())
	{
		s_autoJoinTarget = g_joinGameName;
		s_autoJoinPhase  = AJ_JOINING;
		s_autoJoinDeadline = timeGetTime() + AUTO_JOIN_TIMEOUT_MS;
		g_joinGameName.clear();
		g_launcherHostGame = FALSE;
	}
	else if (g_launcherHostGame)
	{
		s_autoJoinPhase  = AJ_HOSTING;
		s_autoJoinDeadline = timeGetTime() + AUTO_JOIN_TIMEOUT_MS;
		g_launcherHostGame = FALSE;
	}
	else
	{
		s_autoJoinPhase = AJ_NONE;
	}

	// get the ids for our controls
	parentLanLobbyID = TheNameKeyGenerator->nameToKey( "LanLobbyMenu.wnd:LanLobbyMenuParent" );
	buttonBackID = TheNameKeyGenerator->nameToKey( "LanLobbyMenu.wnd:ButtonBack" );
	buttonClearID = TheNameKeyGenerator->nameToKey( "LanLobbyMenu.wnd:ButtonClear" );
	buttonHostID = TheNameKeyGenerator->nameToKey( "LanLobbyMenu.wnd:ButtonHost" );
	buttonJoinID = TheNameKeyGenerator->nameToKey( "LanLobbyMenu.wnd:ButtonJoin" );
	buttonDirectConnectID = TheNameKeyGenerator->nameToKey( "LanLobbyMenu.wnd:ButtonDirectConnect" );
	buttonEmoteID = TheNameKeyGenerator->nameToKey( "LanLobbyMenu.wnd:ButtonEmote" );
	staticToolTipID = TheNameKeyGenerator->nameToKey( "LanLobbyMenu.wnd:StaticToolTip" );
	textEntryPlayerNameID = TheNameKeyGenerator->nameToKey( "LanLobbyMenu.wnd:TextEntryPlayerName" );
	textEntryChatID = TheNameKeyGenerator->nameToKey( "LanLobbyMenu.wnd:TextEntryChat" );
	listboxPlayersID = TheNameKeyGenerator->nameToKey( "LanLobbyMenu.wnd:ListboxPlayers" );
	listboxChatWindowID = TheNameKeyGenerator->nameToKey( "LanLobbyMenu.wnd:ListboxChatWindowLanLobby" );
	listboxGamesID = TheNameKeyGenerator->nameToKey( "LanLobbyMenu.wnd:ListboxGames" );
	staticTextGameInfoID = TheNameKeyGenerator->nameToKey( "LanLobbyMenu.wnd:StaticTextGameInfo" );


	// Get pointers to the window buttons
	parentLanLobby = TheWindowManager->winGetWindowFromId( nullptr, parentLanLobbyID );
	buttonBack = TheWindowManager->winGetWindowFromId( nullptr,  buttonBackID);
	buttonClear = TheWindowManager->winGetWindowFromId( nullptr,  buttonClearID);
	buttonHost = TheWindowManager->winGetWindowFromId( nullptr, buttonHostID );
	buttonJoin = TheWindowManager->winGetWindowFromId( nullptr, buttonJoinID );
	buttonDirectConnect = TheWindowManager->winGetWindowFromId( nullptr, buttonDirectConnectID );
	buttonEmote = TheWindowManager->winGetWindowFromId( nullptr,buttonEmoteID  );
	staticToolTip = TheWindowManager->winGetWindowFromId( nullptr, staticToolTipID );
	textEntryPlayerName = TheWindowManager->winGetWindowFromId( nullptr, textEntryPlayerNameID );
	textEntryChat = TheWindowManager->winGetWindowFromId( nullptr, textEntryChatID );
	listboxPlayers = TheWindowManager->winGetWindowFromId( nullptr, listboxPlayersID );
	listboxChatWindow = TheWindowManager->winGetWindowFromId( nullptr, listboxChatWindowID );
	listboxGames = TheWindowManager->winGetWindowFromId( nullptr, listboxGamesID );
	staticTextGameInfo = TheWindowManager->winGetWindowFromId( nullptr, staticTextGameInfoID );
	listboxPlayers->winSetTooltipFunc(playerTooltip);

	// ── Discombobulator-mode customizations ───────────────────────
	// We don't ship our own LanLobbyMenu.wnd — instead we patch the
	// stock layout in code so we never have to touch the binary asset:
	//   1. Title text → "Online Multiplayer" (was the localized
	//      "GUI:LANLobby" string from the .csf, which is wrong now
	//      that the lobby is fronted by our relay server, not LAN)
	//   2. Direct Connect button repurposed → "OPTIONS"; click handler
	//      below opens the standard Options menu layout
	//   3. Back button → "EXIT GAME" + click handler quits the engine
	//   4. Player-name CLEAR button hidden — the launcher already
	//      provides the canonical player name via -playername and
	//      we disable the text entry below when that's set, so the
	//      Clear button is either redundant or actively confusing
	{
		GameWindow *titleWin = TheWindowManager->winGetWindowFromId(
			nullptr, TheNameKeyGenerator->nameToKey("LanLobbyMenu.wnd:StaticTextTitle"));
		if (titleWin)
			GadgetStaticTextSetText(titleWin, UnicodeString(L"Online Multiplayer"));

		if (buttonDirectConnect)
			GadgetButtonSetText(buttonDirectConnect, UnicodeString(L"OPTIONS"));

		if (buttonBack)
			GadgetButtonSetText(buttonBack, UnicodeString(L"EXIT GAME"));

		if (buttonClear)
			buttonClear->winHide(TRUE);
	}

	// Show Menu
	layout->hide( FALSE );

	// Init LAN API Singleton
	if (!TheLAN)
	{
		TheLAN = NEW LANAPI();	/// @todo clh delete TheLAN and
		useFpsLimit = TheGlobalData->m_useFpsLimit;
	}
	else
	{
		TheWritableGlobalData->m_useFpsLimit = useFpsLimit;
		TheLAN->reset();
	}

	// Choose an IP address, then initialize the LAN singleton
	UnsignedInt IP = TheGlobalData->m_defaultIP;
	IPEnumeration IPs;
	const WideChar* IPSource;
	if (!IP)
	{
		EnumeratedIP *IPlist = IPs.getAddresses();
		/*
		while (IPlist && IPlist->getNext())
		{
			IPlist = IPlist->getNext();
		}
		*/
		DEBUG_ASSERTCRASH(IPlist, ("No IP addresses found!"));
		if (!IPlist)
		{
			/// @todo: display error and exit lan lobby if no IPs are found
		}

		IPSource = L"Local IP chosen";
		IP = IPlist->getIP();
	}
	else
	{
		IPSource = L"Default local IP";
	}
#if defined(RTS_DEBUG)
	UnicodeString str;
	str.format(L"%s: %d.%d.%d.%d", IPSource, PRINTF_IP_AS_4_INTS(IP));
	GadgetListBoxAddEntryText(listboxChatWindow, str, chatSystemColor, -1, 0);
#endif

	// TheLAN->init() sets us to be in a LAN menu screen automatically.
	TheLAN->init();
	if (TheLAN->SetLocalIP(IP) == FALSE) {
		LANSocketErrorDetected = TRUE;
	}

	//Initialize the gadgets on the window
	//UnicodeString	txtInput;
	//txtInput.translate(IPs.getMachineName());
	LANPreferences prefs;
	defaultName = prefs.getUserName();

	// Discombobulator: launcher-provided name override. When the
	// launcher passes -playername "X" (from an authenticated account)
	// we use it verbatim and hide the editable text entry entirely —
	// the launcher + server are the canonical source of identity.
	// If -authtoken was also set, g_authDisplayName may contain
	// the server-verified name (populated during token exchange).
	{
		extern UnicodeString g_launcherPlayerName;
		extern char g_authDisplayName[];
		// Prefer the server-verified auth display name if available
		if (g_authDisplayName[0] != '\0')
		{
			defaultName.translate(AsciiString(g_authDisplayName));
			if (textEntryPlayerName)
				textEntryPlayerName->winHide(TRUE);
		}
		else if (!g_launcherPlayerName.isEmpty())
		{
			defaultName = g_launcherPlayerName;
			if (textEntryPlayerName)
				textEntryPlayerName->winHide(TRUE);
		}
	}

	defaultName.truncateTo(g_lanPlayerNameLength);

	GadgetTextEntrySetText( textEntryPlayerName, defaultName);
	// Clear the text entry line
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);

	GadgetListBoxReset(listboxPlayers);
	GadgetListBoxReset(listboxGames);

	defaultName.truncateTo(g_lanPlayerNameLength);
	TheLAN->RequestSetName(defaultName);
	TheLAN->RequestLocations();

	/*
	UnicodeString unicodeChat;

	unicodeChat = L"Local IP list:";
	GadgetListBoxAddEntryText(listboxChatWindow, unicodeChat, chatSystemColor, -1, 0);

	IPlist = IPs.getAddresses();
	while (IPlist)
	{
		unicodeChat.translate(IPlist->getIPstring());
		GadgetListBoxAddEntryText(listboxChatWindow, unicodeChat, chatSystemColor, -1, 0);
		IPlist = IPlist->getNext();
	}
	*/

	// Set Keyboard to Main Parent
	//TheWindowManager->winSetFocus( parentLanLobby );
	TheWindowManager->winSetFocus( textEntryChat );
	CreateLANGameInfoWindow(staticTextGameInfo);

	//TheShell->showShellMap(FALSE);
	//shellmapOn = FALSE;
	// coming out of a game, re-load the shell map
	TheShell->showShellMap(TRUE);

	// check for MOTD
	TheLAN->checkMOTD();
	layout->hide(FALSE);
	layout->bringForward();

	justEntered = TRUE;
	initialGadgetDelay = 2;
	GameWindow *win = TheWindowManager->winGetWindowFromId(nullptr, TheNameKeyGenerator->nameToKey("LanLobbyMenu.wnd:GadgetParent"));
	if(win)
		win->winHide(TRUE);


	// animate controls
	//TheShell->registerWithAnimateManager(parentLanLobby, WIN_ANIMATION_SLIDE_TOP, TRUE);
//	TheShell->registerWithAnimateManager(buttonHost, WIN_ANIMATION_SLIDE_LEFT, TRUE, 600);
//	TheShell->registerWithAnimateManager(buttonJoin, WIN_ANIMATION_SLIDE_LEFT, TRUE, 400);
//	TheShell->registerWithAnimateManager(buttonDirectConnect, WIN_ANIMATION_SLIDE_LEFT, TRUE, 200);
//	//TheShell->registerWithAnimateManager(buttonOptions, WIN_ANIMATION_SLIDE_LEFT, TRUE, 1);
//	TheShell->registerWithAnimateManager(buttonBack, WIN_ANIMATION_SLIDE_RIGHT, TRUE, 1);

}

//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{

	LANisShuttingDown = false;

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout, (LANnextScreen != nullptr) );

	if (LANnextScreen != nullptr)
	{
		TheShell->push(LANnextScreen);
	}

	LANnextScreen = nullptr;

}

//-------------------------------------------------------------------------------------------------
/** Lan Lobby menu shutdown method */
//-------------------------------------------------------------------------------------------------
void LanLobbyMenuShutdown( WindowLayout *layout, void *userData )
{
	// Only persist the name back to Network.ini when the user could
	// actually edit it. If the launcher passed -playername, the field
	// is read-only and saving it would overwrite whatever the user
	// had stored before — losing their pre-launcher name forever.
	{
		extern UnicodeString g_launcherPlayerName;
		extern char g_authDisplayName[];
		// Skip writeback if the name came from a launcher/server override
		if (g_launcherPlayerName.isEmpty() && g_authDisplayName[0] == '\0')
		{
			LANPreferences prefs;
			prefs["UserName"] = UnicodeStringToQuotedPrintable(GadgetTextEntryGetText( textEntryPlayerName ));
			prefs.write();
		}
	}

	DestroyGameInfoWindow();
	// hide menu
	//layout->hide( TRUE );

	// TheLAN can be null here: when the user hits Back from the lobby,
	// the code at lines ~963-968 deletes TheLAN *after* queueing the
	// shell pop, then flips the engine quit flag. If the game exits
	// before the queued pop completes, Shell::deconstruct later runs
	// popImmediate() on the still-stacked lobby layout, and this
	// shutdown callback fires with TheLAN already gone — read-AV on
	// the vtable dereference. LanGameOptionsMenuShutdown guards the
	// same way (see LanGameOptionsMenu.cpp ~line 1396).
	if (TheLAN)
		TheLAN->RequestLobbyLeave( true );

	// Reset the LAN singleton
	//TheLAN->reset();

	// our shutdown is complete
	//TheShell->shutdownComplete( layout );
	TheWritableGlobalData->m_useFpsLimit = useFpsLimit;

	LANisShuttingDown = true;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;

	LANSocketErrorDetected = FALSE;

	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}

	TheShell->reverseAnimatewindow();
	TheTransitionHandler->reverse("LanLobbyFade");
	//if(	shellmapOn)
//		TheShell->showShellMap(TRUE);
}


//-------------------------------------------------------------------------------------------------
/** Lan Lobby menu update method */
//-------------------------------------------------------------------------------------------------
void LanLobbyMenuUpdate( WindowLayout * layout, void *userData)
{
	if (TheGameLogic->isInShellGame() && TheGameLogic->getFrame() == 1)
	{
		SignalUIInteraction(SHELL_SCRIPT_HOOK_LAN_ENTERED_FROM_GAME);
	}

	if(justEntered)
	{
		if(initialGadgetDelay == 1)
		{
			TheTransitionHandler->setGroup("LanLobbyFade");
			initialGadgetDelay = 2;
			justEntered = FALSE;
		}
		else
			initialGadgetDelay--;
	}

	if(LANisShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
		shutdownComplete(layout);

	if (TheShell->isAnimFinished() && !LANbuttonPushed && TheLAN)
		TheLAN->update();

	// -joingame / -hostgame auto-launch driver. Runs each frame after
	// TheLAN has had a chance to process inbound relay messages so the
	// in-memory game list reflects whatever the relay has pushed up
	// to this tick. We deliberately don't fire on the very first
	// frame — we let TheLAN->update() above run at least once and
	// let the lobby's intro fade settle so the player isn't dropped
	// into the next screen mid-transition.
	if ((s_autoJoinPhase == AJ_JOINING || s_autoJoinPhase == AJ_HOSTING)
		&& !LANbuttonPushed
		&& !LANisShuttingDown
		&& TheLAN
		&& TheShell->isAnimFinished()
		&& TheTransitionHandler->isFinished())
	{
		if (s_autoJoinPhase == AJ_JOINING)
		{
			LANGameInfo *target = TheLAN->LookupGame(s_autoJoinTarget);
			if (target != nullptr)
			{
				// Found it — synthesize the same call the in-game
				// Join button makes. RequestGameJoin sends
				// MSG_REQUEST_JOIN and waits for the host's
				// accept/deny via OnGameJoin. Success path pushes
				// LanGameOptionsMenu; failure path drops a system
				// chat line and leaves us right here.
				LANbuttonPushed = true;
				s_autoJoinPhase = AJ_DONE;
				TheLAN->RequestGameJoin(target);
			}
			else if (timeGetTime() >= s_autoJoinDeadline)
			{
				// Game never showed up. Could be: host stopped
				// hosting between launcher click and engine boot,
				// host's relay connection got a new sessionID, or
				// the user typed a stale game name. Either way,
				// give up silently — the player is now in the lobby
				// and can pick something else.
				s_autoJoinPhase = AJ_DONE;
			}
		}
		else // AJ_HOSTING
		{
			// Synthesize the in-game Host button click. Empty game
			// name = engine-default name (RequestGameCreate fills
			// in something like sessionID+seed+hostname). Engine
			// pushes LanGameOptionsMenu after the create succeeds,
			// dropping the player into the create-game / lobby
			// setup dialog directly.
			LANbuttonPushed = true;
			s_autoJoinPhase = AJ_DONE;
			TheLAN->RequestGameCreate(L"", FALSE);
		}
	}

	if (LANSocketErrorDetected == TRUE) {
		LANSocketErrorDetected = FALSE;
		DEBUG_LOG(("SOCKET ERROR!  BAILING!"));
		MessageBoxOk(TheGameText->fetch("GUI:NetworkError"), TheGameText->fetch("GUI:SocketError"), nullptr);

		// we have a socket problem, back out to the main menu.
		TheWindowManager->winSendSystemMsg(buttonBack->winGetParent(), GBM_SELECTED,
																			 (WindowMsgData)buttonBack, buttonBackID);
	}


}

//-------------------------------------------------------------------------------------------------
/** Lan Lobby menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanLobbyMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;
			if (LANbuttonPushed)
				break;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{

					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitIsSet( state, KEY_STATE_UP ) )
					{
						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED,
																							(WindowMsgData)buttonBack, buttonBackID );

					}

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}

			}

		}

	}

	return MSG_IGNORED;
}

//-------------------------------------------------------------------------------------------------
/** Lan Lobby menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanLobbyMenuSystem( GameWindow *window, UnsignedInt msg,
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;

	switch( msg )
	{


		case GWM_CREATE:
			{
				SignalUIInteraction(SHELL_SCRIPT_HOOK_LAN_OPENED);
				break;
			}

		case GWM_DESTROY:
			{
				SignalUIInteraction(SHELL_SCRIPT_HOOK_LAN_CLOSED);
				break;
			}

		case GWM_INPUT_FOCUS:
			{
				// if we're givin the opportunity to take the keyboard focus we must say we want it
				if( mData1 == TRUE )
					*(Bool *)mData2 = TRUE;

				return MSG_HANDLED;
			}
		case GLM_DOUBLE_CLICKED:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if( controlID == listboxGamesID )
				{
					int rowSelected = mData2;

					if (rowSelected >= 0)
					{
						LANGameInfo * theGame = TheLAN->LookupGameByListOffset(rowSelected);
						if (theGame)
						{
							TheLAN->RequestGameJoin(theGame);
						}
					}
				}
				break;
			}
		case GLM_SELECTED:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if( controlID == listboxGamesID )
				{
					int rowSelected = mData2;
					if( rowSelected < 0 )
					{
						HideGameInfoWindow(TRUE);
						break;
					}
					LANGameInfo * theGame = TheLAN->LookupGameByListOffset(rowSelected);
					if (theGame)
						RefreshGameInfoWindow(theGame, theGame->getName());
					else
						HideGameInfoWindow(TRUE);

				}
				break;
			}
		case GBM_SELECTED:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if ( controlID == buttonBackID )
				{
					// In the relay-only Discombobulator build the lobby's
					// "back" button is repurposed to Exit Game — there's
					// no useful shell to fall back into when the player
					// launched straight into multiplayer with -mpmenu.
					//
					// Order matters: TheShell->pop() runs LanLobbyMenu-
					// Shutdown synchronously, which dereferences TheLAN
					// (RequestLobbyLeave). So we pop *first*, THEN
					// delete TheLAN, THEN flip the engine quit flag —
					// matching the original pop + delete sequence so the
					// shutdown path is unchanged. The actual shell
					// pop-transition is queued for after this frame, but
					// the engine main loop sees m_quitting on its next
					// iteration and exits before the transition finishes.
					LANbuttonPushed = true;
					DEBUG_LOG(("Back was hit - quitting game from lobby"));
					TheShell->pop();
					delete TheLAN;
					TheLAN = nullptr;
					TheGameEngine->setQuitting(TRUE);
				}
				else if ( controlID == buttonHostID )
				{
					TheLAN->RequestGameCreate( L"", FALSE);

				}
				else if ( controlID == buttonClearID )
				{
					GadgetTextEntrySetText(textEntryPlayerName, UnicodeString::TheEmptyString);
					TheWindowManager->winSendSystemMsg( window,
																						GEM_UPDATE_TEXT,
																						(WindowMsgData)textEntryPlayerName,
																						0 );

				}
				else if ( controlID == buttonJoinID )
				{

					//TheShell->push( "Menus/LanGameOptionsMenu.wnd" );

					int rowSelected = -1;
					GadgetListBoxGetSelected( listboxGames, &rowSelected );

					if (rowSelected >= 0)
					{
						LANGameInfo * theGame = TheLAN->LookupGameByListOffset(rowSelected);
						if (theGame)
						{
							TheLAN->RequestGameJoin(theGame);
						}
					}
					else
					{
						GadgetListBoxAddEntryText(listboxChatWindow, TheGameText->fetch("LAN:ErrorNoGameSelected") , chatSystemColor, -1, 0);
					}

				}
				else if ( controlID == buttonEmoteID )
				{
					// read the user's input
					txtInput.set(GadgetTextEntryGetText( textEntryChat ));
					// Clear the text entry line
					GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
					// Clean up the text (remove leading/trailing chars, etc)
					txtInput.trim();
					// Echo the user's input to the chat window
					if (!txtInput.isEmpty()) {
//						TheLAN->RequestChat(txtInput, LANAPIInterface::LANCHAT_EMOTE);
						TheLAN->RequestChat(txtInput, LANAPIInterface::LANCHAT_NORMAL);
					}
				}
				else if (controlID == buttonDirectConnectID)
				{
					// Discombobulator: this is the relabeled "OPTIONS"
					// button — open the standard Options menu layout
					// over the lobby instead of pushing the (removed)
					// Direct Connect screen. Same pattern the main menu
					// uses for its Options button: get the cached
					// layout, init it, show it on top.
					WindowLayout *optLayout = TheShell->getOptionsLayout(TRUE);
					if (optLayout)
					{
						optLayout->runInit();
						optLayout->hide(FALSE);
						optLayout->bringForward();
					}
				}

				break;
			}

		case GEM_UPDATE_TEXT:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if ( controlID == textEntryPlayerNameID )
				{
					// grab the user's name
					txtInput.set(GadgetTextEntryGetText( textEntryPlayerName ));

					// Clean up the text (remove leading/trailing chars, etc)
					const WideChar *c = txtInput.str();
					while (c && (iswspace(*c)))
						c++;

					if (c)
						txtInput = UnicodeString(c);
					else
						txtInput = UnicodeString::TheEmptyString;

					txtInput.truncateTo(g_lanPlayerNameLength);

					if (!txtInput.isEmpty() && txtInput.getCharAt(txtInput.getLength()-1) == L',')
						txtInput.removeLastChar(); // we use , for strtok's so we can't allow them in names.  :(

					if (!txtInput.isEmpty() && txtInput.getCharAt(txtInput.getLength()-1) == L':')
						txtInput.removeLastChar(); // we use : for strtok's so we can't allow them in names.  :(

					if (!txtInput.isEmpty() && txtInput.getCharAt(txtInput.getLength()-1) == L';')
						txtInput.removeLastChar(); // we use ; for strtok's so we can't allow them in names.  :(

					// send it over the network
					if (!txtInput.isEmpty())
						TheLAN->RequestSetName(txtInput);
					else
						{
							TheLAN->RequestSetName(defaultName);
						}

					// Put the whitespace-free version in the box
					GadgetTextEntrySetText( textEntryPlayerName, txtInput );

				}
				break;
			}
		case GEM_EDIT_DONE:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				// Take the user's input and echo it into the chat window as well as
				// send it to the other clients on the lan
				if ( controlID == textEntryChatID )
				{

					// read the user's input
					txtInput.set(GadgetTextEntryGetText( textEntryChat ));
					// Clear the text entry line
					GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
					// Clean up the text (remove leading/trailing chars, etc)
					while (!txtInput.isEmpty() && iswspace(txtInput.getCharAt(0)))
						txtInput = UnicodeString(txtInput.str()+1);

					// Echo the user's input to the chat window
					if (!txtInput.isEmpty())
						TheLAN->RequestChat(txtInput, LANAPIInterface::LANCHAT_NORMAL);

				}
				/*
				else if ( controlID == textEntryPlayerNameID )
				{
					// grab the user's name
					txtInput.set(GadgetTextEntryGetText( textEntryPlayerName ));

					// Clean up the text (remove leading/trailing chars, etc)
					txtInput.trim();

					// send it over the network
					if (!txtInput.isEmpty())
						TheLAN->RequestSetName(txtInput);

					// Put the whitespace-free version in the box
					GadgetTextEntrySetText( textEntryPlayerName, txtInput );

				}
				*/
				break;
			}
		default:
			return MSG_IGNORED;

	}

	return MSG_HANDLED;
}
