/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// FILE: W3DGameClient.cpp /////////////////////////////////////////////////
// W3D implementation of the GameClient - rewritten for D3D11

#include <stdlib.h>

#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/ModuleFactory.h"
#include "Common/GlobalData.h"
#include "Common/GameLOD.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/RayEffect.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Mouse.h"
#include "GameClient/TerrainVisual.h"
#include "GameClient/Snow.h"
#include "W3DDevice/GameClient/W3DGameClient.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "Common/ModelState.h"
#include "Common/Geometry.h"
#include "Common/MapObject.h"  // MAP_XY_FACTOR, MAP_HEIGHT_SCALE
#include "GameLogic/Object.h"  // Object class for bib methods
#include "Common/MapReaderWriterInfo.h"  // CachedFileInputStream
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/TerrainRenderer.h"
#include "W3DDevice/GameClient/W3DTerrainTracks.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

#include "GameClient/View.h"
#include "Renderer.h"
#include "W3DDevice/GameClient/ImageCache.h"
#include "WW3D2/ww3d.h"

// Win32 input - using Win32 messages, not DirectInput
#include "Win32Device/GameClient/Win32Mouse.h"

extern void ResetDX11TerrainProps();
extern void AddDX11TerrainProp(Int id, Coord3D location, Real angle, Real scale, const AsciiString &modelName);
extern void RemoveDX11TerrainPropsForConstruction(const Coord3D *pos, const GeometryInfo &geom, Real angle);
extern void AddScorchMark(float x, float y, float z, float radius, int type);
extern void ClearAllScorchMarks();
extern void ClearTerrainTrackTextures();

// Bib buffer interface (defined in D3D11Shims.cpp)
extern void D3D11BibBuffer_AddBib(Vector3 corners[4], ObjectID id, Bool highlight);
extern void D3D11BibBuffer_AddBibDrawable(Vector3 corners[4], DrawableID id, Bool highlight);
extern void D3D11BibBuffer_RemoveBib(ObjectID id);
extern void D3D11BibBuffer_RemoveBibDrawable(DrawableID id);
extern void D3D11BibBuffer_ClearAll();
extern void D3D11BibBuffer_RemoveHighlighting();

W3DGameClient::W3DGameClient()
{
}

W3DGameClient::~W3DGameClient()
{
}

void W3DGameClient::init()
{
	GameClient::init();
}

void W3DGameClient::update()
{
	GameClient::update();
}

void W3DGameClient::reset()
{
	GameClient::reset();
}

Drawable *W3DGameClient::friend_createDrawable(const ThingTemplate *tmplate,
											   DrawableStatusBits statusBits)
{
	if (tmplate == nullptr)
		return nullptr;


	return newInstance(Drawable)(tmplate, statusBits);
}

void W3DGameClient::addScorch(const Coord3D *pos, Real radius, Scorches type)
{
	if (!pos || radius <= 0.0f)
		return;

	AddScorchMark(pos->x, pos->y, pos->z, radius, (int)type);
}

void W3DGameClient::createRayEffectByTemplate(const Coord3D *start,
											   const Coord3D *end,
											   const ThingTemplate* tmpl)
{
	Drawable *draw = TheThingFactory->newDrawable(tmpl);
	if (draw)
	{
		Coord3D pos;
		pos.x = (end->x - start->x) * 0.5f + start->x;
		pos.y = (end->y - start->y) * 0.5f + start->y;
		pos.z = (end->z - start->z) * 0.5f + start->z;
		draw->setPosition(&pos);
		TheRayEffects->addRayEffect(draw, start, end);
	}
}

void W3DGameClient::setTimeOfDay(TimeOfDay tod)
{
	GameClient::setTimeOfDay(tod);
	TheDisplay->setTimeOfDay(tod);
}

// Global team color (0-255 per channel). Used by the W3DAssetManager
// Create_Render_Obj(name, scale, color) codepath when creating team-colored
// models. Stored here so draw modules can query the local player's house color.
static Int s_teamColorPacked = 0x00FF00; // default green

Int W3DGameClient_GetTeamColor() { return s_teamColorPacked; }

void W3DGameClient::setTeamColor(Int red, Int green, Int blue)
{
	// Pack into 0x00RRGGBB for use by the asset manager's recoloring system
	s_teamColorPacked = ((red & 0xFF) << 16) | ((green & 0xFF) << 8) | (blue & 0xFF);
}

