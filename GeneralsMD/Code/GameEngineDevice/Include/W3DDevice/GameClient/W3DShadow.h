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

#ifndef __W3DSHADOW_H_
#define __W3DSHADOW_H_

#include "vector3.h"
#include "GameClient/Shadow.h"

class RenderInfoClass;
enum TimeOfDay : Int;

// Umbrella manager — routes addShadow() to the volumetric or projected
// sub-manager based on ShadowType, and forwards resource/reset calls to
// both. The sub-managers are owned by this class (constructed in its ctor,
// deleted in its dtor).
class W3DShadowManager
{
public:
	W3DShadowManager(void);
	~W3DShadowManager(void);

	Bool init(void);

	// Flags the system to render shadows on the next scene pass. The scene
	// render calls DoShadows() twice per frame, so managers use this flag
	// to ensure each shadow only renders once.
	void queueShadows(Bool state) { m_isShadowScene = state; }
	Bool isShadowScene(void) { return m_isShadowScene; }

	// Shadow list management — routes to the appropriate sub-manager.
	void	Reset(void);
	Shadow*	addShadow(RenderObjClass* robj, Shadow::ShadowTypeInfo* shadowInfo = nullptr, Drawable* draw = nullptr);
	void	removeShadow(Shadow* shadow);
	void	removeAllShadows(void);

	void	setShadowColor(UnsignedInt color) { m_shadowColor = color; }
	UnsignedInt getShadowColor() { return m_shadowColor; }

	void	setLightPosition(Int lightIndex, Real x, Real y, Real z);
	void	setTimeOfDay(TimeOfDay tod);
	void	invalidateCachedLightPositions(void);
	Vector3& getLightPosWorld(Int lightIndex);

	// Mask used to mask out stencil bits used for storing occlusion / player color.
	void	setStencilShadowMask(int mask) { m_stencilShadowMask = mask; }
	Int		getStencilShadowMask(void) { return m_stencilShadowMask; }

	void	ReleaseResources(void);
	Bool	ReAcquireResources(void);

protected:
	Bool				m_isShadowScene;
	UnsignedInt	m_shadowColor;		// ARGB color + alpha for all shadows in the scene.
	Int					m_stencilShadowMask;
};

extern W3DShadowManager* TheW3DShadowManager;

// Called twice per scene render: once before opaque objects (stencilPass=false)
// to render projected decals, and once after (stencilPass=true) to render
// volumetric stencil shadows. Mirrors the original W3D scene hook.
void DoShadows(RenderInfoClass& rinfo, Bool stencilPass);

#endif	//__W3DSHADOW_H_
