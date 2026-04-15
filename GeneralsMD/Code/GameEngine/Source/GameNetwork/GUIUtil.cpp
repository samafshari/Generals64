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

// FILE: GUIUtil.cpp //////////////////////////////////////////////////////
// Author: Matthew D. Campbell, Sept 2002

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include <set>

#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/MapUtil.h"
#include "Common/NameKeyGenerator.h"

#include "Common/MultiplayerSettings.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameText.h"
#include "GameLogic/GameLogic.h" // SUPERWEAPON_RESTRICT_COUNT
#include "GameNetwork/GameInfo.h"
#include "Common/PlayerTemplate.h"
#include "GameNetwork/LANAPICallbacks.h" // for acceptTrueColor, etc
#include "GameClient/ChallengeGenerals.h"


// -----------------------------------------------------------------------------

static Bool winInitialized = FALSE;

void EnableSlotListUpdates( Bool val )
{
	winInitialized = val;
}

Bool AreSlotListUpdatesEnabled()
{
	return winInitialized;
}

// -----------------------------------------------------------------------------

void EnableAcceptControls(Bool Enabled, GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[], GameWindow *buttonStart,
										GameWindow *buttonMapStartPosition[], Int slotNum)
{
	if(slotNum == -1 || slotNum >= MAX_SLOTS )
		slotNum = myGame->getLocalSlotNum();

	Bool isObserver = myGame->getConstSlot(slotNum)->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER;

	if( !myGame->amIHost() && (buttonStart != nullptr) )
		buttonStart->winEnable(Enabled);
	if(comboColor[slotNum])
	{
		if (isObserver)
		{
			GadgetComboBoxHideList(comboColor[slotNum]);
		}

		// Network multiplayer: human players set their color in the
		// launcher via -color, not from the in-game dropdown — the
		// launcher is the canonical source of identity. The combo
		// is still useful for the HOST picking BOT colors, and
		// skirmish / challenge modes still use it for everything
		// since no launcher is involved there.
		Bool colorEditable = !isObserver;
		const Bool isNetworkGame = (myGame != (GameInfo *)TheSkirmishGameInfo)
		                        && (myGame != (GameInfo *)TheChallengeGameInfo);
		if (isNetworkGame && myGame->getConstSlot(slotNum)->isHuman())
			colorEditable = FALSE;
		comboColor[slotNum]->winEnable(Enabled && colorEditable);
	}
	if(comboPlayerTemplate[slotNum])
		comboPlayerTemplate[slotNum]->winEnable(Enabled);
	if(comboTeam[slotNum])
	{
		if (isObserver)
		{
			GadgetComboBoxHideList(comboTeam[slotNum]);
		}
		comboTeam[slotNum]->winEnable(Enabled && !isObserver);
	}

	Bool canChooseStartSpot = FALSE;
	if (!isObserver)
		canChooseStartSpot = TRUE;
	for (Int i=0; i<MAX_SLOTS && !canChooseStartSpot && myGame->amIHost(); ++i)
	{
		if (myGame->getConstSlot(i) && myGame->getConstSlot(i)->isAI())
			canChooseStartSpot = TRUE;
	}

	if (slotNum == myGame->getLocalSlotNum())
	{
		if (myGame->getConstSlot(myGame->getLocalSlotNum())->hasMap())
		{
			for (Int i=0; i<MAX_SLOTS; ++i)
			{
				if (buttonMapStartPosition[i])
				{
					buttonMapStartPosition[i]->winEnable(Enabled && canChooseStartSpot);
				}
			}
		}
		else
		{
			for (Int i=0; i<MAX_SLOTS; ++i)
			{
				if (buttonMapStartPosition[i])
					buttonMapStartPosition[i]->winEnable(FALSE);
			}
		}
	}
}

// -----------------------------------------------------------------------------

