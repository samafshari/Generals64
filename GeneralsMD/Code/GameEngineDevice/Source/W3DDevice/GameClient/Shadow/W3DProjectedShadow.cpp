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

// DX11 port of W3DProjectedShadowManager — SHADOW_DECAL / SHADOW_ALPHA_DECAL /
// SHADOW_ADDITIVE_DECAL plus SHADOW_PROJECTION fall-through. Mirrors the
// original's queueDecal / flushDecals flow, but builds a CPU-side vertex
// batch and hands it to Render::Renderer's existing unlit-decal pipeline
// (SetDecalMultiplicative/Alpha/Additive3DState + Draw3DIndexed) instead of
// re-implementing the fixed-function blend + FVF paths.

#include "always.h"
#include "W3DDevice/GameClient/W3DProjectedShadow.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "Common/GlobalData.h"
#include "Common/Debug.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableInfo.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"
#include "Common/MapObject.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/rendobj.h"
#include "vector3.h"
#include "aabox.h"
#include "matrix3d.h"

#ifdef BUILD_WITH_D3D11
#include "Renderer.h"
#include "Core/Buffer.h"
#include "Core/Device.h"
#include "Core/Texture.h"
#include "W3DDevice/GameClient/ImageCache.h"
#endif

// Heightmap sampling — used by queueDecal/queueSimpleDecal. BaseHeightMap.h
// pulls in WorldHeightMap.h transitively.
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"

// DX11: TheTerrainRenderObject is never populated in this build (the .cpp
// that sets it isn't compiled). The live path is GetTerrainHeightMap() —
// it prefers TheTerrainRenderObject->getMap() and falls back to the
// D3D11TerrainVisual's logic heightmap. Defined in D3D11Shims.cpp.
extern WorldHeightMap* GetTerrainHeightMap();

// Forward singleton — declared in shadow.h, owned by W3DShadow.cpp, pointed
// at our instance by the umbrella manager.
W3DProjectedShadowManager* TheW3DProjectedShadowManager = nullptr;

#define DEFAULT_RENDER_TARGET_WIDTH 512
#define DEFAULT_RENDER_TARGET_HEIGHT 512

// Bridge offset used by queueDecal when a caster object is on a bridge so
// its shadow doesn't drop through the span onto the terrain below.
#define BRIDGE_OFFSET_FACTOR 1.5f

// Bounding rectangle around rendered portion of terrain. Set by renderShadows
// before any queueDecal call so each decal's footprint is clipped to what the
// terrain renderer will actually draw this frame.
static Int s_drawEdgeX = 0;
static Int s_drawEdgeY = 0;
static Int s_drawStartX = 0;
static Int s_drawStartY = 0;

// -----------------------------------------------------------------------------
// W3DShadowTexture — thin wrapper around a Render::Texture* loaded from the
// image cache. The original held a refcounted TextureClass plus per-caster
// cached state (last light position, last orientation, effective uv axes,
// local bounds); since SHADOW_DECAL doesn't need any of that, and SHADOW_
// PROJECTION dynamic texture generation is stubbed for this port, we store
// just the pointer + name + the minimal fields we need.
// -----------------------------------------------------------------------------

class W3DShadowTexture
{
public:
	W3DShadowTexture(void)
		: m_texture(nullptr)
		, m_refCount(1)
	{
		m_lastLightPosition.Set(0.0f, 0.0f, 0.0f);
		m_shadowUV[0].Set(1.0f, 0.0f, 0.0f);		// u runs along world +X
		m_shadowUV[1].Set(0.0f, -1.0f, 0.0f);	// v runs along world -Y
		m_areaEffectBox.Center.Set(0, 0, 0);
		m_areaEffectBox.Extent.Set(0, 0, 0);
		m_name[0] = '\0';
	}

	~W3DShadowTexture(void) {}

	void Set_Name(const char* name)
	{
		size_t len = strlen(name);
		if (len >= sizeof(m_name))
			len = sizeof(m_name) - 1;
		memcpy(m_name, name, len);
		m_name[len] = '\0';
	}

	const char* Get_Name(void) const { return m_name; }

#ifdef BUILD_WITH_D3D11
	Render::Texture*	getTexture(void) { return m_texture; }
	void					setTexture(Render::Texture* t) { m_texture = t; }
#endif

	void		setLightPosHistory(const Vector3& pos) { m_lastLightPosition = pos; }
	Vector3&	getLightPosHistory(void) { return m_lastLightPosition; }

	AABoxClass&	getBoundingBox(void) { return m_areaEffectBox; }
	void		setBoundingBox(const AABoxClass& b) { m_areaEffectBox = b; }

	// ref count not integrated with WW's RefCountClass (the original used
	// MSGNEW/SET_REF_OWNER); here it's just bookkeeping for the manager to
	// know when to purge on freeAllTextures.
	void	addRef(void) { ++m_refCount; }
	Int		releaseRef(void) { return --m_refCount; }

private:
	char						m_name[128];
#ifdef BUILD_WITH_D3D11
	Render::Texture*	m_texture;
#else
	void*							m_texture;
#endif
	Vector3					m_lastLightPosition;
	Vector3					m_shadowUV[2];
	AABoxClass			m_areaEffectBox;
	Int							m_refCount;
};

// -----------------------------------------------------------------------------
// W3DShadowTextureManager — string-keyed cache of shadow textures. Matches
// the original's HashTableClass-based lookup but backed by std::unordered_map
// now that the WW3D hash utility is gone.
// -----------------------------------------------------------------------------

class W3DShadowTextureManager
{
public:
	W3DShadowTextureManager(void) {}
	~W3DShadowTextureManager(void) { freeAllTextures(); }

	W3DShadowTexture*	peekTexture(const char* name)
	{
		auto it = m_textures.find(name);
		return (it == m_textures.end()) ? nullptr : it->second;
	}

	W3DShadowTexture*	getTexture(const char* name)
	{
		W3DShadowTexture* t = peekTexture(name);
		if (t)
			t->addRef();
		return t;
	}

	Bool addTexture(W3DShadowTexture* newTexture)
	{
		if (!newTexture)
			return FALSE;
		newTexture->addRef();	// one owning ref for the cache
		m_textures[newTexture->Get_Name()] = newTexture;
		return TRUE;
	}

	void freeAllTextures(void)
	{
		// On reset / shutdown, every shadow that held a texture is gone, so
		// there shouldn't be any outstanding refs. Free every entry.
		for (auto& kv : m_textures)
			delete kv.second;
		m_textures.clear();
	}

	void invalidateCachedLightPositions(void)
	{
		static Vector3 zero(0.0f, 0.0f, 0.0f);
		for (auto& kv : m_textures)
		{
			if (kv.second)
				kv.second->setLightPosHistory(zero);
		}
	}

private:
	std::unordered_map<std::string, W3DShadowTexture*> m_textures;
};

// =============================================================================
// DX11 decal batch state. The original drove rendering through a persistent
// D3D8 dynamic VB/IB with a BRUTE_FORCE dword FVF; the DX11 port builds a
// CPU-side Vertex3D / uint16 index batch and uploads it on flushDecals into
// a dynamic Render::VertexBuffer / Render::IndexBuffer. This avoids the
// Map(WRITE_DISCARD/NO_OVERWRITE) bookkeeping but matches the semantics
// (one draw per contiguous run of decals using the same texture + blend).
// =============================================================================

