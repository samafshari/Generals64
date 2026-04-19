/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// FILE: W3DDisplay.cpp ///////////////////////////////////////////////////////
// Display implementation using D3D11 Renderer
///////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#include <DirectXMath.h>
#endif
#ifdef USE_SDL
#include <SDL3/SDL.h>
#endif
#include <cstdarg>
#include <cstdio>
#include <algorithm>

#include "Common/GlobalData.h"
#include "Common/GameLOD.h"
#include "Common/FramePacer.h"
#include "Common/LivePerf.h"
#include "GameClient/GameText.h"
#include "GameClient/Mouse.h"
#include "GameClient/InGameUI.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Color.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "Common/DrawModule.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "Lib/BaseType.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "WW3D2/camera.h"  // needed for CameraClass::Get_Position in the sun shadow pass
#include "Renderer.h"
#include "GPUParticles.h"
#include "Inspector/Inspector.h"
#ifdef USE_SDL
#include "Platform/SDLPlatform.h"
#endif
#include "W3DDevice/GameClient/ModelRenderer.h"
#include "W3DDevice/GameClient/TerrainRenderer.h"
#include "WW3D2/hlod.h"
#include "WW3D2/scene.h"
// Forward declare - can't include scene.h (chains to dx8wrapper.h)
class SimpleSceneClass;
#include "GameClient/GameWindowManager.h"
#include "GameClient/Shell.h"
#include "W3DDevice/GameClient/ImageCache.h"
#include "GameClient/Image.h"
#include "GameClient/ParticleSys.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/ww3d.h"
#include "GameClient/View.h"
#include "GameClient/VideoPlayer.h"
#include "W3DDevice/GameClient/W3DView.h"
#include "W3DDevice/GameClient/W3DInGameUI.h"
#include "W3DDevice/GameClient/W3DTerrainTracks.h"
// BaseHeightMap.h includes dx8wrapper.h - use helpers instead
class WorldHeightMap;
extern WorldHeightMap* GetTerrainHeightMap(); // defined in D3D11Shims.cpp
class CameraClass;
extern void RenderTerrainPropsDX11(CameraClass *camera); // defined in D3D11Shims.cpp
extern void RenderScorchMarksDX11(CameraClass *camera); // defined in D3D11Shims.cpp
extern void RenderTerrainTracksDX11(CameraClass *camera); // defined in D3D11Shims.cpp
extern void RenderBibsDX11(CameraClass *camera); // defined in D3D11Shims.cpp
extern void RenderStatusCircleDX11(); // defined in D3D11Shims.cpp
extern void RenderSnowDX11(); // defined in W3DGameClient.cpp
extern void RenderWaypointsDX11(CameraClass *camera); // defined in D3D11Shims.cpp
extern void RenderWaterTracksDX11(CameraClass *camera); // defined in D3D11Shims.cpp

#ifdef _WIN32
extern HWND ApplicationHWnd;
#endif

// Debug render toggles (F9 overlay)
int g_debugFrameDrawCalls = 0;  // reset each frame, incremented by Draw3D
bool g_debugDisableTerrain = false;
bool g_debugDisableWater = false;
bool g_debugDisableShroud = false;
bool g_debugDisableModels = false;
bool g_debugDisableLighting = false;
bool g_debugDisableSkyBox = false;
bool g_debugDisableRoads = false;
bool g_debugDisableBridges = false;
bool g_debugDisableProps = false;
bool g_debugDisableBibs = false;
bool g_debugDisableScorch = false;
bool g_debugDisableTracks = false;
bool g_debugDisableWaypoints = false;
bool g_debugDisableTranslucent = false;
bool g_debugDisableParticles = false;
// Shadow subsystem toggles. Both projected and volumetric paths enabled.
// Volumetric rendering prefers ZFail stencil updates to avoid
// camera-inside-volume pillar artifacts from airborne casters.
bool g_debugDisableProjectedShadows = false;
bool g_debugDisableSunShadowMap     = false; // Sun shadow map pass (replaces stencil volumes)
bool g_debugDisableSnow = false;
bool g_debugDisableUI = false;
bool g_debugDisableBegin2DEnd2D = false;  // skip the inner Begin2D/End2D in drawViews
bool g_debugDisableMultiDrawSkip = false;
bool g_debugDisableSyncWrite = false;    // Sync runs but doesn't write to s_snapshot
bool g_debugDisableFrustumCull = false;
bool g_debugDisableFlushConstants = false;
bool g_debugDisableReflection = false;
bool g_debugDisableDrawViews = false;
bool g_debugDisableUpdateViews = false;
bool g_debugDisableStatusCircle = false;
bool g_debugDisableLightPulse = false;
bool g_debugDisableMouse = false;
bool g_debugDisableWW3DSync = false;
bool g_debugDisableTrackUpdate = false;
bool g_debugDisableFSRVideo = false;     // FSR upscaling for video playback (ON by default)
bool g_debugDisableParticleGlow = true;       // Particle glow FX — OFF (causes water artifacts)
bool g_debugDisableHeatDistortion = false;    // Heat distortion FX — ON by default
// Shockwave distortion rings trigger on dynamic point lights above intensity
// threshold — original ZH had no such effect so this is a remaster
// enhancement. Default ON; flip to true from Inspector → Visual FX to
// restore the un-augmented look. Threshold still wants tuning: a single
// tank-shell impact buddy-light currently clears the bar.
bool g_debugDisableShockwave = false;         // Shockwave distortion rings — ON by default
bool g_debugDisableGodRays = false;           // Volumetric light shafts — ON by default
// "Cinematic" post-processing was previously ON by default and produced
// the classic teal-shadow / orange-highlight Hollywood blockbuster look.
// Generals 2003 had no such grading, and the warm highlight tint pushed
// sandy desert maps and infantry uniforms toward red. Default to OFF so
// the rendered scene matches the original game; the user can re-enable
// from the Inspector → Debug Draw menu if they want the cinematic look.
bool g_debugDisableChromaAberration = false;  // Chromatic aberration — ON by default (remaster look)
bool g_debugDisableColorGrade = false;        // Cinematic color grading — ON by default (remaster look)
bool g_debugDisableSharpen = false;           // Contrast-adaptive sharpening — ON by default
bool g_debugDisableLaserGlow = false;         // Laser/stream glow pass — ON by default
bool g_debugDisableTracerStreak = false;      // Tracer fading trail — ON by default
bool g_debugDisableColorAwareFX = false;      // Toxin haze / fire bloom — ON by default
bool g_debugDisableVolumetric = true;         // Volumetric explosion clouds — OFF by default
// Modern AOE paints hardcoded fog colors over toxin/anthrax/radiation/napalm/
// fire-field particles. The colors don't match the INI-authored particles and
// the fog blob occludes the actual particles. The original game just rendered
// the particles directly. Default OFF — re-enable from inspector if desired.
bool g_debugDisableModernAOE = true;         // Modern ground AOE fog — OFF by default
bool g_debugDisableSurfaceSpec = false;       // Surface specular highlights — ON by default
bool g_debugDisableDistanceFog = true;       // Distance fog — OFF by default
bool g_debugDisableLensFlare = false;         // Procedural lens flare — ON by default
bool g_debugDisableSmoothParticleFade = false; // Smooth lifetime-based particle alpha — ON by default
bool g_debugDisableVolumetricTrails = false;  // GPU volumetric smoke trails — ON by default
bool g_useClassicTrails = true;               // Classic ribbon trails — ON by default; matches the original D3D8 StreakRenderer look
bool g_debugDisableBloom = false;             // Bloom — ON by default

// --- Visual ENHANCEMENT toggles ---
// These all default to FALSE so the renderer matches the original DX8 look.
// Each one is opt-in via the Inspector "Render Toggles → Visual FX" tab.
// Naming convention: g_useEnhancedXxx = false means classic, true means enhanced.
// All default OFF — classic look matches original DX8 game. User opts in
// via Inspector → Render Toggles → Enhancements tab.
// Post-audit polish (iter 10): enhanced water shader parameters dampened
// after the first audit run showed visible tessellation artifacts. Bump
// scale 0.45→0.15, fresnel mix 0.35→0.15, spec pow96*0.55→pow48*0.25,
// foam threshold 1/2000→1/3500, foam uses smoothstep instead of pow.
bool g_useEnhancedWater = false;
bool g_useEnhancedParticles = true;  // Enhanced particles (4-way blend mode preservation + unlit shader)
bool g_useEnhancedSmudges = true;    // Enhanced smudges (heat-haze refraction on explosions)
bool g_useDepthBasedFoam = false;            // Implied by g_useEnhancedWater


// Autotest mode (defined in CommandLine.cpp)
extern Int g_autotestFrames;

// Forward declarations for autotest exit
class GameEngine;
extern GameEngine *TheGameEngine;
// Cannot include GameEngine.h (chains to DX8), so declare setQuitting directly
namespace { void AutotestQuit() { PostQuitMessage(0); } }

// Forward declarations
class RTS3DScene;
class W3DAssetManager;
extern W3DAssetManager* EnsureWW3DAssetManagerInstance();
extern void DestroyWW3DAssetManagerInstance();

// Preload helper - defined in D3D11Shims.cpp to avoid pulling in assetmgr.h here
extern void PreloadModelViaAssetManager(W3DAssetManager* mgr, const char* name);

static void AppendDX11SceneTrace(const char* format, ...)
{
	return; // Debug logging removed
}

// Static members for backward compatibility with draw modules
RTS3DScene *W3DDisplay::m_3DScene = nullptr;
W3DAssetManager *W3DDisplay::m_assetManager = nullptr;

// ============================================================================
// Construction / destruction
// ============================================================================

W3DDisplay::W3DDisplay()
	: m_initialized(0)
	, m_isClippedEnabled(false)
	, m_averageFPS(0)
	, m_currentFPS(0)
	, m_benchmarkDisplayString(nullptr)
	, m_lastLightUpdateTime(0)
{
	m_clipRegion.lo.x = 0;
	m_clipRegion.lo.y = 0;
	m_clipRegion.hi.x = 0;
	m_clipRegion.hi.y = 0;
}

W3DDisplay::~W3DDisplay()
{
	// Inspector must shut down before the Renderer — it holds D3D11
	// device references that become invalid the moment Renderer::Shutdown
	// destroys the device.
	Inspector::Shutdown();

	// Shadow manager holds D3D resources — tear it down before the
	// Renderer is shut down.
	if (TheW3DShadowManager)
	{
		TheW3DShadowManager->ReleaseResources();
		delete TheW3DShadowManager;
		TheW3DShadowManager = nullptr;
	}

	if (m_assetManager)
	{
		DestroyWW3DAssetManagerInstance();
		m_assetManager = nullptr;
	}

	Render::ModelRenderer::Instance().Shutdown();
	Render::TerrainRenderer::Instance().Shutdown();
	Render::ImageCache::Instance().Clear();
	Render::Renderer::Instance().Shutdown();
}

