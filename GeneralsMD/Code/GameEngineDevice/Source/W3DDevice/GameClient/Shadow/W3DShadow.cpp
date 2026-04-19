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

// Umbrella shadow manager. Owns the projected and volumetric sub-managers
// and routes Shadow creation to the right one based on ShadowType. The
// per-frame DoShadows() hook is called from the scene render path.

#include "always.h"
#include "Common/GlobalData.h"
#include "WW3D2/rinfo.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/W3DProjectedShadow.h"
#include "W3DDevice/GameClient/W3DVolumetricShadow.h"

// Distance of the sun light source from ground. Arbitrary but far enough
// that the direction vector dominates over position for shadow projection.
#define SUN_DISTANCE_FROM_GROUND	10000.0f

W3DShadowManager*			TheW3DShadowManager = nullptr;
ProjectedShadowManager*	TheProjectedShadowManager = nullptr;

// World-space position of each shadow-casting light. Index 0 is the sun;
// support for more was dropped from most of the original code.
static Vector3 s_lightPosWorld[MAX_SHADOW_LIGHTS] =
{
	Vector3(94.0161f, 50.499f, 200.0f)
};

// Called twice per scene render by W3DDisplay::draw — once before opaque
// meshes (stencilPass=false) for projected decals, once after
// (stencilPass=true) for volumetric stencil shadows.
void DoShadows(RenderInfoClass& rinfo, Bool stencilPass)
{
	// The expected init-order is: INI load → GlobalData::setTimeOfDay →
	// TheGameClient->setTimeOfDay → ... → W3DShadowManager::setTimeOfDay.
	// In this build the W3DTerrainLogic::loadMap post-INI hop never fires
	// (map load goes through a different path), so m_terrainLightPos is
	// never refreshed from the INI and s_lightPosWorld stays at its
	// construction-time default (straight-down (0,0,10000)).
	//
	// Authoritative source for the sun direction per time-of-day is
	// m_terrainObjectsLighting[tod][0].lightPos — that array IS populated
	// directly from the INI field parsers and is valid by the time any
	// frame renders. Refresh s_lightPosWorld from it here once per frame.
	// Cheap: it's just a few arithmetic ops.
	if (TheW3DShadowManager && TheGlobalData && !stencilPass)
	{
		TimeOfDay tod = TheGlobalData->m_timeOfDay;
		if (tod >= TIME_OF_DAY_FIRST && tod < TIME_OF_DAY_COUNT)
			TheW3DShadowManager->setTimeOfDay(tod);
	}

	Int projectionCount = 0;

	// Projected shadows render first because they may fill the stencil
	// buffer, which volumetric shadows then consume.
	if (stencilPass == FALSE && TheW3DProjectedShadowManager)
	{
		if (TheW3DShadowManager && TheW3DShadowManager->isShadowScene())
			projectionCount = TheW3DProjectedShadowManager->renderShadows(rinfo);
	}

	if (stencilPass == TRUE && TheW3DVolumetricShadowManager)
	{
		// The scene render path can fire this hook more than once; the
		// shadowScene flag guards against rendering shadows twice in one
		// logical frame.
		if (TheW3DShadowManager && TheW3DShadowManager->isShadowScene())
			TheW3DVolumetricShadowManager->renderShadows(projectionCount);
	}

	// After the stencil pass, clear the shadowScene flag so any further
	// draws this frame don't re-render shadows.
	if (TheW3DShadowManager && stencilPass)
		TheW3DShadowManager->queueShadows(FALSE);
}

W3DShadowManager::W3DShadowManager(void)
	: m_isShadowScene(false)
	, m_shadowColor(0x7fa0a0a0)
	, m_stencilShadowMask(0)
{
	DEBUG_ASSERTCRASH(TheW3DVolumetricShadowManager == nullptr && TheW3DProjectedShadowManager == nullptr,
		("Creating new shadow managers without deleting old ones"));

	// Derive initial sun position from global terrain light direction.
	Vector3 lightRay(-TheGlobalData->m_terrainLightPos[0].x,
		-TheGlobalData->m_terrainLightPos[0].y,
		-TheGlobalData->m_terrainLightPos[0].z);
	lightRay.Normalize();
	s_lightPosWorld[0] = lightRay * SUN_DISTANCE_FROM_GROUND;

	TheW3DVolumetricShadowManager = NEW W3DVolumetricShadowManager;
	TheProjectedShadowManager = TheW3DProjectedShadowManager = NEW W3DProjectedShadowManager;
}