void W3DGameClient::setTextureLOD(Int level)
{
	// Store texture reduction factor in GlobalData so the image cache / texture
	// loader can downsample textures on load.  Higher values = more reduction:
	// 0 = full-res, 1 = half, 2 = quarter, etc.
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_textureReductionFactor = level;
}

void W3DGameClient::notifyTerrainObjectMoved(Object *obj)
{
	if (!obj)
		return;

	// When infantry/vehicles move, remove any terrain props (trees, bushes) that
	// overlap their current position.  This matches the original W3D behavior
	// where units flatten vegetation as they walk through it.
	const Coord3D *pos = obj->getPosition();
	if (!pos)
		return;

	const GeometryInfo &geom = obj->getGeometryInfo();
	Real angle = obj->getOrientation();

	RemoveDX11TerrainPropsForConstruction(pos, geom, angle);
}

#ifdef _WIN32
// Keyboard using Win32 GetAsyncKeyState - maps VK codes to DirectInput scan codes (DIK_*)
// This replaces DirectInput keyboard without requiring the DInput library.
class Win32Keyboard : public Keyboard
{
public:
	Bool getCapsState() override { return (GetKeyState(VK_CAPITAL) & 1) != 0; }

	void init() override
	{
		Keyboard::init();
		memset(m_prevKeyState, 0, sizeof(m_prevKeyState));
		m_pendingCount = 0;
		m_pendingRead = 0;
		buildVKtoDIKMap();
	}

	void update() override
	{
		// Poll all keys and build pending event list
		m_pendingCount = 0;
		m_pendingRead = 0;

		for (int vk = 0; vk < 256; ++vk)
		{
			bool isDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
			bool wasDown = m_prevKeyState[vk] != 0;

			if (isDown != wasDown)
			{
				UnsignedByte dik = m_vkToDIK[vk];
				if (dik != 0 && m_pendingCount < MAX_PENDING)
				{
					PendingKey& pk = m_pending[m_pendingCount++];
					pk.dikCode = dik;
					pk.state = isDown ? KEY_STATE_DOWN : KEY_STATE_UP;
				}
				m_prevKeyState[vk] = isDown ? 1 : 0;
			}
		}

		Keyboard::update();
	}

	void getKey(KeyboardIO* key) override
	{
		if (m_pendingRead < m_pendingCount)
		{
			const PendingKey& pk = m_pending[m_pendingRead++];
			key->key = pk.dikCode;
			key->state = pk.state;
			key->status = KeyboardIO::STATUS_UNUSED;
			key->keyDownTimeMsec = timeGetTime();
		}
		else
		{
			key->key = KEY_NONE;
			key->state = 0;
			key->status = KeyboardIO::STATUS_UNUSED;
		}
	}

private:
	static const int MAX_PENDING = 64;

	struct PendingKey
	{
		UnsignedByte dikCode;
		UnsignedShort state;
	};

	UnsignedByte m_prevKeyState[256];
	PendingKey m_pending[MAX_PENDING];
	int m_pendingCount = 0;
	int m_pendingRead = 0;
	UnsignedByte m_vkToDIK[256];

