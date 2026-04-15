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

// FILE: W3DLaserDraw.cpp /////////////////////////////////////////////////////////////////////////
// Author: Colin Day, May 2001
// Desc:   W3DLaserDraw
// Updated: Kris Morness July 2002 -- made it data driven and added new features to make it flexible.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include <stdlib.h>

#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/Color.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/RayEffect.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/LaserUpdate.h"
#include "W3DDevice/GameClient/Module/W3DLaserDraw.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/camera.h"
#include "WW3D2/segline.h"
#include "WWMath/vector3.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/ww3d.h"
#include "Renderer.h"
#include "Texture.h"
#include "W3DDevice/GameClient/ImageCache.h"



// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DLaserDrawModuleData::W3DLaserDrawModuleData()
{
	m_innerBeamWidth = 0.0f;         //The total width of beam
	m_outerBeamWidth = 1.0f;         //The total width of beam
  m_numBeams = 1;                 //Number of overlapping cylinders that make the beam. 1 beam will just use inner data.
  m_maxIntensityFrames = 0;				//Laser stays at max intensity for specified time in ms.
  m_fadeFrames = 0;               //Laser will fade and delete.
	m_scrollRate = 0.0f;
	m_tile = false;
	m_segments = 1;
	m_arcHeight = 0.0f;
	m_segmentOverlapRatio = 0.0f;
	m_tilingScalar = 1.0f;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DLaserDrawModuleData::~W3DLaserDrawModuleData()
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DLaserDrawModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "NumBeams",							INI::parseUnsignedInt,					nullptr, offsetof( W3DLaserDrawModuleData, m_numBeams ) },
		{ "InnerBeamWidth",				INI::parseReal,									nullptr, offsetof( W3DLaserDrawModuleData, m_innerBeamWidth ) },
		{ "OuterBeamWidth",				INI::parseReal,									nullptr, offsetof( W3DLaserDrawModuleData, m_outerBeamWidth ) },
		{ "InnerColor",						INI::parseColorInt,							nullptr, offsetof( W3DLaserDrawModuleData, m_innerColor ) },
		{ "OuterColor",						INI::parseColorInt,							nullptr, offsetof( W3DLaserDrawModuleData, m_outerColor ) },
		{ "MaxIntensityLifetime",	INI::parseDurationUnsignedInt,	nullptr, offsetof( W3DLaserDrawModuleData, m_maxIntensityFrames ) },
		{ "FadeLifetime",					INI::parseDurationUnsignedInt,	nullptr, offsetof( W3DLaserDrawModuleData, m_fadeFrames ) },
		{ "Texture",							INI::parseAsciiString,					nullptr, offsetof( W3DLaserDrawModuleData, m_textureName ) },
		{ "ScrollRate",						INI::parseReal,									nullptr, offsetof( W3DLaserDrawModuleData, m_scrollRate ) },
		{ "Tile",									INI::parseBool,									nullptr, offsetof( W3DLaserDrawModuleData, m_tile ) },
		{ "Segments",							INI::parseUnsignedInt,					nullptr, offsetof( W3DLaserDrawModuleData, m_segments ) },
    { "ArcHeight",						INI::parseReal,									nullptr, offsetof( W3DLaserDrawModuleData, m_arcHeight ) },
		{ "SegmentOverlapRatio",	INI::parseReal,									nullptr, offsetof( W3DLaserDrawModuleData, m_segmentOverlapRatio ) },
		{ "TilingScalar",					INI::parseReal,									nullptr, offsetof( W3DLaserDrawModuleData, m_tilingScalar ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
  p.add(dataFieldParse);
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DLaserDraw::W3DLaserDraw( Thing *thing, const ModuleData* moduleData ) :
	DrawModule( thing, moduleData ),
	m_line3D(nullptr),
	m_texture(nullptr),
	m_textureAspectRatio(1.0f),
	m_selfDirty(TRUE)
{
	Vector3 dummyPos1( 0.0f, 0.0f, 0.0f );
	Vector3 dummyPos2( 1.0f, 1.0f, 1.0f );
	Int i;

	const W3DLaserDrawModuleData *data = getW3DLaserDrawModuleData();
	WW3DAssetManager *assetManager = WW3DAssetManager::Get_Instance();

	m_texture = assetManager ? assetManager->Get_Texture( data->m_textureName.str() ) : nullptr;
	if (m_texture)
	{
		if (!m_texture->Is_Initialized())
			m_texture->Init();	//make sure texture is actually loaded before accessing surface.

		SurfaceClass::SurfaceDescription surfaceDesc;
		m_texture->Get_Level_Description(surfaceDesc);
		m_textureAspectRatio = (Real)surfaceDesc.Width/(Real)surfaceDesc.Height;
	}

	//Get the color components for calculation purposes.
	Real innerRed, innerGreen, innerBlue, innerAlpha, outerRed, outerGreen, outerBlue, outerAlpha;
	GameGetColorComponentsReal( data->m_innerColor, &innerRed, &innerGreen, &innerBlue, &innerAlpha );
	GameGetColorComponentsReal( data->m_outerColor, &outerRed, &outerGreen, &outerBlue, &outerAlpha );

	//Make sure our beams range between 1 and the maximum cap.
#ifdef I_WANT_TO_BE_FIRED
// srj sez: this data is const for a reason. casting away the constness because we don't like the values
// isn't an acceptable solution. if you need to constrain the values, do so at parsing time, when
// it's still legal to modify these values. (In point of fact, there's not even really any reason to limit
// the numBeams or segments anymore.)
	data->m_numBeams =		 __min( __max( 1, data->m_numBeams ), MAX_LASER_LINES );
	data->m_segments =		 __min( __max( 1, data->m_segments ), MAX_SEGMENTS );
	data->m_tilingScalar = __max( 0.01f, data->m_tilingScalar );
#endif

	//Allocate an array of lines equal to the number of beams * segments
	m_line3D = NEW SegmentedLineClass *[ data->m_numBeams * data->m_segments ];

	for( UnsignedInt segment = 0; segment < data->m_segments; segment++ )
	{
		//We don't care about segment positioning yet until we actually set the position

		// create all the lines we need at the right transparency level
		for( i = data->m_numBeams - 1; i >= 0; i-- )
		{
			int index = segment * data->m_numBeams + i;

			Real red, green, blue, alpha, width;

			if( data->m_numBeams == 1 )
			{
				width = data->m_innerBeamWidth;
				alpha = innerAlpha;
				red = innerRed * innerAlpha;
				green = innerGreen * innerAlpha;
				blue = innerBlue * innerAlpha;
			}
			else
			{
				//Calculate the scale between min and max values
				//0 means use min value, 1 means use max value
				//0.2 means min value + 20% of the diff between min and max
				Real scale = i / ( data->m_numBeams - 1.0f);

				width		= data->m_innerBeamWidth	+ scale * (data->m_outerBeamWidth - data->m_innerBeamWidth);
				alpha		= innerAlpha							+ scale * (outerAlpha - innerAlpha);
				red			= innerRed								+ scale * (outerRed - innerRed) * innerAlpha;
				green		= innerGreen							+ scale * (outerGreen - innerGreen) * innerAlpha;
				blue		= innerBlue								+ scale * (outerBlue - innerBlue) * innerAlpha;
			}

			m_line3D[ index ] = NEW SegmentedLineClass;

			SegmentedLineClass *line = m_line3D[ index ];
			if( line )
			{
				line->Set_Texture( m_texture );
				line->Set_Shader( ShaderClass::_PresetAdditiveShader );	//pick the alpha blending mode you want - see shader.h for others.
				line->Set_Width( width );
				line->Set_Color( Vector3( red, green, blue ) );
				line->Set_UV_Offset_Rate( Vector2(0.0f, data->m_scrollRate) );	//amount to scroll texture on each draw
				if( m_texture )
				{
					line->Set_Texture_Mapping_Mode(SegLineRendererClass::TILED_TEXTURE_MAP);	//this tiles the texture across the line
				}

				// D3D11: Don't add to scene graph — lasers are rendered directly
				// in doDrawModule using the Renderer API to avoid scene graph
				// visibility/frustum issues.
				line->Set_Visible( 0 );
			}


		}

	}

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DLaserDraw::~W3DLaserDraw()
{
	const W3DLaserDrawModuleData *data = getW3DLaserDrawModuleData();

	for( UnsignedInt i = 0; i < data->m_numBeams * data->m_segments; i++ )
	{

		// remove line from scene
		if (W3DDisplay::m_3DScene != nullptr)
			W3DDisplay::m_3DScene->Remove_Render_Object( m_line3D[ i ] );

		// delete line
		REF_PTR_RELEASE( m_line3D[ i ] );

	}

	delete [] m_line3D;
	REF_PTR_RELEASE(m_texture);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Real W3DLaserDraw::getLaserTemplateWidth() const
{
	const W3DLaserDrawModuleData *data = getW3DLaserDrawModuleData();
	return data->m_outerBeamWidth * 0.5f;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
// Helper: build a camera-facing quad from two world-space points
static uint32_t BuildLaserQuad(
	const Render::Float3& cameraPos,
	const Vector3& p0, const Vector3& p1,
	float width, float r, float g, float b, float a,
	float tileFactor, float uvOffset,
	Render::Vertex3D* outVerts)
{
	float halfW = width * 0.5f;
	Vector3 segDir(p1.X - p0.X, p1.Y - p0.Y, p1.Z - p0.Z);
	float segLen = segDir.Length();
	if (segLen < 0.001f) return 0;
	segDir *= (1.0f / segLen);

	Vector3 mid((p0.X + p1.X) * 0.5f, (p0.Y + p1.Y) * 0.5f, (p0.Z + p1.Z) * 0.5f);
	Vector3 viewDir(mid.X - cameraPos.x, mid.Y - cameraPos.y, mid.Z - cameraPos.z);
	float vLen = viewDir.Length();
	if (vLen < 0.001f) return 0;
	viewDir *= (1.0f / vLen);

	Vector3 right;
	Vector3::Cross_Product(segDir, viewDir, &right);
	float rLen = right.Length();
	if (rLen < 0.0001f) return 0;
	right *= (halfW / rLen);

	uint8_t cr = (uint8_t)(r * 255.0f > 255.0f ? 255 : r * 255.0f);
	uint8_t cg = (uint8_t)(g * 255.0f > 255.0f ? 255 : g * 255.0f);
	uint8_t cb = (uint8_t)(b * 255.0f > 255.0f ? 255 : b * 255.0f);
	uint8_t ca = (uint8_t)(a * 255.0f > 255.0f ? 255 : a * 255.0f);
	uint32_t color = (ca << 24) | (cb << 16) | (cg << 8) | cr;

	float v0 = uvOffset;
	float v1 = uvOffset + tileFactor;

	// Two triangles forming a quad
	auto setVert = [&](int idx, const Vector3& base, const Vector3& off, float u, float v) {
		outVerts[idx].position = { base.X + off.X, base.Y + off.Y, base.Z + off.Z };
		outVerts[idx].normal = { viewDir.X, viewDir.Y, viewDir.Z };
		outVerts[idx].texcoord = { u, v };
		outVerts[idx].color = color;
	};
	setVert(0, p0, right, 0, v0);
	setVert(1, p0, Vector3(-right.X, -right.Y, -right.Z), 1, v0);
	setVert(2, p1, right, 0, v1);
	setVert(3, p1, right, 0, v1);
	setVert(4, p0, Vector3(-right.X, -right.Y, -right.Z), 1, v0);
	setVert(5, p1, Vector3(-right.X, -right.Y, -right.Z), 1, v1);
	return 6;
}

void W3DLaserDraw::doDrawModule(const Matrix3D* transformMtx)
{
	const W3DLaserDrawModuleData *data = getW3DLaserDrawModuleData();

	Drawable *draw = getDrawable();
	static NameKeyType key_LaserUpdate = NAMEKEY( "LaserUpdate" );
	LaserUpdate *update = (LaserUpdate*)draw->findClientUpdateModule( key_LaserUpdate );
	if( !update )
		return;

	if (update->isDirty() || m_selfDirty)
	{
		update->setDirty(false);
		m_selfDirty = false;
	}

	const Coord3D* startPos = update->getStartPos();
	const Coord3D* endPos = update->getEndPos();
	float dx = endPos->x - startPos->x;
	float dy = endPos->y - startPos->y;
	float dz = endPos->z - startPos->z;
	float beamLen = sqrtf(dx*dx + dy*dy + dz*dz);
	if (beamLen < 0.5f) return;

	auto& renderer = Render::Renderer::Instance();
	const auto& frameData = renderer.GetFrameData();
	Render::Float3 camPos = { frameData.cameraPos.x, frameData.cameraPos.y, frameData.cameraPos.z };

	// White fallback texture
	static Render::Texture s_whiteTex;
	static bool s_whiteTexReady = false;
	if (!s_whiteTexReady) {
		const uint32_t white = 0xFFFFFFFF;
		s_whiteTex.CreateFromRGBA(renderer.GetDevice(), &white, 1, 1, false);
		s_whiteTexReady = true;
	}
	Real innerRed, innerGreen, innerBlue, innerAlpha;
	GameGetColorComponentsReal( data->m_innerColor, &innerRed, &innerGreen, &innerBlue, &innerAlpha );

	// Boost dim colors for additive visibility
	Real maxC = innerRed > innerGreen ? (innerRed > innerBlue ? innerRed : innerBlue) : (innerGreen > innerBlue ? innerGreen : innerBlue);
	if (maxC > 0.001f && maxC < 0.5f) {
		Real boost = 0.5f / maxC;
		innerRed *= boost; innerGreen *= boost; innerBlue *= boost;
	}
	if (innerAlpha < 0.5f) innerAlpha = 0.5f;

	Render::Float4x4 identity = {
		1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
	};

	Vector3 p0(startPos->x, startPos->y, startPos->z);
	Vector3 p1(endPos->x, endPos->y, endPos->z);

	renderer.SetLaserGlow3DState();

	float glowWidth = data->m_outerBeamWidth * update->getWidthScale() * 1.5f;
	if (glowWidth < 0.75f) glowWidth = 0.75f;

	Render::Vertex3D verts[6];
	uint32_t vertCount = BuildLaserQuad(camPos, p0, p1,
		glowWidth, innerRed, innerGreen, innerBlue, innerAlpha,
		1.0f, 0.0f, verts);

	if (vertCount > 0)
	{
		Render::VertexBuffer vb;
		vb.Create(renderer.GetDevice(), verts, vertCount, sizeof(Render::Vertex3D));
		renderer.Draw3DNoIndex(vb, vertCount, &s_whiteTex, identity, {1,1,1,1});
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DLaserDraw::crc( Xfer *xfer )
{

	// extend base class
	DrawModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void W3DLaserDraw::xfer( Xfer *xfer )
{

	// version
	const XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	DrawModule::xfer( xfer );

	// Kris says there is no data to save for these, go ask him.
	// m_selfDirty is not saved, is runtime only

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DLaserDraw::loadPostProcess()
{

	// extend base class
	DrawModule::loadPostProcess();

	m_selfDirty = true;	// so we update the first time after reload

}