W3DShadowManager::~W3DShadowManager(void)
{
	delete TheW3DVolumetricShadowManager;
	TheW3DVolumetricShadowManager = nullptr;
	delete TheW3DProjectedShadowManager;
	TheProjectedShadowManager = TheW3DProjectedShadowManager = nullptr;
}

Bool W3DShadowManager::init(void)
{
	Bool result = true;

	if (TheW3DVolumetricShadowManager && TheW3DVolumetricShadowManager->init())
	{
		if (TheW3DVolumetricShadowManager->ReAcquireResources())
			result = true;
	}
	if (TheW3DProjectedShadowManager && TheW3DProjectedShadowManager->init())
	{
		if (TheW3DProjectedShadowManager->ReAcquireResources())
			result = true;
	}

	return result;
}

void W3DShadowManager::Reset(void)
{
	if (TheW3DVolumetricShadowManager)
		TheW3DVolumetricShadowManager->reset();
	if (TheW3DProjectedShadowManager)
		TheW3DProjectedShadowManager->reset();
}

Bool W3DShadowManager::ReAcquireResources(void)
{
	Bool result = true;

	if (TheW3DVolumetricShadowManager && !TheW3DVolumetricShadowManager->ReAcquireResources())
		result = false;
	if (TheW3DProjectedShadowManager && !TheW3DProjectedShadowManager->ReAcquireResources())
		result = false;

	return result;
}

void W3DShadowManager::ReleaseResources(void)
{
	if (TheW3DVolumetricShadowManager)
		TheW3DVolumetricShadowManager->ReleaseResources();
	if (TheW3DProjectedShadowManager)
		TheW3DProjectedShadowManager->ReleaseResources();
}

Shadow* W3DShadowManager::addShadow(RenderObjClass* robj, Shadow::ShadowTypeInfo* shadowInfo, Drawable* draw)
{
	ShadowType type = SHADOW_VOLUME;

	if (shadowInfo)
		type = shadowInfo->m_type;

	switch (type)
	{
	case SHADOW_VOLUME:
		if (TheW3DVolumetricShadowManager)
			return (Shadow*)TheW3DVolumetricShadowManager->addShadow(robj, shadowInfo, draw);
		break;
	case SHADOW_PROJECTION:
	case SHADOW_DECAL:
		if (TheW3DProjectedShadowManager)
			return (Shadow*)TheW3DProjectedShadowManager->addShadow(robj, shadowInfo, draw);
		break;
	default:
		return nullptr;
	}

	return nullptr;
}

void W3DShadowManager::removeShadow(Shadow* shadow)
{
	if (shadow)
		shadow->release();
}

void W3DShadowManager::removeAllShadows(void)
{
	if (TheW3DVolumetricShadowManager)
		TheW3DVolumetricShadowManager->removeAllShadows();
	if (TheW3DProjectedShadowManager)
		TheW3DProjectedShadowManager->removeAllShadows();
}

void W3DShadowManager::invalidateCachedLightPositions(void)
{
	if (TheW3DVolumetricShadowManager)
		TheW3DVolumetricShadowManager->invalidateCachedLightPositions();
	if (TheW3DProjectedShadowManager)
		TheW3DProjectedShadowManager->invalidateCachedLightPositions();
}

Vector3& W3DShadowManager::getLightPosWorld(Int lightIndex)
{
	return s_lightPosWorld[lightIndex];
}

void W3DShadowManager::setLightPosition(Int lightIndex, Real x, Real y, Real z)
{
	// Only one shadow-casting light is supported — indices past 0 are ignored.
	if (lightIndex != 0)
		return;

	s_lightPosWorld[lightIndex] = Vector3(x, y, z);
}

void W3DShadowManager::setTimeOfDay(TimeOfDay tod)
{
	const GlobalData::TerrainLighting* ol = &TheGlobalData->m_terrainObjectsLighting[tod][0];

	Vector3 lightRay(-ol->lightPos.x, -ol->lightPos.y, -ol->lightPos.z);
	lightRay.Normalize();
	lightRay *= SUN_DISTANCE_FROM_GROUND;

	setLightPosition(0, lightRay.X, lightRay.Y, lightRay.Z);
}
