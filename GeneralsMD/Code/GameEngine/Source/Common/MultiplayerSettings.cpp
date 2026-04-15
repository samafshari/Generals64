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

// FILE: MultiplayerSettings.cpp ///////////////////////////////////////////////////////////////////////////
// The MultiplayerSettings object
// Author: Matthew D. Campbell, January 2002
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define DEFINE_TERRAIN_LOD_NAMES
#define DEFINE_TIME_OF_DAY_NAMES

#include "Common/MultiplayerSettings.h"
#include "Common/INI.h"
#include "GameNetwork/GameInfo.h" // for PLAYERTEMPLATE_*

// PUBLIC DATA ////////////////////////////////////////////////////////////////////////////////////
MultiplayerSettings *TheMultiplayerSettings = nullptr;				///< The MultiplayerSettings singleton

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
const FieldParse MultiplayerColorDefinition::m_colorFieldParseTable[] =
{

	{ "TooltipName",	INI::parseAsciiString,	nullptr,	offsetof( MultiplayerColorDefinition, m_tooltipName ) },
	{ "RGBColor",			INI::parseRGBColor,			nullptr,	offsetof( MultiplayerColorDefinition, m_rgbValue ) },
	{ "RGBNightColor",			INI::parseRGBColor,		nullptr,	offsetof( MultiplayerColorDefinition, m_rgbValueNight ) },
	{ nullptr,					nullptr,						nullptr,						0 }

};

const FieldParse MultiplayerSettings::m_multiplayerSettingsFieldParseTable[] =
{

	{ "StartCountdownTimer",			INI::parseInt,	nullptr,	offsetof( MultiplayerSettings, m_startCountdownTimerSeconds ) },
	{ "MaxBeaconsPerPlayer",			INI::parseInt,	nullptr,	offsetof( MultiplayerSettings, m_maxBeaconsPerPlayer ) },
	{ "UseShroud",								INI::parseBool,	nullptr,	offsetof( MultiplayerSettings, m_isShroudInMultiplayer ) },
	{ "ShowRandomPlayerTemplate",	INI::parseBool,	nullptr,	offsetof( MultiplayerSettings, m_showRandomPlayerTemplate ) },
	{ "ShowRandomStartPos",				INI::parseBool,	nullptr,	offsetof( MultiplayerSettings, m_showRandomStartPos ) },
	{ "ShowRandomColor",					INI::parseBool,	nullptr,	offsetof( MultiplayerSettings, m_showRandomColor ) },

	{ nullptr,					nullptr,						nullptr,						0 }

};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
MultiplayerSettings::MultiplayerSettings()
{
	m_maxBeaconsPerPlayer = 3;
	//

	m_startCountdownTimerSeconds = 0;
	m_numColors = 0;
	m_isShroudInMultiplayer = TRUE;
	m_showRandomPlayerTemplate = TRUE;
	m_showRandomStartPos = TRUE;
	m_showRandomColor = TRUE;
	m_observerColor;
	m_randomColor;

  m_gotDefaultStartingMoney = false;
}

MultiplayerColorDefinition::MultiplayerColorDefinition()
{
	m_tooltipName.clear();
	m_rgbValue.setFromInt(0xFFFFFFFF);
	m_rgbValueNight=m_rgbValue;
	m_color = 0xFFFFFFFF;
	m_colorNight = m_color;
}

MultiplayerColorDefinition * MultiplayerSettings::getColor(Int which)
{
	if (which == PLAYERTEMPLATE_RANDOM)
	{
		return &m_randomColor;
	}
	else if (which == PLAYERTEMPLATE_OBSERVER)
	{
		return &m_observerColor;
	}
	else if (which < 0 || which >= getNumColors())
	{
		return nullptr;
	}

	return &m_colorList[which];
}

// resolveSlotColor / resolveSlotNightColor / packSlotColor are
// header-inline now — see MultiplayerSettings.h. Slot color values
// are raw 24-bit RGB ints; no palette lookup or sentinel decoding
// is needed.