void ShowUnderlyingGUIElements( Bool show, const char *layoutFilename, const char *parentName,
															 const char **gadgetsToHide, const char **perPlayerGadgetsToHide )
{
	AsciiString parentNameStr;
	parentNameStr.format("%s:%s", layoutFilename, parentName);
	NameKeyType parentID = NAMEKEY(parentNameStr);
	GameWindow *parent = TheWindowManager->winGetWindowFromId( nullptr, parentID );
	if (!parent)
	{
		DEBUG_CRASH(("Window %s not found", parentNameStr.str()));
		return;
	}

	// hide some GUI elements of the screen underneath
	GameWindow *win;

	Int player;
	const char **text;

	text = gadgetsToHide;
	while (*text)
	{
		AsciiString gadgetName;
		gadgetName.format("%s:%s", layoutFilename, *text);
		win	= TheWindowManager->winGetWindowFromId( parent, NAMEKEY(gadgetName) );
		//DEBUG_ASSERTCRASH(win, ("Cannot find %s to show/hide it", gadgetName.str()));
		if (win)
		{
			win->winHide( !show );
		}
		++text;
	}

	text = perPlayerGadgetsToHide;
	while (*text)
	{
		for (player = 0; player < MAX_SLOTS; ++player)
		{
			AsciiString gadgetName;
			gadgetName.format("%s:%s%d", layoutFilename, *text, player);
			win	= TheWindowManager->winGetWindowFromId( parent, NAMEKEY(gadgetName) );
			//DEBUG_ASSERTCRASH(win, ("Cannot find %s to show/hide it", gadgetName.str()));
			if (win)
			{
				win->winHide( !show );
			}
		}
		++text;
	}
}

// -----------------------------------------------------------------------------

void PopulateColorComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver)
{
	// Slot colors are raw 24-bit RGB ints now (not palette indices),
	// so the combo box stores the RGB as the per-row item data and
	// the dedup loop compares RGBs directly. The multiplayer.ini
	// palette is still the source of the displayed presets — players
	// who want a color outside the presets get one from the launcher
	// via -color, and we synthesize a "Custom" entry below so the
	// dropdown can still show + select that color.
	Int numColors = TheMultiplayerSettings->getNumColors();
	UnicodeString colorName;

	// Build the set of preset RGBs that other slots have already
	// claimed, so we can hide them from this slot's dropdown.
	std::set<Int> takenRgbs;
	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		GameSlot *slot = myGame->getSlot(i);
		if (slot && (i != comboBox) && slot->getColor() >= 0)
			takenRgbs.insert(slot->getColor());
	}

	// Track which RGBs are in the stock palette so the Custom-entry
	// logic at the bottom can decide whether THIS slot's color
	// already maps to a preset (no Custom needed) or is a launcher-
	// supplied custom value (needs a synthesized entry).
	std::set<Int> presetRgbs;

	Bool wasObserver = (GadgetComboBoxGetLength(comboArray[comboBox]) == 1);
	GadgetComboBoxReset(comboArray[comboBox]);

	// First entry is always the "random / no choice" sentinel —
	// item data -1 (no real RGB), display as either "None" (observer)
	// or the random-pick label.
	MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_RANDOM);
	Int newIndex = GadgetComboBoxAddEntry(comboArray[comboBox],
		(isObserver)?TheGameText->fetch("GUI:None"):TheGameText->fetch("GUI:???"), def->getColor());
	GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, reinterpret_cast<void *>(static_cast<intptr_t>(-1)));

	if (isObserver)
	{
		GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);
		return;
	}

	for (Int c = 0; c < numColors; ++c)
	{
		def = TheMultiplayerSettings->getColor(c);
		if (!def) continue;

		// Item data is the raw 24-bit RGB the slot will be set to
		// when this entry is picked. Hide presets already taken by
		// another slot.
		Int rgb = (Int)(def->getColor() & 0x00FFFFFFu);
		presetRgbs.insert(rgb);
		if (takenRgbs.find(rgb) != takenRgbs.end())
			continue;

		colorName = TheGameText->fetch(def->getTooltipName().str());
		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], colorName, def->getColor());
		GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, reinterpret_cast<void *>(static_cast<intptr_t>(rgb)));
	}

	// Custom-color entry. If THIS slot's current color is a real
	// RGB (not the random sentinel) and isn't in the preset
	// palette, synthesize a "Custom" entry whose item data is the
	// slot's actual RGB and whose displayed swatch text is rendered
	// in that color. Without this, the dropdown can't represent the
	// current slot color → it ends up empty / unselected after
	// UpdateSlotList runs its selection loop, which is what the
	// user kept seeing for launcher-supplied colors.
	GameSlot *thisSlot = myGame->getSlot(comboBox);
	if (thisSlot && thisSlot->getColor() >= 0)
	{
		Int slotRgb = thisSlot->getColor() & 0x00FFFFFF;
		if (presetRgbs.find(slotRgb) == presetRgbs.end())
		{
			UnicodeString customLabel;
			customLabel.translate(AsciiString("Custom"));
			Color swatchColor = MultiplayerSettings::resolveSlotColor(slotRgb);
			newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], customLabel, swatchColor);
			GadgetComboBoxSetItemData(comboArray[comboBox], newIndex,
				reinterpret_cast<void *>(static_cast<intptr_t>(slotRgb)));
		}
	}

	if (wasObserver)
		GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);
}

