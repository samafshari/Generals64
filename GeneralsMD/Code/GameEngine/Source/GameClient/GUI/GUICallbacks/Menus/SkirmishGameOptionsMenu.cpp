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
// FILE: SkirmishGameOptionsMenu.cpp
// Author: Chris Brue, August 2002
// Description: Lan Game Options Menu
///////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine


#include "Common/BattleHonors.h"
#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
#include "Common/PlayerTemplate.h"
#include "Common/QuotedPrintable.h"
#include "Common/RandomValue.h"
#include "Common/SkirmishBattleHonors.h"
#include "Common/SkirmishPreferences.h"
#include "GameLogic/GameLogic.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/ClientInstance.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/Display.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"

#include "Common/MultiplayerSettings.h"
#include "GameClient/GameText.h"
#include "GameClient/ExtendedMessageBox.h"
#include "GameClient/MessageBox.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/IPEnumeration.h"
#include "WWDownload/Registry.h"


namespace
{
	Int skirmishBattleHonorRowsToSkip = 0;

	void ResetSkirmishBattleHonorInsertion()
	{
		skirmishBattleHonorRowsToSkip = 0;
	}

	void SkirmishBattleHonorTooltip(GameWindow *window, WinInstanceData *, UnsignedInt mouse)
	{
		Int x = LOLONGTOSHORT(mouse);
		Int y = HILONGTOSHORT(mouse);
		Int row;
		Int col;
		GadgetListBoxGetEntryBasedOnXY(window, x, y, row, col);

		if (row == -1 || col == -1)
		{
			TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonors"));
			return;
		}

		const Int battleHonor = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetListBoxGetItemData(window, row, col)));
		const Int extraValue = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetListBoxGetItemData(window, row - 1, col)));
		if (battleHonor == 0)
		{
			TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonors"));
			return;
		}

		const Real tooltipWidth = 1.5f;
		if (BitIsSet(battleHonor, BATTLE_HONOR_NOT_GAINED))
		{
			if (BitIsSet(battleHonor, BATTLE_HONOR_LOYALTY_USA))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyUSADisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_LOYALTY_CHINA))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyChinaDisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_LOYALTY_GLA))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyGLADisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_BATTLE_TANK))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorBattleTankDisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_AIR_WING))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorAirWingDisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_ENDURANCE))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorEnduranceDisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_CAMPAIGN_USA))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorCampaignUSADisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_CAMPAIGN_CHINA))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorCampaignChinaDisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_CAMPAIGN_GLA))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorCampaignGLADisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_BLITZ10))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorBlitzDisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_FAIR_PLAY))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorFairPlayDisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_APOCALYPSE))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorApocalypseDisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_CHALLENGE_MODE))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorCampaignChallengeDisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_ULTIMATE))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorUltimateDisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_GLOBAL_GENERAL))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorGlobalGeneralDisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_CHALLENGE))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorChallengeDisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_STREAK))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorStreakDisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_STREAK_ONLINE))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorStreakOnlineDisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_DOMINATION))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorDominationDisabled"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_DOMINATION_ONLINE))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorDominationOnlineDisabled"), -1, nullptr, tooltipWidth);
		}
		else
		{
			if (BitIsSet(battleHonor, BATTLE_HONOR_LOYALTY_USA))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyUSA"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_LOYALTY_CHINA))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyChina"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_LOYALTY_GLA))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorLoyaltyGLA"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_BATTLE_TANK))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorBattleTank"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_AIR_WING))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorAirWing"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_ENDURANCE))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorEndurance"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_CAMPAIGN_USA))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorCampaignUSA"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_CAMPAIGN_CHINA))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorCampaignChina"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_CAMPAIGN_GLA))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorCampaignGLA"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_BLITZ5))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorBlitz5"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_BLITZ10))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorBlitz10"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_FAIR_PLAY))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorFairPlay"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_APOCALYPSE))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorApocalypse"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_OFFICERSCLUB))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorOfficersClub"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_CHALLENGE_MODE))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorCampaignChallenge"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_ULTIMATE))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorUltimate"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_GLOBAL_GENERAL))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorGlobalGeneral"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_CHALLENGE))
				TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorChallenge"), -1, nullptr, tooltipWidth);
			else if (BitIsSet(battleHonor, BATTLE_HONOR_STREAK))
			{
				if (extraValue >= 1000)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorStreak1000"), -1, nullptr, tooltipWidth);
				else if (extraValue >= 500)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorStreak500"), -1, nullptr, tooltipWidth);
				else if (extraValue >= 100)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorStreak100"), -1, nullptr, tooltipWidth);
				else if (extraValue >= 25)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorStreak25"), -1, nullptr, tooltipWidth);
				else if (extraValue >= 10)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorStreak10"), -1, nullptr, tooltipWidth);
				else if (extraValue >= 3)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorStreak3"), -1, nullptr, tooltipWidth);
				else
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorStreakDisabled"), -1, nullptr, tooltipWidth);
			}
			else if (BitIsSet(battleHonor, BATTLE_HONOR_STREAK_ONLINE))
			{
				if (extraValue >= 1000)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorStreak1000Online"), -1, nullptr, tooltipWidth);
				else if (extraValue >= 500)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorStreak500Online"), -1, nullptr, tooltipWidth);
				else if (extraValue >= 100)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorStreak100Online"), -1, nullptr, tooltipWidth);
				else if (extraValue >= 25)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorStreak25Online"), -1, nullptr, tooltipWidth);
				else if (extraValue >= 10)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorStreak10Online"), -1, nullptr, tooltipWidth);
				else if (extraValue >= 3)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorStreak3Online"), -1, nullptr, tooltipWidth);
				else
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorStreakOnlineDisabled"), -1, nullptr, tooltipWidth);
			}
			else if (BitIsSet(battleHonor, BATTLE_HONOR_DOMINATION))
			{
				if (extraValue >= 10000)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorDomination10000"), -1, nullptr, tooltipWidth);
				else if (extraValue >= 1000)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorDomination1000"), -1, nullptr, tooltipWidth);
				else if (extraValue >= 500)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorDomination500"), -1, nullptr, tooltipWidth);
				else if (extraValue >= 100)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorDomination100"), -1, nullptr, tooltipWidth);
				else
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorDominationDisabled"), -1, nullptr, tooltipWidth);
			}
			else if (BitIsSet(battleHonor, BATTLE_HONOR_DOMINATION_ONLINE))
			{
				if (extraValue >= 10000)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorDomination10000Online"), -1, nullptr, tooltipWidth);
				else if (extraValue >= 1000)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorDomination1000Online"), -1, nullptr, tooltipWidth);
				else if (extraValue >= 500)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorDomination500Online"), -1, nullptr, tooltipWidth);
				else if (extraValue >= 100)
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorDomination100Online"), -1, nullptr, tooltipWidth);
				else
					TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:BattleHonorDominationOnlineDisabled"), -1, nullptr, tooltipWidth);
			}
		}
	}

	void InsertSkirmishBattleHonor(GameWindow *list, const Image *image, Bool enabled, Int itemData, Int &row, Int &column,
		UnicodeString text = UnicodeString::TheEmptyString, Int extra = 0)
	{
		const Real widthScale = TheDisplay->getWidth() / static_cast<Real>(DEFAULT_DISPLAY_WIDTH);
		const Real heightScale = TheDisplay->getHeight() / static_cast<Real>(DEFAULT_DISPLAY_HEIGHT);
		const Real honorScale = widthScale < heightScale ? widthScale : heightScale;
		const Int width = static_cast<Int>(MAX_BATTLE_HONOR_IMAGE_WIDTH * honorScale);
		const Int height = static_cast<Int>(MAX_BATTLE_HONOR_IMAGE_HEIGHT * honorScale);

		static const Int enabledColor = 0xFFFFFFFF;
		static const Int disabledColor = GameMakeColor(80, 80, 80, 255);
		Int color = enabled ? enabledColor : disabledColor;

		if (!enabled)
			itemData |= BATTLE_HONOR_NOT_GAINED;

		GadgetListBoxAddEntryImage(list, image, row, column, height, width, TRUE, color);
		GadgetListBoxSetItemData(list, reinterpret_cast<void *>(static_cast<intptr_t>(itemData)), row, column);
		GadgetListBoxSetItemData(list, reinterpret_cast<void *>(static_cast<intptr_t>(extra)), row - 1, column);
		(void)text;

		if (++column >= GadgetListBoxGetNumColumns(list))
		{
			column = 0;
			row = row + 1 + skirmishBattleHonorRowsToSkip;
			skirmishBattleHonorRowsToSkip = max(skirmishBattleHonorRowsToSkip - 1, 0);
		}
	}
}

// The shared popup helpers are stubbed out in this build, so skirmish needs local copies.
#define BattleHonorTooltip SkirmishBattleHonorTooltip
#define ResetBattleHonorInsertion ResetSkirmishBattleHonorInsertion
#define InsertBattleHonor InsertSkirmishBattleHonor


SkirmishGameInfo *TheSkirmishGameInfo = nullptr;

// window ids ------------------------------------------------------------------------------
static NameKeyType parentSkirmishGameOptionsID = NAMEKEY_INVALID;
static NameKeyType textEntryPlayerNameID = NAMEKEY_INVALID;
static NameKeyType comboBoxPlayerID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
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

static NameKeyType textEntryMapDisplayID = NAMEKEY_INVALID;
static NameKeyType buttonExitID = NAMEKEY_INVALID;
static NameKeyType buttonStartID = NAMEKEY_INVALID;
static NameKeyType buttonSelectMapID = NAMEKEY_INVALID;
static NameKeyType buttonResetID = NAMEKEY_INVALID;
static NameKeyType windowMapID = NAMEKEY_INVALID;
// Legacy slider id kept for source compat — the WND no longer ships a
// SliderGameSpeed control; lookups will simply return nullptr.
static NameKeyType sliderGameSpeedID = NAMEKEY_INVALID;
static NameKeyType staticTextGameSpeedID = NAMEKEY_INVALID;
// Modernised to ComboBoxes to match the Multiplayer lobby. Old checkbox/
// slider names are retained at the top of the file only for grep-ability.
static NameKeyType comboBoxSuperweaponLimitID = NAMEKEY_INVALID;
static NameKeyType comboBoxGameSpeedID        = NAMEKEY_INVALID;
static NameKeyType comboBoxStartingCashID     = NAMEKEY_INVALID;

// Window Pointers ------------------------------------------------------------------------
static GameWindow *staticTextGameSpeed = nullptr;
static GameWindow *parentSkirmishGameOptions = nullptr;
static GameWindow *buttonExit = nullptr;
static GameWindow *buttonStart = nullptr;
static GameWindow *buttonSelectMap = nullptr;
static GameWindow *textEntryMapDisplay = nullptr;
static GameWindow *buttonReset = nullptr;
static GameWindow *windowMap = nullptr;
static GameWindow *textEntryPlayerName = nullptr;
static GameWindow *comboBoxSuperweaponLimit = nullptr;
static GameWindow *comboBoxGameSpeed        = nullptr;
static GameWindow *comboBoxStartingCash     = nullptr;
static GameWindow *comboBoxPlayer[MAX_SLOTS] = {0};

