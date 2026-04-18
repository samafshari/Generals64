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

#include "PreRTS.h"

#define DEFINE_SHADOW_NAMES

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Xfer.h"
#include "GameClient/RadiusDecal.h"
#include "GameClient/Shadow.h"
#include "GameLogic/GameLogic.h"


// ------------------------------------------------------------------------------------------------
RadiusDecalTemplate::RadiusDecalTemplate() :
	m_shadowType(SHADOW_ALPHA_DECAL),
	m_minOpacity(1.0f),
	m_maxOpacity(1.0f),
	m_opacityThrobTime(LOGICFRAMES_PER_SECOND),
	m_color(0),
	m_onlyVisibleToOwningPlayer(true),
	m_name(AsciiString::TheEmptyString)
{
}

// ------------------------------------------------------------------------------------------------
void RadiusDecalTemplate::createRadiusDecal(const Coord3D& /*pos*/, Real radius, const Player* owningPlayer, RadiusDecal& result) const
{
	result.clear();

	if (owningPlayer == nullptr)
	{
		DEBUG_CRASH(("You MUST specify a non-null owningPlayer to createRadiusDecal. (srj)"));
		return;
	}

	if (m_name.isEmpty() || radius <= 0.0f)
		return;

	result.m_empty = false;
	result.m_template = this;
}

// ------------------------------------------------------------------------------------------------
void RadiusDecalTemplate::xferRadiusDecalTemplate( Xfer *xfer )
{
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferAsciiString(&m_name);
	xfer->xferUser(&m_shadowType, sizeof(m_shadowType));
	xfer->xferReal(&m_minOpacity);
  xfer->xferReal(&m_maxOpacity);
	xfer->xferUnsignedInt(&m_opacityThrobTime);
	xfer->xferColor(&m_color);
	xfer->xferBool(&m_onlyVisibleToOwningPlayer);
}

// ------------------------------------------------------------------------------------------------
/*static*/ void RadiusDecalTemplate::parseRadiusDecalTemplate(INI* ini, void *instance, void * store, const void* /*userData*/)
{
	static const FieldParse dataFieldParse[] =
	{
		{ "Texture",										INI::parseAsciiString,				nullptr,							offsetof( RadiusDecalTemplate, m_name ) },
		{ "Style",											INI::parseBitString32,				TheShadowNames,		offsetof( RadiusDecalTemplate, m_shadowType ) },
		{ "OpacityMin",									INI::parsePercentToReal,			nullptr,							offsetof( RadiusDecalTemplate, m_minOpacity ) },
		{ "OpacityMax",									INI::parsePercentToReal,			nullptr,							offsetof( RadiusDecalTemplate, m_maxOpacity) },
		{ "OpacityThrobTime",						INI::parseDurationUnsignedInt,nullptr,							offsetof( RadiusDecalTemplate, m_opacityThrobTime ) },
		{ "Color",											INI::parseColorInt,						nullptr,							offsetof( RadiusDecalTemplate, m_color ) },
		{ "OnlyVisibleToOwningPlayer",	INI::parseBool,								nullptr,							offsetof( RadiusDecalTemplate, m_onlyVisibleToOwningPlayer ) },
		{ nullptr, nullptr, nullptr, 0 }
	};

	ini->initFromINI(store, dataFieldParse);
}

// ------------------------------------------------------------------------------------------------
RadiusDecal::RadiusDecal() :
	m_template(nullptr),
	m_empty(true)
{
}

// ------------------------------------------------------------------------------------------------
RadiusDecal::RadiusDecal(const RadiusDecal& /*that*/) :
	m_template(nullptr),
	m_empty(true)
{
	DEBUG_CRASH(("not fully implemented"));
}

// ------------------------------------------------------------------------------------------------
RadiusDecal& RadiusDecal::operator=(const RadiusDecal& that)
{
	if (this != &that)
	{
		m_template = nullptr;
		m_empty = true;
		DEBUG_CRASH(("not fully implemented"));
	}
	return *this;
}

// ------------------------------------------------------------------------------------------------
void RadiusDecal::xferRadiusDecal( Xfer *xfer )
{
	if (xfer->getXferMode() == XFER_LOAD)
	{
		clear();
	}
}

// ------------------------------------------------------------------------------------------------
void RadiusDecal::clear()
{
	m_template = nullptr;
	m_empty = true;
}

// ------------------------------------------------------------------------------------------------
RadiusDecal::~RadiusDecal()
{
	clear();
}

// ------------------------------------------------------------------------------------------------
void RadiusDecal::update()
{
}

void RadiusDecal::setOpacity( Real /*o*/ )
{
}

// ------------------------------------------------------------------------------------------------
void RadiusDecal::setPosition(const Coord3D& /*pos*/)
{
}