#ifdef BUILD_WITH_D3D11
namespace {

// Target sizes — matches the original 32768-vertex / 65536-index budget so
// worst-case full-map decals fit in one batch.
constexpr uint32_t kShadowDecalVertexCap = 32768;
constexpr uint32_t kShadowDecalIndexCap  = 65536;

struct DecalBatch
{
	std::vector<Render::Vertex3D>	verts;
	std::vector<uint16_t>				indices;
	Render::VertexBuffer					vb;
	Render::IndexBuffer						ib;
	bool											buffersCreated = false;

	void reset(void)
	{
		verts.clear();
		indices.clear();
	}
};

static DecalBatch s_decalBatch;

// Lazily stand up the dynamic VB / IB at their cap — done once per device.
static bool EnsureDecalBuffers(Render::Device& device)
{
	if (s_decalBatch.buffersCreated)
		return true;
	s_decalBatch.verts.reserve(kShadowDecalVertexCap);
	s_decalBatch.indices.reserve(kShadowDecalIndexCap);
	if (!s_decalBatch.vb.Create(device, nullptr, kShadowDecalVertexCap, sizeof(Render::Vertex3D), /*dynamic*/true))
		return false;
	if (!s_decalBatch.ib.Create(device, nullptr, kShadowDecalIndexCap, /*dynamic*/true))
		return false;
	s_decalBatch.buffersCreated = true;
	return true;
}

// Pick the right Renderer decal-state helper for the shadow type.
static void ApplyDecalState(Render::Renderer& renderer, ShadowType type)
{
	switch (type)
	{
	case SHADOW_DECAL:
		renderer.SetDecalMultiplicative3DState();
		break;
	case SHADOW_ALPHA_DECAL:
		renderer.SetDecalAlphaBlend3DState();
		break;
	case SHADOW_ADDITIVE_DECAL:
		renderer.SetDecalAdditive3DState();
		break;
	default:
		// Projection / directional projection fall back to mul-blend so they
		// still darken terrain even if the dynamic texture generator isn't
		// running. See TODO in renderShadows.
		renderer.SetDecalMultiplicative3DState();
		break;
	}
}

} // namespace
#endif // BUILD_WITH_D3D11

// =============================================================================
// W3DProjectedShadowManager
// =============================================================================

W3DProjectedShadowManager::W3DProjectedShadowManager(void)
	: m_shadowList(nullptr)
	, m_decalList(nullptr)
	, m_dynamicRenderTarget(nullptr)
	, m_renderTargetHasAlpha(FALSE)
	, m_W3DShadowTextureManager(nullptr)
	, m_numDecalShadows(0)
	, m_numProjectionShadows(0)
{
}

W3DProjectedShadowManager::~W3DProjectedShadowManager(void)
{
	// Drain any still-live shadows before the texture cache goes — each
	// shadow dtor touches its texture's ref counter.
	removeAllShadows();
	ReleaseResources();
	delete m_W3DShadowTextureManager;
	m_W3DShadowTextureManager = nullptr;

	DEBUG_ASSERTCRASH(m_shadowList == nullptr, ("Destroy of non-empty projected shadow list"));
	DEBUG_ASSERTCRASH(m_decalList == nullptr, ("Destroy of non-empty projected decal list"));
}

Bool W3DProjectedShadowManager::init(void)
{
	if (m_W3DShadowTextureManager == nullptr)
		m_W3DShadowTextureManager = NEW W3DShadowTextureManager;
	return TRUE;
}

void W3DProjectedShadowManager::reset(void)
{
	// Caller (W3DShadowManager::Reset) is supposed to have drained the
	// shadow lists via removeAllShadows first; these asserts guard the
	// invariant. The texture cache always clears so the next map can
	// load its own shadow decals from scratch.
	DEBUG_ASSERTCRASH(m_shadowList == nullptr, ("Reset of non-empty projected shadow list"));
	DEBUG_ASSERTCRASH(m_decalList == nullptr, ("Reset of non-empty projected decal list"));

	if (m_W3DShadowTextureManager)
		m_W3DShadowTextureManager->freeAllTextures();
}

void W3DProjectedShadowManager::shutdown(void)
{
	removeAllShadows();
	ReleaseResources();
}

Bool W3DProjectedShadowManager::ReAcquireResources(void)
{
#ifdef BUILD_WITH_D3D11
	// The dynamic render target isn't wired up yet — SHADOW_PROJECTION's
	// silhouette-to-texture path falls back to the decal renderer using a
	// cached lookup texture, so no off-screen RT is strictly required to
	// draw decals or fall-through projection. When we revive dynamic
	// projection this is the place to stand up a 512x512 RTV.
	// DX11: no managed pool; decal VBs are created lazily on first use.
	m_dynamicRenderTarget = nullptr;
	m_renderTargetHasAlpha = FALSE;
#endif
	return TRUE;
}

void W3DProjectedShadowManager::ReleaseResources(void)
{
	invalidateCachedLightPositions();	// stale images will regenerate

#ifdef BUILD_WITH_D3D11
	// Release batch buffers — Render::VertexBuffer / IndexBuffer hold
	// ComPtr<ID3D11Buffer> so Destroy() drops the ref.
	if (s_decalBatch.buffersCreated)
	{
		s_decalBatch.vb.Destroy(Render::Renderer::Instance().GetDevice());
		s_decalBatch.ib.Destroy(Render::Renderer::Instance().GetDevice());
		s_decalBatch.buffersCreated = false;
	}
	s_decalBatch.reset();
#endif
}

void W3DProjectedShadowManager::invalidateCachedLightPositions(void)
{
	if (m_W3DShadowTextureManager)
		m_W3DShadowTextureManager->invalidateCachedLightPositions();
}

void W3DProjectedShadowManager::updateRenderTargetTextures(void)
{
	if (!m_shadowList)
		return;
	if (!TheGlobalData || !TheGlobalData->m_useShadowDecals)
		return;

	// Projection textures aren't regenerated yet — see renderShadows() note.
	// Decals never need updates because their image is static.
	if (m_numProjectionShadows)
	{
		for (W3DProjectedShadow* shadow = m_shadowList; shadow; shadow = shadow->m_next)
		{
			if (shadow->m_type != SHADOW_DECAL)
				shadow->update();
		}
	}
}

// -----------------------------------------------------------------------------
// Helpers for loading a decal texture through the existing DX11 image cache.
// Returns the shared W3DShadowTexture wrapper (created + inserted on first
// call for a given filename).
// -----------------------------------------------------------------------------

#ifdef BUILD_WITH_D3D11
static W3DShadowTexture* TryLoadDecalTexture(W3DShadowTextureManager* mgr, const char* textureName)
{
	if (!mgr || !textureName || !textureName[0])
		return nullptr;

	W3DShadowTexture* st = mgr->getTexture(textureName);
	if (st)
		return st;

	Render::Texture* tex = Render::ImageCache::Instance().GetTexture(
		Render::Renderer::Instance().GetDevice(), textureName);
	if (!tex)
		return nullptr;

	st = NEW W3DShadowTexture;
	st->Set_Name(textureName);
	st->setTexture(tex);
	mgr->addTexture(st);
	return st;
}

