/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// FILE: Win32GameEngine.h - rewritten for D3D11

#pragma once

#include "Common/GameEngine.h"
#include "Common/GlobalData.h"   // TheGlobalData for headless check in createRadar
#include "GameLogic/GameLogic.h"
#include "GameNetwork/NetworkInterface.h"
#include "MilesAudioDevice/MilesAudioManager.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"
#include "Win32Device/Common/Win32LocalFileSystem.h"
#include "W3DDevice/Common/W3DModuleFactory.h"
#include "W3DDevice/GameLogic/W3DGameLogic.h"
#include "W3DDevice/GameClient/W3DGameClient.h"
#include "W3DDevice/Common/W3DFunctionLexicon.h"
#include "W3DDevice/Common/W3DThingFactory.h"
#include "GameClient/ParticleSys.h"
#include "Common/Radar.h"

// Factory function for D3D11 radar (defined in D3D11Shims.cpp)
extern Radar* CreateD3D11Radar();

// D3D11 particle system manager - renders particles as camera-facing billboard quads
class D3D11ParticleSystemManager : public ParticleSystemManager
{
public:
	Int getOnScreenParticleCount() override { return m_onScreenParticleCount; }
	void doParticles(RenderInfoClass &rinfo) override;
	void queueParticleRender() override {}
};

class Win32GameEngine : public GameEngine
{
public:
	Win32GameEngine();
	virtual ~Win32GameEngine();

	virtual void init();
	virtual void reset();
	virtual void update();
	virtual void serviceWindowsOS();

protected:
	virtual GameLogic *createGameLogic();
	virtual GameClient *createGameClient();
	virtual ModuleFactory *createModuleFactory();
	virtual ThingFactory *createThingFactory();
	virtual FunctionLexicon *createFunctionLexicon();
	virtual LocalFileSystem *createLocalFileSystem();
	virtual ArchiveFileSystem *createArchiveFileSystem();
	virtual NetworkInterface *createNetwork();
	virtual Radar *createRadar();
	virtual WebBrowser *createWebBrowser();
	virtual AudioManager *createAudioManager();
	virtual ParticleSystemManager* createParticleSystemManager();

protected:
	UINT m_previousErrorMode;
};

inline GameLogic *Win32GameEngine::createGameLogic() { return NEW W3DGameLogic; }
inline GameClient *Win32GameEngine::createGameClient() { return NEW W3DGameClient; }
inline ModuleFactory *Win32GameEngine::createModuleFactory() { return NEW W3DModuleFactory; }
inline ThingFactory *Win32GameEngine::createThingFactory() { return NEW W3DThingFactory; }
inline FunctionLexicon *Win32GameEngine::createFunctionLexicon() { return NEW W3DFunctionLexicon; }
inline LocalFileSystem *Win32GameEngine::createLocalFileSystem() { return NEW Win32LocalFileSystem; }
inline ArchiveFileSystem *Win32GameEngine::createArchiveFileSystem() { return NEW Win32BIGFileSystem; }
inline NetworkInterface *Win32GameEngine::createNetwork() { return NetworkInterface::createNetwork(); }
#ifdef USE_SDL
#include "SDLAudioManager.h"
inline AudioManager *Win32GameEngine::createAudioManager() { return NEW SDLAudioManager; }
#else
inline AudioManager *Win32GameEngine::createAudioManager() { return NEW MilesAudioManager; }
#endif

// Headless builds skip W3DDisplay's D3D11 renderer init (see W3DDisplay::init
// — early returns when m_headless). Returning a D3D11Radar here would try to
// GetDevice() on an uninitialised renderer and NULL-deref in the radar's
// CreateDynamic texture call. Return nullptr so the GameEngine::init fallback
// (`if (!radar) radar = NEW RadarDummy`) kicks in — the dummy is a no-op
// implementation that's safe for headless replay verification.
inline Radar *Win32GameEngine::createRadar()
{
	if (TheGlobalData && TheGlobalData->m_headless)
		return nullptr;
	return CreateD3D11Radar();
}
inline WebBrowser *Win32GameEngine::createWebBrowser() { return nullptr; }
inline ParticleSystemManager* Win32GameEngine::createParticleSystemManager() { return NEW D3D11ParticleSystemManager; }