static GameWindow *comboBoxColor[MAX_SLOTS] = {0};

static GameWindow *comboBoxPlayerTemplate[MAX_SLOTS] = {0};

static GameWindow *comboBoxTeam[MAX_SLOTS] = {0};

//static GameWindow *buttonStartPosition[MAX_SLOTS] = {0};
//
static GameWindow *buttonMapStartPosition[MAX_SLOTS] = {0};
//external declarations of the Gadgets the callbacks can use

WindowLayout *skirmishMapSelectLayout = nullptr;

static Int	initialGadgetDelay = 2;
static Bool justEntered = FALSE;
static Bool buttonPushed = FALSE;
static Bool stillNeedsToSetOptions = FALSE;
void skirmishUpdateSlotList();
static void populateSkirmishBattleHonors();
enum{ GREATER_NO_FPS_LIMIT = 60};
Bool doUpdateSlotList = TRUE;

static Int getNextSelectablePlayer(Int start)
{
	if (!TheSkirmishGameInfo->amIHost())
		return -1;
	for (Int j=start; j<MAX_SLOTS; ++j)
	{
		GameSlot *slot = TheSkirmishGameInfo->getSlot(j);
		if (slot && slot->getStartPos() == -1 && (j==TheSkirmishGameInfo->getLocalSlotNum() || slot->isAI()))
		{
			return j;
		}
	}
	return -1;
}

SkirmishPreferences::SkirmishPreferences()
{
	loadFromIniFile();
}

SkirmishPreferences::~SkirmishPreferences()
{
}

Bool SkirmishPreferences::loadFromIniFile()
{
	if (rts::ClientInstance::getInstanceId() > 1u)
	{
		AsciiString fname;
		fname.format("Skirmish_Instance%.2u.ini", rts::ClientInstance::getInstanceId());
		return load(fname);
	}

	return load("Skirmish.ini");
}

AsciiString SkirmishPreferences::getSlotList()
{
	return getAsciiString("SlotList", AsciiString::TheEmptyString);
}

void SkirmishPreferences::setSlotList()
{
	setAsciiString("SlotList", GameInfoToAsciiString(TheSkirmishGameInfo));
}

UnicodeString SkirmishPreferences::getUserName()
{
	UnicodeString ret;
	SkirmishPreferences::const_iterator it = find("UserName");
	if (it == end())
	{
		IPEnumeration IPs;
		ret.translate(IPs.getMachineName());
		return ret;
	}

	ret = QuotedPrintableToUnicodeString(it->second);
	ret.trim();
	if (ret.isEmpty())
	{
		IPEnumeration IPs;
		ret.translate(IPs.getMachineName());
		return ret;
	}

	return ret;
}

Int SkirmishPreferences::getPreferredColor()
{
	Int ret;
	SkirmishPreferences::const_iterator it = find("Color");
	if (it == end())
	{
		return -1;
	}

	ret = atoi(it->second.str());
	if (ret < -1 || ret >= TheMultiplayerSettings->getNumColors())
		ret = -1;

	return ret;
}