// ============================================================================
// init - Create the D3D11 device and initialize the renderer
// ============================================================================

void W3DDisplay::init()
{
	Display::init();

	if (m_initialized)
		return;

	if (m_assetManager == nullptr)
		m_assetManager = EnsureWW3DAssetManagerInstance();

	if (TheGlobalData->m_headless)
	{
		m_initialized = true;
		return;
	}

#ifdef _WIN32
	// For borderless fullscreen, use the actual monitor resolution
	if (!TheGlobalData->m_windowed)
	{
		// Get physical pixel size via DXGI (not affected by DPI scaling)
		HMONITOR hMon = MonitorFromWindow(ApplicationHWnd, MONITOR_DEFAULTTOPRIMARY);
		MONITORINFO mi = {};
		mi.cbSize = sizeof(mi);
		GetMonitorInfo(hMon, &mi);
		Int screenW = mi.rcMonitor.right - mi.rcMonitor.left;
		Int screenH = mi.rcMonitor.bottom - mi.rcMonitor.top;
		TheWritableGlobalData->m_xResolution = screenW;
		TheWritableGlobalData->m_yResolution = screenH;
	}
#endif

	setWidth(TheGlobalData->m_xResolution);
	setHeight(TheGlobalData->m_yResolution);
	setBitDepth(32);
	setWindowed(TheGlobalData->m_windowed);

	// Initialize the D3D11 renderer - this is REAL device creation
	bool debug = false;
#if defined(RTS_DEBUG)
	debug = true;
#endif

#ifdef _WIN32
	if (!Render::Renderer::Instance().Init(ApplicationHWnd, debug))
#else
	if (!Render::Renderer::Instance().Init(nullptr, debug))
#endif
	{
		DEBUG_CRASH(("Failed to initialize D3D11 Renderer"));
		throw ERROR_INVALID_D3D;
	}

	// Create a scene for 3D objects (draw modules add/remove render objects here)
	if (!m_3DScene)
	{
		extern void CreateD3D11Scene();
		CreateD3D11Scene();
	}

#if defined(BUILD_WITH_D3D11) && defined(USE_SDL)
	// Bring up the in-process inspector overlay. Hidden by default — F10
	// toggles. Failure to init is non-fatal: log and keep going so a
	// broken inspector can never block normal gameplay.
	{
		SDL_Window* sdlWindow = Platform::SDLPlatform::Instance().GetWindow();
		auto& dev = Render::Renderer::Instance().GetDevice();
		if (sdlWindow && dev.GetDevice() && dev.GetContext())
		{
			if (!Inspector::Init(sdlWindow, dev.GetDevice(), dev.GetContext()))
				DEBUG_LOG(("Inspector::Init failed — overlay disabled this session"));
			else
				DEBUG_LOG(("Inspector ready — press F10 to toggle"));
		}
	}
#endif

#ifdef _WIN32
	// In windowed mode, if a specific resolution was requested, resize the window to match
	if (TheGlobalData->m_windowed && ApplicationHWnd)
	{
		int reqW = TheGlobalData->m_xResolution;
		int reqH = TheGlobalData->m_yResolution;
		if (reqW > 0 && reqH > 0)
		{
			RECT rc = { 0, 0, reqW, reqH };
			AdjustWindowRect(&rc, GetWindowLong(ApplicationHWnd, GWL_STYLE), FALSE);
			SetWindowPos(ApplicationHWnd, nullptr, 0, 0,
				rc.right - rc.left, rc.bottom - rc.top,
				SWP_NOMOVE | SWP_NOZORDER);
			Render::Renderer::Instance().Resize(reqW, reqH);
		}
	}
#endif

	// Use the ACTUAL D3D11 render target size
	int actualW = Render::Renderer::Instance().GetWidth();
	int actualH = Render::Renderer::Instance().GetHeight();
	setWidth(actualW);
	setHeight(actualH);
	TheWritableGlobalData->m_xResolution = actualW;
	TheWritableGlobalData->m_yResolution = actualH;

	// Check if level was never set and default to setting most suitable for system
	if (TheGameLODManager && TheGameLODManager->getStaticLODLevel() == STATIC_GAME_LOD_UNKNOWN)
	{
		TheGameLODManager->setStaticLODLevel(TheGameLODManager->getRecommendedStaticLODLevel());
	}

	// Bring up the shadow manager. Must come after Renderer::Init so that
	// D3D resources exist, and after GlobalData is populated (its ctor
	// reads m_terrainLightPos for the initial sun position).
	if (!TheW3DShadowManager)
	{
		TheW3DShadowManager = NEW W3DShadowManager;
		TheW3DShadowManager->init();
	}

	m_initialized = true;
	if (TheGlobalData->m_displayDebug)
	{
		m_debugDisplayCallback = StatDebugDisplay;
	}

#ifdef BUILD_WITH_VULKAN
	DEBUG_LOG(("W3DDisplay::init - Vulkan Renderer initialized at %dx%d", getWidth(), getHeight()));
#else
	DEBUG_LOG(("W3DDisplay::init - D3D11 Renderer initialized at %dx%d", getWidth(), getHeight()));
#endif
}

// ============================================================================
// reset
// ============================================================================

void W3DDisplay::reset()
{
	Display::reset();
	m_lightPulses.clear();
	m_lastLightUpdateTime = 0;

	// Per-map reset: drop all shadows belonging to the previous map.
	if (TheW3DShadowManager)
		TheW3DShadowManager->Reset();
}

// ============================================================================
// setDisplayMode - Resize the renderer
// ============================================================================

Bool W3DDisplay::setDisplayMode(UnsignedInt xres, UnsignedInt yres, UnsignedInt bitdepth, Bool windowed)
{
	if (xres == 0 || yres == 0)
		return FALSE;

	Display::setDisplayMode(xres, yres, bitdepth, windowed);
	Render::Renderer::Instance().Resize(xres, yres);
	setBitDepth(bitdepth);
	setWindowed(windowed);

	return TRUE;
}

void W3DDisplay::setWidth(UnsignedInt width)
{
	Display::setWidth(width);
}

void W3DDisplay::setHeight(UnsignedInt height)
{
	Display::setHeight(height);
}

Int W3DDisplay::getDisplayModeCount()
{
	return 1; // We support native resolution only
}

void W3DDisplay::getDisplayModeDescription(Int modeIndex, Int *xres, Int *yres, Int *bitDepth)
{
#ifdef USE_SDL
	SDL_DisplayID displayID = SDL_GetPrimaryDisplay();
	const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(displayID);
	if (mode) { *xres = mode->w; *yres = mode->h; }
	else { *xres = 1920; *yres = 1080; }
#elif defined(_WIN32)
	*xres = GetSystemMetrics(SM_CXSCREEN);
	*yres = GetSystemMetrics(SM_CYSCREEN);
#else
	*xres = 1920; *yres = 1080;
#endif
	*bitDepth = 32;
}

void W3DDisplay::setGamma(Real gamma, Real bright, Real contrast, Bool calibrate)
{
#ifdef BUILD_WITH_D3D11
	// Apply gamma/brightness/contrast via the DXGI swap chain's gamma ramp.
	auto& device = Render::Renderer::Instance().GetDevice();
	IDXGISwapChain1* swapChain = device.GetSwapChain();
	if (!swapChain)
		return;

	ComPtr<IDXGIOutput> output;
	if (FAILED(swapChain->GetContainingOutput(&output)) || !output)
		return;

	// Build a gamma ramp.  Each channel entry maps a linear [0..1] input to
	// an output intensity.  We apply:  out = contrast * pow(in, 1/gamma) + brightness
	// The DXGI ramp uses float values in [0, 1].
	DXGI_GAMMA_CONTROL ramp = {};
	ramp.Scale  = { 1.0f, 1.0f, 1.0f };
	ramp.Offset = { 0.0f, 0.0f, 0.0f };

	Real invGamma = (gamma > 0.01f) ? (1.0f / gamma) : 1.0f;

	for (int i = 0; i < 1025; ++i)
	{
		Real t = (Real)i / 1024.0f;
		Real value = contrast * powf(t, invGamma) + bright;
		if (value < 0.0f) value = 0.0f;
		if (value > 1.0f) value = 1.0f;
		ramp.GammaCurve[i].Red   = value;
		ramp.GammaCurve[i].Green = value;
		ramp.GammaCurve[i].Blue  = value;
	}

	output->SetGammaControl(&ramp);
#endif // BUILD_WITH_D3D11

#ifdef BUILD_WITH_VULKAN
	// Vulkan doesn't have a direct gamma ramp API.
	// Store the gamma value — a post-process shader can apply it if needed.
	// For now, the brightness slider has no visible effect on Vulkan.
	// TODO: Implement as a full-screen post-process pass or via swapchain sRGB format.
	(void)gamma; (void)bright; (void)contrast; (void)calibrate;
#endif
}

// ============================================================================
// updateAverageFPS
// ============================================================================

void W3DDisplay::updateAverageFPS()
{
	static UnsignedInt lastTime = 0;
	static Int frameCount = 0;
	UnsignedInt now = timeGetTime();

	frameCount++;

	if (now - lastTime > 500)
	{
		m_currentFPS = (Real)frameCount * 1000.0f / (Real)(now - lastTime);
		m_averageFPS = m_averageFPS * 0.8f + m_currentFPS * 0.2f;
		frameCount = 0;
		lastTime = now;
	}
}

// ============================================================================
// step
// ============================================================================

void W3DDisplay::step()
{
	Display::step();
}

// ============================================================================
// Sun shadow map — depth pre-pass helpers.
//
// BuildSunViewProjection: builds an orthographic matrix fitting a square
// region of ground (`kShadowFootprint` world units on a side) centered at the
// camera's XY position, viewed from the direction of the sun.
//
// RenderShadowCasters: walks the same scene containers the main pass walks
// (W3DDisplay::m_3DScene + TheGameClient drawable list) plus the terrain,
// issuing normal Draw3DIndexed calls. Because Renderer::BeginShadowPass has
// (a) swapped the color RT for the shadow DSV with color-writes disabled,
// (b) pinned FrameConstants.viewProjection to sunVP, the same VS code paths
// now write the scene's depth from the sun's POV.
// ============================================================================

// How many world units across on the ground the sun "sees" per shadow pass.
// Larger = more shadows in frame but softer / lower effective resolution;
// smaller = sharper but shadows pop at camera movement. The Inspector's
// Shadows panel edits these live, so they're mutable globals (not constexpr).
// Defaults tuned visually in-game (2026-04-19).
float g_shadowFootprint  = 1500.0f;
float g_sunEyeDistance   = 6500.0f;
float g_sunNear          = 0.1f;
float g_sunFar           = 45000.0f;

