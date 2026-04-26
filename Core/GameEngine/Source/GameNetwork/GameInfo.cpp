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

// FILE: GameInfo.cpp //////////////////////////////////////////////////////
// game setup state info
// Author: Matthew D. Campbell, December 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/CRCDebug.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/GameState.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Energy.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Xfer.h"
#include "GameNetwork/FileTransfer.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"
#include "GameNetwork/GameSpy/StagingRoomGameInfo.h"
#include "GameNetwork/LANAPI.h"						// for testing packet size
#include "GameNetwork/LANAPICallbacks.h"	// for testing packet size
#include "strtok_r.h"



GameInfo *TheGameInfo = nullptr;

// GameSlot ----------------------------------------

GameSlot::GameSlot()
{
	reset();
}

void GameSlot::reset()
{
	m_state = SLOT_CLOSED; // decent default
	m_isAccepted = false;
	m_hasMap = true;
	m_color = -1;
	m_shaderId = 0;       // default house-color shader
	m_startPos = -1;
	m_playerTemplate = -1;
	m_teamNumber = -1;
	m_NATBehavior = FirewallHelperClass::FIREWALL_TYPE_SIMPLE;
	m_lastFrameInGame = 0;
	m_disconnected = FALSE;
	m_port = 0;
	m_sessionID = 0;
	m_isMuted = FALSE;
	m_origPlayerTemplate = -1;
	m_origStartPos = -1;
	m_origColor = -1;
}

void GameSlot::saveOffOriginalInfo()
{
	DEBUG_LOG(("GameSlot::saveOffOriginalInfo() - orig was color=%d, pos=%d, house=%d",
		m_origColor, m_origStartPos, m_origPlayerTemplate));
	m_origPlayerTemplate = m_playerTemplate;
	m_origStartPos = m_startPos;
	m_origColor = m_color;
	DEBUG_LOG(("GameSlot::saveOffOriginalInfo() - color=%d, pos=%d, house=%d",
		m_color, m_startPos, m_playerTemplate));
}

static Int getSlotIndex(const GameSlot *slot)
{
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		if (TheGameInfo->getConstSlot(i) == slot)
			return i;
	}
	return -1;
}

static Bool isSlotLocalAlly(const GameSlot *slot)
{
	Int slotIndex = getSlotIndex(slot);
	Int localIndex = TheGameInfo->getLocalSlotNum();
	const GameSlot *localSlot = TheGameInfo->getConstSlot(localIndex);

	// if either doesn't exist, not an ally
	if (slotIndex < 0 || localIndex < 0)
		return FALSE;

	// if slot is us, ally
	if (slotIndex == localIndex)
		return TRUE;

	// if slot is same team as us, ally
	if (slot->getTeamNumber() == localSlot->getTeamNumber() && slot->getTeamNumber() >= 0)
		return TRUE;

	// if we're an observer, we see all
	if (localSlot->getOriginalPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
		return TRUE;

	// nope
	return FALSE;
}

UnicodeString GameSlot::getApparentPlayerTemplateDisplayName() const
{
	if (TheMultiplayerSettings && TheMultiplayerSettings->showRandomPlayerTemplate() &&
		m_origPlayerTemplate == PLAYERTEMPLATE_RANDOM && !isSlotLocalAlly(this))
	{
		return TheGameText->fetch("GUI:Random");
	}
	else if (m_origPlayerTemplate == PLAYERTEMPLATE_OBSERVER)
	{
		return TheGameText->fetch("GUI:Observer");
	}
	DEBUG_LOG(("Fetching player template display name for player template %d (orig is %d)",
		m_playerTemplate, m_origPlayerTemplate));
	if (m_playerTemplate < 0)
	{
		return TheGameText->fetch("GUI:Random");
	}
	return ThePlayerTemplateStore->getNthPlayerTemplate(m_playerTemplate)->getDisplayName();
}

Int GameSlot::getApparentPlayerTemplate() const
{
	if (TheMultiplayerSettings && TheMultiplayerSettings->showRandomPlayerTemplate() &&
		!isSlotLocalAlly(this))
	{
		return m_origPlayerTemplate;
	}
	return m_playerTemplate;
}

Int GameSlot::getApparentColor() const
{
	// All slot color values are raw 0x00RRGGBB ints since the launcher
	// revamp — see GameSlot::setColor in GameInfo.h. The old observer
	// branch returned `getColor(PLAYERTEMPLATE_OBSERVER)->getColor()`
	// which silently cast a packed Color value (e.g. 0xFF808080) into
	// the Int return type as if it were a palette index, breaking
	// every downstream resolveSlotColor / getColor(int) consumer.
	//
	// Just return the random sentinel for observers — every render
	// site funnels through MultiplayerSettings::resolveSlotColor()
	// which maps any negative value to a defensive white default.
	if (m_origPlayerTemplate == PLAYERTEMPLATE_OBSERVER)
		return -1;

	if (TheMultiplayerSettings && TheMultiplayerSettings->showRandomColor() &&
		!isSlotLocalAlly(this))
	{
		return m_origColor;
	}
	return m_color;
}

Int GameSlot::getApparentStartPos() const
{
	if (TheMultiplayerSettings && TheMultiplayerSettings->showRandomStartPos() &&
		!isSlotLocalAlly(this))
	{
		return m_origStartPos;
	}
	return m_startPos;
}


void GameSlot::unAccept()
{
	if (isHuman())
	{
		m_isAccepted = false;
	}
}

void GameSlot::setMapAvailability( Bool hasMap )
{
	if (isHuman())
	{
		m_hasMap = hasMap;
	}
}

void GameSlot::setState( SlotState state, UnicodeString name, UnsignedInt IP )
{
	if (!(isAI() &&  (state == SLOT_EASY_AI || state == SLOT_MED_AI || state == SLOT_BRUTAL_AI
		|| state == SLOT_BRUTAL_REAL_AI || state == SLOT_INSANE_AI || state == SLOT_NIGHTMARE_AI)))
	{
		m_color = -1;
		m_startPos = -1;
		m_playerTemplate = -1;
		m_teamNumber = -1;

		if (state == SLOT_OPEN && TheGameSpyGame && TheGameSpyGame->getConstSlot(0) == this)
		{
			DEBUG_CRASH(("Game Is Hosed!"));
		}
	}
	if (state == SLOT_PLAYER)
	{
		reset();
		m_state = state;
		m_name = name;
	}
	else
	{
		m_state = state;
		m_isAccepted = true;
		m_hasMap = true;
		switch(state)
		{
		case SLOT_OPEN:
			m_name = TheGameText->fetch("GUI:Open");
			break;
		case SLOT_EASY_AI:
			m_name = TheGameText->fetch("GUI:EasyAI");
			break;
		case SLOT_MED_AI:
			m_name = TheGameText->fetch("GUI:MediumAI");
			break;
		case SLOT_BRUTAL_AI:
			m_name = TheGameText->fetch("GUI:HardAI");
			break;
		case SLOT_BRUTAL_REAL_AI:
			m_name = UnicodeString(L"Brutal Army");
			break;
		case SLOT_INSANE_AI:
			m_name = UnicodeString(L"Insane Army");
			break;
		case SLOT_NIGHTMARE_AI:
			m_name = UnicodeString(L"Nightmare Army");
			break;
		case SLOT_CLOSED:
		default:
			m_name = TheGameText->fetch("GUI:Closed");
			break;
		}
	}

	m_IP = IP;
}

// Various tests
Bool GameSlot::isHuman() const
{
	return m_state == SLOT_PLAYER;
}

Bool GameSlot::isOccupied() const
{
	return m_state == SLOT_PLAYER || isAI();
}

Bool GameSlot::isAI() const
{
	return m_state == SLOT_EASY_AI || m_state == SLOT_MED_AI || m_state == SLOT_BRUTAL_AI
		|| m_state == SLOT_BRUTAL_REAL_AI || m_state == SLOT_INSANE_AI || m_state == SLOT_NIGHTMARE_AI;
}

Bool GameSlot::isPlayer( AsciiString userName ) const
{
	UnicodeString uName;
	uName.translate(userName);
	return (m_state == SLOT_PLAYER && !m_name.compareNoCase(uName));
}

Bool GameSlot::isPlayer( UnicodeString userName ) const
{
	return (m_state == SLOT_PLAYER && !m_name.compareNoCase(userName));
}

Bool GameSlot::isPlayer( UnsignedInt ip ) const
{
	return (m_state == SLOT_PLAYER && m_IP == ip);
}

Bool GameSlot::isOpen() const
{
	return m_state == SLOT_OPEN;
}

// GameInfo ----------------------------------------

GameInfo::GameInfo()
{
	for (int i=0; i<MAX_SLOTS; ++i)
	{
		m_slot[i] = nullptr;
	}
	reset();
}

void GameInfo::init()
{
	reset();
}

void GameInfo::reset()
{
	m_crcInterval = NET_CRC_INTERVAL;
	m_inGame = false;
	m_inProgress = false;
	m_gameID = 0;
	m_mapName = "NOMAP";
	m_mapMask = 0;
	m_seed = GetTickCount(); //GameClientRandomValue(0, INT_MAX - 1);
	m_useStats = TRUE;
	m_surrendered = FALSE;
  m_oldFactionsOnly = FALSE;
  m_clientServerMode = FALSE;
  m_gameFps = 70; // Default skirmish/MP logic FPS — see GameInfo::getGameFps comment
  m_sharedTeamMoney = FALSE; // Team-pooled credits mode; host toggles via lobby checkbox
  m_sharedTeamPower = FALSE; // Team-pooled energy mode; host toggles via lobby checkbox
  m_aiChecksMoney   = TRUE;  // Default ON: AI must pay for construction like a human. Host can toggle off in
                             // the lobby to restore retail "AI builds for free" cheat. NOTE: because default
                             // flipped, ACM= is now emitted unconditionally in GameInfoToAsciiString — same
                             // reasoning as ARC below: emit-only-when-on would silently break the
                             // host-unchecks case (joiners' reset-initialized TRUE never gets overwritten
                             // because the absent key wouldn't trigger the setter).
  m_aiRebuildsCC    = TRUE;  // New default: let the AI recover from decapitation. Host can toggle off via lobby.
                             // NOTE: because default flipped, ARC= is emitted unconditionally in
                             // GameInfoToAsciiString (can't rely on emit-only-when-on any more).
  m_sharedTeamControl = FALSE; // Team shared-control mode; host toggles via lobby checkbox. Requires Shared Money + Power.
//	m_localIP = 0; // BGC - actually we don't want this to be reset since the m_localIP is
										// set properly in the constructor of LANGameInfo which uses this as a base class.
	m_mapCRC = 0;
	m_mapSize = 0;
  m_superweaponRestriction = 3;
  m_startingCash = TheGlobalData->m_defaultStartingCash;

	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		if (m_slot[i])
			m_slot[i]->reset();
	}

	m_preorderMask = 0;
}