Int SkirmishPreferences::getPreferredFaction()
{
	Int ret;
	SkirmishPreferences::const_iterator it = find("PlayerTemplate");
	if (it == end())
	{
		return PLAYERTEMPLATE_RANDOM;
	}

	ret = atoi(it->second.str());
	if (ret < PLAYERTEMPLATE_MIN || ret >= ThePlayerTemplateStore->getPlayerTemplateCount())
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

Bool SkirmishPreferences::usesSystemMapDir()
{
	SkirmishPreferences::const_iterator it = find("UseSystemMapDir");
	if (it == end())
		return TRUE;

	if (stricmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

AsciiString SkirmishPreferences::getPreferredMap()
{
	AsciiString ret;
	SkirmishPreferences::const_iterator it = find("Map");
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

static const char superweaponRestrictionKey[] = "SuperweaponRestrict";

UnsignedShort SkirmishPreferences::getSuperweaponRestricted() const
{
  const_iterator it = find(superweaponRestrictionKey);
  if (it == end())
  {
    // No stored value → match the GameLogic / LAN lobby default of 3.
    // Prior builds returned "unrestricted" (0) here, which under the
    // DeterminedBySuperweaponRestriction semantic actually means
    // "superweapons disabled", not "unlimited".
    return 3;
  }

  // Legacy builds stored a Bool as "Yes"/"No"; collapse that to 1/3 so
  // users coming from the old format still get a sane experience
  // (checked = restrict to 1, unchecked = the default cap of 3).
  if ( it->second.compareNoCase( "yes" ) == 0 )
    return 1;
  if ( it->second.compareNoCase( "no" ) == 0 )
    return 3;

  // Numeric form written by setSuperweaponRestricted(UnsignedShort).
  return (UnsignedShort)atoi( it->second.str() );
}

void SkirmishPreferences::setSuperweaponRestricted( UnsignedShort superweaponLimit )
{
  AsciiString option;
  option.format( "%u", (unsigned)superweaponLimit );
  (*this)[superweaponRestrictionKey] = option;
}

static const char startingCashKey[] = "StartingCash";
Money SkirmishPreferences::getStartingCash() const
{
  const_iterator it = find(startingCashKey);
  if (it == end())
  {
    return TheMultiplayerSettings->getDefaultStartingMoney();
  }

  Money money;
  money.deposit( strtoul( it->second.str(), nullptr, 10 ), FALSE, FALSE );

  return money;
}

void SkirmishPreferences::setStartingCash( const Money & startingCash )
{
  AsciiString option;

  option.format( "%d", startingCash.countMoney() );

  (*this)[startingCashKey] = option;
}



Bool SkirmishPreferences::write()
{
	if (!TheSkirmishGameInfo)
		return FALSE;

	AsciiString tmp;

	tmp.format("%d", TheSkirmishGameInfo->getConstSlot(0)->getColor());
	(*this)["Color"] = tmp;

	tmp.format("%d", TheSkirmishGameInfo->getConstSlot(0)->getPlayerTemplate());
	(*this)["PlayerTemplate"] = tmp;

	(*this)["Map"] = TheSkirmishGameInfo->getMap();

	(*this)["UserName"] = UnicodeStringToQuotedPrintable(TheSkirmishGameInfo->getConstSlot(0)->getName());

  setStartingCash( TheSkirmishGameInfo->getStartingCash() );
  setSuperweaponRestricted( TheSkirmishGameInfo->getSuperweaponRestriction() );

	setSlotList();

	// FPS now comes from the ComboBoxGameSpeed item data (fps preset from
	// the s_skirmishGameSpeedFpsValues table). Fall back to the GameInfo's
	// current FPS if the combo isn't available for any reason.
	Int maxFPS = TheSkirmishGameInfo ? TheSkirmishGameInfo->getGameFps() : 30;
	if (comboBoxGameSpeed)
	{
		Int selIndex = -1;
		GadgetComboBoxGetSelectedPos(comboBoxGameSpeed, &selIndex);
		if (selIndex >= 0)
		{
			const Int fps = static_cast<Int>(reinterpret_cast<uintptr_t>(
				GadgetComboBoxGetItemData(comboBoxGameSpeed, selIndex)));
			if (fps > 0) maxFPS = fps;
		}
	}
	setInt("FPS", maxFPS);

	return UserPreferences::write();
}

/*
static void playerTooltip(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	Int idx = -1;
	for (Int i=1; i<MAX_SLOTS; ++i)
	{
		if (window && window == GadgetComboBoxGetEditBox(comboBoxPlayer[i]))
		{
			idx = i;
			break;
		}
	}
	if (idx == -1)
		return;

	UnicodeString tooltip;
	/// @todo get this to display the correct tooltip for the player field CCB
	tooltip.format(TheGameText->fetch("TOOLTIP:LANPlayer") );
	TheMouse->setCursorTooltip( tooltip );
}
*/

// Legacy no-op. The game speed slider + its live readout have been
// replaced by ComboBoxGameSpeed, which displays the selected FPS in its
// own dropdown label. Kept as a stub so any stale callers (likely none
// after the slider removal) still link.
void setFPSTextBox( Int /*sliderPos*/ )
{
}

void reallyDoStart()
{
	if (TheGameLogic->isInGame())
		TheGameLogic->clearGameData(FALSE);

	// FPS selection now comes from the ComboBoxGameSpeed item data instead
	// of the old SliderGameSpeed track position. Legacy bounds preserved so
	// a malformed dropdown can't push the sim into an unplayable state.
	Int maxFPS = TheSkirmishGameInfo ? TheSkirmishGameInfo->getGameFps() : 30;
	if (comboBoxGameSpeed)
	{
		Int selIndex = -1;
		GadgetComboBoxGetSelectedPos(comboBoxGameSpeed, &selIndex);
		if (selIndex >= 0)
		{
			const Int fps = static_cast<Int>(reinterpret_cast<uintptr_t>(
				GadgetComboBoxGetItemData(comboBoxGameSpeed, selIndex)));
			if (fps > 0) maxFPS = fps;
		}
	}
	DEBUG_LOG(("Skirmish ComboBoxGameSpeed FPS = %d", maxFPS));
	if (maxFPS > GREATER_NO_FPS_LIMIT)
		maxFPS = 1000;
	if (maxFPS < 15)
		maxFPS = 15;

  TheWritableGlobalData->m_mapName = TheSkirmishGameInfo->getMap();
  TheSkirmishGameInfo->startGame(0);

		Bool isSkirmish = TRUE;
	const MapMetaData *md = TheMapCache->findMap(TheSkirmishGameInfo->getMap());
	if (md)
	{
		isSkirmish = md->m_isMultiplayer; // we can now select solo campaign maps in Skirmish.
	}

	if (isSkirmish)
	{
		InitRandom(TheSkirmishGameInfo->getSeed());

		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
		msg->appendIntegerArgument(GAME_SKIRMISH);
		msg->appendIntegerArgument(DIFFICULTY_NORMAL);	// not really used; just specified so we can add the game speed last
		msg->appendIntegerArgument(0);									// not really used; just specified so we can add the game speed last
		msg->appendIntegerArgument(maxFPS);							// FPS limit
	}
	else
	{
		InitRandom(0);

		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
		msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
		msg->appendIntegerArgument(DIFFICULTY_NORMAL);	// not really used; just specified so we can add the game speed last
		msg->appendIntegerArgument(0);									// not really used; just specified so we can add the game speed last
		msg->appendIntegerArgument(maxFPS);							// FPS limit
	}
}

Bool sandboxOk = FALSE;
static void startPressed()
{

	BOOL isReady = FALSE;
	Int playerCount = TheSkirmishGameInfo->getNumPlayers();
	AsciiString lowerMap = TheSkirmishGameInfo->getMap();
	lowerMap.toLower();
	std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
	if (it == TheMapCache->end())
	{
		buttonPushed = FALSE;
		MessageBoxOk(TheGameText->fetch("GUI:ErrorStartingGame"), TheGameText->fetch("GUI:CantFindMap"), nullptr);
	}
	MapMetaData mmd = it->second;
	if(playerCount > mmd.m_numPlayers)
	{
		buttonPushed = FALSE;
		UnicodeString msg;
		msg.format(TheGameText->fetch("GUI:TooManyPlayers"), mmd.m_numPlayers);
		MessageBoxOk(TheGameText->fetch("GUI:ErrorStartingGame"), msg, nullptr);
	}
	/*
  else if (playerCount < 2 && !sandboxOk)
  {
		sandboxOk = TRUE;
		MessageBoxOk(TheGameText->fetch("GUI:ErrorStartingGame"), TheGameText->fetch("GUI:SandboxWarning"), nullptr);
  }
	*/
	else
	{
		isReady = TRUE;
	}

	if(isReady)
	{
		reallyDoStart();
	}

}

/////////////////////////////////////////////////////
// MapSelectorTooltip - shows tooltips for the tech buildings
//											and supply depots
void MapSelectorTooltip(GameWindow *window,
												WinInstanceData *instData,
												UnsignedInt mouse)
{
	Int x, y;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);

	Int pixelX, pixelY;
	window->winGetScreenPosition(&pixelX, &pixelY);

	const Image *image = TheMappedImageCollection->findImageByName("TecBuilding");
	const Image *image2 = TheMappedImageCollection->findImageByName("Cash");


	ICoord2DList::iterator it = TheSupplyAndTechImageLocations.m_techPosList.begin();
	ICoord2DList::iterator it2 = TheSupplyAndTechImageLocations.m_supplyPosList.begin();

	if( image )
	{
		// Check to see if we mouse over a tech building
		while(it != TheSupplyAndTechImageLocations.m_techPosList.end())
		{
			if ((x > (pixelX + it->x) && x < (pixelX + it->x + SUPPLY_TECH_SIZE))
				  && ( y > (pixelY + it->y) && y < (pixelY + it->y + SUPPLY_TECH_SIZE)))
			{
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:TechBuilding"), -1, nullptr); //, 1.5f
				return;
			}
			it++;
		}
	}

	if (image2)
	{
		// Check to see if we mouse over a supply dock
		while (it2 != TheSupplyAndTechImageLocations.m_supplyPosList.end())
		{
			if ((x > (pixelX + it2->x) && x < (pixelX + it2->x + SUPPLY_TECH_SIZE))
					 && ( y > (pixelY + it2->y) && y < (pixelY + it2->y + SUPPLY_TECH_SIZE)))
			{
				TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:SupplyDock"), -1, nullptr); // , 1.5f
				break;
			}
			it2++;
		}
	}

}


void positionStartSpotControls( GameWindow *win, GameWindow *mapWindow, Coord3D *pos, MapMetaData *mmd, GameWindow *buttonMapStartPositions[])
{
	if(!win || !mmd || !mapWindow || !buttonMapStartPositions)
		return;
	DEBUG_ASSERTCRASH(win && mmd,("positionStartSpotControls:: we don't have a window to position or any mapmetadata"));
	ICoord2D winMapSize, winMapPos, gadgetPos, gadgetSize;
	mapWindow->winGetSize(&winMapSize.x, &winMapSize.y);
	mapWindow->winGetScreenPosition(&winMapPos.x, &winMapPos.y);
	win->winGetSize(&gadgetSize.x, &gadgetSize.y);
	ICoord2D ul, lr;
	findDrawPositions(0,0, winMapSize.x, winMapSize.y,mmd->m_extent, &ul, &lr);
	Int smallWidth = lr.x - ul.x;
	Int smallHeight= lr.y - ul.y;

	// When we actually draw the map, save off it's screen position and use that instead of the map window's position/size
	Real position;
	position = (pos->x - mmd->m_extent.lo.x) / (mmd->m_extent.hi.x - mmd->m_extent.lo.x);
	gadgetPos.x = (position * smallWidth) - gadgetSize.x /2 + ul.x;// + winMapPos.x;

	position = (pos->y - mmd->m_extent.lo.y) / (mmd->m_extent.hi.y - mmd->m_extent.lo.y);
	gadgetPos.y = ((1- position) * smallHeight) - gadgetSize.y /2 + ul.y;// + winMapPos.y;



	// loop through and make sure we're not on top of anyone else
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		if(buttonMapStartPositions[i] == win)
			break;
		ICoord2D tempPos;
		buttonMapStartPositions[i]->winGetScreenPosition(&tempPos.x, &tempPos.y);
		// we're inside the other gadget
		if(gadgetPos.x > tempPos.x && gadgetPos.x < tempPos.x + gadgetSize.x
				&& gadgetPos.y > tempPos.y && gadgetPos.y < tempPos.y + gadgetSize.y)
		{
			Int closerRight = tempPos.x + gadgetSize.x - gadgetPos.x;
			Int closerBottom = tempPos.y + gadgetSize.y - gadgetPos.y;
			// we're closer to the right then the bottom
			if( closerRight < closerBottom)
				gadgetPos.x = tempPos.x + gadgetSize.x + 1;
			else if( closerBottom < closerRight)
				gadgetPos.y = tempPos.y + gadgetSize.y + 1;
			else
			{
				gadgetPos.x = tempPos.x + gadgetSize.x + 1;
				gadgetPos.y = tempPos.y + gadgetSize.y + 1;
			}
		}
	}
	win->winSetPosition(gadgetPos.x,gadgetPos.y);
}
TechAndSupplyImages TheSupplyAndTechImageLocations;

void positionAdditionalImages( MapMetaData *mmd, GameWindow *mapWindow, Bool force)
{
	TheSupplyAndTechImageLocations.m_supplyPosList.clear();
	TheSupplyAndTechImageLocations.m_techPosList.clear();

	if( !mmd || !mapWindow || mapWindow->winIsHidden())
		return;
	static MapMetaData *prevMMD = nullptr;
	if(force)
		prevMMD = nullptr;
	// we already populated the supply and tech image locations.
	if(mmd == prevMMD)
		return;
	ICoord2D winMapSize, winMapPos;
	mapWindow->winGetSize(&winMapSize.x, &winMapSize.y);
	mapWindow->winGetScreenPosition(&winMapPos.x, &winMapPos.y);

	//SUPPLY_TECH_SIZE
	ICoord2D ul, lr;
	findDrawPositions(0,0, winMapSize.x, winMapSize.y,mmd->m_extent, &ul, &lr);
	Int smallWidth = lr.x - ul.x;
	Int smallHeight= lr.y - ul.y;

	Coord3DList::iterator it = mmd->m_supplyPositions.begin();
		// loop through and make sure we're not on top of anyone else
	while( it != mmd->m_supplyPositions.end())
	{

		ICoord2D markerPos;

		// When we actually draw the map, save off it's screen position and use that instead of the map window's position/size
		Real position;
		position = (it->x - mmd->m_extent.lo.x) / (mmd->m_extent.hi.x - mmd->m_extent.lo.x);
		markerPos.x = (position * smallWidth) - SUPPLY_TECH_SIZE /2 + ul.x;// + winMapPos.x;

		position = (it->y - mmd->m_extent.lo.y) / (mmd->m_extent.hi.y - mmd->m_extent.lo.y);
		markerPos.y = ((1- position) * smallHeight) - SUPPLY_TECH_SIZE /2 + ul.y;// + winMapPos.y;
		TheSupplyAndTechImageLocations.m_supplyPosList.push_front(markerPos);
		it++;
	}

	it = mmd->m_techPositions.begin();
		// loop through and make sure we're not on top of anyone else
	while( it != mmd->m_techPositions.end())
	{

		ICoord2D markerPos;
		// When we actually draw the map, save off it's screen position and use that instead of the map window's position/size
		Real position;
		position = (it->x - mmd->m_extent.lo.x) / (mmd->m_extent.hi.x - mmd->m_extent.lo.x);
		markerPos.x = (position * smallWidth) - SUPPLY_TECH_SIZE /2 + ul.x;// + winMapPos.x;

		position = (it->y - mmd->m_extent.lo.y) / (mmd->m_extent.hi.y - mmd->m_extent.lo.y);
		markerPos.y = ((1- position) * smallHeight) - SUPPLY_TECH_SIZE /2 + ul.y;// + winMapPos.y;
		TheSupplyAndTechImageLocations.m_techPosList.push_front(markerPos);
		it++;
	}


	//TheSupplyAndTechImageLocations
}

void positionStartSpots( AsciiString mapName, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow)
{
	AsciiString lowerMap = mapName;
	lowerMap.toLower();
	std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
	if (it == TheMapCache->end())
	{
		mapWindow->winSetUserData(nullptr);

		static const Image *unknownImage = nullptr;
		if (!unknownImage)
			unknownImage = TheMappedImageCollection->findImageByName("UnknownMap");
		if (unknownImage)
		{
			mapWindow->winSetStatus(WIN_STATUS_IMAGE);
			mapWindow->winSetEnabledImage(0, unknownImage);
		}
		else
		{
			mapWindow->winClearStatus(WIN_STATUS_IMAGE);
		}

		positionAdditionalImages(nullptr, mapWindow, TRUE);
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			if (buttonMapStartPositions[i] != nullptr)
			{
				buttonMapStartPositions[i]->winHide(TRUE);
			}
		}
	}
	else
	{
		MapMetaData mmd = it->second;

		Image *image = getMapPreviewImage(mapName);
		if (mapWindow != nullptr) {
			mapWindow->winSetUserData((void *)TheMapCache->findMap(mapName));
			if(image)
			{
				mapWindow->winSetStatus(WIN_STATUS_IMAGE);
				mapWindow->winSetEnabledImage(0, image);
			}
			else
			{
				static const Image *unknownImage = nullptr;
				if (!unknownImage)
					unknownImage = TheMappedImageCollection->findImageByName("UnknownMap");
				if (unknownImage)
				{
					mapWindow->winSetStatus(WIN_STATUS_IMAGE);
					mapWindow->winSetEnabledImage(0, unknownImage);
				}
				else
				{
					mapWindow->winClearStatus(WIN_STATUS_IMAGE);
				}
			}
		}

		positionAdditionalImages(&mmd, mapWindow, TRUE);

		AsciiString waypointName;
		Int i = 0;
		for(; i < mmd.m_numPlayers && mmd.m_isMultiplayer; ++i )
		{
			waypointName.format("Player_%d_Start", i+1); // start pos waypoints are 1-based
			WaypointMap::iterator wmIt = mmd.m_waypoints.find(waypointName);
			if( wmIt != mmd.m_waypoints.end())
			{
				Coord3D *pos = &wmIt->second;
				positionStartSpotControls(buttonMapStartPositions[i], mapWindow,pos, &mmd, buttonMapStartPositions);
				if (buttonMapStartPositions[i] != nullptr)
				{
					buttonMapStartPositions[i]->winHide(FALSE);
				}
			}
			else
			{
				DEBUG_CRASH(("positionStartSpots:: someone messed with the map cash.  We couldn't find waypoint <%s> in map <%s>", waypointName.str(),lowerMap.str()));
			}
		}
		// hide the rest
		for (; i < MAX_SLOTS; ++i)
		{
			if (buttonMapStartPositions[i] != nullptr)
			{
				buttonMapStartPositions[i]->winHide(TRUE);
			}
		}
	}
}

void positionStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow)
{
	AsciiString localMapFname = myGame->getMap();
	if (!myGame->isGameInProgress())
	{
		Int localIdx = myGame->getLocalSlotNum();
		if (localIdx == -1)
			localMapFname = AsciiString::TheEmptyString;
		else
		{
			if (!myGame->getConstSlot(localIdx)->hasMap())
			{
				localMapFname = AsciiString::TheEmptyString;
			}
		}
	}
	positionStartSpots(localMapFname, buttonMapStartPositions, mapWindow);
}

void updateMapStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], Bool onLoadScreen )
{
	AsciiString lowerMap = myGame->getMap();
	lowerMap.toLower();
	std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
	if (it == TheMapCache->end())
	{
		for (Int i = 0; i < MAX_SLOTS; ++i)
    {
      if ( buttonMapStartPositions[i] != nullptr )
      {
  			buttonMapStartPositions[i]->winHide(TRUE);
      }
    }
		return;
	}
	MapMetaData mmd = it->second;

	Int i = 0;
	for(; i < MAX_SLOTS; ++i)
	{
    if ( buttonMapStartPositions[i] != nullptr )
    {
		  GadgetButtonSetText(buttonMapStartPositions[i], UnicodeString::TheEmptyString);
		  if (!onLoadScreen)
		  {
			  buttonMapStartPositions[i]->winSetTooltip(TheGameText->fetch("TOOLTIP:StartPosition"));
		  }
    }
	}
	for( i = 0; i < MAX_SLOTS; ++i)
	{
    if ( buttonMapStartPositions[i] == nullptr )
      continue;

		GameSlot *gs =myGame->getSlot(i);
		if(onLoadScreen)
		{
			if(gs->getApparentStartPos() >=0 && gs->getApparentStartPos() < mmd.m_numPlayers && gs->getPlayerTemplate() > PLAYERTEMPLATE_MIN )
			{
				AsciiString displayNumber;
				displayNumber.format("NUMBER:%d",i + 1);
				GadgetButtonSetText(buttonMapStartPositions[gs->getApparentStartPos()], TheGameText->fetch(displayNumber));
			}
		}
		else
		{
			if(gs->getStartPos() >=0 && gs->getStartPos() < mmd.m_numPlayers && gs->getPlayerTemplate() > PLAYERTEMPLATE_MIN )
			{
				AsciiString displayNumber;
				displayNumber.format("NUMBER:%d",i + 1);
				GadgetButtonSetText(buttonMapStartPositions[gs->getStartPos()], TheGameText->fetch(displayNumber));
				//added start position tooltip
				//Fixed again to show the right number , ie "i + 1"
				UnicodeString temp;
				temp.format(TheGameText->fetch("TOOLTIP:StartPositionN"), i + 1);
				buttonMapStartPositions[gs->getStartPos()]->winSetTooltip(temp);
			}
		}
	}
}

static void handlePlayerSelection(int index)
{
  if( index == 0 || index >=MAX_SLOTS)
    return;

	GameWindow *combo = comboBoxPlayer[index];
	Int playerType, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
  UnicodeString title = GadgetComboBoxGetText(combo);
	playerType = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(combo, selIndex)));
	GameInfo *myGame = TheSkirmishGameInfo;

	if (myGame)
	{
		GameSlot * slot = myGame->getSlot(index);
    if(!slot)
      return;
    slot->setState(SlotState(playerType), title);

	}
  //skirmishUpdateSlotList();
}

static void handleColorSelection(int index)
{
	GameWindow *combo = comboBoxColor[index];
	Int color, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	color = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(combo, selIndex)));

	GameInfo *myGame = TheSkirmishGameInfo;

	if (myGame)
	{
		GameSlot * slot = myGame->getSlot(index);
    if(!slot)
      return;
		if (color == slot->getColor())
			return;

		// `color` is the combo box item data, raw 0x00RRGGBB int now
		// (-1 = random). The old `< getNumColors()` palette-index
		// check silently rejected every real RGB pick — see
		// LanGameOptionsMenu::handleColorSelection for the same fix.
		if (color != -1)
		{
			Bool colorAvailable = TRUE;
			for(Int i=0; i <MAX_SLOTS; i++)
			{
				GameSlot *checkSlot = myGame->getSlot(i);
				if(checkSlot && color == checkSlot->getColor() && slot != checkSlot)
				{
					colorAvailable = FALSE;
					break;
				}
			}
			if(!colorAvailable)
				return;
		}
    slot->setColor(color);
  }
  //skirmishUpdateSlotList();
}

static void handlePlayerTemplateSelection(int index)
{
	GameWindow *combo = comboBoxPlayerTemplate[index];
	Int playerTemplate, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	playerTemplate = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(combo, selIndex)));
	GameInfo *myGame = TheSkirmishGameInfo;

	if (myGame)
	{
		GameSlot * slot = myGame->getSlot(index);
    if(!slot)
      return;
		if (playerTemplate == slot->getPlayerTemplate())
			return;

		slot->setPlayerTemplate(playerTemplate);
	}
  //skirmishUpdateSlotList();
}

static void handleStartPositionSelection(int index, Int position)
{

	GameInfo *myGame = TheSkirmishGameInfo;

	if (myGame)
	{
		GameSlot * slot = myGame->getSlot(index);
		if (position == slot->getStartPos())
			return;
		if( position < 0)
		{
			slot->setStartPos(position);
			return;
		}

		Bool isAvailable = TRUE;
		for(Int i = 0; i < MAX_SLOTS; ++i)
		{
			if(i != index && myGame->getSlot(i)->getStartPos() == position)
			{
				isAvailable = FALSE;
				break;
			}
		}
		if(isAvailable)
		{
			slot->setStartPos(position);
		}
	}
}

static void handleTeamSelection(int index)
{
	GameWindow *combo = comboBoxTeam[index];
	Int team, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	team = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(combo, selIndex)));
	GameInfo *myGame = TheSkirmishGameInfo;

	if (myGame)
	{
		GameSlot * slot = myGame->getSlot(index);
    if(!slot)
      return;
		if (team == slot->getTeamNumber())
			return;

		slot->setTeamNumber(team);
	}
  //skirmishUpdateSlotList();
}

static void handleStartingCashSelection()
{
  GameInfo *myGame = TheSkirmishGameInfo;

  if (myGame)
  {
    Int selIndex;
    GadgetComboBoxGetSelectedPos(comboBoxStartingCash, &selIndex);

    Money startingCash;
    startingCash.deposit( static_cast<UnsignedInt>(reinterpret_cast<uintptr_t>(GadgetComboBoxGetItemData( comboBoxStartingCash, selIndex ))), FALSE, FALSE );
    myGame->setStartingCash( startingCash );
  }
}

// Preset values mirrored from LanGameOptionsMenu so host-side lobbies
// feel identical across Skirmish and LAN. Counts are interpreted per-TEAM.
// Value 0 means "superweapons disabled" (NOT unlimited); see
// Player::canBuildMoreOfType's DeterminedBySuperweaponRestriction branch.
static const Int s_skirmishSuperweaponLimitValues[] = { 0, 1, 2, 3, 4, 5, 10, 50 };
static const Int s_skirmishSuperweaponLimitCount    =
    sizeof(s_skirmishSuperweaponLimitValues) / sizeof(s_skirmishSuperweaponLimitValues[0]);

static void PopulateSkirmishSuperweaponLimitComboBox(GameWindow *comboBox, GameInfo *myGame)
{
  GadgetComboBoxReset(comboBox);
  if (!comboBox || !myGame) return;

  const UnsignedShort currentLimit = myGame->getSuperweaponRestriction();
  Int currentIdx = -1;

  for (Int i = 0; i < s_skirmishSuperweaponLimitCount; ++i)
  {
    const Int value = s_skirmishSuperweaponLimitValues[i];
    UnicodeString label;
    if (value == 0)
      label = L"Off";
    else
      label.format(L"%d", value);
    Int newIdx = GadgetComboBoxAddEntry(comboBox, label,
      comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor());
    GadgetComboBoxSetItemData(comboBox, newIdx, reinterpret_cast<void *>(static_cast<uintptr_t>(value)));
    if (value == (Int)currentLimit)
      currentIdx = newIdx;
  }

  if (currentIdx == -1)
  {
    UnicodeString label;
    label.format(L"%d", (Int)currentLimit);
    currentIdx = GadgetComboBoxAddEntry(comboBox, label,
      comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor());
    GadgetComboBoxSetItemData(comboBox, currentIdx, reinterpret_cast<void *>(static_cast<uintptr_t>(currentLimit)));
  }

  GadgetComboBoxSetSelectedPos(comboBox, currentIdx);
}

// FPS presets match LAN lobby exactly. 30 is the campaign/shell baseline;
// higher values run the simulation hotter for snappier play.
static const Int s_skirmishGameSpeedFpsValues[] = { 30, 50, 60, 70, 90, 120, 150 };
static const Int s_skirmishGameSpeedFpsCount    =
    sizeof(s_skirmishGameSpeedFpsValues) / sizeof(s_skirmishGameSpeedFpsValues[0]);

static void PopulateSkirmishGameSpeedComboBox(GameWindow *comboBox, GameInfo *myGame)
{
  GadgetComboBoxReset(comboBox);
  if (!comboBox || !myGame) return;

  const Int currentFps = myGame->getGameFps();
  Int currentIdx = -1;

  for (Int i = 0; i < s_skirmishGameSpeedFpsCount; ++i)
  {
    const Int fps = s_skirmishGameSpeedFpsValues[i];
    UnicodeString label;
    label.format(L"%d hz", fps);
    Int newIdx = GadgetComboBoxAddEntry(comboBox, label,
      comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor());
    GadgetComboBoxSetItemData(comboBox, newIdx, reinterpret_cast<void *>(static_cast<uintptr_t>(fps)));
    if (fps == currentFps)
      currentIdx = newIdx;
  }

  if (currentIdx == -1)
  {
    UnicodeString label;
    label.format(L"%d hz", currentFps);
    currentIdx = GadgetComboBoxAddEntry(comboBox, label,
      comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor());
    GadgetComboBoxSetItemData(comboBox, currentIdx, reinterpret_cast<void *>(static_cast<uintptr_t>(currentFps)));
  }

  GadgetComboBoxSetSelectedPos(comboBox, currentIdx);
}

