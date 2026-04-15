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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : WW3D                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/pointgr.cpp                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   PointGroupClass::PointGroupClass -- constructor                                           *
 *   PointGroupClass::~PointGroupClass -- destructor                                           *
 *   PointGroupClass::operator= -- assignment operator                                         *
 *   PointGroupClass::Set_Arrays -- set shared buffer arrays                                   *
 *   PointGroupClass::Set_Texture -- set texture with ref counting                             *
 *   PointGroupClass::Get_Texture -- get texture with add-ref                                  *
 *   PointGroupClass::Peek_Texture -- get texture without add-ref                              *
 *   PointGroupClass::Render -- no-op (D3D11 handles rendering externally)                     *
 *   PointGroupClass::RenderVolumeParticle -- no-op                                            *
 *   PointGroupClass::_Init -- build static orientation and UV tables                          *
 *   PointGroupClass::_Shutdown -- release UV frame tables                                     *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "pointgr.h"
#include "texture.h"
#include "shader.h"
#include "refcount.h"
#include "wwmath.h"
#include <math.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------

Vector3 PointGroupClass::_TriVertexLocationOrientationTable[256][3];
Vector3 PointGroupClass::_QuadVertexLocationOrientationTable[256][4];
Vector3 PointGroupClass::_ScreenspaceVertexLocationSizeTable[2][3];
Vector2 *PointGroupClass::_TriVertexUVFrameTable[5]  = { nullptr, nullptr, nullptr, nullptr, nullptr };
Vector2 *PointGroupClass::_QuadVertexUVFrameTable[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
VertexMaterialClass *PointGroupClass::PointMaterial = nullptr;

VectorClass<Vector3>      PointGroupClass::compressed_loc;
VectorClass<Vector4>      PointGroupClass::compressed_diffuse;
VectorClass<float>        PointGroupClass::compressed_size;
VectorClass<unsigned char> PointGroupClass::compressed_orient;
VectorClass<unsigned char> PointGroupClass::compressed_frame;
VectorClass<Vector3>      PointGroupClass::transformed_loc;

// ---------------------------------------------------------------------------
// PointGroupClass::PointGroupClass
// ---------------------------------------------------------------------------
PointGroupClass::PointGroupClass() :
	PointLoc(nullptr),
	PointDiffuse(nullptr),
	APT(nullptr),
	PointSize(nullptr),
	PointOrientation(nullptr),
	PointFrame(nullptr),
	PointCount(0),
	FrameRowColumnCountLog2(0),
	Texture(nullptr),
	Shader(ShaderClass::_PresetAdditiveSpriteShader),
	PointMode(TRIS),
	Flags(0),
	DefaultPointSize(1.0f),
	DefaultPointColor(1.0f, 1.0f, 1.0f),
	DefaultPointAlpha(1.0f),
	DefaultPointOrientation(0),
	DefaultPointFrame(0),
	VPXMin(0.0f),
	VPYMin(0.0f),
	VPXMax(0.0f),
	VPYMax(0.0f),
	Billboard(false)
{
}

// ---------------------------------------------------------------------------
// PointGroupClass::~PointGroupClass
// ---------------------------------------------------------------------------
PointGroupClass::~PointGroupClass()
{
	REF_PTR_RELEASE(PointLoc);
	REF_PTR_RELEASE(PointDiffuse);
	REF_PTR_RELEASE(APT);
	REF_PTR_RELEASE(PointSize);
	REF_PTR_RELEASE(PointOrientation);
	REF_PTR_RELEASE(PointFrame);
	REF_PTR_RELEASE(Texture);
}

// ---------------------------------------------------------------------------
// PointGroupClass::operator=
// ---------------------------------------------------------------------------
PointGroupClass & PointGroupClass::operator = (const PointGroupClass & that)
{
	if (this != &that) {
		REF_PTR_SET(PointLoc, that.PointLoc);
		REF_PTR_SET(PointDiffuse, that.PointDiffuse);
		REF_PTR_SET(APT, that.APT);
		REF_PTR_SET(PointSize, that.PointSize);
		REF_PTR_SET(PointOrientation, that.PointOrientation);
		REF_PTR_SET(PointFrame, that.PointFrame);
		REF_PTR_SET(Texture, that.Texture);

		PointCount               = that.PointCount;
		FrameRowColumnCountLog2  = that.FrameRowColumnCountLog2;
		Shader                   = that.Shader;
		PointMode                = that.PointMode;
		Flags                    = that.Flags;
		DefaultPointSize         = that.DefaultPointSize;
		DefaultPointColor        = that.DefaultPointColor;
		DefaultPointAlpha        = that.DefaultPointAlpha;
		DefaultPointOrientation  = that.DefaultPointOrientation;
		DefaultPointFrame        = that.DefaultPointFrame;
		VPXMin                   = that.VPXMin;
		VPYMin                   = that.VPYMin;
		VPXMax                   = that.VPXMax;
		VPYMax                   = that.VPYMax;
		Billboard                = that.Billboard;
	}
	return *this;
}

// ---------------------------------------------------------------------------
// PointGroupClass::Set_Arrays
// Store input buffer pointers using ref-counted assignment.  active_point_count
// of -1 means use the full size of the PointLoc buffer.
// ---------------------------------------------------------------------------
void PointGroupClass::Set_Arrays(
	ShareBufferClass<Vector3>      *locs,
	ShareBufferClass<Vector4>      *diffuse,
	ShareBufferClass<unsigned int> *apt,
	ShareBufferClass<float>        *sizes,
	ShareBufferClass<unsigned char>*orientations,
	ShareBufferClass<unsigned char>*frames,
	int active_point_count,
	float vpxmin, float vpymin,
	float vpxmax, float vpymax)
{
	REF_PTR_SET(PointLoc,         locs);
	REF_PTR_SET(PointDiffuse,     diffuse);
	REF_PTR_SET(APT,              apt);
	REF_PTR_SET(PointSize,        sizes);
	REF_PTR_SET(PointOrientation, orientations);
	REF_PTR_SET(PointFrame,       frames);

	if (active_point_count < 0) {
		PointCount = locs ? locs->Get_Count() : 0;
	} else {
		PointCount = active_point_count;
	}

	VPXMin = vpxmin;
	VPYMin = vpymin;
	VPXMax = vpxmax;
	VPYMax = vpymax;
}

// ---------------------------------------------------------------------------
// PointGroupClass::Set_Texture / Get_Texture / Peek_Texture
// ---------------------------------------------------------------------------
void PointGroupClass::Set_Texture(TextureClass *texture)
{
	REF_PTR_SET(Texture, texture);
}

TextureClass * PointGroupClass::Get_Texture()
{
	if (Texture) {
		Texture->Add_Ref();
	}
	return Texture;
}

TextureClass * PointGroupClass::Peek_Texture()
{
	return Texture;
}

// ---------------------------------------------------------------------------
// PointGroupClass::Set_Shader / Get_Shader
// ---------------------------------------------------------------------------
void PointGroupClass::Set_Shader(ShaderClass shader)
{
	Shader = shader;
}

ShaderClass PointGroupClass::Get_Shader()
{
	return Shader;
}

// ---------------------------------------------------------------------------
// PointGroupClass::Set_Billboard / Get_Billboard
// ---------------------------------------------------------------------------
void PointGroupClass::Set_Billboard(bool shouldBillboard)
{
	Billboard = shouldBillboard;
}

bool PointGroupClass::Get_Billboard()
{
	return Billboard;
}

// ---------------------------------------------------------------------------
// PointGroupClass::Set_Point_Size / Get_Point_Size
// ---------------------------------------------------------------------------
void PointGroupClass::Set_Point_Size(float size)
{
	DefaultPointSize = size;
}

float PointGroupClass::Get_Point_Size()
{
	return DefaultPointSize;
}

// ---------------------------------------------------------------------------
// PointGroupClass::Set_Point_Color / Get_Point_Color
// ---------------------------------------------------------------------------
void PointGroupClass::Set_Point_Color(Vector3 color)
{
	DefaultPointColor = color;
}

Vector3 PointGroupClass::Get_Point_Color()
{
	return DefaultPointColor;
}

// ---------------------------------------------------------------------------
// PointGroupClass::Set_Point_Alpha / Get_Point_Alpha
// ---------------------------------------------------------------------------
void PointGroupClass::Set_Point_Alpha(float alpha)
{
	DefaultPointAlpha = alpha;
}

float PointGroupClass::Get_Point_Alpha()
{
	return DefaultPointAlpha;
}

// ---------------------------------------------------------------------------
// PointGroupClass::Set_Point_Orientation / Get_Point_Orientation
// ---------------------------------------------------------------------------
void PointGroupClass::Set_Point_Orientation(unsigned char orientation)
{
	DefaultPointOrientation = orientation;
}

unsigned char PointGroupClass::Get_Point_Orientation()
{
	return DefaultPointOrientation;
}

// ---------------------------------------------------------------------------
// PointGroupClass::Set_Point_Frame / Get_Point_Frame
// ---------------------------------------------------------------------------
void PointGroupClass::Set_Point_Frame(unsigned char frame)
{
	DefaultPointFrame = frame;
}

unsigned char PointGroupClass::Get_Point_Frame()
{
	return DefaultPointFrame;
}

// ---------------------------------------------------------------------------
// PointGroupClass::Set_Point_Mode / Get_Point_Mode
// ---------------------------------------------------------------------------
void PointGroupClass::Set_Point_Mode(PointModeEnum mode)
{
	PointMode = mode;
}

PointGroupClass::PointModeEnum PointGroupClass::Get_Point_Mode()
{
	return PointMode;
}

// ---------------------------------------------------------------------------
// PointGroupClass::Set_Flag / Get_Flag
// ---------------------------------------------------------------------------
void PointGroupClass::Set_Flag(FlagsType flag, bool onoff)
{
	if (onoff) {
		Flags |= (1u << flag);
	} else {
		Flags &= ~(1u << flag);
	}
}

int PointGroupClass::Get_Flag(FlagsType flag)
{
	return (Flags >> flag) & 1;
}

// ---------------------------------------------------------------------------
// PointGroupClass::Get_Frame_Row_Column_Count_Log2 / Set_...
// ---------------------------------------------------------------------------
unsigned char PointGroupClass::Get_Frame_Row_Column_Count_Log2()
{
	return FrameRowColumnCountLog2;
}

void PointGroupClass::Set_Frame_Row_Column_Count_Log2(unsigned char frccl2)
{
	if (frccl2 > 4) frccl2 = 4;
	FrameRowColumnCountLog2 = frccl2;
}

// ---------------------------------------------------------------------------
// PointGroupClass::Get_Polygon_Count
// ---------------------------------------------------------------------------
int PointGroupClass::Get_Polygon_Count()
{
	// Each point in QUADS mode generates 2 triangles; TRIS/SCREENSPACE uses 1.
	if (PointMode == QUADS) {
		return PointCount * 2;
	}
	return PointCount;
}

// ---------------------------------------------------------------------------
// PointGroupClass::Render
// No-op: D3D11 rendering is driven externally by the scene iteration path.
// ---------------------------------------------------------------------------
void PointGroupClass::Render(RenderInfoClass & /*rinfo*/)
{
	// Intentionally empty — rendering handled by D3D11Shims scene iteration.
}

// ---------------------------------------------------------------------------
// PointGroupClass::RenderVolumeParticle
// No-op.
// ---------------------------------------------------------------------------
void PointGroupClass::RenderVolumeParticle(RenderInfoClass & /*rinfo*/, unsigned int /*depth*/)
{
	// Intentionally empty.
}

// ---------------------------------------------------------------------------
// PointGroupClass::Update_Arrays
// Stub implementation — processing happens in the D3D11 renderer.
// ---------------------------------------------------------------------------
void PointGroupClass::Update_Arrays(
	Vector3      * /*point_loc*/,
	Vector4      * /*point_diffuse*/,
	float        * /*point_size*/,
	unsigned char* /*point_orientation*/,
	unsigned char* /*point_frame*/,
	int            /*active_points*/,
	int            /*total_points*/,
	int          & vnum,
	int          & pnum)
{
	vnum = 0;
	pnum = 0;
}

// ---------------------------------------------------------------------------
// PointGroupClass::_Init
//
// Build 256-entry orientation tables for both triangle and quad modes, and
// allocate five UV frame tables for each mode (1x1, 2x2, 4x4, 8x8, 16x16).
//
// Triangle base vertices (unit equilateral triangle, centred at origin):
//   v0 = ( 0,        2/3,     0 )   — top
//   v1 = (-sqrt3/3, -1/3,     0 )   — bottom-left
//   v2 = ( sqrt3/3, -1/3,     0 )   — bottom-right
//
// Quad base vertices (unit square, centred at origin):
//   v0 = (-0.5,  0.5,  0 )
//   v1 = ( 0.5,  0.5,  0 )
//   v2 = ( 0.5, -0.5,  0 )
//   v3 = (-0.5, -0.5,  0 )
//
// Each entry is produced by rotating the base by angle = i * 2*PI / 256.
// ---------------------------------------------------------------------------
void PointGroupClass::_Init()
{
	// --- Base tri vertices ---
	const float kTriRadius = 2.0f / 3.0f;
	const float kTriHalfBase = WWMath::Sqrt(3.0f) / 3.0f;

	const Vector3 triBase[3] = {
		Vector3(  0.0f,       kTriRadius,  0.0f),
		Vector3( -kTriHalfBase, -kTriRadius * 0.5f, 0.0f),
		Vector3(  kTriHalfBase, -kTriRadius * 0.5f, 0.0f)
	};

	// --- Base quad vertices ---
	const Vector3 quadBase[4] = {
		Vector3(-0.5f,  0.5f, 0.0f),
		Vector3( 0.5f,  0.5f, 0.0f),
		Vector3( 0.5f, -0.5f, 0.0f),
		Vector3(-0.5f, -0.5f, 0.0f)
	};

	// --- Fill orientation tables ---
	for (int i = 0; i < 256; ++i) {
		float angle = (float)i * (2.0f * WWMATH_PI / 256.0f);
		float cosA  = WWMath::Cos(angle);
		float sinA  = WWMath::Sin(angle);

		for (int v = 0; v < 3; ++v) {
			float x = triBase[v].X;
			float y = triBase[v].Y;
			_TriVertexLocationOrientationTable[i][v].Set(
				cosA * x - sinA * y,
				sinA * x + cosA * y,
				0.0f
			);
		}

		for (int v = 0; v < 4; ++v) {
			float x = quadBase[v].X;
			float y = quadBase[v].Y;
			_QuadVertexLocationOrientationTable[i][v].Set(
				cosA * x - sinA * y,
				sinA * x + cosA * y,
				0.0f
			);
		}
	}

	// --- Screenspace size table (2 entries: size 1 and size 2) ---
	// These are simple screen-aligned triangles.
	for (int s = 0; s < 2; ++s) {
		float scale = (float)(s + 1);
		_ScreenspaceVertexLocationSizeTable[s][0].Set( 0.0f,       scale * (2.0f / 3.0f),       0.0f);
		_ScreenspaceVertexLocationSizeTable[s][1].Set(-scale * (WWMath::Sqrt(3.0f) / 3.0f), -scale * (1.0f / 3.0f), 0.0f);
		_ScreenspaceVertexLocationSizeTable[s][2].Set( scale * (WWMath::Sqrt(3.0f) / 3.0f), -scale * (1.0f / 3.0f), 0.0f);
	}

	// --- Build UV frame tables ---
	// gridSizes[k] = rows/cols = 2^k, giving frameCount = gridSizes[k]^2 frames.
	for (int k = 0; k < 5; ++k) {
		int  gridSize   = 1 << k;           // 1, 2, 4, 8, 16
		int  frameCount = gridSize * gridSize; // 1, 4, 16, 64, 256
		float cellSize  = 1.0f / (float)gridSize;

		// 3 UVs per triangle frame, 4 UVs per quad frame
		_TriVertexUVFrameTable[k]  = W3DNEWARRAY Vector2[frameCount * 3];
		_QuadVertexUVFrameTable[k] = W3DNEWARRAY Vector2[frameCount * 4];

		for (int f = 0; f < frameCount; ++f) {
			int col = f % gridSize;
			int row = f / gridSize;

			float u0 = col       * cellSize;
			float v0 = row       * cellSize;
			float u1 = (col + 1) * cellSize;
			float v1 = (row + 1) * cellSize;

			// Triangle UVs: map the three vertices to a rectangular sub-region.
			// We use a triangle that covers the cell with one vertex at the centre-top
			// and two at the bottom corners.
			Vector2 *tri = &_TriVertexUVFrameTable[k][f * 3];
			tri[0].Set((u0 + u1) * 0.5f, v0);       // top centre
			tri[1].Set(u0,               v1);        // bottom-left
			tri[2].Set(u1,               v1);        // bottom-right

			// Quad UVs: standard four corners (matches quadBase vertex order).
			Vector2 *quad = &_QuadVertexUVFrameTable[k][f * 4];
			quad[0].Set(u0, v0);   // top-left    (-0.5,  0.5)
			quad[1].Set(u1, v0);   // top-right   ( 0.5,  0.5)
			quad[2].Set(u1, v1);   // bottom-right( 0.5, -0.5)
			quad[3].Set(u0, v1);   // bottom-left (-0.5, -0.5)
		}
	}
}

// ---------------------------------------------------------------------------
// PointGroupClass::_Shutdown
// Release allocated UV frame table arrays.
// ---------------------------------------------------------------------------
void PointGroupClass::_Shutdown()
{
	for (int k = 0; k < 5; ++k) {
		delete[] _TriVertexUVFrameTable[k];
		_TriVertexUVFrameTable[k] = nullptr;

		delete[] _QuadVertexUVFrameTable[k];
		_QuadVertexUVFrameTable[k] = nullptr;
	}
}

// ---------------------------------------------------------------------------
// SegmentGroupClass
// ---------------------------------------------------------------------------
SegmentGroupClass::SegmentGroupClass()
{
}

SegmentGroupClass::~SegmentGroupClass()
{
}