	void buildVKtoDIKMap()
	{
		memset(m_vkToDIK, 0, sizeof(m_vkToDIK));

		// Letters A-Z
		m_vkToDIK['A'] = 0x1E; m_vkToDIK['B'] = 0x30; m_vkToDIK['C'] = 0x2E;
		m_vkToDIK['D'] = 0x20; m_vkToDIK['E'] = 0x12; m_vkToDIK['F'] = 0x21;
		m_vkToDIK['G'] = 0x22; m_vkToDIK['H'] = 0x23; m_vkToDIK['I'] = 0x17;
		m_vkToDIK['J'] = 0x24; m_vkToDIK['K'] = 0x25; m_vkToDIK['L'] = 0x26;
		m_vkToDIK['M'] = 0x32; m_vkToDIK['N'] = 0x31; m_vkToDIK['O'] = 0x18;
		m_vkToDIK['P'] = 0x19; m_vkToDIK['Q'] = 0x10; m_vkToDIK['R'] = 0x13;
		m_vkToDIK['S'] = 0x1F; m_vkToDIK['T'] = 0x14; m_vkToDIK['U'] = 0x16;
		m_vkToDIK['V'] = 0x2F; m_vkToDIK['W'] = 0x11; m_vkToDIK['X'] = 0x2D;
		m_vkToDIK['Y'] = 0x15; m_vkToDIK['Z'] = 0x2C;

		// Numbers 0-9
		m_vkToDIK['0'] = 0x0B; m_vkToDIK['1'] = 0x02; m_vkToDIK['2'] = 0x03;
		m_vkToDIK['3'] = 0x04; m_vkToDIK['4'] = 0x05; m_vkToDIK['5'] = 0x06;
		m_vkToDIK['6'] = 0x07; m_vkToDIK['7'] = 0x08; m_vkToDIK['8'] = 0x09;
		m_vkToDIK['9'] = 0x0A;

		// Function keys
		m_vkToDIK[VK_F1]  = 0x3B; m_vkToDIK[VK_F2]  = 0x3C; m_vkToDIK[VK_F3]  = 0x3D;
		m_vkToDIK[VK_F4]  = 0x3E; m_vkToDIK[VK_F5]  = 0x3F; m_vkToDIK[VK_F6]  = 0x40;
		m_vkToDIK[VK_F7]  = 0x41; m_vkToDIK[VK_F8]  = 0x42; m_vkToDIK[VK_F9]  = 0x43;
		m_vkToDIK[VK_F10] = 0x44; m_vkToDIK[VK_F11] = 0x57; m_vkToDIK[VK_F12] = 0x58;

		// Special keys
		m_vkToDIK[VK_ESCAPE]    = 0x01; // DIK_ESCAPE
		m_vkToDIK[VK_BACK]     = 0x0E; // DIK_BACK
		m_vkToDIK[VK_TAB]      = 0x0F; // DIK_TAB
		m_vkToDIK[VK_RETURN]   = 0x1C; // DIK_RETURN
		m_vkToDIK[VK_SPACE]    = 0x39; // DIK_SPACE
		m_vkToDIK[VK_LSHIFT]   = 0x2A; // DIK_LSHIFT
		m_vkToDIK[VK_RSHIFT]   = 0x36; // DIK_RSHIFT
		m_vkToDIK[VK_LCONTROL] = 0x1D; // DIK_LCONTROL
		m_vkToDIK[VK_RCONTROL] = 0x9D; // DIK_RCONTROL
		m_vkToDIK[VK_LMENU]    = 0x38; // DIK_LMENU (left alt)
		m_vkToDIK[VK_RMENU]    = 0xB8; // DIK_RMENU (right alt)
		m_vkToDIK[VK_SHIFT]    = 0x2A; // DIK_LSHIFT (generic shift)
		m_vkToDIK[VK_CONTROL]  = 0x1D; // DIK_LCONTROL (generic ctrl)
		m_vkToDIK[VK_MENU]     = 0x38; // DIK_LMENU (generic alt)

		// Arrow keys
		m_vkToDIK[VK_UP]    = 0xC8; // DIK_UP
		m_vkToDIK[VK_DOWN]  = 0xD0; // DIK_DOWN
		m_vkToDIK[VK_LEFT]  = 0xCB; // DIK_LEFT
		m_vkToDIK[VK_RIGHT] = 0xCD; // DIK_RIGHT

		// Navigation keys
		m_vkToDIK[VK_INSERT]  = 0xD2; // DIK_INSERT
		m_vkToDIK[VK_DELETE]  = 0xD3; // DIK_DELETE
		m_vkToDIK[VK_HOME]    = 0xC7; // DIK_HOME
		m_vkToDIK[VK_END]     = 0xCF; // DIK_END
		m_vkToDIK[VK_PRIOR]   = 0xC9; // DIK_PRIOR (page up)
		m_vkToDIK[VK_NEXT]    = 0xD1; // DIK_NEXT (page down)

		// Numpad
		m_vkToDIK[VK_NUMPAD0] = 0x52; m_vkToDIK[VK_NUMPAD1] = 0x4F;
		m_vkToDIK[VK_NUMPAD2] = 0x50; m_vkToDIK[VK_NUMPAD3] = 0x51;
		m_vkToDIK[VK_NUMPAD4] = 0x4B; m_vkToDIK[VK_NUMPAD5] = 0x4C;
		m_vkToDIK[VK_NUMPAD6] = 0x4D; m_vkToDIK[VK_NUMPAD7] = 0x47;
		m_vkToDIK[VK_NUMPAD8] = 0x48; m_vkToDIK[VK_NUMPAD9] = 0x49;
		m_vkToDIK[VK_MULTIPLY] = 0x37;  // DIK_NUMPADSTAR
		m_vkToDIK[VK_ADD]      = 0x4E;  // DIK_NUMPADPLUS
		m_vkToDIK[VK_SUBTRACT] = 0x4A;  // DIK_NUMPADMINUS
		m_vkToDIK[VK_DECIMAL]  = 0x53;  // DIK_NUMPADPERIOD

		// Punctuation
		m_vkToDIK[VK_OEM_1]      = 0x27; // DIK_SEMICOLON (;:)
		m_vkToDIK[VK_OEM_PLUS]   = 0x0D; // DIK_EQUALS (=+)
		m_vkToDIK[VK_OEM_COMMA]  = 0x33; // DIK_COMMA (,<)
		m_vkToDIK[VK_OEM_MINUS]  = 0x0C; // DIK_MINUS (-_)
		m_vkToDIK[VK_OEM_PERIOD] = 0x34; // DIK_PERIOD (.>)
		m_vkToDIK[VK_OEM_2]      = 0x35; // DIK_SLASH (/?)
		m_vkToDIK[VK_OEM_3]      = 0x29; // DIK_GRAVE (`~)
		m_vkToDIK[VK_OEM_4]      = 0x1A; // DIK_LBRACKET ([{)
		m_vkToDIK[VK_OEM_5]      = 0x2B; // DIK_BACKSLASH (\|)
		m_vkToDIK[VK_OEM_6]      = 0x1B; // DIK_RBRACKET (]})
		m_vkToDIK[VK_OEM_7]      = 0x28; // DIK_APOSTROPHE ('")
	}
};
#endif // _WIN32

