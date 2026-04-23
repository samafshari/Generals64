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

// FILE: Energy.h ////////////////////////////////////////////////////////////
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
// File name:  Energy.h
//
// Created:    Steven Johnson, October 2001
//
// Desc:			 @todo
//
//-----------------------------------------------------------------------------

#pragma once

// INCLUDES /////////////////////////////////////////////////////////////////////////////////////
#include "Common/Snapshot.h"

// ----------------------------------------------------------------------------------------------

class Player;
class Object;

// ----------------------------------------------------------------------------------------------
/**
	This class is used to encapsulate the Player's energy use and production.
	for consistent nomenclature, we'll arbitrarily call energy units "kilowatts"
	(though that may have no bearing on reality).
*/
class Energy : public Snapshot
{

public:

	Energy();

	// reset energy information to base values.
	void init( Player *owner)
	{
		m_energyProduction = 0;
		m_energyConsumption = 0;
		m_powerSabotagedTillFrame = 0;
		m_owner = owner;
		// Pool binding is NOT reset here on purpose — it's set by
		// GameLogic::startNewGame after the player list is built,
		// which runs AFTER Player::init (which calls this).
	}

	/// return current energy production in kilowatts
	Int getProduction() const;

	/// return current energy consumption in kilowatts
	Int getConsumption() const { return m_energyConsumption; }

	Bool hasSufficientPower() const;

	// If adding is false, we're supposed to be removing this.
	void adjustPower(Int powerDelta, Bool adding);

	/// new 'obj' will now add/subtract from this energy construct
	void objectEnteringInfluence( Object *obj );

	/// 'obj' will now no longer add/subtrack from this energy construct
	void objectLeavingInfluence( Object *obj );

	/** Adds an energy bonus to the player's pool if the power bonus status bit is set */
	void addPowerBonus( Object *obj );
	void removePowerBonus( Object *obj );

	// When pool-bound, setPowerSabotagedTillFrame propagates the frame
	// to every teammate's Energy so a single sabotage crate affects the
	// whole team — per the shared-power feature spec. Out-of-line so
	// the propagation loop + idempotency guard can live in Energy.cpp
	// without pulling PlayerList.h into every consumer of Energy.h.
	void setPowerSabotagedTillFrame( UnsignedInt frame );
	UnsignedInt getPowerSabotagedTillFrame() const { return m_powerSabotagedTillFrame; }

	// Bind this Energy to the per-team shared power pool. Called from
	// GameLogic::startNewGame after the player roster is built.
	//
	//   enabled    : GameInfo::isSharedTeamPower
	//   teamNumber : GameSlot::getTeamNumber(); -1 = solo
	//
	// When enabled && teamNumber >= 0, subsequent calls to
	// getProduction / getConsumption / hasSufficientPower /
	// getEnergySupplyRatio return team-wide sums instead of this
	// player's individual values, and setPowerSabotagedTillFrame
	// propagates to the whole team. m_energyProduction /
	// m_energyConsumption on each Energy still accumulate from that
	// player's own buildings (via objectEnteringInfluence etc).
	void setSharedTeamBinding( Bool enabled, Int teamNumber );
	Bool isSharedPoolBound() const { return m_useSharedPool; }
	Int  getSharedTeamIndex() const { return m_sharedTeamIndex; }

	/**
		return the percentage of energy needed that we actually produce, as a 0.0 ... 1.0 fraction.
	*/
	Real getEnergySupplyRatio() const;

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

	void addProduction(Int amt);
	void addConsumption(Int amt);

	// Team-sum helpers for the pooled getters. Walk ThePlayerList once
	// and add up every teammate's raw m_energyProduction / Consumption
	// (factoring each teammate's sabotage state for production).
	Int getTeamTotalProduction() const;
	Int getTeamTotalConsumption() const;

	// Fire onPowerBrownOutChange on every teammate in the pool — used
	// by addProduction / addConsumption / setPowerSabotagedTillFrame so
	// the whole team reacts together when the pool flips brownout state.
	void broadcastBrownOutToTeam(Bool brownedOut);

private:

	Int		m_energyProduction;		///< level of energy production, in kw
	Int		m_energyConsumption;	///< level of energy consumption, in kw
	UnsignedInt m_powerSabotagedTillFrame; ///< If power is sabotaged, the frame will be greater than now.
	Player *m_owner;						///< Tight pointer to the Player I am intrinsic to.
	Bool   m_useSharedPool;			///< TRUE when bound to a team power pool (see setSharedTeamBinding)
	Int    m_sharedTeamIndex;		///< GameSlot team number when m_useSharedPool is TRUE; unused otherwise
};
