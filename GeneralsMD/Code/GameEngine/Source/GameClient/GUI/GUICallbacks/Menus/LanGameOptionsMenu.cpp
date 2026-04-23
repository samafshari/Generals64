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
// FILE: LanGameOptionsMenu.cpp
// Author: Chris Huybregts, October 2001
// Description: Lan Game Options Menu
///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine


#include "Common/PlayerTemplate.h"
#include "Common/GameEngine.h"
#include "Common/UserPreferences.h"
#include "Common/QuotedPrintable.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"

#include "GameNetwork/FirewallHelper.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/IPEnumeration.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "Common/MultiplayerSettings.h"
#include "GameClient/GameText.h"
#include "GameNetwork/GUIUtil.h"


extern char *LANnextScreen;
extern Bool LANisShuttingDown;
extern Bool LANbuttonPushed;
extern void MapSelectorTooltip(GameWindow *window, WinInstanceData *instData,	UnsignedInt mouse);
extern void gameAcceptTooltip(GameWindow *window, WinInstanceData *instData, UnsignedInt mouse);
Color white = GameMakeColor( 255, 255, 255, 255 );
static bool s_isIniting = FALSE;
// window ids ------------------------------------------------------------------------------
static NameKeyType parentLanGameOptionsID = NAMEKEY_INVALID;

static NameKeyType comboBoxPlayerID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType buttonAcceptID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType comboBoxColorID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType comboBoxPlayerTemplateID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType comboBoxTeamID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

//static NameKeyType buttonStartPositionID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
//																										NAMEKEY_INVALID,NAMEKEY_INVALID,
//																										NAMEKEY_INVALID,NAMEKEY_INVALID,
//																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType buttonMapStartPositionID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType textEntryChatID = NAMEKEY_INVALID;
static NameKeyType textEntryMapDisplayID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType buttonStartID = NAMEKEY_INVALID;
static NameKeyType buttonEmoteID = NAMEKEY_INVALID;
static NameKeyType buttonSelectMapID = NAMEKEY_INVALID;
static NameKeyType comboBoxSuperweaponLimitID = NAMEKEY_INVALID;
static NameKeyType comboBoxStartingCashID = NAMEKEY_INVALID;
static NameKeyType comboBoxGameSpeedID = NAMEKEY_INVALID;
static NameKeyType checkBoxSharedMoneyID = NAMEKEY_INVALID;
static NameKeyType windowMapID = NAMEKEY_INVALID;
// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentLanGameOptions = nullptr;
static GameWindow *buttonBack = nullptr;
static GameWindow *buttonStart = nullptr;
static GameWindow *buttonSelectMap = nullptr;
static GameWindow *buttonEmote = nullptr;
static GameWindow *textEntryChat = nullptr;
static GameWindow *textEntryMapDisplay = nullptr;
static GameWindow *comboBoxSuperweaponLimit = nullptr;
static GameWindow *comboBoxStartingCash = nullptr;
static GameWindow *comboBoxGameSpeed = nullptr;
static GameWindow *windowMap = nullptr;

// "Shared Money" checkbox — declared in LanGameOptionsMenu.wnd
// alongside the other host-side options (starting cash, game speed,
// superweapon limit) and looked up here by NAMEKEY. Host-only;
// disabled for joiners. When toggled, flips GameInfo::m_sharedTeamMoney
// and re-broadcasts the game options string so every joiner picks up
// the new value via the STM= key in ParseAsciiStringToGameInfo.
static GameWindow *checkBoxSharedMoney = nullptr;

// Preset values for the host's superweapon-limit combo. Counts are interpreted
// PER TEAM, not per player — see Player::canBuildMoreOfType for the change.
// Value 0 means "superweapons disabled" (NOT unlimited); see
// Player::canBuildMoreOfType's DeterminedBySuperweaponRestriction branch.
static const Int s_superweaponLimitValues[] = { 0, 1, 2, 3, 4, 5, 10, 50 };
static const Int s_superweaponLimitCount = sizeof(s_superweaponLimitValues) / sizeof(s_superweaponLimitValues[0]);

static void PopulateSuperweaponLimitComboBox(GameWindow *comboBox, GameInfo *myGame)
{
	GadgetComboBoxReset(comboBox);
	if (!comboBox || !myGame)
		return;

	const UnsignedShort currentLimit = myGame->getSuperweaponRestriction();
	Int currentSelectionIndex = -1;

	for (Int i = 0; i < s_superweaponLimitCount; ++i)
	{
		const Int value = s_superweaponLimitValues[i];
		UnicodeString label;
		if (value == 0)
			label = L"Off";
		else
			label.format(L"%d", value);
		Int newIndex = GadgetComboBoxAddEntry(comboBox, label,
			comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor());
		GadgetComboBoxSetItemData(comboBox, newIndex, reinterpret_cast<void *>(static_cast<uintptr_t>(value)));

		if (value == (Int)currentLimit)
			currentSelectionIndex = newIndex;
	}

	if (currentSelectionIndex == -1)
	{
		// The current value isn't one of the presets — add it on the fly so
		// the dropdown reflects the actual GameInfo state.
		UnicodeString label;
		label.format(L"%d", (Int)currentLimit);
		currentSelectionIndex = GadgetComboBoxAddEntry(comboBox, label,
			comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor());
		GadgetComboBoxSetItemData(comboBox, currentSelectionIndex, reinterpret_cast<void *>(static_cast<uintptr_t>(currentLimit)));
	}

	GadgetComboBoxSetSelectedPos(comboBox, currentSelectionIndex);
}

// Preset values for the host's game-speed combo. The first entry (30) matches
// LOGICFRAMES_PER_SECOND so the host can opt back into the campaign / shell
// baseline. Anything above that is "smoother + faster" — see the network
// run-ahead path and GameLogic::startNewGame for how this is consumed.
static const Int s_gameSpeedFpsValues[] = { 30, 50, 60, 70, 90, 120, 150 };
static const Int s_gameSpeedFpsCount = sizeof(s_gameSpeedFpsValues) / sizeof(s_gameSpeedFpsValues[0]);

static void PopulateGameSpeedComboBox(GameWindow *comboBox, GameInfo *myGame)
{
	GadgetComboBoxReset(comboBox);
	if (!comboBox || !myGame)
		return;

	const Int currentFps = myGame->getGameFps();
	Int currentSelectionIndex = -1;

	for (Int i = 0; i < s_gameSpeedFpsCount; ++i)
	{
		const Int fps = s_gameSpeedFpsValues[i];
		UnicodeString label;
		// Format as "70 hz" — short and unambiguous. No CSF lookup so we
		// don't need to ship a new localization key for every preset.
		label.format(L"%d hz", fps);
		Int newIndex = GadgetComboBoxAddEntry(comboBox, label,
			comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor());
		GadgetComboBoxSetItemData(comboBox, newIndex, reinterpret_cast<void *>(static_cast<uintptr_t>(fps)));

		if (fps == currentFps)
			currentSelectionIndex = newIndex;
	}

	if (currentSelectionIndex == -1)
	{
		// The current value isn't one of the presets — add it on the fly so
		// the dropdown reflects the actual GameInfo state instead of silently
		// snapping to a different value.
		UnicodeString label;
		label.format(L"%d hz", currentFps);
		currentSelectionIndex = GadgetComboBoxAddEntry(comboBox, label,
			comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor());
		GadgetComboBoxSetItemData(comboBox, currentSelectionIndex, reinterpret_cast<void *>(static_cast<uintptr_t>(currentFps)));
	}

	GadgetComboBoxSetSelectedPos(comboBox, currentSelectionIndex);
}