#ifdef USE_SDL
#include "SDLMouse.h"
#include "SDLKeyboard.h"
#endif

Keyboard *W3DGameClient::createKeyboard()
{
#ifdef USE_SDL
	return NEW SDLKeyboard;
#elif defined(_WIN32)
	return NEW Win32Keyboard;
#else
	return nullptr; // No keyboard on this platform without SDL
#endif
}

Mouse *W3DGameClient::createMouse()
{
#ifdef USE_SDL
	SDLMouse* mouse = NEW SDLMouse;
	// TheWin32Mouse is only used by the Win32 WndProc path.
	// With SDL, mouse events flow through SDLPlatform -> SDLMouse directly.
	TheWin32Mouse = nullptr;
	return mouse;
#else
	Win32Mouse* mouse = NEW Win32Mouse;
	TheWin32Mouse = mouse;
	return mouse;
#endif
}

// D3D11 TerrainVisual - loads heightmap data and provides terrain queries
// Visual terrain rendering is handled separately by TerrainRenderer
class D3D11TerrainVisual : public TerrainVisual
{
	WorldHeightMap* m_heightMap;
	Int m_mapWidth;   // grid cells in X
	Int m_mapHeight;  // grid cells in Y
	Int m_borderSize;
	Real m_minZ;
	Real m_maxZ;

public:
	D3D11TerrainVisual() : m_heightMap(nullptr), m_mapWidth(0), m_mapHeight(0),
		m_borderSize(0), m_minZ(0), m_maxZ(0) {}

	~D3D11TerrainVisual() override {
		ResetDX11TerrainProps();
		ClearAllScorchMarks();
		ClearTerrainTrackTextures();
		D3D11BibBuffer_ClearAll();
		delete TheTerrainTracksRenderObjClassSystem;
		TheTerrainTracksRenderObjClassSystem = nullptr;
		REF_PTR_RELEASE(m_heightMap);
	}

	void reset() override
	{
		ResetDX11TerrainProps();
		ClearAllScorchMarks();
		ClearTerrainTrackTextures();
		D3D11BibBuffer_ClearAll();
		if (TheTerrainTracksRenderObjClassSystem)
			TheTerrainTracksRenderObjClassSystem->Reset();
		Render::TerrainRenderer::Instance().Shutdown();
		REF_PTR_RELEASE(m_heightMap);
		m_mapWidth = m_mapHeight = m_borderSize = 0;
		m_minZ = m_maxZ = 0;
		TerrainVisual::reset();
	}

