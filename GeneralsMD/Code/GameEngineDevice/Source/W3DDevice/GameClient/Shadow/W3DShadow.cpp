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
// Stencil-volume shadow system removed — sun shadow mapping now handles
// all opaque-mesh shadow casting from W3DDisplay::draw. SHADOW_VOLUME type
// in ThingTemplate data is treated as "no per-object shadow record" because
// the shadow-map pass walks all scene meshes unconditionally.

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

// Called once per scene render by W3DDisplay::draw (stencilPass=false)
// to render the projected decal shadows. The old stencilPass=true branch
// used to run volumetric stencil shadows; that system has been replaced
// by the sun shadow-map pre-pass in W3DDisplay::draw, so the stencilPass
// argument is now ignored other than for terminating the shadow-scene
// latch after the second call.
void DoShadows(RenderInfoClass& rinfo, Bool stencilPass)
{
	// Authoritative source for the sun direction per time-of-day is
	// m_terrainObjectsLighting[tod][0].lightPos — populated directly from
	// INI by the field parsers. Refresh s_lightPosWorld every frame so
	// the sun shadow-map pass and the projected decal pass always agree
	// with the current time-of-day light.
	if (TheW3DShadowManager && TheGlobalData && !stencilPass)
	{
		TimeOfDay tod = TheGlobalData->m_timeOfDay;
		if (tod >= TIME_OF_DAY_FIRST && tod < TIME_OF_DAY_COUNT)
			TheW3DShadowManager->setTimeOfDay(tod);
	}

	// Projected decal shadows (small ground stamps under units and
	// buildings). Independent of the shadow-map system.
	if (stencilPass == FALSE && TheW3DProjectedShadowManager)
	{
		if (TheW3DShadowManager && TheW3DShadowManager->isShadowScene())
			TheW3DProjectedShadowManager->renderShadows(rinfo);
	}

	// Clear the shadowScene latch after the trailing call.
	if (TheW3DShadowManager && stencilPass)
		TheW3DShadowManager->queueShadows(FALSE);
}

W3DShadowManager::W3DShadowManager(void)
	: m_isShadowScene(false)
	, m_shadowColor(0x7fa0a0a0)
	, m_stencilShadowMask(0)
{
	DEBUG_ASSERTCRASH(TheW3DProjectedShadowManager == nullptr,
		("Creating new shadow managers without deleting old ones"));

	// Derive initial sun position from global terrain light direction.
	Vector3 lightRay(-TheGlobalData->m_terrainLightPos[0].x,
		-TheGlobalData->m_terrainLightPos[0].y,
		-TheGlobalData->m_terrainLightPos[0].z);
	lightRay.Normalize();
	s_lightPosWorld[0] = lightRay * SUN_DISTANCE_FROM_GROUND;

	TheProjectedShadowManager = TheW3DProjectedShadowManager = NEW W3DProjectedShadowManager;
}

W3DShadowManager::~W3DShadowManager(void)
{
	delete TheW3DProjectedShadowManager;
	TheProjectedShadowManager = TheW3DProjectedShadowManager = nullptr;
}

Bool W3DShadowManager::init(void)
{
	if (TheW3DProjectedShadowManager && TheW3DProjectedShadowManager->init())
	{
		if (TheW3DProjectedShadowManager->ReAcquireResources())
			return true;
	}
	return true;
}

void W3DShadowManager::Reset(void)
{
	if (TheW3DProjectedShadowManager)
		TheW3DProjectedShadowManager->reset();
}

Bool W3DShadowManager::ReAcquireResources(void)
{
	if (TheW3DProjectedShadowManager && !TheW3DProjectedShadowManager->ReAcquireResources())
		return false;
	return true;
}

void W3DShadowManager::ReleaseResources(void)
{
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
		// Stencil-volume shadows are gone. Shadow mapping (from the sun)
		// handles opaque casters globally at render time — no per-object
		// Shadow record needed.
		return nullptr;
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
	if (TheW3DProjectedShadowManager)
		TheW3DProjectedShadowManager->removeAllShadows();
}

void W3DShadowManager::invalidateCachedLightPositions(void)
{
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