static void handleSuperweaponLimitSelection()
{
  GameInfo *myGame = TheSkirmishGameInfo;
  if (!myGame || !comboBoxSuperweaponLimit) return;

  Int selIndex = -1;
  GadgetComboBoxGetSelectedPos(comboBoxSuperweaponLimit, &selIndex);
  if (selIndex < 0) return;

  const Int newLimit = static_cast<Int>(reinterpret_cast<uintptr_t>(
    GadgetComboBoxGetItemData(comboBoxSuperweaponLimit, selIndex)));
  // 0 is a legal choice: it means "superweapons disabled". Reject only
  // values that could only come from a malformed item-data payload.
  if (newLimit < 0) return;

  myGame->setSuperweaponRestriction(static_cast<UnsignedShort>(newLimit));
}

static void handleGameSpeedSelection()
{
  GameInfo *myGame = TheSkirmishGameInfo;
  if (!myGame || !comboBoxGameSpeed) return;

  Int selIndex = -1;
  GadgetComboBoxGetSelectedPos(comboBoxGameSpeed, &selIndex);
  if (selIndex < 0) return;

  const Int newFps = static_cast<Int>(reinterpret_cast<uintptr_t>(
    GadgetComboBoxGetItemData(comboBoxGameSpeed, selIndex)));
  if (newFps <= 0) return;

  myGame->setGameFps(newFps);
}


//-------------------------------------------------------------------------------------------------
/** Initialize the Gadgets Options Menu */
//-------------------------------------------------------------------------------------------------
void InitSkirmishGameGadgets()
{
	//Initialize the gadget IDs
	parentSkirmishGameOptionsID = TheNameKeyGenerator->nameToKey( "SkirmishGameOptionsMenu.wnd:SkirmishGameOptionsMenuParent" );
	buttonExitID = TheNameKeyGenerator->nameToKey( "SkirmishGameOptionsMenu.wnd:ButtonBack" );
	buttonStartID = TheNameKeyGenerator->nameToKey( "SkirmishGameOptionsMenu.wnd:ButtonStart" );
	textEntryMapDisplayID = TheNameKeyGenerator->nameToKey( "SkirmishGameOptionsMenu.wnd:TextEntryMapDisplay" );
	buttonSelectMapID = TheNameKeyGenerator->nameToKey( "SkirmishGameOptionsMenu.wnd:ButtonSelectMap" );
	buttonResetID = TheNameKeyGenerator->nameToKey( "SkirmishGameOptionsMenu.wnd:ButtonReset" );
	windowMapID = TheNameKeyGenerator->nameToKey( "SkirmishGameOptionsMenu.wnd:MapWindow" );
	staticTextGameSpeedID = TheNameKeyGenerator->nameToKey( "SkirmishGameOptionsMenu.wnd:StaticTextGameSpeed" );
  comboBoxSuperweaponLimitID = TheNameKeyGenerator->nameToKey( "SkirmishGameOptionsMenu.wnd:ComboBoxSuperweaponLimit" );
  comboBoxGameSpeedID        = TheNameKeyGenerator->nameToKey( "SkirmishGameOptionsMenu.wnd:ComboBoxGameSpeed" );
  comboBoxStartingCashID     = TheNameKeyGenerator->nameToKey( "SkirmishGameOptionsMenu.wnd:ComboBoxStartingCash" );

	// Initialize the pointers to our gadgets
	parentSkirmishGameOptions = TheWindowManager->winGetWindowFromId( nullptr, parentSkirmishGameOptionsID );
	DEBUG_ASSERTCRASH(parentSkirmishGameOptions, ("Could not find the parentSkirmishGameOptions" ));
	buttonSelectMap = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions,buttonSelectMapID  );
	DEBUG_ASSERTCRASH(buttonSelectMap, ("Could not find the buttonSelectMap"));
	buttonStart = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions,buttonStartID  );
	DEBUG_ASSERTCRASH(buttonStart, ("Could not find the buttonStart"));
	buttonExit = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions,  buttonExitID);
	DEBUG_ASSERTCRASH(buttonExit, ("Could not find the buttonExit"));
	textEntryMapDisplay = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, textEntryMapDisplayID );
	DEBUG_ASSERTCRASH(textEntryMapDisplay, ("Could not find the textEntryMapDisplay"));
	buttonReset = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, buttonResetID );
	DEBUG_ASSERTCRASH(buttonReset, ("Could not find the buttonReset"));
	// StaticTextGameSpeed now labels the ComboBoxGameSpeed dropdown (sits to
	// its left, matching StartingMoney / LimitSuperweapons). The old
	// SliderGameSpeed + CheckboxLimitSuperweapons controls have been replaced
	// by ComboBoxGameSpeed + ComboBoxSuperweaponLimit.
	staticTextGameSpeed = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, staticTextGameSpeedID );
	if (staticTextGameSpeed)
		staticTextGameSpeed->winHide(FALSE);

	comboBoxSuperweaponLimit = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxSuperweaponLimitID );
	DEBUG_ASSERTCRASH(comboBoxSuperweaponLimit, ("Could not find ComboBoxSuperweaponLimit"));

	comboBoxGameSpeed = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxGameSpeedID );
	DEBUG_ASSERTCRASH(comboBoxGameSpeed, ("Could not find ComboBoxGameSpeed"));

	comboBoxStartingCash = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxStartingCashID );
	DEBUG_ASSERTCRASH(comboBoxStartingCash, ("Could not find the comboBoxStartingCash"));

	PopulateStartingCashComboBox(comboBoxStartingCash, TheSkirmishGameInfo);
	PopulateSkirmishSuperweaponLimitComboBox(comboBoxSuperweaponLimit, TheSkirmishGameInfo);
	PopulateSkirmishGameSpeedComboBox(comboBoxGameSpeed, TheSkirmishGameInfo);

	// Hide LAN-only widgets inherited from LanGameOptionsMenu.wnd. These
	// don't exist in Skirmish semantically (no remote peers, no per-slot
	// accept handshake, no lobby chat). winGetWindowFromId returns null
	// if the widget isn't present, and winHide is a no-op then — safe.
	{
		static const char* kLanOnlyWidgets[] = {
			"SkirmishGameOptionsMenu.wnd:ButtonEmote",
			"SkirmishGameOptionsMenu.wnd:TextEntryChat",
			"SkirmishGameOptionsMenu.wnd:ListboxChatWindowLanGame",
		};
		for (size_t i = 0; i < sizeof(kLanOnlyWidgets)/sizeof(kLanOnlyWidgets[0]); ++i)
		{
			NameKeyType id = TheNameKeyGenerator->nameToKey(kLanOnlyWidgets[i]);
			GameWindow* w = TheWindowManager->winGetWindowFromId(parentSkirmishGameOptions, id);
			if (w) w->winHide(TRUE);
		}
		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			AsciiString wname;
			wname.format("SkirmishGameOptionsMenu.wnd:ButtonAccept%d", i);
			NameKeyType id = TheNameKeyGenerator->nameToKey(wname);
			GameWindow* w = TheWindowManager->winGetWindowFromId(parentSkirmishGameOptions, id);
			if (w) w->winHide(TRUE);
		}
	}

	textEntryPlayerNameID = TheNameKeyGenerator->nameToKey( "SkirmishGameOptionsMenu.wnd:TextEntryPlayerName" );
  textEntryPlayerName = TheWindowManager->winGetWindowFromId( nullptr, textEntryPlayerNameID );
	DEBUG_ASSERTCRASH(textEntryPlayerName, ("Could not find the textEntryPlayerName" ));

	windowMap = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions,windowMapID  );
	DEBUG_ASSERTCRASH(windowMap, ("Could not find the SkirmishGameOptionsMenu.wnd:MapWindow" ));

	windowMap->winSetTooltipFunc(MapSelectorTooltip);

	Int i = 0;
	for (; i < MAX_SLOTS; i++)
	{
		AsciiString tmpString;
		tmpString.format("SkirmishGameOptionsMenu.wnd:ComboBoxPlayer%d", i);
		if(i != 0)
    {
      comboBoxPlayerID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		  comboBoxPlayer[i] = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxPlayerID[i] );
		  GadgetComboBoxReset(comboBoxPlayer[i]);
		  //GadgetComboBoxGetEditBox(comboBoxPlayer[i])->winSetTooltipFunc(playerTooltip);
    }
		Color white = GameMakeColor( 255, 255, 255, 255 );

		if( i == 0 )
		{
      GadgetTextEntrySetText(textEntryPlayerName, TheGameText->fetch("GUI:Player") );
			//GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch( "GUI:Start" ), white);
			//GadgetComboBoxSetSelectedPos(comboBoxPlayer[i],0);
		}
		else
		{
      GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:Open"),white);  // leave this first
      GadgetComboBoxSetItemData(comboBoxPlayer[i], 0, reinterpret_cast<void *>(static_cast<intptr_t>(SLOT_OPEN)));
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:Closed"),white);  // leave this first
      GadgetComboBoxSetItemData(comboBoxPlayer[i], 1, reinterpret_cast<void *>(static_cast<intptr_t>(SLOT_CLOSED)));
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:EasyAI"),white);
      GadgetComboBoxSetItemData(comboBoxPlayer[i], 2, reinterpret_cast<void *>(static_cast<intptr_t>(SLOT_EASY_AI)));
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:MediumAI"),white);
      GadgetComboBoxSetItemData(comboBoxPlayer[i], 3, reinterpret_cast<void *>(static_cast<intptr_t>(SLOT_MED_AI)));
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:HardAI"),white);
      GadgetComboBoxSetItemData(comboBoxPlayer[i], 4, reinterpret_cast<void *>(static_cast<intptr_t>(SLOT_BRUTAL_AI)));
			GadgetComboBoxSetSelectedPos(comboBoxPlayer[i],0);

		}

		tmpString.format("SkirmishGameOptionsMenu.wnd:ComboBoxColor%d", i);
		comboBoxColorID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxColor[i] = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxColorID[i] );
		DEBUG_ASSERTCRASH(comboBoxColor[i], ("Could not find the comboBoxColor[%d]",i ));

		tmpString.format("SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate%d", i);
		comboBoxPlayerTemplateID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxPlayerTemplate[i] = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxPlayerTemplateID[i] );
		DEBUG_ASSERTCRASH(comboBoxPlayerTemplate[i], ("Could not find the comboBoxPlayerTemplate[%d]",i ));

		// add tooltips to the player template combobox and listbox
		comboBoxPlayerTemplate[i]->winSetTooltipFunc(playerTemplateComboBoxTooltip);
		GadgetComboBoxGetListBox(comboBoxPlayerTemplate[i])->winSetTooltipFunc(playerTemplateListBoxTooltip);

		tmpString.format("SkirmishGameOptionsMenu.wnd:ComboBoxTeam%d", i);
		comboBoxTeamID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxTeam[i] = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, comboBoxTeamID[i] );
		DEBUG_ASSERTCRASH(comboBoxTeam[i], ("Could not find the comboBoxTeam[%d]",i ));