	Bool load(AsciiString filename) override
	{

		if (!TerrainVisual::load(filename))
			return FALSE;

		ResetDX11TerrainProps();
		Render::TerrainRenderer::Instance().Shutdown();

		// Release any previous heightmap
		REF_PTR_RELEASE(m_heightMap);

		CachedFileInputStream fileStrm;
		if (!fileStrm.open(filename)) {
			return FALSE;
		}

		ChunkInputStream* pStrm = &fileStrm;
		// The DX11 terrain renderer needs the full visual map data so it can
		// access source tiles, texture classes, and per-cell UV assignments.
		m_heightMap = NEW WorldHeightMap(pStrm, false);
		if (!m_heightMap)
			return FALSE;

		m_mapWidth = m_heightMap->getXExtent();
		m_mapHeight = m_heightMap->getYExtent();
		m_borderSize = m_heightMap->getBorderSize();

		// Compute Z range from heightmap data
		Int minHt = WorldHeightMap::getMaxHeightValue();
		Int maxHt = 0;
		for (Int j = 0; j < m_mapHeight; j++) {
			for (Int i = 0; i < m_mapWidth; i++) {
				UnsignedByte h = m_heightMap->getHeight(i, j);
				if (h < minHt) minHt = h;
				if (h > maxHt) maxHt = h;
			}
		}
		m_minZ = minHt * MAP_HEIGHT_SCALE;
		m_maxZ = maxHt * MAP_HEIGHT_SCALE;

		// Initialize terrain tracks system for vehicle tread marks
		if (!TheGlobalData->m_headless)
		{
			if (!TheTerrainTracksRenderObjClassSystem)
			{
				TheTerrainTracksRenderObjClassSystem = NEW TerrainTracksRenderObjClassSystem;
				TheTerrainTracksRenderObjClassSystem->init(nullptr);
			}
		}

		return TRUE;
	}

	WorldHeightMap* getLogicHeightMap() override { return m_heightMap; }
	WorldHeightMap* getClientHeightMap() override { return m_heightMap; }