static GameWindow *comboBoxPlayer[MAX_SLOTS] = {0};
static GameWindow *buttonAccept[MAX_SLOTS] = {0};

static GameWindow *comboBoxColor[MAX_SLOTS] = {0};

// Per-slot color swatch — a small solid-colored square that
// REPLACES the color combo box for human players in network games.
// Humans can't change their color from inside the game (the launcher
// is the canonical source via -color), so we hide the dropdown and
// show a static colored square instead. AI slots and skirmish-mode
// slots keep the combo box. Created programmatically in
// LanGameOptionsMenuInit; lanUpdateSlotList toggles visibility +
// recolors each tick.
static GameWindow *colorSwatch[MAX_SLOTS] = {0};

static GameWindow *comboBoxPlayerTemplate[MAX_SLOTS] = {0};

static GameWindow *comboBoxTeam[MAX_SLOTS] = {0};

//static GameWindow *buttonStartPosition[MAX_SLOTS] = {0};
//
static GameWindow *buttonMapStartPosition[MAX_SLOTS] = {0};

//external declarations of the Gadgets the callbacks can use
GameWindow *listboxChatWindowLanGame = nullptr;
NameKeyType listboxChatWindowLanGameID = NAMEKEY_INVALID;
WindowLayout *mapSelectLayout = nullptr;

static Int getNextSelectablePlayer(Int start)
{
	LANGameInfo *game = TheLAN->GetMyGame();
	if (!game->amIHost())
		return -1;
	for (Int j=start; j<MAX_SLOTS; ++j)
	{
		LANGameSlot *slot = game->getLANSlot(j);
		if (slot && slot->getStartPos() == -1 &&
			( (j==game->getLocalSlotNum() && game->getConstSlot(j)->getPlayerTemplate()!=PLAYERTEMPLATE_OBSERVER)
			|| slot->isAI()))
		{
			return j;
		}
	}
	return -1;
}

static Int getFirstSelectablePlayer(const GameInfo *game)
{
	const GameSlot *slot = game->getConstSlot(game->getLocalSlotNum());
	if (!game->amIHost() || (slot && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER))
		return game->getLocalSlotNum();

	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		slot = game->getConstSlot(i);
		if (slot && slot->isAI())
			return i;
	}

	return game->getLocalSlotNum();
}

void updateMapStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], Bool onLoadScreen = FALSE );
void positionStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);
void LanPositionStartSpots()
{

	positionStartSpots( TheLAN->GetMyGame(), buttonMapStartPosition, windowMap);
}
static void playerTooltip(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	Int idx = -1;
	Int i=0;
	for (; i<MAX_SLOTS; ++i)
	{
		if (window && window == GadgetComboBoxGetEditBox(comboBoxPlayer[i]))
		{
			idx = i;
			break;
		}
	}
	if (idx == -1)
		return;

	LANGameSlot *slot = TheLAN->GetMyGame()->getLANSlot(i);
	if (!slot)
		return;

	LANPlayer *player = slot->getUser();
	if (!player)
	{
		DEBUG_ASSERTCRASH(TheLAN->GetMyGame()->getIP(i) == 0, ("No player info in listbox!"));
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString );
		return;
	}

	setLANPlayerTooltip(player);
}

void StartPressed()
{
	LANGameInfo *myGame = TheLAN->GetMyGame();

	Bool isReady = true;
	Bool allHaveMap = true;
	Int playerCount = 0;
	if (!myGame)
	{
		return;
	}
	myGame->getLANSlot(0)->setAccept(); // cause we are, of course!

	int i;

	int numUsers = 0;
	int numHumans = 0;
	for (i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = myGame->getSlot(i);
		if (slot && slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
		{
			if (slot && slot->isHuman())
				numHumans++;
			numUsers++;
		}
	}

	// Check for too many players
	const MapMetaData *md = TheMapCache->findMap( myGame->getMap() );
	if (!md || md->m_numPlayers < numUsers)
	{
		if (TheLAN->AmIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:TooManyPlayers"), (md)?md->m_numPlayers:0);
			TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
		}
		return;
	}

	// Check for observer + AI players
	if (TheGlobalData->m_netMinPlayers && !numHumans)
	{
		if (TheLAN->AmIHost())
		{
			UnicodeString text = TheGameText->fetch("GUI:NeedHumanPlayers");
			TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
		}
		return;
	}

	// Check for too few players
	if (numUsers < TheGlobalData->m_netMinPlayers)
	{
		if (TheLAN->AmIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:NeedMorePlayers"),numUsers);
			TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
		}
		return;
	}

	// Check for too few teams
	int numRandom = 0;
	std::set<Int> teams;
	for (i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = myGame->getSlot(i);
		if (slot && slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
		{
			if (slot->getTeamNumber() >= 0)
			{
				teams.insert(slot->getTeamNumber());
			}
			else
			{
				++numRandom;
			}
		}
	}
	if (numRandom + teams.size() < TheGlobalData->m_netMinPlayers)
	{
		if (TheLAN->AmIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:NeedMoreTeams"));
			TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
		}
		return;
	}

	if (numRandom + teams.size() < 2)
	{
		UnicodeString text;
		text.format(TheGameText->fetch("GUI:SandboxMode"));
			TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
	}

	// see if everyone's accepted and count the number of players in the game
	UnicodeString mapDisplayName;
	const MapMetaData *mapData = TheMapCache->findMap( myGame->getMap() );
	Bool willTransfer = TRUE;
	if (mapData)
	{
		mapDisplayName.format(L"%ls", mapData->m_displayName.str());
		willTransfer = !mapData->m_isOfficial;
	}
	else
	{
		mapDisplayName.format(L"%hs", myGame->getMap().str());
		willTransfer = WouldMapTransfer(myGame->getMap());
	}
	for( i = 0; i < MAX_SLOTS; i++ )
	{
		LANGameSlot *slot = myGame->getLANSlot(i);
		if( slot->isHuman() && !slot->isAccepted())
		{
			isReady = false;
			if (!willTransfer)
			{
				if (!slot->hasMap())
				{
					UnicodeString msg;
					msg.format(TheGameText->fetch("GUI:PlayerNoMap"), slot->getName().str(), mapDisplayName.str());
					GadgetListBoxAddEntryText(listboxChatWindowLanGame, msg , chatSystemColor, -1, 0);
					allHaveMap = false;
				}
			}
		}
		if( slot->isHuman() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER )
			playerCount++;
	}

	if(isReady)
	{
		for( i = 0; i < MAX_SLOTS; i++ )
		{
			LANGameSlot *slot = myGame->getLANSlot(i);
			if (slot && slot->isOpen())
			{
				slot->setState( SLOT_CLOSED );
				GadgetComboBoxSetSelectedPos(comboBoxPlayer[i], SLOT_CLOSED);
			}
		}
		Int seconds = TheMultiplayerSettings->getStartCountdownTimerSeconds();
		if (seconds)
			TheLAN->RequestGameStartTimer(seconds);
		else
			TheLAN->RequestGameStart();
		LANEnableStartButton(false);
	}
	else
	{
		// Does everyone have the map?
		if (allHaveMap)
		{
			GadgetListBoxAddEntryText(listboxChatWindowLanGame, TheGameText->fetch("GUI:NotifiedStartIntent") , chatSystemColor, -1, 0);
			TheLAN->RequestAccept();
		}
	}

}

void LANEnableStartButton(Bool enabled)
{
	buttonStart->winEnable(enabled);
	buttonSelectMap->winEnable(enabled);
}

static void handleColorSelection(int index)
{
	GameWindow *combo = comboBoxColor[index];
	Int color, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	color = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(combo, selIndex)));

	LANGameInfo *myGame = TheLAN->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(index);
		if (color == slot->getColor())
			return;

		// `color` is the combo box item data, which is now a raw
		// 0x00RRGGBB int (or -1 = random sentinel) — the old
		// `< getNumColors()` bounds check was a palette-index check
		// that filters out every real RGB value, so dropdown picks
		// were silently ignored. Just guard against the random
		// sentinel and run the duplicate-color check across all
		// slots — the comparison still works because both sides are
		// raw RGB ints.
		if (color != -1)
		{
			Bool colorAvailable = TRUE;
			for(Int i=0; i <MAX_SLOTS; i++)
			{
				LANGameSlot *checkSlot = myGame->getLANSlot(i);
				if(color == checkSlot->getColor() && slot != checkSlot)
				{
					colorAvailable = FALSE;
					break;
				}
			}
			if(!colorAvailable)
				return;
		}

		slot->setColor(color);

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the color from the host
			if (!slot->isLocalPlayer() || !AreSlotListUpdatesEnabled())
				return;

			AsciiString options;
			options.format("Color=%d", color);
			TheLAN->RequestGameOptions(options, true);
		}
	}
}

