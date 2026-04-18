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

#pragma once

#ifndef __SHADOW_H_
#define __SHADOW_H_

// shadow bit flags, keep this in sync with TheShadowNames
enum ShadowType : Int
{
	SHADOW_NONE											=	0x00000000,
	SHADOW_DECAL										=	0x00000001,		// shadow decal applied via modulate blend
	SHADOW_VOLUME										=	0x00000002,
	SHADOW_PROJECTION								=	0x00000004,
	SHADOW_DYNAMIC_PROJECTION				= 0x00000008,		// extra setting for shadows which need dynamic updates
	SHADOW_DIRECTIONAL_PROJECTION		= 0x00000010,		// extra setting for shadow decals that rotate with sun direction
	SHADOW_ALPHA_DECAL							= 0x00000020,		// not really for shadows but for other decal uses. Alpha blended.
	SHADOW_ADDITIVE_DECAL						= 0x00000040		// not really for shadows but for other decal uses. Additive blended.
};
#ifdef DEFINE_SHADOW_NAMES
static const char* const TheShadowNames[] =
{
	"SHADOW_DECAL",
	"SHADOW_VOLUME",
	"SHADOW_PROJECTION",
	"SHADOW_DYNAMIC_PROJECTION",
	"SHADOW_DIRECTIONAL_PROJECTION",
	"SHADOW_ALPHA_DECAL",
	"SHADOW_ADDITIVE_DECAL",
	nullptr
};
#endif  // end DEFINE_SHADOW_NAMES

// Maximum shadow casting light sources in scene. Matches original: support
// for more than 1 was dropped from most of the original code, so most shadow
// arrays in the managers are sized to this.
#define MAX_SHADOW_LIGHTS 1

class RenderObjClass;		// forward reference
class Drawable;			// forward reference

// Interface to all shadow objects — individual shadow instance held by a
// DrawModule. Each concrete manager returns derived instances.
class Shadow
{
public:
	struct ShadowTypeInfo
	{
		char			m_ShadowName[64];	// when set, overrides the default model shadow (used mostly for Decals).
		ShadowType	m_type;				// type of shadow
		Bool			allowUpdates;		// whether to update the shadow image when object/light moves.
		Bool			allowWorldAlign;	// whether to align shadow to world geometry or draw as horizontal decal.
		Real			m_sizeX;			// world size of decal projection
		Real			m_sizeY;			// world size of decal projection
		Real			m_offsetX;			// world shift along x axis
		Real			m_offsetY;			// world shift along y axis
	};

	Shadow(void)
		: m_isEnabled(true)
		, m_isInvisibleEnabled(false)
		, m_opacity(0x000000ff)
		, m_color(0xffffffff)
		, m_type(SHADOW_NONE)
		, m_diffuse(0xffffffff)
		, m_x(0), m_y(0), m_z(0)
		, m_oowDecalSizeX(0), m_oowDecalSizeY(0)
		, m_decalSizeX(0), m_decalSizeY(0)
		, m_localAngle(0.0f)
	{ }

	virtual ~Shadow() { }

	// If set, suppresses rendering regardless of enableShadowRender(). Used by the shroud.
	void enableShadowInvisible(Bool isEnabled);
	void enableShadowRender(Bool isEnabled);
	Bool isRenderEnabled(void) { return m_isEnabled; }
	Bool isInvisibleEnabled(void) { return m_isInvisibleEnabled; }

	// Release this shadow back to its owning manager.
	virtual void release(void) = 0;

	void setOpacity(Int value);
	void setColor(UnsignedInt value);
	void setAngle(Real angle);
	void setPosition(Real x, Real y, Real z);

	void setSize(Real sizeX, Real sizeY)
	{
		m_decalSizeX = sizeX;
		m_decalSizeY = sizeY;
		m_oowDecalSizeX = (sizeX == 0) ? 0 : (1.0f / sizeX);
		m_oowDecalSizeY = (sizeY == 0) ? 0 : (1.0f / sizeY);
	}

protected:
	Bool				m_isEnabled;
	Bool				m_isInvisibleEnabled;
	UnsignedInt	m_opacity;		// 0 (transparent) to 255 (opaque)
	UnsignedInt	m_color;		// ARGB, alpha ignored
	ShadowType	m_type;
	Int					m_diffuse;		// diffuse color used to tint/fade shadow
	Real				m_x, m_y, m_z;	// world position of shadow center when not bound to robj/drawable
	Real				m_oowDecalSizeX;
	Real				m_oowDecalSizeY;
	Real				m_decalSizeX;
	Real				m_decalSizeY;
	Real				m_localAngle;	// rotation around z-axis of shadow image when not bound to robj/drawable
};

inline void Shadow::enableShadowRender(Bool isEnabled)
{
	m_isEnabled = isEnabled;
}

inline void Shadow::enableShadowInvisible(Bool isEnabled)
{
	m_isInvisibleEnabled = isEnabled;
}

inline void Shadow::setOpacity(Int value)
{
	m_opacity = value;

	if (m_type & SHADOW_ALPHA_DECAL)
	{
		m_diffuse = (m_color & 0x00ffffff) + (value << 24);
	}
	else if (m_type & SHADOW_ADDITIVE_DECAL)
	{
		Real fvalue = (Real)m_opacity / 255.0f;
		m_diffuse =
			REAL_TO_INT(((Real)(m_color & 0xff) * fvalue))
			| REAL_TO_INT(((Real)((m_color >> 8) & 0xff) * fvalue))
			| REAL_TO_INT(((Real)((m_color >> 16) & 0xff) * fvalue));
	}
}

inline void Shadow::setColor(UnsignedInt value)
{
	m_color = value & 0x00ffffff;

	if (m_type & SHADOW_ALPHA_DECAL)
	{
		m_diffuse = m_color | (m_opacity << 24);
	}
	else if (m_type & SHADOW_ADDITIVE_DECAL)
	{
		Real fvalue = (Real)m_opacity / 255.0f;
		m_diffuse =
			REAL_TO_INT(((Real)(m_color & 0xff) * fvalue))
			| REAL_TO_INT(((Real)((m_color >> 8) & 0xff) * fvalue))
			| REAL_TO_INT(((Real)((m_color >> 16) & 0xff) * fvalue));
	}
}

inline void Shadow::setPosition(Real x, Real y, Real z)
{
	m_x = x;
	m_y = y;
	m_z = z;
}

inline void Shadow::setAngle(Real angle)
{
	m_localAngle = angle;
}

// Abstract projected-shadow/decal manager, exposed separately so non-W3D
// code (decal systems not tied to a specific device) can add decals without
// pulling in the full W3D header.
class ProjectedShadowManager
{
public:
	virtual ~ProjectedShadowManager() { }
	virtual Shadow* addDecal(RenderObjClass* robj, Shadow::ShadowTypeInfo* shadowInfo) = 0;
	virtual Shadow* addDecal(Shadow::ShadowTypeInfo* shadowInfo) = 0;
};

extern ProjectedShadowManager* TheProjectedShadowManager;

#endif // __SHADOW_H_