static W3DShadowTexture* LoadDecalTexture(W3DShadowTextureManager* mgr, const char* textureName)
{
	W3DShadowTexture* st = TryLoadDecalTexture(mgr, textureName);
	if (!st)
	{
		DEBUG_CRASH(("W3DProjectedShadowManager: missing decal texture '%s'", textureName));
		return nullptr;
	}
	return st;
}
#else
static W3DShadowTexture* TryLoadDecalTexture(W3DShadowTextureManager*, const char*) { return nullptr; }
static W3DShadowTexture* LoadDecalTexture(W3DShadowTextureManager*, const char*) { return nullptr; }
#endif

// -----------------------------------------------------------------------------
// queueDecal — the workhorse. Walks the heightmap cells under the shadow's
// footprint and emits one triangle-fan per cell into the decal batch. Each
// vertex is stamped with (worldX, worldY, terrainZ) plus a uv computed from
// the caster's transform so the decal rotates with the caster.
// -----------------------------------------------------------------------------

void W3DProjectedShadowManager::queueDecal(W3DProjectedShadow* shadow)
{
#ifdef BUILD_WITH_D3D11
	if (!shadow || !shadow->m_shadowTexture[0])
		return;

	WorldHeightMap* hmap = GetTerrainHeightMap();
	if (!hmap)
		return;

	const Int borderSize = hmap->getBorderSizeInline();
	const Real mapScaleInv = 1.0f / MAP_XY_FACTOR;

	// Derive caster's world position + orientation.
	Vector3 objPos;
	Matrix3D objXform(true);
	Real layerHeight = 0.0f;
	RenderObjClass* robj = shadow->m_robj;

	if (robj)
	{
		objPos = robj->Get_Position();
		objXform = robj->Get_Transform();

		// Bridges / layers: if the caster sits on a non-ground layer we
		// lift the decal to match so it doesn't draw below the bridge.
		if (robj->Get_User_Data() && TheTerrainLogic)
		{
			Drawable* draw = ((DrawableInfo*)robj->Get_User_Data())->m_drawable;
			const Object* object = draw ? draw->getObject() : nullptr;
			if (object)
			{
				PathfindLayerEnum layer = object->getLayer();
				if (layer != LAYER_GROUND)
					layerHeight = BRIDGE_OFFSET_FACTOR + TheTerrainLogic->getLayerHeight(objPos.X, objPos.Y, layer);
			}
		}
	}
	else
	{
		objPos.Set(shadow->m_x, shadow->m_y, shadow->m_z);
		objXform.Rotate_Z(shadow->m_localAngle);
	}

	objPos.Z = 0.0f;	// decals project top-down; object height doesn't enter UV math

	// Build UV axes from the caster's X/Y vectors. Match the original: if
	// the X axis is degenerate (pure-Z object), fall back to Y and rotate.
	Vector3 uVector = objXform.Get_X_Vector();
	uVector.Z = 0.0f;
	Real vecLength = uVector.Length();
	Vector3 vVector;
	if (vecLength != 0.0f)
	{
		uVector *= 1.0f / vecLength;
		vVector = uVector;
		vVector.Rotate_Z(-1.0f, 0.0f);	// rotate -90°: (x,y)->(y,-x)
	}
	else
	{
		vVector = objXform.Get_Y_Vector();
		vVector.Z = 0.0f;
		vecLength = vVector.Length();
		if (vecLength != 0.0f)
			vVector *= 1.0f / vecLength;
		else
			vVector.Set(0.0f, -1.0f, 0.0f);
		uVector = vVector;
		uVector.Rotate_Z(1.0f, 0.0f);
	}

	// Footprint AABB in world space (caster-local before translation).
	const Real dx = shadow->m_decalSizeX;
	const Real dy = shadow->m_decalSizeY;
	const Vector3 left_x   = -dx * (uVector * (0.5f + shadow->m_decalOffsetU));
	const Vector3 right_x  =  dx * (uVector * (0.5f - shadow->m_decalOffsetU));
	const Vector3 top_y    = -dy * (vVector * (0.5f + shadow->m_decalOffsetV));
	const Vector3 bottom_y =  dy * (vVector * (0.5f - shadow->m_decalOffsetV));
	const Vector3 boxCorners[4] = {
		left_x + top_y,
		right_x + top_y,
		right_x + bottom_y,
		left_x + bottom_y
	};

	Real min_x = boxCorners[0].X, max_x = boxCorners[0].X;
	Real min_y = boxCorners[0].Y, max_y = boxCorners[0].Y;
	for (Int i = 1; i < 4; ++i)
	{
		if (boxCorners[i].X < min_x) min_x = boxCorners[i].X;
		if (boxCorners[i].X > max_x) max_x = boxCorners[i].X;
		if (boxCorners[i].Y < min_y) min_y = boxCorners[i].Y;
		if (boxCorners[i].Y > max_y) max_y = boxCorners[i].Y;
	}

	// Transform UV axes into per-unit-world-space for the vertex loop.
	uVector *= shadow->m_oowDecalSizeX;
	vVector *= shadow->m_oowDecalSizeY;
	const Real uOffset = shadow->m_decalOffsetU + 0.5f;
	const Real vOffset = shadow->m_decalOffsetV + 0.5f;

	// Clip to the heightmap cells currently drawn this frame.
	Int startX = REAL_TO_INT_FLOOR(((objPos.X + min_x) * mapScaleInv)) + borderSize;
	Int endX   = REAL_TO_INT_CEIL (((objPos.X + max_x) * mapScaleInv)) + borderSize;
	Int startY = REAL_TO_INT_FLOOR(((objPos.Y + min_y) * mapScaleInv)) + borderSize;
	Int endY   = REAL_TO_INT_CEIL (((objPos.Y + max_y) * mapScaleInv)) + borderSize;

	startX = std::max(startX, s_drawStartX);
	startX = std::min(startX, s_drawEdgeX);
	startY = std::max(startY, s_drawStartY);
	startY = std::min(startY, s_drawEdgeY);
	endX   = std::max(endX, s_drawStartX);
	endX   = std::min(endX, s_drawEdgeX);
	endY   = std::max(endY, s_drawStartY);
	endY   = std::min(endY, s_drawEdgeY);

	// Keep the same 104-cell cap the original uses so a single decal can't
	// overflow the index-buffer cap. Clip from both sides.
	Int numExtraX = (endX - startX + 1) - 104;
	if (numExtraX > 0)
	{
		Int numStartExtraX = REAL_TO_INT_FLOOR((Real)numExtraX / 2.0f);
		Int numEdgeExtraX  = numExtraX - numStartExtraX;
		startX += numStartExtraX;
		endX   -= numEdgeExtraX;
	}
	Int numExtraY = (endY - startY + 1) - 104;
	if (numExtraY > 0)
	{
		Int numStartExtraY = REAL_TO_INT_FLOOR((Real)numExtraY / 2.0f);
		Int numEdgeExtraY  = numExtraY - numStartExtraY;
		startY += numStartExtraY;
		endY   -= numEdgeExtraY;
	}

	const Int vertsPerRow    = endX - startX + 1;
	const Int vertsPerColumn = endY - startY + 1;
	if (vertsPerRow <= 1 || vertsPerColumn <= 1)
		return;

	const Int numVerts = vertsPerRow * vertsPerColumn;
	const Int numIndex = (endX - startX) * (endY - startY) * 6;

	// Flush if we'd overflow either cap — keeps one texture per draw.
	if (s_decalBatch.verts.size() + numVerts > kShadowDecalVertexCap ||
		s_decalBatch.indices.size() + numIndex > kShadowDecalIndexCap)
	{
		flushDecals(shadow->m_shadowTexture[0], shadow->m_type);
	}

	const uint32_t baseVertex = (uint32_t)s_decalBatch.verts.size();

	// Diffuse — packed ABGR matching Vertex3D.color layout. m_diffuse is
	// already ARGB so swap R↔B.
	uint32_t diffuseABGR;
	{
		uint32_t argb = (uint32_t)shadow->m_diffuse;
		uint32_t a = (argb >> 24) & 0xFF;
		uint32_t r = (argb >> 16) & 0xFF;
		uint32_t g = (argb >> 8)  & 0xFF;
		uint32_t b = argb & 0xFF;
		diffuseABGR = (a << 24) | (b << 16) | (g << 8) | r;
	}

	for (Int j = startY; j <= endY; ++j)
	{
		const Real worldY = (Real)(j - borderSize) * MAP_XY_FACTOR;
		for (Int i = startX; i <= endX; ++i)
		{
			const Real worldX = (Real)(i - borderSize) * MAP_XY_FACTOR;
			Real worldZ = (Real)hmap->getHeight(i, j) * MAP_HEIGHT_SCALE;
			if (layerHeight)
				worldZ = std::max(worldZ, layerHeight);
			else
				worldZ += 0.01f * MAP_XY_FACTOR;	// tiny bias to avoid z-fight with terrain

			Render::Vertex3D v;
			v.position = { worldX, worldY, worldZ };
			v.normal   = { 0.0f, 0.0f, 1.0f };
			Vector3 delta(worldX - objPos.X, worldY - objPos.Y, worldZ);
			v.texcoord = {
				(float)(Vector3::Dot_Product(uVector, delta) + uOffset),
				(float)(Vector3::Dot_Product(vVector, delta) + vOffset)
			};
			v.color = diffuseABGR;
			s_decalBatch.verts.push_back(v);
		}
	}

	// Index buffer — 6 per cell. Winding order depends on heightmap flip
	// state per cell, matching the original.
	Int rowStart = 0;
	for (Int j = startY; j < endY; ++j, rowStart += vertsPerRow)
	{
		Int i = rowStart;
		for (Int k = startX; k < endX; ++k, ++i)
		{
			uint16_t i0 = (uint16_t)(baseVertex + i);
			uint16_t i1 = (uint16_t)(baseVertex + i + 1);
			uint16_t i2 = (uint16_t)(baseVertex + i + vertsPerRow);
			uint16_t i3 = (uint16_t)(baseVertex + i + 1 + vertsPerRow);
			if (hmap->getFlipState(k, j))
			{
				s_decalBatch.indices.push_back(i1);
				s_decalBatch.indices.push_back(i2);
				s_decalBatch.indices.push_back(i0);
				s_decalBatch.indices.push_back(i1);
				s_decalBatch.indices.push_back(i3);
				s_decalBatch.indices.push_back(i2);
			}
			else
			{
				s_decalBatch.indices.push_back(i0);
				s_decalBatch.indices.push_back(i3);
				s_decalBatch.indices.push_back(i2);
				s_decalBatch.indices.push_back(i0);
				s_decalBatch.indices.push_back(i1);
				s_decalBatch.indices.push_back(i3);
			}
		}
	}
#else
	(void)shadow;
#endif
}