// -----------------------------------------------------------------------------

void PopulatePlayerTemplateComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool allowObservers)
{
	Int numPlayerTemplates = ThePlayerTemplateStore->getPlayerTemplateCount();
	UnicodeString playerTemplateName;

	GadgetComboBoxReset(comboArray[comboBox]);

	MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_RANDOM);
	Int newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], TheGameText->fetch("GUI:Random"), def->getColor());
	GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, reinterpret_cast<void *>(static_cast<intptr_t>(PLAYERTEMPLATE_RANDOM)));

	std::set<AsciiString> seenSides;

	for (Int c=0; c<numPlayerTemplates; ++c)
	{
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(c);
		if (!fac)
			continue;

		if (fac->getStartingBuilding().isEmpty())
			continue;

		if ( myGame->oldFactionsOnly() && !fac->isOldFaction() )
		  continue;

		// Prevent players from selecting the disabled Generals for use.
		// This is also enforced at game loading (GameLogic.cpp and UserPreferences.cpp).
		// @todo: unlock these when something rad happens
		Bool disallowLockedGenerals = TRUE;
		const GeneralPersona *general = TheChallengeGenerals->getGeneralByTemplateName(fac->getName());
		Bool startsLocked = general ? !general->isStartingEnabled() : FALSE;
		if (disallowLockedGenerals && startsLocked)
			continue;


		AsciiString side;
		side.format("SIDE:%s", fac->getSide().str());
		if (seenSides.find(side) != seenSides.end())
			continue;

		seenSides.insert(side);

		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], TheGameText->fetch(side), def->getColor());
		GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, reinterpret_cast<void *>(static_cast<intptr_t>(c)));
	}
	seenSides.clear();

	// disabling observers for Multiplayer test
	if (allowObservers)
	{
		def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_OBSERVER);
		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], TheGameText->fetch("GUI:Observer"), def->getColor());
		GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, reinterpret_cast<void *>(static_cast<intptr_t>(PLAYERTEMPLATE_OBSERVER)));
	}
	GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);

}

// -----------------------------------------------------------------------------

void PopulateTeamComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver)
{
	Int numTeams = MAX_SLOTS/2;
	UnicodeString teamName;

	GadgetComboBoxReset(comboArray[comboBox]);

	MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_RANDOM);
	Int newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], TheGameText->fetch("Team:0"), def->getColor());
	GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, reinterpret_cast<void *>(static_cast<intptr_t>(-1)));

	if (isObserver)
	{
		GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);
		return;
	}

	for (Int c=0; c<numTeams; ++c)
	{
		AsciiString teamStr;
		teamStr.format("Team:%d", c + 1);
		teamName = TheGameText->fetch(teamStr.str());
		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], teamName, def->getColor());
		GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, reinterpret_cast<void *>(static_cast<intptr_t>(c)));
	}
	GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);
}