Bool GameInfo::isPlayerPreorder(Int index)
{
	if (index >= 0 && index < MAX_SLOTS)
		return ((m_preorderMask & (1 << index)) != 0);
	return FALSE;
}

void GameInfo::markPlayerAsPreorder(Int index)
{
	if (index >= 0 && index < MAX_SLOTS)
		m_preorderMask |= 1 << index;
}


void GameInfo::clearSlotList()
{
	for (int i=0; i<MAX_SLOTS; ++i)
	{
		if (m_slot[i])
			m_slot[i]->setState(SLOT_CLOSED);
	}
}

Int GameInfo::getNumPlayers() const
{
	Int numPlayers = 0;
	for (int i=0; i<MAX_SLOTS; ++i)
	{
		if (m_slot[i] && m_slot[i]->isOccupied())
			numPlayers++;
	}
	return numPlayers;
}

Int GameInfo::getNumNonObserverPlayers() const
{
	Int numPlayers = 0;
	for (int i=0; i<MAX_SLOTS; ++i)
	{
		if (m_slot[i] && m_slot[i]->isOccupied() && m_slot[i]->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
			numPlayers++;
	}
	return numPlayers;
}

Int GameInfo::getMaxPlayers() const
{
	if (!TheMapCache)
		return -1;

	AsciiString lowerMap = m_mapName;
	lowerMap.toLower();
	MapCache::iterator it = TheMapCache->find(lowerMap);
	if (it == TheMapCache->end())
		return -1;
	MapMetaData data = it->second;
	return data.m_numPlayers;
}

void GameInfo::enterGame()
{
	DEBUG_ASSERTCRASH(!m_inGame && !m_inProgress, ("Entering game at a bad time!"));
	reset();
	m_inGame = true;
	m_inProgress = false;
}

void GameInfo::leaveGame()
{
	DEBUG_ASSERTCRASH(m_inGame && !m_inProgress, ("Leaving game at a bad time!"));
	reset();
}

void GameInfo::startGame( Int gameID )
{
	DEBUG_ASSERTCRASH(m_inGame && !m_inProgress, ("Starting game at a bad time!"));
	m_gameID = gameID;
	closeOpenSlots();
	m_inProgress = true;
}

void GameInfo::endGame()
{
	DEBUG_ASSERTCRASH(m_inGame && m_inProgress, ("Ending game without playing one!"));
	m_inGame = false;
	m_inProgress = false;
}

void GameInfo::setSlot( Int slotNum, GameSlot slotInfo )
{
	DEBUG_ASSERTCRASH( slotNum >= 0 && slotNum < MAX_SLOTS, ("GameInfo::setSlot - Invalid slot number"));
	if (slotNum < 0 || slotNum >= MAX_SLOTS)
		return;

	DEBUG_ASSERTCRASH( m_slot[slotNum], ("null slot pointer"));
	if (!m_slot[slotNum])
		return;

//	Bool isHuman = slotInfo.isHuman();
//	Bool wasHuman = m_slot[slotNum]->isHuman();

	if (slotNum == 0)
	{
		slotInfo.setAccept();
		slotInfo.setMapAvailability(true);
	}
	*m_slot[slotNum] = slotInfo;

#ifdef DEBUG_LOGGING
	UnsignedInt ip = slotInfo.getIP();
#endif

	DEBUG_LOG(("GameInfo::setSlot - setting slot %d to be player %ls with IP %d.%d.%d.%d", slotNum, slotInfo.getName().str(),
							PRINTF_IP_AS_4_INTS(ip)));
}

GameSlot* GameInfo::getSlot( Int slotNum )
{
	DEBUG_ASSERTCRASH( slotNum >= 0 && slotNum < MAX_SLOTS, ("GameInfo::getSlot - Invalid slot number"));
	if (slotNum < 0 || slotNum >= MAX_SLOTS)
		return nullptr;

	DEBUG_ASSERTCRASH( m_slot[slotNum], ("null slot pointer") );
	return m_slot[slotNum];
}

const GameSlot* GameInfo::getConstSlot( Int slotNum ) const
{
	DEBUG_ASSERTCRASH( slotNum >= 0 && slotNum < MAX_SLOTS, ("GameInfo::getSlot - Invalid slot number"));
	if (slotNum < 0 || slotNum >= MAX_SLOTS)
		return nullptr;

	DEBUG_ASSERTCRASH( m_slot[slotNum], ("null slot pointer") );
	return m_slot[slotNum];
}

Int GameInfo::getLocalSlotNum() const
{
	DEBUG_ASSERTCRASH(m_inGame, ("Looking for local game slot while not in game"));
	if (!m_inGame)
		return -1;

	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = getConstSlot(i);
		if (slot == nullptr) {
			continue;
		}
		if (slot->isPlayer(m_localIP))
			return i;
	}
	return -1;
}