// -----------------------------------------------------------------------------
// queueSimpleDecal — single-quad fallback. Produces 4 verts + 6 indices at
// the ground-height beneath the caster, optionally tilted to the ground
// normal. Used when terrain-conforming is unnecessary or too expensive.
// -----------------------------------------------------------------------------

void W3DProjectedShadowManager::queueSimpleDecal(W3DProjectedShadow* shadow)
{
#ifdef BUILD_WITH_D3D11
	if (!shadow || !shadow->m_shadowTexture[0] || !shadow->m_robj)
		return;
	if (!TheTerrainLogic)
		return;

	Vector3 objPos = shadow->m_robj->Get_Position();
	Matrix3D objXform = shadow->m_robj->Get_Transform();
	Coord3D normalRaw;
	// DX11: BaseHeightMapRenderObjClass::getHeightMapHeight isn't built in
	// this target — go through TheTerrainLogic instead, same answer.
	Real groundHeight = TheTerrainLogic->getGroundHeight(objPos.X, objPos.Y, &normalRaw);
	Vector3 groundNormal(normalRaw.x, normalRaw.y, normalRaw.z);

	Vector3 uVector = objXform.Get_X_Vector();
	Real uAlongN = Vector3::Dot_Product(uVector, groundNormal);
	uVector -= uAlongN * groundNormal;
	uVector.Normalize();
	Vector3 vVector;
	Vector3::Cross_Product(uVector, groundNormal, &vVector);

	if (s_decalBatch.verts.size() + 4 > kShadowDecalVertexCap ||
		s_decalBatch.indices.size() + 6 > kShadowDecalIndexCap)
	{
		flushDecals(shadow->m_shadowTexture[0], shadow->m_type);
	}

	objPos.Z = groundHeight;
	objPos += groundNormal * 1.0f;	// tiny offset to prevent z-fighting

	uint32_t baseVertex = (uint32_t)s_decalBatch.verts.size();
	uint32_t diffuseABGR;
	{
		uint32_t argb = (uint32_t)shadow->m_diffuse;
		uint32_t a = (argb >> 24) & 0xFF;
		uint32_t r = (argb >> 16) & 0xFF;
		uint32_t g = (argb >> 8) & 0xFF;
		uint32_t b = argb & 0xFF;
		diffuseABGR = (a << 24) | (b << 16) | (g << 8) | r;
	}

	Vector3 vertex = objPos + vVector * shadow->m_decalSizeY * -0.5f - uVector * shadow->m_decalSizeX * 0.5f;
	Render::Vertex3D v;
	v.normal = { 0.0f, 0.0f, 1.0f };
	v.color = diffuseABGR;

	// Top-left
	v.position = { vertex.X, vertex.Y, vertex.Z };
	v.texcoord = { 0.0f, 0.0f };
	s_decalBatch.verts.push_back(v);
	// Bottom-left
	vertex += vVector * shadow->m_decalSizeY;
	v.position = { vertex.X, vertex.Y, vertex.Z };
	v.texcoord = { 0.0f, 1.0f };
	s_decalBatch.verts.push_back(v);
	// Bottom-right
	vertex += uVector * shadow->m_decalSizeX;
	v.position = { vertex.X, vertex.Y, vertex.Z };
	v.texcoord = { 1.0f, 1.0f };
	s_decalBatch.verts.push_back(v);
	// Top-right
	vertex -= vVector * shadow->m_decalSizeY;
	v.position = { vertex.X, vertex.Y, vertex.Z };
	v.texcoord = { 1.0f, 0.0f };
	s_decalBatch.verts.push_back(v);

	s_decalBatch.indices.push_back((uint16_t)(baseVertex + 0));
	s_decalBatch.indices.push_back((uint16_t)(baseVertex + 1));
	s_decalBatch.indices.push_back((uint16_t)(baseVertex + 2));
	s_decalBatch.indices.push_back((uint16_t)(baseVertex + 0));
	s_decalBatch.indices.push_back((uint16_t)(baseVertex + 2));
	s_decalBatch.indices.push_back((uint16_t)(baseVertex + 3));
#else
	(void)shadow;
#endif
}

