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


// FILE: W3DListBox.cpp ///////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: W3DListBox.cpp
//
// Created:   Colin Day, June 2001
//
// Desc:      W3D implementation for the list box control
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include <stdlib.h>
#include <math.h>
#include <windows.h> // GetTickCount for shader animation clock

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "GameClient/GameWindowGlobal.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "W3DDevice/GameClient/W3DGadget.h"
#include "W3DDevice/GameClient/W3DDisplay.h"

// Shader ids on the wire (see Data/GeneralsRemastered.Data/ShaderEffects.cs
// and Launcher/.../ShaderEffectPreview.cs — kept in lockstep with both).
// 0 = stock (no animation). Non-zero means the chat row was sent by a
// launcher client who picked a cosmetic shader for their house color.
enum ChatShaderId
{
	CHAT_SHADER_STOCK       = 0,
	CHAT_SHADER_PULSE       = 1,
	CHAT_SHADER_RAINBOW     = 2,
	CHAT_SHADER_SHIMMER     = 3,
	CHAT_SHADER_CHROME      = 4,
	CHAT_SHADER_HOLOGRAPHIC = 5,
	CHAT_SHADER_HEXCAMO     = 6,
	CHAT_SHADER_FROST       = 7,
};

// Modulate a base ARGB color by a time-varying cosmetic shader effect.
// Called per-frame at list draw time so animated chat entries pulse /
// cycle hue / flicker without any gameplay-tick plumbing. Returns the
// input color unchanged for shaderId 0 so entries without a shader cost
// nothing beyond the branch.
//
// The specific curves are lightweight approximations of the HLSL
// ApplyShaderEffect in Shader3D.hlsl — not identical, but visually
// recognisable as the same effect so players can spot "oh that's
// Rainbow / Chrome / Frost" at a glance in chat.
static Color applyChatShaderColor(Color base, Int shaderId, UnsignedInt tMs)
{
	if (shaderId <= CHAT_SHADER_STOCK || shaderId > CHAT_SHADER_FROST)
		return base;

	// Unpack ARGB. The engine uses 0xAARRGGBB everywhere.
	UnsignedInt a = (base >> 24) & 0xFF;
	UnsignedInt r = (base >> 16) & 0xFF;
	UnsignedInt g = (base >>  8) & 0xFF;
	UnsignedInt b =  base        & 0xFF;

	// Period helpers. sinf is plenty — the animation is a visual
	// cue, not gameplay-critical, so precision beyond a byte of
	// color is wasted.
	const Real t = (Real)tMs * 0.001f;

	switch (shaderId)
	{
		case CHAT_SHADER_PULSE:
		{
			// Brightness wobbles between 78% and 100% (same envelope
			// as the swatch preview's scale pulse) — scaled text
			// would re-flow the list, so we ride the color instead.
			Real k = 0.89f + 0.11f * sinf(t * (2.0f * 3.14159f / 1.2f));
			r = (UnsignedInt)(r * k); if (r > 255) r = 255;
			g = (UnsignedInt)(g * k); if (g > 255) g = 255;
			b = (UnsignedInt)(b * k); if (b > 255) b = 255;
			break;
		}
		case CHAT_SHADER_RAINBOW:
		{
			// Full 3s hue cycle through the same 7 key colors the
			// swatch preview uses — looked up with a crude
			// piecewise-linear LUT.
			static const UnsignedInt rainbow[7][3] = {
				{0xFF,0x33,0x33}, {0xFF,0xCC,0x33}, {0x33,0xFF,0x33},
				{0x33,0xCC,0xFF}, {0x66,0x33,0xFF}, {0xFF,0x33,0xCC},
				{0xFF,0x33,0x33},
			};
			Real phase = t / 3.0f; phase -= floorf(phase);
			Real idxF = phase * 6.0f;
			Int  i0 = (Int)idxF; if (i0 > 5) i0 = 5;
			Int  i1 = i0 + 1;
			Real frac = idxF - (Real)i0;
			r = (UnsignedInt)((1.0f - frac) * rainbow[i0][0] + frac * rainbow[i1][0]);
			g = (UnsignedInt)((1.0f - frac) * rainbow[i0][1] + frac * rainbow[i1][1]);
			b = (UnsignedInt)((1.0f - frac) * rainbow[i0][2] + frac * rainbow[i1][2]);
			break;
		}
		case CHAT_SHADER_SHIMMER:
		{
			// Alpha flicker on the 0.9s envelope the swatch uses.
			// Clamped so text never goes fully transparent.
			Real phase = t / 0.9f; phase -= floorf(phase);
			Real mult;
			if      (phase < 0.30f) mult = 1.0f - 1.50f * phase;       // 1.00 → 0.55
			else if (phase < 0.45f) mult = 0.55f + 3.00f * (phase - 0.30f); // 0.55 → 1.00
			else if (phase < 0.75f) mult = 1.0f  - 1.00f * (phase - 0.45f); // 1.00 → 0.70
			else                    mult = 0.70f + 1.20f * (phase - 0.75f); // 0.70 → 1.00
			UnsignedInt am = (UnsignedInt)(a * mult);
			if (am > 255) am = 255;
			a = am;
			break;
		}
		case CHAT_SHADER_CHROME:
		{
			// Metallic look — desaturate toward a bright silver and
			// overdrive brightness. Ignores base color on purpose,
			// same as the swatch preview's white→silver gradient.
			Real phase = 0.5f + 0.5f * sinf(t * 2.0f);
			r = (UnsignedInt)(0x88 + (0xFF - 0x88) * phase);
			g = (UnsignedInt)(0x99 + (0xFF - 0x99) * phase);
			b = (UnsignedInt)(0xAA + (0xFF - 0xAA) * phase);
			break;
		}
		case CHAT_SHADER_HOLOGRAPHIC:
		{
			// Cycles magenta / cyan / yellow. Base color ignored on
			// purpose so the effect reads as "iridescent film".
			Real phase = t / 3.0f; phase -= floorf(phase);
			if (phase < 1.0f/3.0f)
			{
				Real k = phase * 3.0f;
				r = (UnsignedInt)((1.0f - k) * 0xFF + k * 0x33);
				g = (UnsignedInt)((1.0f - k) * 0x33 + k * 0xFF);
				b = (UnsignedInt)((1.0f - k) * 0xFF + k * 0xFF);
			}
			else if (phase < 2.0f/3.0f)
			{
				Real k = (phase - 1.0f/3.0f) * 3.0f;
				r = (UnsignedInt)((1.0f - k) * 0x33 + k * 0xFF);
				g = (UnsignedInt)((1.0f - k) * 0xFF + k * 0xFF);
				b = (UnsignedInt)((1.0f - k) * 0xFF + k * 0x33);
			}
			else
			{
				Real k = (phase - 2.0f/3.0f) * 3.0f;
				r = (UnsignedInt)((1.0f - k) * 0xFF + k * 0xFF);
				g = (UnsignedInt)((1.0f - k) * 0xFF + k * 0x33);
				b = (UnsignedInt)((1.0f - k) * 0x33 + k * 0xFF);
			}
			break;
		}
		case CHAT_SHADER_HEXCAMO:
		{
			// Darker base color, matches the swatch preview's 70%
			// darkened fill. Static — no time dependency.
			r = (UnsignedInt)(r * 7) / 10;
			g = (UnsignedInt)(g * 7) / 10;
			b = (UnsignedInt)(b * 7) / 10;
			break;
		}
		case CHAT_SHADER_FROST:
		{
			// Pale cyan with a subtle breathing shift toward white
			// — the swatch's cool glow isn't replicable in a single
			// text color so we hint at it via brightness breathing.
			Real k = 0.9f + 0.1f * sinf(t * 1.6f);
			r = (UnsignedInt)(0xCC * k); if (r > 255) r = 255;
			g = (UnsignedInt)(0xEE * k); if (g > 255) g = 255;
			b = (UnsignedInt)(0xFF * k); if (b > 255) b = 255;
			break;
		}
	}

	return (Color)((a << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF));
}