Int GameInfo::getSlotNum( AsciiString userName ) const
{
	DEBUG_ASSERTCRASH(m_inGame, ("Looking for game slot while not in game"));
	if (!m_inGame)
		return -1;

	UnicodeString uName;
	uName.translate(userName);
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = getConstSlot(i);
		if (slot->isPlayer( uName ))
			return i;
	}
	return -1;
}

Bool GameInfo::amIHost() const
{
	DEBUG_ASSERTCRASH(m_inGame, ("Looking for game slot while not in game"));
	if (!m_inGame)
		return false;

	return getConstSlot(0)->isPlayer(m_localIP);
}

void GameInfo::setMap( AsciiString mapName )
{
	m_mapName = mapName;
	if (m_inGame && amIHost())
	{
		const MapMetaData *mapData = TheMapCache->findMap( mapName );
		if (mapData)
		{
			m_mapMask = 1;
			AsciiString path = mapName;
			path.truncateBy(3);
			path.concat("tga");
			DEBUG_LOG(("GameInfo::setMap() - Looking for '%s'", path.str()));
			File *fp = TheFileSystem->openFile(path.str());
			if (fp)
			{
				m_mapMask |= 2;
				fp->close();
				fp = nullptr;
			}

			AsciiString newMapName;
			if (!mapName.isEmpty())
			{
				AsciiString token;
				mapName.nextToken(&token, "\\/");
				// add all the tokens except the last one.
				// that way we don't add the filename, just the
				// directory name, we can do this since the filename
				// is just the directory name with the file extention
				// added onto it.
				while (mapName.find('\\') != nullptr)
				{
					if (!newMapName.isEmpty())
					{
						newMapName.concat('/');
					}
					newMapName.concat(token);
					mapName.nextToken(&token, "\\/");
				}
			}
			newMapName.concat("/map.ini");
			DEBUG_LOG(("GameInfo::setMap() - Looking for '%s'", newMapName.str()));
			fp = TheFileSystem->openFile(newMapName.str());
			if (fp)
			{
				m_mapMask |= 4;
				fp->close();
				fp = nullptr;
			}

			path = GetStrFileFromMap(m_mapName);
			DEBUG_LOG(("GameInfo::setMap() - Looking for '%s'", path.str()));
			fp = TheFileSystem->openFile(path.str());
			if (fp)
			{
				m_mapMask |= 8;
				fp->close();
				fp = nullptr;
			}

			path = GetSoloINIFromMap(m_mapName);
			DEBUG_LOG(("GameInfo::setMap() - Looking for '%s'", path.str()));
			fp = TheFileSystem->openFile(path.str());
			if (fp)
			{
				m_mapMask |= 16;
				fp->close();
				fp = nullptr;
			}

			path = GetAssetUsageFromMap(m_mapName);
			DEBUG_LOG(("GameInfo::setMap() - Looking for '%s'", path.str()));
			fp = TheFileSystem->openFile(path.str());
			if (fp)
			{
				m_mapMask |= 32;
				fp->close();
				fp = nullptr;
			}

			path = GetReadmeFromMap(m_mapName);
			DEBUG_LOG(("GameInfo::setMap() - Looking for '%s'", path.str()));
			fp = TheFileSystem->openFile(path.str());
			if (fp)
			{
				m_mapMask |= 64;
				fp->close();
				fp = nullptr;
			}
		}
		else
		{
			m_mapMask = 0;
		}
	}
}

void GameInfo::setMapContentsMask( Int mask )
{
	m_mapMask = mask;
}

void GameInfo::setMapCRC( UnsignedInt mapCRC )
{
	m_mapCRC = mapCRC;
	if (!TheMapCache)
		return;

	// check the map cache
	if (m_inGame && getLocalSlotNum() >= 0)
	{
		//TheMapCache->updateCache();
		AsciiString lowerMap = m_mapName;
		lowerMap.toLower();
		//DEBUG_LOG(("GameInfo::setMapCRC - looking for map file \"%s\" in the map cache", lowerMap.str()));
		std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
		if (it == TheMapCache->end())
		{
			/*
			DEBUG_LOG(("GameInfo::setMapCRC - could not find map file."));
			it = TheMapCache->begin();
			while (it != TheMapCache->end())
			{
				DEBUG_LOG(("\t\"%s\"", it->first.str()));
				++it;
			}
			*/
			getSlot(getLocalSlotNum())->setMapAvailability(false);
		}
		else if (m_mapCRC != it->second.m_CRC)
		{
			DEBUG_LOG(("GameInfo::setMapCRC - map CRC's do not match (%X/%X).", m_mapCRC, it->second.m_CRC));
			getSlot(getLocalSlotNum())->setMapAvailability(false);
		}
		else
		{
			//DEBUG_LOG(("GameInfo::setMapCRC - map CRC's match."));
			getSlot(getLocalSlotNum())->setMapAvailability(true);
		}
	}
}

void GameInfo::setMapSize( UnsignedInt mapSize )
{
	m_mapSize = mapSize;
	if (!TheMapCache)
		return;

	// check the map cache
	if (m_inGame && getLocalSlotNum() >= 0)
	{
		//TheMapCache->updateCache();
		AsciiString lowerMap = m_mapName;
		lowerMap.toLower();
		std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
		if (it == TheMapCache->end())
		{
			DEBUG_LOG(("GameInfo::setMapSize - could not find map file."));
			getSlot(getLocalSlotNum())->setMapAvailability(false);
		}
		else if (m_mapCRC != it->second.m_CRC)
		{
			DEBUG_LOG(("GameInfo::setMapSize - map CRC's do not match."));
			getSlot(getLocalSlotNum())->setMapAvailability(false);
		}
		else
		{
			//DEBUG_LOG(("GameInfo::setMapSize - map CRC's match."));
			getSlot(getLocalSlotNum())->setMapAvailability(true);
		}
	}
}

void GameInfo::setSeed( Int seed )
{
	m_seed = seed;
}

void GameInfo::setSlotPointer( Int index, GameSlot *slot )
{
	if (index < 0 || index >= MAX_SLOTS)
		return;

	m_slot[index] = slot;
}

void GameInfo::setSuperweaponRestriction( UnsignedShort restriction )
{
  m_superweaponRestriction = restriction;
}

void GameInfo::setStartingCash( const Money & startingCash )
{
  m_startingCash = startingCash;
}

Bool GameInfo::isColorTaken(Int colorIdx, Int slotToIgnore ) const
{
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = getConstSlot(i);
		if (slot && slot->getColor() == colorIdx && i != slotToIgnore)
			return true;
	}
	return false;
}

Bool GameInfo::isStartPositionTaken(Int positionIdx, Int slotToIgnore ) const
{
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = getConstSlot(i);
		if (slot && slot->getStartPos() == positionIdx && i != slotToIgnore)
			return true;
	}
	return false;
}

