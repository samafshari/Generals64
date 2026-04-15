/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// FILE: W3DGameClient.h ///////////////////////////////////////////////////
// W3D implementation of the game interface - rewritten for D3D11

#pragma once

#include "GameClient/GameClient.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DInGameUI.h"
#include "W3DDevice/GameClient/W3DGameWindowManager.h"
#include "W3DDevice/GameClient/W3DGameFont.h"
#include "W3DDevice/GameClient/W3DDisplayStringManager.h"
#include "VideoDevice/Bink/BinkVideoPlayer.h"
#ifdef RTS_HAS_FFMPEG
#include "VideoDevice/FFmpeg/FFmpegVideoPlayer.h"
#endif

class ThingTemplate;
class Win32Mouse;

extern Win32Mouse *TheWin32Mouse;

class W3DGameClient : public GameClient
{
public:

	W3DGameClient();
	virtual ~W3DGameClient();

	virtual Drawable *friend_createDrawable( const ThingTemplate *thing, DrawableStatusBits statusBits = DRAWABLE_STATUS_DEFAULT );

	virtual void init();
	virtual void update();
	virtual void reset();

	virtual void addScorch(const Coord3D *pos, Real radius, Scorches type);
	virtual void createRayEffectByTemplate( const Coord3D *start, const Coord3D *end, const ThingTemplate* tmpl );

	virtual void setTimeOfDay( TimeOfDay tod );

	virtual void setTeamColor( Int red, Int green, Int blue );
	virtual void setTextureLOD( Int level );
	virtual void notifyTerrainObjectMoved(Object *obj);

protected:

	virtual Keyboard *createKeyboard();
	virtual Mouse *createMouse();

	virtual Display *createGameDisplay() { return NEW W3DDisplay; }
	virtual InGameUI *createInGameUI() { return NEW W3DInGameUI; }
	virtual GameWindowManager *createWindowManager() { return NEW W3DGameWindowManager; }
	virtual FontLibrary *createFontLibrary() { return NEW W3DFontLibrary; }
	virtual DisplayStringManager *createDisplayStringManager() { return NEW W3DDisplayStringManager; }
#ifdef RTS_HAS_FFMPEG
	virtual VideoPlayerInterface *createVideoPlayer() { return NEW FFmpegVideoPlayer; }
#else
	virtual VideoPlayerInterface *createVideoPlayer() { return NEW BinkVideoPlayer; }
#endif
	virtual TerrainVisual *createTerrainVisual();
	virtual SnowManager *createSnowManager();
	virtual void setFrameRate(Real msecsPerFrame);
};