// DEFINES ////////////////////////////////////////////////////////////////////

// PRIVATE TYPES //////////////////////////////////////////////////////////////

// PRIVATE DATA ///////////////////////////////////////////////////////////////

// PUBLIC DATA ////////////////////////////////////////////////////////////////

// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// drawHiliteBar ==============================================================
/** Draw image for the hilite bar */
//=============================================================================
static void drawHiliteBar( const Image *left, const Image *right,
													 const Image *center, const Image *smallCenter,
													 Int startX, Int startY,
													 Int endX, Int endY )
{
	ICoord2D barWindowSize;  // end point of bar from window origin
	Int xOffset = 0, yOffset = 0;  // incase we want this functionality later
	ICoord2D start, end;
	Int i;
	IRegion2D clipRegion;



	barWindowSize.x = endX - startX;
	barWindowSize.y = endY - startY;

	//
	// the bar window size will always be at least big enough to accommodate
	// the left and right ends
	//
	if( barWindowSize.x < left->getImageWidth() + right->getImageWidth() )
		barWindowSize.x = left->getImageWidth() + right->getImageWidth();

	// get image sizes for the ends
	ICoord2D leftSize, rightSize;
	leftSize.x = left->getImageWidth();
	leftSize.y = left->getImageHeight();
	rightSize.x = right->getImageWidth();
	rightSize.y = right->getImageHeight();

	// get two key points used in the end drawing
	ICoord2D leftEnd, rightStart;
	leftEnd.x = startX + leftSize.x + xOffset;
	leftEnd.y = startY + barWindowSize.y + yOffset;
	rightStart.x = startX + barWindowSize.x - rightSize.x + xOffset;
	rightStart.y = startY + yOffset;

	// draw the center repeating bar
	Int centerWidth, pieces;

	// get width we have to draw our repeating center in
	centerWidth = rightStart.x - leftEnd.x;

	// how many whole repeating pieces will fit in that width
	pieces = centerWidth / center->getImageWidth();



	// draw the pieces
	start.x = leftEnd.x;
	start.y = startY + yOffset;
	end.y = start.y + barWindowSize.y;
	for( i = 0; i < pieces; i++ )
	{

		end.x = start.x + center->getImageWidth();
		TheWindowManager->winDrawImage( center,
																		start.x, start.y,
																		end.x, end.y );
		start.x += center->getImageWidth();

	}

	//
	// how many small repeating pieces will fit in the gap from where the
	// center repeating bar stopped and the right image, draw them
	// and overlapping underneath where the right end will go
	//
		// set the text clip region to the outline of the listbox
	clipRegion.lo.x = leftEnd.x;
	clipRegion.lo.y = startY + yOffset;
	clipRegion.hi.x = leftEnd.x + centerWidth;
	clipRegion.hi.y = start.y + barWindowSize.y;
	TheDisplay->setClipRegion(&clipRegion);
	centerWidth = rightStart.x - start.x;
	if( centerWidth )
	{

		pieces = centerWidth / smallCenter->getImageWidth() + 1;
		end.y = start.y + barWindowSize.y;
		for( i = 0; i < pieces; i++ )
		{

			end.x = start.x + smallCenter->getImageWidth();
			TheWindowManager->winDrawImage( smallCenter,
																			start.x, start.y,
																			end.x, end.y );
			start.x += smallCenter->getImageWidth();

		}

	}
	TheDisplay->enableClipping(FALSE);
	// draw left end
	start.x = startX + xOffset;
	start.y = startY + yOffset;
	end = leftEnd;
	TheWindowManager->winDrawImage(left, start.x, start.y, end.x, end.y);

	// draw right end
	start = rightStart;
	end.x = start.x + rightSize.x;
	end.y = start.y + barWindowSize.y;
	TheWindowManager->winDrawImage(right, start.x, start.y, end.x, end.y);

}