static Render::Float4x4 BuildSunViewProjection(CameraClass* camera)
{
	using namespace Render;

	// Camera focus on the ground: start at camera XY, Z = 0. A more accurate
	// estimate would ray-cast the camera forward onto the heightmap, but the
	// XY position is already close enough for a 2400-unit footprint and it
	// avoids a heightmap probe on every frame.
	Vector3 camPos(0, 0, 0);
	if (camera)
		camPos = camera->Get_Position();
	Float3 focus = { camPos.X, camPos.Y, 0.0f };

	// Sun direction — ray from sun to ground, normalized. Pull from the
	// shadow manager's cached light position (set from the map's INI /
	// time-of-day data and refreshed per frame in DoShadows).
	Float3 sunRay = { 0.0f, 0.0f, -1.0f };
	if (TheW3DShadowManager)
	{
		Vector3 lw = TheW3DShadowManager->getLightPosWorld(0);
		float len = sqrtf(lw.X * lw.X + lw.Y * lw.Y + lw.Z * lw.Z);
		if (len > 0.001f)
		{
			// getLightPosWorld returns the sun POSITION (far above ground) —
			// the ray to the ground is -position / |position| from a caster
			// at origin, but for directional-shadow purposes we just want
			// the unit sun-direction with Z negative.
			sunRay = { -lw.X / len, -lw.Y / len, -lw.Z / len };
		}
	}

	// Sun eye: place far enough along the reverse of sunRay that the entire
	// visible footprint sits inside the ortho near/far band.
	Float3 sunEye = {
		focus.x - sunRay.x * g_sunEyeDistance,
		focus.y - sunRay.y * g_sunEyeDistance,
		focus.z - sunRay.z * g_sunEyeDistance,
	};

	// Up vector — world +Y is a safe non-parallel default for Generals'
	// Z-up coordinate system. Sun is always mostly-vertical (rayZ < 0) with
	// some horizontal component, so world-Y never ends up parallel to the
	// view direction.
	Float3 up = { 0.0f, 1.0f, 0.0f };

	Float4x4 view = Float4x4LookAtRH(sunEye, focus, up);
	Float4x4 proj = Float4x4OrthoRH(g_shadowFootprint, g_shadowFootprint, g_sunNear, g_sunFar);
	return Float4x4Multiply(view, proj);
}

// Forward-declare the scene-walk helper — its body lives alongside the
// reflection-meshes helper in D3D11Shims.cpp. Returns number of submitted
// render objects.
extern int RenderShadowCastersDX11(CameraClass* camera);

// ============================================================================
// draw - THE MAIN RENDERING FUNCTION
// This is called every frame. Real D3D11 rendering happens here.
// ============================================================================