static void handlePlayerTemplateSelection(int index)
{
	GameWindow *combo = comboBoxPlayerTemplate[index];
	Int playerTemplate, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	playerTemplate = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(combo, selIndex)));
	LANGameInfo *myGame = TheLAN->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(index);
		if (playerTemplate == slot->getPlayerTemplate())
			return;

		Int oldTemplate = slot->getPlayerTemplate();
		slot->setPlayerTemplate(playerTemplate);

		if (oldTemplate == PLAYERTEMPLATE_OBSERVER)
		{
			// was observer, so populate color & team with all, and enable
			GadgetComboBoxSetSelectedPos(comboBoxColor[index], 0);
			GadgetComboBoxSetSelectedPos(comboBoxTeam[index], 0);
			slot->setStartPos(-1);
		}
		else if (playerTemplate == PLAYERTEMPLATE_OBSERVER)
		{
			// is becoming observer, so populate color & team with random only, and disable
			GadgetComboBoxSetSelectedPos(comboBoxColor[index], 0);
			GadgetComboBoxSetSelectedPos(comboBoxTeam[index], 0);
			slot->setStartPos(-1);
		}

		myGame->resetAccepted();

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the playerTemplate from the host
			if (AreSlotListUpdatesEnabled())
			{
				AsciiString options;
				options.format("PlayerTemplate=%d", playerTemplate);
				TheLAN->RequestGameOptions(options, true);
			}
		}
	}
}

static void handleStartPositionSelection(Int player, int startPos)
{
	LANGameInfo *myGame = TheLAN->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(player);
		if (startPos == slot->getStartPos())
			return;
		Bool skip = FALSE;
		if (startPos < 0)
		{
			skip = TRUE;
		}

		if(!skip)
		{
			Bool isAvailable = TRUE;
			for(Int i = 0; i < MAX_SLOTS; ++i)
			{
				if(i != player && myGame->getSlot(i)->getStartPos() == startPos)
				{
					isAvailable = FALSE;
					break;
				}
			}
			if( !isAvailable )
				return;
		}
		slot->setStartPos(startPos);

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				myGame->resetAccepted();
				TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the color from the host
			if (AreSlotListUpdatesEnabled())
			{
				AsciiString options;
				options.format("StartPos=%d", slot->getStartPos());
				TheLAN->RequestGameOptions(options, true);
			}
		}
	}
}



static void handleTeamSelection(int index)
{
	GameWindow *combo = comboBoxTeam[index];
	Int team, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	team = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(combo, selIndex)));
	LANGameInfo *myGame = TheLAN->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(index);
		if (team == slot->getTeamNumber())
			return;

		slot->setTeamNumber(team);
		myGame->resetAccepted();

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				myGame->resetAccepted();
				TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the team from the host
			if (AreSlotListUpdatesEnabled())
			{
				AsciiString options;
				options.format("Team=%d", team);
				TheLAN->RequestGameOptions(options, true);
			}
		}
	}
}

static void handleStartingCashSelection()
{
  LANGameInfo *myGame = TheLAN->GetMyGame();

  if (myGame)
  {
    Int selIndex;
    GadgetComboBoxGetSelectedPos(comboBoxStartingCash, &selIndex);

    Money startingCash;
    startingCash.deposit( static_cast<UnsignedInt>(reinterpret_cast<uintptr_t>(GadgetComboBoxGetItemData( comboBoxStartingCash, selIndex ))), FALSE, FALSE );
    myGame->setStartingCash( startingCash );
    myGame->resetAccepted();

    if (myGame->amIHost())
    {
      if (!s_isIniting)
      {
        // send around the new data
        TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
        lanUpdateSlotList(); // Update the accepted button UI
      }
    }
  }
}

static void handleGameSpeedSelection()
{
	LANGameInfo *myGame = TheLAN->GetMyGame();
	if (!myGame || !comboBoxGameSpeed)
		return;

	// Only the host may change game speed; for non-hosts the combo is also
	// disabled at init time, but a defensive check here keeps the GameInfo
	// untouched if anything ever calls this on a client.
	if (!myGame->amIHost())
		return;

	Int selIndex = -1;
	GadgetComboBoxGetSelectedPos(comboBoxGameSpeed, &selIndex);
	if (selIndex < 0)
		return;

	const Int newFps = static_cast<Int>(reinterpret_cast<uintptr_t>(
		GadgetComboBoxGetItemData(comboBoxGameSpeed, selIndex)));
	if (newFps <= 0)
		return;

	myGame->setGameFps(newFps);
	myGame->resetAccepted();

	if (!s_isIniting)
	{
		// Re-broadcast options so all joiners pick up the new GFPS=N value via
		// ParseAsciiStringToGameInfo on their side.
		TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
		lanUpdateSlotList();
	}
}

static void handleSharedMoneyToggle()
{
	LANGameInfo *myGame = TheLAN->GetMyGame();
	if (!myGame || !checkBoxSharedMoney)
		return;

	// Joiners see this checkbox disabled, but if something ever calls
	// into here off-host we still want to leave the authoritative
	// GameInfo untouched.
	if (!myGame->amIHost())
		return;

	const Bool newVal = GadgetCheckBoxIsChecked(checkBoxSharedMoney);
	if (newVal == myGame->isSharedTeamMoney())
		return;

	myGame->setSharedTeamMoney(newVal);
	myGame->resetAccepted();

	if (!s_isIniting)
	{
		// Rebroadcast so every peer picks up STM=N via ParseAsciiStringToGameInfo.
		TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
		lanUpdateSlotList();
	}
}

static void handleSuperweaponLimitSelection()
{
	LANGameInfo *myGame = TheLAN->GetMyGame();
	if (!myGame || !comboBoxSuperweaponLimit)
		return;

	if (!myGame->amIHost())
		return;

	Int selIndex = -1;
	GadgetComboBoxGetSelectedPos(comboBoxSuperweaponLimit, &selIndex);
	if (selIndex < 0)
		return;

	const Int newLimit = static_cast<Int>(reinterpret_cast<uintptr_t>(
		GadgetComboBoxGetItemData(comboBoxSuperweaponLimit, selIndex)));
	// 0 is a legal choice: it means "superweapons disabled". Negative values
	// can only happen from a malformed item-data payload, so still reject them.
	if (newLimit < 0)
		return;

	myGame->setSuperweaponRestriction(static_cast<UnsignedShort>(newLimit));
	myGame->resetAccepted();

	if (!s_isIniting)
	{
		TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
		lanUpdateSlotList();
	}
}