// drawListBoxText ============================================================
/** Draw the text for a listbox */
//=============================================================================
static void drawListBoxText( GameWindow *window, WinInstanceData *instData,
														 Int x, Int y, Int width, Int height,
														 Bool useImages )
{
	Int drawY;
	ListboxData *list = (ListboxData *)window->winGetUserData();
	Int i;
	Bool selected;
	Int listLineHeight;
	Color textColor;
//	W3DGameWindow *w3dWindow = static_cast<W3DGameWindow *>(window);
	IRegion2D clipRegion;
	ICoord2D start, end;

	//
	// save the clipping information region cause we're going to use it here
	// in drawing the text
	//
//	TheWindowManager->winGetClipRegion( &clipRegion );

	// set clip region to inside the outline box.
//	TheWindowManager->winSetClipRegion( x, y, width, height );

	// set the text clip region to the outline of the listbox
	clipRegion.lo.x = x + 1;
	clipRegion.lo.y = y -3;
	clipRegion.hi.x = x + width - 1;
	clipRegion.hi.y = y + height - 1;

	drawY = y - list->displayPos;

	for( i = 0; ; i++ )
	{

		if( i > 0 )
			if( list->listData[(i - 1)].listHeight >
					(list->displayPos + list->displayHeight) )
				break;

		if( i == list->endPos )
			break;

		if( list->listData[i].listHeight < list->displayPos )
		{
			drawY += (list->listData[i].height + 1);
			continue;
		}

		listLineHeight = list->listData[i].height + 1;
		//textColor =  list->listData[i].textColor;
		selected = FALSE;

		if( list->multiSelect )
		{
			Int j = 0;

			while( list->selections[j] >= 0 )
			{
				if( i == list->selections[j] )
				{
					selected = TRUE;
					break;
				}

				j++;
			}
		}
		else
		{
			if( i == list->selectPos )
				selected = TRUE;
		}

		// this item is selected, draw the selection color or image
		if( selected )
		{

			if( useImages )
			{
				const Image *left, *right, *center, *smallCenter;

				if( BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
				{

					left				= GadgetListBoxGetDisabledSelectedItemImageLeft( window );
					right				= GadgetListBoxGetDisabledSelectedItemImageRight( window );
					center			= GadgetListBoxGetDisabledSelectedItemImageCenter( window );
					smallCenter = GadgetListBoxGetDisabledSelectedItemImageSmallCenter( window );

				}
				else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
				{

					left				= GadgetListBoxGetHiliteSelectedItemImageLeft( window );
					right				= GadgetListBoxGetHiliteSelectedItemImageRight( window );
					center			= GadgetListBoxGetHiliteSelectedItemImageCenter( window );
					smallCenter = GadgetListBoxGetHiliteSelectedItemImageSmallCenter( window );

				}
				else
				{

					left				= GadgetListBoxGetEnabledSelectedItemImageLeft( window );
					right				= GadgetListBoxGetEnabledSelectedItemImageRight( window );
					center			= GadgetListBoxGetEnabledSelectedItemImageCenter( window );
					smallCenter = GadgetListBoxGetEnabledSelectedItemImageSmallCenter( window );

				}

				// draw select image across area

				//
				// where are we going to draw ... taking into account the clipping
				// region of the edge of the listbox
				//
				start.x = x;
				start.y = drawY;
				end.x = start.x + width;
				end.y = start.y + listLineHeight;

				if( end.y > clipRegion.hi.y )
					end.y = clipRegion.hi.y;
				if( start.y < clipRegion.lo.y )
					start.y = clipRegion.lo.y;

				if( left && right && center && smallCenter )
					drawHiliteBar( left, right, center, smallCenter, start.x + 1, start.y, end.x , end.y );

			}
			else
			{
				Color selectColor = WIN_COLOR_UNDEFINED,
							selectBorder = WIN_COLOR_UNDEFINED;

				if( BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
				{
					selectColor  = GadgetListBoxGetDisabledSelectedItemColor( window );
					selectBorder = GadgetListBoxGetDisabledSelectedItemBorderColor( window );
				}
				else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
				{
					selectColor  = GadgetListBoxGetHiliteSelectedItemColor( window );
					selectBorder = GadgetListBoxGetHiliteSelectedItemBorderColor( window );
				}
				else
				{
					selectColor  = GadgetListBoxGetEnabledSelectedItemColor( window );
					selectBorder = GadgetListBoxGetEnabledSelectedItemBorderColor( window );
				}

				// draw border

				//
				// where are we going to draw ... taking into account the clipping
				// region of the edge of the listbox
				//
				start.x = x;
				start.y = drawY;
				end.x = start.x + width;
				end.y = start.y + listLineHeight;

				if( end.y > clipRegion.hi.y )
					end.y = clipRegion.hi.y;
				if( start.y < clipRegion.lo.y )
					start.y = clipRegion.lo.y;

				if( selectBorder != WIN_COLOR_UNDEFINED )
					TheWindowManager->winOpenRect( selectBorder,
																				 WIN_DRAW_LINE_WIDTH,
																				 start.x, start.y,
																				 end.x, end.y );

				// draw filled inner rect

				//
				// where are we going to draw ... taking into account the clipping
				// region of the edge of the listbox
				//
				start.x = x + 1;
				start.y = drawY + 1;
				end.x = start.x + width - 2;
				end.y = start.y + listLineHeight - 2;

				if( end.y > clipRegion.hi.y )
					end.y = clipRegion.hi.y;
				if( start.y < clipRegion.lo.y )
					start.y = clipRegion.lo.y;

				if( selectColor != WIN_COLOR_UNDEFINED )
					TheWindowManager->winFillRect( selectColor,
																				 WIN_DRAW_LINE_WIDTH,
																				 start.x, start.y,
																				 end.x, end.y );

			}

		}




		Color dropColor = TheWindowManager->winMakeColor( 0, 0, 0, 255 );
		DisplayString *string;

		ListEntryCell *cells = list->listData[i].cell;
		Int columnX = x;
		IRegion2D columnRegion;
		if( cells )
		{
			// loop through all the cells
			for( Int j = 0; j < list->columns; j++ )
			{
				// setup the Clip Region size

				columnRegion.lo.x = columnX;
				columnRegion.lo.y = drawY;
				if(list->columns == 1 && list->slider && list->slider->winIsHidden())
					columnRegion.hi.x = columnX + width-3;
				else
					columnRegion.hi.x = columnX + list->columnWidth[j];
				columnRegion.hi.y = drawY + list->listData[i].height;
				if(columnRegion.lo.y < clipRegion.lo.y )
					columnRegion.lo.y = clipRegion.lo.y;
				if( columnRegion.hi.y > clipRegion.hi.y )
					columnRegion.hi.y = clipRegion.hi.y;

				// Display the Text Case;
				if(cells[j].cellType == LISTBOX_TEXT)
				{
					textColor = cells[j].color;
					// Cosmetic chat shader — modulates the entry's
					// base color in place of solid draw so a chat row
					// from a launcher client with e.g. Rainbow shows
					// the cycling hues in-game too. Cost is one branch
					// per cell when no shader is set (the common case).
					if (cells[j].shaderId != 0)
						textColor = applyChatShaderColor(textColor, cells[j].shaderId, GetTickCount());
					string = (DisplayString *)cells[j].data;
					if( BitIsSet( window->winGetStatus(), WIN_STATUS_ONE_LINE ) == TRUE )
					{
						string->setWordWrap(0);
						// make sure the font of the text is the same as the windows
						if( string->getFont() != window->winGetFont() )
							string->setFont( window->winGetFont() );

						// draw this text after setting the clip region for it
						string->setClipRegion( &columnRegion );
						string->draw( columnX + TEXT_X_OFFSET,
													drawY,
													textColor,
													dropColor );

					}
					else
					{

						// make sure the font of the text is the same as the windows
						if( string->getFont() != window->winGetFont() )
							string->setFont( window->winGetFont() );

						// set clip region and draw
						string->setClipRegion( &columnRegion );
						string->draw( columnX + TEXT_X_OFFSET,
													drawY,
													textColor,
													dropColor );
					}
				}
				else if(cells[j].cellType == LISTBOX_IMAGE && cells[j].data)
				{
					Int width, height;
					if (cells[j].width > 0)
						width = cells[j].width;
					else
						width = list->columnWidth[j];
					if(cells[j].height > 0)
						height = cells[j].height;
					else
						height = list->listData[i].height;
					if(j == 0)
						width--;
					Int offsetX,offsetY;
					if(width < list->columnWidth[j])
						offsetX = columnX + ((list->columnWidth[j] - width) / 2);
					else
						offsetX = columnX;
					if(height < list->listData[i].height)
						offsetY = drawY + ((list->listData[i].height - height) / 2);
					else
						offsetY = drawY;

					offsetY++;
					if(offsetX <x+1)
						offsetX = x+1;
					TheDisplay->setClipRegion( &columnRegion );
					TheWindowManager->winDrawImage( (const Image *)cells[j].data,
																offsetX, offsetY,
																offsetX + width, offsetY + height,cells[j].color );

				}
				columnX = columnX + list->columnWidth[j];
			}
		}


		drawY += listLineHeight;
		TheDisplay->enableClipping(FALSE);
	}

//	TheWindowManager->winSetClipRegion( clipRegion.lo.x, clipRegion.lo.y,
//																			clipRegion.hi.x, clipRegion.hi.y );

}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// W3DGadgetListBoxDraw =======================================================
/** Draw colored list box using standard graphics */
//=============================================================================
void W3DGadgetListBoxDraw( GameWindow *window, WinInstanceData *instData )
{
	Int width, height, fontHeight, x, y;
	Color background, border, titleColor, titleBorder;
	ListboxData *list = (ListboxData *)window->winGetUserData();
	ICoord2D size;
	DisplayString *title = instData->getTextDisplayString();

	// get window position and size
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &size.x, &size.y );

	// get font height
	fontHeight = TheWindowManager->winFontHeight( instData->getFont() );

	// alias width and height from size
	width = size.x;
	height = size.y;

	// get the right colors
	if( BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
	{
		background		= GadgetListBoxGetDisabledColor( window );
		border				= GadgetListBoxGetDisabledBorderColor( window );
		titleColor		= window->winGetDisabledTextColor();
		titleBorder		= window->winGetDisabledTextBorderColor();
	}
	else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
	{
		background		= GadgetListBoxGetHiliteColor( window );
		border				= GadgetListBoxGetHiliteBorderColor( window );
		titleColor		= window->winGetHiliteTextColor();
		titleBorder		= window->winGetHiliteTextBorderColor();
	}
	else
	{
		background		= GadgetListBoxGetEnabledColor( window );
		border				= GadgetListBoxGetEnabledBorderColor( window );
		titleColor		= window->winGetEnabledTextColor();
		titleBorder		= window->winGetEnabledTextBorderColor();
	}

	// Draw the title
	if( title && title->getTextLength() )
	{

		// set the font of this text to that of the window if not already
		if( title->getFont() != window->winGetFont() )
			title->setFont( window->winGetFont() );

		// draw the text
		title->draw( x + 1, y, titleColor, titleBorder );

		y += fontHeight + 1;
		height -= fontHeight + 1;

	}

	// draw the back border
	if( border != WIN_COLOR_UNDEFINED )
		TheWindowManager->winOpenRect( border, WIN_DRAW_LINE_WIDTH,
																	 x, y, x + width, y + height );

	// draw background
	if( background != WIN_COLOR_UNDEFINED )
		TheWindowManager->winFillRect( background, WIN_DRAW_LINE_WIDTH,
																	 x + 1, y + 1,
																	 x + width - 1, y + height - 1 );

	// If ScrollBar was requested ... adjust width.
	if( list->slider  && !list->slider->winIsHidden())
	{
		ICoord2D sliderSize;

		list->slider->winGetSize( &sliderSize.x, &sliderSize.y );
		width -= (sliderSize.x +3);

	}

	// draw the text
	drawListBoxText( window, instData, x, y + 4 , width, height-4, TRUE );



}

// W3DGadgetListBoxImageDraw ==================================================
/** Draw list box with user supplied images */
//=============================================================================
void W3DGadgetListBoxImageDraw( GameWindow *window, WinInstanceData *instData )
{
	Int width, height, x, y;
	const Image *image;
	ListboxData *list = (ListboxData *)window->winGetUserData();
	ICoord2D size;
	Color titleColor, titleBorder;
	DisplayString *title = instData->getTextDisplayString();

	// get window position and size
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &size.x, &size.y );

	// save off width and height so we can change them
	width = size.x;
	height = size.y;

	// If ScrollBar was requested ... adjust width.
	if( list->slider )
	{
		ICoord2D sliderSize;

		list->slider->winGetSize( &sliderSize.x, &sliderSize.y );
		width -= sliderSize.x;

	}

	// get the image
	if( BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
	{
		image				= GadgetListBoxGetDisabledImage( window );
		titleColor	= window->winGetDisabledTextColor();
		titleBorder = window->winGetDisabledTextBorderColor();
	}
	else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
	{
		image				= GadgetListBoxGetHiliteImage( window );
		titleColor	= window->winGetHiliteTextColor();
		titleBorder = window->winGetHiliteTextBorderColor();
	}
	else
	{
		image				= GadgetListBoxGetEnabledImage( window );
		titleColor	= window->winGetEnabledTextColor();
		titleBorder = window->winGetEnabledTextBorderColor();
	}

	// draw the back image
	if( image )
	{
		ICoord2D start, end;

		start.x = x + instData->m_imageOffset.x;
		start.y = y + instData->m_imageOffset.y;
		end.x = start.x + width;
		end.y = start.y + height;
		TheWindowManager->winDrawImage( image,
																		start.x, start.y,
																		end.x, end.y );

	}

	// Draw the title
	if( title && title->getTextLength() )
	{

		// set font to font of the window if not already
		if( title->getFont() != window->winGetFont() )
			title->setFont( window->winGetFont() );

		// draw the text
		title->draw( x + 1, y, titleColor, titleBorder );

		y += TheWindowManager->winFontHeight( instData->getFont() );
		height -= TheWindowManager->winFontHeight( instData->getFont() ) + 1;

	}

	// draw the listbox text
	drawListBoxText( window, instData, x, y+4, width, height-4, TRUE );



}