void W3DDisplay::draw()
{
	LIVE_PERF_SCOPE("W3DDisplay::draw");
	static int s_loggedFrames = 0;

	// The game loop calls draw() multiple times per logic frame (e.g., 11 times
	// during gameFrame=0 loading). Each call does BeginFrame (clear to black) +
	// EndFrame (Present), causing rapid black frame flashing.
	// Fix: track if we've already rendered for this logic frame and skip
	// redundant clear+present cycles. Allow re-rendering when the logic frame
	// advances (new game state to display).
	//
	// HOWEVER, once gameplay actually starts (curGameFrame > 0) and the
	// FramePacer's logic-time-scale accumulator is decoupling render from
	// the 30 Hz simulation, multi-draw per logic tick is REQUIRED, not a
	// bug. Each render between two logic ticks lerps drawables further
	// along the prev→current logic transforms via Drawable::draw's
	// getInterpolationFraction path. Without this we'd be capped at one
	// draw per logic tick (i.e. 30 FPS in campaign).
	static unsigned int s_lastRenderedGameFrame = UINT_MAX;
	static bool s_renderedThisFrame = false;
	unsigned int curGameFrame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	if (curGameFrame != s_lastRenderedGameFrame)
	{
		s_lastRenderedGameFrame = curGameFrame;
		s_renderedThisFrame = false;
	}
	else if (s_renderedThisFrame && !g_debugDisableMultiDrawSkip)
	{
		// Already rendered and presented for this game frame.
		// BUT: if the game is paused (e.g. ESC menu), we must keep
		// rendering so the menu is visible and transitions animate.
		// Also allow re-rendering during load screens — the load screen
		// plays a video in its own loop while the game frame stays at 0.
		//
		// CRITICAL: the shell menu (not in a match) and the shell map need
		// to keep rendering every frame, because the logic frame never
		// advances while sitting at the main menu. Without this exception
		// the entire main menu freezes on the first rendered frame.
		const bool inGame = TheGameLogic && TheGameLogic->isInGame();
		const bool inShellGame = TheGameLogic && TheGameLogic->isInShellGame();
		const bool gamePaused = TheGameLogic && TheGameLogic->isGamePaused();
		const bool fullscreenMoviePlaying = isMoviePlaying();
		const bool loadScreenActive = TheGlobalData && TheGlobalData->m_loadScreenRender;
		const bool uiMoviePlaying = TheInGameUI &&
			(TheInGameUI->videoBuffer() != nullptr || TheInGameUI->cameoVideoBuffer() != nullptr);
		// Inspector pause freezes the logic frame counter, which would
		// otherwise trip this early-out and stop calling Inspector::
		// BeginFrame/Render — the toolbar would stop receiving NewFrame
		// and become unresponsive. Force re-render whenever the
		// inspector has paused the simulation.
		const bool inspectorPaused = Inspector::IsPaused();

		// Bypass the dedupe entirely once gameplay has begun and the
		// FramePacer is running the simulation on its accumulator. The
		// renderer must be free to draw many frames per 30 Hz logic tick
		// for visual interpolation to look smooth. We restrict this to
		// non-shell, non-frame-zero (so the original loading-flash dedupe
		// still triggers in the shell map and the very first setup
		// frames) and only when no fullscreen UI/video state would prefer
		// the existing carve-outs below.
		const bool interpolatingGameplay =
			TheFramePacer && TheFramePacer->isLogicTimeScaleEnabled()
			&& curGameFrame > 0 && inGame && !inShellGame
			&& !gamePaused && !fullscreenMoviePlaying && !loadScreenActive
			&& !uiMoviePlaying && !inspectorPaused;
		if (interpolatingGameplay)
		{
			// Fall through to the actual rendering path below — each
			// render advances the visual interpolation fraction.
		}
		else if (inGame && !gamePaused && !fullscreenMoviePlaying && !loadScreenActive && !uiMoviePlaying && !inspectorPaused)
		{
			return;
		}
	}

#ifdef _WIN32
	if (ApplicationHWnd && ::IsIconic(ApplicationHWnd))
		return;
#endif

	if (TheGlobalData->m_headless)
		return;

	// Inspector::BeginFrame must run before any engine rendering for the
	// frame, but after the headless / iconified early-outs above (no
	// point starting an ImGui frame for a frame we're going to skip).
	Inspector::BeginFrame();

	// The view update phase computes the tactical camera transform and refreshes
	// per-view world state before any 3D rendering. The original W3D display
	// path did this each frame; without it the DX11 terrain pass renders with an
	// uninitialized camera at the origin.
	if (!g_debugDisableUpdateViews) updateViews();

	// Inspector free-fly editor camera override. Runs AFTER the
	// engine's updateViews() has recomputed its own camera transform
	// from the pivot/angle/zoom system, so the override clobbers the
	// engine's value before any 3D rendering reads it. No-op when the
	// inspector camera is disabled.
	Inspector::Camera::ApplyToEngineCamera();

	// Advance WW3D animation clock. This drives skeletal animation, UV-offset
	// mappers, particle emitters, dazzles, and anything else that reads
	// WW3D::Get_Sync_Time() or Get_Logic_Time_Milliseconds().
	// Advance WW3D animation clock ONLY on logic tick boundaries.
	// Multiple render frames may occur per logic tick — animation must not
	// advance between them or meshes flicker due to sub-frame bone changes.
	if (!g_debugDisableWW3DSync)
	{
		static unsigned int s_lastSyncLogicFrame = UINT_MAX;
		unsigned int logicFrame = TheGameLogic ? TheGameLogic->getFrame() : 0;
		if (logicFrame != s_lastSyncLogicFrame)
		{
			s_lastSyncLogicFrame = logicFrame;
			WW3D::Sync(true);
		}
	}

	if (!g_debugDisableTrackUpdate && TheTerrainTracksRenderObjClassSystem)
		TheTerrainTracksRenderObjClassSystem->update();

	updateAverageFPS();

	auto& renderer = Render::Renderer::Instance();

	// Set fog + specular before BeginFrame so they're in frame constants
	renderer.SetAtmosphereEnabled(false);
	renderer.SetSurfaceSpecularEnabled(!g_debugDisableSurfaceSpec);
	// Wire enhanced-water toggle to atmosphereParams.w. Default OFF =
	// classic look matching original DX8.
	renderer.SetEnhancedWaterEnabled(g_useEnhancedWater);

	// Begin a new frame - clears the screen with actual D3D11 calls
	g_debugFrameDrawCalls = 0;
	renderer.BeginFrame();

	// --- UPDATE DYNAMIC LIGHTS ---
	{
		UnsignedInt now = timeGetTime();
		if (m_lastLightUpdateTime == 0)
			m_lastLightUpdateTime = now;
		float deltaMs = (float)(now - m_lastLightUpdateTime);
		// Clamp delta to avoid huge jumps (e.g. after alt-tab)
		if (deltaMs > 200.0f)
			deltaMs = 200.0f;
		m_lastLightUpdateTime = now;

		if (!g_debugDisableLightPulse)
		{
			updateLightPulses(deltaMs);
			applyLightPulsesToRenderer();
		}
	}

	// --- 3D SCENE RENDERING ---
	CameraClass* camera = nullptr;
	if (TheTacticalView)
	{
		W3DView* w3dView = static_cast<W3DView*>(TheTacticalView);
		camera = w3dView->get3DCamera();
	}

	// Diagnostic: log camera state for the first few frames after a mission starts
	{
		static unsigned int s_diagLastFrame = UINT_MAX;
		static int s_diagCount = 0;
		if (curGameFrame < s_diagLastFrame && curGameFrame < 5) {
			s_diagCount = 0; // reset when game frame counter resets (new mission)
		}
		s_diagLastFrame = curGameFrame;
		if (s_diagCount < 30 && curGameFrame <= 60 && TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame())
		{
			char diagBuf[256];
			snprintf(diagBuf, sizeof(diagBuf),
				"MissionRender frame=%u zoom=%.3f cam=%p hmap=%p terrainReady=%d letterbox=%d\n",
				curGameFrame,
				TheTacticalView ? TheTacticalView->getZoom() : -1.0f,
				camera,
				GetTerrainHeightMap(),
				(GetTerrainHeightMap() && Render::TerrainRenderer::Instance().IsReady()) ? 1 : 0,
				m_letterBoxEnabled ? 1 : 0);
			OutputDebugStringA(diagBuf);
			s_diagCount++;
		}
	}

	// Render the terrain if we have a heightmap and a camera
	WorldHeightMap* heightMap = GetTerrainHeightMap();
	if (heightMap)
	{
		auto& terrainRenderer = Render::TerrainRenderer::Instance();

		// Build the mesh on first use or if invalidated
		if (!terrainRenderer.IsReady())
		{
			terrainRenderer.BuildMesh(heightMap);
			terrainRenderer.BuildRoadMesh(heightMap);
			terrainRenderer.BuildBridgeMeshes(heightMap);
		}

		if (camera)
		{
			if (s_loggedFrames < 6)
			{
				AppendDX11SceneTrace(
					"W3DDisplay::draw frame=%d heightMap=%p tacticalView=%p camera=%p terrainReady=%d\n",
					s_loggedFrames,
					heightMap,
					TheTacticalView,
					camera,
					terrainRenderer.IsReady() ? 1 : 0);
			}
			{
			// Profile each render pass (writes timing to perf log for first 30 frames)
			static int s_perfFrames = 0;
			auto perfNow = []() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; };
			auto perfMs = [](long long start, long long end) {
				LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);
				return (double)(end - start) * 1000.0 / (double)freq.QuadPart;
			};

			// Set time for shader animations (water bump, cloud scroll)
			renderer.SetTime(static_cast<float>(WW3D::Get_Sync_Time()));

			// --- Sun shadow map pre-pass ---
			// Builds a D32 depth texture of shadow-CASTERS only, from the
			// sun's POV. The main Shader3D.PSMain samples this on t4 via
			// ComputeShadowVisibility in ComputeLighting to darken the
			// primary directional light on shadow RECEIVERS.
			//
			// Key invariant: the terrain is a RECEIVER, not a caster. If
			// we rendered terrain into this map it would fill the entire
			// footprint with a single smooth depth surface, which then
			// sample-matches every ground receiver and swamps contributions
			// from actual casters — producing the visible "one big plane"
			// bug. Terrain therefore only shows up in the main pass, where
			// it samples the map and draws itself shadowed.
			if (!g_debugDisableSunShadowMap && camera)
			{
				LIVE_PERF_SCOPE("W3DDisplay::shadowMap");
				Render::Float4x4 sunVP = BuildSunViewProjection(camera);
				renderer.BeginShadowPass(sunVP);

				// Only scene meshes + drawables (buildings, units, props).
				// RenderShadowCastersDX11 internally calls
				// ModelRenderer::BeginFrame which stomps the shadow viewport
				// with the main camera's backbuffer-sized one; the helper
				// re-asserts the shadow viewport before issuing any draws.
				RenderShadowCastersDX11(camera);

				renderer.EndShadowPass();
				// Restore3DState is called inside EndShadowPass, which binds the
				// main 3D shader + rebinds the shadow SRV on t4 / sampler on s2
				// for receivers to read.
			}

			// depth is cleared in BeginFrame — no extra clear needed
			{
			LIVE_PERF_SCOPE("W3DDisplay::terrainPasses");
			auto t0 = perfNow();
			if (!g_debugDisableSkyBox)  terrainRenderer.RenderSkyBox(camera);
			renderer.Restore3DState();
			auto t1 = perfNow();
			if (!g_debugDisableTerrain) terrainRenderer.Render(camera, heightMap);
			renderer.Restore3DState();
			auto t2 = perfNow();
			if (!g_debugDisableRoads)   terrainRenderer.RenderRoads(camera);
			renderer.Restore3DState();
			auto t3 = perfNow();
			if (!g_debugDisableBridges) terrainRenderer.RenderBridges(camera);
			renderer.Restore3DState();
			auto t4 = perfNow();
			if (!g_debugDisableProps)   RenderTerrainPropsDX11(camera);
			renderer.Restore3DState();
			auto t5 = perfNow();
			if (!g_debugDisableBibs)    RenderBibsDX11(camera);
			renderer.Restore3DState();
			auto t6 = perfNow();
			if (!g_debugDisableScorch)  RenderScorchMarksDX11(camera);
			renderer.Restore3DState();
			auto t7 = perfNow();
			if (!g_debugDisableTracks)  RenderTerrainTracksDX11(camera);
			renderer.Restore3DState();
			auto t8 = perfNow();
			if (!g_debugDisableWaypoints) RenderWaypointsDX11(camera);
			renderer.Restore3DState();
			auto t9 = perfNow();
			if (!g_debugDisableShroud)  terrainRenderer.RenderShroud(camera);
			renderer.Restore3DState();
			auto t10 = perfNow();

			(void)t0; (void)t1; (void)t2; (void)t3; (void)t4; (void)t5; (void)t6; (void)t7; (void)t8; (void)t9; (void)t10;
			}
		}
		}
		else if (s_loggedFrames < 6)
		{
			AppendDX11SceneTrace(
				"W3DDisplay::draw frame=%d heightMap=%p tacticalView=%p camera=null terrainReady=%d\n",
				s_loggedFrames,
				heightMap,
				TheTacticalView,
				terrainRenderer.IsReady() ? 1 : 0);
		}
	}
	else if (s_loggedFrames < 6)
	{
		AppendDX11SceneTrace(
			"W3DDisplay::draw frame=%d heightMap=null tacticalView=%p\n",
			s_loggedFrames,
			TheTacticalView);
	}

	// Raise the scene flag so the two DoShadows() hooks know they're
	// running inside a frame that should emit shadows.
	if (TheW3DShadowManager)
		TheW3DShadowManager->queueShadows(TRUE);

	// Projected shadow decals — render before opaque meshes so they stamp
	// onto the already-drawn terrain and are then overdrawn by opaque
	// object geometry.
	if (camera && TheW3DShadowManager && !g_debugDisableProjectedShadows)
	{
		RenderInfoClass rinfo(*camera);
		DoShadows(rinfo, FALSE);
		renderer.Restore3DState();
	}

	// Draw all views of the world (calls drawViews which triggers view rendering)
	{
		LIVE_PERF_SCOPE("W3DDisplay::drawViews");
		if (!g_debugDisableDrawViews) drawViews();
	}

	// Flush deferred translucent meshes sorted back-to-front.
	{
		LIVE_PERF_SCOPE("W3DDisplay::flushTranslucent");
		if (!g_debugDisableTranslucent)
			Render::ModelRenderer::Instance().FlushTranslucent();
		renderer.Restore3DState();
	}

	// Stencil-volume shadow pass removed. Sun shadow mapping runs as a
	// pre-pass earlier in draw(); the trailing call into DoShadows with
	// stencilPass=TRUE now exists only to clear the shadow-scene latch
	// set by the projected-decal pre-pass.
	if (camera && TheW3DShadowManager)
	{
		RenderInfoClass rinfo(*camera);
		DoShadows(rinfo, TRUE);
	}

	// --- Occluded-unit silhouettes ---
	// DISABLED: first pass overpowered visible units (drew the silhouette
	// color across every unit's surface, not just where occluded). Needs
	// to be rewritten with a proper stencil-buffer pass that only marks
	// "behind something" pixels per unit, then draws the colored quad
	// only for those marked pixels. Tracked in feature parity backlog.

	// Render water BEFORE particle capture so heat distortion doesn't affect water
	if (camera && !g_debugDisableWater)
	{
		LIVE_PERF_SCOPE("W3DDisplay::water");
		Render::TerrainRenderer::Instance().RenderWater(camera);
		RenderWaterTracksDX11(camera);
	}

	// Particle post-FX disabled — the original game rendered particles with
	// simple additive/alpha blending, no glow bloom or heat distortion.
	renderer.SetParticleGlowEnabled(false);
	renderer.SetHeatDistortionEnabled(false);
	renderer.SetColorAwareFxEnabled(false);

	if (!g_debugDisableParticles && TheParticleSystemManager && camera)
	{
		LIVE_PERF_SCOPE("W3DDisplay::particles");
		RenderInfoClass rinfo(*camera);
		TheParticleSystemManager->doParticles(rinfo);
		renderer.Restore3DState();
	}

	// GPU volumetric smoke particles — update and render
	extern bool g_debugDisableVolumetricTrails;
	if (!g_debugDisableVolumetricTrails)
	{
		LIVE_PERF_SCOPE("W3DDisplay::gpuVolumetric");
		auto& gpuP = Render::GPUParticleSystem::Instance();
		if (!gpuP.IsReady())
			gpuP.Init(renderer.GetDevice());
		if (gpuP.IsReady() && camera)
		{
			float dt = 1.0f / 30.0f; // fixed timestep matching game logic
			gpuP.Update(renderer.GetDevice(), dt, { 0.5f, 0.2f, 0.0f }); // gentle wind

			const auto& fd = renderer.GetFrameData();
			Matrix3D camTM = camera->Get_Transform();
			Render::Float3 camRight = { camTM[0][0], camTM[1][0], camTM[2][0] };
			Render::Float3 camUp = { camTM[0][1], camTM[1][1], camTM[2][1] };
			Render::Float3 camPos = { fd.cameraPos.x, fd.cameraPos.y, fd.cameraPos.z };

			renderer.FlushFrameConstants(); // ensure viewProjection is in b0
			gpuP.Render(renderer.GetDevice(), fd.viewProjection, camPos,
				camRight, camUp, (float)renderer.GetWidth(), (float)renderer.GetHeight());
			renderer.Restore3DState();
		}
	}

	if (!g_debugDisableSnow) RenderSnowDX11();

	// Track significant particle systems for fade-out clouds when game logic deletes them
	// Only track explosions, smoke, toxin, fire — NOT bullets, muzzle flashes, debris
	if (TheParticleSystemManager && !g_debugDisableVolumetric)
	{
		LIVE_PERF_SCOPE("W3DDisplay::particleTracking");
		auto& sysList = TheParticleSystemManager->getAllParticleSystems();
		for (auto it = sysList.begin(); it != sysList.end(); ++it)
		{
			ParticleSystem* sys = *it;
			if (!sys || !sys->getTemplate()) continue;
			const char* name = sys->getTemplate()->getName().str();
			if (!name || !name[0]) continue;

			// Only track significant visual effects — skip small weapon FX
			bool isExplosion = (strstr(name, "Explo") || strstr(name, "explo") ||
			                    strstr(name, "Detonation") || strstr(name, "detonation"));
			bool isSmoke = (strstr(name, "Smoke") || strstr(name, "smoke") ||
			                strstr(name, "SmokePlume") || strstr(name, "DarkSmoke"));
			bool isToxin = (strstr(name, "Toxin") || strstr(name, "toxin") ||
			                strstr(name, "Anthrax") || strstr(name, "anthrax") ||
			                strstr(name, "Poison") || strstr(name, "poison"));
			bool isFire = (strstr(name, "Fire") || strstr(name, "fire") ||
			               strstr(name, "Flame") || strstr(name, "flame") ||
			               strstr(name, "Napalm") || strstr(name, "napalm"));
			bool isNuke = (strstr(name, "Nuke") || strstr(name, "nuke") ||
			               strstr(name, "Nuclear") || strstr(name, "nuclear"));

			if (!isExplosion && !isSmoke && !isToxin && !isFire && !isNuke) continue;

			Coord3D pos;
			sys->getPosition(&pos);
			if (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f) continue;

			// Get color from first particle
			float r = 0.3f, g = 0.3f, b = 0.3f;
			Particle* firstP = sys->getFirstParticle();
			if (firstP)
			{
				const RGBColor* col = firstP->getColor();
				r = col->red; g = col->green; b = col->blue;
			}

			float size = isSmoke ? 8.0f : (isExplosion || isNuke ? 10.0f : 6.0f);
			renderer.TrackParticleSystem((uintptr_t)sys, pos.x, pos.y, pos.z, r, g, b, size);
		}
		renderer.FinishParticleTracking();
	}

	// Apply particle post-FX (glow/heat distortion) after all 3D, before UI
	renderer.ApplyParticleFX();

	// Volumetric explosion clouds + ground AOE fog
	renderer.SetVolumetricExplosionsEnabled(!g_debugDisableVolumetric);

	extern bool g_debugDisableModernAOE;
	renderer.SetModernAOEEnabled(!g_debugDisableModernAOE);
	renderer.ClearGroundFog();
	if (!g_debugDisableModernAOE && TheParticleSystemManager)
	{
		int fogIdx = 0;
		auto& sysList = TheParticleSystemManager->getAllParticleSystems();
		for (auto it = sysList.begin(); it != sysList.end() && fogIdx < 8; ++it)
		{
			ParticleSystem* sys = *it;
			if (!sys || !sys->getTemplate()) continue;
			const char* name = sys->getTemplate()->getName().str();
			if (!name || !name[0]) continue;

			// Match AOE ground effect keywords
			bool isToxin = (strstr(name, "Toxin") || strstr(name, "toxin") ||
			                strstr(name, "Anthrax") || strstr(name, "anthrax") ||
			                strstr(name, "Poison") || strstr(name, "poison"));
			bool isRadiation = (strstr(name, "Radiat") || strstr(name, "radiat") ||
			                    strstr(name, "Nuclear") || strstr(name, "nuclear") ||
			                    strstr(name, "Nuke") || strstr(name, "nuke"));
			bool isNapalm = (strstr(name, "Napalm") || strstr(name, "napalm") ||
			                 strstr(name, "FireField") || strstr(name, "fireField"));

			if (!isToxin && !isRadiation && !isNapalm) continue;

			Coord3D pos;
			sys->getPosition(&pos);
			if (pos.x == 0.0f && pos.y == 0.0f) continue;

			float r, g, b;
			if (isNapalm)         { r = 0.8f; g = 0.3f; b = 0.05f; }
			else if (isRadiation) { r = 0.5f; g = 0.7f; b = 0.1f;  }
			else                  { r = 0.1f; g = 0.6f; b = 0.15f;  }

			renderer.SetGroundFog(fogIdx++, pos.x, pos.y, pos.z, 25.0f, r, g, b, 0.6f);
		}
	}

	// Begin pre-UI post-processing chain (single backbuffer copy for all effects)
	{
	LIVE_PERF_SCOPE("W3DDisplay::postProcess");
	renderer.BeginPostChain();

	renderer.ApplyVolumetricExplosions();

	// Screen-space shockwave distortion rings from explosions
	renderer.SetShockwaveEnabled(!g_debugDisableShockwave);
	renderer.ApplyShockwave();

	// Volumetric god rays from sun direction
	renderer.SetGodRaysEnabled(!g_debugDisableGodRays);
	renderer.ApplyGodRays();

	renderer.EndPostChain();
	}

	// Draw in-world 2D overlays (health bars, "Building XX%" text, icons)
	// AFTER all 3D effects including particles, so smoke doesn't occlude the text.
	if (TheTacticalView)
	{
		LIVE_PERF_SCOPE("W3DDisplay::postOverlays");
		static_cast<W3DView*>(TheTacticalView)->drawPostOverlays();
	}

	// Render the 3D-state portions of the in-game UI (click-to-move yellow
	// arrow, building placement rotation indicator). These go through
	// ModelRenderer which reads the 3D viewProjection from cbuffer b0.
	// Begin2D below replaces b0 with screen-size data, so these MUST run
	// first. W3DInGameUI::draw() (called inside Begin2D) only handles the
	// 2D portions (drag rectangle, postDraw, postWindowDraw).
	if (!g_debugDisableUI && TheInGameUI)
		static_cast<W3DInGameUI*>(TheInGameUI)->draw3DOverlays();

	// --- 2D UI RENDERING ---
	renderer.Begin2D();

	// Draw fullscreen video (cutscene/briefing) if playing
	if (isMoviePlaying()) {
		static int s_drawVidLog = 0;
		if (s_drawVidLog < 5) {
			char dbg[128];
			snprintf(dbg, sizeof(dbg), "W3DDisplay: drawing video buf=%p valid=%d\n",
				m_videoBuffer, m_videoBuffer ? m_videoBuffer->valid() : 0);
			OutputDebugStringA(dbg);
			s_drawVidLog++;
		}
		drawScaledVideoBuffer(m_videoBuffer, m_videoStream);
	}

	// Draw the window manager (renders all GUI windows)
	if (!g_debugDisableUI && TheWindowManager)
		TheWindowManager->winRepaint();

	// Draw in-game UI
	if (!g_debugDisableUI && TheInGameUI)
		TheInGameUI->draw();

	// Draw mouse cursor
	if (!g_debugDisableMouse && TheMouse)
		TheMouse->draw();

	// Draw letterbox if enabled
	if (m_letterBoxEnabled)
	{
		UnsignedInt barHeight = getHeight() / 8;
		drawFillRect(0, 0, getWidth(), barHeight, 0xFF000000);
		drawFillRect(0, getHeight() - barHeight, getWidth(), barHeight, 0xFF000000);
	}

	// Cinematic text (script-driven, set via doDisplayCinematicText). Drawn
	// on top of the letterbox bars to match the original's layering so the
	// text sits in the black-bar area during cinematics. Caches a single
	// DisplayString instead of the original's allocate-per-frame-and-leak.
	if (m_cinematicText != AsciiString::TheEmptyString && m_cinematicTextFrames != 0 && TheDisplayStringManager)
	{
		static DisplayString* s_cinematicDS = nullptr;
		static AsciiString s_lastText;
		static GameFont* s_lastFont = nullptr;
		static Int s_lastDisplayWidth = -1;

		if (!s_cinematicDS)
			s_cinematicDS = TheDisplayStringManager->newDisplayString();

		const Int displayWidth = getWidth();
		const Bool textChanged = (s_lastText != m_cinematicText);
		const Bool fontChanged = (s_lastFont != m_cinematicFont);
		const Bool widthChanged = (s_lastDisplayWidth != displayWidth);

		if (s_cinematicDS && (textChanged || fontChanged || widthChanged))
		{
			if (m_cinematicFont)
				s_cinematicDS->setFont(m_cinematicFont);
			s_cinematicDS->setWordWrap(displayWidth - 20);
			s_cinematicDS->setWordWrapCentered(TRUE);
			UnicodeString uText;
			uText.translate(m_cinematicText);
			s_cinematicDS->setText(uText);
			s_lastText = m_cinematicText;
			s_lastFont = m_cinematicFont;
			s_lastDisplayWidth = displayWidth;
		}

		if (s_cinematicDS)
		{
			const Color color = GameMakeColor(255, 255, 255, 255);
			const Color dropColor = GameMakeColor(0, 0, 0, 255);
			const Int yPos = (Int)(getHeight() * 0.9f);
			Int xPos;
			const Int textWidth = s_cinematicDS->getWidth();
			if (textWidth > displayWidth)
				xPos = 20;
			else
				xPos = (displayWidth - textWidth) / 2;
			s_cinematicDS->draw(xPos, yPos, color, dropColor);
		}

		// m_cinematicTextFrames is expressed in LOGIC frames (30 Hz), but
		// draw() runs at render rate (often 60/144/240 Hz). Original engine
		// throttled draw() to 30 Hz internally so the per-call decrement
		// matched logic frames 1:1 — here we tie the decrement to
		// TheGameLogic->getFrame() advances instead, so the text expires at
		// the intended wall-clock duration regardless of render rate.
		if (m_cinematicTextFrames > 0 && TheGameLogic)
		{
			static UnsignedInt s_lastLogicFrame = ~0u;
			const UnsignedInt curLogicFrame = TheGameLogic->getFrame();
			if (curLogicFrame != s_lastLogicFrame)
			{
				s_lastLogicFrame = curLogicFrame;
				--m_cinematicTextFrames;
			}
		}
	}

	// Draw FPS counter if debug
	if (m_debugDisplayCallback)
	{
		UnsignedInt color = 0xCC000000;
		drawFillRect(5, 5, 80, 20, color);
	}

	// Render status circle (team dot + scripted screen fades)
	// Must be last in 2D pass so screen fades overlay everything.
	if (!g_debugDisableStatusCircle) RenderStatusCircleDX11();


	renderer.End2D();

	// Movie capture: save each frame as a numbered screenshot
	if (m_movieCaptureEnabled)
	{
		char captureFilename[64];
		snprintf(captureFilename, sizeof(captureFilename), "movie_%05d.bmp", m_movieCaptureFrame++);
		renderer.CaptureScreenshot(captureFilename);
	}

	// scene_trace.log flicker diagnostic removed

	// dark_frames.log detector removed

	// Stage-3 Vulkan probe: skip the entire post-process chain. If the
	// engine's video+UI draws are producing valid pixels but post-process
	// is stomping them, this test will make the video/menu visible. Put
	// this back once the real issue is isolated.