	void getTerrainColorAt(Real x, Real y, RGBColor* c) override {
		if (!c) return;
		if (m_heightMap) { m_heightMap->getTerrainColorAt(x, y, c); return; }
		c->red=0.4f; c->green=0.5f; c->blue=0.3f;
	}
	TerrainType* getTerrainTile(Real, Real) override { return nullptr; }
	void enableWaterGrid(Bool) override {}
	void setWaterGridHeightClamps(const WaterHandle*, Real, Real) override {}
	void setWaterAttenuationFactors(const WaterHandle*, Real, Real, Real, Real) override {}
	void setWaterTransform(const WaterHandle*, Real, Real, Real, Real) override {}
	void setWaterTransform(const Matrix3D*) override {}
	void getWaterTransform(const WaterHandle*, Matrix3D*) override {}
	void setWaterGridResolution(const WaterHandle*, Real, Real, Real) override {}
	void getWaterGridResolution(const WaterHandle*, Real*, Real*, Real*) override {}
	void changeWaterHeight(Real, Real, Real) override {}
	void addWaterVelocity(Real, Real, Real, Real) override {}
	Bool getWaterGridHeight(Real, Real, Real* h) override { if(h)*h=0; return FALSE; }
	void addFactionBib(Object* factionBuilding, Bool highlight, Real extra) override
	{
		if (!m_heightMap || !factionBuilding) return;
		const Matrix3D* mtx = factionBuilding->getDrawable() ? factionBuilding->getDrawable()->getTransformMatrix() : nullptr;
		if (!mtx) return;
		Vector3 corners[4];
		Coord3D pos;
		pos.set(0, 0, 0);
		Real exitWidth = factionBuilding->getTemplate()->getFactoryExitWidth();
		Real extraWidth = factionBuilding->getTemplate()->getFactoryExtraBibWidth() + extra;
		const GeometryInfo info = factionBuilding->getTemplate()->getTemplateGeometryInfo();
		Real sizeX = info.getMajorRadius();
		Real sizeY = info.getMinorRadius();
		if (info.getGeomType() != GEOMETRY_BOX) {
			sizeY = sizeX;
		}
		corners[0].Set(pos.x - sizeX - extraWidth, pos.y - sizeY - extraWidth, pos.z);
		corners[1].Set(pos.x + sizeX + exitWidth + extraWidth, pos.y - sizeY - extraWidth, pos.z);
		corners[2].Set(pos.x + sizeX + exitWidth + extraWidth, pos.y + sizeY + extraWidth, pos.z);
		corners[3].Set(pos.x - sizeX - extraWidth, pos.y + sizeY + extraWidth, pos.z);
		mtx->Transform_Vector(*mtx, corners[0], &corners[0]);
		mtx->Transform_Vector(*mtx, corners[1], &corners[1]);
		mtx->Transform_Vector(*mtx, corners[2], &corners[2]);
		mtx->Transform_Vector(*mtx, corners[3], &corners[3]);
		D3D11BibBuffer_AddBib(corners, factionBuilding->getID(), highlight);
	}
	void addFactionBibDrawable(Drawable* factionBuilding, Bool highlight, Real extra) override
	{
		if (!m_heightMap || !factionBuilding) return;
		const Matrix3D* mtx = factionBuilding->getTransformMatrix();
		if (!mtx) return;
		Vector3 corners[4];
		Coord3D pos;
		pos.set(0, 0, 0);
		Real exitWidth = factionBuilding->getTemplate()->getFactoryExitWidth();
		Real extraWidth = factionBuilding->getTemplate()->getFactoryExtraBibWidth() + extra;
		const GeometryInfo info = factionBuilding->getTemplate()->getTemplateGeometryInfo();
		Real sizeX = info.getMajorRadius();
		Real sizeY = info.getMinorRadius();
		if (info.getGeomType() != GEOMETRY_BOX) {
			sizeY = sizeX;
		}
		corners[0].Set(pos.x - sizeX - extraWidth, pos.y - sizeY - extraWidth, pos.z);
		corners[1].Set(pos.x + sizeX + exitWidth + extraWidth, pos.y - sizeY - extraWidth, pos.z);
		corners[2].Set(pos.x + sizeX + exitWidth + extraWidth, pos.y + sizeY + extraWidth, pos.z);
		corners[3].Set(pos.x - sizeX - extraWidth, pos.y + sizeY + extraWidth, pos.z);
		mtx->Transform_Vector(*mtx, corners[0], &corners[0]);
		mtx->Transform_Vector(*mtx, corners[1], &corners[1]);
		mtx->Transform_Vector(*mtx, corners[2], &corners[2]);
		mtx->Transform_Vector(*mtx, corners[3], &corners[3]);
		D3D11BibBuffer_AddBibDrawable(corners, factionBuilding->getID(), highlight);
	}
	void removeFactionBib(Object* factionBuilding) override
	{
		if (factionBuilding)
			D3D11BibBuffer_RemoveBib(factionBuilding->getID());
	}
	void removeFactionBibDrawable(Drawable* factionBuilding) override
	{
		if (factionBuilding)
			D3D11BibBuffer_RemoveBibDrawable(factionBuilding->getID());
	}
	void removeAllBibs() override
	{
		D3D11BibBuffer_ClearAll();
	}
	void addProp(const ThingTemplate *tTemplate, const Coord3D *pos, Real angle) override
	{
		if (tTemplate == nullptr || pos == nullptr) {
			return;
		}

		ModelConditionFlags state;
		state.clear();
		if (TheGlobalData->m_weather == WEATHER_SNOWY) {
			state.set(MODELCONDITION_SNOW);
		}
		if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT) {
			state.set(MODELCONDITION_NIGHT);
		}

		AsciiString modelName;
		const Real scale = tTemplate->getAssetScale();
		const ModuleInfo &moduleInfo = tTemplate->getDrawModuleInfo();
		if (moduleInfo.getCount() > 0) {
			const ModuleData *moduleData = moduleInfo.getNthData(0);
			const W3DModelDrawModuleData *modelDrawData = moduleData ? moduleData->getAsW3DModelDrawModuleData() : nullptr;
			if (modelDrawData != nullptr) {
				modelName = modelDrawData->getBestModelNameForWB(state);
			}
		}

