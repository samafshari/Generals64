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

// FILE: W3DRopeDraw.cpp ////////////////////////////////////////////////////////////////////////
// Author: Steven Johnson, Aug 2002
// Desc:   Rope drawing
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/ClientRandomValue.h"
#include "GameClient/Color.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameLogic/GameLogic.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/Module/W3DRopeDraw.h"
#include "WW3D2/line3d.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "Common/GameState.h"
#include "Renderer.h"
#include "Texture.h"


///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DRopeDraw::W3DRopeDraw( Thing *thing, const ModuleData* moduleData ) : DrawModule( thing, moduleData )
{
	m_curLen = 0.0f;
	m_maxLen = 1.0f;
	m_width = 0.5f;
	m_color.red = 0.0f;
	m_color.green = 0.0f;
	m_color.blue = 0.0f;
	m_curSpeed = 0.0f;
	m_maxSpeed = 0.0f;
	m_accel = 0.0f;
	m_wobbleLen = m_maxLen;	// huge
	m_wobbleAmp = 0.0f;
	m_segments.clear();
	m_curWobblePhase = 0.0f;
	m_curZOffset = 0.0f;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DRopeDraw::buildSegments()
{
	DEBUG_ASSERTCRASH(m_segments.empty(), ("Hmmn, not empty"));
	m_segments.clear();

	Int numSegs = ceil(m_maxLen / m_wobbleLen);
	Real eachLen = m_maxLen / (Real)numSegs;
	Coord3D pos = *getDrawable()->getPosition();
	for (int i = 0; i < numSegs; ++i, pos.z += eachLen)
	{
		SegInfo info;

		Real axis = GameClientRandomValueReal(0, 2*PI);
		info.wobbleAxisX = Cos(axis);
		info.wobbleAxisY = Sin(axis);
		info.line = NEW Line3DClass( Vector3(pos.x,pos.y,pos.z),
																	 Vector3(pos.x,pos.y,pos.z+eachLen),
																	 m_width * 0.5f,  // width
																	 m_color.red,  // red
																	 m_color.green,  // green
																	 m_color.blue,  // blue
																	 1.0f );  // transparency

		info.softLine = NEW Line3DClass( Vector3(pos.x,pos.y,pos.z),
																	 Vector3(pos.x,pos.y,pos.z+eachLen),
																	 m_width,  // width
																	 m_color.red,  // red
																	 m_color.green,  // green
																	 m_color.blue,  // blue
																	 0.5f );  // transparency

		if (W3DDisplay::m_3DScene)
		{
			W3DDisplay::m_3DScene->Add_Render_Object( info.line );
			W3DDisplay::m_3DScene->Add_Render_Object( info.softLine );
		}
		m_segments.push_back(info);
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DRopeDraw::tossSegments()
{
	// remove tracer from the scene and delete
	for (std::vector<SegInfo>::iterator it = m_segments.begin(); it != m_segments.end(); ++it)
	{
		if (it->line)
		{
			if (W3DDisplay::m_3DScene)
				W3DDisplay::m_3DScene->Remove_Render_Object(it->line);
			REF_PTR_RELEASE((it->line));
		}
		if (it->softLine)
		{
			if (W3DDisplay::m_3DScene)
				W3DDisplay::m_3DScene->Remove_Render_Object(it->softLine);
			REF_PTR_RELEASE((it->softLine));
		}
	}
	m_segments.clear();
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DRopeDraw::initRopeParms(Real length, Real width, const RGBColor& color, Real wobbleLen, Real wobbleAmp, Real wobbleRate)
{
	m_maxLen = max(1.0f, length);
	m_curLen = 0.0f;
	m_width = width;
	m_color = color;
	m_wobbleLen = min(m_maxLen, wobbleLen);
	m_wobbleAmp = wobbleAmp;
	m_wobbleRate = wobbleRate;
	m_curZOffset = 0.0f;

	tossSegments();
	buildSegments();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DRopeDraw::setRopeCurLen(Real length)
{
	m_curLen = length;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DRopeDraw::setRopeSpeed(Real curSpeed, Real maxSpeed, Real accel)
{
	m_curSpeed = curSpeed;
	m_maxSpeed = maxSpeed;
	m_accel = accel;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DRopeDraw::~W3DRopeDraw()
{
	tossSegments();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
// Inline render of a single colored line as a screen-aligned quad. The
// original DX8 path relied on Line3DClass objects in W3DDisplay::m_3DScene
// being walked by RTS3DScene::Customized_Render; the D3D11 pipeline iterates
// Drawables instead, so we render directly here from inside doDrawModule
// (which is invoked during the 3D pass with valid 3D state at cbuffer b0).
static void RenderRopeLineQuad(const Vector3& p0World, const Vector3& p1World,
							   Real width, Real r, Real g, Real b, Real a)
{
	if (a <= 0.001f || width < 0.001f)
		return;

	auto& renderer = Render::Renderer::Instance();
	const auto& frameData = renderer.GetFrameData();
	Render::Float3 camPos = { frameData.cameraPos.x, frameData.cameraPos.y, frameData.cameraPos.z };

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
	const Real halfW = width * 0.5f;
	right *= (halfW / rLen);

	uint8_t cr = (uint8_t)(r * 255.0f > 255.0f ? 255 : r * 255.0f);
	uint8_t cg = (uint8_t)(g * 255.0f > 255.0f ? 255 : g * 255.0f);
	uint8_t cb = (uint8_t)(b * 255.0f > 255.0f ? 255 : b * 255.0f);
	uint8_t ca = (uint8_t)(a * 255.0f > 255.0f ? 255 : a * 255.0f);
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

void W3DRopeDraw::doDrawModule(const Matrix3D* transformMtx)
{
	if (m_segments.empty())
	{
		buildSegments();
	}

	if (!m_segments.empty())
	{
		Real deflection = Sin(m_curWobblePhase) * m_wobbleAmp;
		const Coord3D* pos = getDrawable()->getPosition();
		Vector3 start(pos->x, pos->y, pos->z + m_curZOffset);
		Real eachLen = m_curLen / m_segments.size();
		for (std::vector<SegInfo>::iterator it = m_segments.begin(); it != m_segments.end(); ++it)
		{
			Vector3 end(pos->x + deflection*it->wobbleAxisX, pos->y + deflection*it->wobbleAxisY, start.Z - eachLen);
			if (it->line)
				(it->line)->Reset(start, end);
			if (it->softLine)
				(it->softLine)->Reset(start, end);

			// Direct render: the Line3DClass entries above are kept around so the
			// destructor's symmetric scene Remove still works, but the actual draw
			// goes through Renderer::Draw3DNoIndex from here. Render the inner
			// (thin, opaque) line first then the outer (wide, half-alpha) glow
			// so blending matches the original two-pass rope appearance.
			RenderRopeLineQuad(start, end, m_width * 0.5f,
				m_color.red, m_color.green, m_color.blue, 1.0f);
			RenderRopeLineQuad(start, end, m_width,
				m_color.red, m_color.green, m_color.blue, 0.5f);

			start = end;
		}
	}

	m_curWobblePhase += m_wobbleRate;
	if (m_curWobblePhase > 2*PI)
		m_curWobblePhase -= 2*PI;

	m_curZOffset += m_curSpeed;
	m_curSpeed += m_accel;
	if (m_curSpeed > m_maxSpeed)
		m_curSpeed = m_maxSpeed;
	else if (m_curSpeed < -m_maxSpeed)
		m_curSpeed = -m_maxSpeed;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DRopeDraw::crc( Xfer *xfer )
{

	// extend base class
	DrawModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void W3DRopeDraw::xfer( Xfer *xfer )
{

	// version
	const XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	DrawModule::xfer( xfer );

	// m_segments is not saved

	// cur len
	xfer->xferReal( &m_curLen );

	// max len
	xfer->xferReal( &m_maxLen );

	// width
	xfer->xferReal( &m_width );

	// color
	xfer->xferRGBColor( &m_color );

	// cur speed
	xfer->xferReal( &m_curSpeed );

	// max speed
	xfer->xferReal( &m_maxSpeed );

	// acceleration
	xfer->xferReal( &m_accel );

	// wobble len
	xfer->xferReal( &m_wobbleLen );

	// wobble amp
	xfer->xferReal( &m_wobbleAmp );

	// wobble rate
	xfer->xferReal( &m_wobbleRate );

	// current wobble phase
	xfer->xferReal( &m_curWobblePhase );

	// cur Z offset
	xfer->xferReal( &m_curZOffset );

	if (xfer->getXferMode() == XFER_LOAD)
		tossSegments();


}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DRopeDraw::loadPostProcess()
{

	// extend base class
	DrawModule::loadPostProcess();

}
