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

// FILE: Money.cpp /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: Money.cpp
//
// Created:   Steven Johnson, October 2001
//
// Desc:      @todo
//
//-----------------------------------------------------------------------------

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#include "Common/Money.h"

#include "Common/AudioSettings.h"
#include "Common/GameAudio.h"
#include "Common/MiscAudio.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/NetworkDefs.h"	// for MAX_SLOTS

// Per-team shared credits pool for the host-enabled "shared money"
// multiplayer mode. Indexed by GameSlot::getTeamNumber(); valid team
// numbers are [0, MAX_SLOTS/2) per GameInfo's validation but the array
// is sized to MAX_SLOTS for headroom. Entries are zeroed at game start
// by PlayerList setup and (on save-load) by SkirmishGameInfo::xfer.
//
// Kept as a file-scope static rather than a member of GameInfo because
// every Money instance needs cheap O(1) access on every withdraw /
// deposit — a pointer hop per transaction would cost more than the
// whole feature is worth. Lockstep determinism is preserved because
// every peer applies the same command stream in the same order against
// the same static array.
static UnsignedInt s_sharedPool[MAX_SLOTS] = { 0 };

void Money::resetAllSharedPools()
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
		s_sharedPool[i] = 0u;
}

UnsignedInt Money::getSharedPoolForTeam(Int team)
{
	if (team < 0 || team >= MAX_SLOTS) return 0u;
	return s_sharedPool[team];
}

void Money::setSharedPoolForTeam(Int team, UnsignedInt amount)
{
	if (team < 0 || team >= MAX_SLOTS) return;
	s_sharedPool[team] = amount;
}

void Money::setSharedPoolBinding(Bool enabled, Int teamNumber)
{
	const Bool shouldUsePool = enabled && teamNumber >= 0 && teamNumber < MAX_SLOTS;

	if (shouldUsePool)
	{
		// Migrate any cash already in m_money (e.g. starting cash that
		// PlayerTemplate::init deposited before we bound) into the
		// team pool, so call-order between this binding and the
		// setStartingCash path doesn't matter.
		if (!m_useSharedPool && m_money > 0)
		{
			s_sharedPool[teamNumber] += m_money;
			m_money = 0;
		}
		m_useSharedPool = TRUE;
		m_sharedTeamIndex = teamNumber;
	}
	else
	{
		// Solo player (team -1) or shared mode off. Leave m_money
		// as-is; no migration out of the pool because the "on→off"
		// transition doesn't happen mid-game in any supported flow.
		m_useSharedPool = FALSE;
		m_sharedTeamIndex = -1;
	}
}

// ------------------------------------------------------------------------------------------------
UnsignedInt Money::withdraw(UnsignedInt amountToWithdraw, Bool playSound)
{
#if defined(RTS_DEBUG)
	Player* player = ThePlayerList->getNthPlayer(m_playerIndex);
	if (player != nullptr && player->buildsForFree())
		return 0;
#endif

	// In shared-money mode the spendable balance lives in the team pool.
	// All call sites (build cost, AI purchase, unit repair, etc.) funnel
	// through here so we only need to redirect in one place.
	UnsignedInt &balance = m_useSharedPool ? s_sharedPool[m_sharedTeamIndex] : m_money;

	if (amountToWithdraw > balance)
		amountToWithdraw = balance;

	if (amountToWithdraw == 0)
		return amountToWithdraw;

	if (playSound)
	{
		triggerAudioEvent(TheAudio->getMiscAudio()->m_moneyWithdrawSound);
	}

	balance -= amountToWithdraw;

	return amountToWithdraw;
}

// ------------------------------------------------------------------------------------------------
void Money::deposit(UnsignedInt amountToDeposit, Bool playSound, Bool trackIncome)
{
	if (amountToDeposit == 0)
		return;

	if (playSound)
	{
		triggerAudioEvent(TheAudio->getMiscAudio()->m_moneyDepositSound);
	}

	if (trackIncome)
	{
		// Income tracking is PER-PLAYER regardless of pooling — so
		// individual cash-per-minute HUD and AcademyStats::recordIncome
		// still reflect this player's contribution even when the cash
		// itself flows into the team pool.
		m_incomeBuckets[m_currentBucket] += amountToDeposit;
		m_cashPerMinute += amountToDeposit;
	}

	UnsignedInt &balance = m_useSharedPool ? s_sharedPool[m_sharedTeamIndex] : m_money;
	balance += amountToDeposit;

	if( amountToDeposit > 0 )
	{
		Player *player = ThePlayerList->getNthPlayer( m_playerIndex );
		if( player )
		{
			player->getAcademyStats()->recordIncome();
		}
	}
}