//-------------------------------------------------------------------------------------------------
// Per-effect color modulation. Mirrors HLSL ApplyShaderEffect /
// launcher ShaderEffectPreview, but flattened to a single ARGB so
// the result can be pushed at any 2D widget that takes a Color.
// Spatial inputs (worldPos, normal, fresnel) collapse to time-only
// modulation here — small UI rects can't show gradients or noise,
// so each effect picks the dominant time-driven look.
//-------------------------------------------------------------------------------------------------
namespace
{
	inline Real clamp01(Real v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

	// Standard RGB↔HSV pair. Used only by the Rainbow variant.
	void rgbToHsv(Real r, Real g, Real b, Real& h, Real& s, Real& v)
	{
		Real mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
		Real mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
		v = mx;
		Real d = mx - mn;
		s = (mx <= 0.0f) ? 0.0f : (d / mx);
		if (d <= 0.0f) { h = 0.0f; return; }
		Real hh;
		if (mx == r)      hh = (g - b) / d + (g < b ? 6.0f : 0.0f);
		else if (mx == g) hh = (b - r) / d + 2.0f;
		else              hh = (r - g) / d + 4.0f;
		h = hh / 6.0f;  // 0..1
	}

	void hsvToRgb(Real h, Real s, Real v, Real& r, Real& g, Real& b)
	{
		Real i = floorf(h * 6.0f);
		Real ff = h * 6.0f - i;
		Real p = v * (1.0f - s);
		Real q = v * (1.0f - ff * s);
		Real tt = v * (1.0f - (1.0f - ff) * s);
		switch ((Int)i % 6)
		{
			case 0: r = v;  g = tt; b = p;  break;
			case 1: r = q;  g = v;  b = p;  break;
			case 2: r = p;  g = v;  b = tt; break;
			case 3: r = p;  g = q;  b = v;  break;
			case 4: r = tt; g = p;  b = v;  break;
			default:r = v;  g = p;  b = q;  break;
		}
	}
}

Color MultiplayerSettings::resolveSlotColorWithEffect(Int slotColor, Int shaderId, UnsignedInt timeMs)
{
	Color base = resolveSlotColor(slotColor);
	// Random sentinel resolves to white above; no sane animation
	// for "color isn't picked yet". Stock shader == no effect.
	if (shaderId == 0 || slotColor < 0)
		return base;

	UnsignedByte ur, ug, ub, ua;
	GameGetColorComponents(base, &ur, &ug, &ub, &ua);
	Real r = ur / 255.0f;
	Real g = ug / 255.0f;
	Real b = ub / 255.0f;
	Real t = timeMs * 0.001f;

	switch (shaderId)
	{
		case 1: // Pulse — brightness breath
		{
			Real puls = 0.55f + 0.45f * sinf(t * 3.0f);
			r *= puls; g *= puls; b *= puls;
			break;
		}
		case 2: // Rainbow — hue rotate
		{
			Real h, s, v;
			rgbToHsv(r, g, b, h, s, v);
			// Boost saturation for very desaturated colors so the
			// rotation is actually visible (a near-white slot would
			// otherwise just shimmer through pastels).
			if (s < 0.4f) s = 0.4f + s * 0.5f;
			h += t * 0.25f;     // ~4s per full revolution
			h -= floorf(h);
			hsvToRgb(h, s, v, r, g, b);
			break;
		}
		case 3: // Shimmer — fast bright flicker
		{
			Real spark = 0.85f + 0.15f * sinf(t * 14.0f) + 0.08f * sinf(t * 47.0f + 1.3f);
			r *= spark; g *= spark; b *= spark;
			break;
		}
		case 4: // Chrome — desaturate body + slow white pulse
		{
			Real gray = 0.299f * r + 0.587f * g + 0.114f * b;
			Real wPul = 0.20f + 0.15f * sinf(t * 1.5f);
			r = gray * 0.75f + r * 0.25f + wPul;
			g = gray * 0.75f + g * 0.25f + wPul;
			b = gray * 0.75f + b * 0.25f + wPul;
			break;
		}
		case 5: // Holographic — blend base with cycling iridescent triplet
		{
			Real ir = 0.5f + 0.5f * sinf(t * 1.7f);
			Real ig = 0.5f + 0.5f * sinf(t * 1.7f + 2.1f);
			Real ib = 0.5f + 0.5f * sinf(t * 1.7f + 4.2f);
			Real k = 0.55f;
			r = r * (1.0f - k) + ir * k;
			g = g * (1.0f - k) + ig * k;
			b = b * (1.0f - k) + ib * k;
			break;
		}
		case 6: // Hex camo — slow two-tone breath between dim and bright
		{
			Real mix = 0.5f + 0.5f * sinf(t * 1.2f);
			Real fac = 0.55f + 0.55f * mix;  // 0.55..1.10
			r *= fac; g *= fac; b *= fac;
			break;
		}
		case 7: // Frost — desaturate + cyan rim that pulses
		{
			Real gray = 0.299f * r + 0.587f * g + 0.114f * b;
			Real cy = 0.35f + 0.20f * sinf(t * 1.3f);
			r = gray * 0.55f + r * 0.45f;
			g = gray * 0.55f + g * 0.45f + cy * 0.30f;
			b = gray * 0.55f + b * 0.45f + cy * 0.55f;
			break;
		}
		default:
			return base;
	}

	r = clamp01(r); g = clamp01(g); b = clamp01(b);
	return GameMakeColor((UnsignedByte)(r * 255.0f),
	                     (UnsignedByte)(g * 255.0f),
	                     (UnsignedByte)(b * 255.0f),
	                     ua);
}

MultiplayerColorDefinition * MultiplayerSettings::findMultiplayerColorDefinitionByName(AsciiString name)
{
	MultiplayerColorIter iter = m_colorList.begin();

	while (iter != m_colorList.end())
	{
		if (iter->second.getTooltipName() == name)
			return &(iter->second);

		++iter;
	}

	return nullptr;
}

MultiplayerColorDefinition * MultiplayerSettings::newMultiplayerColorDefinition(AsciiString name)
{
 	MultiplayerColorDefinition tmp;
	Int numColors = getNumColors();

	m_colorList[numColors] = tmp;
	m_numColors = m_colorList.size();

	return &m_colorList[numColors];
}

void MultiplayerSettings::addStartingMoneyChoice( const Money & money, Bool isDefault )
{
  m_startingMoneyList.push_back( money );
  if ( isDefault )
  {
    DEBUG_ASSERTCRASH( !m_gotDefaultStartingMoney, ("Cannot have more than one default MultiplayerStartingMoneyChoice") );
    m_defaultStartingMoney = money;
    m_gotDefaultStartingMoney = true;
  }
}

MultiplayerColorDefinition * MultiplayerColorDefinition::operator =(const MultiplayerColorDefinition& other)
{
	m_tooltipName = other.getTooltipName();
	m_rgbValue = other.getRGBValue();
	m_color = other.getColor();
	m_rgbValueNight = other.getRGBNightValue();
	m_colorNight = other.getNightColor();

	return this;
}

void MultiplayerColorDefinition::setColor( RGBColor rgb )
{
	m_color = rgb.getAsInt() | 0xFF << 24;
}

void MultiplayerColorDefinition::setNightColor( RGBColor rgb )
{
	m_colorNight = rgb.getAsInt() | 0xFF << 24;
}

