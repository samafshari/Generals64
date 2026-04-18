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

#ifndef __W3D_VOLUMETRIC_SHADOW_H_
#define __W3D_VOLUMETRIC_SHADOW_H_

#include "matrix4.h"
#include "W3DDevice/GameClient/W3DBufferManager.h"
#include "GameClient/Shadow.h"

// Max sub-meshes per shadow caster hierarchy. Matches the original —
// the array index is a byte so the cap must fit in 0..255.
#define MAX_SHADOW_CASTER_MESHES	160

class W3DShadowGeometry;
class W3DShadowGeometryManager;
struct Geometry;
struct PolyNeighbor;
class W3DVolumetricShadow;
class Drawable;
class RenderObjClass;
class MeshClass;

// Render-task node threaded through W3DBufferManager::W3DVertexBuffer's
// render-task list. Each shadow submesh owns one per (light,mesh) slot.
struct W3DVolumetricShadowRenderTask : public W3DBufferManager::W3DRenderTask
{
	W3DVolumetricShadow*	m_parentShadow;
	UnsignedByte			m_meshIndex;
	UnsignedByte			m_lightIndex;
};

// ShadowManager --------------------------------------------------------------
class W3DVolumetricShadowManager
{
public:
	W3DVolumetricShadowManager(void);
	~W3DVolumetricShadowManager(void);

	Bool	init(void);
	void	reset(void);

	W3DVolumetricShadow*	addShadow(RenderObjClass* robj, Shadow::ShadowTypeInfo* shadowInfo, Drawable* draw);
	void					removeShadow(W3DVolumetricShadow* shadow);
	void					removeAllShadows(void);

	// Queues a dynamic shadow caster for rendering; used internally only.
	void addDynamicShadowTask(W3DVolumetricShadowRenderTask* task)
	{
		W3DBufferManager::W3DRenderTask* oldTask = m_dynamicShadowVolumesToRender;
		m_dynamicShadowVolumesToRender = task;
		m_dynamicShadowVolumesToRender->m_nextTask = oldTask;
	}

	void	invalidateCachedLightPositions(void);
	void	loadTerrainShadows(void);

	// projectionCount is the number of projected shadows rendered earlier
	// this frame — used to decide whether to force a stencil fill when no
	// volumetric shadows actually render.
	void	renderShadows(Int projectionCount);

	void	ReleaseResources(void);
	Bool	ReAcquireResources(void);

protected:
	// Fullscreen darkening pass — multiplies the backbuffer with the
	// shadow color wherever the stencil buffer is non-zero.
	void	renderStencilShadows(void);

	W3DVolumetricShadow*				m_shadowList;
	W3DVolumetricShadowRenderTask*		m_dynamicShadowVolumesToRender;
	W3DShadowGeometryManager*			m_W3DShadowGeometryManager;
};

extern W3DVolumetricShadowManager* TheW3DVolumetricShadowManager;

// W3DVolumetricShadow --------------------------------------------------------
class W3DVolumetricShadow : public Shadow
{
	friend class W3DVolumetricShadowManager;

	enum
	{
		SHADOW_DYNAMIC = 0x1	// flag indicating a dynamic shadow caster (animated mesh)
	};

public:
	W3DVolumetricShadow(void);
	~W3DVolumetricShadow(void);

protected:
	virtual void release(void) { TheW3DVolumetricShadowManager->removeShadow(this); }

	// tie in geometry and transformation for this shadow
	void SetGeometry(W3DShadowGeometry* geometry);
	void setShadowLengthScale(Real value) { m_shadowLengthScale = value; }
	void updateOptimalExtrusionPadding(void);
	void setOptimalExtrusionPadding(Real value) { m_extraExtrusionPadding = value; }
	const W3DShadowGeometry* getGeometry(void) { return m_geometry; }

	void setRenderObject(RenderObjClass* robj) { m_robj = robj; }
	void setRenderObjExtent(Real extent) { m_robjExtent = extent; }

	// called once per frame, updates shadow volume when necessary
	void Update();
	void updateVolumes(Real zoffset);
	void updateMeshVolume(Int meshIndex, Int lightIndex, const Matrix3D* meshXform, const AABoxClass& meshBox, float floorZ);

	// rendering interface
	void RenderVolume(Int meshIndex, Int lightIndex);
	void RenderMeshVolume(Int meshIndex, Int lightIndex, const Matrix3D* meshXform);
	void RenderDynamicMeshVolume(Int meshIndex, Int lightIndex, const Matrix3D* meshXform);

	void setLightPosHistory(Int lightIndex, Int meshIndex, Vector3& pos) { m_lightPosHistory[lightIndex][meshIndex] = pos; }

	W3DVolumetricShadow* m_next;

	// silhouette tools
	void buildSilhouette(Int meshIndex, Vector3* lightPosWorld);
	void addSilhouetteEdge(Int meshIndex, PolyNeighbor* visible, PolyNeighbor* hidden);
	void addNeighborlessEdges(Int meshIndex, PolyNeighbor* us);
	void addSilhouetteIndices(Int meshIndex, Short edgeStart, Short edgeEnd);
	Bool allocateSilhouette(Int meshIndex, Int numVertices);
	void deleteSilhouette(Int meshIndex);
	void resetSilhouette(Int meshIndex);

	// shadow volume construction
	void constructVolume(Vector3* lightPos, Real shadowExtrudeDistance, Int volumeIndex, Int meshIndex);
	void constructVolumeVB(Vector3* lightPosObject, Real shadowExtrudeDistance, Int volumeIndex, Int meshIndex);
	Bool allocateShadowVolume(Int volumeIndex, Int meshIndex);
	void deleteShadowVolume(Int volumeIndex);
	void resetShadowVolume(Int volumeIndex, Int meshIndex);

	W3DShadowGeometry*	m_geometry;
	RenderObjClass*		m_robj;

	Real	m_shadowLengthScale;
	Real	m_robjExtent;
	Real	m_extraExtrusionPadding;

	// per-light per-mesh shadow volume data
	static Geometry m_tempShadowVolume;
	Geometry*								m_shadowVolume[MAX_SHADOW_LIGHTS][MAX_SHADOW_CASTER_MESHES];
	W3DBufferManager::W3DVertexBufferSlot*	m_shadowVolumeVB[MAX_SHADOW_LIGHTS][MAX_SHADOW_CASTER_MESHES];
	W3DBufferManager::W3DIndexBufferSlot*	m_shadowVolumeIB[MAX_SHADOW_LIGHTS][MAX_SHADOW_CASTER_MESHES];
	W3DVolumetricShadowRenderTask			m_shadowVolumeRenderTask[MAX_SHADOW_LIGHTS][MAX_SHADOW_CASTER_MESHES];
	Int										m_shadowVolumeCount[MAX_SHADOW_CASTER_MESHES];
	Vector3									m_lightPosHistory[MAX_SHADOW_LIGHTS][MAX_SHADOW_CASTER_MESHES];
	Matrix4x4								m_objectXformHistory[MAX_SHADOW_LIGHTS][MAX_SHADOW_CASTER_MESHES];

	// silhouette building space
	Short*	m_silhouetteIndex[MAX_SHADOW_CASTER_MESHES];
	Short	m_numSilhouetteIndices[MAX_SHADOW_CASTER_MESHES];
	Short	m_maxSilhouetteEntries[MAX_SHADOW_CASTER_MESHES];

	Int		m_numIndicesPerMesh[MAX_SHADOW_CASTER_MESHES];
};

#endif	//__W3D_VOLUMETRIC_SHADOW_H_