//		tmpString.format("SkirmishGameOptionsMenu.wnd:ButtonStartPosition%d", i);
//		buttonStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
//		buttonStartPosition[i] = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, buttonStartPositionID[i] );
//		DEBUG_ASSERTCRASH(buttonStartPosition[i], ("Could not find the ButtonStartPosition[%d]",i ));
//
		tmpString.format("SkirmishGameOptionsMenu.wnd:ButtonMapStartPosition%d", i);
		buttonMapStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, buttonMapStartPositionID[i] );
		DEBUG_ASSERTCRASH(buttonMapStartPosition[i], ("Could not find the ButtonMapStartPosition[%d]",i ));
	}

	for (i = 0; i < MAX_SLOTS; ++i)
	{
		PopulateColorComboBox(i, comboBoxColor, TheSkirmishGameInfo );
		GadgetComboBoxSetSelectedPos(comboBoxColor[i], 0);
		PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, TheSkirmishGameInfo, FALSE );
		PopulateTeamComboBox(i, comboBoxTeam, TheSkirmishGameInfo );

//		if (buttonStartPosition[i])
//			buttonStartPosition[i]->winHide(TRUE); // not using these right now
	}

	populateSkirmishBattleHonors();
}

void skirmishUpdateSlotList()
{
  if(doUpdateSlotList)
  {
    doUpdateSlotList = FALSE;

	  AsciiString lowerMap = TheSkirmishGameInfo->getMap();
	  lowerMap.toLower();
	  std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
	  if (it != TheMapCache->end())
	  {
      GadgetStaticTextSetText(textEntryMapDisplay, it->second.m_displayName);
    }
    GadgetTextEntrySetText( textEntryPlayerName, TheSkirmishGameInfo->getSlot(0)->getName() );
    UpdateSlotList( TheSkirmishGameInfo, comboBoxPlayer,
										comboBoxColor, comboBoxPlayerTemplate,
									  comboBoxTeam, nullptr, buttonStart, buttonMapStartPosition );
		updateMapStartSpots(TheSkirmishGameInfo, buttonMapStartPosition, FALSE);
    doUpdateSlotList = TRUE;
  }
}
void updateSkirmishGameOptions();
void skirmishPositionStartSpots()
{
	positionStartSpots( TheSkirmishGameInfo, buttonMapStartPosition, windowMap);

	updateSkirmishGameOptions();
}
//-------------------------------------------------------------------------------------------------
/** Init TextEntryMapDisplay */
//-------------------------------------------------------------------------------------------------
void initSkirmishGameOptions()
{

}

static const char *layoutFilename = "SkirmishGameOptionsMenu.wnd";
static const char *parentName = "SkirmishGameOptionsMenuParent";
static const char *gadgetsToHide[] =
{
	"TextEntryPlayerName",
	"StaticTextPlayers",
	"StaticTextColor",
	"StaticTextTeam",
	"StaticTextFaction",
	nullptr
};
static const char *perPlayerGadgetsToHide[] =
{
	"ComboBoxPlayer",
	"ComboBoxTeam",
	"ComboBoxColor",
	"ComboBoxPlayerTemplate",
	nullptr
};

//-------------------------------------------------------------------------------------------------
/** Update options on screen */
//-------------------------------------------------------------------------------------------------
void updateSkirmishGameOptions()
{
	Bool isSkirmish = TRUE;
	const MapMetaData *md = TheMapCache->findMap(TheSkirmishGameInfo->getMap());
	if (md)
	{
		isSkirmish = md->m_isMultiplayer; // we can now select solo campaign maps in Skirmish.
    GadgetStaticTextSetText(textEntryMapDisplay, md->m_displayName);
	}
	else
	{
		DEBUG_CRASH(("map not found, should not happen"));
		UnicodeString mapDisplay;
		mapDisplay.translate(AsciiString(TheSkirmishGameInfo->getMap().str()));
		GadgetStaticTextSetText(textEntryMapDisplay, mapDisplay);
	}
	if (isSkirmish)
	{
		ShowUnderlyingGUIElements(TRUE, layoutFilename, parentName, gadgetsToHide, perPlayerGadgetsToHide );
	}
	else
	{
		ShowUnderlyingGUIElements(FALSE, layoutFilename, parentName, gadgetsToHide, perPlayerGadgetsToHide );
		for (Int i=1; i<MAX_SLOTS; ++i)
		{
			GadgetComboBoxSetSelectedPos(comboBoxPlayer[i], 0);
		}
	}

  // Superweapon limit is now a ComboBox (per-team count). Re-sync the
  // dropdown to reflect whatever GameInfo says right now (e.g. after
  // loading skirmish prefs that set a specific numeric limit).
  if (comboBoxSuperweaponLimit)
    PopulateSkirmishSuperweaponLimitComboBox(comboBoxSuperweaponLimit, TheSkirmishGameInfo);
  Int itemCount = GadgetComboBoxGetLength(comboBoxStartingCash);
  Int index = 0;
  for ( ; index < itemCount; index++ )
  {
    Int value  = static_cast<Int>(reinterpret_cast<intptr_t>(GadgetComboBoxGetItemData(comboBoxStartingCash, index)));
    if ( value == TheSkirmishGameInfo->getStartingCash().countMoney() )
    {
      GadgetComboBoxSetSelectedPos(comboBoxStartingCash, index, TRUE);
      break;
    }
  }

  DEBUG_ASSERTCRASH( index < itemCount, ("Could not find new starting cash amount %d in list", TheSkirmishGameInfo->getStartingCash().countMoney() ) );
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Skirmish Game Options Menu */
//-------------------------------------------------------------------------------------------------
void SkirmishGameOptionsMenuInit( WindowLayout *layout, void *userData )
{
	if (TheGameEngine->getQuitting())
		return;

	stillNeedsToSetOptions = FALSE;

	// Legacy SliderGameSpeed widget name — kept only so winGetWindowFromId
	// can silently return null if some old WND asset still has it. The
	// new game-speed UI is ComboBoxGameSpeed (see InitSkirmishGameGadgets).
	sliderGameSpeedID = TheNameKeyGenerator->nameToKey( "SkirmishGameOptionsMenu.wnd:SliderGameSpeed" );

	sandboxOk = FALSE;
  doUpdateSlotList = FALSE;
  if( !TheSkirmishGameInfo )
	{
    TheSkirmishGameInfo = NEW SkirmishGameInfo;
		SignalUIInteraction(SHELL_SCRIPT_HOOK_SKIRMISH_OPENED);
	}
  else if( TheSkirmishGameInfo->isInGame() )
	{
    TheSkirmishGameInfo->endGame();
	}
	else
	{
		SignalUIInteraction(SHELL_SCRIPT_HOOK_SKIRMISH_OPENED);
	}
  TheSkirmishGameInfo->init();
  TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
  Int localIP = TheSkirmishGameInfo->getSlot(0)->getIP();
	TheSkirmishGameInfo->setLocalIP(localIP);

  TheSkirmishGameInfo->enterGame();

  EnableSlotListUpdates(FALSE);
	//initialize the gadgets
	InitSkirmishGameGadgets();
  EnableSlotListUpdates(TRUE);

  SkirmishPreferences prefs;
  GameSlot gSlot;
  gSlot.setName(prefs.getUserName());
  gSlot.setState( SLOT_PLAYER, prefs.getUserName() );
  gSlot.setColor(prefs.getPreferredColor());
  gSlot.setPlayerTemplate(prefs.getPreferredFaction());
  TheSkirmishGameInfo->setSlot(0,gSlot);

	SkirmishBattleHonors honors;
	if (honors.getWins() > 10)
		gSlot.setState(SLOT_BRUTAL_AI);
	else if (honors.getWins() > 5)
		gSlot.setState(SLOT_MED_AI);
	else
		gSlot.setState(SLOT_EASY_AI);
	TheSkirmishGameInfo->setSlot(1, gSlot);

	ParseAsciiStringToGameInfo(TheSkirmishGameInfo, prefs.getSlotList());
	TheSkirmishGameInfo->setSeed(GetTickCount());

	UnsignedInt isPreorder = 0;
	GetUnsignedIntFromRegistry("", "Preorder", isPreorder);
	if (isPreorder != 0)
	{
		TheSkirmishGameInfo->markPlayerAsPreorder(0);
	}

  TheSkirmishGameInfo->setStartingCash( prefs.getStartingCash() );
  TheSkirmishGameInfo->setSuperweaponRestriction( prefs.getSuperweaponRestricted() );

  TheSkirmishGameInfo->setMap(prefs.getPreferredMap());
	const MapMetaData *md = TheMapCache->findMap(TheSkirmishGameInfo->getMap());
	if (!md)
	{
		TheSkirmishGameInfo->setMapCRC(0);
		TheSkirmishGameInfo->setMapSize(0);
	}
	else
	{
		TheSkirmishGameInfo->setMapCRC(md->m_CRC);
		TheSkirmishGameInfo->setMapSize(md->m_filesize);
	}

	AsciiString lowerMap = prefs.getPreferredMap();
	lowerMap.toLower();
	std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
	if (it != TheMapCache->end())
	{
    GadgetStaticTextSetText(textEntryMapDisplay, it->second.m_displayName);
  }

	skirmishPositionStartSpots();
	//updateSkirmishGameOptions();
	//initSkirmishGameOptions();

	// Restore the preferred game speed into GameInfo and reflect it in the
	// ComboBoxGameSpeed dropdown. The slider that used to live here has
	// been retired in favour of the LAN-style combobox.
	{
		const Int savedFps = max(15, min(150, prefs.getInt("FPS", TheGlobalData->m_framesPerSecondLimit)));
		if (TheSkirmishGameInfo)
			TheSkirmishGameInfo->setGameFps(savedFps);
		if (comboBoxGameSpeed)
			PopulateSkirmishGameSpeedComboBox(comboBoxGameSpeed, TheSkirmishGameInfo);
	}
	buttonStart->winSetText(TheGameText->fetch("GUI:Start"));
	/* hey, for now we're also going to disable the map select button until it doesn't crash */
	//buttonSelectMap->winEnable( FALSE );
	//buttonSelectMap->winEnable( TRUE );
	//updateSkirmishGameOptions();

	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		comboBoxColor[i]->winEnable(TRUE);
		comboBoxPlayerTemplate[i]->winEnable(TRUE);
		comboBoxTeam[i]->winEnable(TRUE);
	}

	// Show the Menu
	layout->hide( FALSE );

	// Set Keyboard to Main Parent
	TheWindowManager->winSetFocus( parentSkirmishGameOptions );

	// animate controls
  TheShell->showShellMap(TRUE);
//	TheShell->registerWithAnimateManager(buttonExit, WIN_ANIMATION_SLIDE_RIGHT, TRUE, 1);
  doUpdateSlotList = TRUE;
  skirmishUpdateSlotList();
	justEntered = TRUE;
	initialGadgetDelay = 2;
	GameWindow *win = TheWindowManager->winGetWindowFromId(nullptr, TheNameKeyGenerator->nameToKey("SkirmishGameOptionsMenu.wnd:SubParent"));
	if(win)
		win->winHide(TRUE);
	buttonPushed = FALSE;
}