#if 0
	// Begin post-UI post-processing chain (single backbuffer copy for all effects)
	renderer.BeginPostChain();

	// Post-processing (bloom) before presenting
	extern bool g_debugDisableBloom;
	renderer.SetBloomEnabled(!g_debugDisableBloom);
	renderer.ApplyPostProcessing();

	// Lens flare from sun direction (after bloom so flare ghosts get bloomed)
	renderer.SetLensFlareEnabled(!g_debugDisableLensFlare);
	renderer.ApplyLensFlare();

	// Cinematic post-processing (chromatic aberration + vignette + color grading)
	renderer.SetChromaEnabled(!g_debugDisableChromaAberration);
	renderer.SetVignetteEnabled(false);
	renderer.SetColorGradeEnabled(!g_debugDisableColorGrade);
	renderer.ApplyCinematic();

	// Sharpen after bloom+cinematic to recover detail
	renderer.SetSharpenEnabled(!g_debugDisableSharpen);
	renderer.ApplySharpen();

	renderer.EndPostChain();
#else
	// B&W cutscene filter: the full post-process chain above is disabled
	// while Vulkan post-processing is investigated, but the script-driven
	// FT_VIEW_BW_FILTER still needs to desaturate the scene for mission
	// briefings (USA01, GLA campaign intros). Run ApplyCinematic() on its
	// own outside the post chain so it only does the backbuffer→sceneRT
	// copy + desaturation shader when SetBwFilterEnabled(true) has been
	// called this frame (via W3DView::draw). Other cinematic flags stay
	// off, so this is a no-op outside the BW cutscene window.
