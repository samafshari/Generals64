/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// FILE: W3DDisplay.h /////////////////////////////////////////////////////////
// W3D Implementation for the Display - rewritten for D3D11

#pragma once

#include "GameClient/Display.h"
#include <vector>

class VideoBuffer;
class DisplayString;

// A dynamic point light that brightens then fades (used for explosions)
struct LightPulse
{
	float posX, posY, posZ;      // World position
	float colorR, colorG, colorB; // Light color (0-1 range)
	float innerRadius;
	float outerRadius;
	float intensity;              // Current brightness multiplier (0 to 1)
	float increaseRate;           // Intensity increase per millisecond
	float decayRate;              // Intensity decrease per millisecond
	bool increasing;              // true = still brightening, false = decaying
};

class W3DDisplay : public Display
{
public:
	W3DDisplay();
	~W3DDisplay();

	virtual void init();
	virtual void reset();

	virtual void setWidth( UnsignedInt width );
	virtual void setHeight( UnsignedInt height );
	virtual Bool setDisplayMode( UnsignedInt xres, UnsignedInt yres, UnsignedInt bitdepth, Bool windowed );
	virtual Int getDisplayModeCount();
	virtual void getDisplayModeDescription(Int modeIndex, Int *xres, Int *yres, Int *bitDepth);
	virtual void setGamma(Real gamma, Real bright, Real contrast, Bool calibrate);
	virtual void doSmartAssetPurgeAndPreload(const char* usageFileName);
#if defined(RTS_DEBUG)
	virtual void dumpAssetUsage(const char* mapname);
#endif

	virtual void setClipRegion( IRegion2D *region );
	virtual Bool isClippingEnabled() { return m_isClippedEnabled; }
	virtual void enableClipping( Bool onoff ) { m_isClippedEnabled = onoff; }

	virtual void step();
	virtual void draw();

	virtual void createLightPulse( const Coord3D *pos, const RGBColor *color, Real innerRadius, Real outerRadius,
								   UnsignedInt increaseFrameTime, UnsignedInt decayFrameTime );
	virtual void setTimeOfDay( TimeOfDay tod );

	virtual void drawLine( Int startX, Int startY, Int endX, Int endY, Real lineWidth, UnsignedInt lineColor );
	virtual void drawLine( Int startX, Int startY, Int endX, Int endY, Real lineWidth, UnsignedInt lineColor1, UnsignedInt lineColor2 );
	virtual void drawOpenRect( Int startX, Int startY, Int width, Int height, Real lineWidth, UnsignedInt lineColor );
	virtual void drawFillRect( Int startX, Int startY, Int width, Int height, UnsignedInt color );
	virtual void drawRectClock(Int startX, Int startY, Int width, Int height, Int percent, UnsignedInt color);
	virtual void drawRemainingRectClock(Int startX, Int startY, Int width, Int height, Int percent, UnsignedInt color);
	virtual void drawImage( const Image *image, Int startX, Int startY, Int endX, Int endY, Color color = 0xFFFFFFFF, DrawImageMode mode = DRAW_IMAGE_ALPHA);
	virtual void drawImageRotatedCCW90( const Image *image, Int startX, Int startY, Int endX, Int endY, Color color = 0xFFFFFFFF, DrawImageMode mode = DRAW_IMAGE_ALPHA) override;
	virtual void drawScaledVideoBuffer( VideoBuffer *buffer, VideoStreamInterface *stream );
	virtual void drawVideoBuffer( VideoBuffer *buffer, Int startX, Int startY, Int endX, Int endY );
	virtual VideoBuffer* createVideoBuffer();

	virtual void takeScreenShot();
	virtual void toggleMovieCapture();
	virtual void toggleLetterBox();
	virtual void enableLetterBox(Bool enable);
	virtual Bool isLetterBoxFading();
	virtual Bool isLetterBoxed();

	virtual void clearShroud();
	virtual void setShroudLevel(Int x, Int y, CellShroudStatus setting);
	virtual void setBorderShroudLevel(UnsignedByte level);
#if defined(RTS_DEBUG)
	virtual void dumpModelAssets(const char *path);
#endif
	virtual void preloadModelAssets( AsciiString model );
	virtual void preloadTextureAssets( AsciiString texture );

	virtual Real getAverageFPS();
	virtual Real getCurrentFPS();
	virtual Int getLastFrameDrawCalls();

	// Static members accessed by draw modules for scene/asset management
	static class RTS3DScene *m_3DScene;
	static class W3DAssetManager *m_assetManager;

protected:

	void updateAverageFPS();
	void updateLightPulses(float deltaMs);
	void applyLightPulsesToRenderer();

	Byte m_initialized;
	IRegion2D m_clipRegion;
	Bool m_isClippedEnabled;
	Real m_averageFPS;
	Real m_currentFPS;

	DisplayString *m_benchmarkDisplayString;

	// Dynamic point lights from explosions
	std::vector<LightPulse> m_lightPulses;
	uint32_t m_lastLightUpdateTime;

	// Movie capture (frame-by-frame screenshot recording)
	Bool m_movieCaptureEnabled = FALSE;
	Int m_movieCaptureFrame = 0;
};