// -----------------------------------------------------------------------------
// flushDecals — commit whatever's in the CPU batch to the GPU. Picks the
// Renderer's decal state helper from the shadow type. Called by queueDecal
// when a batch overflows, by renderShadows at the end of each list, and
// when the caller wants to switch decal textures mid-frame.
// -----------------------------------------------------------------------------

void W3DProjectedShadowManager::flushDecals(W3DShadowTexture* texture, ShadowType type)
{
#ifdef BUILD_WITH_D3D11
	if (s_decalBatch.verts.empty() || s_decalBatch.indices.empty())
	{
		s_decalBatch.reset();
		return;
	}
	if (!texture || !texture->getTexture())
	{
		s_decalBatch.reset();
		return;
	}

	Render::Renderer& renderer = Render::Renderer::Instance();
	Render::Device& device = renderer.GetDevice();

	if (!EnsureDecalBuffers(device))
	{
		s_decalBatch.reset();
		return;
	}

	// Upload this batch's CPU arrays into the persistent dynamic VB/IB.
	const uint32_t vbBytes = (uint32_t)(s_decalBatch.verts.size() * sizeof(Render::Vertex3D));
	const uint32_t ibBytes = (uint32_t)(s_decalBatch.indices.size() * sizeof(uint16_t));
	s_decalBatch.vb.Update(device, s_decalBatch.verts.data(), vbBytes);
	s_decalBatch.ib.Update(device, s_decalBatch.indices.data(), ibBytes);

	ApplyDecalState(renderer, type);

	Render::Float4x4 worldIdentity = Render::Float4x4Identity();
	Render::Float4 tint = { 1.0f, 1.0f, 1.0f, 1.0f };

	renderer.Draw3DIndexed(
		s_decalBatch.vb, s_decalBatch.ib,
		(uint32_t)s_decalBatch.indices.size(),
		texture->getTexture(),
		worldIdentity, tint);

	s_decalBatch.reset();
#else
	(void)texture;
	(void)type;
#endif
}

// -----------------------------------------------------------------------------
// renderShadows — entry from DoShadows(rinfo, FALSE). Iterates both the
// object-bound (m_shadowList) and loose-decal (m_decalList) lists, groups
// by (texture, type) so batches stay coherent, and hands each shadow off
// to queueDecal. Projection shadows currently fall through to queueDecal
// using the cached silhouette texture — a TODO further down covers the
// real projection path.
// -----------------------------------------------------------------------------

Int W3DProjectedShadowManager::renderShadows(RenderInfoClass& rinfo)
{
	Int projectionCount = 0;

#ifdef BUILD_WITH_D3D11
	if (!TheGlobalData || !TheGlobalData->m_useShadowDecals)
		return 0;
	if (!m_shadowList && !m_decalList)
		return 0;

	// Compute clipping extent for queueDecal from currently-drawn terrain.
	WorldHeightMap* hmap = GetTerrainHeightMap();
	if (!hmap)
		return 0;

	s_drawEdgeY = hmap->getDrawOrgY() + hmap->getDrawHeight() - 1;
	s_drawEdgeX = hmap->getDrawOrgX() + hmap->getDrawWidth() - 1;
	if (s_drawEdgeX > (hmap->getXExtent() - 1))
		s_drawEdgeX = hmap->getXExtent() - 1;
	if (s_drawEdgeY > (hmap->getYExtent() - 1))
		s_drawEdgeY = hmap->getYExtent() - 1;
	s_drawStartX = hmap->getDrawOrgX();
	s_drawStartY = hmap->getDrawOrgY();

	// ---- Object-bound shadows (SHADOW_DECAL + SHADOW_PROJECTION) --------
	W3DShadowTexture* lastTex = nullptr;
	ShadowType lastType = SHADOW_NONE;

	for (W3DProjectedShadow* shadow = m_shadowList; shadow; shadow = shadow->m_next)
	{
		if (!shadow->m_isEnabled || shadow->m_isInvisibleEnabled)
			continue;

		// Any decal variant (plain / alpha / additive) uses the batched
		// heightmap-conforming path. Group by (texture, type) so each
		// batch hits one draw call.
		if (shadow->m_type & (SHADOW_DECAL | SHADOW_ALPHA_DECAL | SHADOW_ADDITIVE_DECAL))
		{
			if (lastTex == nullptr)
				lastTex = shadow->m_shadowTexture[0];
			if (lastType == SHADOW_NONE)
				lastType = shadow->m_type;

			if (shadow->m_shadowTexture[0] != lastTex || shadow->m_type != lastType)
			{
				flushDecals(lastTex, lastType);
				lastTex = shadow->m_shadowTexture[0];
				lastType = shadow->m_type;
			}

			// Only draw if the caster is actually visible — prevents
			// shadows ghosting under shrouded or off-screen units.
			if (shadow->m_robj && shadow->m_robj->Is_Really_Visible())
			{
				queueDecal(shadow);
				++projectionCount;
			}
			continue;
		}

		// TODO: SHADOW_PROJECTION / SHADOW_DYNAMIC_PROJECTION should bind
		// a per-object projection matrix and render the silhouette into
		// the dynamic RT, then stamp the resulting texture onto terrain
		// + receivers. For now we fall through to the decal renderer
		// using the cached silhouette texture (addShadow builds one via
		// the texture manager) so visual parity is maintained on the
		// terrain; receivers (buildings, infantry) won't be stamped yet.
		// DX11: stubbed until TexProjectClass is ported.
		if (shadow->m_type & (SHADOW_PROJECTION | SHADOW_DYNAMIC_PROJECTION | SHADOW_DIRECTIONAL_PROJECTION))
		{
			if (!shadow->m_shadowTexture[0])
				continue;

			if (lastTex == nullptr)
				lastTex = shadow->m_shadowTexture[0];
			if (lastType == SHADOW_NONE)
				lastType = SHADOW_DECAL;

			if (shadow->m_shadowTexture[0] != lastTex || lastType != SHADOW_DECAL)
			{
				flushDecals(lastTex, lastType);
				lastTex = shadow->m_shadowTexture[0];
				lastType = SHADOW_DECAL;
			}

			if (shadow->m_robj && shadow->m_robj->Is_Really_Visible())
			{
				queueDecal(shadow);
				++projectionCount;
			}
		}
	}

	flushDecals(lastTex, lastType);

	// ---- Loose decals (m_decalList) --------------------------------------
	lastTex = nullptr;
	lastType = SHADOW_NONE;
	for (W3DProjectedShadow* shadow = m_decalList; shadow; shadow = shadow->m_next)
	{
		if (!shadow->m_isEnabled || shadow->m_isInvisibleEnabled)
			continue;

		if (lastTex == nullptr)
			lastTex = shadow->m_shadowTexture[0];
		if (lastType == SHADOW_NONE)
			lastType = shadow->m_type;

		if (shadow->m_shadowTexture[0] != lastTex || shadow->m_type != lastType)
		{
			flushDecals(lastTex, lastType);
			lastTex = shadow->m_shadowTexture[0];
			lastType = shadow->m_type;
		}

		// Loose decals render even when the caster is hidden — some (selection
		// rings, scorch marks) don't have a caster at all. The original gated
		// on `robj && !robj->Is_Really_Visible()`; we preserve that.
		if (!(shadow->m_robj && !shadow->m_robj->Is_Really_Visible()))
		{
			queueDecal(shadow);
			++projectionCount;
		}
	}

	flushDecals(lastTex, lastType);
#else
	(void)rinfo;
#endif

	return projectionCount;
}