#ifdef BUILD_WITH_D3D11
	renderer.SetChromaEnabled(false);
	renderer.SetVignetteEnabled(false);
	renderer.SetColorGradeEnabled(false);
	renderer.ApplyCinematic();
#endif
#endif

	// Film grain removed

	// Flush 3D debug-draw primitives (selection wireframes, gizmos,
	// world axis) ON TOP of the engine's rendered scene but BELOW
	// ImGui panels. Doing this AFTER post-processing means bloom
	// won't blur the wireframes; doing it BEFORE Inspector::Render
	// means panels can occlude debug lines that pass behind them.
	renderer.FlushDebugDraw();

	// Inspector overlay draws last so it sits on top of everything,
	// including post-process effects and 3D debug overlays.
	Inspector::Render();

	// Present the frame to the screen - actual D3D11 Present call
	renderer.EndFrame();
	s_renderedThisFrame = true; // Mark: don't re-render for this game frame

	if (s_loggedFrames < 6)
		++s_loggedFrames;

	// --- AUTOTEST MODE ---
	if (g_autotestFrames > 0)
	{
		static int s_autotestRenderedFrames = 0;
		s_autotestRenderedFrames++;

		if (s_autotestRenderedFrames >= g_autotestFrames)
		{
			// Take screenshot
			renderer.CaptureScreenshot("autotest_screenshot.bmp");

			// Exit the game
			g_autotestFrames = 0; // prevent re-entry
			AutotestQuit();
		}
	}
}

// ============================================================================
// 2D drawing primitives - all use real D3D11 rendering via the Renderer
// ============================================================================

void W3DDisplay::drawLine(Int startX, Int startY, Int endX, Int endY,
						  Real lineWidth, UnsignedInt lineColor)
{
	Render::Renderer::Instance().DrawLine(
		(float)startX, (float)startY,
		(float)endX, (float)endY,
		lineWidth, lineColor);
}

void W3DDisplay::drawLine(Int startX, Int startY, Int endX, Int endY,
						  Real lineWidth, UnsignedInt lineColor1, UnsignedInt lineColor2)
{
	// For now draw with the first color; gradient lines need custom shader work
	drawLine(startX, startY, endX, endY, lineWidth, lineColor1);
}

void W3DDisplay::drawOpenRect(Int startX, Int startY, Int width, Int height,
							  Real lineWidth, UnsignedInt lineColor)
{
	// Draw four lines forming a rectangle
	drawLine(startX, startY, startX + width, startY, lineWidth, lineColor);
	drawLine(startX + width, startY, startX + width, startY + height, lineWidth, lineColor);
	drawLine(startX + width, startY + height, startX, startY + height, lineWidth, lineColor);
	drawLine(startX, startY + height, startX, startY, lineWidth, lineColor);
}

void W3DDisplay::drawFillRect(Int startX, Int startY, Int width, Int height,
							  UnsignedInt color)
{
	Render::Renderer::Instance().DrawRect(
		(float)startX, (float)startY,
		(float)width, (float)height,
		color);
}

// 4-quadrant circular clock overlay matching the original.
// Fills clockwise from top-center using rectangles and triangles.
// Quadrants: 1=upper-right, 2=lower-right, 3=lower-left, 4=upper-left.
void W3DDisplay::drawRectClock(Int startX, Int startY, Int width, Int height,
							   Int percent, UnsignedInt color)
{
	if (percent < 1 || percent > 100) return;

	auto& renderer = Render::Renderer::Instance();
	float sx = (float)startX, sy = (float)startY;
	float w = (float)width, h = (float)height;
	float hw = w * 0.5f, hh = h * 0.5f;
	float cx = sx + hw, cy = sy + hh; // center

	if (percent == 100) { drawFillRect(startX, startY, width, height, color); return; }

	if (percent > 75)
	{
		// Full quadrants 1+2 (right half) + 3 (lower-left)
		drawFillRect(startX + width/2, startY, width - width/2, height, color);
		drawFillRect(startX, startY + height/2, width/2, height - height/2, color);
		// Partial quadrant 4 (upper-left)
		float remain = (float)(percent - 75);
		if (remain > 12) {
			renderer.DrawTri(sx, sy, sx, cy, cx, cy, color);
			float pct = (remain - 12.0f) / 13.0f;
			renderer.DrawTri(sx, sy, cx, cy, sx + hw * pct, sy, color);
		} else {
			float pct = remain / 12.0f;
			renderer.DrawTri(sx, cy - hh * pct, sx, cy, cx, cy, color);
		}
	}
	else if (percent > 50)
	{
		// Full quadrants 1+2
		drawFillRect(startX + width/2, startY, width - width/2, height, color);
		// Partial quadrant 3 (lower-left)
		float remain = (float)(percent - 50);
		if (remain > 12) {
			renderer.DrawTri(cx, cy, sx, sy + h, cx, sy + h, color);
			float pct = (remain - 12.0f) / 13.0f;
			renderer.DrawTri(sx, sy + h - hh * pct, sx, sy + h, cx, cy, color);
		} else {
			float pct = remain / 12.0f;
			renderer.DrawTri(cx, sy + h, cx, cy, cx - hw * pct, sy + h, color);
		}
	}
	else if (percent > 25)
	{
		// Full quadrant 1
		drawFillRect(startX + width/2, startY, width - width/2, height/2, color);
		// Partial quadrant 2 (lower-right)
		float remain = (float)(percent - 25);
		if (remain > 12) {
			renderer.DrawTri(cx, cy, sx + w, sy + h, sx + w, cy, color);
			float pct = (remain - 12.0f) / 13.0f;
			renderer.DrawTri(cx, cy, sx + w - hw * pct, sy + h, sx + w, sy + h, color);
		} else {
			float pct = remain / 12.0f;
			renderer.DrawTri(sx + w, cy, cx, cy, sx + w, cy + hh * pct, color);
		}
	}
	else
	{
		// Partial quadrant 1 (upper-right)
		float p = (float)percent;
		if (p > 12) {
			renderer.DrawTri(cx, sy, cx, cy, sx + w, sy, color);
			float pct = (p - 12.0f) / 13.0f;
			renderer.DrawTri(sx + w, sy, cx, cy, sx + w, sy + hh * pct, color);
		} else {
			float pct = p / 12.0f;
			renderer.DrawTri(cx, sy, cx, cy, cx + hw * pct, sy, color);
		}
	}
}

void W3DDisplay::drawRemainingRectClock(Int startX, Int startY, Int width, Int height,
										Int percent, UnsignedInt color)
{
	// Draw the unfilled/remaining dark overlay counterclockwise from 12 o'clock.
	// As percent increases 0→100, the dark area shrinks and the clear area grows
	// CLOCKWISE from 12 o'clock (matching the original game's clock-wipe effect).
	if (percent >= 100) return;
	if (percent <= 0) { drawFillRect(startX, startY, width, height, color); return; }

	Int remaining = 100 - percent;

	auto& renderer = Render::Renderer::Instance();
	float sx = (float)startX, sy = (float)startY;
	float w = (float)width, h = (float)height;
	float hw = w * 0.5f, hh = h * 0.5f;
	float cx = sx + hw, cy = sy + hh;

	if (remaining == 100) { drawFillRect(startX, startY, width, height, color); return; }

	// Counterclockwise fill order: Q4 (upper-left) → Q3 (lower-left) → Q2 (lower-right) → Q1 (upper-right)
	if (remaining > 75)
	{
		// Full Q4+Q3+Q2 + partial Q1 (upper-right)
		drawFillRect(startX, startY, width/2, height, color);                           // left half (Q4+Q3)
		drawFillRect(startX + width/2, startY + height/2, width - width/2, height - height/2, color); // Q2
		float remain = (float)(remaining - 75);
		if (remain > 12) {
			renderer.DrawTri(sx+w, cy, cx, cy, sx+w, sy, color);
			float pct = (remain - 12.0f) / 13.0f;
			renderer.DrawTri(sx+w, sy, cx, cy, sx+w - hw * pct, sy, color);
		} else {
			float pct = remain / 12.0f;
			renderer.DrawTri(sx+w, cy, cx, cy, sx+w, cy - hh * pct, color);
		}
	}
	else if (remaining > 50)
	{
		// Full Q4+Q3 + partial Q2 (lower-right)
		drawFillRect(startX, startY, width/2, height, color); // left half
		float remain = (float)(remaining - 50);
		if (remain > 12) {
			renderer.DrawTri(cx, sy+h, cx, cy, sx+w, sy+h, color);
			float pct = (remain - 12.0f) / 13.0f;
			renderer.DrawTri(sx+w, sy+h, cx, cy, sx+w, sy+h - hh * pct, color);
		} else {
			float pct = remain / 12.0f;
			renderer.DrawTri(cx, sy+h, cx, cy, cx + hw * pct, sy+h, color);
		}
	}
	else if (remaining > 25)
	{
		// Full Q4 + partial Q3 (lower-left)
		drawFillRect(startX, startY, width/2, height/2, color); // Q4
		float remain = (float)(remaining - 25);
		if (remain > 12) {
			renderer.DrawTri(sx, cy, cx, cy, sx, sy+h, color);
			float pct = (remain - 12.0f) / 13.0f;
			renderer.DrawTri(sx, sy+h, cx, cy, sx + hw * pct, sy+h, color);
		} else {
			float pct = remain / 12.0f;
			renderer.DrawTri(sx, cy, cx, cy, sx, cy + hh * pct, color);
		}
	}
	else
	{
		// Partial Q4 (upper-left) — counterclockwise from 12 o'clock toward 9 o'clock
		float p = (float)remaining;
		if (p > 12) {
			renderer.DrawTri(cx, sy, cx, cy, sx, sy, color);
			float pct = (p - 12.0f) / 13.0f;
			renderer.DrawTri(sx, sy, cx, cy, sx, sy + hh * pct, color);
		} else {
			float pct = p / 12.0f;
			renderer.DrawTri(cx, sy, cx, cy, cx - hw * pct, sy, color);
		}
	}
}