void GameInfo::resetAccepted()
{
	GameSlot *slot = getSlot(0);
	if (slot)
		slot->setAccept();
	for(int i = 1; i< MAX_SLOTS; i++)
	{
		slot = getSlot(i);
		if (slot)
			slot->unAccept();
	}
}

void GameInfo::resetStartSpots()
{
	GameSlot *slot = nullptr;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		slot = getSlot(i);
		if (slot != nullptr)
		{
			slot->setStartPos(-1);
		}
	}
}

// adjust the slots in the game to open or closed
// depending on the players in there now and the number of
// players the map can hold.
void GameInfo::adjustSlotsForMap()
{
	const MapMetaData *md = TheMapCache->findMap(m_mapName);
	if (md != nullptr)
	{
		// get the number of players allowed from the map.
		Int numPlayers = md->m_numPlayers;
		Int numPlayerSlots = 0;

		// first get the number of occupied slots.
		Int i = 0;
		for (; i < MAX_SLOTS; ++i)
		{
			GameSlot *tempSlot = getSlot(i);
			if (tempSlot->isOccupied())
			{
				++numPlayerSlots;
			}
		}

		// now go through and close the appropriate number of slots.
		// note that no players are kicked in this process, we leave
		// that up to the user.
		for (i = 0; i < MAX_SLOTS; ++i)
		{
			// we have room for more players, if this slot is unoccupied, set it to open.
			GameSlot *slot = getSlot(i);
			if (numPlayers > numPlayerSlots)
			{
				if (!(slot->isOccupied()))
				{
					GameSlot newSlot;
					newSlot.setState(SLOT_OPEN);
					setSlot(i, newSlot);
					++numPlayerSlots;
				}
			}
			else
			{
				if (!(slot->isOccupied()))
				{
					// we don't have any more room, set this slot to closed.
					GameSlot newSlot;
					newSlot.setState(SLOT_CLOSED);
					setSlot(i, newSlot);
				}
			}
		}
	}
}

void GameInfo::closeOpenSlots()
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		GameSlot *slot = getSlot(i);
		if (!(slot->isOccupied()))
		{
			GameSlot newSlot;
			newSlot.setState(SLOT_CLOSED);
			setSlot(i, newSlot);
		}
	}
}

static Bool isSlotLocalAlly(GameInfo *game, const GameSlot *slot)
{
	const GameSlot *localSlot = game->getConstSlot(game->getLocalSlotNum());
	if (!localSlot)
		return TRUE;

	if (slot == localSlot)
		return TRUE;

	if (slot->getTeamNumber() < 0)
		return FALSE;

	return slot->getTeamNumber() == localSlot->getTeamNumber();
}

Bool GameInfo::isSkirmish()
{
	Bool sawAI = FALSE;

	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		if (i == getLocalSlotNum())
			continue;

		if (getConstSlot(i)->isHuman())
			return FALSE;

		if (getConstSlot(i)->isAI())
		{
			if (isSlotLocalAlly(getConstSlot(i)))
				return FALSE;
			sawAI = TRUE;
		}
	}
	return sawAI;
}

Bool GameInfo::isMultiPlayer()
{
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		if (i == getLocalSlotNum())
			continue;

		if (getConstSlot(i)->isHuman())
			return TRUE;
	}

	return FALSE;
}

Bool GameInfo::isSandbox()
{
	Int localSlotNum = getLocalSlotNum();
	Int localTeam = getConstSlot(localSlotNum)->getTeamNumber();
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		if (i == localSlotNum)
			continue;

		const GameSlot *slot = getConstSlot(i);
		if (slot->isOccupied() && (slot->getTeamNumber() < 0 || slot->getTeamNumber() != localTeam))
			return FALSE;
	}
	return TRUE;
}


// Convenience Functions ----------------------------------------

static const char slotListID		= 'S';

