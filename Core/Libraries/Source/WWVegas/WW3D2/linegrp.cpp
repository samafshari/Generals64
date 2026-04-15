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
 *                 Project Name : Linegroup.cpp                                                *
 *                                                                                             *
 *              Original Author:: Hector Yee                                                   *
 *                                                                                             *
 *                      $Author:: Kenny Mitchell                                               $*
 *                                                                                             *
 *                     $Modtime:: 06/26/02 4:04p                                             $*
 *                                                                                             *
 *                    $Revision:: 2                                                            $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 *---------------------------------------------------------------------------------------------*
 *
 * D3D11 port notes:
 *   Render() is intentionally a no-op.  Line-mode particle rendering is not
 *   used by the D3D11 pipeline; the scene iteration in D3D11Shims / ModelRenderer
 *   handles all visible geometry.  All data-management methods are fully
 *   implemented so that callers which set up a LineGroupClass (e.g. line-mode
 *   particle emitters) do not crash or leak.
 *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "linegrp.h"
#include "sharebuf.h"
#include "texture.h"
#include "rinfo.h"

// -------------------------------------------------------------------------
// LineGroupClass
// -------------------------------------------------------------------------

LineGroupClass::LineGroupClass(void)
	: StartLineLoc(NULL),
	  EndLineLoc(NULL),
	  LineDiffuse(NULL),
	  TailDiffuse(NULL),
	  ALT(NULL),
	  LineSize(NULL),
	  LineUCoord(NULL),
	  LineCount(0),
	  Texture(NULL),
	  Shader(ShaderClass::_PresetAdditiveSpriteShader),
	  Flags(0),
	  DefaultLineSize(0.0f),
	  DefaultLineColor(1.0f, 1.0f, 1.0f),
	  DefaultLineAlpha(1.0f),
	  DefaultLineUCoord(0.0f),
	  DefaultTailDiffuse(0.0f, 0.0f, 0.0f, 0.0f),
	  LineMode(TETRAHEDRON)
{
}

LineGroupClass::~LineGroupClass(void)
{
	REF_PTR_RELEASE(StartLineLoc);
	REF_PTR_RELEASE(EndLineLoc);
	REF_PTR_RELEASE(LineDiffuse);
	REF_PTR_RELEASE(TailDiffuse);
	REF_PTR_RELEASE(ALT);
	REF_PTR_RELEASE(LineSize);
	REF_PTR_RELEASE(LineUCoord);
	REF_PTR_RELEASE(Texture);
}

// -------------------------------------------------------------------------
// Set_Arrays -- store shared buffer pointers with proper ref-counting.
// active_line_count overrides the array length when >= 0.
// -------------------------------------------------------------------------
void LineGroupClass::Set_Arrays(
	ShareBufferClass<Vector3>      *startlocs,
	ShareBufferClass<Vector3>      *endlocs,
	ShareBufferClass<Vector4>      *diffuse,
	ShareBufferClass<Vector4>      *taildiffuse,
	ShareBufferClass<unsigned int> *alt,
	ShareBufferClass<float>        *sizes,
	ShareBufferClass<float>        *ucoords,
	int                             active_line_count)
{
	// Location arrays are mandatory
	WWASSERT(startlocs);
	WWASSERT(endlocs);

	// All optional arrays must match the location array length
	WWASSERT(startlocs->Get_Count() == endlocs->Get_Count());
	WWASSERT(!diffuse     || startlocs->Get_Count() == diffuse->Get_Count());
	WWASSERT(!taildiffuse || startlocs->Get_Count() == taildiffuse->Get_Count());
	WWASSERT(!alt         || startlocs->Get_Count() == alt->Get_Count());
	WWASSERT(!sizes       || startlocs->Get_Count() == sizes->Get_Count());
	WWASSERT(!ucoords     || startlocs->Get_Count() == ucoords->Get_Count());

	REF_PTR_SET(StartLineLoc, startlocs);
	REF_PTR_SET(EndLineLoc,   endlocs);
	REF_PTR_SET(LineDiffuse,  diffuse);
	REF_PTR_SET(TailDiffuse,  taildiffuse);
	REF_PTR_SET(ALT,          alt);
	REF_PTR_SET(LineSize,     sizes);
	REF_PTR_SET(LineUCoord,   ucoords);

	if (ALT) {
		LineCount = active_line_count;
	} else {
		LineCount = (active_line_count >= 0) ? active_line_count : StartLineLoc->Get_Count();
	}
}