// -----------------------------------------------------------------------------
static UnicodeString formatMoneyForStartingCashComboBox( const Money & moneyAmount )
{
  UnicodeString rtn;
  rtn.format( TheGameText->fetch( "GUI:StartingMoneyFormat" ), moneyAmount.countMoney() );
  return rtn;
}

void PopulateStartingCashComboBox(GameWindow *comboBox, GameInfo *myGame)
{
  GadgetComboBoxReset(comboBox);

  const MultiplayerStartingMoneyList & startingCashMap = TheMultiplayerSettings->getStartingMoneyList();
  Int currentSelectionIndex = -1;

  MultiplayerStartingMoneyList::const_iterator it = startingCashMap.begin();
  for ( ; it != startingCashMap.end(); it++ )
  {
    Int newIndex = GadgetComboBoxAddEntry(comboBox, formatMoneyForStartingCashComboBox( *it ),
                                          comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor());
    GadgetComboBoxSetItemData(comboBox, newIndex, reinterpret_cast<void *>(static_cast<uintptr_t>(it->countMoney())));

    if ( myGame->getStartingCash().amountEqual( *it ) )
    {
      currentSelectionIndex = newIndex;
    }
  }

  if ( currentSelectionIndex == -1 )
  {
    DEBUG_CRASH( ("Current selection for starting cash not found in list") );
    currentSelectionIndex = GadgetComboBoxAddEntry(comboBox, formatMoneyForStartingCashComboBox( myGame->getStartingCash() ),
                                          comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor());
    GadgetComboBoxSetItemData(comboBox, currentSelectionIndex, reinterpret_cast<void *>(static_cast<uintptr_t>(it->countMoney())) );
  }

  GadgetComboBoxSetSelectedPos(comboBox, currentSelectionIndex);
}

// -----------------------------------------------------------------------------

