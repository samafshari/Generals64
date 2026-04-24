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

// FILE: Energy.cpp /////////////////////////////////////////////////////////
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
// File name: Energy.cpp
//
// Created:   Steven Johnson, October 2001
//
// Desc:      @todo
//
//-----------------------------------------------------------------------------

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Energy.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"


//-----------------------------------------------------------------------------
Energy::Energy()
{
	m_energyProduction = 0;
	m_energyConsumption = 0;
	m_owner = nullptr;
	m_powerSabotagedTillFrame = 0;
	m_useSharedPool = FALSE;
	m_sharedTeamIndex = -1;
}

// ------------------------------------------------------------------------------------------------
// Shared-power helpers. Called by the pooled getters below.
//
// Sum-across-teammates is computed live each frame by iterating
// ThePlayerList — simple, deterministic, and saves us having to keep
// a parallel cache in sync on every objectEnteringInfluence /
// objectLeavingInfluence / addPowerBonus call.
//
// Each teammate's own sabotage frame is honored individually: if a
// teammate is sabotaged, their production contributes 0. Because
// setPowerSabotagedTillFrame propagates to every teammate in a pool,
// "one sabotaged → all contribute 0" — which matches the feature
// spec ("sabotage applies to all members of the same team").
// ------------------------------------------------------------------------------------------------
static Bool isEnergySabotaged(const Energy *e)
{
	return TheGameLogic && TheGameLogic->getFrame() < e->getPowerSabotagedTillFrame();
}

Int Energy::getTeamTotalProduction() const
{
	if (!m_useSharedPool || !ThePlayerList)
		return isEnergySabotaged(this) ? 0 : m_energyProduction;

	Int total = 0;
	const Int count = ThePlayerList->getPlayerCount();
	for (Int i = 0; i < count; ++i)
	{
		Player *p = ThePlayerList->getNthPlayer(i);
		if (!p) continue;
		const Energy *e = p->getEnergy();
		if (!e || !e->m_useSharedPool || e->m_sharedTeamIndex != m_sharedTeamIndex)
			continue;
		if (isEnergySabotaged(e))
			continue;
		total += e->m_energyProduction;
	}
	return total;
}

Int Energy::getTeamTotalConsumption() const
{
	if (!m_useSharedPool || !ThePlayerList)
		return m_energyConsumption;

	Int total = 0;
	const Int count = ThePlayerList->getPlayerCount();
	for (Int i = 0; i < count; ++i)
	{
		Player *p = ThePlayerList->getNthPlayer(i);
		if (!p) continue;
		const Energy *e = p->getEnergy();
		if (!e || !e->m_useSharedPool || e->m_sharedTeamIndex != m_sharedTeamIndex)
			continue;
		// Consumption is NOT zeroed on sabotage — enemies can still
		// drain power from your pool even while your plants are out,
		// matching the "whole team loses power" feel the feature asks
		// for. This mirrors the solo branch where hasSufficientPower
		// returns FALSE when sabotaged regardless of consumption.
		total += e->m_energyConsumption;
	}
	return total;
}

//-----------------------------------------------------------------------------
Int Energy::getProduction() const
{
	if (m_useSharedPool)
		return getTeamTotalProduction();

	if( TheGameLogic->getFrame() < m_powerSabotagedTillFrame )
	{
		//Power sabotaged, therefore no power.
		return 0;
	}
	return m_energyProduction;
}

//-----------------------------------------------------------------------------
Int Energy::getConsumption() const
{
	// In shared-power mode, consumption is the team-wide sum — every
	// teammate's load counts against the shared pool. Was previously
	// inline in Energy.h returning m_energyConsumption directly, which
	// was the source of the "my bar green but friend's units offline"
	// bug: the HUD power bar was computing a per-player supply ratio
	// while hasSufficientPower was computing the team-wide one, so
	// teammates with more load than local production got stranded in
	// brownout state until their OWN reactor was upgraded.
	if (m_useSharedPool)
		return getTeamTotalConsumption();

	return m_energyConsumption;
}

//-----------------------------------------------------------------------------
Real Energy::getEnergySupplyRatio() const
{
	DEBUG_ASSERTCRASH(m_energyProduction >= 0 && m_energyConsumption >= 0, ("neg Energy numbers"));

	if (m_useSharedPool)
	{
		const Int teamProd = getTeamTotalProduction();
		const Int teamCons = getTeamTotalConsumption();
		if (teamCons == 0)
			return (Real)teamProd;
		return (Real)teamProd / (Real)teamCons;
	}

	if( TheGameLogic->getFrame() < m_powerSabotagedTillFrame )
	{
		//Power sabotaged, therefore no power, no ratio.
		return 0.0f;
	}

	if (m_energyConsumption == 0)
		return (Real)m_energyProduction;

	return (Real)m_energyProduction / (Real)m_energyConsumption;
}