// -------------------------------------------------------------------------
// Scalar defaults
// -------------------------------------------------------------------------
void LineGroupClass::Set_Line_Size(float size)
{
	DefaultLineSize = size;
}

float LineGroupClass::Get_Line_Size(void)
{
	return DefaultLineSize;
}

void LineGroupClass::Set_Line_Color(const Vector3 &color)
{
	DefaultLineColor = color;
}

Vector3 LineGroupClass::Get_Line_Color(void)
{
	return DefaultLineColor;
}

void LineGroupClass::Set_Tail_Diffuse(const Vector4 &tdiffuse)
{
	DefaultTailDiffuse = tdiffuse;
}

Vector4 LineGroupClass::Get_Tail_Diffuse(void)
{
	return DefaultTailDiffuse;
}

void LineGroupClass::Set_Line_Alpha(float alpha)
{
	DefaultLineAlpha = alpha;
}

float LineGroupClass::Get_Line_Alpha(void)
{
	return DefaultLineAlpha;
}

void LineGroupClass::Set_Line_UCoord(float ucoord)
{
	DefaultLineUCoord = ucoord;
}

float LineGroupClass::Get_Line_UCoord(void)
{
	return DefaultLineUCoord;
}

// -------------------------------------------------------------------------
// Flags
// -------------------------------------------------------------------------
void LineGroupClass::Set_Flag(FlagsType flag, bool on)
{
	if (on) {
		Flags |= (1u << flag);
	} else {
		Flags &= ~(1u << flag);
	}
}

int LineGroupClass::Get_Flag(FlagsType flag)
{
	return (Flags >> flag) & 0x1;
}

// -------------------------------------------------------------------------
// Texture -- standard ref-counted pattern
// -------------------------------------------------------------------------
void LineGroupClass::Set_Texture(TextureClass *texture)
{
	REF_PTR_SET(Texture, texture);
}

TextureClass *LineGroupClass::Get_Texture(void)
{
	if (Texture) {
		Texture->Add_Ref();
	}
	return Texture;
}

TextureClass *LineGroupClass::Peek_Texture(void)
{
	return Texture;
}

// -------------------------------------------------------------------------
// Shader
// -------------------------------------------------------------------------
void LineGroupClass::Set_Shader(const ShaderClass &shader)
{
	Shader = shader;
}

ShaderClass LineGroupClass::Get_Shader(void)
{
	return Shader;
}

// -------------------------------------------------------------------------
// Line mode
// -------------------------------------------------------------------------
void LineGroupClass::Set_Line_Mode(LineModeType linemode)
{
	LineMode = linemode;
}

LineGroupClass::LineModeType LineGroupClass::Get_Line_Mode(void)
{
	return LineMode;
}

// -------------------------------------------------------------------------
// Get_Polygon_Count -- reflect how many triangles the geometry would produce
// (used by higher-level LOD / statistics systems; no actual draw is needed)
// -------------------------------------------------------------------------
int LineGroupClass::Get_Polygon_Count(void)
{
	switch (LineMode) {
		case TETRAHEDRON:
			return LineCount * 4;
		case PRISM:
			return LineCount * 8;
	}
	WWASSERT(0);
	return 0;
}

// -------------------------------------------------------------------------
// Render -- intentional no-op for the D3D11 port.
//
// The original DX8 implementation built dynamic vertex/index buffers and
// submitted them via DX8Wrapper.  In the D3D11 port all of those types have
// been removed.  Line-mode particle systems are uncommon in Generals and the
// emitter data is still correctly loaded and managed; only the final GPU
// submission is skipped.  A future implementation could re-add GPU submission
// using the D3D11 rendering path when needed.
// -------------------------------------------------------------------------
void LineGroupClass::Render(RenderInfoClass & /*rinfo*/)
{
	// no-op: D3D11 rendering for line-mode particles not implemented
}