void lanUpdateSlotList()
{
	if(!AreSlotListUpdatesEnabled() || s_isIniting)
		return;
	UpdateSlotList( TheLAN->GetMyGame(), comboBoxPlayer, comboBoxColor,
		comboBoxPlayerTemplate, comboBoxTeam, buttonAccept, buttonStart, buttonMapStartPosition);

	updateMapStartSpots(TheLAN->GetMyGame(), buttonMapStartPosition);

	// Swatch / combo visibility toggle. For HUMAN slots in this
	// (network) game we hide the color combo and show a static
	// colored swatch instead — humans set their color in the
	// launcher via -color, the in-game dropdown is read-only for
	// them. AI / open / closed slots keep the combo so the host
	// can pick bot colors normally. Skirmish has its own update
	// path and doesn't go through here, so the combo stays
	// editable for skirmish humans (which is what we want — no
	// launcher in skirmish).
	{
		LANGameInfo *game = TheLAN->GetMyGame();
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			if (!comboBoxColor[i] || !colorSwatch[i] || !game)
				continue;
			GameSlot *slot = game->getSlot(i);
			Bool isHumanSlot = slot && slot->isHuman();
			if (isHumanSlot)
			{
				comboBoxColor[i]->winHide(TRUE);
				colorSwatch[i]->winHide(FALSE);
				// Recolor the swatch from the slot's current
				// color. resolveSlotColor pads the alpha so the
				// gadget renderer gets a fully-opaque ARGB.
				// Random sentinel (-1) shows as a white square
				// until the host actually starts the game and
				// populateRandomSideAndColor picks a real RGB.
				Color swatchColor = (slot->getColor() >= 0)
					? MultiplayerSettings::resolveSlotColor(slot->getColor())
					: (Color)0xFFFFFFFFu;
				GadgetButtonSetEnabledColor(colorSwatch[i], swatchColor);
				GadgetButtonSetEnabledSelectedColor(colorSwatch[i], swatchColor);
			}
			else
			{
				colorSwatch[i]->winHide(TRUE);
				comboBoxColor[i]->winHide(FALSE);
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Gadgets Options Menu */
//-------------------------------------------------------------------------------------------------
void InitLanGameGadgets()
{
	//Initialize the gadget IDs
	parentLanGameOptionsID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:LanGameOptionsMenuParent" );
	buttonBackID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ButtonBack" );
	buttonStartID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ButtonStart" );
	textEntryChatID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:TextEntryChat" );
	textEntryMapDisplayID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:TextEntryMapDisplay" );
	listboxChatWindowLanGameID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ListboxChatWindowLanGame" );
	buttonEmoteID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ButtonEmote" );
	buttonSelectMapID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ButtonSelectMap" );
  comboBoxSuperweaponLimitID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ComboBoxSuperweaponLimit" );
  comboBoxStartingCashID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ComboBoxStartingCash" );
  comboBoxGameSpeedID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ComboBoxGameSpeed" );
  checkBoxSharedMoneyID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:CheckBoxSharedMoney" );
	windowMapID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:MapWindow" );

	// Initialize the pointers to our gadgets
	parentLanGameOptions = TheWindowManager->winGetWindowFromId( nullptr, parentLanGameOptionsID );
	DEBUG_ASSERTCRASH(parentLanGameOptions, ("Could not find the parentLanGameOptions"));
	buttonEmote = TheWindowManager->winGetWindowFromId( parentLanGameOptions,buttonEmoteID  );
	DEBUG_ASSERTCRASH(buttonEmote, ("Could not find the buttonEmote"));
	buttonSelectMap = TheWindowManager->winGetWindowFromId( parentLanGameOptions,buttonSelectMapID  );
	DEBUG_ASSERTCRASH(buttonSelectMap, ("Could not find the buttonSelectMap"));
	buttonStart = TheWindowManager->winGetWindowFromId( parentLanGameOptions,buttonStartID  );
	DEBUG_ASSERTCRASH(buttonStart, ("Could not find the buttonStart"));
	buttonBack = TheWindowManager->winGetWindowFromId( parentLanGameOptions,  buttonBackID);
	DEBUG_ASSERTCRASH(buttonBack, ("Could not find the buttonBack"));
	listboxChatWindowLanGame = TheWindowManager->winGetWindowFromId( parentLanGameOptions, listboxChatWindowLanGameID );
	DEBUG_ASSERTCRASH(listboxChatWindowLanGame, ("Could not find the listboxChatWindowLanGame"));
	textEntryChat = TheWindowManager->winGetWindowFromId( parentLanGameOptions, textEntryChatID );
	DEBUG_ASSERTCRASH(textEntryChat, ("Could not find the textEntryChat"));
	textEntryMapDisplay = TheWindowManager->winGetWindowFromId( parentLanGameOptions, textEntryMapDisplayID );
	DEBUG_ASSERTCRASH(textEntryMapDisplay, ("Could not find the textEntryMapDisplay"));
  comboBoxSuperweaponLimit = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxSuperweaponLimitID );
  DEBUG_ASSERTCRASH(comboBoxSuperweaponLimit, ("Could not find the comboBoxSuperweaponLimit"));
  PopulateSuperweaponLimitComboBox(comboBoxSuperweaponLimit, TheLAN->GetMyGame());
  comboBoxStartingCash = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxStartingCashID );
  DEBUG_ASSERTCRASH(comboBoxStartingCash, ("Could not find the comboBoxStartingCash"));
	PopulateStartingCashComboBox(comboBoxStartingCash, TheLAN->GetMyGame());
  comboBoxGameSpeed = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxGameSpeedID );
  DEBUG_ASSERTCRASH(comboBoxGameSpeed, ("Could not find the comboBoxGameSpeed"));
	PopulateGameSpeedComboBox(comboBoxGameSpeed, TheLAN->GetMyGame());

  checkBoxSharedMoney = TheWindowManager->winGetWindowFromId( parentLanGameOptions, checkBoxSharedMoneyID );
  DEBUG_ASSERTCRASH(checkBoxSharedMoney, ("Could not find the checkBoxSharedMoney"));
	if (checkBoxSharedMoney)
	{
		// Override the .wnd's CSF key with a literal so we don't ship
		// a new "GUI:SharedMoney" string key right now. If/when the
		// CSF gets that entry, the .wnd's TEXT = "GUI:SharedMoney"
		// will automatically take over and this override becomes a
		// no-op for localized builds.
		GadgetCheckBoxSetText(checkBoxSharedMoney, UnicodeString(L"Shared Money"));
		GadgetCheckBoxSetChecked(checkBoxSharedMoney,
			TheLAN->GetMyGame() ? TheLAN->GetMyGame()->isSharedTeamMoney() : FALSE);
	}

	windowMap = TheWindowManager->winGetWindowFromId( parentLanGameOptions,windowMapID  );
	DEBUG_ASSERTCRASH(windowMap, ("Could not find the LanGameOptionsMenu.wnd:MapWindow" ));

	Int localSlotNum = TheLAN->GetMyGame()->getLocalSlotNum();
	DEBUG_ASSERTCRASH(localSlotNum >= 0, ("Bad slot number!"));

	//Tooltip function is being set for techBuildings, and supplyDocks
	windowMap->winSetTooltipFunc(MapSelectorTooltip);

	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		AsciiString tmpString;
		tmpString.format("LanGameOptionsMenu.wnd:ComboBoxPlayer%d", i);
		comboBoxPlayerID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxPlayer[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxPlayerID[i] );
		GadgetComboBoxReset(comboBoxPlayer[i]);
		GadgetComboBoxGetEditBox(comboBoxPlayer[i])->winSetTooltipFunc(playerTooltip);

		if(localSlotNum != i)
		{
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:Open"),white);
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:Closed"),white);
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:EasyAI"),white);
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:MediumAI"),white);
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:HardAI"),white);
			GadgetComboBoxSetSelectedPos(comboBoxPlayer[i],0);
		}
		/*
		if(i != 0)
		{
			TheLAN->GetMyGame()->getLANSlot(i)->setState(SLOT_OPEN);
		}
		*/

		tmpString.format("LanGameOptionsMenu.wnd:ComboBoxColor%d", i);
		comboBoxColorID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxColor[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxColorID[i] );
		DEBUG_ASSERTCRASH(comboBoxColor[i], ("Could not find the comboBoxColor[%d]",i ));
		PopulateColorComboBox(i, comboBoxColor, TheLAN->GetMyGame());
		GadgetComboBoxSetSelectedPos(comboBoxColor[i], 0);

		// Programmatically create a per-slot color SWATCH that
		// REPLACES the dropdown for human players in network
		// games. The swatch is a solid-colored RECTANGLE the
		// exact size of the combo box it covers, with a thin
		// game-blue border so it visually matches the rest of
		// the UI (text entries, panel frames, etc.). It's hidden
		// at init; lanUpdateSlotList toggles its visibility +
		// recolors it from the slot's color each tick.
		if (comboBoxColor[i])
		{
			// Use the combo's PARENT as the swatch's parent, and
			// the combo's RELATIVE position (winGetPosition is
			// relative to the parent) so the two widgets share
			// the same coordinate origin and the swatch lands
			// directly over the combo regardless of how the
			// .wnd nests the chat panel inside the layout root.
			GameWindow *cbParent = comboBoxColor[i]->winGetParent();
			Int cbX = 0, cbY = 0, cbW = 0, cbH = 0;
			comboBoxColor[i]->winGetPosition(&cbX, &cbY);
			comboBoxColor[i]->winGetSize(&cbW, &cbH);

			WinInstanceData swInst;
			swInst.init();
			BitSet(swInst.m_style, GWS_PUSH_BUTTON);
			// Status flags: ENABLED so the gadget uses the enabled
			// draw data (where our color setter writes); NO_INPUT
			// so it can't be clicked / hovered (purely a display).
			// We deliberately do NOT set WIN_STATUS_IMAGE — that
			// flag tells gogoGadgetPushButton to install the
			// image-based draw func, which renders nothing when no
			// image is assigned. The plain push-button draw func
			// honors winSetEnabledColor and gives us a solid fill.
			colorSwatch[i] = TheWindowManager->gogoGadgetPushButton(
				cbParent ? cbParent : parentLanGameOptions,
				WIN_STATUS_ENABLED | WIN_STATUS_NO_INPUT,
				cbX, cbY,
				cbW, cbH,
				&swInst, nullptr, TRUE);

			if (colorSwatch[i])
			{
				// Default white fill until lanUpdateSlotList runs.
				// Set both Enabled[0] (background) and Enabled[1]
				// (selected/highlight overlay) so the swatch reads
				// solid regardless of which sub-state the gadget
				// renderer happens to use this frame.
				//
				// The border color is the engine's UI accent blue
				// (matches the chat panel frame, the LAN-game card
				// hairlines, etc.) so the swatch reads as part of
				// the same visual system as the other slot row
				// widgets — a colored rectangle with a thin blue
				// frame, exactly like the in-game text entries.
				const Color SwatchBorder = GameMakeColor(60, 100, 180, 255);
				GadgetButtonSetEnabledColor(colorSwatch[i], 0xFFFFFFFFu);
				GadgetButtonSetEnabledSelectedColor(colorSwatch[i], 0xFFFFFFFFu);
				GadgetButtonSetEnabledBorderColor(colorSwatch[i], SwatchBorder);
				GadgetButtonSetEnabledSelectedBorderColor(colorSwatch[i], SwatchBorder);
				colorSwatch[i]->winHide(TRUE);
			}
		}

		tmpString.format("LanGameOptionsMenu.wnd:ComboBoxPlayerTemplate%d", i);
		comboBoxPlayerTemplateID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxPlayerTemplate[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxPlayerTemplateID[i] );
		DEBUG_ASSERTCRASH(comboBoxPlayerTemplate[i], ("Could not find the comboBoxPlayerTemplate[%d]",i ));
		PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, TheLAN->GetMyGame(), TRUE);

		// add tooltips to the player template combobox and listbox
		comboBoxPlayerTemplate[i]->winSetTooltipFunc(playerTemplateComboBoxTooltip);
		GadgetComboBoxGetListBox(comboBoxPlayerTemplate[i])->winSetTooltipFunc(playerTemplateListBoxTooltip);

		tmpString.format("LanGameOptionsMenu.wnd:ComboBoxTeam%d", i);
		comboBoxTeamID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxTeam[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxTeamID[i] );
		DEBUG_ASSERTCRASH(comboBoxTeam[i], ("Could not find the comboBoxTeam[%d]",i ));
		PopulateTeamComboBox(i, comboBoxTeam, TheLAN->GetMyGame());

		tmpString.clear();
		tmpString.format("LanGameOptionsMenu.wnd:ButtonAccept%d", i);
		buttonAcceptID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonAccept[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonAcceptID[i] );
		DEBUG_ASSERTCRASH(buttonAccept[i], ("Could not find the buttonAccept[%d]",i ));
		buttonAccept[i]->winSetTooltipFunc(gameAcceptTooltip);