// ------------------------------------------------------------------------------------------------
void Money::onPlayerKilled()
{
	if (m_useSharedPool)
	{
		// Shared pool stays — teammates keep spending. We leave
		// m_money and the income buckets alone so the dead player's
		// stat line still shows how much they contributed.
		return;
	}
	// Solo: preserve retail behavior — drain to zero.
	m_money = 0;
}

// ------------------------------------------------------------------------------------------------
void Money::setStartingCash(UnsignedInt amount)
{
	// Per-player income trackers always reset — they're per-player even
	// in shared mode so individual stats start from zero each game.
	std::fill(m_incomeBuckets, m_incomeBuckets + ARRAY_SIZE(m_incomeBuckets), 0u);
	m_currentBucket = 0u;
	m_cashPerMinute = 0u;

	if (m_useSharedPool)
	{
		// Each teammate contributes their starting cash into the team
		// pool, so a team of N players starts with N × startingCash.
		// This matches the "pooled economy" feel of comparable modes
		// in other RTS titles — a 3-player alliance should have a real
		// advantage at game start, not share one player's allowance.
		s_sharedPool[m_sharedTeamIndex] += amount;
		m_money = 0;
	}
	else
	{
		m_money = amount;
	}
}

// ------------------------------------------------------------------------------------------------
void Money::updateIncomeBucket()
{
	UnsignedInt frame = TheGameLogic->getFrame();
	UnsignedInt nextBucket = (frame / LOGICFRAMES_PER_SECOND) % ARRAY_SIZE(m_incomeBuckets);
	if (nextBucket != m_currentBucket)
	{
		m_cashPerMinute -= m_incomeBuckets[nextBucket];
		m_currentBucket = nextBucket;
		m_incomeBuckets[m_currentBucket] = 0u;
	}
}

// ------------------------------------------------------------------------------------------------
UnsignedInt Money::getCashPerMinute() const
{
	return m_cashPerMinute;
}

void Money::triggerAudioEvent(const AudioEventRTS& audioEvent)
{
	Real volume = TheAudio->getAudioSettings()->m_preferredMoneyTransactionVolume;
	volume *= audioEvent.getVolume();
	if (volume <= 0.0f)
		return;

	//@todo: Do we do this frequently enough that it is a performance hit?
	AudioEventRTS event = audioEvent;
	event.setPlayerIndex(m_playerIndex);
	event.setVolume(volume);
	TheAudio->addAudioEvent(&event);
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void Money::crc( Xfer *xfer )
{

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: a contributor @tweak Serialize income tracking
	*/
// ------------------------------------------------------------------------------------------------
void Money::xfer( Xfer *xfer )
{

	// version
#if RETAIL_COMPATIBLE_XFER_SAVE
	XferVersion currentVersion = 1;
#else
	XferVersion currentVersion = 2;
#endif
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// money value
	xfer->xferUnsignedInt( &m_money );

	if (version <= 1)
	{
		setStartingCash(m_money);
	}
	else
	{
		xfer->xferUser(m_incomeBuckets, sizeof(m_incomeBuckets));
		xfer->xferUnsignedInt(&m_currentBucket);

		m_cashPerMinute = std::accumulate(m_incomeBuckets, m_incomeBuckets + ARRAY_SIZE(m_incomeBuckets), 0u);
	}
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void Money::loadPostProcess()
{

}


// ------------------------------------------------------------------------------------------------
/** Parse a money amount for the ini file. E.g. DefaultStartingMoney = 10000 */
// ------------------------------------------------------------------------------------------------
void Money::parseMoneyAmount( INI *ini, void *instance, void *store, const void* userData )
{
  // Someday, maybe, have multiple fields like Gold:10000 Wood:1000 Tiberian:10
  Money * money = (Money *)store;
	UnsignedInt moneyAmount;
	INI::parseUnsignedInt( ini, instance, &moneyAmount, userData );
	money->setStartingCash(moneyAmount);
}
