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

// FILE: W3DPoliceCarDraw.cpp /////////////////////////////////////////////////////////////////////
// Author: Colin Day, May 2001
// Desc:   W3DPoliceCarDraw
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include <stdlib.h>

#include "Common/STLTypedefs.h"
#include "Common/Thing.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameClient/Color.h"
#include "W3DDevice/GameClient/Module/W3DPoliceCarDraw.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "Common/RandomValue.h"
#include "WW3D2/hanim.h"
#include "W3DDevice/GameClient/W3DScene.h"

// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
/** Create a dynamic light for the search light */
//-------------------------------------------------------------------------------------------------
W3DDynamicLight *W3DPoliceCarDraw::createDynamicLight()
{
	W3DDynamicLight *light = nullptr;

	// get me a dynamic light from the scene
	light = nullptr; // D3D11: dynamic lights handled by Renderer
	if( light )
	{

		light->setEnabled( TRUE );
		light->Set_Ambient( Vector3( 0.0f, 0.0f, 0.0f ) );
		// Use all ambient, and no diffuse.  This produces a circle of light on
		// even and uneven ground.  Diffuse lighting shows up ground unevenness, which looks
		// funny on a searchlight.  So  no diffuse.  jba.
		light->Set_Diffuse( Vector3( 0.0f, 0.0f, 0.0f ) );
		light->Set_Position( Vector3( 0.0f, 0.0f, 0.0f ) );
		light->Set_Far_Attenuation_Range( 5, 15 );

	}

	return light;

}

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DPoliceCarDraw::W3DPoliceCarDraw( Thing *thing, const ModuleData* moduleData ) : W3DTruckDraw( thing, moduleData )
{
	m_light = nullptr;
	m_curFrame = GameClientRandomValueReal(0, 10 );

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DPoliceCarDraw::~W3DPoliceCarDraw()
{

	// disable the light ... the scene will re-use it later
	if( m_light )
	{
		// Have it fade out over 5 frames.
		m_light->setFrameFade(0, 5);
		m_light->setDecayRange();
		m_light->setDecayColor();
		m_light = nullptr;
	}

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DPoliceCarDraw::doDrawModule(const Matrix3D* transformMtx)
{
	const Real floatAmt = 8.0f;
	const Real animAmt = 0.25;

	// get pointers to our render objects that we'll need
	RenderObjClass* policeCarRenderObj = getRenderObject();
	if( policeCarRenderObj == nullptr )
		return;

	HAnimClass *anim = policeCarRenderObj->Peek_Animation();
	if (anim)
	{
		Real frames = anim->Get_Num_Frames();
		m_curFrame += animAmt;
		if (m_curFrame > frames-1) {
			m_curFrame = 0;
		}
		policeCarRenderObj->Set_Animation(anim, m_curFrame);
	}
	Real red = 0;
	Real green = 0;
	Real blue = 0;
	if (m_curFrame < 3) {
		red = 1; green = 0.5;
	} else if (m_curFrame < 6) {
		red = 1;
	} else if (m_curFrame < 7) {
		red = 1; green = 0.5;
	} else if (m_curFrame < 9) {
		red = 0.5+(9-m_curFrame)/4;
		blue = (m_curFrame-5)/6;
	} else if (m_curFrame < 12) {
		blue=1;
	} else if (m_curFrame <= 14) {
		green = (m_curFrame-11)/3;
		blue = (14-m_curFrame)/2;
		red =		(m_curFrame-11)/3;
	}

	// Police car siren light. Original used a recyclable W3DDynamicLight slot
	// from RTS3DScene::getADynamicLight() which our D3D11 port no longer
	// exposes (the W3DScene scene graph isn't walked for rendering anymore).
	// Push a fresh single-frame light pulse via TheDisplay each frame so the
	// existing point-light pipeline (originally added for explosion pulses)
	// also drives the police car siren. Each pulse expires the next logic
	// frame so they don't accumulate; with one pulse per render frame this
	// produces a stable continuously-updating dynamic light at the cost of
	// a few extra entries in m_lightPulses while the police car is alive.
	if (TheDisplay && (red > 0.001f || green > 0.001f || blue > 0.001f))
	{
		Coord3D lightPos = *getDrawable()->getPosition();
		lightPos.z += floatAmt;
		RGBColor lightColor;
		lightColor.red   = red;
		lightColor.green = green;
		lightColor.blue  = blue;
		// Inner radius 3, attenuation width 17 → outer 20 — matches the
		// original Set_Far_Attenuation_Range(3, 20). 0 increase frames so
		// the pulse jumps to peak intensity immediately on this frame; 1
		// decay frame so it expires before next render-frame's replacement.
		TheDisplay->createLightPulse(&lightPos, &lightColor, 3.0f, 17.0f, 0, 1);
	}

	// m_light retained for symmetry with the destructor's fade-out path,
	// even though it's no longer the rendering source.
	(void)m_light;

	W3DTruckDraw::doDrawModule(transformMtx);
}


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DPoliceCarDraw::crc( Xfer *xfer )
{

	// extend base class
	W3DTruckDraw::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void W3DPoliceCarDraw::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	W3DTruckDraw::xfer( xfer );

	// John A says there is no data for these to save

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DPoliceCarDraw::loadPostProcess()
{

	// extend base class
	W3DTruckDraw::loadPostProcess();

}