AsciiString GameInfoToAsciiString( const GameInfo *game )
{
	if (!game)
		return AsciiString::TheEmptyString;

	AsciiString mapName = game->getMap();
	mapName = TheGameState->realMapPathToPortableMapPath(mapName);
	AsciiString newMapName;
	if (!mapName.isEmpty())
	{
		AsciiString token;
		mapName.nextToken(&token, "\\/");
		// add all the tokens except the last one.
		// that way we don't add the filename, just the
		// directory name, we can do this since the filename
		// is just the directory name with the file extention
		// added onto it.
		while (mapName.find('\\') != nullptr)
		{
			if (!newMapName.isEmpty())
			{
				newMapName.concat('/');
			}
			newMapName.concat(token);
			mapName.nextToken(&token, "\\/");
		}
		DEBUG_LOG(("Map name is %s", mapName.str()));
	}

	AsciiString optionsString;
#if RTS_GENERALS
	optionsString.format("M=%2.2x%s;MC=%X;MS=%d;SD=%d;C=%d;", game->getMapContentsMask(), newMapName.str(),
		game->getMapCRC(), game->getMapSize(), game->getSeed(), game->getCRCInterval());
#else
	optionsString.format("US=%d;M=%2.2x%s;MC=%X;MS=%d;SD=%d;C=%d;SR=%u;SC=%u;O=%c;", game->getUseStats(), game->getMapContentsMask(), newMapName.str(),
		game->getMapCRC(), game->getMapSize(), game->getSeed(), game->getCRCInterval(), game->getSuperweaponRestriction(),
		game->getStartingCash().countMoney(), game->oldFactionsOnly() ? 'Y' : 'N' );
#endif

	// Client-server mode flag
	if (game->isClientServerMode()) {
		optionsString.concat("CS=1;");
	}

	// Host-chosen logic / lockstep FPS. Always serialize so joiners pick up the
	// host's preference even if it differs from the project default of 70.
	{
		AsciiString gfpsStr;
		gfpsStr.format("GFPS=%d;", game->getGameFps());
		optionsString.concat(gfpsStr);
	}

	// Shared-money team mode flag. Only emit when on so joiners on an
	// older build (that don't know the key) simply see the default-off
	// behavior; a value-absent key is parsed as FALSE by the loop below.
	if (game->isSharedTeamMoney())
		optionsString.concat("STM=1;");

	// Shared-power team mode flag, same emit-only-when-on convention.
	if (game->isSharedTeamPower())
		optionsString.concat("STP=1;");

	// AI Checks Money flag. Default flipped from FALSE to TRUE, so we
	// always emit 0/1 — same reasoning as ARC below. Emit-only-when-on
	// would silently break the host-unchecks case because the absent
	// key wouldn't trigger the setter on joiners and they'd stay at
	// their reset-initialized TRUE.
	{
		AsciiString acmStr;
		acmStr.format("ACM=%d;", game->isAiChecksMoney() ? 1 : 0);
		optionsString.concat(acmStr);
	}

	// AI Rebuilds CC flag. The default flipped from FALSE to TRUE, so
	// "emit only when on" would silently break when the host unchecks —
	// joiners' reset-initialized state (TRUE) would not be overwritten
	// because the absent key wouldn't trigger the setter. Always emit
	// the explicit 0/1 so toggling off propagates correctly.
	{
		AsciiString arcStr;
		arcStr.format("ARC=%d;", game->isAiRebuildsCC() ? 1 : 0);
		optionsString.concat(arcStr);
	}

	// Shared Control flag. Always emit 0/1 (not emit-only-when-on) so
	// that a host disabling SC actually propagates to joiners: with
	// emit-only-when-on, an absent key leaves the joiner at its
	// previously-parsed value, not at the reset default. Even though
	// the default is FALSE (which would make emit-only-when-on look
	// correct on first join), subsequent host toggles would be silently
	// lost. Defense-in-depth vs the emit-only-when-on bug that STM and
	// STP still carry. isSharedTeamControlEffective() requires STM+STP
	// to also be on, so the reader still guards against a malformed
	// STC=1 without prereqs.
	{
		AsciiString stcStr;
		stcStr.format("STC=%d;", game->isSharedTeamControl() ? 1 : 0);
		optionsString.concat(stcStr);
	}

	//add player info for each slot
	optionsString.concat(slotListID);
	optionsString.concat('=');
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = game->getConstSlot(i);

		AsciiString str;
		if (slot && slot->isHuman())
		{
			AsciiString tmp;  //all this data goes after name
			// Field order (backwards compat note): sessionID was the
			// last field in the original serializer; shader id is
			// appended AFTER it so older parsers that stop at
			// sessionID still get a complete slot. New parsers
			// (this build) read shader id as an optional trailing
			// field, defaulting to 0 if absent.
			tmp.format( ",%X,%d,%c%c,%d,%d,%d,%d,%d,%X,%d:",
				slot->getIP(), slot->getPort(),
				(slot->isAccepted()?'T':'F'),
				(slot->hasMap()?'T':'F'),
				slot->getColor(), slot->getPlayerTemplate(),
				slot->getStartPos(), slot->getTeamNumber(),
				slot->getNATBehavior(),
				slot->getSessionID(),
				slot->getShaderId() );
			//make sure name doesn't cause overflow of m_lanMaxOptionsLength
			int lenCur = tmp.getLength() + optionsString.getLength() + 2;  //+2 for H and trailing ;
			int lenRem = m_lanMaxOptionsLength - lenCur;  //length remaining before overflowing
			int lenMax = lenRem / (MAX_SLOTS-i);  //share lenRem with all remaining slots
			if (lenMax < 1) lenMax = 1;  // prevent infinite loop when options string is already near capacity
			AsciiString name = WideCharStringToMultiByte(slot->getName().str()).c_str();
			while( name.getLength() > lenMax )
				name.removeLastChar();  //what a horrible way to truncate.  I hate AsciiString.

			str.format( "H%s%s", name.str(), tmp.str() );
		}
		else if (slot && slot->isAI())
		{
			Char c;
			switch (slot->getState())
			{
				case SLOT_EASY_AI:        c = 'E'; break;
				case SLOT_MED_AI:         c = 'M'; break;
				case SLOT_BRUTAL_REAL_AI: c = 'B'; break;
				case SLOT_INSANE_AI:      c = 'I'; break;
				case SLOT_NIGHTMARE_AI:   c = 'N'; break;
				case SLOT_BRUTAL_AI:
				default:                  c = 'H'; break;  // legacy "Hard AI" wire char
			}
			str.format("C%c,%d,%d,%d,%d,%d:", c,
				slot->getColor(), slot->getPlayerTemplate(),
				slot->getStartPos(), slot->getTeamNumber(),
				slot->getShaderId());
		}
		else if (slot && slot->getState() == SLOT_OPEN)
		{
			str = "O:";
		}
		else if (slot && slot->getState() == SLOT_CLOSED)
		{
			str = "X:";
		}
		else
		{
			DEBUG_CRASH(("Bad slot type"));
			str = "X:";
		}
		optionsString.concat(str);
	}
	optionsString.concat(';');

	DEBUG_ASSERTCRASH(!TheLAN || (optionsString.getLength() < m_lanMaxOptionsLength),
		("WARNING: options string is longer than expected!  Length is %d, but max is %d!",
		optionsString.getLength(), m_lanMaxOptionsLength));

	return optionsString;
}