static void computeClippedImageDraw(
	Int startX, Int startY, Int endX, Int endY,
	const Region2D* imageUV, Region2D& outUV,
	Int& outClippedStartX, Int& outClippedStartY,
	Int& outClippedEndX, Int& outClippedEndY,
	bool clipEnabled, const IRegion2D& clipRegion,
	bool& outDiscard)
{
	outUV = imageUV ? *imageUV : Region2D{ { 0.0f, 0.0f }, { 1.0f, 1.0f } };
	outClippedStartX = startX;
	outClippedStartY = startY;
	outClippedEndX = endX;
	outClippedEndY = endY;
	outDiscard = false;

	if (!clipEnabled)
		return;

	if (outClippedEndX <= clipRegion.lo.x || outClippedEndY <= clipRegion.lo.y ||
		outClippedStartX >= clipRegion.hi.x || outClippedStartY >= clipRegion.hi.y)
	{
		outDiscard = true;
		return;
	}

	const Int originalWidth = endX - startX;
	const Int originalHeight = endY - startY;
	if (originalWidth <= 0 || originalHeight <= 0)
	{
		outDiscard = true;
		return;
	}

	outClippedStartX = outClippedStartX > clipRegion.lo.x ? outClippedStartX : clipRegion.lo.x;
	outClippedStartY = outClippedStartY > clipRegion.lo.y ? outClippedStartY : clipRegion.lo.y;
	outClippedEndX   = outClippedEndX   < clipRegion.hi.x ? outClippedEndX   : clipRegion.hi.x;
	outClippedEndY   = outClippedEndY   < clipRegion.hi.y ? outClippedEndY   : clipRegion.hi.y;

	const float uWidth = outUV.hi.x - outUV.lo.x;
	const float vHeight = outUV.hi.y - outUV.lo.y;
	const float leftPercent = float(outClippedStartX - startX) / float(originalWidth);
	const float rightPercent = float(outClippedEndX - startX) / float(originalWidth);
	const float topPercent = float(outClippedStartY - startY) / float(originalHeight);
	const float bottomPercent = float(outClippedEndY - startY) / float(originalHeight);

	outUV.lo.x = outUV.lo.x + (uWidth * leftPercent);
	outUV.hi.x = outUV.lo.x + (uWidth * (rightPercent - leftPercent));
	outUV.lo.y = outUV.lo.y + (vHeight * topPercent);
	outUV.hi.y = outUV.lo.y + (vHeight * (bottomPercent - topPercent));
}

void W3DDisplay::drawImageRotatedCCW90(const Image *image, Int startX, Int startY,
	Int endX, Int endY, Color color, DrawImageMode mode)
{
	if (!image)
		return;

	auto& renderer = Render::Renderer::Instance();
	AsciiString filename = image->getFilename();
	if (filename.isEmpty())
		return;

	Region2D clippedUV;
	Int clippedStartX, clippedStartY, clippedEndX, clippedEndY;
	bool discard = false;
	computeClippedImageDraw(startX, startY, endX, endY,
		image->getUV(), clippedUV,
		clippedStartX, clippedStartY, clippedEndX, clippedEndY,
		m_isClippedEnabled != 0, m_clipRegion, discard);
	if (discard)
		return;

	Render::Texture* tex = Render::ImageCache::Instance().GetTexture(
		renderer.GetDevice(), filename.str());
	if (!tex)
		return;

	if (mode == DRAW_IMAGE_GRAYSCALE)
		renderer.Set2DGrayscale(true);

	renderer.DrawImageUVRotatedCCW90(*tex,
		(float)clippedStartX, (float)clippedStartY,
		(float)(clippedEndX - clippedStartX), (float)(clippedEndY - clippedStartY),
		clippedUV.lo.x, clippedUV.lo.y, clippedUV.hi.x, clippedUV.hi.y,
		color);

	if (mode == DRAW_IMAGE_GRAYSCALE)
		renderer.Set2DGrayscale(false);
}

void W3DDisplay::drawImage(const Image *image, Int startX, Int startY,
						   Int endX, Int endY, Color color, DrawImageMode mode)
{
	if (!image)
		return;

	auto& renderer = Render::Renderer::Instance();
	AsciiString filename = image->getFilename();

	if (filename.isEmpty())
		return;

	const char *filenameStr = filename.str();

	const Region2D *imageUV = image->getUV();
	Region2D clippedUV = imageUV ? *imageUV : Region2D{ { 0.0f, 0.0f }, { 1.0f, 1.0f } };
	Int clippedStartX = startX;
	Int clippedStartY = startY;
	Int clippedEndX = endX;
	Int clippedEndY = endY;

	if (m_isClippedEnabled)
	{
		if (clippedEndX <= m_clipRegion.lo.x || clippedEndY <= m_clipRegion.lo.y ||
			clippedStartX >= m_clipRegion.hi.x || clippedStartY >= m_clipRegion.hi.y)
		{
			return;
		}

		const Int originalWidth = endX - startX;
		const Int originalHeight = endY - startY;
		if (originalWidth <= 0 || originalHeight <= 0)
			return;

		clippedStartX = clippedStartX > m_clipRegion.lo.x ? clippedStartX : m_clipRegion.lo.x;
		clippedStartY = clippedStartY > m_clipRegion.lo.y ? clippedStartY : m_clipRegion.lo.y;
		clippedEndX = clippedEndX < m_clipRegion.hi.x ? clippedEndX : m_clipRegion.hi.x;
		clippedEndY = clippedEndY < m_clipRegion.hi.y ? clippedEndY : m_clipRegion.hi.y;

		const float uWidth = clippedUV.hi.x - clippedUV.lo.x;
		const float vHeight = clippedUV.hi.y - clippedUV.lo.y;
		const float leftPercent = float(clippedStartX - startX) / float(originalWidth);
		const float rightPercent = float(clippedEndX - startX) / float(originalWidth);
		const float topPercent = float(clippedStartY - startY) / float(originalHeight);
		const float bottomPercent = float(clippedEndY - startY) / float(originalHeight);

		clippedUV.lo.x = clippedUV.lo.x + (uWidth * leftPercent);
		clippedUV.hi.x = clippedUV.lo.x + (uWidth * (rightPercent - leftPercent));
		clippedUV.lo.y = clippedUV.lo.y + (vHeight * topPercent);
		clippedUV.hi.y = clippedUV.lo.y + (vHeight * (bottomPercent - topPercent));
	}

	// Load the texture from the game's file system
	Render::Texture* tex = Render::ImageCache::Instance().GetTexture(
		renderer.GetDevice(), filenameStr);

	if (tex)
	{
		if (mode == DRAW_IMAGE_GRAYSCALE)
			renderer.Set2DGrayscale(true);

		if (imageUV)
		{
			renderer.DrawImageUV(*tex,
				(float)clippedStartX, (float)clippedStartY,
				(float)(clippedEndX - clippedStartX), (float)(clippedEndY - clippedStartY),
				clippedUV.lo.x, clippedUV.lo.y, clippedUV.hi.x, clippedUV.hi.y,
				color);
		}
		else
		{
			renderer.DrawImage(*tex,
				(float)clippedStartX, (float)clippedStartY,
				(float)(clippedEndX - clippedStartX), (float)(clippedEndY - clippedStartY),
				color);
		}

		if (mode == DRAW_IMAGE_GRAYSCALE)
			renderer.Set2DGrayscale(false);
	}
	else
	{
		// Texture not found - log and draw placeholder
		// missing_textures.log writing removed
		drawFillRect(clippedStartX, clippedStartY, clippedEndX - clippedStartX, clippedEndY - clippedStartY,
			(color & 0xFF000000) | 0x00333333);
	}
}

void W3DDisplay::drawScaledVideoBuffer(VideoBuffer *buffer, VideoStreamInterface *stream)
{
	if (!buffer || !buffer->valid())
		return;

	// Scale to fill the entire display
	drawVideoBuffer(buffer, 0, 0, getWidth(), getHeight());
}

void W3DDisplay::drawVideoBuffer(VideoBuffer *buffer, Int startX, Int startY,
								 Int endX, Int endY)
{
	if (!buffer || !buffer->valid())
		return;

	// Lock the buffer to get raw pixel data
	void* pixels = buffer->lock();
	if (!pixels)
		return;

	UnsignedInt w = buffer->width();
	UnsignedInt h = buffer->height();

	// Upload BGRA video data directly to a B8G8R8A8 texture — zero CPU conversion.
	// FFmpeg decodes to AV_PIX_FMT_BGR0 (BGRA) which matches DXGI_FORMAT_B8G8R8A8_UNORM.
	// The GPU texture sampler handles the channel mapping in hardware.
	static Render::Texture s_videoTexture;
	static uint32_t s_lastW = 0, s_lastH = 0;

	if (w != s_lastW || h != s_lastH)
	{
		s_lastW = w;
		s_lastH = h;
		s_videoTexture.CreateDynamic(Render::Renderer::Instance().GetDevice(), w, h,
			Render::PixelFormat::BGRA8_UNORM);
	}

	// Upload BGRA pixels directly — no conversion needed
	s_videoTexture.UpdateFromRGBA(Render::Renderer::Instance().GetDevice(),
		pixels, w, h);

	buffer->unlock();

	auto& renderer = Render::Renderer::Instance();
	extern bool g_debugDisableFSRVideo;
	if (!g_debugDisableFSRVideo && renderer.IsFSRReady())
	{
		renderer.DrawImageFSR(s_videoTexture,
			(float)startX, (float)startY,
			(float)(endX - startX), (float)(endY - startY));
	}
	else
	{
		renderer.DrawImage(s_videoTexture,
			(float)startX, (float)startY,
			(float)(endX - startX), (float)(endY - startY),
			0xFFFFFFFF);
	}
}

// D3D11 video buffer - holds a system-memory pixel buffer that Bink (or other
// decoders) can write into, which drawVideoBuffer then uploads to a D3D11 texture.
class D3D11VideoBuffer : public VideoBuffer
{
public:
	D3D11VideoBuffer() : VideoBuffer(TYPE_X8R8G8B8), m_data(nullptr), m_allocated(false), m_locked(false) {}

	~D3D11VideoBuffer() override
	{
		free();
	}

	Bool allocate(UnsignedInt width, UnsignedInt height) override
	{
		free();
		m_width = width;
		m_height = height;

		// Use power-of-2 texture dimensions for compatibility
		m_textureWidth = width;
		m_textureHeight = height;

		UnsignedInt bytesPerPixel = 4; // TYPE_X8R8G8B8
		m_pitch = m_textureWidth * bytesPerPixel;
		m_data = new uint8_t[m_pitch * m_textureHeight];
		memset(m_data, 0, m_pitch * m_textureHeight);
		m_allocated = true;
		return TRUE;
	}

