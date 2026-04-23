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

// FILE: Money.h ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:    RTS3
//
// File name:  Money.h
//
// Created:    Steven Johnson, October 2001
//
// Desc:			 @todo
//
//-----------------------------------------------------------------------------

#pragma once

#include "Lib/BaseType.h"
#include "Common/Debug.h"
#include "Common/Snapshot.h"

class AudioEventRTS;

// ----------------------------------------------------------------------------------------------
/**
	How much "money" (Tiberium, Gems, Magic Resource Boxes, whatever) the Player has.
	This is currently a Very Simple Class but is encapsulated
	in anticipation of future expansion.
*/
class Money : public Snapshot
{

public:

	Money() : m_playerIndex(0), m_useSharedPool(FALSE), m_sharedTeamIndex(-1)
	{
		init();
	}

	void init()
	{
		setStartingCash(0);
	}

	UnsignedInt countMoney() const
	{
		// Defined inline in Money.cpp's translation unit via an
		// extern helper — keeps the file-scope s_sharedPool array
		// hidden from every TU that includes Money.h.
		return m_useSharedPool ? getSharedPoolForTeam(m_sharedTeamIndex) : m_money;
	}

	/// returns the actual amount withdrawn, which may be less than you want. (sorry, can't go into debt...)
	UnsignedInt withdraw(UnsignedInt amountToWithdraw, Bool playSound = TRUE);
	void deposit(UnsignedInt amountToDeposit, Bool playSound = TRUE, Bool trackIncome = TRUE);

	// Called by Player::killPlayer when a player is eliminated. In SOLO
	// mode this zeroes m_money (matches the retail "force $$$ to 0 on
	// death" behavior). In SHARED-MONEY mode it's a deliberate no-op:
	// per the feature spec the dead player's contribution stays in the
	// team pool for surviving teammates to spend. Income tracking stays
	// as-is either way so the dead player's stats line is preserved.
	void onPlayerKilled();

	void setStartingCash(UnsignedInt amount);
	void updateIncomeBucket();
	UnsignedInt getCashPerMinute() const;

	void setPlayerIndex(Int ndx) { m_playerIndex = ndx; }

	// Bind this Money to the per-team shared pool. Called from
	// GameLogic::startNewGame after the player roster is built, and
	// again from SkirmishGameInfo::loadPostProcess on save-load.
	//
	//   enabled       : game-wide flag from GameInfo::isSharedTeamMoney
	//   teamNumber    : the player's GameSlot::getTeamNumber(); -1 = no team
	//
	// When enabled && teamNumber >= 0, subsequent withdraw / deposit /
	// countMoney operations go to the shared pool at s_sharedPool[teamNumber]
	// instead of m_money. Income tracking (m_incomeBuckets / m_cashPerMinute)
	// stays per-player so individual cash-per-minute and AcademyStats
	// continue to record the player's contribution even though the cash
	// itself lives in the team pool.
	void setSharedPoolBinding( Bool enabled, Int teamNumber );
	Bool isSharedPoolBound() const { return m_useSharedPool; }

	// Shared-pool static accessors (for GameInfo save/load xfer and for
	// startup bookkeeping). The pool is keyed by GameSlot team number
	// which is validated to [-1, MAX_SLOTS/2) elsewhere; the array is
	// sized to MAX_SLOTS for headroom.
	static void resetAllSharedPools();
	static UnsignedInt getSharedPoolForTeam( Int teamNumber );
	static void setSharedPoolForTeam( Int teamNumber, UnsignedInt amount );

  static void parseMoneyAmount( INI *ini, void *instance, void *store, const void* userData );

  // Does the amount of this == the amount of that (compare everything except m_playerIndex)
  Bool amountEqual( const Money & that ) const
  {
    return m_money == that.m_money;
  }

protected:

	void triggerAudioEvent(const AudioEventRTS& audioEvent);

	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

private:

	UnsignedInt m_money;	///< amount of money
	Int m_playerIndex;	///< what is my player index?
	UnsignedInt m_incomeBuckets[60];	///< circular buffer of 60 seconds for income tracking
	UnsignedInt m_currentBucket;
	UnsignedInt m_cashPerMinute;
	Bool m_useSharedPool;	///< route withdraw/deposit/countMoney through s_sharedPool[m_sharedTeamIndex]
	Int  m_sharedTeamIndex;	///< GameSlot team number when m_useSharedPool is TRUE; unused otherwise
};