		if (modelName.isNotEmpty()) {
			AddDX11TerrainProp(1, *pos, angle, scale, modelName);
		}
	}

	void setRawMapHeight(const ICoord2D* pos, Int height) override
	{
		if (m_heightMap && pos)
		{
			// Grid positions are in playable-area coordinates (origin at top-left of
			// the playable zone).  The heightmap data array includes the border, so
			// we must add borderSize to convert to array indices.  The original
			// W3DTerrainVisual::setRawMapHeight does the same offset.
			Int x = pos->x + m_borderSize;
			Int y = pos->y + m_borderSize;
			// Only lower, never raise — matches original behaviour (prevents
			// scissoring artefacts with roads).
			if (x >= 0 && x < m_mapWidth && y >= 0 && y < m_mapHeight)
			{
				UnsignedByte cur = m_heightMap->getHeight(x, y);
				if ((Int)cur > height)
				{
					m_heightMap->getDataPtr()[y * m_mapWidth + x] = (UnsignedByte)height;
					Render::TerrainRenderer::Instance().Invalidate();
				}
			}
		}
	}

	Int getRawMapHeight(const ICoord2D* pos) override
	{
		if (m_heightMap && pos)
		{
			Int x = pos->x + m_borderSize;
			Int y = pos->y + m_borderSize;
			return m_heightMap->getHeight(x, y);
		}
		return 0;
	}

	void getExtent(Region3D* r) const
	{
		if (!r) return;
		if (m_heightMap) {
			Real playableW = (m_mapWidth - 2 * m_borderSize) * MAP_XY_FACTOR;
			Real playableH = (m_mapHeight - 2 * m_borderSize) * MAP_XY_FACTOR;
			r->lo.x = m_borderSize * MAP_XY_FACTOR;
			r->lo.y = m_borderSize * MAP_XY_FACTOR;
			r->lo.z = m_minZ;
			r->hi.x = r->lo.x + playableW;
			r->hi.y = r->lo.y + playableH;
			r->hi.z = m_maxZ;
		} else {
			r->lo.x = r->lo.y = r->lo.z = 0;
			r->hi.x = r->hi.y = 1000;
			r->hi.z = 100;
		}
	}

	void getMaximumPathfindExtent(Region3D* r) const { getExtent(r); }

	void getExtentIncludingBorder(Region3D* r) const
	{
		if (!r) return;
		if (m_heightMap) {
			r->lo.x = 0;
			r->lo.y = 0;
			r->lo.z = m_minZ;
			r->hi.x = m_mapWidth * MAP_XY_FACTOR;
			r->hi.y = m_mapHeight * MAP_XY_FACTOR;
			r->hi.z = m_maxZ;
		} else {
			getExtent(r);
		}
	}

	void replaceSkyboxTextures(const AsciiString* oldTex[5], const AsciiString* newTex[5]) override {}
	void setTerrainTracksDetail() override {
		if (TheTerrainTracksRenderObjClassSystem)
			TheTerrainTracksRenderObjClassSystem->setDetail();
	}
	void setShoreLineDetail() override {}
	void removeBibHighlighting() override
	{
		D3D11BibBuffer_RemoveHighlighting();
	}
	void removeTreesAndPropsForConstruction(const Coord3D *pos, const GeometryInfo &geom, Real angle) override
	{
		RemoveDX11TerrainPropsForConstruction(pos, geom, angle);
	}
#ifdef DO_SEISMIC_SIMULATIONS
	void updateSeismicSimulations() override {}
	void addSeismicSimulation(const SeismicSimulationNode&) override {}
#endif
};

TerrainVisual *W3DGameClient::createTerrainVisual()
{
	return NEW D3D11TerrainVisual;
}

// ============================================================================
// D3D11 Snow Manager - renders falling snow particles as camera-facing quads
// ============================================================================

#define SNOW_MAX_PARTICLES 4096
#define SNOW_NOISE_SIZE 64

// Fast sine approximation matching the original W3D WWMath::Fast_Sin
static inline float FastSin(float x)
{
	return sinf(x);
}

class D3D11SnowManager : public SnowManager
{
public:
	D3D11SnowManager()
		: m_snowTexture(nullptr)
	{
	}

	~D3D11SnowManager() override
	{
		m_snowTexture = nullptr;
	}

	void init() override
	{
		SnowManager::init();
	}

	void reset() override
	{
		SnowManager::reset();
		m_snowTexture = nullptr;
	}

	void update() override
	{
		// Advance animation time, matching original W3DSnowManager::update
		m_time += WW3D::Get_Logic_Frame_Time_Seconds();
		m_time = fmod(m_time, m_fullTimePeriod);
	}

	void updateIniSettings() override
	{
		SnowManager::updateIniSettings();
		m_snowTexture = nullptr; // force reload
	}