//
//		tmpString.format("LanGameOptionsMenu.wnd:ButtonStartPosition%d", i);
//		buttonStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
//		buttonStartPosition[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonStartPositionID[i] );
//		DEBUG_ASSERTCRASH(buttonStartPosition[i], ("Could not find the ButtonStartPosition[%d]",i ));

		tmpString.format("LanGameOptionsMenu.wnd:ButtonMapStartPosition%d", i);
		buttonMapStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonMapStartPositionID[i] );
		DEBUG_ASSERTCRASH(buttonMapStartPosition[i], ("Could not find the ButtonMapStartPosition[%d]",i ));

		if(i !=0 && buttonAccept[i])
			buttonAccept[i]->winHide(TRUE);
	}
	if( buttonAccept[0] )
		GadgetButtonSetEnabledColor(buttonAccept[0], acceptTrueColor );

}

void DeinitLanGameGadgets()
{
	parentLanGameOptions = nullptr;
	buttonEmote = nullptr;
	buttonSelectMap = nullptr;
	buttonStart = nullptr;
	buttonBack = nullptr;
	listboxChatWindowLanGame = nullptr;
	textEntryChat = nullptr;
	textEntryMapDisplay = nullptr;
  comboBoxSuperweaponLimit = nullptr;
  comboBoxStartingCash = nullptr;
  comboBoxGameSpeed = nullptr;
  // Shared-money checkbox: we created it via gogoGadgetCheckbox, so the
  // window manager tear-down will destroy the widget itself when the
  // parent menu window goes away. We only need to null our cached
  // pointer so the next init starts clean.
  checkBoxSharedMoney = nullptr;
	if (windowMap)
	{
		windowMap->winSetUserData(nullptr);
		windowMap = nullptr;
	}
	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		comboBoxPlayer[i] = nullptr;
		comboBoxColor[i] = nullptr;
		colorSwatch[i] = nullptr;
		comboBoxPlayerTemplate[i] = nullptr;
		comboBoxTeam[i] = nullptr;
		buttonAccept[i] = nullptr;
//		buttonStartPosition[i] = nullptr;
		buttonMapStartPosition[i] = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Lan Game Options Menu */
//-------------------------------------------------------------------------------------------------
void LanGameOptionsMenuInit( WindowLayout *layout, void *userData )
{
	if (TheLAN->GetMyGame() && TheLAN->GetMyGame()->isGameInProgress())
	{
		// If we init while the game is in progress, we are really returning to the menu
		// after the game.  So, we pop the menu and go back to the lobby.  Whee!
		DEBUG_LOG(("Popping to lobby after a game!"));
		TheShell->popImmediate();
		return;
	}
	s_isIniting = TRUE;

	LANbuttonPushed = false;
	LANisShuttingDown = false;

	//initialize the gadgets
	EnableSlotListUpdates(FALSE);
	InitLanGameGadgets();
	EnableSlotListUpdates(TRUE);
	Int start = 0;

	// Make sure the text fields are clear
	GadgetListBoxReset( listboxChatWindowLanGame );
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);

	//The dialog needs to react differently depending on whether it's the host or not.
	TheMapCache->updateCache();
	if (TheLAN->AmIHost())
	{
		// read in some prefs
		LANGameInfo *game = TheLAN->GetMyGame();
		LANGameSlot *slot = game->getLANSlot(0);
		LANPreferences pref;

		// Discombobulator: launcher-supplied house color takes
		// precedence over the LANPreferences saved value. Slot color
		// is now a raw 24-bit RGB int (-1 = random); the value flows
		// through the standard slot wire encoding to remote peers
		// unchanged. Same path for the cosmetic shader id.
		extern Bool g_launcherPlayerColorSet;
		extern UnsignedInt g_launcherPlayerColor;
		extern Int g_launcherPlayerShaderId;
		if (g_launcherPlayerColorSet)
			slot->setColor( MultiplayerSettings::packSlotColor(g_launcherPlayerColor) );
		else
			slot->setColor( pref.getPreferredColor() );

		slot->setShaderId( g_launcherPlayerShaderId );

		slot->setPlayerTemplate( pref.getPreferredFaction() );
		slot->setNATBehavior(FirewallHelperClass::FIREWALL_TYPE_SIMPLE);
		game->setMap( pref.getPreferredMap() );
    game->setStartingCash( pref.getStartingCash() );
    // The old pref was a Bool ("limit / don't limit"). With the new
    // per-team combo model the value is a count, so default to 3
    // (the GameInfo::reset baseline) and let the host change it via the
    // ComboBoxSuperweaponLimit dropdown.
    game->setSuperweaponRestriction( 3 );
		AsciiString lowerMap = pref.getPreferredMap();
		lowerMap.toLower();
		std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
		if (it != TheMapCache->end())
		{
			TheLAN->GetMyGame()->getSlot(0)->setMapAvailability(true);
			TheLAN->GetMyGame()->setMapCRC( it->second.m_CRC );
			TheLAN->GetMyGame()->setMapSize( it->second.m_filesize );

			TheLAN->GetMyGame()->adjustSlotsForMap(); // BGC- adjust the slots for the selected map.
		}

		//GadgetTextEntrySetText(comboBoxPlayer[0], TheLAN->GetMyName());
		lanUpdateSlotList();
		updateGameOptions();
		start = 1; // leave my combo boxes usable

		comboBoxPlayer[0]->winEnable(FALSE);
	}
	else
	{

		//DEBUG_LOG(("LanGameOptionsMenuInit(): map is %s", TheLAN->GetMyGame()->getMap().str()));
		buttonStart->winSetText(TheGameText->fetch("GUI:Accept"));
		buttonSelectMap->winEnable( FALSE );
    comboBoxSuperweaponLimit->winEnable( FALSE ); // Can look but only host can touch
    comboBoxStartingCash->winEnable( FALSE );     // Ditto
    comboBoxGameSpeed->winEnable( FALSE );        // Host-only, see handleGameSpeedSelection
    if (checkBoxSharedMoney)
      checkBoxSharedMoney->winEnable( FALSE );    // Host-only, mirrors the other host-side options
		TheLAN->GetMyGame()->setMapCRC( TheLAN->GetMyGame()->getMapCRC() );		// force a recheck
		TheLAN->GetMyGame()->setMapSize( TheLAN->GetMyGame()->getMapSize() ); // of if we have the map
		TheLAN->RequestHasMap();
		lanUpdateSlotList();
		updateGameOptions();

		// Discombobulator: launcher-supplied house color for joiners.
		// Joiners can't write directly to their slot — the host owns
		// authoritative slot state and broadcasts updates. So we ask
		// the host to apply the change via the standard "Color=" key
		// over RequestGameOptions; the host's LANAPICallbacks handler
		// accepts any 24-bit RGB int and rebroadcasts the new slot
		// list to every peer.
		extern Bool g_launcherPlayerColorSet;
		extern UnsignedInt g_launcherPlayerColor;
		if (g_launcherPlayerColorSet)
		{
			AsciiString options;
			options.format("Color=%d",
				MultiplayerSettings::packSlotColor(g_launcherPlayerColor));
			TheLAN->RequestGameOptions(options, true);
		}
		// Shader id propagation TODO: there's no "Shader=" key in
		// the host's options handler yet, so for now joiner shader
		// ids only round-trip through the slot list once the host
		// itself reads them from a future broadcast or the joiner
		// gets promoted to host. Wire format already carries the
		// field; the missing piece is a host-side handler analogous
		// to the "Color=" path. Tracked as a follow-up.
	}
	for (Int i = start; i < MAX_SLOTS; ++i)
	{
		//I'm a client, disable the controls I can't touch.
		if (!TheLAN->AmIHost())
			comboBoxPlayer[i]->winEnable(FALSE);

		comboBoxColor[i]->winEnable(FALSE);
		comboBoxPlayerTemplate[i]->winEnable(FALSE);
		comboBoxTeam[i]->winEnable(FALSE);
//		buttonStartPosition[i]->winEnable(FALSE);
	}

//	for (i = 0; i < MAX_SLOTS; ++i)
//	{
//		if (buttonStartPosition[i])
//			buttonStartPosition[i]->winHide(TRUE); // not picking start spots this way any more
//	}
//
	// Show the Menu
	layout->hide( FALSE );

	// Set Keyboard to Main Parent
	TheWindowManager->winSetFocus( parentLanGameOptions );

	s_isIniting = FALSE;

	if (TheLAN->AmIHost())
	{
		TheLAN->RequestGameOptions(GenerateGameOptionsString(),true);
		TheLAN->RequestGameAnnounce();
	}
	lanUpdateSlotList();
	LanPositionStartSpots();
	TheTransitionHandler->setGroup("LanGameOptionsFade");

	// animate controls
	//TheShell->registerWithAnimateManager(buttonBack, WIN_ANIMATION_SLIDE_RIGHT, TRUE, 1);

}