// DX11: projection-onto-terrain stub (used by the old SHADOW_PROJECTION path
// that rendered a dynamic silhouette + re-projected onto the visible heightmap
// patch). The decal fall-through in renderShadows covers terrain coverage for
// now; when TexProjectClass is ported, this function builds the projected
// vertex grid instead.
Int W3DProjectedShadowManager::renderProjectedTerrainShadow(W3DProjectedShadow* /*shadow*/, AABoxClass& /*box*/)
{
	return 0;
}

// =============================================================================
// addDecal / addShadow / createDecalShadow — factory paths. Each follows the
// original: look up or create the texture, allocate a shadow, splice into
// the appropriate list, and update counters.
// =============================================================================

Shadow* W3DProjectedShadowManager::addDecal(Shadow::ShadowTypeInfo* shadowInfo)
{
	if (!shadowInfo || !m_W3DShadowTextureManager)
		return nullptr;

	char texture_name[128];
	Int nameLen = (Int)strlen(shadowInfo->m_ShadowName);
	if (nameLen <= 0)
		return nullptr;
	strncpy(texture_name, shadowInfo->m_ShadowName, nameLen);
	texture_name[nameLen] = '\0';
	strcat(texture_name, ".tga");

	W3DShadowTexture* st = LoadDecalTexture(m_W3DShadowTextureManager, texture_name);
	if (!st)
		return nullptr;

	W3DProjectedShadow* shadow = NEW W3DProjectedShadow;
	if (!shadow)
		return nullptr;

	shadow->setRenderObject(nullptr);
	shadow->setTexture(0, st);
	shadow->m_type = shadowInfo->m_type;
	shadow->m_allowWorldAlign = shadowInfo->allowWorldAlign;
	shadow->m_flags = (shadowInfo->m_type & SHADOW_DIRECTIONAL_PROJECTION) ? 1 : 0;

	const Real sx = shadowInfo->m_sizeX;
	const Real sy = shadowInfo->m_sizeY;
	shadow->m_oowDecalSizeX = (sx != 0.0f) ? (1.0f / sx) : 0.0f;
	shadow->m_oowDecalSizeY = (sy != 0.0f) ? (1.0f / sy) : 0.0f;
	shadow->m_decalSizeX = sx;
	shadow->m_decalSizeY = sy;
	shadow->m_decalOffsetU = 0.0f;
	shadow->m_decalOffsetV = 0.0f;

	shadow->init();

	// Insert into m_decalList next to same-texture entries so flushDecals
	// batches them together.
	W3DProjectedShadow* prev = nullptr;
	W3DProjectedShadow* next = nullptr;
	for (next = m_decalList; next; prev = next, next = next->m_next)
	{
		if (next->m_shadowTexture[0] == st)
		{
			shadow->m_next = next;
			if (prev)
				prev->m_next = shadow;
			else
				m_decalList = shadow;
			break;
		}
	}
	if (next == nullptr)
	{
		shadow->m_next = m_decalList;
		m_decalList = shadow;
	}

	switch (shadow->m_type)
	{
	case SHADOW_DECAL:
	case SHADOW_ALPHA_DECAL:
	case SHADOW_ADDITIVE_DECAL:
		++m_numDecalShadows;
		break;
	case SHADOW_PROJECTION:
		++m_numProjectionShadows;
		break;
	default:
		break;
	}

	return shadow;
}

Shadow* W3DProjectedShadowManager::addDecal(RenderObjClass* robj, Shadow::ShadowTypeInfo* shadowInfo)
{
	if (!robj || !shadowInfo || !m_W3DShadowTextureManager)
		return nullptr;

	char texture_name[128];
	Int nameLen = (Int)strlen(shadowInfo->m_ShadowName);
	if (nameLen <= 0)
		return nullptr;
	strncpy(texture_name, shadowInfo->m_ShadowName, nameLen);
	texture_name[nameLen] = '\0';
	strcat(texture_name, ".tga");

	W3DShadowTexture* st = LoadDecalTexture(m_W3DShadowTextureManager, texture_name);
	if (!st)
		return nullptr;

	W3DProjectedShadow* shadow = NEW W3DProjectedShadow;
	if (!shadow)
		return nullptr;

	shadow->setRenderObject(robj);
	shadow->setTexture(0, st);
	shadow->m_type = shadowInfo->m_type;
	shadow->m_allowWorldAlign = shadowInfo->allowWorldAlign;
	shadow->m_flags = (shadowInfo->m_type & SHADOW_DIRECTIONAL_PROJECTION) ? 1 : 0;

	// Bounding-box fallback — if the caller didn't size the decal, use the
	// caster's extents so it covers the footprint.
	AABoxClass box;
	robj->Get_Obj_Space_Bounding_Box(box);

	Real decalSizeX = shadowInfo->m_sizeX;
	Real decalSizeY = shadowInfo->m_sizeY;
	Real decalOffsetX = shadowInfo->m_offsetX;
	Real decalOffsetY = shadowInfo->m_offsetY;
	if (decalSizeX == 0.0f) decalSizeX = box.Extent.X * 2.0f;
	if (decalSizeY == 0.0f) decalSizeY = box.Extent.Y * 2.0f;

	shadow->m_oowDecalSizeX = 1.0f / decalSizeX;
	shadow->m_oowDecalSizeY = 1.0f / decalSizeY;
	shadow->m_decalSizeX = decalSizeX;
	shadow->m_decalSizeY = decalSizeY;
	shadow->m_decalOffsetU = decalOffsetX ? (decalOffsetX * shadow->m_oowDecalSizeX) : 0.0f;
	shadow->m_decalOffsetV = decalOffsetY ? (decalOffsetY * shadow->m_oowDecalSizeY) : 0.0f;

	shadow->init();

	W3DProjectedShadow* prev = nullptr;
	W3DProjectedShadow* next = nullptr;
	for (next = m_decalList; next; prev = next, next = next->m_next)
	{
		if (next->m_shadowTexture[0] == st)
		{
			shadow->m_next = next;
			if (prev)
				prev->m_next = shadow;
			else
				m_decalList = shadow;
			break;
		}
	}
	if (next == nullptr)
	{
		shadow->m_next = m_decalList;
		m_decalList = shadow;
	}

	switch (shadow->m_type)
	{
	case SHADOW_DECAL:
	case SHADOW_ALPHA_DECAL:
	case SHADOW_ADDITIVE_DECAL:
		++m_numDecalShadows;
		break;
	case SHADOW_PROJECTION:
		++m_numProjectionShadows;
		break;
	default:
		break;
	}

	return shadow;
}

