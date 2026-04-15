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

// FILE: W3DGameFont.cpp //////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:    RTS3
//
// File name:  W3DGameFont.cpp
//
// Created:    Colin Day, June 2001
//
// Desc:       W3D implementation for managing font definitions
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include <stdlib.h>
#include <cstdarg>
#include <cstring>

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "Common/Debug.h"
#include "Common/GlobalData.h"
#include "W3DDevice/GameClient/W3DGameFont.h"
#include "WW3D2/ww3d.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/render2dsentence.h"
#include "GameClient/GlobalLanguage.h"

// DEFINES ////////////////////////////////////////////////////////////////////

// PRIVATE TYPES //////////////////////////////////////////////////////////////

// PRIVATE DATA ///////////////////////////////////////////////////////////////

// PUBLIC DATA ////////////////////////////////////////////////////////////////

// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static Bool isShellMapFontTraceEnabled()
{
	return FALSE;
}

static void appendShellMapFontTrace(const char *format, ...)
{
	return; // Debug logging removed
}

// W3DFontLibrary::loadFontData ===============================================
/** Load a font */
//=============================================================================
Bool W3DFontLibrary::loadFontData( GameFont *font )
{
	// sanity
	if( font == nullptr )
		return FALSE;

	const char* name = font->nameString.str();
	const Int size = font->pointSize;
	const Bool bold = font->bold;
	appendShellMapFontTrace("W3DFontLibrary: loadFontData begin name=%s size=%d bold=%d", name, size, bold);

	// get the font data from the asset manager
	appendShellMapFontTrace("W3DFontLibrary: Get_FontChars begin name=%s size=%d bold=%d", name, size, bold);
	FontCharsClass *fontChar = WW3DAssetManager::Get_Instance()->Get_FontChars( name, size, bold );
	appendShellMapFontTrace("W3DFontLibrary: Get_FontChars end name=%s size=%d bold=%d fontChar=%p", name, size, bold, fontChar);

	if( fontChar == nullptr )
	{
		DEBUG_CRASH(( "Unable to find font '%s' in Asset Manager", name ));
		return FALSE;
	}

	// assign font data
	font->fontData = fontChar;
	font->height = fontChar->Get_Char_Height();
	appendShellMapFontTrace("W3DFontLibrary: font metrics end name=%s height=%d", font->nameString.str(), font->height);

	// load Unicode of same point size
	name = TheGlobalLanguageData ? TheGlobalLanguageData->m_unicodeFontName.str() : "Arial Unicode MS";
	appendShellMapFontTrace("W3DFontLibrary: unicode Get_FontChars begin name=%s size=%d bold=%d", name, size, bold);
	fontChar->AlternateUnicodeFont = WW3DAssetManager::Get_Instance()->Get_FontChars( name, size, bold );
	appendShellMapFontTrace("W3DFontLibrary: unicode Get_FontChars end name=%s size=%d bold=%d fontChar=%p", name, size, bold, fontChar->AlternateUnicodeFont);

	appendShellMapFontTrace("W3DFontLibrary: loadFontData end name=%s size=%d bold=%d", font->nameString.str(), size, bold);
	return TRUE;
}

// W3DFontLibrary::releaseFontData ============================================
/** Release font data */
//=============================================================================
void W3DFontLibrary::releaseFontData( GameFont *font )
{

	// presently we don't need to do anything because fonts are handled in
	// the W3D asset manager which is all taken for of us
	if (font && font->fontData)
	{
		if(((FontCharsClass *)(font->fontData))->AlternateUnicodeFont)
			((FontCharsClass *)(font->fontData))->AlternateUnicodeFont->Release_Ref();
		((FontCharsClass *)(font->fontData))->Release_Ref();

		font->fontData = nullptr;
	}

}

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////

