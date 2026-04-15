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

// FILE: W3DProjectileStreamDraw.cpp ////////////////////////////////////////////////////////////
// Tile a texture strung between Projectiles
// Graham Smallwood, May 2002
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameLogic/Object.h"
#include "W3DDevice/GameClient/Module/W3DProjectileStreamDraw.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/ImageCache.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/segline.h"
#include "WWMath/vector3.h"
#include "Renderer.h"
#include "Texture.h"
#include <unordered_map>
#include <string>

//-------------------------------------------------------------------------------------------------
W3DProjectileStreamDrawModuleData::W3DProjectileStreamDrawModuleData()
{
	m_textureName = "";
	m_width = 0.0f;
	m_tileFactor = 0.0f;
	m_scrollRate = 0.0f;
	m_maxSegments = 0;
}

//-------------------------------------------------------------------------------------------------
W3DProjectileStreamDrawModuleData::~W3DProjectileStreamDrawModuleData()
{
}

//-------------------------------------------------------------------------------------------------
void W3DProjectileStreamDrawModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "Texture",			INI::parseAsciiString,	nullptr, offsetof(W3DProjectileStreamDrawModuleData, m_textureName) },
		{ "Width",				INI::parseReal,					nullptr, offsetof(W3DProjectileStreamDrawModuleData, m_width) },
		{ "TileFactor",		INI::parseReal,					nullptr, offsetof(W3DProjectileStreamDrawModuleData, m_tileFactor) },
		{ "ScrollRate",		INI::parseReal,					nullptr, offsetof(W3DProjectileStreamDrawModuleData, m_scrollRate) },
		{ "MaxSegments",	INI::parseInt,					nullptr, offsetof(W3DProjectileStreamDrawModuleData, m_maxSegments) },
		{ nullptr, nullptr, nullptr, 0 }
	};
  p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DProjectileStreamDraw::~W3DProjectileStreamDraw()
{
	for( Int lineIndex = 0; lineIndex < m_linesValid; lineIndex++ )
	{
		SegmentedLineClass *deadLine = m_allLines[lineIndex];
		if (deadLine)
		{	// D3D11: scene removal handled by Renderer
			REF_PTR_RELEASE( deadLine );
		}
	}

	REF_PTR_RELEASE( m_texture );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DProjectileStreamDraw::W3DProjectileStreamDraw( Thing *thing, const ModuleData* moduleData ) : DrawModule( thing, moduleData )
{
	const W3DProjectileStreamDrawModuleData* d = getW3DProjectileStreamDrawModuleData();
	WW3DAssetManager *assetManager = WW3DAssetManager::Get_Instance();
	m_texture = assetManager ? assetManager->Get_Texture( d->m_textureName.str() ) : nullptr;
	for( Int index = 0; index < MAX_PROJECTILE_STREAM; index++ )
		m_allLines[index] = nullptr;
	m_linesValid = 0;
}

void W3DProjectileStreamDraw::setFullyObscuredByShroud(Bool fullyObscured)
{
	if (fullyObscured)
	{	//we need to remove all our lines from the scene because they are hidden
		for( Int lineIndex = 0; lineIndex < m_linesValid; lineIndex++ )
		{
			SegmentedLineClass *deadLine = m_allLines[lineIndex];
			if (deadLine && deadLine->Peek_Scene())
				deadLine->Remove();
		}
	}
	else
	{	//we need to restore lines into scene
		for( Int lineIndex = 0; lineIndex < m_linesValid; lineIndex++ )
		{
			SegmentedLineClass *deadLine = m_allLines[lineIndex];
			(void)deadLine; // D3D11: scene add handled by Renderer
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Map behavior states into W3D animations. */
//-------------------------------------------------------------------------------------------------
void W3DProjectileStreamDraw::doDrawModule(const Matrix3D* )
{
	// get object from logic
	Object *me = getDrawable()->getObject();
	if (me == nullptr)
		return;

	static NameKeyType key_ProjectileStreamUpdate = NAMEKEY("ProjectileStreamUpdate");
	ProjectileStreamUpdate* update = (ProjectileStreamUpdate*)me->findUpdateModule(key_ProjectileStreamUpdate);

	const W3DProjectileStreamDrawModuleData *data = getW3DProjectileStreamDrawModuleData();

	Vector3 allPoints[MAX_PROJECTILE_STREAM];
	Int pointsUsed;

	update->getAllPoints( allPoints, &pointsUsed );

	Vector3 stagingPoints[MAX_PROJECTILE_STREAM];
	Vector3 zeroVector(0, 0, 0);

	Int linesMade = 0;
	Int currentMasterPoint = 0;
	UnsignedInt currentStagingPoint = 0;

	if( data->m_maxSegments )
	{
		// If I have a drawing cap, I need to increase the start point in the array.  The furthest (oldest)
		// point from the tank is in spot zero.
		currentMasterPoint = pointsUsed - data->m_maxSegments;
		currentMasterPoint = max( 0, currentMasterPoint ); // (but if they say to draw more than exists, draw all)
	}

	// Okay.  I have an array of ordered points that may have blanks in it.  I need to copy to the staging area
	// until I hit a blank or the end.  Then if I have a line made, I'll overwrite it, otherwise I'll make a new one.
	// I'll keep doing this until I run out of valid points.
	while( currentMasterPoint < pointsUsed )
	{
		while( currentMasterPoint < pointsUsed  &&  allPoints[currentMasterPoint] != zeroVector )
		{
			// While I am not looking at a bad point (off edge or zero)
			stagingPoints[currentStagingPoint] = allPoints[currentMasterPoint];// copy to the staging
			currentStagingPoint++;// increment how many I have
			currentMasterPoint++;// increment what I am looking at
		}
		// Use or reuse a line
		if( currentStagingPoint > 1 )
		{
			// Don't waste a line on a double hole (0) or a one point line (1)
			makeOrUpdateLine( stagingPoints, currentStagingPoint, linesMade );
			linesMade++;// keep track of how many are real this frame
		}
		currentMasterPoint++;//I am either pointed off the edge anyway, or I am pointed at a zero I want to skip
		currentStagingPoint = 0;//start over in the staging area
	}

	Int oldLinesValid = m_linesValid;
	for( Int lineIndex = linesMade; lineIndex < oldLinesValid; lineIndex++ )
	{
		// Delete any line we aren't using anymore.
		SegmentedLineClass *deadLine = m_allLines[lineIndex];
		if (deadLine->Peek_Scene())
			// D3D11: scene operations handled by Renderer
		REF_PTR_RELEASE( deadLine );

		m_allLines[lineIndex] = nullptr;
		m_linesValid--;
	}

	// --- D3D11 inline render of all active SegmentedLineClass entries ---
	// The original DX8 path added each SegmentedLineClass to W3DDisplay::
	// m_3DScene and let RTS3DScene::Customized_Render walk it. The D3D11
	// pipeline iterates Drawables instead of the W3D scene, so the lines
	// are never picked up. Render them here as a strip of screen-aligned
	// additive quads, mirroring the W3DTracerDraw / W3DRopeDraw fix
	// pattern. This produces the smoke/flame trail behind SCUDs and other
	// projectile-stream weapons.
	if (m_linesValid > 0 && data->m_width > 0.001f)
	{
		// Per-texture-name cache so we resolve and create the D3D11
		// texture exactly once per unique smoke/flame texture name.
		// Cleared via the existing ImageCache shutdown path.
		static std::unordered_map<std::string, Render::Texture*> s_streamTextureCache;
		Render::Texture* d3d11Tex = nullptr;
		const char* texName = data->m_textureName.str();
		if (texName && texName[0])
		{
			auto it = s_streamTextureCache.find(texName);
			if (it != s_streamTextureCache.end())
			{
				d3d11Tex = it->second;
			}
			else
			{
				d3d11Tex = Render::ImageCache::Instance().GetTexture(
					Render::Renderer::Instance().GetDevice(), texName);
				s_streamTextureCache[texName] = d3d11Tex;
			}
		}

		// Fall back to a tiny white texture so the additive shader has
		// something to sample even when the named texture failed to load.
		static Render::Texture s_whiteTex;
		static bool s_whiteTexReady = false;
		if (!s_whiteTexReady)
		{
			const uint32_t white = 0xFFFFFFFF;
			s_whiteTex.CreateFromRGBA(Render::Renderer::Instance().GetDevice(), &white, 1, 1, false);
			s_whiteTexReady = true;
		}
		Render::Texture* boundTex = d3d11Tex ? d3d11Tex : &s_whiteTex;

		auto& renderer = Render::Renderer::Instance();
		const auto& frameData = renderer.GetFrameData();
		const Render::Float3 camPos = {
			frameData.cameraPos.x, frameData.cameraPos.y, frameData.cameraPos.z };

		const Real halfW = data->m_width * 0.5f;
		const Real tileFactor = data->m_tileFactor > 0.001f ? data->m_tileFactor : 1.0f;
		// Animate the V scroll the same way the original
		// SegLineRenderer did via Set_UV_Offset_Rate. The original used
		// frame-count-based offsets; we drive it from sync time so the
		// scroll rate is wall-clock-stable across render frame rates.
		const float scrollV =
			data->m_scrollRate * (float)WW3D::Get_Sync_Time() * 0.001f;

		// Use additive blending — the original used
		// _PresetAdditiveSpriteShader for the line shader.
		renderer.SetLaserGlow3DState();

		Render::Float4x4 identity = {
			1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1
		};

		for (Int lineIndex = 0; lineIndex < m_linesValid; ++lineIndex)
		{
			SegmentedLineClass* line = m_allLines[lineIndex];
			if (!line)
				continue;

			const int numPts = line->Get_Num_Points();
			if (numPts < 2)
				continue;

			// Build a contiguous quad strip across all segments. Each
			// segment shares its end-cap vertices with the next segment so
			// the texture stays continuous along the polyline.
			std::vector<Render::Vertex3D> verts;
			verts.reserve((size_t)(numPts - 1) * 6);

			for (int p = 0; p < numPts - 1; ++p)
			{
				Vector3 p0;
				Vector3 p1;
				line->Get_Point_Location((unsigned)p, p0);
				line->Get_Point_Location((unsigned)(p + 1), p1);

				Vector3 segDir(p1.X - p0.X, p1.Y - p0.Y, p1.Z - p0.Z);
				Real segLen = segDir.Length();
				if (segLen < 0.001f)
					continue;
				segDir *= (1.0f / segLen);

				Vector3 mid((p0.X + p1.X) * 0.5f,
				            (p0.Y + p1.Y) * 0.5f,
				            (p0.Z + p1.Z) * 0.5f);
				Vector3 viewDir(mid.X - camPos.x, mid.Y - camPos.y, mid.Z - camPos.z);
				Real vLen = viewDir.Length();
				if (vLen < 0.001f)
					continue;
				viewDir *= (1.0f / vLen);

				Vector3 right;
				Vector3::Cross_Product(segDir, viewDir, &right);
				Real rLen = right.Length();
				if (rLen < 0.0001f)
					continue;
				right *= (halfW / rLen);

				// Tile the texture across each segment. p_idx steps the
				// V coordinate so consecutive segments are continuous.
				const float v0 = scrollV + (float)p * tileFactor;
				const float v1 = scrollV + (float)(p + 1) * tileFactor;

				const uint32_t white = 0xFFFFFFFF;

				auto pushVert = [&](const Vector3& base, const Vector3& off,
				                    float u, float v) {
					Render::Vertex3D vx{};
					vx.position = { base.X + off.X, base.Y + off.Y, base.Z + off.Z };
					vx.normal = { viewDir.X, viewDir.Y, viewDir.Z };
					vx.texcoord = { u, v };
					vx.color = white;
					verts.push_back(vx);
				};

				const Vector3 negRight(-right.X, -right.Y, -right.Z);
				pushVert(p0, right,    0.0f, v0);
				pushVert(p0, negRight, 1.0f, v0);
				pushVert(p1, right,    0.0f, v1);
				pushVert(p1, right,    0.0f, v1);
				pushVert(p0, negRight, 1.0f, v0);
				pushVert(p1, negRight, 1.0f, v1);
			}

			if (verts.empty())
				continue;

			Render::VertexBuffer vb;
			vb.Create(renderer.GetDevice(), verts.data(),
				(uint32_t)verts.size(), sizeof(Render::Vertex3D));
			renderer.Draw3DNoIndex(vb, (uint32_t)verts.size(),
				boundTex, identity, { 1, 1, 1, 1 });
		}
	}
}

void W3DProjectileStreamDraw::makeOrUpdateLine( Vector3 *points, UnsignedInt pointCount, Int lineIndex )
{
	Bool newLine = FALSE;

	if( m_allLines[lineIndex] == nullptr )
	{
		//Need a new one if this is blank, otherwise I'll reset the existing one
		m_allLines[lineIndex] = NEW SegmentedLineClass;
		m_linesValid++;
		newLine = TRUE;
	}

	SegmentedLineClass *line = m_allLines[lineIndex];

	line->Set_Points(pointCount, points);	//tell the line which points to use

	if( newLine )
	{
		// This is one time stuff we only need to do if this is a new and not a change
		const W3DProjectileStreamDrawModuleData *data = getW3DProjectileStreamDrawModuleData();
		line->Set_Texture(m_texture);	//set the texture
		line->Set_Shader(ShaderClass::_PresetAdditiveSpriteShader);	//pick the alpha blending mode you want - see shader.h for others.
		line->Set_Width(data->m_width);	//set line width in world units
		line->Set_Texture_Mapping_Mode(SegLineRendererClass::TILED_TEXTURE_MAP);	//this tiles the texture across the line
		line->Set_Texture_Tile_Factor(data->m_tileFactor);	//number of times to tile texture across each segment
		line->Set_UV_Offset_Rate(Vector2(0.0f,data->m_scrollRate));	//amount to scroll texture on each draw
		// D3D11: scene operations handled by Renderer
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DProjectileStreamDraw::crc( Xfer *xfer )
{

	// extend base class
	DrawModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void W3DProjectileStreamDraw::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	DrawModule::xfer( xfer );

	// Graham says there is no data that needs saving here

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DProjectileStreamDraw::loadPostProcess()
{

	// extend base class
	DrawModule::loadPostProcess();

}