	void render()
	{
		if (!TheWeatherSetting->m_snowEnabled || !m_isVisible)
			return;

		if (!TheTacticalView)
			return;

		auto& renderer = Render::Renderer::Instance();
		auto& device = renderer.GetDevice();

		// Load the snow texture on first use
		if (!m_snowTexture)
		{
			m_snowTexture = Render::ImageCache::Instance().GetTexture(
				device, TheWeatherSetting->m_snowTexture.str());
			if (!m_snowTexture)
				return;
		}

		// Upload noise table to GPU on first use
		if (!m_noiseBufferReady)
		{
			m_noiseBuffer.Create(device, sizeof(float), SNOW_NOISE_SIZE * SNOW_NOISE_SIZE, m_startingHeights);
			m_noiseBufferReady = true;
		}

		// Camera vectors
		const Coord3D &cPos = TheTacticalView->get3DCameraPosition();
		Coord3D lookAt;
		TheTacticalView->getPosition(&lookAt);

		float fwdX = lookAt.x - cPos.x, fwdY = lookAt.y - cPos.y, fwdZ = lookAt.z - cPos.z;
		float fwdLen = sqrtf(fwdX * fwdX + fwdY * fwdY + fwdZ * fwdZ);
		if (fwdLen < 0.001f) return;
		fwdX /= fwdLen; fwdY /= fwdLen; fwdZ /= fwdLen;

		float rX = fwdY * 1.0f - fwdZ * 0.0f;
		float rY = fwdZ * 0.0f - fwdX * 1.0f;
		float rZ = fwdX * 0.0f - fwdY * 0.0f;
		float rLen = sqrtf(rX * rX + rY * rY + rZ * rZ);
		if (rLen < 0.001f) { rX = 1.0f; rLen = 1.0f; }
		rX /= rLen; rY /= rLen; rZ /= rLen;

		float uX = rY * fwdZ - rZ * fwdY;
		float uY = rZ * fwdX - rX * fwdZ;
		float uZ = rX * fwdY - rY * fwdX;

		// Compute grid
		int numEmittersInHalf = (int)floor(m_boxDimensions / m_emitterSpacing * 0.5f);
		int cubeCenterX = (int)floor(cPos.x / m_emitterSpacing);
		int cubeCenterY = (int)floor(cPos.y / m_emitterSpacing);
		int cubeOriginX = cubeCenterX - numEmittersInHalf;
		int cubeOriginY = cubeCenterY - numEmittersInHalf;
		int gridWidth = numEmittersInHalf * 2;
		if (gridWidth <= 0) return;

		uint32_t instanceCount = (uint32_t)(gridWidth * gridWidth);
		if (instanceCount > SNOW_MAX_PARTICLES)
			instanceCount = SNOW_MAX_PARTICLES;

		float snowCeiling = cPos.z + m_boxDimensions / 2.0f;
		float cameraOffset = fmod(cPos.z, m_boxDimensions);
		float heightTraveled = m_time * m_velocity + cameraOffset;

		// Fill constant buffer
		Render::Renderer::SnowConstants params = {};
		params.snowGrid = { m_emitterSpacing, m_quadSize * 0.5f, snowCeiling, heightTraveled };
		params.snowAnim = { m_amplitude, m_frequencyScaleX, m_frequencyScaleY, m_boxDimensions };
		params.snowOrigin = { cubeOriginX, cubeOriginY, gridWidth, 0 };
		params.snowCamRight = { rX, rY, rZ, 0.0f };
		params.snowCamUp = { uX, uY, uZ, 0.0f };
		params.snowCamFwd = { fwdX, fwdY, fwdZ, m_quadSize };

		renderer.DrawSnowInstanced(instanceCount, m_snowTexture, m_noiseBuffer, params);
		renderer.Restore3DState();
	}

private:
	Render::Texture* m_snowTexture;
	Render::GPUBuffer m_noiseBuffer;
	bool m_noiseBufferReady = false;
};

// Global function that W3DDisplay can call to render snow
void RenderSnowDX11()
{
	if (TheSnowManager)
	{
		static_cast<D3D11SnowManager*>(TheSnowManager)->render();
	}
}

SnowManager *W3DGameClient::createSnowManager()
{
	return NEW D3D11SnowManager;
}

void W3DGameClient::setFrameRate(Real msecsPerFrame)
{
	// Frame rate is managed by the Renderer's vsync
}