static Int grabHexInt(const char *s)
{
	char tmp[5] = "0xff";
	tmp[2] = s[0];
	tmp[3] = s[1];
	Int b = strtol(tmp, nullptr, 16);
	return b;
}
Bool ParseAsciiStringToGameInfo(GameInfo *game, AsciiString options)
{
	// Parse game options
	char *buf = strdup(options.str());
	char *bufPtr = buf;
	char *strPos, *keyValPair;
	GameSlot newSlot[MAX_SLOTS];
	Bool optionsOk = true;
	AsciiString mapName;
	Int mapContentsMask;
	UnsignedInt mapCRC, mapSize;
	Int seed = 0;
	Int crc = 100;
	Bool sawCRC = FALSE;
  Bool oldFactionsOnly = FALSE;
	Int useStats = TRUE;
  Money startingCash = TheGlobalData->m_defaultStartingCash;
  UnsignedShort restriction = 0; // Always the default

	Bool sawMap = FALSE;
	Bool sawMapCRC = FALSE;
	Bool sawMapSize = FALSE;
	Bool sawSeed = FALSE;
	Bool sawSlotlist = FALSE;
	Bool sawUseStats = FALSE;
	Bool sawSuperweaponRestriction = FALSE;
	Bool sawStartingCash = FALSE;
	Bool sawOldFactions = FALSE;

	//DEBUG_LOG(("Saw options of %s", options.str()));
	DEBUG_LOG(("ParseAsciiStringToGameInfo - parsing [%s]", options.str()));


	while ( (keyValPair = strtok_r(bufPtr, ";", &strPos)) != nullptr )
	{
		bufPtr = nullptr; // strtok within the same string

		AsciiString key, val;
		char *pos = nullptr;
		char *keyPtr, *valPtr;
		keyPtr = (strtok_r(keyValPair, "=", &pos));
		valPtr = (strtok_r(nullptr, "\n", &pos));
		if (keyPtr)
			key = keyPtr;
		if (valPtr)
			val = valPtr;

		if (val.isEmpty())
		{
			optionsOk = false;
			DEBUG_LOG(("ParseAsciiStringToGameInfo - saw empty value, quitting"));
			break;
		}

		if (key.compare("US") == 0)
		{
			useStats = atoi(val.str());
			sawUseStats = true;
		}
		else
		if (key.compare("M") == 0)
		{
			if (val.getLength() < 3)
			{
				optionsOk = FALSE;
				DEBUG_LOG(("ParseAsciiStringToGameInfo - saw bogus map; quitting"));
				break;
			}
			mapContentsMask = grabHexInt(val.str());
			AsciiString tempstr;
			AsciiString token;
			tempstr = val.str()+2;
			tempstr.nextToken(&token, "\\/");
			while (!tempstr.isEmpty())
			{
				mapName.concat(token);
				mapName.concat('\\');
				tempstr.nextToken(&token, "\\/");
			}
			mapName.concat(token);
			mapName.concat('\\');
			mapName.concat(token);
			mapName.concat('.');
			mapName.concat(TheMapCache->getMapExtension());
			AsciiString realMapName = TheGameState->portableMapPathToRealMapPath(mapName);
			if (realMapName.isEmpty())
			{
				// in other words is bogus and points outside of the approved target directory for maps, avoid an arbitrary file overwrite vulnerability
				// if the save or network game embeds a custom map to store at the location, by flagging the options as not OK and rejecting the game.
				optionsOk = FALSE;
				DEBUG_LOG(("ParseAsciiStringToGameInfo - saw bogus map name ('%s'); quitting", mapName.str()));
				break;
			}
			mapName = realMapName;
			sawMap = true;
			DEBUG_LOG(("ParseAsciiStringToGameInfo - map name is %s", mapName.str()));
		}
		else if (key.compare("MC") == 0)
		{
			mapCRC = 0;
			sscanf(val.str(), "%X", &mapCRC);
			sawMapCRC = true;
		}
		else if (key.compare("MS") == 0)
		{
			mapSize = atoi(val.str());
			sawMapSize = true;
		}
		else if (key.compare("SD") == 0)
		{
			seed = atoi(val.str());
			sawSeed = true;
//			DEBUG_LOG(("ParseAsciiStringToGameInfo - random seed is %d", seed));
		}
		else if (key.compare("C") == 0)
		{
			crc = atoi(val.str());
			sawCRC = TRUE;
		}
    else if (key.compare("SR") == 0 )
    {
      restriction = (UnsignedShort)atoi(val.str());
      sawSuperweaponRestriction = TRUE;
    }
    else if (key.compare("SC") == 0 )
    {
      UnsignedInt startingCashAmount = strtoul( val.str(), nullptr, 10 );
      startingCash.init();
      startingCash.deposit( startingCashAmount, FALSE, FALSE );
      sawStartingCash = TRUE;
    }
    else if (key.compare("O") == 0 )
    {
      oldFactionsOnly = ( val.compareNoCase( "Y" ) == 0 );
      sawOldFactions = TRUE;
    }
		else if (key.compare("CS") == 0)
		{
			game->setClientServerMode(atoi(val.str()) != 0);
		}
		else if (key.compare("GFPS") == 0)
		{
			Int fps = atoi(val.str());
			// Clamp to a sane range. The lockstep network rate is also clamped
			// in ConnectionManager::updateRunAhead, but enforcing it here too
			// guards against malformed options strings forcing absurd values.
			if (fps < 5)   fps = 5;
			if (fps > 240) fps = 240;
			game->setGameFps(fps);
		}
		else if (key.compare("STM") == 0)
		{
			game->setSharedTeamMoney( atoi(val.str()) != 0 );
		}
		else if (key.compare("STP") == 0)
		{
			game->setSharedTeamPower( atoi(val.str()) != 0 );
		}
		else if (key.compare("ACM") == 0)
		{
			game->setAiChecksMoney( atoi(val.str()) != 0 );
		}
		else if (key.compare("ARC") == 0)
		{
			game->setAiRebuildsCC( atoi(val.str()) != 0 );
		}
		else if (key.compare("STC") == 0)
		{
			game->setSharedTeamControl( atoi(val.str()) != 0 );
		}
		else if (key.getLength() == 1 && *key.str() == slotListID)
		{
			sawSlotlist = true;
			/// @TODO: Need to read in all the slot info... big mess right now.
			char *rawSlotBuf = strdup(val.str());
			char *freeMe = nullptr;
			AsciiString rawSlot;
//			Bool slotsOk = true;	//flag that lets us know whether or not the slot list is good.

//			DEBUG_LOG(("ParseAsciiStringToGameInfo - Parsing slot list"));
			for (int i=0; i<MAX_SLOTS; ++i)
				{
					rawSlot = strtok_r(rawSlotBuf,":",&pos);
					if( rawSlotBuf )
						freeMe = rawSlotBuf;
					rawSlotBuf = nullptr;
					switch (*rawSlot.str())
					{
						case 'H':
						{
//							DEBUG_LOG(("ParseAsciiStringToGameInfo - Human player"));
							char *slotPos = nullptr;
							//Parse out the Name
							AsciiString slotValue(strtok_r((char *)rawSlot.str(),",",&slotPos));
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue name is empty, quitting"));
								break;
							}
							UnicodeString name;
              				name.set(MultiByteToWideCharSingleLine(slotValue.str() +1).c_str());

							//DEBUG_LOG(("ParseAsciiStringToGameInfo - name is %s", slotValue.str()+1));

							//Parse out the IP
							slotValue = strtok_r(nullptr,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue IP address is empty, quitting"));
								break;
							}
							UnsignedInt playerIP = 0;
							sscanf(slotValue.str(),"%x", &playerIP);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - IP address is %x", playerIP));

							//set the state of the slot
							newSlot[i].setState(SLOT_PLAYER, name, playerIP);

							// parse out the port
							slotValue = strtok_r(nullptr, ",", &slotPos);
							if (slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue port is empty, quitting"));
								break;
							}
							UnsignedInt playerPort = 0;
							sscanf(slotValue.str(), "%d", &playerPort);
							newSlot[i].setPort(playerPort);
							DEBUG_LOG(("ParseAsciiStringToGameInfo - port is %d", playerPort));

							//Read if it's accepted or not
							slotValue = strtok_r(nullptr,",",&slotPos);
							if(slotValue.getLength() != 2)
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue accepted is mis-sized, quitting"));
								break;
							}
							const char *svs = slotValue.str();
							if(*svs == 'T') {
								newSlot[i].setAccept();
								//DEBUG_LOG(("ParseAsciiStringToGameInfo - player has accepted"));
							} else if (*svs == 'F') {
								newSlot[i].unAccept();
								//DEBUG_LOG(("ParseAsciiStringToGameInfo - player has not accepted"));
							}
							++svs;
							if(*svs == 'T') {
								newSlot[i].setMapAvailability(TRUE);
								//DEBUG_LOG(("ParseAsciiStringToGameInfo - player has map"));
							} else {
								newSlot[i].setMapAvailability(FALSE);
								//DEBUG_LOG(("ParseAsciiStringToGameInfo - player does not have map"));
							}

							//Read color index
							slotValue = strtok_r(nullptr,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue color is empty, quitting"));
								break;
							}
							Int color = atoi(slotValue.str());
							// Slot color is a raw 24-bit RGB int now, not
							// a palette index. -1 = random sentinel.
							// Anything else outside the [-1, 0xFFFFFF]
							// range is malformed input, reject.
							if (color < -1 || color > 0xFFFFFF)
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - player color was invalid, quitting"));
								break;
							}
							newSlot[i].setColor(color);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - player color set to %d", color));

							//Read playerTemplate index
							slotValue = strtok_r(nullptr,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue player template is empty, quitting"));
								break;
							}
							Int playerTemplate = atoi(slotValue.str());
							if (playerTemplate < PLAYERTEMPLATE_MIN || playerTemplate >= ThePlayerTemplateStore->getPlayerTemplateCount())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - player template value is invalid, quitting"));
								break;
							}
							newSlot[i].setPlayerTemplate(playerTemplate);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - player template is %d", playerTemplate));

							//Read start position index
							slotValue = strtok_r(nullptr,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue start position is empty, quitting"));
								break;
							}
							Int startPos = atoi(slotValue.str());
							if (startPos < -1 || startPos >= MAX_SLOTS)
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - player start position is invalid, quitting"));
								break;
							}
							newSlot[i].setStartPos(startPos);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - player start position is %d", startPos));

							//Read team index
							slotValue = strtok_r(nullptr,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue team number is empty, quitting"));
								break;
							}
							Int team = atoi(slotValue.str());
							if (team < -1 || team >= MAX_SLOTS/2)
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - team number is invalid, quitting"));
								break;
							}
							newSlot[i].setTeamNumber(team);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - team number is %d", team));

							// Read the NAT behavior
							slotValue = strtok_r(nullptr, ",",&slotPos);
							if (slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - NAT behavior is empty, quitting"));
								break;
							}
							FirewallHelperClass::FirewallBehaviorType NATType = (FirewallHelperClass::FirewallBehaviorType)atoi(slotValue.str());
							if ((NATType < FirewallHelperClass::FIREWALL_MIN) ||
									(NATType > FirewallHelperClass::FIREWALL_MAX)) {
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - NAT behavior is invalid, quitting"));
								break;
							}
							newSlot[i].setNATBehavior(NATType);
							DEBUG_LOG(("ParseAsciiStringToGameInfo - NAT behavior is %X", NATType));

							// Read the session ID (optional for backwards compatibility)
							slotValue = strtok_r(nullptr, ",",&slotPos);
							if (!slotValue.isEmpty())
							{
								UnsignedInt sessionID = 0;
								sscanf(slotValue.str(), "%x", &sessionID);
								newSlot[i].setSessionID(sessionID);
								DEBUG_LOG(("ParseAsciiStringToGameInfo - session ID is %X", sessionID));
							}

							// Read the shader id (optional, defaults to 0).
							// Trailing field — older serializers stop after
							// sessionID, in which case the slot keeps the
							// default 0 shader ("stock house-color").
							slotValue = strtok_r(nullptr, ",",&slotPos);
							if (!slotValue.isEmpty())
							{
								newSlot[i].setShaderId(atoi(slotValue.str()));
							}
						}
						break;
						case 'C':
						{
            	DEBUG_LOG(("ParseAsciiStringToGameInfo - AI player"));
							char *slotPos = nullptr;
							//Parse out the Name
							AsciiString slotValue(strtok_r((char *)rawSlot.str(),",",&slotPos));
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue AI Type is empty, quitting"));
								break;
							}

							switch(*(slotValue.str() + 1))
							{
								case 'E':
								{
									newSlot[i].setState(SLOT_EASY_AI);
								}
								break;
								case 'M':
								{
									newSlot[i].setState(SLOT_MED_AI);
								}
								break;
								case 'H':
								{
									// Legacy "Hard AI" wire char — maps to SLOT_BRUTAL_AI which
									// the engine still treats as DIFFICULTY_HARD.
									newSlot[i].setState(SLOT_BRUTAL_AI);
								}
								break;
								case 'B':
								{
									newSlot[i].setState(SLOT_BRUTAL_REAL_AI);
								}
								break;
								case 'I':
								{
									newSlot[i].setState(SLOT_INSANE_AI);
								}
								break;
								case 'N':
								{
									newSlot[i].setState(SLOT_NIGHTMARE_AI);
								}
								break;
								default:
								{
									optionsOk = false;
									DEBUG_LOG(("ParseAsciiStringToGameInfo - Unknown AI, quitting"));
								}
								break;
							}

							//Read color index
							slotValue = strtok_r(nullptr,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue color is empty, quitting"));
								break;
							}
							Int color = atoi(slotValue.str());
							// Slot color is a raw 24-bit RGB int now, not
							// a palette index. -1 = random sentinel.
							// Anything else outside the [-1, 0xFFFFFF]
							// range is malformed input, reject.
							if (color < -1 || color > 0xFFFFFF)
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - player color was invalid, quitting"));
								break;
							}
							newSlot[i].setColor(color);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - player color set to %d", color));

							//Read playerTemplate index
							slotValue = strtok_r(nullptr,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue player template is empty, quitting"));
								break;
							}
							Int playerTemplate = atoi(slotValue.str());
							if (playerTemplate < PLAYERTEMPLATE_MIN || playerTemplate >= ThePlayerTemplateStore->getPlayerTemplateCount())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - player template value is invalid, quitting"));
								break;
							}
							newSlot[i].setPlayerTemplate(playerTemplate);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - player template is %d", playerTemplate));

							//Read start pos
							slotValue = strtok_r(nullptr,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue start pos is empty, quitting"));
								break;
							}
							Int startPos = atoi(slotValue.str());
							Bool isStartPosBad = FALSE;
							if (startPos < -1 || startPos >= MAX_SLOTS)
							{
								isStartPosBad = TRUE;
							}
							for (Int j=0; j<i; ++j)
							{
								if (startPos >= 0 && startPos == newSlot[i].getStartPos())
								{
									isStartPosBad = TRUE; // can't have multiple people using the same start pos
								}
							}
							if (isStartPosBad)
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - start pos is invalid, quitting"));
								break;
							}
							newSlot[i].setStartPos(startPos);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - start spot is %d", startPos));

							//Read team index
							slotValue = strtok_r(nullptr,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue team number is empty, quitting"));
								break;
							}
							Int team = atoi(slotValue.str());
							if (team < -1 || team >= MAX_SLOTS/2)
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - team number is invalid, quitting"));
								break;
							}
							newSlot[i].setTeamNumber(team);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - team number is %d", team));

							// Optional trailing shader id (defaults to 0).
							// Same compatibility pattern as the human slot
							// shader-id read above — older serializers stop
							// after the team number.
							slotValue = strtok_r(nullptr,",",&slotPos);
							if (!slotValue.isEmpty())
							{
								newSlot[i].setShaderId(atoi(slotValue.str()));
							}
						}
						break;
						case 'O':
						{
							newSlot[i].setState( SLOT_OPEN );
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - Slot is open"));
						}
						break;
						case 'X':
						{
							newSlot[i].setState( SLOT_CLOSED );
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - Slot is closed"));
						}
						break;
						default:
						{
							optionsOk = false;
							DEBUG_LOG(("ParseAsciiStringToGameInfo - unrecognized slot entry, quitting"));
						}
						break;
					}
				}

			free(freeMe);
		}
		else
		{
			optionsOk = false;
			break;
		}
	}

	free(buf);

	// a strict requirement in the Zero Hour Replay file:
	//  * UseStats
	//  * SuperweaponRestriction
	//  * StartingCash
	//  * OldFactionsOnly
	// In Generals they never were.
	if (optionsOk && sawMap && sawMapCRC && sawMapSize && sawSeed && sawSlotlist && sawCRC)
	{
		// We were setting the Global Data directly here, but Instead, I'm now
		// first setting the data in game.  We'll set the global data when
		// we start a game.
		if (!game)
			return true;

		//DEBUG_LOG(("ParseAsciiStringToGameInfo - game options all good, setting info"));

		for(Int i = 0; i<MAX_SLOTS; i++)
			game->setSlot(i,newSlot[i]);

		game->setMap(mapName);
		game->setMapCRC(mapCRC);
		game->setMapSize(mapSize);
		game->setMapContentsMask(mapContentsMask);
		game->setSeed(seed);
		game->setCRCInterval(crc);
		game->setUseStats(useStats);
		game->setSuperweaponRestriction(restriction);
		game->setStartingCash(startingCash);
		game->setOldFactionsOnly(oldFactionsOnly);
		// m_sharedTeamMoney was already applied via game->setSharedTeamMoney
		// inside the STM= branch above (along with clientServerMode / gameFps),
		// so no extra write needed here. Those three keys are applied in-line
		// rather than cached into locals like the other options.

		return true;
	}

	DEBUG_LOG(("ParseAsciiStringToGameInfo - game options messed up"));
	return false;
}


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//------------------------- SkirmishGameInfo ---------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SkirmishGameInfo::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void SkirmishGameInfo::xfer( Xfer *xfer )
{
#if RTS_GENERALS
	const XferVersion currentVersion = 2;
#else
	// Version 5 adds the "shared money" team-pool flag + the per-team
	// pool balances. Older saves default to sharedTeamMoney=FALSE and
	// an all-zero pool.
	// Version 6 adds the "shared power" team-mode flag. No pool state
	// is persisted because pooled production / consumption is derived
	// each frame from the per-player Energy objects (which already get
	// rebuilt from building objectEnteringInfluence calls on load).
	// Version 7 adds the "AI Checks Money" host flag (default FALSE,
	// preserves retail AI-cheat behavior in older saves).
	// Version 8 adds the "AI Rebuilds CC" host flag (default FALSE,
	// preserves retail decapitation behavior).
	// Version 9 adds the "Shared Control" team-mode flag (default FALSE,
	// and isSharedTeamControl() requires Shared Money + Power anyway so
	// a v9 save from a session that had all three on replays as
	// shared-control-on; older saves simply load as FALSE).
	const XferVersion currentVersion = 9;
#endif
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );


	xfer->xferInt(&m_preorderMask);
	xfer->xferInt(&m_crcInterval);
	xfer->xferBool(&m_inGame);
	xfer->xferBool(&m_inProgress);
	xfer->xferBool(&m_surrendered);
	xfer->xferInt(&m_gameID);

	Int slot = MAX_SLOTS;
	xfer->xferInt(&slot);
	DEBUG_ASSERTCRASH(slot==MAX_SLOTS, ("MAX_SLOTS changed, need to change version. jba."));

	for (slot = 0; slot < MAX_SLOTS; slot++)
	{
		Int state = m_slot[slot]->getState();
		xfer->xferInt(&state);

		UnicodeString name=m_slot[slot]->getName();
		if (version >= 2)
		{
			xfer->xferUnicodeString(&name);
		}

		Bool isAccepted=m_slot[slot]->isAccepted();
		xfer->xferBool(&isAccepted);

		Bool isMuted=m_slot[slot]->isMuted();
		xfer->xferBool(&isMuted);
		m_slot[slot]->mute(isMuted);

		Int color=m_slot[slot]->getColor();
		xfer->xferInt(&color);

		Int startPos=m_slot[slot]->getStartPos();
		xfer->xferInt(&startPos);

		Int playerTemplate=m_slot[slot]->getPlayerTemplate();
		xfer->xferInt(&playerTemplate);

		Int teamNumber=m_slot[slot]->getTeamNumber();
		xfer->xferInt(&teamNumber);

		Int origColor=m_slot[slot]->getOriginalColor();
		xfer->xferInt(&origColor);

		Int origStartPos=m_slot[slot]->getOriginalStartPos();
		xfer->xferInt(&origStartPos);

 		Int origPlayerTemplate=m_slot[slot]->getOriginalPlayerTemplate();
		xfer->xferInt(&origPlayerTemplate);

		if( xfer->getXferMode() == XFER_LOAD ) {
			m_slot[slot]->setState((SlotState)state, name);
			if (isAccepted) m_slot[slot]->setAccept();

			m_slot[slot]->setPlayerTemplate(origPlayerTemplate);
			m_slot[slot]->setStartPos(origStartPos);
			m_slot[slot]->setColor(origColor);
			m_slot[slot]->saveOffOriginalInfo();

			m_slot[slot]->setTeamNumber(teamNumber);
			m_slot[slot]->setColor(color);
			m_slot[slot]->setStartPos(startPos);
			m_slot[slot]->setPlayerTemplate(playerTemplate);
		}
	}

	xfer->xferUnsignedInt(&m_localIP);

	xfer->xferMapName(&m_mapName);
	xfer->xferUnsignedInt(&m_mapCRC);
	xfer->xferUnsignedInt(&m_mapSize);
	xfer->xferInt(&m_mapMask);
	xfer->xferInt(&m_seed);

  if ( version >= 3 )
  {
    xfer->xferUnsignedShort( &m_superweaponRestriction );

    if ( version == 3 )
    {
      // Version 3 had a bool which is now gone
      Bool obsoleteBool;
      xfer->xferBool( &obsoleteBool );
    }

    xfer->xferSnapshot( &m_startingCash );
  }
  else if ( xfer->getXferMode() == XFER_LOAD )
  {
    m_superweaponRestriction = 3;
    m_startingCash = TheGlobalData->m_defaultStartingCash;
  }

  // Version 5: shared-money team-pool flag + pool balances. The pool
  // itself is a file-scope static array in Money.cpp (keyed by GameSlot
  // team number). Iterating by slot count keeps the save format stable
  // regardless of which players are currently alive — a dead player's
  // team still owns its pool balance until the game ends.
  if ( version >= 5 )
  {
    xfer->xferBool( &m_sharedTeamMoney );

    Int poolCount = MAX_SLOTS;
    xfer->xferInt( &poolCount );
    DEBUG_ASSERTCRASH(poolCount == MAX_SLOTS, ("MAX_SLOTS changed — bump shared-money pool xfer version."));

    for ( Int t = 0; t < MAX_SLOTS; ++t )
    {
      UnsignedInt pool = Money::getSharedPoolForTeam(t);
      xfer->xferUnsignedInt( &pool );
      if ( xfer->getXferMode() == XFER_LOAD )
        Money::setSharedPoolForTeam(t, pool);
    }
  }
  else if ( xfer->getXferMode() == XFER_LOAD )
  {
    // Pre-v5 save — no shared-money mode was possible.
    m_sharedTeamMoney = FALSE;
    Money::resetAllSharedPools();
  }

  if ( version >= 6 )
  {
    xfer->xferBool( &m_sharedTeamPower );
  }
  else if ( xfer->getXferMode() == XFER_LOAD )
  {
    // Pre-v6 save — no shared-power mode was possible.
    m_sharedTeamPower = FALSE;
  }

  if ( version >= 7 )
  {
    xfer->xferBool( &m_aiChecksMoney );
  }
  else if ( xfer->getXferMode() == XFER_LOAD )
  {
    // Pre-v7 save — AI always cheated on the money check.
    m_aiChecksMoney = FALSE;
  }

  if ( version >= 8 )
  {
    xfer->xferBool( &m_aiRebuildsCC );
  }
  else if ( xfer->getXferMode() == XFER_LOAD )
  {
    // Pre-v8 save — AI never recovered from CC loss.
    m_aiRebuildsCC = FALSE;
  }

  if ( version >= 9 )
  {
    xfer->xferBool( &m_sharedTeamControl );
  }
  else if ( xfer->getXferMode() == XFER_LOAD )
  {
    // Pre-v9 save — no shared-control mode was possible.
    m_sharedTeamControl = FALSE;
  }
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SkirmishGameInfo::loadPostProcess()
{
	// Rebind every loaded Player's Money to the per-team pool. Money's
	// in-memory routing state (m_useSharedPool / m_sharedTeamIndex) is
	// NOT persisted in Money::xfer on purpose — we reconstruct it from
	// the slot team numbers we just loaded here, which keeps the Money
	// save format backwards-compatible with pre-shared-money saves.
	//
	// Migration inside setSharedPoolBinding is a no-op in this path
	// because shared-mode saves always have m_money == 0 (setStartingCash
	// / deposit route into the pool, never into m_money, when bound).
	if (!ThePlayerList)
		return;

	const Bool sharedMoney = isSharedTeamMoney();
	const Bool sharedPower = isSharedTeamPower();
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		const GameSlot *slot = getConstSlot(i);
		if (!slot || !slot->isOccupied())
			continue;
		if (slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			continue;

		AsciiString playerName;
		playerName.format("player%d", i);
		Player *p = ThePlayerList->findPlayerWithNameKey(NAMEKEY(playerName));
		if (!p)
			continue;

		const Int teamNum = slot->getTeamNumber();
		p->getMoney()->setSharedPoolBinding(sharedMoney, teamNum);
		p->getEnergy()->setSharedTeamBinding(sharedPower, teamNum);
	}
}