//-------------------------------------------------------------------------------------------------
/** Update options on screen */
//-------------------------------------------------------------------------------------------------
void updateGameOptions()
{
	LANGameInfo *theGame = TheLAN->GetMyGame();
	UnicodeString mapDisplayName;
	if (theGame && AreSlotListUpdatesEnabled())
	{
		const GameSlot *localSlot = theGame->getConstSlot(theGame->getLocalSlotNum());
		const MapMetaData *mapData = TheMapCache->findMap( TheLAN->GetMyGame()->getMap() );
		if (mapData && localSlot && localSlot->hasMap())
		{
			mapDisplayName.format(L"%ls", mapData->m_displayName.str());
		}
		else
		{
			AsciiString s = TheLAN->GetMyGame()->getMap();
			if (s.reverseFind('\\'))
			{
				s = s.reverseFind('\\') + 1;
			}
			mapDisplayName.format(L"%hs", s.str());
		}
		UnicodeString old = GadgetStaticTextGetText(textEntryMapDisplay);
		if(old.compare(mapDisplayName) != 0)
			LanPositionStartSpots();
		GadgetStaticTextSetText(textEntryMapDisplay, mapDisplayName);

    // Mirror the host's superweapon limit into the combo. If the host picked a
    // value not in our preset list, repopulate so the dropdown shows it.
    if (comboBoxSuperweaponLimit)
    {
      Int swItemCount = GadgetComboBoxGetLength(comboBoxSuperweaponLimit);
      Int swIndex = 0;
      Bool swFound = FALSE;
      for ( ; swIndex < swItemCount; ++swIndex )
      {
        Int value = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(comboBoxSuperweaponLimit, swIndex)));
        if (value == (Int)theGame->getSuperweaponRestriction())
        {
          GadgetComboBoxSetSelectedPos(comboBoxSuperweaponLimit, swIndex, TRUE);
          swFound = TRUE;
          break;
        }
      }
      if (!swFound)
      {
        PopulateSuperweaponLimitComboBox(comboBoxSuperweaponLimit, theGame);
      }
    }

		Int itemCount = GadgetComboBoxGetLength(comboBoxStartingCash);
    Int index = 0;
    for ( ; index < itemCount; index++ )
    {
      Int value  = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(comboBoxStartingCash, index)));
      if ( value == theGame->getStartingCash().countMoney() )
      {
        GadgetComboBoxSetSelectedPos(comboBoxStartingCash, index, TRUE);
        break;
      }
    }

    DEBUG_ASSERTCRASH( index < itemCount, ("Could not find new starting cash amount %d in list", theGame->getStartingCash().countMoney() ) );

    // Mirror the host's Shared Money state into the checkbox. The checkbox
    // was created programmatically so there's no .wnd-driven refresh — we
    // just poll each updateGameOptions tick. Hosts see their own clicks
    // reflected immediately; joiners see the GameInfo value, which was
    // just applied by ParseAsciiStringToGameInfo's STM= branch.
    if (checkBoxSharedMoney)
    {
      Bool shouldBeChecked = theGame->isSharedTeamMoney();
      if (GadgetCheckBoxIsChecked(checkBoxSharedMoney) != shouldBeChecked)
        GadgetCheckBoxSetChecked(checkBoxSharedMoney, shouldBeChecked);
    }

    // Mirror the host's chosen game speed into the combo. If the host picked a
    // value not in our preset list (manual override on their end), repopulate
    // so the dropdown shows the actual value instead of silently snapping.
    if (comboBoxGameSpeed)
    {
      Int speedItemCount = GadgetComboBoxGetLength(comboBoxGameSpeed);
      Int speedIndex = 0;
      Bool found = FALSE;
      for ( ; speedIndex < speedItemCount; ++speedIndex )
      {
        Int value = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(comboBoxGameSpeed, speedIndex)));
        if (value == theGame->getGameFps())
        {
          GadgetComboBoxSetSelectedPos(comboBoxGameSpeed, speedIndex, TRUE);
          found = TRUE;
          break;
        }
      }
      if (!found)
      {
        PopulateGameSpeedComboBox(comboBoxGameSpeed, theGame);
      }
    }
	}
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void setLANPlayerTooltip(LANPlayer* player)
{
	UnicodeString tooltip;

	if (!player->getLogin().isEmpty() || !player->getHost().isEmpty())
	{
		tooltip.format(TheGameText->fetch("TOOLTIP:LANPlayer"), player->getLogin().str(), player->getHost().str());
	}

#if defined(RTS_DEBUG)
	UnicodeString ip;
	ip.format(L" - %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(player->getIP()));
	tooltip.concat(ip);
#endif

	if (!tooltip.isEmpty())
	{
		TheMouse->setCursorTooltip( tooltip );
	}
}


//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{
	DeinitLanGameGadgets();
	textEntryMapDisplay = nullptr;
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
/** Lan Game Options menu shutdown method */
//-------------------------------------------------------------------------------------------------
void LanGameOptionsMenuShutdown( WindowLayout *layout, void *userData )
{
	TheMouse->setCursor(Mouse::ARROW);
	TheMouse->setMouseText(UnicodeString::TheEmptyString,nullptr,nullptr);
	EnableSlotListUpdates(FALSE);
	LANisShuttingDown = true;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}

	TheShell->reverseAnimatewindow();
	TheTransitionHandler->reverse("LanGameOptionsFade");
	if (TheLAN)
		TheLAN->ResetGameStartTimer();

	/*
	// hide menu
	layout->hide( TRUE );

	// Reset the LAN singleton
//	TheLAN->reset();

	// our shutdown is complete
	TheShell->shutdownComplete( layout );
	*/
}

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu update method */
//-------------------------------------------------------------------------------------------------
void LanGameOptionsMenuUpdate( WindowLayout * layout, void *userData)
{
	if(LANisShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
		shutdownComplete(layout);
	//TheLAN->update(); // this is handled in the lobby

	// Animate per-slot color swatches for any human player whose
	// profile shader effect is non-stock. lanUpdateSlotList sets the
	// stock RGB on slot events; this tick overlays the time-driven
	// modulation so the swatch reads the same as the launcher's
	// animated preview tile (and similar in spirit to what the HLSL
	// renders on units in-world).
	LANGameInfo *game = TheLAN ? TheLAN->GetMyGame() : nullptr;
	if (game)
	{
		UnsignedInt now = timeGetTime();
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			GameWindow *swatch = colorSwatch[i];
			if (!swatch || swatch->winIsHidden())
				continue;
			GameSlot *slot = game->getSlot(i);
			if (!slot || !slot->isHuman())
				continue;
			Int shaderId = slot->getShaderId();
			if (shaderId == 0)
				continue;
			Int slotColor = slot->getColor();
			if (slotColor < 0)
				continue;
			Color animated = MultiplayerSettings::resolveSlotColorWithEffect(slotColor, shaderId, now);
			GadgetButtonSetEnabledColor(swatch, animated);
			GadgetButtonSetEnabledSelectedColor(swatch, animated);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanGameOptionsMenuInput( GameWindow *window, UnsignedInt msg,
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
/** Lan Game Options menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanGameOptionsMenuSystem( GameWindow *window, UnsignedInt msg,
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;
	switch( msg )
	{
		//-------------------------------------------------------------------------------------------------
		case GWM_CREATE:
			{
				break;
			}
		//-------------------------------------------------------------------------------------------------
		case GWM_DESTROY:
			{
				if (windowMap)
					windowMap->winSetUserData(nullptr);

				break;
			}
		//-------------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
			{
				// if we're givin the opportunity to take the keyboard focus we must say we want it
				if( mData1 == TRUE )
					*(Bool *)mData2 = TRUE;

				return MSG_HANDLED;
			}
		//-------------------------------------------------------------------------------------------------
		case GCM_SELECTED:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				LANGameInfo *myGame = TheLAN->GetMyGame();

        if ( controlID == comboBoxStartingCashID )
        {
          handleStartingCashSelection();
        }
        else if ( controlID == comboBoxGameSpeedID )
        {
          handleGameSpeedSelection();
        }
        else if ( controlID == comboBoxSuperweaponLimitID )
        {
          handleSuperweaponLimitSelection();
        }
        else
        {
				  for (Int i = 0; i < MAX_SLOTS; i++)
				  {
					  if (controlID == comboBoxColorID[i])
					  {
						  handleColorSelection(i);
					  }
					  else if (controlID == comboBoxPlayerTemplateID[i])
					  {
						  handlePlayerTemplateSelection(i);
					  }
					  else if (controlID == comboBoxTeamID[i])
					  {
						  handleTeamSelection(i);
					  }
					  else if( controlID == comboBoxPlayerID[i] && myGame->amIHost() )
					  {
						  // We don't have anything that'll happen if we click on ourselves
						  if(i == myGame->getLocalSlotNum())
						   break;
						  // Get
						  Int pos = -1;
						  GadgetComboBoxGetSelectedPos(comboBoxPlayer[i], &pos);
						  if( pos != SLOT_PLAYER && pos >= 0)
						  {
							  if( myGame->getLANSlot(i)->getState() == SLOT_PLAYER )
							  {
								  UnicodeString name = myGame->getPlayerName(i);
								  myGame->getLANSlot(i)->setState(SlotState(pos));
								  myGame->resetAccepted();
								  TheLAN->OnPlayerLeave(name);
							  }
							  else if( myGame->getLANSlot(i)->getState() != pos )
							  {
								  Bool wasAI = (myGame->getLANSlot(i)->isAI());
								  myGame->getLANSlot(i)->setState(SlotState(pos));
								  Bool isAI = (myGame->getLANSlot(i)->isAI());
								  if (wasAI || isAI)
									  myGame->resetAccepted();
								  if (wasAI ^ isAI)
									  PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, myGame, wasAI);
								  if (!s_isIniting)
								  {
									  TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
									  lanUpdateSlotList();
								  }
							  }
						  }
              break;
            }
          }
				}
        break;
			}
		//-------------------------------------------------------------------------------------------------
		case GBM_SELECTED:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if ( controlID == checkBoxSharedMoneyID )
				{
					handleSharedMoneyToggle();
					break;
				}

				if ( controlID == buttonBackID )
				{
					if( mapSelectLayout )
						{
							mapSelectLayout->destroyWindows();
							deleteInstance(mapSelectLayout);
							mapSelectLayout = nullptr;
						}
					TheLAN->RequestGameLeave();
					//TheShell->pop();

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
					if (!txtInput.isEmpty())
						TheLAN->RequestChat(txtInput, LANAPIInterface::LANCHAT_EMOTE);
				}
				else if ( controlID == buttonSelectMapID )
				{
					//buttonBack->winEnable( false );

					mapSelectLayout = TheWindowManager->winCreateLayout( "Menus/LanMapSelectMenu.wnd" );
					mapSelectLayout->runInit();
					mapSelectLayout->hide( FALSE );
					mapSelectLayout->bringForward();

				}
				else if ( controlID == buttonStartID )
				{
					if (TheLAN->AmIHost())
					{
						StartPressed();
						//TheLAN->RequestGameStart();
					}
					else
					{
						//I'm the Client... send an accept message to the host.
						TheLAN->RequestAccept();

						// Disable the accept button
						EnableAcceptControls(TRUE, TheLAN->GetMyGame(), comboBoxPlayer, comboBoxColor, comboBoxPlayerTemplate,
							comboBoxTeam, buttonAccept, buttonStart, buttonMapStartPosition);

					}
				}
				else
				{
					for (Int i = 0; i < MAX_SLOTS; i++)
					{
						if (controlID == buttonMapStartPositionID[i])
						{
							LANGameInfo *game = TheLAN->GetMyGame();
							Int playerIdxInPos = -1;
							for (Int j=0; j<MAX_SLOTS; ++j)
							{
								LANGameSlot *slot = game->getLANSlot(j);
								if (slot && slot->getStartPos() == i)
								{
									playerIdxInPos = j;
									break;
								}
							}
							if (playerIdxInPos >= 0)
							{
								LANGameSlot *slot = game->getLANSlot(playerIdxInPos);
								if (playerIdxInPos == game->getLocalSlotNum() || (game->amIHost() && slot && slot->isAI()))
								{
									// it's one of my type.  Try to change it.
									Int nextPlayer = getNextSelectablePlayer(playerIdxInPos+1);
									handleStartPositionSelection(playerIdxInPos, -1);
									if (nextPlayer >= 0)
									{
										handleStartPositionSelection(nextPlayer, i);
									}
								}
							}
							else
							{
								// nobody in the slot - put us in
								Int nextPlayer = getNextSelectablePlayer(0);
								if (nextPlayer < 0)
									nextPlayer = getFirstSelectablePlayer(game);
								handleStartPositionSelection(nextPlayer, i);
							}
						}
					}
				}

				break;
			}
		//-------------------------------------------------------------------------------------------------
		case GBM_SELECTED_RIGHT:
		{
			if (LANbuttonPushed)
				break;

			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
			for (Int i = 0; i < MAX_SLOTS; i++)
			{
				if (controlID == buttonMapStartPositionID[i])
				{
					LANGameInfo *game = TheLAN->GetMyGame();
					Int playerIdxInPos = -1;
					for (Int j=0; j<MAX_SLOTS; ++j)
					{
						LANGameSlot *slot = game->getLANSlot(j);
						if (slot && slot->getStartPos() == i)
						{
							playerIdxInPos = j;
							break;
						}
					}
					if (playerIdxInPos >= 0)
					{
						LANGameSlot *slot = game->getLANSlot(playerIdxInPos);
						if (playerIdxInPos == game->getLocalSlotNum() || (game->amIHost() && slot && slot->isAI()))
						{
							// it's one of my type.  Remove it.
							handleStartPositionSelection(playerIdxInPos, -1);
						}
					}
				}
			}
			break;
		}
		//-------------------------------------------------------------------------------------------------
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
					txtInput.trim();
					// Echo the user's input to the chat window
					if (!txtInput.isEmpty())
						TheLAN->RequestChat(txtInput, LANAPIInterface::LANCHAT_NORMAL);

				}
				break;
			}
		//-------------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

//-------------------------------------------------------------------------------------------------
/** Utility FUnction used as a bridge from other windows to this one */
//-------------------------------------------------------------------------------------------------
void PostToLanGameOptions( PostToLanGameType post )
{
	if (post >= POST_TO_LAN_GAME_TYPE_COUNT)
		return;
	LanPositionStartSpots();
	switch (post)
	{
			//-------------------------------------------------------------------------------------------------
		case SEND_GAME_OPTS:
		{
			LANGameInfo *game = TheLAN->GetMyGame();
			game->resetAccepted();
			updateGameOptions();
			lanUpdateSlotList();

			//buttonBack->winEnable( true );

			for(Int i = 0; i < MAX_SLOTS; ++i)
			{
				game->getSlot(i)->setStartPos(-1);
			}

			TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
			break;
		}
		case MAP_BACK:
		{
				//buttonBack->winEnable( true );
		}
		default:
			return;
	}
}