	void free() override
	{
		delete[] m_data;
		m_data = nullptr;
		m_allocated = false;
		m_locked = false;
	}

	void* lock() override
	{
		if (!m_allocated || !m_data)
			return nullptr;
		m_locked = true;
		return m_data;
	}

	void unlock() override
	{
		m_locked = false;
	}

	Bool valid() override
	{
		return m_allocated && m_data != nullptr;
	}

private:
	uint8_t* m_data;
	bool m_allocated;
	bool m_locked;
};

VideoBuffer* W3DDisplay::createVideoBuffer()
{
	return NEW D3D11VideoBuffer;
}

void W3DDisplay::setClipRegion(IRegion2D *region)
{
	if (region)
	{
		m_clipRegion = *region;
		m_isClippedEnabled = TRUE;
	}
}

// ============================================================================
// Shroud / Fog of War
// ============================================================================

void W3DDisplay::clearShroud()
{
	Render::TerrainRenderer::Instance().ClearShroud();
}

void W3DDisplay::setShroudLevel(Int x, Int y, CellShroudStatus setting)
{
	// Shroud uses multiplicative blend: the texture value is a brightness multiplier.
	// 0 = pitch black (fully shrouded), 255 = full brightness (fully clear).
	// Read configurable values from GlobalData INI, matching the original behavior.
	uint8_t brightness;
	if (setting == CELLSHROUD_SHROUDED)
		brightness = TheGlobalData ? TheGlobalData->m_shroudAlpha : 0;
	else if (setting == CELLSHROUD_FOGGED)
		brightness = TheGlobalData ? TheGlobalData->m_fogAlpha : 127;
	else
		brightness = TheGlobalData ? TheGlobalData->m_clearAlpha : 255;

	Render::TerrainRenderer::Instance().SetShroudLevel(x, y, brightness);
}

void W3DDisplay::setBorderShroudLevel(UnsignedByte level)
{
	Render::TerrainRenderer::Instance().SetBorderShroudLevel(level);
}

// ============================================================================
// Lighting
// ============================================================================

void W3DDisplay::createLightPulse(const Coord3D *pos, const RGBColor *color,
								  Real innerRadius, Real outerRadius,
								  UnsignedInt increaseFrameTime, UnsignedInt decayFrameTime)
{
	if (!pos || !color)
		return;

	// Convert frame counts to milliseconds. The game logic runs at ~33ms per frame (30 fps logic).
	static const float MS_PER_FRAME = 33.333f;
	float increaseMs = increaseFrameTime * MS_PER_FRAME;
	float decayMs = decayFrameTime * MS_PER_FRAME;

	LightPulse pulse;
	pulse.posX = pos->x;
	pulse.posY = pos->y;
	pulse.posZ = pos->z;
	pulse.colorR = color->red;
	pulse.colorG = color->green;
	pulse.colorB = color->blue;
	pulse.innerRadius = innerRadius;
	pulse.outerRadius = outerRadius;
	pulse.intensity = 0.0f;
	pulse.increasing = true;

	// Rate = how fast intensity changes per millisecond
	// Intensity goes from 0 to 1 during increase, then 1 to 0 during decay
	pulse.increaseRate = (increaseMs > 0.0f) ? (1.0f / increaseMs) : 100.0f; // instant if zero
	pulse.decayRate = (decayMs > 0.0f) ? (1.0f / decayMs) : 100.0f;

	// Cap the number of active light pulses to prevent runaway accumulation
	if (m_lightPulses.size() < 32)
		m_lightPulses.push_back(pulse);
}

void W3DDisplay::updateLightPulses(float deltaMs)
{
	for (size_t i = 0; i < m_lightPulses.size(); )
	{
		LightPulse& lp = m_lightPulses[i];

		if (lp.increasing)
		{
			lp.intensity += lp.increaseRate * deltaMs;
			if (lp.intensity >= 1.0f)
			{
				lp.intensity = 1.0f;
				lp.increasing = false;
			}
		}
		else
		{
			lp.intensity -= lp.decayRate * deltaMs;
		}

		// Remove expired pulses
		if (!lp.increasing && lp.intensity <= 0.0f)
		{
			// Swap with last element and pop for O(1) removal
			m_lightPulses[i] = m_lightPulses.back();
			m_lightPulses.pop_back();
		}
		else
		{
			++i;
		}
	}
}

void W3DDisplay::applyLightPulsesToRenderer()
{
	auto& renderer = Render::Renderer::Instance();

	if (m_lightPulses.empty())
	{
		renderer.ClearPointLights();
		return;
	}

	// Sort by intensity (strongest first) so we send the most visible lights to the shader
	// Only need to find the top kMaxPointLights, so do a partial sort
	const uint32_t maxLights = Render::kMaxPointLights;
	uint32_t count = (uint32_t)m_lightPulses.size();
	if (count > maxLights)
	{
		// Partial sort: move the strongest lights to the front
		std::partial_sort(m_lightPulses.begin(), m_lightPulses.begin() + maxLights, m_lightPulses.end(),
			[](const LightPulse& a, const LightPulse& b) { return a.intensity > b.intensity; });
		count = maxLights;
	}

	Render::Float4 positions[Render::kMaxPointLights];
	Render::Float4 colors[Render::kMaxPointLights];

	for (uint32_t i = 0; i < count; ++i)
	{
		const LightPulse& lp = m_lightPulses[i];
		positions[i] = { lp.posX, lp.posY, lp.posZ, lp.outerRadius };
		colors[i] = { lp.colorR * lp.intensity, lp.colorG * lp.intensity, lp.colorB * lp.intensity, lp.innerRadius };
	}

	renderer.SetPointLights(positions, colors, count);
}

void W3DDisplay::setTimeOfDay(TimeOfDay tod)
{
	// Apply lighting from GlobalData's terrain lighting settings
	if (!TheGlobalData)
		return;

	auto& renderer = Render::Renderer::Instance();

	// Inspector Lights panel is editing values live — don't clobber.
	if (renderer.LightsOverridden())
		return;

	// Set ambient light from GlobalData
	const GlobalData::TerrainLighting& lighting = TheGlobalData->m_terrainLighting[tod][0];
	renderer.SetAmbientLight({
		lighting.ambient.red,
		lighting.ambient.green,
		lighting.ambient.blue,
		1.0f
	});

	// Set up directional lights from GlobalData (up to 3)
	Render::Float3 lightDirs[3];
	Render::Float4 lightColors[3];
	uint32_t lightCount = 0;

	for (int i = 0; i < MAX_GLOBAL_LIGHTS && i < 3; ++i)
	{
		const GlobalData::TerrainLighting& light = TheGlobalData->m_terrainLighting[tod][i];
		if (light.diffuse.red > 0.001f || light.diffuse.green > 0.001f || light.diffuse.blue > 0.001f)
		{
			// Normalize light direction
			float len = sqrtf(light.lightPos.x * light.lightPos.x +
							  light.lightPos.y * light.lightPos.y +
							  light.lightPos.z * light.lightPos.z);
			if (len > 0.001f)
			{
				lightDirs[lightCount] = {
					light.lightPos.x / len,
					light.lightPos.y / len,
					light.lightPos.z / len
				};
				lightColors[lightCount] = {
					light.diffuse.red,
					light.diffuse.green,
					light.diffuse.blue,
					1.0f
				};
				++lightCount;
			}
		}
	}

	if (lightCount > 0)
		renderer.SetDirectionalLights(lightDirs, lightColors, lightCount);

	// Propagate the sun direction into the shadow manager so its
	// cached sun position follows time-of-day transitions.
	if (TheW3DShadowManager)
	{
		TheW3DShadowManager->setTimeOfDay(tod);
		TheW3DShadowManager->invalidateCachedLightPositions();
	}
}

// ============================================================================
// Letterbox
// ============================================================================

void W3DDisplay::toggleLetterBox()
{
	m_letterBoxEnabled = !m_letterBoxEnabled;
	m_letterBoxFadeStartTime = timeGetTime();
}

void W3DDisplay::enableLetterBox(Bool enable)
{
	if (m_letterBoxEnabled == enable) return;
	m_letterBoxEnabled = enable;
	m_letterBoxFadeStartTime = timeGetTime();
}

Bool W3DDisplay::isLetterBoxFading()
{
	return (timeGetTime() - m_letterBoxFadeStartTime) < 1000;
}

Bool W3DDisplay::isLetterBoxed()
{
	return m_letterBoxEnabled;
}

// ============================================================================
// Screenshots - real D3D11 implementation
// ============================================================================

void W3DDisplay::takeScreenShot()
{
	static int screenshotIndex = 0;
	char filename[64];
	snprintf(filename, sizeof(filename), "screenshot_%03d.bmp", screenshotIndex++);
	if (Render::Renderer::Instance().CaptureScreenshot(filename))
		DEBUG_LOG(("Screenshot saved: %s", filename));
	else
		DEBUG_LOG(("Screenshot FAILED"));
}

void W3DDisplay::toggleMovieCapture()
{
	m_movieCaptureEnabled = !m_movieCaptureEnabled;
	if (m_movieCaptureEnabled)
	{
		m_movieCaptureFrame = 0;
		DEBUG_LOG(("Movie capture STARTED"));
	}
	else
	{
		DEBUG_LOG(("Movie capture STOPPED after %d frames", m_movieCaptureFrame));
	}
}

// FPS display is now inline in draw()

// ============================================================================
// Asset management - will be implemented with D3D11 texture/model loading
// ============================================================================

#if defined(RTS_DEBUG)
void W3DDisplay::dumpModelAssets(const char *path) {}
void W3DDisplay::dumpAssetUsage(const char* mapname) {}
#endif

void W3DDisplay::preloadModelAssets(AsciiString model)
{
	// Force the W3DAssetManager to load and cache the render object prototype
	// so it's ready when the draw module needs it (avoids hitching on first use).
	if (m_assetManager && !model.isEmpty())
		PreloadModelViaAssetManager(m_assetManager, model.str());
}

void W3DDisplay::preloadTextureAssets(AsciiString texture)
{
	// Pre-load a texture into the D3D11 image cache so it's available
	// without disk I/O when first drawn.
	if (!texture.isEmpty())
	{
		auto& device = Render::Renderer::Instance().GetDevice();
		Render::ImageCache::Instance().GetTexture(device, texture.str());
	}
}
void W3DDisplay::doSmartAssetPurgeAndPreload(const char* usageFileName) {}

// ============================================================================
// Stats
// ============================================================================

Real W3DDisplay::getAverageFPS() { return m_averageFPS; }
Real W3DDisplay::getCurrentFPS() { return m_currentFPS; }
Int W3DDisplay::getLastFrameDrawCalls() { return 0; }
