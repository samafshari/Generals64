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
#include "Inspector/Inspector.h"


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
void RadiusDecalTemplate::createRadiusDecal(const Coord3D& pos, Real radius, const Player* owningPlayer, RadiusDecal& result) const
{
	result.clear();

	if (owningPlayer == nullptr)
	{
		DEBUG_CRASH(("You MUST specify a non-null owningPlayer to createRadiusDecal. (srj)"));
		return;
	}

	if (m_name.isEmpty() || radius <= 0.0f)
	{
		Inspector::Log("[RadiusDecal] create SKIP name='%s' radius=%.1f",
			m_name.str() ? m_name.str() : "(null)", (double)radius);
		return;
	}

	result.m_empty = false;
	result.m_template = this;

	if (!TheProjectedShadowManager)
	{
		Inspector::Log("[RadiusDecal] create FAIL TheProjectedShadowManager=null name='%s'", m_name.str());
		return;
	}

	Shadow::ShadowTypeInfo info;
	memset(&info, 0, sizeof(info));
	const char* name = m_name.str();
	size_t len = strlen(name);
	if (len >= sizeof(info.m_ShadowName))
		len = sizeof(info.m_ShadowName) - 1;
	memcpy(info.m_ShadowName, name, len);
	info.m_ShadowName[len] = '\0';
	info.m_type = m_shadowType;
	info.allowUpdates = false;
	// allowWorldAlign=false makes queueDecal flatten the footprint to the
	// tallest terrain cell under it, so the ring shows as a single plane
	// above the highest point rather than diving into valleys.
	info.allowWorldAlign = false;
	info.m_sizeX = radius * 2.0f;
	info.m_sizeY = radius * 2.0f;
	info.m_offsetX = 0.0f;
	info.m_offsetY = 0.0f;

	result.m_shadow = TheProjectedShadowManager->addDecal(&info);
	if (result.m_shadow)
	{
		result.m_shadow->setSize(info.m_sizeX, info.m_sizeY);
		result.m_shadow->setColor((UnsignedInt)m_color);
		result.m_shadow->setOpacity((Int)(m_minOpacity * 255.0f));
		result.m_shadow->setPosition(pos.x, pos.y, pos.z);

		// Visibility policy: the decal must still exist (isEmpty==false) so
		// net sync matches across players; we just hide it when the local
		// player isn't the owner.
		Bool hidden = false;
		if (m_onlyVisibleToOwningPlayer
			&& ThePlayerList
			&& ThePlayerList->getLocalPlayer() != owningPlayer)
		{
			result.m_shadow->enableShadowInvisible(true);
			hidden = true;
		}
		Inspector::Log("[RadiusDecal] create OK name='%s' radius=%.1f size=%.1fx%.1f type=0x%X hidden=%d",
			m_name.str(), (double)radius, (double)info.m_sizeX, (double)info.m_sizeY,
			(unsigned)info.m_type, hidden ? 1 : 0);
	}
	else
	{
		Inspector::Log("[RadiusDecal] create FAIL addDecal returned null name='%s'", m_name.str());
	}
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
	m_shadow(nullptr),
	m_empty(true)
{
}

// ------------------------------------------------------------------------------------------------
RadiusDecal::RadiusDecal(const RadiusDecal& /*that*/) :
	m_template(nullptr),
	m_shadow(nullptr),
	m_empty(true)
{
	DEBUG_CRASH(("not fully implemented"));
}

// ------------------------------------------------------------------------------------------------
RadiusDecal& RadiusDecal::operator=(const RadiusDecal& that)
{
	if (this != &that)
	{
		clear();
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
	if (m_shadow)
	{
		m_shadow->release();
		m_shadow = nullptr;
	}
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
	if (!m_shadow || !m_template || !TheGameLogic)
		return;

	const UnsignedInt throb = m_template->m_opacityThrobTime;
	if (throb == 0)
	{
		m_shadow->setOpacity((Int)(m_template->m_maxOpacity * 255.0f));
		return;
	}

	// Triangle wave between min and max over throbTime frames.
	const UnsignedInt frame = TheGameLogic->getFrame() % throb;
	Real phase = (Real)frame / (Real)throb;		// 0..1
	Real tri = 1.0f - (Real)fabs(2.0f * phase - 1.0f);	// 0..1..0
	const Real minO = m_template->m_minOpacity;
	const Real maxO = m_template->m_maxOpacity;
	const Real o = minO + (maxO - minO) * tri;
	m_shadow->setOpacity((Int)(o * 255.0f));
}

void RadiusDecal::setOpacity( Real o )
{
	if (!m_shadow)
		return;
	if (o < 0.0f) o = 0.0f;
	else if (o > 1.0f) o = 1.0f;
	m_shadow->setOpacity((Int)(o * 255.0f));
}

// ------------------------------------------------------------------------------------------------
void RadiusDecal::setPosition(const Coord3D& pos)
{
	if (m_shadow)
		m_shadow->setPosition(pos.x, pos.y, pos.z);
}
