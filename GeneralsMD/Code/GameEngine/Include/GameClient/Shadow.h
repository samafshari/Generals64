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

// shadow bit flags, keep this in sync with TheShadowNames
enum ShadowType : Int
{
	SHADOW_NONE											=	0x00000000,
	SHADOW_DECAL										=	0x00000001,
	SHADOW_VOLUME										=	0x00000002,
	SHADOW_PROJECTION								=	0x00000004,
	SHADOW_DYNAMIC_PROJECTION				= 0x00000008,
	SHADOW_DIRECTIONAL_PROJECTION		= 0x00000010,
	SHADOW_ALPHA_DECAL							= 0x00000020,
	SHADOW_ADDITIVE_DECAL						= 0x00000040
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