//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{

	// hide the layout
	layout->hide( TRUE );
  TheShell->shutdownComplete( layout );

	// our shutdown is complete
	// what the munkees does this do?

	//TheShell->shutdownComplete( layout, (LANnextScreen != nullptr) );

	//if (LANnextScreen != nullptr)
	//{
	//	TheShell->push(LANnextScreen);
	//}

	//LANnextScreen = nullptr;

}

//-------------------------------------------------------------------------------------------------
/** Skirmish Game Options menu shutdown method */
//-------------------------------------------------------------------------------------------------
void SkirmishGameOptionsMenuShutdown( WindowLayout *layout, void *userData )
{
	SignalUIInteraction(SHELL_SCRIPT_HOOK_SKIRMISH_CLOSED);

	TheMouse->setCursor(Mouse::ARROW);
	TheMouse->setMouseText(UnicodeString::TheEmptyString,nullptr,nullptr);
  // if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}

	TheShell->reverseAnimatewindow();


	// hide menu
//	layout->hide( TRUE );

	// our shutdown is complete
	TheTransitionHandler->reverse("SkirmishGameOptionsMenuFade");

}

//-------------------------------------------------------------------------------------------------
/** Skirmish Game Options menu update method */
//-------------------------------------------------------------------------------------------------
void SkirmishGameOptionsMenuUpdate( WindowLayout * layout, void *userData)
{
	if (TheGameLogic->isInShellGame() && TheGameLogic->getFrame() == 1)
	{
		SignalUIInteraction(SHELL_SCRIPT_HOOK_SKIRMISH_ENTERED_FROM_GAME);
	}

	if(justEntered)
	{
		if(initialGadgetDelay == 1)
		{
			stillNeedsToSetOptions = TRUE;
			TheWindowManager->winSetFocus( parentSkirmishGameOptions );
			initialGadgetDelay = 2;
			justEntered = FALSE;
		}
		else
			initialGadgetDelay--;
	}

	// Coming back from a game, the shell map will be reloading,
	// in which case this transition isn't wanted until the load is done.
	if(stillNeedsToSetOptions && !TheGameLogic->isLoadingMap())
	{
		TheTransitionHandler->setGroup("SkirmishGameOptionsMenuFade");
		stillNeedsToSetOptions = FALSE;
	}

	if(TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
			TheShell->shutdownComplete( layout );
}

//-------------------------------------------------------------------------------------------------
/** Skirmish Game Options menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType SkirmishGameOptionsMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;

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
																							(WindowMsgData)buttonExit, buttonExitID );
					}
					// don't let key fall through anywhere else
					return MSG_HANDLED;
				}
			}
		}
		break;
	}
	return MSG_IGNORED;
}


//-------------------------------------------------------------------------------------------------
/** Skirmish Game Options menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType SkirmishGameOptionsMenuSystem( GameWindow *window, UnsignedInt msg,
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
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
        if ( controlID == comboBoxStartingCashID )
        {
          handleStartingCashSelection();
        }
        else if ( controlID == comboBoxSuperweaponLimitID )
        {
          handleSuperweaponLimitSelection();
        }
        else if ( controlID == comboBoxGameSpeedID )
        {
          handleGameSpeedSelection();
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
            else if (controlID == comboBoxPlayerID[i])
            {
              handlePlayerSelection(i);
            }
				  }
        }
				sandboxOk = FALSE;
        skirmishUpdateSlotList();
        break;
			}
		//-------------------------------------------------------------------------------------------------
		case GSM_SLIDER_TRACK:
		{
			// No sliders remain on this screen — the old SliderGameSpeed
			// was replaced by ComboBoxGameSpeed. Nothing to do.
			break;
		}
		//-------------------------------------------------------------------------------------------------
		case GBM_SELECTED:
			{

				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
///				static NameKeyType buttonResetFPSID = TheNameKeyGenerator->nameToKey( "SkirmishGameOptionsMenu.wnd:ButtonResetFPS" );
				if(buttonPushed)
					break;
				if ( controlID == buttonExitID )
				{
					buttonPushed = TRUE;
          SkirmishPreferences prefs;
          prefs.write();
					if( skirmishMapSelectLayout )
						{
							skirmishMapSelectLayout->destroyWindows();
							deleteInstance(skirmishMapSelectLayout);
							skirmishMapSelectLayout = nullptr;
						}
					TheShell->pop();
          delete TheSkirmishGameInfo;
          TheSkirmishGameInfo = nullptr;

				}
//				else if ( controlID == buttonResetFPSID )
//				{
//					static NameKeyType sliderGameSpeedID = TheNameKeyGenerator->nameToKey( "SkirmishGameOptionsMenu.wnd:SliderGameSpeed" );
//					GameWindow *sliderGameSpeed = TheWindowManager->winGetWindowFromId( parentSkirmishGameOptions, sliderGameSpeedID );
//					GadgetSliderSetPosition( sliderGameSpeed, TheGlobalData->m_framesPerSecondLimit );
//				}
				else if ( controlID == buttonSelectMapID )
				{
					sandboxOk = FALSE;
					//buttonBack->winEnable( false );
					skirmishMapSelectLayout = TheWindowManager->winCreateLayout( "Menus/SkirmishMapSelectMenu.wnd" );
					skirmishMapSelectLayout->runInit();
					skirmishMapSelectLayout->hide( FALSE );
					skirmishMapSelectLayout->bringForward();

				}
				else if ( controlID == buttonStartID )
				{
					buttonPushed = TRUE;
          SkirmishPreferences prefs;
          prefs.write();
					startPressed();
				}
				else if ( controlID == buttonResetID )
				{
					SkirmishBattleHonors stats;
					stats.clear();
					stats.write();
					populateSkirmishBattleHonors();
				}
        // Legacy CheckboxLimitSuperweapons click handler removed — the
        // superweapon limit is now a ComboBox routed via GCM_SELECTED.
				else
				{
					for (Int i = 0; i < MAX_SLOTS; i++)
					{
						if (controlID == buttonMapStartPositionID[i])
						{
							Int playerIdxInPos = -1;
							for (Int j=0; j<MAX_SLOTS; ++j)
							{
								GameSlot *slot = TheSkirmishGameInfo->getSlot(j);
								if (slot && slot->getStartPos() == i)
								{
									playerIdxInPos = j;
									break;
								}
							}
							if (playerIdxInPos >= 0)
							{
								GameSlot *slot = TheSkirmishGameInfo->getSlot(playerIdxInPos);
								if (playerIdxInPos == TheSkirmishGameInfo->getLocalSlotNum() || (TheSkirmishGameInfo->amIHost() && slot && slot->isAI()))
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
									nextPlayer = TheSkirmishGameInfo->getLocalSlotNum();
								handleStartPositionSelection(nextPlayer, i);
							}
							skirmishUpdateSlotList();
							sandboxOk = FALSE;
						}
					}
				}
				break;
			}
		//-------------------------------------------------------------------------------------------------
		case GBM_SELECTED_RIGHT:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
			for (Int i = 0; i < MAX_SLOTS; i++)
			{
				if (controlID == buttonMapStartPositionID[i])
				{
					Int playerIdxInPos = -1;
					for (Int j=0; j<MAX_SLOTS; ++j)
					{
						GameSlot *slot = TheSkirmishGameInfo->getSlot(j);
						if (slot && slot->getStartPos() == i)
						{
							playerIdxInPos = j;
							break;
						}
					}
					if (playerIdxInPos >= 0)
					{
						GameSlot *slot = TheSkirmishGameInfo->getSlot(playerIdxInPos);
						if (playerIdxInPos == TheSkirmishGameInfo->getLocalSlotNum() || (TheSkirmishGameInfo->amIHost() && slot && slot->isAI()))
						{
							// it's one of my type.  Remove it.
							handleStartPositionSelection(playerIdxInPos, -1);
						}
						skirmishUpdateSlotList();
						sandboxOk = FALSE;
					}
				}
			}
			break;
		}
		//-------------------------------------------------------------------------------------------------
	  case GEM_EDIT_DONE:
	  case GEM_UPDATE_TEXT:
      {
        GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
        if( controlID == textEntryPlayerNameID)
        {
          TheSkirmishGameInfo->getSlot(0)->setName(GadgetTextEntryGetText(textEntryPlayerName));
          //GadgetTextEntrySetText( textEntryPlayerName, TheSkirmishGameInfo->getSlot(0)->getName() );
        }
        break;
      }

		//-------------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

void populateSkirmishBattleHonors()
{
	GameWindow *list = TheWindowManager->winGetWindowFromId(nullptr, NAMEKEY("SkirmishGameOptionsMenu.wnd:ListboxInfo"));
	if (!list)
		return;

	SkirmishBattleHonors stats;

	list->winSetTooltipFunc(BattleHonorTooltip);
	GadgetListBoxReset( list );
	Int column = 0;
	Int row = 0;
	Int honors = stats.getHonors();

	UnicodeString uStr;
	GameWindow *streakWindow = TheWindowManager->winGetWindowFromId( nullptr, NAMEKEY("SkirmishGameOptionsMenu.wnd:StaticTextStreakValue") );
	if (streakWindow)
	{
		uStr.format(L"%d", stats.getWinStreak());
		GadgetStaticTextSetText(streakWindow, uStr);
	}
	GameWindow *bestStreakWindow = TheWindowManager->winGetWindowFromId( nullptr, NAMEKEY("SkirmishGameOptionsMenu.wnd:StaticTextBestStreakValue") );
	if (bestStreakWindow)
	{
		uStr.format(L"%d", stats.getBestWinStreak());
		GadgetStaticTextSetText(bestStreakWindow, uStr);
	}
	GameWindow *winsWindow = TheWindowManager->winGetWindowFromId( nullptr, NAMEKEY("SkirmishGameOptionsMenu.wnd:StaticTextWinsValue") );
	if (winsWindow)
	{
		uStr.format(L"%d", stats.getWins());
		GadgetStaticTextSetText(winsWindow, uStr);
	}
	GameWindow *lossesWindow = TheWindowManager->winGetWindowFromId( nullptr, NAMEKEY("SkirmishGameOptionsMenu.wnd:StaticTextLossesValue") );
	if (lossesWindow)
	{
		uStr.format(L"%d", stats.getLosses());
		GadgetStaticTextSetText(lossesWindow, uStr);
	}

	ResetBattleHonorInsertion();
	GadgetListBoxAddEntryImage(list, nullptr, 0, 0, 10, 10, TRUE, GameMakeColor(255,255,255,255));

	// FIRST ROW OF HONORS
	row = 1; column = 0;

	// TEST FOR CHINA CAMPAIGN HONOR (GOLD, SILVER, BRONZE or GREYED OUT)
	if (stats.getCHINACampaignComplete(DIFFICULTY_HARD))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("ChinaCampaign_G"), TRUE,
			BATTLE_HONOR_CAMPAIGN_CHINA, row, column);
	}
	else if (stats.getCHINACampaignComplete(DIFFICULTY_NORMAL))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("ChinaCampaign_S"), TRUE,
			BATTLE_HONOR_CAMPAIGN_CHINA, row, column);
	}
	else if (stats.getCHINACampaignComplete(DIFFICULTY_EASY))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("ChinaCampaign_B"), TRUE,
			BATTLE_HONOR_CAMPAIGN_CHINA, row, column);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("ChinaCampaign_B"), (honors & BATTLE_HONOR_CAMPAIGN_CHINA),
			BATTLE_HONOR_CAMPAIGN_CHINA, row, column);
	}

	// TEST FOR GLA CAMPAIGN HONOR (GOLD, SILVER, BRONZE or GREYED OUT)
	if (stats.getGLACampaignComplete(DIFFICULTY_HARD))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("GLACampaign_G"), TRUE,
			BATTLE_HONOR_CAMPAIGN_GLA, row, column);
	}
	else if (stats.getGLACampaignComplete(DIFFICULTY_NORMAL))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("GLACampaign_S"), TRUE,
			BATTLE_HONOR_CAMPAIGN_GLA, row, column);
	}
	else if (stats.getGLACampaignComplete(DIFFICULTY_EASY))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("GLACampaign_B"), TRUE,
			BATTLE_HONOR_CAMPAIGN_GLA, row, column);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("GLACampaign_B"), (honors & BATTLE_HONOR_CAMPAIGN_GLA),
			BATTLE_HONOR_CAMPAIGN_GLA, row, column);
	}

	// TEST FOR GLA CAMPAIGN HONOR (GOLD, SILVER, BRONZE or GREYED OUT)
	if (stats.getUSACampaignComplete(DIFFICULTY_HARD))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("USACampaign_G"), TRUE,
			BATTLE_HONOR_CAMPAIGN_USA, row, column);
	}
	else if (stats.getUSACampaignComplete(DIFFICULTY_NORMAL))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("USACampaign_S"), TRUE,
			BATTLE_HONOR_CAMPAIGN_USA, row, column);
	}
	else if (stats.getUSACampaignComplete(DIFFICULTY_EASY))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("USACampaign_B"), TRUE,
			BATTLE_HONOR_CAMPAIGN_USA, row, column);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("USACampaign_B"), (honors & BATTLE_HONOR_CAMPAIGN_USA),
			BATTLE_HONOR_CAMPAIGN_USA, row, column);
	}

	// TEST FOR GENERALS CHALLENGE HONOR (GOLD, SILVER, BRONZE or GREYED OUT)
	Bool completedOnHard		= FALSE;
	Bool completedOnNormal	= FALSE;
	Bool completedOnEasy		= FALSE;
	for (int i = 0; i < MAX_GLOBAL_GENERAL_TYPES; ++i)
	{
		if (stats.getChallengeCampaignComplete(i, DIFFICULTY_HARD))
			completedOnHard = TRUE;

		if (stats.getChallengeCampaignComplete(i, DIFFICULTY_NORMAL))
			completedOnNormal = TRUE;

		if (stats.getChallengeCampaignComplete(i, DIFFICULTY_EASY))
			completedOnEasy = TRUE;
	}

	if (completedOnHard)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Challenge_Gold"), TRUE,
			BATTLE_HONOR_CHALLENGE_MODE, row, column);
	}
	else if (completedOnNormal)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Challenge_Silver"), TRUE,
			BATTLE_HONOR_CHALLENGE_MODE, row, column);
	}
	else if (completedOnEasy)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Challenge_Bronz"), TRUE,
			BATTLE_HONOR_CHALLENGE_MODE, row, column);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Challenge_Bronz"), FALSE,
			BATTLE_HONOR_CHALLENGE_MODE, row, column);
	}

	// TEST FOR AIR WING HONOR
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorAirWing"), (honors & BATTLE_HONOR_AIR_WING),
		BATTLE_HONOR_AIR_WING, row, column);

	// TEST FOR BATTLE TANK HONOR
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorBattleTank"), (honors & BATTLE_HONOR_BATTLE_TANK),
		BATTLE_HONOR_BATTLE_TANK, row, column);
	GadgetListBoxAddEntryImage(list, nullptr, 2, 0, 10, 10, TRUE, GameMakeColor(255,255,255,255));

	// NEXT ROW OF HONORS
	row = 3; column = 0;

	// TEST FOR ENDURANCE HONOR (GOLD, SILVER, BRONZE or GREYED OUT)
	MapCache::const_iterator it;
	Bool missingEasy = FALSE;
	Bool missingMedium = FALSE;
	Bool missingBrutal = FALSE;
	for (it = TheMapCache->begin(); it != TheMapCache->end(); ++it)
	{
		if (!it->second.m_isOfficial || !it->second.m_isMultiplayer)
			continue;

		Bool easy = TRUE;
		Bool med = TRUE;
		Bool hard = TRUE;
		if (stats.getEnduranceMedal(it->first, SLOT_EASY_AI) == 0)
			easy = FALSE;
		if (stats.getEnduranceMedal(it->first, SLOT_MED_AI) == 0)
			med = FALSE;
		if (stats.getEnduranceMedal(it->first, SLOT_BRUTAL_AI) == 0)
			hard = FALSE;

		if (!easy && !med && !hard)
			missingEasy = TRUE;

		if (!med && !hard)
			missingMedium = TRUE;

		if (!hard)
			missingBrutal = TRUE;
	}
	if (!missingBrutal)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Endurance_G"), TRUE,
			BATTLE_HONOR_ENDURANCE, row, column);
	}
	else if (!missingMedium)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Endurance_S"), TRUE,
			BATTLE_HONOR_ENDURANCE, row, column);
	}
	else if (!missingEasy)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Endurance_B"), TRUE,
			BATTLE_HONOR_ENDURANCE, row, column);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Endurance_B"), FALSE,
			BATTLE_HONOR_ENDURANCE, row, column);
	}

	// TEST FOR APOCALYPSE HONOR
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Apocalypse"), (honors & BATTLE_HONOR_APOCALYPSE),
		BATTLE_HONOR_APOCALYPSE, row, column);

	// TEST FOR BLITZ HONOR
	if (honors & BATTLE_HONOR_BLITZ5)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorBlitz5"), TRUE,
			BATTLE_HONOR_BLITZ5, row, column);
	}
	else if (honors & BATTLE_HONOR_BLITZ10)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorBlitz10"), TRUE,
			BATTLE_HONOR_BLITZ10, row, column);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorBlitz10"), FALSE,
			BATTLE_HONOR_BLITZ10, row, column);
	}

	// TEST FOR STREAK HONOR
	Int streak = stats.getBestWinStreak();
	uStr.format(L"%10d", streak);
	if (streak >= 1000)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_1000"), TRUE,
			BATTLE_HONOR_STREAK, row, column, uStr, streak);
	}
	else if (streak >= 500)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_500"), TRUE,
			BATTLE_HONOR_STREAK, row, column, uStr, streak);
	}
	else if (streak >= 100)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_100"), TRUE,
			BATTLE_HONOR_STREAK, row, column, uStr, streak);
	}
	else if (streak >= 25)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_G"), TRUE,
			BATTLE_HONOR_STREAK, row, column, uStr, streak);
	}
	else if (streak >= 10)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_S"), TRUE,
			BATTLE_HONOR_STREAK, row, column, uStr, streak);
	}
	else if (streak >= 3)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_B"), TRUE,
			BATTLE_HONOR_STREAK, row, column, uStr, streak);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorStreak_B"), FALSE,
			BATTLE_HONOR_STREAK, row, column, uStr, streak);
	}

	// TEST FOR DOMINATION HONOR
	Int totalWins = stats.getWins();
	uStr.format(L"%10d", totalWins);
	if (totalWins >= 10000)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_10000"), TRUE,
			BATTLE_HONOR_DOMINATION, row, column, uStr, totalWins);
	}
	else if (totalWins >= 1000)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_1000"), TRUE,
			BATTLE_HONOR_DOMINATION, row, column, uStr, totalWins);
	}
	else if (totalWins >= 500)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_500"), TRUE,
			BATTLE_HONOR_DOMINATION, row, column, uStr, totalWins);
	}
	else if (totalWins >= 100)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_100"), TRUE,
			BATTLE_HONOR_DOMINATION, row, column, uStr, totalWins);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Domination_100"), FALSE,
			BATTLE_HONOR_DOMINATION, row, column, uStr, totalWins);
	}

	// TEST FOR ULTIMATE HONOR
	Bool perfect = TRUE;
	for (it = TheMapCache->begin(); it != TheMapCache->end(); ++it)
	{
		if (!it->second.m_isOfficial || !it->second.m_isMultiplayer)
			continue;

		Int totalOpponentSlots = it->second.m_numPlayers - 1;
		Int numBrutalOpponentsBeaten = stats.getEnduranceMedal(it->first, SLOT_BRUTAL_AI);
		if (numBrutalOpponentsBeaten < totalOpponentSlots)
		{
			perfect = FALSE;
			break;
		}
	}
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Ultimate"), perfect,
		BATTLE_HONOR_ULTIMATE, row, column);

	// TEST FOR CHALLENGE HONOR

	/*
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Loyalty_USA"), (honors & BATTLE_HONOR_LOYALTY_USA),
		BATTLE_HONOR_LOYALTY_USA, row, column);
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Loyalty_China"), (honors & BATTLE_HONOR_LOYALTY_CHINA),
		BATTLE_HONOR_LOYALTY_CHINA, row, column);
	InsertBattleHonor(list, TheMappedImageCollection->findImageByName("Loyalty_GLA"), (honors & BATTLE_HONOR_LOYALTY_GLA),
		BATTLE_HONOR_LOYALTY_GLA, row, column);
	*/

	/*
	if(BitIsSet(challenge, BH_CHALLENGE_MASK_7))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge7"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitIsSet(challenge, BH_CHALLENGE_MASK_6))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge6"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitIsSet(challenge, BH_CHALLENGE_MASK_5))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge5"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitIsSet(challenge, BH_CHALLENGE_MASK_4))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge4"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitIsSet(challenge, BH_CHALLENGE_MASK_3))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge3"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitIsSet(challenge, BH_CHALLENGE_MASK_2))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge2"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else if (BitIsSet(challenge, BH_CHALLENGE_MASK_1))
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge1"), TRUE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	else
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("HonorChallenge1"), FALSE,
			BATTLE_HONOR_CHALLENGE, row, column);
	}
	*/

	UnsignedInt isPreorder = 0;
	GetUnsignedIntFromRegistry("", "Preorder", isPreorder);
	if (isPreorder != 0)
	{
		InsertBattleHonor(list, TheMappedImageCollection->findImageByName("OfficersClub"), TRUE,
			BATTLE_HONOR_OFFICERSCLUB, row, column);
	}
}