W3DProjectedShadow* W3DProjectedShadowManager::addShadow(RenderObjClass* robj, Shadow::ShadowTypeInfo* shadowInfo, Drawable* /*draw*/)
{
	if (!robj || !m_W3DShadowTextureManager)
		return nullptr;
	if (!TheGlobalData || !TheGlobalData->m_useShadowDecals)
		return nullptr;

	W3DShadowTexture* st = nullptr;
	ShadowType shadowType = SHADOW_NONE;
	Bool allowWorldAlign = FALSE;
	Real decalSizeX = 0.0f, decalSizeY = 0.0f;
	Real decalOffsetX = 0.0f, decalOffsetY = 0.0f;
	Bool allowSunDirection = FALSE;
	char texture_name[128];

	if (shadowInfo)
	{
		if (shadowInfo->m_type == SHADOW_DECAL)
		{
			Int nameLen = (Int)strlen(shadowInfo->m_ShadowName);
			if (nameLen <= 1)
				strcpy(texture_name, "shadow.tga");
			else
			{
				strncpy(texture_name, shadowInfo->m_ShadowName, nameLen);
				texture_name[nameLen] = '\0';
				strcat(texture_name, ".tga");
			}
			st = LoadDecalTexture(m_W3DShadowTextureManager, texture_name);
			if (!st)
				return nullptr;
			shadowType = SHADOW_DECAL;
			allowSunDirection = (shadowInfo->m_type & SHADOW_DIRECTIONAL_PROJECTION) ? TRUE : FALSE;
			decalSizeX = shadowInfo->m_sizeX;
			decalSizeY = shadowInfo->m_sizeY;
			decalOffsetX = shadowInfo->m_offsetX;
			decalOffsetY = shadowInfo->m_offsetY;
		}
		else if (shadowInfo->m_type == SHADOW_PROJECTION)
		{
			// Projection uses a cached silhouette texture keyed by robj name.
			// The texture isn't generated in this port — if the asset cache
			// has a prebuilt silhouette, we use it; otherwise the call
			// silently succeeds without adding a shadow so the rest of the
			// engine doesn't error.
			if (shadowInfo->m_ShadowName[0] != '\0')
				strcpy(texture_name, shadowInfo->m_ShadowName);
			else
				strcpy(texture_name, robj->Get_Name());

			st = m_W3DShadowTextureManager->getTexture(texture_name);
			if (!st)
			{
				// Try direct texture load (<name>.tga) before falling back to the
				// generic blob, so per-object authored silhouettes are used when present.
				char texture_name_tga[132];
				size_t tnLen = strlen(texture_name);
				if (tnLen > 0 && tnLen < (sizeof(texture_name_tga) - 5))
				{
					memcpy(texture_name_tga, texture_name, tnLen);
					texture_name_tga[tnLen] = '\0';
					if (strchr(texture_name_tga, '.') == nullptr)
						strcat(texture_name_tga, ".tga");
					st = TryLoadDecalTexture(m_W3DShadowTextureManager, texture_name_tga);
				}
				if (!st)
					st = TryLoadDecalTexture(m_W3DShadowTextureManager, texture_name);
				// DX11: fall back to the generic shadow.tga until dynamic
				// silhouette generation lands.
				if (!st)
				{
					st = LoadDecalTexture(m_W3DShadowTextureManager, "shadow.tga");
					if (!st)
						return nullptr;
				}
			}
			shadowType = SHADOW_PROJECTION;
		}
		else
		{
			return nullptr;
		}
	}
	else
	{
		strcpy(texture_name, robj->Get_Name());
		st = m_W3DShadowTextureManager->getTexture(texture_name);
		if (!st)
		{
			char texture_name_tga[132];
			size_t tnLen = strlen(texture_name);
			if (tnLen > 0 && tnLen < (sizeof(texture_name_tga) - 5))
			{
				memcpy(texture_name_tga, texture_name, tnLen);
				texture_name_tga[tnLen] = '\0';
				if (strchr(texture_name_tga, '.') == nullptr)
					strcat(texture_name_tga, ".tga");
				st = TryLoadDecalTexture(m_W3DShadowTextureManager, texture_name_tga);
			}
			if (!st)
				st = TryLoadDecalTexture(m_W3DShadowTextureManager, texture_name);
			if (!st)
			{
				st = LoadDecalTexture(m_W3DShadowTextureManager, "shadow.tga");
				if (!st)
					return nullptr;
			}
		}
		shadowType = SHADOW_PROJECTION;
	}

	W3DProjectedShadow* shadow = NEW W3DProjectedShadow;
	if (!shadow)
		return nullptr;

	shadow->setRenderObject(robj);
	shadow->setTexture(0, st);
	shadow->m_type = shadowType;
	shadow->m_allowWorldAlign = allowWorldAlign;
	shadow->m_flags = allowSunDirection ? 1 : 0;

	AABoxClass box;
	robj->Get_Obj_Space_Bounding_Box(box);

	if (shadowType == SHADOW_PROJECTION)
	{
		// Match original SHADOW_PROJECTION UV orientation semantics.
		Real oowDecalSizeX = decalSizeX ? (1.0f / decalSizeX) : (1.0f / (box.Extent.X * 2.0f));
		Real oowDecalSizeY = decalSizeY ? (-1.0f / decalSizeY) : (-1.0f / (box.Extent.Y * 2.0f));
		Real offsetU = decalOffsetX ? (-decalOffsetX * oowDecalSizeX) : 0.0f;
		Real offsetV = decalOffsetY ? (-decalOffsetY * oowDecalSizeY) : 0.0f;

		shadow->m_oowDecalSizeX = oowDecalSizeX;
		shadow->m_oowDecalSizeY = oowDecalSizeY;
		shadow->m_decalSizeX = (oowDecalSizeX != 0.0f) ? (1.0f / oowDecalSizeX) : 0.0f;
		shadow->m_decalSizeY = (oowDecalSizeY != 0.0f) ? (1.0f / oowDecalSizeY) : 0.0f;
		shadow->m_decalOffsetU = offsetU;
		shadow->m_decalOffsetV = offsetV;
	}
	else
	{
		// Decal path uses direct world-space footprint sizes.
		if (decalSizeX == 0.0f)
			decalSizeX = box.Extent.X * 2.0f;
		if (decalSizeY == 0.0f)
			decalSizeY = box.Extent.Y * 2.0f;

		shadow->m_decalSizeX = decalSizeX;
		shadow->m_decalSizeY = decalSizeY;
		shadow->m_oowDecalSizeX = 1.0f / decalSizeX;
		shadow->m_oowDecalSizeY = 1.0f / decalSizeY;
		shadow->m_decalOffsetU = decalOffsetX ? (decalOffsetX * shadow->m_oowDecalSizeX) : 0.0f;
		shadow->m_decalOffsetV = decalOffsetY ? (decalOffsetY * shadow->m_oowDecalSizeY) : 0.0f;
	}

	shadow->init();

	// Splice into m_shadowList, grouped by texture for batch coherence.
	W3DProjectedShadow* prev = nullptr;
	W3DProjectedShadow* next = nullptr;
	for (next = m_shadowList; next; prev = next, next = next->m_next)
	{
		if (next->m_shadowTexture[0] == st)
		{
			shadow->m_next = next;
			if (prev)
				prev->m_next = shadow;
			else
				m_shadowList = shadow;
			break;
		}
	}
	if (next == nullptr)
	{
		shadow->m_next = m_shadowList;
		m_shadowList = shadow;
	}

	switch (shadow->m_type)
	{
	case SHADOW_DECAL:
		++m_numDecalShadows;
		break;
	case SHADOW_PROJECTION:
		++m_numProjectionShadows;
		break;
	default:
		break;
	}

	return shadow;
}