//-------------------------------------------------------------------------------------------------
Bool Energy::hasSufficientPower() const
{
	if (m_useSharedPool)
	{
		// Team pool is sufficient when team production >= team consumption.
		// Individual sabotage already zeroes contributors, so no separate
		// sabotage check needed here — if every teammate is sabotaged
		// (propagation guarantees they all are), team production is 0
		// and this returns FALSE as soon as any consumer exists.
		return getTeamTotalProduction() >= getTeamTotalConsumption();
	}

	if( TheGameLogic->getFrame() < m_powerSabotagedTillFrame )
	{
		//Power sabotaged, therefore no power.
		return FALSE;
	}
	return m_energyProduction >= m_energyConsumption;
}

//-------------------------------------------------------------------------------------------------
void Energy::adjustPower(Int powerDelta, Bool adding)
{
	if (powerDelta == 0) {
		return;
	}

	if (powerDelta > 0) {
		if (adding) {
			addProduction(powerDelta);
		} else {
			addProduction(-powerDelta);
		}
	} else {
		// Seems a little odd, however, consumption is reversed. Negative power is positive consumption.
		if (adding) {
			addConsumption(-powerDelta);
		} else {
			addConsumption(powerDelta);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** new 'obj' will now add/subtract from this energy construct */
//-------------------------------------------------------------------------------------------------
void Energy::objectEnteringInfluence( Object *obj )
{

	// sanity
	if( obj == nullptr )
		return;

	// get the amount of energy this object produces or consumes
	Int energy = obj->getTemplate()->getEnergyProduction();

	// adjust energy
	if( energy < 0 )
		addConsumption( -energy );
	else if( energy > 0 )
		addProduction( energy );

	// sanity
	DEBUG_ASSERTCRASH( m_energyProduction >= 0 && m_energyConsumption >= 0,
										 ("Energy - Negative Energy numbers, Produce=%d Consume=%d\n",
										 m_energyProduction, m_energyConsumption) );

}

//-------------------------------------------------------------------------------------------------
/** 'obj' will now no longer add/subtrack from this energy construct */
//-------------------------------------------------------------------------------------------------
void Energy::objectLeavingInfluence( Object *obj )
{

	// sanity
	if( obj == nullptr )
		return;

	// get the amount of energy this object produces or consumes
	Int energy = obj->getTemplate()->getEnergyProduction();

	// adjust energy
	if( energy < 0 )
		addConsumption( energy );
	else if( energy > 0 )
		addProduction( -energy );

	// sanity
	DEBUG_ASSERTCRASH( m_energyProduction >= 0 && m_energyConsumption >= 0,
										 ("Energy - Negative Energy numbers, Produce=%d Consume=%d\n",
										 m_energyProduction, m_energyConsumption) );

}

//-------------------------------------------------------------------------------------------------
/** Adds an energy bonus to the player's pool of energy when the "Control Rods" upgrade
		is made to the American Cold Fusion Plant */
//-------------------------------------------------------------------------------------------------
void Energy::addPowerBonus( Object *obj )
{

	// sanity
	if( obj == nullptr )
		return;

	addProduction(obj->getTemplate()->getEnergyBonus());

	// sanity
	DEBUG_ASSERTCRASH( m_energyProduction >= 0 && m_energyConsumption >= 0,
										 ("Energy - Negative Energy numbers, Produce=%d Consume=%d\n",
										 m_energyProduction, m_energyConsumption) );

}

// ------------------------------------------------------------------------------------------------
/** Removed an energy bonus */
// ------------------------------------------------------------------------------------------------
void Energy::removePowerBonus( Object *obj )
{

	// sanity
	if( obj == nullptr )
		return;

#if !RETAIL_COMPATIBLE_CRC
	if ( obj->isDisabled() )
		return;
#endif

	addProduction( -obj->getTemplate()->getEnergyBonus() );

	// sanity
	DEBUG_ASSERTCRASH( m_energyProduction >= 0 && m_energyConsumption >= 0,
										 ("Energy - Negative Energy numbers, Produce=%d Consume=%d\n",
										 m_energyProduction, m_energyConsumption) );

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Private functions
// ------------------------------------------------------------------------------------------------
void Energy::addProduction(Int amt)
{
	m_energyProduction += amt;

	if( m_owner == nullptr )
		return;

	// A repeated Brownout signal does nothing bad, and we need to handle more than just edge cases.
	// Like low power, now even more low power, refresh disable.
	const Bool brownedOut = !hasSufficientPower();
	if (m_useSharedPool)
		broadcastBrownOutToTeam(brownedOut);
	else
		m_owner->onPowerBrownOutChange( brownedOut );
}

// ------------------------------------------------------------------------------------------------
void Energy::addConsumption(Int amt)
{
	m_energyConsumption += amt;

	if( m_owner == nullptr )
		return;

	const Bool brownedOut = !hasSufficientPower();
	if (m_useSharedPool)
		broadcastBrownOutToTeam(brownedOut);
	else
		m_owner->onPowerBrownOutChange( brownedOut );
}

// ------------------------------------------------------------------------------------------------
void Energy::broadcastBrownOutToTeam(Bool brownedOut)
{
	// Fire the brownout callback on every teammate's Player so their
	// radar, superweapons, base defenses etc all flip state in lockstep
	// with the team pool's supply/demand. Caller guarantees we're in
	// a pool; we still defensively early-out if the player list is
	// unavailable (shouldn't happen mid-game).
	if (!ThePlayerList) return;
	const Int count = ThePlayerList->getPlayerCount();
	for (Int i = 0; i < count; ++i)
	{
		Player *p = ThePlayerList->getNthPlayer(i);
		if (!p) continue;
		const Energy *e = p->getEnergy();
		if (!e || !e->m_useSharedPool || e->m_sharedTeamIndex != m_sharedTeamIndex)
			continue;
		p->onPowerBrownOutChange(brownedOut);
	}
}

// ------------------------------------------------------------------------------------------------
void Energy::setPowerSabotagedTillFrame( UnsignedInt frame )
{
	// Idempotent short-circuit: crucial for the pooled propagation path
	// so a setFrame call starting on any teammate visits each other
	// teammate exactly once and then early-returns instead of
	// recursing. Also keeps Player::update's "clear to 0" loop cheap
	// when it fires on every teammate every frame.
	if (m_powerSabotagedTillFrame == frame)
		return;

	m_powerSabotagedTillFrame = frame;

	if (m_useSharedPool && ThePlayerList)
	{
		// Propagate to every teammate's Energy so "one teammate
		// sabotaged → whole team is out of power" as per the feature
		// spec. Each teammate that already has this frame value
		// early-returns at the top of this function, so propagation
		// terminates after one pass through the team.
		const Int count = ThePlayerList->getPlayerCount();
		for (Int i = 0; i < count; ++i)
		{
			Player *p = ThePlayerList->getNthPlayer(i);
			if (!p) continue;
			Energy *e = p->getEnergy();
			if (!e || e == this) continue;
			if (!e->m_useSharedPool || e->m_sharedTeamIndex != m_sharedTeamIndex)
				continue;
			e->setPowerSabotagedTillFrame(frame);
		}

		// Broadcast a brownout change now that the team's effective
		// production has shifted. Without this, buildings that only
		// react on addProduction/addConsumption wouldn't see the
		// sabotage-induced drop until the next building change.
		broadcastBrownOutToTeam(!hasSufficientPower());
	}
}

// ------------------------------------------------------------------------------------------------
void Energy::setSharedTeamBinding( Bool enabled, Int teamNumber )
{
	// Symmetric with Money::setSharedPoolBinding — this just flips
	// the routing flag; there's no balance to migrate because team
	// production / consumption is derived from the per-player values
	// each time a getter runs.
	const Bool shouldBind = enabled && teamNumber >= 0;
	m_useSharedPool = shouldBind;
	m_sharedTeamIndex = shouldBind ? teamNumber : -1;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void Energy::crc( Xfer *xfer )
{

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void Energy::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// It is actually incorrect to save these, as they are reconstructed when the buildings are loaded
	// I need to version though so old games will load wrong rather than crashing

	// production
	if( version < 2 )
		xfer->xferInt( &m_energyProduction );

	// consumption
	if( version < 2 )
		xfer->xferInt( &m_energyConsumption );

	// owning player index
	Int owningPlayerIndex;
	if( xfer->getXferMode() == XFER_SAVE )
		owningPlayerIndex = m_owner->getPlayerIndex();
	xfer->xferInt( &owningPlayerIndex );
	m_owner = ThePlayerList->getNthPlayer( owningPlayerIndex );

	//Sabotage
	if( version >= 3 )
	{
		xfer->xferUnsignedInt( &m_powerSabotagedTillFrame );
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void Energy::loadPostProcess()
{

}
