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

// FILE: W3DTracerDraw.cpp ////////////////////////////////////////////////////////////////////////
// Author: Colin Day, December 2001
// Desc:   Tracer drawing
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include <stdlib.h>

#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameLogic/GameLogic.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/Module/W3DTracerDraw.h"
#include "WW3D2/line3d.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "Renderer.h"
#include "Texture.h"


///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DTracerDraw::W3DTracerDraw( Thing *thing, const ModuleData* moduleData ) : DrawModule( thing, moduleData )
{

	// set opacity
	m_opacity = 1.0f;
	m_length = 20.0f;
	m_width = 0.5f;
	m_color.red = 0.9f;
	m_color.green = 0.8f;
	m_color.blue = 0.7f;
	m_speedInDistPerFrame = 1.0f;
	m_theTracer = nullptr;

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DTracerDraw::setTracerParms(Real speed, Real length, Real width, const RGBColor& color, Real initialOpacity)
{
	m_speedInDistPerFrame = speed;
	m_length = length;
	m_width = width;
	m_color = color;
	m_opacity = initialOpacity;
	if (m_theTracer)
	{
		Vector3 start( 0.0f, 0.0f, 0.0f );
		Vector3 stop( m_length, 0.0f, 0.0f );
		m_theTracer->Reset(start, stop, m_width);
		m_theTracer->Re_Color(m_color.red, m_color.green, m_color.blue);
		m_theTracer->Set_Opacity( m_opacity );
		// these calls nuke the internal transform, so re-set it here
		m_theTracer->Set_Transform( *getDrawable()->getTransformMatrix() );
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DTracerDraw::~W3DTracerDraw()
{
	// remove tracer from the scene and delete
	if( m_theTracer )
	{
		W3DDisplay::m_3DScene->Remove_Render_Object( m_theTracer );
		REF_PTR_RELEASE( m_theTracer );
	}
}

//-------------------------------------------------------------------------------------------------
void W3DTracerDraw::reactToTransformChange( const Matrix3D *oldMtx,
																							 const Coord3D *oldPos,
																							 Real oldAngle )
{
	if( m_theTracer )
		m_theTracer->Set_Transform( *getDrawable()->getTransformMatrix() );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DTracerDraw::doDrawModule(const Matrix3D* transformMtx)
{

	// In the original DX8 path we created a Line3DClass and added it to
	// W3DDisplay::m_3DScene; the scene's Customized_Render walked the scene
	// and rendered each Line3D every frame. Our D3D11 pipeline iterates
	// Drawables (not the W3D scene), so the Line3D would never be picked
	// up — we still allocate it here so existing transform/lifetime book-
	// keeping code keeps working, but we render the tracer ourselves with
	// a screen-aligned colored quad submitted directly through the Renderer.

	if( m_theTracer == nullptr )
	{
		Vector3 start( 0.0f, 0.0f, 0.0f );
		Vector3 stop( m_length, 0.0f, 0.0f );

		m_theTracer = NEW Line3DClass( start,
																	 stop,
																	 m_width,  // width
																	 m_color.red,  // red
																	 m_color.green,  // green
																	 m_color.blue,  // blue
																	 m_opacity );  // transparency
		// Adding to m_3DScene is harmless (the scene is no longer walked
		// for rendering) — keep it so the destructor's symmetric Remove
		// stays valid and any future code that does iterate the scene
		// still finds the tracer.
		if (W3DDisplay::m_3DScene)
			W3DDisplay::m_3DScene->Add_Render_Object( m_theTracer );

		// Initialize the tracer's local-to-world transform from the drawable
		// so subsequent per-frame Translate calls advance along the right
		// forward axis.
		m_theTracer->Set_Transform( *transformMtx );
	}

	// Fade as the tracer ages
	UnsignedInt expDate = getDrawable()->getExpirationDate();
	if (expDate != 0)
	{
		Real decay = m_opacity / (expDate - TheGameLogic->getFrame());
		m_opacity -= decay;
		m_theTracer->Set_Opacity( m_opacity );
	}

	// Advance the tracer along its forward axis (matches original behavior)
	if (m_speedInDistPerFrame != 0.0f)
	{
		Matrix3D pos = m_theTracer->Get_Transform();
		pos.Translate( Vector3( m_speedInDistPerFrame, 0.0f, 0.0f ) );
		m_theTracer->Set_Transform( pos );
	}

	// --- Direct render of the tracer as a screen-aligned colored quad ---
	// The Line3DClass holds local-space endpoints (0,0,0) → (length,0,0)
	// and a transform matrix; multiply each endpoint through the transform
	// to get world-space positions for the quad.
	if (m_opacity <= 0.001f || m_length < 0.01f || m_width < 0.001f)
		return;

	const Matrix3D& xform = m_theTracer->Get_Transform();
	Vector3 p0Local(0.0f, 0.0f, 0.0f);
	Vector3 p1Local(m_length, 0.0f, 0.0f);
	Vector3 p0World, p1World;
	Matrix3D::Transform_Vector(xform, p0Local, &p0World);
	Matrix3D::Transform_Vector(xform, p1Local, &p1World);

	auto& renderer = Render::Renderer::Instance();
	const auto& frameData = renderer.GetFrameData();
	Render::Float3 camPos = { frameData.cameraPos.x, frameData.cameraPos.y, frameData.cameraPos.z };

	// Build a screen-facing quad oriented along the segment direction.
	Vector3 segDir(p1World.X - p0World.X, p1World.Y - p0World.Y, p1World.Z - p0World.Z);
	Real segLen = segDir.Length();
	if (segLen < 0.001f)
		return;
	segDir *= (1.0f / segLen);

	Vector3 mid((p0World.X + p1World.X) * 0.5f,
	            (p0World.Y + p1World.Y) * 0.5f,
	            (p0World.Z + p1World.Z) * 0.5f);
	Vector3 viewDir(mid.X - camPos.x, mid.Y - camPos.y, mid.Z - camPos.z);
	Real vLen = viewDir.Length();
	if (vLen < 0.001f)
		return;
	viewDir *= (1.0f / vLen);

	Vector3 right;
	Vector3::Cross_Product(segDir, viewDir, &right);
	Real rLen = right.Length();
	if (rLen < 0.0001f)
		return;
	const Real halfW = m_width * 0.5f;
	right *= (halfW / rLen);

	uint8_t cr = (uint8_t)(m_color.red   * 255.0f > 255.0f ? 255 : m_color.red   * 255.0f);
	uint8_t cg = (uint8_t)(m_color.green * 255.0f > 255.0f ? 255 : m_color.green * 255.0f);
	uint8_t cb = (uint8_t)(m_color.blue  * 255.0f > 255.0f ? 255 : m_color.blue  * 255.0f);
	uint8_t ca = (uint8_t)(m_opacity     * 255.0f > 255.0f ? 255 : m_opacity     * 255.0f);
	// Renderer expects ABGR (R in low byte) — match W3DLaserDraw's BuildLaserQuad layout.
	uint32_t packedColor = (ca << 24) | (cb << 16) | (cg << 8) | cr;

	Render::Vertex3D verts[6];
	auto setVert = [&](int idx, const Vector3& base, const Vector3& off) {
		verts[idx].position = { base.X + off.X, base.Y + off.Y, base.Z + off.Z };
		verts[idx].normal = { viewDir.X, viewDir.Y, viewDir.Z };
		verts[idx].texcoord = { 0.0f, 0.0f };
		verts[idx].color = packedColor;
	};
	setVert(0, p0World, right);
	setVert(1, p0World, Vector3(-right.X, -right.Y, -right.Z));
	setVert(2, p1World, right);
	setVert(3, p1World, right);
	setVert(4, p0World, Vector3(-right.X, -right.Y, -right.Z));
	setVert(5, p1World, Vector3(-right.X, -right.Y, -right.Z));

	// White fallback texture (laser glow shader expects a bound texture).
	static Render::Texture s_whiteTex;
	static bool s_whiteTexReady = false;
	if (!s_whiteTexReady)
	{
		const uint32_t white = 0xFFFFFFFF;
		s_whiteTex.CreateFromRGBA(renderer.GetDevice(), &white, 1, 1, false);
		s_whiteTexReady = true;
	}

	Render::VertexBuffer vb;
	vb.Create(renderer.GetDevice(), verts, 6, sizeof(Render::Vertex3D));
	Render::Float4x4 identity = {
		1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
	};
	renderer.SetLaserGlow3DState();
	renderer.Draw3DNoIndex(vb, 6, &s_whiteTex, identity, {1, 1, 1, 1});
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DTracerDraw::crc( Xfer *xfer )
{

	// extend base class
	DrawModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void W3DTracerDraw::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	DrawModule::xfer( xfer );

	// no data to save here, nobody will ever notice

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DTracerDraw::loadPostProcess()
{

	// extend base class
	DrawModule::loadPostProcess();

}