W3DProjectedShadow* W3DProjectedShadowManager::createDecalShadow(Shadow::ShadowTypeInfo* shadowInfo)
{
	if (!shadowInfo || !m_W3DShadowTextureManager)
		return nullptr;

	char texture_name[128];
	Int nameLen = (Int)strlen(shadowInfo->m_ShadowName);
	if (nameLen <= 1)
		strcpy(texture_name, "shadow.tga");
	else
	{
		strncpy(texture_name, shadowInfo->m_ShadowName, nameLen);
		texture_name[nameLen] = '\0';
		strcat(texture_name, ".tga");
	}

	W3DShadowTexture* st = LoadDecalTexture(m_W3DShadowTextureManager, texture_name);
	if (!st)
		return nullptr;

	W3DProjectedShadow* shadow = NEW W3DProjectedShadow;
	if (!shadow)
		return nullptr;

	shadow->setTexture(0, st);
	shadow->m_type = SHADOW_DECAL;
	shadow->m_allowWorldAlign = FALSE;

	const Real defaultWidth = 10.0f;
	Real decalSizeX = shadowInfo->m_sizeX;
	Real decalSizeY = shadowInfo->m_sizeY;
	if (decalSizeX == 0.0f) decalSizeX = defaultWidth * 2.0f;
	if (decalSizeY == 0.0f) decalSizeY = defaultWidth * 2.0f;

	shadow->m_decalSizeX = decalSizeX;
	shadow->m_decalSizeY = decalSizeY;
	shadow->m_oowDecalSizeX = 1.0f / decalSizeX;
	shadow->m_oowDecalSizeY = 1.0f / decalSizeY;
	shadow->m_decalOffsetU = shadowInfo->m_offsetX ? (shadowInfo->m_offsetX * shadow->m_oowDecalSizeX) : 0.0f;
	shadow->m_decalOffsetV = shadowInfo->m_offsetY ? (shadowInfo->m_offsetY * shadow->m_oowDecalSizeY) : 0.0f;
	shadow->m_flags = 0;

	shadow->init();
	// Note: not added to any list — caller owns it (used for temporary decals).
	return shadow;
}

void W3DProjectedShadowManager::removeShadow(W3DProjectedShadow* shadow)
{
	if (!shadow)
		return;

	// Check loose decal list first (alpha / additive decals always live
	// here).
	if (shadow->m_type & (SHADOW_ALPHA_DECAL | SHADOW_ADDITIVE_DECAL))
	{
		W3DProjectedShadow* prev = nullptr;
		for (W3DProjectedShadow* cur = m_decalList; cur; prev = cur, cur = cur->m_next)
		{
			if (cur == shadow)
			{
				if (prev)
					prev->m_next = shadow->m_next;
				else
					m_decalList = shadow->m_next;
				switch (shadow->m_type)
				{
				case SHADOW_DECAL: --m_numDecalShadows; break;
				case SHADOW_PROJECTION: --m_numProjectionShadows; break;
				default: break;
				}
				delete shadow;
				return;
			}
		}
	}

	// Object-bound list (SHADOW_DECAL + SHADOW_PROJECTION).
	W3DProjectedShadow* prev = nullptr;
	for (W3DProjectedShadow* cur = m_shadowList; cur; prev = cur, cur = cur->m_next)
	{
		if (cur == shadow)
		{
			if (prev)
				prev->m_next = shadow->m_next;
			else
				m_shadowList = shadow->m_next;
			switch (shadow->m_type)
			{
			case SHADOW_DECAL: --m_numDecalShadows; break;
			case SHADOW_PROJECTION: --m_numProjectionShadows; break;
			default: break;
			}
			delete shadow;
			return;
		}
	}

	// Not in either list — might be one created by createDecalShadow and
	// never added. Just delete it.
	delete shadow;
}

void W3DProjectedShadowManager::removeAllShadows(void)
{
	W3DProjectedShadow* next = nullptr;
	for (W3DProjectedShadow* cur = m_shadowList; cur; cur = next)
	{
		next = cur->m_next;
		cur->m_next = nullptr;
		delete cur;
	}
	m_shadowList = nullptr;

	for (W3DProjectedShadow* cur = m_decalList; cur; cur = next)
	{
		next = cur->m_next;
		cur->m_next = nullptr;
		delete cur;
	}
	m_decalList = nullptr;

	m_numDecalShadows = 0;
	m_numProjectionShadows = 0;
}

// =============================================================================
// W3DProjectedShadow — per-instance shadow. Mostly just holds state; the
// rendering work happens in the manager.
// =============================================================================

W3DProjectedShadow::W3DProjectedShadow(void)
	: m_robj(nullptr)
	, m_next(nullptr)
	, m_allowWorldAlign(FALSE)
	, m_decalOffsetU(0.0f)
	, m_decalOffsetV(0.0f)
	, m_flags(0)
{
	m_diffuse = 0xffffffff;
	m_lastObjPosition.Set(0, 0, 0);
	m_type = SHADOW_NONE;
	m_isEnabled = TRUE;
	m_isInvisibleEnabled = FALSE;
	for (Int i = 0; i < MAX_SHADOW_LIGHTS; ++i)
		m_shadowTexture[i] = nullptr;
}

W3DProjectedShadow::~W3DProjectedShadow(void)
{
	for (Int i = 0; i < MAX_SHADOW_LIGHTS; ++i)
	{
		// Caller's texture cache still owns a ref; just drop ours.
		if (m_shadowTexture[i])
		{
			m_shadowTexture[i]->releaseRef();
			m_shadowTexture[i] = nullptr;
		}
	}
}

void W3DProjectedShadow::setTexture(Int lightIndex, W3DShadowTexture* texture)
{
	if (lightIndex < 0 || lightIndex >= MAX_SHADOW_LIGHTS)
		return;
	m_shadowTexture[lightIndex] = texture;
}

void W3DProjectedShadow::init(void)
{
	// DX11: TexProjectClass construction for SHADOW_PROJECTION is stubbed.
	// When dynamic silhouette generation lands, allocate the projector here
	// and give it the silhouette texture.
}

void W3DProjectedShadow::update(void)
{
	// DX11: projection refresh stubbed. The original regenerated the
	// silhouette when the sun light moved and rebuilt the per-shadow
	// projection matrix when the caster moved. We rely on the cached
	// texture + the caster's current transform instead.
	if (m_robj)
		setObjPosHistory(m_robj->Get_Position());
}

void W3DProjectedShadow::release(void)
{
	if (TheW3DProjectedShadowManager)
		TheW3DProjectedShadowManager->removeShadow(this);
	else
		delete this;
}
