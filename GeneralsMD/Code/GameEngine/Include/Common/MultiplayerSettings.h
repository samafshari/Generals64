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

// FILE: MultiplayerSettings.h /////////////////////////////////////////////////////////////////////////////
// Settings common to multiplayer games
// Author: Matthew D. Campbell, January 2002
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/Color.h"
#include "Common/Money.h"

// FORWARD DECLARATIONS ///////////////////////////////////////////////////////////////////////////
struct FieldParse;
class MultiplayerSettings;

// PUBLIC /////////////////////////////////////////////////////////////////////////////////////////

class MultiplayerColorDefinition
{
public:
	MultiplayerColorDefinition();
	//-----------------------------------------------------------------------------------------------
	static const FieldParse m_colorFieldParseTable[];		///< the parse table for INI definition
	const FieldParse *getFieldParse() const { return m_colorFieldParseTable; }

	AsciiString getTooltipName() const { return m_tooltipName; };
	RGBColor getRGBValue() const { return m_rgbValue; };
	RGBColor getRGBNightValue() const { return m_rgbValueNight; };
	Color getColor() const { return m_color; }
	Color getNightColor() const { return m_colorNight; }
	void setColor( RGBColor rgb );
	void setNightColor( RGBColor rgb );

	MultiplayerColorDefinition * operator =(const MultiplayerColorDefinition& other);

private:
	AsciiString m_tooltipName;	///< tooltip name for color combo box (AsciiString to pass to TheGameText->fetch())
	RGBColor m_rgbValue;						///< RGB color value
	Color m_color;
	RGBColor m_rgbValueNight;						///< RGB color value
	Color m_colorNight;
};

typedef std::map<Int, MultiplayerColorDefinition> MultiplayerColorList;
typedef std::map<Int, MultiplayerColorDefinition>::iterator MultiplayerColorIter;

// A list of values to display in the starting money dropdown
typedef std::vector< Money > MultiplayerStartingMoneyList;

//-------------------------------------------------------------------------------------------------
/** Multiplayer Settings container class
  *	Defines multiplayer settings */
//-------------------------------------------------------------------------------------------------
class MultiplayerSettings : public SubsystemInterface
{
public:

	MultiplayerSettings();

	virtual void init() { }
	virtual void update() { }
	virtual void reset() { }

	//-----------------------------------------------------------------------------------------------
	static const FieldParse m_multiplayerSettingsFieldParseTable[];		///< the parse table for INI definition
	const FieldParse *getFieldParse() const { return m_multiplayerSettingsFieldParseTable; }

	// Color management --------------------
	MultiplayerColorDefinition * findMultiplayerColorDefinitionByName(AsciiString name);
	MultiplayerColorDefinition * newMultiplayerColorDefinition(AsciiString name);

	Int getStartCountdownTimerSeconds() { return m_startCountdownTimerSeconds; }
	Int getMaxBeaconsPerPlayer() { return m_maxBeaconsPerPlayer; }
	Bool isShroudInMultiplayer() { return m_isShroudInMultiplayer; }
	Bool showRandomPlayerTemplate() { return m_showRandomPlayerTemplate; }
	Bool showRandomStartPos() { return m_showRandomStartPos; }
	Bool showRandomColor() { return m_showRandomColor; }

	Int getNumColors()
	{
		if (m_numColors == 0) {
			m_numColors = m_colorList.size();
		}
		return m_numColors;
	}
	MultiplayerColorDefinition * getColor(Int which);

	// ── Slot color storage model ──────────────────────────────────
	//
	// As of the launcher revamp, slot color values are RAW 24-bit
	// RGB integers (0x00RRGGBB), NOT indices into m_colorList. The
	// stock multiplayer.ini palette is still loaded — but now it
	// only feeds the in-game lobby's combo-box presets and the
	// random-color fallback. Once a slot has a color, it's stored
	// as RGB regardless of which preset (or custom value) it came
	// from.
	//
	// Special sentinel: -1 still means "random — pick at game
	// start" and is resolved by populateRandomSideAndColor().

	/// Pack a 0x00RRGGBB value into a slot color int (just masks
	/// off any spurious high-byte alpha; the slot field is plain
	/// RGB so no sentinel bit is set).
	static Int packSlotColor(UnsignedInt rgb)
	{
		return (Int)(rgb & 0x00FFFFFFu);
	}

	/// Convert a slot color int to a renderer-ready ARGB Color
	/// (alpha forced to 0xFF). Returns white as a defensive default
	/// if asked to resolve the random sentinel — random colors
	/// must be replaced with a real RGB before any rendering site
	/// queries them.
	static Color resolveSlotColor(Int slotColor)
	{
		if (slotColor < 0)
			return (Color)0xFFFFFFFFu;
		return (Color)(0xFF000000u | ((UnsignedInt)slotColor & 0x00FFFFFFu));
	}

	/// Slot night color. With raw-RGB storage we no longer carry a
	/// dedicated night variant per slot (the old palette did, but
	/// the variant was tied to a preset index). Just return the
	/// same RGB so units don't go invisible during a TOD shift.
	static Color resolveSlotNightColor(Int slotColor)
	{
		return resolveSlotColor(slotColor);
	}

	/// Resolve the slot color with the player's profile shader effect
	/// applied as a time-driven color modulation. Mirrors the spirit
	/// of HLSL ApplyShaderEffect / launcher ShaderEffectPreview but
	/// collapsed to a single ARGB so it can be pushed at any 2D UI
	/// element (LAN-lobby color swatch, in-game Diplomacy text, etc).
	/// shaderId 0 / random-sentinel slot colors short-circuit to the
	/// stock resolveSlotColor result.
	static Color resolveSlotColorWithEffect(Int slotColor, Int shaderId, UnsignedInt timeMs);


  const Money & getDefaultStartingMoney() const
  {
    DEBUG_ASSERTCRASH( m_gotDefaultStartingMoney, ("You must specify a default starting money amount in multiplayer.ini") );
    return m_defaultStartingMoney;
  }

  const MultiplayerStartingMoneyList & getStartingMoneyList() const { return m_startingMoneyList; }

  void addStartingMoneyChoice( const Money & money, Bool isDefault );

private:
	Int m_initialCreditsMin;
	Int m_initialCreditsMax;
	Int m_startCountdownTimerSeconds;
	Int m_maxBeaconsPerPlayer;
	Bool m_isShroudInMultiplayer;
	Bool m_showRandomPlayerTemplate;
	Bool m_showRandomStartPos;
	Bool m_showRandomColor;

	MultiplayerColorList m_colorList;
	Int m_numColors;
	MultiplayerColorDefinition m_observerColor;
	MultiplayerColorDefinition m_randomColor;
  MultiplayerStartingMoneyList      m_startingMoneyList;
  Money                             m_defaultStartingMoney;
  Bool                              m_gotDefaultStartingMoney;
};

// singleton
extern MultiplayerSettings *TheMultiplayerSettings;