//  -----------------------------------------------------------------------------------------
// The slot list displaying function
//-------------------------------------------------------------------------------------------------
void UpdateSlotList( GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[],
										GameWindow *buttonStart, GameWindow *buttonMapStartPosition[] )
{
	if(!AreSlotListUpdatesEnabled())
		return;
	//LANGameInfo *myGame = TheLAN->GetMyGame();

	const MapMetaData *mapData = TheMapCache->findMap( myGame->getMap() );
	Bool willTransfer = TRUE;
	if (mapData)
	{
		willTransfer = !mapData->m_isOfficial;
	}
	else
	{
		willTransfer = WouldMapTransfer(myGame->getMap());
	}

	if (myGame)
	{
		for( int i =0; i < MAX_SLOTS; i++ )
		{
			GameSlot * slot = myGame->getSlot(i);

			// if i'm host, enable the controls for AI
			if(myGame->amIHost() && slot->isAI())
			{
				EnableAcceptControls(TRUE, myGame, comboPlayer, comboColor, comboPlayerTemplate,
					comboTeam, buttonAccept, buttonStart, buttonMapStartPosition, i);
			}
			else if (myGame->getLocalSlotNum() == i)
			{
				if(slot->isAccepted() && !myGame->amIHost())
				{
					EnableAcceptControls(FALSE, myGame, comboPlayer, comboColor, comboPlayerTemplate,
						comboTeam, buttonAccept, buttonStart, buttonMapStartPosition);
				}
				else
				{
					if (slot->hasMap()) {
						EnableAcceptControls(TRUE, myGame, comboPlayer, comboColor, comboPlayerTemplate,
							comboTeam, buttonAccept, buttonStart, buttonMapStartPosition);
					}
					else
					{
						EnableAcceptControls(willTransfer, myGame, comboPlayer, comboColor, comboPlayerTemplate,
							comboTeam, buttonAccept, buttonStart, buttonMapStartPosition);
					}
				}

			}
			else if(myGame->amIHost())
			{
				EnableAcceptControls(FALSE, myGame, comboPlayer, comboColor, comboPlayerTemplate,
					comboTeam, buttonAccept, buttonStart, buttonMapStartPosition, i);
			}
			if(slot->isHuman())
			{
				UnicodeString newName = slot->getName();
				UnicodeString oldName = GadgetComboBoxGetText(comboPlayer[i]);
				if (comboPlayer[i] && newName.compare(oldName))
				{
					GadgetComboBoxSetText(comboPlayer[i], newName);
				}
				if(i!= 0 && buttonAccept && buttonAccept[i])
				{
					buttonAccept[i]->winHide(FALSE);
				//Color In the little accepted boxes
					if(slot->isAccepted())
					{
						if(BitIsSet(buttonAccept[i]->winGetStatus(), WIN_STATUS_IMAGE	))
							buttonAccept[i]->winEnable(TRUE);
						else
							GadgetButtonSetEnabledColor(buttonAccept[i], acceptTrueColor );
					}
					else
					{
						if(BitIsSet(buttonAccept[i]->winGetStatus(), WIN_STATUS_IMAGE	))
							buttonAccept[i]->winEnable(FALSE);
						else
							GadgetButtonSetEnabledColor(buttonAccept[i], acceptFalseColor );
					}
				}
			}
			else
			{
				GadgetComboBoxSetSelectedPos(comboPlayer[i], slot->getState(), TRUE);
        if( buttonAccept &&  buttonAccept[i] )
				  buttonAccept[i]->winHide(TRUE);
			}
/*
			if (myGame->getLocalSlotNum() == i && i!=0)
			{
				if (comboPlayer[i])
					comboPlayer[i]->winEnable( TRUE );
			}
			else*/ if (!myGame->amIHost())
			{
				if (comboPlayer[i])
					comboPlayer[i]->winEnable( FALSE );
			}
			// Re-populate the color combo for EVERY slot, not just
			// enabled ones (the ones the local player can edit).
			// Disabled combos belong to remote players, and their
			// slot color can change between updates as the host
			// rebroadcasts the slot list — without re-populating
			// the combo, a remote player who picks a launcher-
			// supplied custom color would never get a "Custom"
			// entry synthesized for their disabled dropdown, and
			// the selection loop below would silently fail to find
			// a matching entry. Re-population is cheap (~8 entries)
			// and unconditional now.
			if (comboColor[i] != nullptr)
				PopulateColorComboBox(i, comboColor, myGame, myGame->getConstSlot(i)->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER);
			Int max, idx;
			if (comboColor[i] != nullptr) {
				max = GadgetComboBoxGetLength(comboColor[i]);
				for (idx=0; idx<max; ++idx)
				{
					Int color = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(comboColor[i], idx)));
					if (color == slot->getColor())
					{
						GadgetComboBoxSetSelectedPos(comboColor[i], idx, TRUE);
						break;
					}
				}
			}

			if (comboTeam[i] != nullptr) {
				max = GadgetComboBoxGetLength(comboTeam[i]);
				for (idx=0; idx<max; ++idx)
				{
					Int team = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(comboTeam[i], idx)));
					if (team == slot->getTeamNumber())
					{
						GadgetComboBoxSetSelectedPos(comboTeam[i], idx, TRUE);
						break;
					}
				}
			}

			if (comboPlayerTemplate[i] != nullptr) {
				max = GadgetComboBoxGetLength(comboPlayerTemplate[i]);
				for (idx=0; idx<max; ++idx)
				{
					Int playerTemplate = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(comboPlayerTemplate[i], idx)));
					if (playerTemplate == slot->getPlayerTemplate())
					{
						GadgetComboBoxSetSelectedPos(comboPlayerTemplate[i], idx, TRUE);
						break;
					}
				}
			}
		}
	}
}

// -----------------------------------------------------------------------------
