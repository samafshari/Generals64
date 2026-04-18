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

#ifndef __W3D_PROJECTED_SHADOW_H_
#define __W3D_PROJECTED_SHADOW_H_

#include "GameClient/Shadow.h"
#include "vector3.h"

class RenderInfoClass;
class RenderObjClass;
class Drawable;
class AABoxClass;

namespace Render { class Texture; }

class W3DShadowTexture;
class W3DShadowTextureManager;
class W3DProjectedShadow;

// Projected-shadow manager. Handles SHADOW_DECAL / SHADOW_ALPHA_DECAL /
// SHADOW_ADDITIVE_DECAL and SHADOW_PROJECTION / SHADOW_DYNAMIC_PROJECTION.
// The decal path walks the terrain heightmap under the caster and emits
// one textured/shaded quad per cell; the projection path wraps a cached
// silhouette texture around the caster via a frustum-aligned projector.
class W3DProjectedShadowManager : public ProjectedShadowManager
{
public:
	W3DProjectedShadowManager(void);
	virtual ~W3DProjectedShadowManager(void);

	Bool	init(void);
	void	reset(void);
	void	shutdown(void);

	// Returns the number of projected shadows rendered, passed through to
	// the volumetric manager so it knows if the stencil was already touched.
	Int		renderShadows(RenderInfoClass& rinfo);

	void	ReleaseResources(void);
	Bool	ReAcquireResources(void);
	void	invalidateCachedLightPositions(void);

	// ProjectedShadowManager overrides — free-standing decals that aren't
	// tied to a specific shadow.
	virtual Shadow* addDecal(RenderObjClass* robj, Shadow::ShadowTypeInfo* shadowInfo) override;
	virtual Shadow* addDecal(Shadow::ShadowTypeInfo* shadowInfo) override;

	// Primary path used by W3DModelDraw::allocateShadows when a
	// ThingTemplate specifies SHADOW_DECAL / SHADOW_PROJECTION.
	W3DProjectedShadow*	addShadow(RenderObjClass* robj, Shadow::ShadowTypeInfo* shadowInfo, Drawable* draw);

	// Create a decal shadow not bound to a specific object (used by
	// selection rings / status indicators).
	W3DProjectedShadow*	createDecalShadow(Shadow::ShadowTypeInfo* shadowInfo);

	void				removeShadow(W3DProjectedShadow* shadow);
	void				removeAllShadows(void);

	void	updateRenderTargetTextures(void);	// per-frame update for dynamic shadow textures

	// Walk the heightmap grid under the shadow footprint and queue textured
	// tris into the decal VB/IB. Called from renderShadows().
	void	queueDecal(W3DProjectedShadow* shadow);
	// Simpler 2-triangle quad fallback when terrain-conforming isn't needed.
	void	queueSimpleDecal(W3DProjectedShadow* shadow);
	// Flush the current batch — selects blend state from the shadow type.
	void	flushDecals(W3DShadowTexture* texture, ShadowType type);

	// Exposed for W3DProjectedShadow::updateTexture (not yet used by the
	// DX11 port — projection texture dynamics are stubbed for now).
	Render::Texture* getRenderTarget(void) { return m_dynamicRenderTarget; }

private:
	// Internal shadow lists (projected vs. decal). Matches original.
	W3DProjectedShadow*	m_shadowList;		// SHADOW_PROJECTION + object-bound SHADOW_DECAL
	W3DProjectedShadow*	m_decalList;		// loose decals (ALPHA / ADDITIVE / generic)
	Render::Texture*		m_dynamicRenderTarget;	// off-screen RT for dynamic shadow texture generation
	Bool					m_renderTargetHasAlpha;
	W3DShadowTextureManager* m_W3DShadowTextureManager;
	Int						m_numDecalShadows;
	Int						m_numProjectionShadows;

	// Render the decal footprint of a projected shadow onto terrain.
	// Returns 1 if anything was drawn. Used by SHADOW_PROJECTION.
	Int renderProjectedTerrainShadow(W3DProjectedShadow* shadow, AABoxClass& box);
};

extern W3DProjectedShadowManager* TheW3DProjectedShadowManager;

// One shadow instance — created by addShadow / addDecal, rendered by
// renderShadows. For SHADOW_DECAL the image is static and simply stamped
// on the terrain; for SHADOW_PROJECTION the texture is generated on first
// use and re-wrapped around the caster every frame.
class W3DProjectedShadow : public Shadow
{
	friend class W3DProjectedShadowManager;

public:
	W3DProjectedShadow(void);
	virtual ~W3DProjectedShadow(void);

	void setRenderObject(RenderObjClass* robj) { m_robj = robj; }
	void setObjPosHistory(const Vector3& pos) { m_lastObjPosition = pos; }
	void setTexture(Int lightIndex, W3DShadowTexture* texture);

	W3DShadowTexture* getTexture(Int lightIndex) { return m_shadowTexture[lightIndex]; }

	// Called each frame for SHADOW_PROJECTION: refreshes the silhouette
	// texture and the projector matrix when light / object moves.
	void update(void);
	void init(void);

protected:
	virtual void release(void) override;

	W3DShadowTexture*	m_shadowTexture[MAX_SHADOW_LIGHTS];
	RenderObjClass*	m_robj;
	Vector3				m_lastObjPosition;
	W3DProjectedShadow*	m_next;
	Bool					m_allowWorldAlign;
	Real					m_decalOffsetU;
	Real					m_decalOffsetV;
	Int					m_flags;
};

#endif //__W3D_PROJECTED_SHADOW_H_
