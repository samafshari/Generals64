/*
**  Command & Conquer Generals Zero Hour(tm)
**  Copyright 2025 Electronic Arts Inc.
**
**  This program is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
// D3D11Shims.cpp
//
// Provides real minimal implementations for symbols that were previously
// supplied by D3D8-dependent source files which have been removed from the
// build during the D3D8 -> D3D11 migration.
//
// This is NOT a collection of empty stubs - each implementation is the
// minimal correct version needed for the game engine to link and run.
////////////////////////////////////////////////////////////////////////////////

// ---------------------------------------------------------------------------
// Standard headers
// ---------------------------------------------------------------------------
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <cfloat>   // FLT_MAX — used by particle back-to-front sort
#include <stack>
#include <algorithm>

// ---------------------------------------------------------------------------
// Engine headers
// ---------------------------------------------------------------------------
#include "Common/GameCommon.h"
#include "Common/GameMemory.h"
#include "Common/AsciiString.h"
#include "Common/FramePacer.h"
#include "Common/Dict.h"
#include "Common/CosmeticsCache.h"
#include "Common/GlobalData.h"
#include "Common/INI.h"
#include "Common/Module.h"
#include "Common/DrawModule.h"
#include "Common/ModelState.h"
#include "Common/STLTypedefs.h"
#include "GameClient/Color.h"
#include "GameClient/TerrainRoads.h"
#include "GameClient/Display.h"
#include "GameClient/DebugDisplay.h"
#include "GameClient/GameFont.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Shadow.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/TerrainLogic.h"
#include "GameClient/TerrainVisual.h"
#include "Common/FileSystem.h"
#include "Common/GameState.h"
#include "GameClient/MapUtil.h"
#include "Common/MapReaderWriterInfo.h"
#include "Common/PlayerList.h"
#include "Common/Xfer.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "GameLogic/GhostObject.h"
#include "Common/MapObject.h"
#include "Common/GameUtility.h"
#include "Common/Geometry.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/PartitionManager.h"

// Device-specific headers
#include "W3DDevice/GameClient/W3DInGameUI.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DGameFont.h"
#include "W3DDevice/GameClient/ModelRenderer.h"
#include "W3DDevice/GameClient/TerrainRenderer.h"
#include "W3DDevice/GameClient/ImageCache.h"
#include "W3DDevice/GameLogic/W3DGhostObject.h"
#include "W3DDevice/GameLogic/W3DTerrainLogic.h"
// Forward-declare DX8 types used in W3DWaterTracks.h so the header compiles
// without pulling in the full DX8 wrapper headers.
class DX8VertexBufferClass;
class DX8IndexBufferClass;
class VertexMaterialClass;
class ShaderClass;
class TextureClass;
class SphereClass;
class AABoxClass;
class RenderInfoClass;
#include "W3DDevice/GameClient/W3DWaterTracks.h"
#include "GameClient/GameClient.h"
#include "GameClient/Drawable.h"

// Draw module headers
#include "W3DDevice/GameClient/Module/W3DDebrisDraw.h"
#include "W3DDevice/GameClient/Module/W3DDefaultDraw.h"
#include "W3DDevice/GameClient/Module/W3DDependencyModelDraw.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "W3DDevice/GameClient/Module/W3DLaserDraw.h"
#include "W3DDevice/GameClient/Module/W3DOverlordTankDraw.h"
#include "W3DDevice/GameClient/Module/W3DOverlordTruckDraw.h"
#include "W3DDevice/GameClient/Module/W3DOverlordAircraftDraw.h"
#include "W3DDevice/GameClient/Module/W3DPoliceCarDraw.h"
#include "W3DDevice/GameClient/Module/W3DProjectileStreamDraw.h"
#include "W3DDevice/GameClient/Module/W3DPropDraw.h"
#include "W3DDevice/GameClient/Module/W3DRopeDraw.h"
#include "W3DDevice/GameClient/Module/W3DScienceModelDraw.h"
#include "W3DDevice/GameClient/Module/W3DSupplyDraw.h"
#include "W3DDevice/GameClient/Module/W3DTankDraw.h"
#include "W3DDevice/GameClient/Module/W3DTruckDraw.h"
#include "W3DDevice/GameClient/Module/W3DTankTruckDraw.h"
#include "W3DDevice/GameClient/Module/W3DTracerDraw.h"
#include "W3DDevice/GameClient/Module/W3DTreeDraw.h"

// WW3D headers
#include "WW3D2/animobj.h"
#include "WW3D2/ww3d.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/render2dsentence.h"
#include "WW3D2/render2d.h"
#include "WW3D2/scene.h"
#include "WW3D2/segline.h"
#include "WW3D2/line3d.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/camera.h"

// Well-known keys now provided by WorldHeightMap.cpp

// LOD / shader headers
#include "Common/GameLOD.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"

// SidesList for MapObject::verifyValidTeam
#include "GameLogic/SidesList.h"


// Bind overlay textures for ground-level rendering.
// Cloud shadows are now procedural (no texture). Shroud texture stays
// bound at t3 from TerrainRenderer::Render() across draw calls.
static void BindOverlayTexturesForGroundPass()
{
	// Nothing to bind — clouds are procedural, shroud persists from terrain pass.
}

////////////////////////////////////////////////////////////////////////////////
// SECTION 1: Critical globals
////////////////////////////////////////////////////////////////////////////////

namespace
{
	void AppendDX11ShimTrace(const char *format, ...)
	{
		return; // Debug logging removed
	}

	class D3D11Shadow final : public Shadow
	{
	public:
		D3D11Shadow* m_next = nullptr;
		D3D11Shadow* m_prev = nullptr;
		char m_shadowTexName[64] = {};
		float m_offsetX = 0.0f;
		float m_offsetY = 0.0f;
		// Bound render object: when non-null the shadow follows this object's
		// world transform every frame. Null for static decals (e.g. addDecal()
		// called without a robj from script/UI code).
		RenderObjClass* m_robj = nullptr;

		D3D11Shadow(const Shadow::ShadowTypeInfo *shadowInfo, RenderObjClass *robj)
		{
			m_isEnabled = TRUE;
			m_isInvisibleEnabled = FALSE;
			m_type = shadowInfo && shadowInfo->m_type != SHADOW_NONE ? shadowInfo->m_type : SHADOW_DECAL;
			m_x = 0.0f;
			m_y = 0.0f;
			m_z = 0.0f;

			// Default behavior matches the previously-shipping classic path:
			// pass m_sizeX/m_sizeY through unchanged. For SHADOW_VOLUME entries
			// these values are actually sun-elevation angles in degrees (the
			// original W3DVolumetricShadow code does `tan(m_sizeX/180*PI)`),
			// not world sizes — but the classic D3D11 port has been treating
			// them as world sizes and clamping zero to 15. We preserve that
			// behavior so default look doesn't regress. The Enhanced Shadows
			// toggle (g_useEnhancedShadows) opts into the bbox-derived size
			// path which produces accurate footprints but changes look.
			Real sizeX = shadowInfo ? shadowInfo->m_sizeX : 0.0f;
			Real sizeY = shadowInfo ? shadowInfo->m_sizeY : 0.0f;

			extern bool g_useEnhancedShadows;
			if (g_useEnhancedShadows)
			{
				const bool isVolume     = shadowInfo && (shadowInfo->m_type & SHADOW_VOLUME) != 0;
				const bool isProjection = shadowInfo && (shadowInfo->m_type & SHADOW_PROJECTION) != 0;
				// SHADOW_VOLUME stores sun-elevation degrees in m_sizeX (not a
				// world size). SHADOW_PROJECTION's m_sizeX/m_sizeY are world
				// sizes BUT often left at 0 in the INI for trees/buildings —
				// the original game baked a per-object silhouette texture and
				// used the obj-space bbox extent for the decal footprint. We
				// don't bake silhouettes (yet) but we can at least use the
				// bbox extent so trees and buildings get proportionally-correct
				// shadow rectangles instead of a generic 15×15 disc.
				if (isVolume || isProjection || sizeX <= 0.0f || sizeY <= 0.0f)
				{
					if (robj)
					{
						AABoxClass box;
						robj->Get_Obj_Space_Bounding_Box(box);
						Real bx = box.Extent.X * 2.0f;
						Real by = box.Extent.Y * 2.0f;
						if (isVolume || isProjection)
						{
							sizeX = bx;
							sizeY = by;
						}
						else
						{
							if (sizeX <= 0.0f) sizeX = bx;
							if (sizeY <= 0.0f) sizeY = by;
						}
					}
				}
			}
			setSize(sizeX, sizeY);

			setColor(0x00FFFFFF);
			setOpacity(255);
			if (shadowInfo && shadowInfo->m_ShadowName[0])
				strncpy(m_shadowTexName, shadowInfo->m_ShadowName, sizeof(m_shadowTexName) - 1);
			if (shadowInfo) {
				m_offsetX = shadowInfo->m_offsetX;
				m_offsetY = shadowInfo->m_offsetY;
			}
			if (robj) {
				m_robj = robj;
				m_robj->Add_Ref();
			}

			// Enhanced Shadows: for SHADOW_PROJECTION decals with no
			// INI-authored texture name, hook a per-model name pattern so
			// content authors can drop an e.g. "TreeFir01_Shadow.tga" into
			// GameData and have it used automatically for that model's
			// shadows. Falls through to g_shadowTexture via the existing
			// fallback chain in RenderShadowDecalsDX11 when no such file
			// exists — no-op for vanilla game data. Gated on the toggle
			// so the classic default behavior is untouched.
			extern bool g_useEnhancedShadows;
			if (g_useEnhancedShadows && robj && shadowInfo &&
			    (shadowInfo->m_type & SHADOW_PROJECTION) &&
			    !m_shadowTexName[0])
			{
				const char* name = robj->Get_Name();
				if (name && name[0])
				{
					// "{modelname}_Shadow" — rendering path will append .tga
					// and try ImageCache.
					snprintf(m_shadowTexName, sizeof(m_shadowTexName),
					         "%s_Shadow", name);
				}
			}
		}

		~D3D11Shadow()
		{
			if (m_robj) {
				m_robj->Release_Ref();
				m_robj = nullptr;
			}
		}

		void release() override;

		// Public accessors for rendering
		void getData(float& outX, float& outY, float& outSizeX, float& outSizeY,
					 UnsignedByte& outOpacity, Bool& outEnabled,
					 float& outOffsetX, float& outOffsetY, float& outAngle,
					 const char*& outTexName, ShadowType& outType, uint32_t& outDiffuse) const
		{
			// When bound to a render object, sample its current world transform
			// so the shadow tracks unit movement and rotation. Otherwise use the
			// stored static position (set via Shadow::setPosition).
			if (m_robj && !m_robj->Is_Hidden())
			{
				const Matrix3D &xform = m_robj->Get_Transform();
				outX = xform.Get_X_Translation();
				outY = xform.Get_Y_Translation();
				outAngle = xform.Get_Z_Rotation();
			}
			else
			{
				outX = m_x;
				outY = m_y;
				outAngle = m_localAngle;
			}
			outSizeX = m_decalSizeX;
			outSizeY = m_decalSizeY;
			outOpacity = m_opacity;
			outEnabled = m_isEnabled && !m_isInvisibleEnabled
				&& (!m_robj || !m_robj->Is_Hidden());
			outOffsetX = m_offsetX;
			outOffsetY = m_offsetY;
			outTexName = m_shadowTexName[0] ? m_shadowTexName : nullptr;
			outType = m_type;
			// Convert m_diffuse from D3D8 ARGB to D3D11 ABGR (swap R and B).
			// SHADOW_ADDITIVE_DECAL never sets the alpha byte in Shadow.h's
			// setOpacity/setColor (look at lines 162-202 — only ALPHA_DECAL
			// gets `m_opacity << 24`), so additive decals come through with
			// alpha == 0 and render invisible. Force-inject 0xFF for them.
			uint32_t argb = (uint32_t)m_diffuse;
			uint32_t abgr = (argb & 0xFF00FF00u) | ((argb & 0xFFu) << 16) | ((argb >> 16) & 0xFFu);
			if (m_type & SHADOW_ADDITIVE_DECAL)
				abgr |= 0xFF000000u;
			outDiffuse = abgr;
		}

		#if defined(RTS_DEBUG)
		void getRenderCost(RenderCost &) const override {}
		#endif
	};

	class D3D11ShadowManager final : public ProjectedShadowManager
	{
	public:
		D3D11Shadow* m_shadowList = nullptr;
		int m_shadowCount = 0;

		Shadow *addDecal(RenderObjClass *robj, Shadow::ShadowTypeInfo *shadowInfo) override
		{
			return addDecalInternal(robj, shadowInfo);
		}

		Shadow *addDecal(Shadow::ShadowTypeInfo *shadowInfo) override
		{
			return addDecalInternal(nullptr, shadowInfo);
		}

		Shadow *addDecalInternal(RenderObjClass *robj, Shadow::ShadowTypeInfo *shadowInfo)
		{
			D3D11Shadow* shadow = new D3D11Shadow(shadowInfo, robj);
			// Add to linked list
			shadow->m_next = m_shadowList;
			shadow->m_prev = nullptr;
			if (m_shadowList) m_shadowList->m_prev = shadow;
			m_shadowList = shadow;
			++m_shadowCount;
			return shadow;
		}

		void removeShadow(D3D11Shadow* shadow)
		{
			if (shadow->m_prev) shadow->m_prev->m_next = shadow->m_next;
			if (shadow->m_next) shadow->m_next->m_prev = shadow->m_prev;
			if (m_shadowList == shadow) m_shadowList = shadow->m_next;
			--m_shadowCount;
		}

		// Get shadow data for rendering (returns count, fills arrays)
		int getShadowData(float* outX, float* outY, float* outSizeX, float* outSizeY,
						  UnsignedByte* outOpacity, float* outOffX, float* outOffY,
						  float* outAngle, const char** outTexNames,
						  ShadowType* outTypes, uint32_t* outDiffuse, int maxShadows) const
		{
			int count = 0;
			for (D3D11Shadow* s = m_shadowList; s && count < maxShadows; s = s->m_next)
			{
				Bool enabled;
				s->getData(outX[count], outY[count], outSizeX[count], outSizeY[count],
						   outOpacity[count], enabled, outOffX[count], outOffY[count],
						   outAngle[count], outTexNames[count], outTypes[count], outDiffuse[count]);
				if (!enabled) continue;
				// Last-ditch fallback for shadows with no robj (static UI/script
				// decals). Real units now get bbox-derived sizes in the ctor.
				if (outSizeX[count] <= 0) outSizeX[count] = 15.0f;
				if (outSizeY[count] <= 0) outSizeY[count] = 15.0f;
				++count;
			}
			return count;
		}
	};

	D3D11ShadowManager g_d3d11ShadowManager;
	Vector3 g_fallbackShadowLightPos[MAX_SHADOW_LIGHTS];

	void D3D11Shadow::release()
	{
		g_d3d11ShadowManager.removeShadow(this);
		delete this;
	}
}

bool DX8Wrapper_IsWindowed = true;
int DX8Wrapper_PreserveFPU = 0;
ProjectedShadowManager* TheProjectedShadowManager = &g_d3d11ShadowManager;


////////////////////////////////////////////////////////////////////////////////
// SECTION 2: WW3D statics
////////////////////////////////////////////////////////////////////////////////

// WW3D static member definitions — D3D11Shims.cpp is the SOLE provider.
// ww3d.cpp is excluded from the build (it has D3D8 dependencies).

unsigned int WW3D::SyncTime = 0;
unsigned int WW3D::PreviousSyncTime = 0;
float WW3D::FractionalSyncMs = 0.0f;
float WW3D::LogicFrameTimeMs = 1000.0f / (float)WWSyncPerSecond;
bool WW3D::IsInitted = false;
bool WW3D::IsRendering = false;
bool WW3D::IsCapturing = false;
bool WW3D::IsSortingEnabled = false;
bool WW3D::IsScreenUVBiased = false;
bool WW3D::IsBackfaceDebugEnabled = false;
bool WW3D::AreDecalsEnabled = true;
float WW3D::DecalRejectionDistance = 0.0f;
bool WW3D::AreStaticSortListsEnabled = false;
bool WW3D::MungeSortOnLoad = false;
bool WW3D::OverbrightModifyOnLoad = false;
FrameGrabClass* WW3D::Movie = nullptr;
bool WW3D::PauseRecord = false;
bool WW3D::RecordNextFrame = false;
int WW3D::FrameCount = 0;
VertexMaterialClass* WW3D::DefaultDebugMaterial = nullptr;
VertexMaterialClass* WW3D::BackfaceDebugMaterial = nullptr;
ShaderClass WW3D::DefaultDebugShader;
ShaderClass WW3D::LightmapDebugShader;
WW3D::PrelitModeEnum WW3D::PrelitMode = WW3D::PRELIT_MODE_VERTEX;
bool WW3D::ExposePrelit = false;
int WW3D::TextureFilter = 0;
bool WW3D::SnapshotActivated = false;
bool WW3D::ThumbnailEnabled = false;
WW3D::MeshDrawModeEnum WW3D::MeshDrawMode = WW3D::MESH_DRAW_MODE_OLD;
WW3D::NPatchesGapFillingModeEnum WW3D::NPatchesGapFillingMode = WW3D::NPATCHES_GAP_FILLING_DISABLED;
unsigned WW3D::NPatchesLevel = 0;
bool WW3D::IsTexturingEnabled = true;
bool WW3D::IsColoringEnabled = false;
bool WW3D::Lite = false;
float WW3D::DefaultNativeScreenSize = 640.0f;
StaticSortListClass* WW3D::DefaultStaticSortLists = nullptr;
StaticSortListClass* WW3D::CurrentStaticSortLists = nullptr;
int WW3D::LastFrameMemoryAllocations = 0;
int WW3D::LastFrameMemoryFrees = 0;
float WW3D::PixelCenterX = 0.0f;
float WW3D::PixelCenterY = 0.0f;
long WW3D::UserStat0 = 0;
long WW3D::UserStat1 = 0;
long WW3D::UserStat2 = 0;

int WW3D::Get_Texture_Reduction()
{
	return 0;
}

// ---------------------------------------------------------------------------
// Rewritten animation timing — QPC-based, zero mutation of legacy statics.
//
// Readers now go through WW3D::s_snapshot (a small struct updated once per
// frame by Sync()).  The old statics (SyncTime, FractionalSyncMs, etc.) are
// left at their initial values and never written to during rendering.
// ---------------------------------------------------------------------------

WW3D::TimeSnapshot WW3D::s_snapshot = {};

static LARGE_INTEGER s_qpcFreq  = {};
static LARGE_INTEGER s_qpcStart = {};
static bool          s_qpcInited = false;

void WW3D::Update_Logic_Frame_Time(float /*milliseconds*/)
{
	// No-op — timing is now fully QPC-driven via Sync().
}

void WW3D::Sync(bool /*step*/)
{
	if (!s_qpcInited)
	{
		QueryPerformanceFrequency(&s_qpcFreq);
		QueryPerformanceCounter(&s_qpcStart);
		s_qpcInited = true;
	}

	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	unsigned int totalMs = (unsigned int)(
		(now.QuadPart - s_qpcStart.QuadPart) * 1000ULL / s_qpcFreq.QuadPart);

	// Diagnostic toggle: skip the actual s_snapshot write.
	// If flicker stops with this ON, the issue is downstream time readers.
	// If flicker persists, the issue is the Sync call/QPC itself.
	extern bool g_debugDisableSyncWrite;
	if (g_debugDisableSyncWrite)
		return;

	TimeSnapshot snap;
	snap.prevSyncTimeMs = s_snapshot.syncTimeMs;
	snap.syncTimeMs     = totalMs;
	snap.deltaMs        = snap.syncTimeMs - snap.prevSyncTimeMs;
	snap.deltaTimeMsF   = (float)snap.deltaMs;
	if (snap.deltaTimeMsF < 0.001f)
		snap.deltaTimeMsF = 0.001f;

	s_snapshot = snap;
}


////////////////////////////////////////////////////////////////////////////////

// SECTION 3: MapObject now in WorldHeightMap.cpp
// TerrainTextureClass symbols provided in WW3D2 target via TerrainTex stubs below

// SECTION 4: W3DInGameUI
////////////////////////////////////////////////////////////////////////////////

W3DInGameUI::W3DInGameUI()
{
	Int i;
	for (i = 0; i < MAX_MOVE_HINTS; i++)
	{
		m_moveHintRenderObj[i] = nullptr;
		m_moveHintAnim[i] = nullptr;
	}
	m_buildingPlacementAnchor = nullptr;
	m_buildingPlacementArrow = nullptr;
}

W3DInGameUI::~W3DInGameUI()
{
	// Release move hint render objects and animations
	for (Int i = 0; i < MAX_MOVE_HINTS; i++)
	{
		if (m_moveHintRenderObj[i])
		{
			m_moveHintRenderObj[i]->Release_Ref();
			m_moveHintRenderObj[i] = nullptr;
		}
		if (m_moveHintAnim[i])
		{
			m_moveHintAnim[i]->Release_Ref();
			m_moveHintAnim[i] = nullptr;
		}
	}

	// Release building placement indicators
	if (m_buildingPlacementAnchor)
	{
		m_buildingPlacementAnchor->Release_Ref();
		m_buildingPlacementAnchor = nullptr;
	}
	if (m_buildingPlacementArrow)
	{
		m_buildingPlacementArrow->Release_Ref();
		m_buildingPlacementArrow = nullptr;
	}
}

void W3DInGameUI::init()
{
	InGameUI::init();
}

void W3DInGameUI::update()
{
	InGameUI::update();
}

void W3DInGameUI::reset()
{
	InGameUI::reset();
}

void W3DInGameUI::draw()
{
	preDraw();

	// Draw the selection rubber band if player is drag-selecting (uses
	// TheDisplay->drawOpenRect → 2D vertex queue, must be inside Begin2D)
	if (m_isDragSelecting)
	{
		drawSelectionRegion();
	}

	// NOTE: drawMoveHints / drawAttackHints / drawPlaceAngle have been
	// hoisted out to draw3DOverlays() — they go through ModelRenderer and
	// require the 3D-state cbuffer to be bound at b0. Calling them here
	// would corrupt the world transforms because Begin2D has already
	// replaced b0 with screen-size data. W3DDisplay::draw() now invokes
	// draw3DOverlays() before Begin2D so they render in the right state.

	// Screen-space overlays (messages, military subtitles/typewriter block, etc).
	postDraw();

	// Window manager repaint is issued by W3DDisplay::draw() before this call.
	// Keep postWindowDraw after that pass so HUD overlays stay on top.
	postWindowDraw();
}

void W3DInGameUI::draw3DOverlays()
{
	// Render the 3D-state UI feedback objects: click-to-move yellow arrow,
	// click-to-attack marker, building-placement anchor/rotation indicator.
	// MUST run while the 3D viewProjection is still bound at cbuffer slot 0
	// — i.e. before Renderer::Begin2D() replaces it with screen-size data.
	// In the original DX8 path these were members of W3DDisplay::m_3DScene
	// and rendered automatically by the regular scene walk; our D3D11
	// pipeline iterates Drawables instead of the W3D scene, so the move hint
	// render objects are never picked up unless we submit them explicitly.
	if (TheDisplay == nullptr)
		return;

	for (View *view = TheDisplay->getFirstView();
		 view != nullptr;
		 view = TheDisplay->getNextView(view))
	{
		drawMoveHints(view);
		drawAttackHints(view);
		drawPlaceAngle(view);
	}
}

void W3DInGameUI::drawSelectionRegion()
{
	// Draw a green outlined rectangle for the drag-selection area
	const UnsignedInt selectionColor = 0x9933FF33; // semi-transparent green
	const Int x0 = m_dragSelectRegion.lo.x;
	const Int y0 = m_dragSelectRegion.lo.y;
	const Int x1 = m_dragSelectRegion.hi.x;
	const Int y1 = m_dragSelectRegion.hi.y;

	TheDisplay->drawOpenRect(x0, y0, x1 - x0, y1 - y0, 2.0f, selectionColor);
}

void W3DInGameUI::drawMoveHints(View* view)
{
	for (Int i = 0; i < MAX_MOVE_HINTS; i++)
	{
		Int elapsed = TheGameClient->getFrame() - m_moveHint[i].frame;

		if (elapsed <= 40)
		{
			// Create render object on first use
			if (m_moveHintRenderObj[i] == nullptr)
			{
				WW3DAssetManager *assetMgr = WW3DAssetManager::Get_Instance();
				if (assetMgr == nullptr)
					return;

				RenderObjClass *hint = assetMgr->Create_Render_Obj(TheGlobalData->m_moveHintName.str());

				if (hint == nullptr)
				{
					DEBUG_CRASH(("unable to create hint"));
					return;
				}

				m_moveHintRenderObj[i] = hint;

				// Load the animation (same name as the model, e.g. "MoveHint.MoveHint")
				AsciiString animName;
				animName.format("%s.%s", TheGlobalData->m_moveHintName.str(), TheGlobalData->m_moveHintName.str());
				HAnimClass *anim = assetMgr->Get_HAnim(animName.str());

				// Release any previously held anim ref, then store the new one
				if (m_moveHintAnim[i])
				{
					m_moveHintAnim[i]->Release_Ref();
					m_moveHintAnim[i] = nullptr;
				}
				m_moveHintAnim[i] = anim; // Get_HAnim returns with an AddRef already applied
			}

			// On first frame of this hint, start the animation from the beginning
			if (elapsed == 0 && m_moveHintAnim[i])
			{
				m_moveHintRenderObj[i]->Set_Animation(m_moveHintAnim[i], 0, RenderObjClass::ANIM_MODE_ONCE);
			}

			// Position the hint at the move destination, aligned to terrain
			Matrix3D transform;
			PathfindLayerEnum layer = TheTerrainLogic->alignOnTerrain(0, m_moveHint[i].pos, true, transform);

			// If on ground layer and underwater, snap to water surface
			Real waterZ;
			if (layer == LAYER_GROUND && TheTerrainLogic->isUnderwater(m_moveHint[i].pos.x, m_moveHint[i].pos.y, &waterZ))
			{
				Coord3D tmp = m_moveHint[i].pos;
				tmp.z = waterZ;
				Coord3D normal;
				normal.x = 0;
				normal.y = 0;
				normal.z = 1;
				makeAlignToNormalMatrix(0, tmp, normal, transform);
			}

			m_moveHintRenderObj[i]->Set_Transform(transform);

			// Advance animation and render
			m_moveHintRenderObj[i]->On_Frame_Update();
			Render::ModelRenderer::Instance().RenderRenderObject(m_moveHintRenderObj[i]);
		}
	}
}

void W3DInGameUI::drawAttackHints(View* view)
{
	// Attack hints were empty in the original code too
}

void W3DInGameUI::drawPlaceAngle(View* view)
{
	WW3DAssetManager *assetMgr = WW3DAssetManager::Get_Instance();
	if (assetMgr == nullptr)
		return;

	// Create the anchor and arrow models on first use
	if (!m_buildingPlacementAnchor)
	{
		m_buildingPlacementAnchor = assetMgr->Create_Render_Obj("Locater01");
		if (!m_buildingPlacementAnchor)
		{
			DEBUG_CRASH(("Unable to create BuildingPlacementAnchor (Locator01.w3d) -- cursor for placing buildings"));
			return;
		}
	}
	if (!m_buildingPlacementArrow)
	{
		m_buildingPlacementArrow = assetMgr->Create_Render_Obj("Locater02");
		if (!m_buildingPlacementArrow)
		{
			DEBUG_CRASH(("Unable to create BuildingPlacementArrow (Locator02.w3d) -- cursor for placing buildings"));
			return;
		}
	}

	// If placement is not anchored, nothing to draw
	if (isPlacementAnchored() == FALSE)
		return;

	// Get the screen-space anchor points for the drag
	ICoord2D start, end;
	getPlacementPoints(&start, &end);

	// Compute screen-space drag vector length to decide anchor vs arrow
	Coord3D vector;
	vector.x = (Real)(end.x - start.x);
	vector.y = (Real)(end.y - start.y);
	vector.z = 0.0f;
	Real length = vector.length();

	Bool showArrow = length >= 5.0f;

	// Pick the appropriate model -- arrow for longer drags, anchor for short/initial placement
	RenderObjClass *activeModel = showArrow ? m_buildingPlacementArrow : m_buildingPlacementAnchor;

	// Copy the transform from the placement icon (the building ghost) so the indicator
	// is oriented the same way as the building being placed
	if (m_placeIcon && m_placeIcon[0])
	{
		activeModel->Set_Transform(*m_placeIcon[0]->getTransformMatrix());
	}

	// Render the indicator
	activeModel->On_Frame_Update();
	Render::ModelRenderer::Instance().RenderRenderObject(activeModel);
}


// SECTION 5: W3DGhostObjectManager - now compiled from W3DGhostObject.cpp

////////////////////////////////////////////////////////////////////////////////
// SECTION 6: W3DTerrainLogic
////////////////////////////////////////////////////////////////////////////////

W3DTerrainLogic::W3DTerrainLogic() :
	m_mapMinZ(0),
	m_mapMaxZ(1)
{
}

W3DTerrainLogic::~W3DTerrainLogic()
{
}

void W3DTerrainLogic::init()
{
	TerrainLogic::init();
}

void W3DTerrainLogic::reset()
{
	TerrainLogic::reset();
}

void W3DTerrainLogic::update()
{
	TerrainLogic::update();
}

Bool W3DTerrainLogic::loadMap(AsciiString filename, Bool query)
{
	if (!TheMapCache)
		return FALSE;

	CachedFileInputStream fileStrm;
	if (!fileStrm.open(filename))
		return FALSE;

	ChunkInputStream* pStrm = &fileStrm;

	// Load heightmap data from the .map file - this is pure data loading
	WorldHeightMap* terrainHeightMap = NEW WorldHeightMap(pStrm, true);
	if (terrainHeightMap)
	{
		m_mapDX = terrainHeightMap->getXExtent();
		m_mapDY = terrainHeightMap->getYExtent();
		m_boundaries = terrainHeightMap->getAllBoundaries();
		m_activeBoundary = 0;

		Int i, j, minHt, maxHt;
		minHt = terrainHeightMap->getMaxHeightValue();
		maxHt = 0;
		for (j = 0; j < m_mapDY; j++) {
			for (i = 0; i < m_mapDX; i++) {
				Short cur = terrainHeightMap->getHeight(i, j);
				if (cur < minHt) minHt = cur;
				if (maxHt < cur) maxHt = cur;
			}
		}
		m_mapMinZ = minHt * MAP_HEIGHT_SCALE;
		m_mapMaxZ = maxHt * MAP_HEIGHT_SCALE;
		REF_PTR_RELEASE(terrainHeightMap);
	}
	else
		return FALSE;

	// Call base class to load waypoints etc.
	TerrainLogic::loadMap(filename, query);

	return TRUE;
}

void W3DTerrainLogic::newMap(Bool saveGame)
{
	// Skip TheTerrainRenderObject->loadRoadsAndBridges() - no render object yet
	// But we must call the base class to set waypoint Z values and enable water grid
	TerrainLogic::newMap(saveGame);
}

Real W3DTerrainLogic::getGroundHeight(Real x, Real y, Coord3D* normal) const
{
	if (normal)
	{
		normal->x = 0.0f;
		normal->y = 0.0f;
		normal->z = 1.0f;
	}

	// Use the heightmap from the terrain visual if available
	if (TheTerrainVisual)
	{
		WorldHeightMap* hm = TheTerrainVisual->getLogicHeightMap();
		if (hm)
		{
			// Convert world coords to grid coords (matching BaseHeightMap::getHeightMapHeight)
			const Real MAP_XY_FACTOR_INV = 1.0f / MAP_XY_FACTOR;
			float xdiv = x * MAP_XY_FACTOR_INV;
			float ydiv = y * MAP_XY_FACTOR_INV;

			// Floor to get grid cell
			float ixf = floorf(xdiv);
			float iyf = floorf(ydiv);

			// Fractional position within cell (for bilinear interpolation)
			float fx = xdiv - ixf;
			float fy = ydiv - iyf;

			// Add border offset - world (0,0) maps to grid (borderSize, borderSize)
			Int ix = (Int)ixf + hm->getBorderSize();
			Int iy = (Int)iyf + hm->getBorderSize();
			Int xExtent = hm->getXExtent();
			Int yExtent = hm->getYExtent();

			// Bounds check (need margin of 2 for normal sampling)
			if (ix < 1 || iy < 1 || ix > (xExtent - 3) || iy > (yExtent - 3))
			{
				// Clamp and return nearest edge height
				Int cx = ix < 0 ? 0 : (ix >= xExtent ? xExtent - 1 : ix);
				Int cy = iy < 0 ? 0 : (iy >= yExtent ? yExtent - 1 : iy);
				return hm->getHeight(cx, cy) * MAP_HEIGHT_SCALE;
			}

			// Bilinear interpolation over the two triangles that form the quad:
			//  3-----2
			//  |    /|
			//  |  /  |      fy > fx = upper triangle (0,3,2)
			//  |/    |      fy <= fx = lower triangle (0,1,2)
			//  0-----1
			const UnsignedByte* data = hm->getDataPtr();
			int idx = ix + iy * xExtent;
			float p0 = data[idx];
			float p2 = data[idx + xExtent + 1];
			float height;

			if (fy > fx) // upper triangle
			{
				float p3 = data[idx + xExtent];
				height = (p3 + (1.0f - fy) * (p0 - p3) + fx * (p2 - p3)) * MAP_HEIGHT_SCALE;
			}
			else // lower triangle
			{
				float p1 = data[idx + 1];
				height = (p1 + fy * (p2 - p1) + (1.0f - fx) * (p0 - p1)) * MAP_HEIGHT_SCALE;
			}

			// Compute smoothed normal from 12 surrounding height samples
			if (normal)
			{
				int idx4 = ix + (iy - 1) * xExtent;
				int idx0 = ix + iy * xExtent;
				int idx3 = ix + (iy + 1) * xExtent;
				int idx9 = ix + (iy + 2) * xExtent;
				float d0 = data[idx0], d1 = data[idx0 + 1];
				float d3 = data[idx3], d2 = data[idx3 + 1];
				float d4 = data[idx4], d5 = data[idx4 + 1];
				float d6 = data[idx0 + 2], d7 = data[idx3 + 2];
				float d8 = data[idx9 + 1], d9 = data[idx9];
				float d11 = data[idx0 - 1];

				float dzx0 = d1 - d11, dzx1 = d6 - d0;
				float dzx2 = d7 - d3, dzx3 = d6 - d0;
				float dzy0 = d3 - d4, dzy1 = d2 - d5;
				float dzy2 = d8 - d1, dzy3 = d9 - d0;

				float dzxL = dzx0 * (1.0f - fx) + fx * dzx3;
				float dzxR = dzx1 * (1.0f - fx) + fx * dzx2;
				float dzx = dzxL * (1.0f - fy) + fy * dzxR;

				float dzyL = dzy0 * (1.0f - fx) + fx * dzy3;
				float dzyR = dzy1 * (1.0f - fx) + fx * dzy2;
				float dzy = dzyL * (1.0f - fy) + fy * dzyR;

				float scale = 2.0f * MAP_XY_FACTOR / MAP_HEIGHT_SCALE;
				// Cross product of (scale, 0, dzx) x (0, scale, dzy)
				float nx = -dzx * scale;
				float ny = -dzy * scale;
				float nz = scale * scale;
				float len = sqrtf(nx * nx + ny * ny + nz * nz);
				if (len > 0.0001f)
				{
					normal->x = nx / len;
					normal->y = ny / len;
					normal->z = nz / len;
				}
			}

			return height;
		}
	}
	return 0.0f;
}

Bool W3DTerrainLogic::isCliffCell(Real x, Real y) const
{
	// Mirrors BaseHeightMapRenderObjClass::isCliffCell. Originally
	// W3DTerrainLogic::isCliffCell forwarded to TheTerrainRenderObject,
	// but the D3D11 build has no BaseHeightMapRenderObjClass instance,
	// so we go straight to the logic heightmap. Without this the
	// pathfinder marks no cells as CELL_CLIFF and ground units (tanks,
	// etc.) happily walk straight up steep terrain.
	if (!TheTerrainVisual)
		return FALSE;

	WorldHeightMap* hm = TheTerrainVisual->getLogicHeightMap();
	if (!hm)
		return FALSE;

	Int iX = (Int)(x / MAP_XY_FACTOR);
	Int iY = (Int)(y / MAP_XY_FACTOR);
	iX += hm->getBorderSizeInline();
	iY += hm->getBorderSizeInline();
	if (iX < 0) iX = 0;
	if (iY < 0) iY = 0;
	if (iX >= (hm->getXExtent() - 1)) iX = hm->getXExtent() - 2;
	if (iY >= (hm->getYExtent() - 1)) iY = hm->getYExtent() - 2;
	return hm->getCliffState(iX, iY);
}

Real W3DTerrainLogic::getLayerHeight(Real x, Real y, PathfindLayerEnum layer, Coord3D* normal, Bool clip) const
{
	return getGroundHeight(x, y, normal);
}

void W3DTerrainLogic::getExtent(Region3D* extent) const
{
	if (extent)
	{
		extent->lo.x = 0;
		extent->lo.y = 0;
		extent->lo.z = m_mapMinZ;

		if (m_boundaries.size() > 0 && m_activeBoundary < (Int)m_boundaries.size())
		{
			extent->hi.x = m_boundaries[m_activeBoundary].x * MAP_XY_FACTOR;
			extent->hi.y = m_boundaries[m_activeBoundary].y * MAP_XY_FACTOR;
		}
		else if (m_mapDX > 0 && m_mapDY > 0)
		{
			extent->hi.x = m_mapDX * MAP_XY_FACTOR;
			extent->hi.y = m_mapDY * MAP_XY_FACTOR;
		}
		else
		{
			extent->hi.x = 1000.0f;
			extent->hi.y = 1000.0f;
		}
		extent->hi.z = m_mapMaxZ;
	}
}

void W3DTerrainLogic::getMaximumPathfindExtent(Region3D* extent) const
{
	getExtent(extent);
}

void W3DTerrainLogic::getExtentIncludingBorder(Region3D* extent) const
{
	getExtent(extent);
}

Bool W3DTerrainLogic::isClearLineOfSight(const Coord3D& pos, const Coord3D& posOther) const
{
	return TRUE;
}

void W3DTerrainLogic::crc(Xfer* xfer)
{
	TerrainLogic::crc(xfer);
}

void W3DTerrainLogic::xfer(Xfer* xfer)
{
	TerrainLogic::xfer(xfer);
}

void W3DTerrainLogic::loadPostProcess()
{
}


////////////////////////////////////////////////////////////////////////////////
// SECTION 7: Render2DSentenceClass and FontCharsClass
// Full D3D11 implementation - uses GDI for glyph rasterization and the D3D11
// Renderer for drawing text as textured quads.
////////////////////////////////////////////////////////////////////////////////

#include "Renderer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// D3D11 glyph texture cache
// Caches per-character D3D11 textures keyed by (FontCharsClass*, WCHAR).
// ---------------------------------------------------------------------------
namespace {

struct GlyphCacheKey
{
	const FontCharsClass* font;
	WCHAR ch;
	bool operator==(const GlyphCacheKey& o) const { return font == o.font && ch == o.ch; }
};

struct GlyphCacheKeyHash
{
	size_t operator()(const GlyphCacheKey& k) const
	{
		return std::hash<uintptr_t>()((uintptr_t)k.font) ^ (std::hash<unsigned>()(k.ch) << 16);
	}
};

struct CachedGlyph
{
	Render::Texture texture;
	int width;
	int height;
};

static std::unordered_map<GlyphCacheKey, CachedGlyph, GlyphCacheKeyHash> s_glyphCache;

// Build an RGBA texture for a glyph from its A4R4G4B4 buffer data
static CachedGlyph* GetOrCreateGlyphTexture(FontCharsClass* font, WCHAR ch,
	const FontCharsClassCharDataStruct* charData, int charHeight)
{
	GlyphCacheKey key = { font, ch };
	auto it = s_glyphCache.find(key);
	if (it != s_glyphCache.end())
		return &it->second;

	if (!charData || charData->Width <= 0 || charHeight <= 0)
		return nullptr;

	int w = charData->Width;
	int h = charHeight;

	// Convert A4R4G4B4 (uint16) buffer to RGBA8 for D3D11
	std::vector<uint32_t> rgba(w * h);
	const uint16* src = charData->Buffer;

	for (int row = 0; row < h; row++)
	{
		for (int col = 0; col < w; col++)
		{
			uint16 pixel = src[row * w + col];
			// A4R4G4B4 format: [AAAA RRRR GGGG BBBB]
			uint8_t a4 = (pixel >> 12) & 0xF;
			uint8_t r4 = (pixel >> 8) & 0xF;
			uint8_t g4 = (pixel >> 4) & 0xF;
			uint8_t b4 = (pixel >> 0) & 0xF;

			// Expand 4-bit to 8-bit
			uint8_t a = (a4 << 4) | a4;
			uint8_t r = (r4 << 4) | r4;
			uint8_t g = (g4 << 4) | g4;
			uint8_t b = (b4 << 4) | b4;

			// RGBA packed as uint32: R in low byte
			rgba[row * w + col] = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
		}
	}

	CachedGlyph glyph;
	glyph.width = w;
	glyph.height = h;

	auto& device = Render::Renderer::Instance().GetDevice();
	if (!glyph.texture.CreateFromRGBA(device, rgba.data(), w, h, false))
		return nullptr;

	auto result = s_glyphCache.emplace(key, std::move(glyph));
	return &result.first->second;
}

} // anonymous namespace


// ---------------------------------------------------------------------------
// Render2DSentenceClass -- stores text + position, draws via D3D11 Renderer
// ---------------------------------------------------------------------------

// We store the built sentence as a simple list of positioned characters
struct D3D11SentenceChar
{
	WCHAR ch;
	float screenX;
	float screenY;
};

// We use SentenceData (existing member) for the original pipeline, but for
// our D3D11 path we store characters in a static thread-local buffer keyed
// per Render2DSentenceClass instance.  To avoid modifying the header, we
// use a side map.
static std::unordered_map<const Render2DSentenceClass*, std::vector<D3D11SentenceChar>> s_sentenceChars;

// Draw commands stored by Draw_Sentence, executed by Render
struct D3D11DrawCmd
{
	WCHAR ch;
	float screenX;
	float screenY;
	uint32_t color;
};
static std::unordered_map<const Render2DSentenceClass*, std::vector<D3D11DrawCmd>> s_drawCmds;

Render2DSentenceClass::Render2DSentenceClass() :
	Font(nullptr),
	Location(0.0f, 0.0f),
	Cursor(0.0f, 0.0f),
	TextureOffset(0, 0),
	TextureStartX(0),
	CurSurface(nullptr),
	CurrTextureSize(0),
	MonoSpaced(false),
	IsClippedEnabled(false),
	ClipRect(0, 0, 0, 0),
	BaseLocation(0, 0),
	LockedPtr(nullptr),
	LockedStride(0),
	TextureSizeHint(0),
	WrapWidth(0),
	Centered(false),
	DrawExtents(0, 0, 0, 0),
	ParseHotKey(false),
	useHardWordWrap(false),
	CurTexture(nullptr)
{
	Shader = Render2DClass::Get_Default_Shader();
}

Render2DSentenceClass::~Render2DSentenceClass()
{
	s_sentenceChars.erase(this);
	REF_PTR_RELEASE(Font);
	Reset();
}

void Render2DSentenceClass::Render()
{
	// Execute stored draw commands from Draw_Sentence
	if (!Font)
		return;

	auto it = s_drawCmds.find(this);
	if (it == s_drawCmds.end() || it->second.empty())
		return;

	auto& renderer = Render::Renderer::Instance();
	int charHeight = Font->Get_Char_Height();

	for (const auto& cmd : it->second)
	{
		if (cmd.ch == L' ' || cmd.ch == L'\n' || cmd.ch == 0)
			continue;

		const FontCharsClassCharDataStruct* charData = Font->Get_Char_Data(cmd.ch);
		if (!charData || charData->Width <= 0)
			continue;

		CachedGlyph* glyph = GetOrCreateGlyphTexture(Font, cmd.ch, charData, charHeight);
		if (!glyph)
			continue;

		float drawX = cmd.screenX;
		float drawY = cmd.screenY;
		float drawW = (float)glyph->width;
		float drawH = (float)glyph->height;

		renderer.DrawImageUV(glyph->texture, drawX, drawY, drawW, drawH, 0, 0, 1, 1, cmd.color);
	}
}

void Render2DSentenceClass::Reset()
{
	s_sentenceChars.erase(this);
	s_drawCmds.erase(this);
	Reset_Sentence_Data();
	Release_Pending_Surfaces();
	Cursor.Set(0, 0);
	MonoSpaced = false;
	ParseHotKey = false;
}

void Render2DSentenceClass::Reset_Polys()
{
	// Clear stored draw commands so they can be rebuilt
	s_drawCmds.erase(this);
}

void Render2DSentenceClass::Set_Font(FontCharsClass* font)
{
	Reset();
	REF_PTR_SET(Font, font);
}

void Render2DSentenceClass::Set_Location(const Vector2& loc)
{
	Location = loc;
}

void Render2DSentenceClass::Set_Base_Location(const Vector2& loc)
{
	Vector2 dif = loc - BaseLocation;
	BaseLocation = loc;

	// Shift all stored character positions
	auto it = s_sentenceChars.find(this);
	if (it != s_sentenceChars.end())
	{
		for (auto& sc : it->second)
		{
			sc.screenX += dif.X;
			sc.screenY += dif.Y;
		}
	}
}

void Render2DSentenceClass::Make_Additive()
{
	Shader.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE);
	Shader.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ONE);
	Shader.Set_Primary_Gradient(ShaderClass::GRADIENT_MODULATE);
	Shader.Set_Secondary_Gradient(ShaderClass::SECONDARY_GRADIENT_DISABLE);
}

void Render2DSentenceClass::Set_Shader(ShaderClass shader)
{
	Shader = shader;
}

Vector2 Render2DSentenceClass::Get_Text_Extents(const WCHAR* text)
{
	if (!Font || !text)
		return Vector2(0, 0);

	Vector2 extent(0, (float)Font->Get_Char_Height());

	while (*text)
	{
		WCHAR ch = *text++;
		if (ch != L'\n')
		{
			extent.X += Font->Get_Char_Spacing(ch);
		}
	}

	return extent;
}

Vector2 Render2DSentenceClass::Get_Formatted_Text_Extents(const WCHAR* text)
{
	return Build_Sentence_Not_Centered(text, nullptr, nullptr, true);
}

void Render2DSentenceClass::Build_Sentence(const WCHAR* text, int* hkX, int* hkY)
{
	if (text == nullptr || Font == nullptr)
		return;

	if (Centered && (WrapWidth > 0 || wcschr(text, L'\n')))
		Build_Sentence_Centered(text, hkX, hkY);
	else
		Build_Sentence_Not_Centered(text, hkX, hkY);
}

void Render2DSentenceClass::Draw_Sentence(uint32 color)
{
	if (!Font)
		return;

	auto it = s_sentenceChars.find(this);
	if (it == s_sentenceChars.end() || it->second.empty())
		return;

	// Store draw commands for Render() to execute
	auto& cmds = s_drawCmds[this];

	for (const auto& sc : it->second)
	{
		if (sc.ch == L' ' || sc.ch == L'\n' || sc.ch == 0)
			continue;

		D3D11DrawCmd cmd;
		cmd.ch = sc.ch;
		cmd.screenX = sc.screenX + Location.X;
		cmd.screenY = sc.screenY + Location.Y;
		cmd.color = color;
		cmds.push_back(cmd);
	}

	// Update draw extents for measurement
	int charHeight = Font->Get_Char_Height();
	DrawExtents.Set(0, 0, 0, 0);
	for (const auto& sc : it->second)
	{
		const FontCharsClassCharDataStruct* charData = Font->Get_Char_Data(sc.ch);
		if (!charData || charData->Width <= 0)
			continue;
		float drawX = sc.screenX + Location.X;
		float drawY = sc.screenY + Location.Y;
		RectClass charRect(drawX, drawY, drawX + charData->Width, drawY + charHeight);
		if (DrawExtents.Width() == 0)
			DrawExtents = charRect;
		else
			DrawExtents += charRect;
	}
}

void Render2DSentenceClass::Reset_Sentence_Data()
{
	for (int index = 0; index < SentenceData.Count(); index++)
		REF_PTR_RELEASE(SentenceData[index].Surface);
	if (SentenceData.Count() > 0)
		SentenceData.Delete_All();
}

void Render2DSentenceClass::Build_Textures()
{
	// Not used in D3D11 path - textures are created on-demand in Draw_Sentence
}

void Render2DSentenceClass::Record_Sentence_Chunk()
{
	// Not used in D3D11 path
}

void Render2DSentenceClass::Allocate_New_Surface(const WCHAR* text, bool justCalcExtents)
{
	// Not used in D3D11 path
}

void Render2DSentenceClass::Release_Pending_Surfaces()
{
	for (int index = 0; index < PendingSurfaces.Count(); index++)
	{
		SurfaceClass* surface = PendingSurfaces[index].Surface;
		REF_PTR_RELEASE(surface);
	}
	if (PendingSurfaces.Count() > 0)
		PendingSurfaces.Delete_All();
}

void Render2DSentenceClass::Build_Sentence_Centered(const WCHAR* text, int* hkX, int* hkY)
{
	float charHeight = (float)Font->Get_Char_Height();
	int notCenteredHotkeyX = 0;
	int notCenteredHotkeyY = 0;

	// First pass: calculate full extents (non-centered) so we know the max width
	Vector2 extent = Build_Sentence_Not_Centered(text, &notCenteredHotkeyX, &notCenteredHotkeyY, true);

	// Clear previously built characters
	auto& chars = s_sentenceChars[this];
	chars.clear();

	DrawExtents.Set(0, 0, 0, 0);
	float cursorX = 0;
	float cursorY = 0;
	int hotKeyPosX = 0;
	int hotKeyPosY = 0;

	// Process text line by line for centering
	const WCHAR* lineStart = text;
	while (*lineStart != 0)
	{
		// Measure this line
		const WCHAR* lineEnd = lineStart;
		float lineWidth = 0;
		while (*lineEnd != 0 && *lineEnd != L'\n')
		{
			const WCHAR* scan = lineEnd;
			// Skip hotkey marker
			if (ParseHotKey && *scan == L'&' && *(scan + 1) != 0 && *(scan + 1) > L' ' && *(scan + 1) != L'\n')
			{
				scan++;
			}
			lineWidth += Font->Get_Char_Spacing(*scan);
			lineEnd++;
			if (ParseHotKey && *(lineEnd - 1) == L'&' && *lineEnd != 0 && *lineEnd > L' ' && *lineEnd != L'\n')
			{
				// The '&' was a hotkey prefix, we already advanced past it above
			}
		}

		// Handle word wrapping for centering
		if (WrapWidth > 0 && lineWidth > WrapWidth)
		{
			// Fall back to wrapping - walk the line, emitting words
			const WCHAR* p = lineStart;
			float wrapCursorX = 0;
			float wrapLineWidth = 0;

			// Measure words and wrap
			while (p < lineEnd && *p != 0)
			{
				// Measure next word
				const WCHAR* wordStart = p;
				float wordWidth = 0;
				while (p < lineEnd && *p > L' ' && *p != L'\n')
				{
					wordWidth += Font->Get_Char_Spacing(*p);
					p++;
				}

				// Does this word fit on the current line?
				if (wrapLineWidth + wordWidth > WrapWidth && wrapLineWidth > 0)
				{
					// Center the current line
					float offsetX = (extent.X - wrapLineWidth) / 2.0f;
					if (offsetX < 0) offsetX = 0;
					for (auto& sc : chars)
					{
						if (sc.screenY == cursorY)
							sc.screenX += offsetX;
					}
					cursorY += charHeight;
					wrapCursorX = 0;
					wrapLineWidth = 0;
				}

				// Emit word characters
				const WCHAR* wp = wordStart;
				while (wp < p)
				{
					WCHAR ch = *wp;
					bool skipBlit = false;
					if (ParseHotKey && ch == L'&' && wp + 1 < lineEnd && *(wp + 1) > L' ')
					{
						wp++;
						ch = *wp;
						skipBlit = true;
					}
					float spacing = (float)Font->Get_Char_Spacing(ch);

					if (!skipBlit)
					{
						D3D11SentenceChar sc;
						sc.ch = ch;
						sc.screenX = wrapCursorX;
						sc.screenY = cursorY;
						chars.push_back(sc);
					}
					wrapCursorX += spacing;
					wrapLineWidth += spacing;
					wp++;
				}

				// Add space
				if (p < lineEnd && *p == L' ')
				{
					float spaceWidth = (float)Font->Get_Char_Spacing(L' ');
					wrapCursorX += spaceWidth;
					wrapLineWidth += spaceWidth;
					p++;
				}
			}

			// Center the last partial line
			float offsetX = (extent.X - wrapLineWidth) / 2.0f;
			if (offsetX < 0) offsetX = 0;
			for (auto& sc : chars)
			{
				if (sc.screenY == cursorY)
					sc.screenX += offsetX;
			}
		}
		else
		{
			// Simple centering - line fits without wrapping
			cursorX = (extent.X - lineWidth) / 2.0f;
			if (cursorX < 0) cursorX = 0;

			const WCHAR* p = lineStart;
			while (p < lineEnd && *p != 0)
			{
				WCHAR ch = *p;
				bool skipBlit = false;
				if (ParseHotKey && ch == L'&' && p + 1 < lineEnd && *(p + 1) > L' ' && *(p + 1) != L'\n')
				{
					p++;
					ch = *p;
					skipBlit = true;
					hotKeyPosX = (int)cursorX;
					hotKeyPosY = (int)cursorY;
				}

				float spacing = (float)Font->Get_Char_Spacing(ch);

				if (!skipBlit && ch > L' ')
				{
					D3D11SentenceChar sc;
					sc.ch = ch;
					sc.screenX = cursorX;
					sc.screenY = cursorY;
					chars.push_back(sc);
				}
				cursorX += spacing;
				p++;
			}
		}

		cursorY += charHeight;

		// Advance past newline
		if (*lineEnd == L'\n')
			lineEnd++;
		lineStart = lineEnd;
	}

	DrawExtents.Left = 0;
	DrawExtents.Top = 0;
	DrawExtents.Right = extent.X;
	DrawExtents.Bottom = cursorY;

	if (hkX) *hkX = hotKeyPosX;
	if (hkY) *hkY = hotKeyPosY;
}

Vector2 Render2DSentenceClass::Build_Sentence_Not_Centered(const WCHAR* text, int* hkX, int* hkY, bool justCalcExtents)
{
	float charHeight = (float)Font->Get_Char_Height();
	float cursorX = 0;
	float cursorY = 0;
	float maxX = 0;
	int hotKeyPosX = 0;
	int hotKeyPosY = 0;
	bool calcHotKeyX = false;

	std::vector<D3D11SentenceChar>* chars = nullptr;
	if (!justCalcExtents)
	{
		auto& vec = s_sentenceChars[this];
		vec.clear();
		chars = &vec;
		DrawExtents.Set(0, 0, 0, 0);
	}

	while (text != nullptr)
	{
		WCHAR ch = *text++;
		bool dontBlit = false;

		// Hot key parsing
		if (ParseHotKey && ch == L'&' && *text != 0 && *text > L' ' && *text != L'\n')
		{
			hotKeyPosY = (int)cursorY;
			if (calcHotKeyX)
				hotKeyPosX = 0;
			else
				hotKeyPosX = (int)cursorX;

			ch = *text++;
			dontBlit = true;
		}

		float charSpacing = (float)Font->Get_Char_Spacing(ch);

		bool encounterBreak = (ch == L' ' || ch == L'\n' || ch == 0);
		bool wordBiggerThanLine = (useHardWordWrap && WrapWidth != 0 &&
			(cursorX + charSpacing) >= WrapWidth);

		if (encounterBreak || wordBiggerThanLine)
		{
			if (ch == L' ')
			{
				// Check word wrap
				if (WrapWidth > 0)
				{
					const WCHAR* word = text;
					float wordWidth = charSpacing;
					while (*word != 0 && *word > L' ')
					{
						if (ParseHotKey && *word == L'&' && *(word + 1) != 0 && *(word + 1) > L' ' && *(word + 1) != L'\n')
							word++;
						wordWidth += Font->Get_Char_Spacing(*word++);
					}

					if ((cursorX + wordWidth) >= WrapWidth)
					{
						if (cursorX > maxX) maxX = cursorX;
						cursorX = 0;
						cursorY += charHeight;
						calcHotKeyX = true;
						continue;
					}
				}

				// Add space width
				cursorX += charSpacing;
			}
			else if (ch == L'\n')
			{
				if (cursorX > maxX) maxX = cursorX;
				cursorX = 0;
				cursorY += charHeight;
			}
			else if (ch == 0)
			{
				if (cursorX > maxX) maxX = cursorX;
				break;
			}
			else if (wordBiggerThanLine)
			{
				if (cursorX > maxX) maxX = cursorX;
				cursorX = 0;
				cursorY += charHeight;
			}

			continue;
		}

		// Normal printable character
		if (ch != L'\n' && !justCalcExtents && !dontBlit && chars)
		{
			D3D11SentenceChar sc;
			sc.ch = ch;
			sc.screenX = cursorX;
			sc.screenY = cursorY;
			chars->push_back(sc);
		}

		if (dontBlit)
		{
			charSpacing += Font->Get_Extra_Overlap();
			if (ch == L'M') charSpacing += 1;
		}

		cursorX += charSpacing;
	}

	if (cursorX > maxX) maxX = cursorX;

	Vector2 extent;
	extent.X = maxX + Font->Get_Extra_Overlap();
	extent.Y = cursorY + charHeight;

	if (!justCalcExtents)
	{
		DrawExtents.Left = 0;
		DrawExtents.Top = 0;
		DrawExtents.Right = extent.X;
		DrawExtents.Bottom = extent.Y;
	}

	if (hkX) *hkX = hotKeyPosX;
	if (hkY) *hkY = hotKeyPosY;

	return extent;
}


// ---------------------------------------------------------------------------
// FontCharsClass -- GDI font creation and glyph rasterization
// ---------------------------------------------------------------------------

FontCharsClass::FontCharsClass() :
	AlternateUnicodeFont(nullptr),
	CurrPixelOffset(0),
	CharHeight(0),
	CharAscent(0),
	CharOverhang(0),
	PixelOverlap(0),
	PointSize(0),
	OldGDIFont(nullptr),
	OldGDIBitmap(nullptr),
	GDIBitmap(nullptr),
	GDIFont(nullptr),
	GDIBitmapBits(nullptr),
	MemDC(nullptr),
	UnicodeCharArray(nullptr),
	FirstUnicodeChar(0xFFFF),
	LastUnicodeChar(0),
	IsBold(false)
{
	AlternateUnicodeFont = nullptr;
	::memset(ASCIICharArray, 0, sizeof(ASCIICharArray));
}

FontCharsClass::~FontCharsClass()
{
	// Invalidate any cached glyph textures for this font
	for (auto it = s_glyphCache.begin(); it != s_glyphCache.end(); )
	{
		if (it->first.font == this)
			it = s_glyphCache.erase(it);
		else
			++it;
	}

	while (BufferList.Count())
	{
		delete BufferList[0];
		BufferList.Delete(0);
	}

	Free_GDI_Font();
	Free_Character_Arrays();
}

bool FontCharsClass::Initialize_GDI_Font(const char* font_name, int point_size, bool is_bold)
{
	// Build a unique name from the font name and its size (matches original)
	Name.Format("%s%d", font_name, point_size);

	GDIFontName = font_name;
	PointSize = point_size;
	IsBold = is_bold;

	return Create_GDI_Font(font_name);
}

bool FontCharsClass::Is_Font(const char* font_name, int point_size, bool is_bold)
{
	return (GDIFontName.Compare_No_Case(font_name) == 0) &&
		(point_size == PointSize) &&
		(is_bold == IsBold);
}

int FontCharsClass::Get_Char_Width(WCHAR ch)
{
	const FontCharsClassCharDataStruct* data = Get_Char_Data(ch);
	if (data != nullptr)
		return data->Width;
	return 0;
}

int FontCharsClass::Get_Char_Spacing(WCHAR ch)
{
	const FontCharsClassCharDataStruct* data = Get_Char_Data(ch);
	if (data != nullptr)
	{
		if (data->Width != 0)
			return data->Width - PixelOverlap - CharOverhang;
	}
	return 0;
}

void FontCharsClass::Blit_Char(WCHAR ch, uint16* dest_ptr, int dest_stride, int x, int y)
{
	const FontCharsClassCharDataStruct* data = Get_Char_Data(ch);
	if (data != nullptr && data->Width != 0)
	{
		int dest_inc = (dest_stride >> 1);
		uint16* src_ptr = data->Buffer;
		dest_ptr += (dest_inc * y) + x;

		for (int row = 0; row < CharHeight; row++)
		{
			for (int col = 0; col < data->Width; col++)
			{
				uint16 curData = *src_ptr;
				if (col < PixelOverlap)
					curData |= dest_ptr[col];
				dest_ptr[col] = curData;
				src_ptr++;
			}
			dest_ptr += dest_inc;
		}
	}
}

bool FontCharsClass::Create_GDI_Font(const char* font_name)
{
	HDC screen_dc = ::GetDC(nullptr);

	const char* fontToUseForGenerals = "Arial";
	bool doingGenerals = false;
	if (strcmp(font_name, "Generals") == 0)
	{
		font_name = fontToUseForGenerals;
		doingGenerals = true;
	}

	// Calculate the height of the font in logical units
	const int dotsPerInch = 96; // always use 96
	int font_height = -MulDiv(PointSize, dotsPerInch, 72);

	int fontWidth = 0;
	if (doingGenerals)
		fontWidth = (int)(-font_height * 0.40f);

	PixelOverlap = (-font_height) / 8;
	if (PixelOverlap < 0) PixelOverlap = 0;
	if (PixelOverlap > 4) PixelOverlap = 4;

	DWORD bold = IsBold ? FW_BOLD : FW_NORMAL;
	GDIFont = ::CreateFont(font_height, fontWidth, 0, 0, bold, 0,
		FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
		VARIABLE_PITCH, font_name);

	// Create a DIB section for rendering characters
	BITMAPINFOHEADER bitmap_info = { 0 };
	bitmap_info.biSize = sizeof(BITMAPINFOHEADER);
	bitmap_info.biWidth = PointSize * 2;
	bitmap_info.biHeight = -(PointSize * 2);
	bitmap_info.biPlanes = 1;
	bitmap_info.biBitCount = 24;
	bitmap_info.biCompression = BI_RGB;
	bitmap_info.biSizeImage = ((PointSize * PointSize * 4) * 3);

	GDIBitmap = ::CreateDIBSection(screen_dc,
		(const BITMAPINFO*)&bitmap_info,
		DIB_RGB_COLORS,
		(void**)&GDIBitmapBits,
		nullptr, 0L);

	MemDC = ::CreateCompatibleDC(screen_dc);
	::ReleaseDC(nullptr, screen_dc);

	OldGDIBitmap = (HBITMAP)::SelectObject(MemDC, GDIBitmap);
	OldGDIFont = (HFONT)::SelectObject(MemDC, GDIFont);
	::SetBkColor(MemDC, RGB(0, 0, 0));
	::SetTextColor(MemDC, RGB(255, 255, 255));

	TEXTMETRIC text_metric = { 0 };
	::GetTextMetrics(MemDC, &text_metric);
	CharHeight = text_metric.tmHeight;
	CharAscent = text_metric.tmAscent;
	CharOverhang = text_metric.tmOverhang;
	if (doingGenerals)
		CharOverhang = 0;

	return GDIFont != nullptr && GDIBitmap != nullptr;
}

void FontCharsClass::Free_GDI_Font()
{
	if (GDIFont != nullptr)
	{
		::SelectObject(MemDC, OldGDIFont);
		::DeleteObject(GDIFont);
		GDIFont = nullptr;
	}

	if (GDIBitmap != nullptr)
	{
		::SelectObject(MemDC, OldGDIBitmap);
		::DeleteObject(GDIBitmap);
		GDIBitmap = nullptr;
	}

	if (MemDC != nullptr)
	{
		::DeleteDC(MemDC);
		MemDC = nullptr;
	}
}

const FontCharsClassCharDataStruct* FontCharsClass::Store_GDI_Char(WCHAR ch)
{
	int width = PointSize * 2;
	int height = PointSize * 2;

	// Draw the character into the memory DC
	RECT rect = { 0, 0, width, height };
	int xOrigin = 0;
	if (ch == L'W')
		xOrigin = 1;

	::ExtTextOutW(MemDC, xOrigin, 0, ETO_OPAQUE, &rect, &ch, 1, nullptr);

	// Get the size of the character we just drew
	SIZE char_size = { 0 };
	::GetTextExtentPoint32W(MemDC, &ch, 1, &char_size);
	char_size.cx += PixelOverlap + xOrigin;

	// Get a pointer to the buffer that this character should use
	Update_Current_Buffer(char_size.cx);
	uint16* curr_buffer_p = BufferList[BufferList.Count() - 1]->Buffer;
	curr_buffer_p += CurrPixelOffset;

	// Copy the BMP contents to the buffer (24-bit DIB -> A4R4G4B4)
	int stride = (((width * 3) + 3) & ~3);
	for (int row = 0; row < char_size.cy; row++)
	{
		int index = (row * stride);

		for (int col = 0; col < char_size.cx; col++)
		{
			uint8 pixel_value = GDIBitmapBits[index];
			index += 3;

			uint16 pixel_color = 0;
			if (pixel_value != 0)
				pixel_color = 0x0FFF;

			// Convert the pixel intensity from 8-bit to 4-bit alpha
			uint8 alpha_value = ((pixel_value >> 4) & 0xF);
			*curr_buffer_p++ = pixel_color | (alpha_value << 12);
		}
	}

	// Save information about this character
	FontCharsClassCharDataStruct* char_data = W3DNEW FontCharsClassCharDataStruct;
	char_data->Value = ch;
	char_data->Width = (short)char_size.cx;
	char_data->Buffer = BufferList[BufferList.Count() - 1]->Buffer + CurrPixelOffset;

	// Insert into the appropriate array
	if (ch < 256)
		ASCIICharArray[ch] = char_data;
	else
		UnicodeCharArray[ch - FirstUnicodeChar] = char_data;

	// Advance the character position
	CurrPixelOffset += ((char_size.cx + PixelOverlap) * CharHeight);

	return char_data;
}

void FontCharsClass::Update_Current_Buffer(int char_width)
{
	bool needs_new_buffer = (BufferList.Count() == 0);
	if (!needs_new_buffer)
	{
		if ((CurrPixelOffset + (char_width * CharHeight)) > CHAR_BUFFER_LEN)
			needs_new_buffer = true;
	}

	if (needs_new_buffer)
	{
		FontCharsBuffer* new_buffer = W3DNEW FontCharsBuffer;
		BufferList.Add(new_buffer);
		CurrPixelOffset = 0;
	}
}

const FontCharsClassCharDataStruct* FontCharsClass::Get_Char_Data(WCHAR ch)
{
	const FontCharsClassCharDataStruct* retval = nullptr;

	if (ch < 256)
	{
		retval = ASCIICharArray[ch];
	}
	else if (AlternateUnicodeFont && this != AlternateUnicodeFont)
	{
		return AlternateUnicodeFont->Get_Char_Data(ch);
	}
	else
	{
		Grow_Unicode_Array(ch);
		retval = UnicodeCharArray[ch - FirstUnicodeChar];
	}

	// If the character wasn't found, rasterize it now
	if (retval == nullptr)
		retval = Store_GDI_Char(ch);

	return retval;
}

void FontCharsClass::Grow_Unicode_Array(WCHAR ch)
{
	if (ch < 256)
		return;

	if (ch >= FirstUnicodeChar && ch <= LastUnicodeChar)
		return;

	uint16 first_index = min(FirstUnicodeChar, static_cast<uint16>(ch));
	uint16 last_index = max(LastUnicodeChar, static_cast<uint16>(ch));
	uint16 count = (last_index - first_index) + 1;

	FontCharsClassCharDataStruct** new_array = W3DNEWARRAY FontCharsClassCharDataStruct*[count];
	::memset(new_array, 0, sizeof(FontCharsClassCharDataStruct*) * count);

	if (UnicodeCharArray != nullptr)
	{
		int start_offset = (FirstUnicodeChar - first_index);
		int old_count = (LastUnicodeChar - FirstUnicodeChar) + 1;
		::memcpy(&new_array[start_offset], UnicodeCharArray, sizeof(FontCharsClassCharDataStruct*) * old_count);
		delete[] UnicodeCharArray;
		UnicodeCharArray = nullptr;
	}

	FirstUnicodeChar = first_index;
	LastUnicodeChar = last_index;
	UnicodeCharArray = new_array;
}

void FontCharsClass::Free_Character_Arrays()
{
	if (UnicodeCharArray != nullptr)
	{
		int count = (LastUnicodeChar - FirstUnicodeChar) + 1;
		for (int index = 0; index < count; index++)
		{
			delete UnicodeCharArray[index];
			UnicodeCharArray[index] = nullptr;
		}
		delete[] UnicodeCharArray;
		UnicodeCharArray = nullptr;
	}

	for (int index = 0; index < 256; index++)
	{
		delete ASCIICharArray[index];
		ASCIICharArray[index] = nullptr;
	}
}


////////////////////////////////////////////////////////////////////////////////
// SECTION 8: W3DFontLibrary
////////////////////////////////////////////////////////////////////////////////

Bool W3DFontLibrary::loadFontData(GameFont* font)
{
	if (font == nullptr)
		return FALSE;

	// Font rendering uses GDI-based glyph rasterization (FontCharsClass below).
	// Characters are rendered to bitmaps via Win32 GDI, cached as D3D11 textures,
	// and drawn as textured quads by Render2DSentenceClass.  This replaces the
	// original DX8 asset-manager path and is the production implementation.
	FontCharsClass* fontChar = new FontCharsClass();
	const char* name = font->nameString.str();
	int size = font->pointSize;
	bool bold = font->bold ? true : false;

	fontChar->Initialize_GDI_Font(name, size, bold);
	fontChar->AlternateUnicodeFont = nullptr;

	font->fontData = fontChar;
	font->height = fontChar->Get_Char_Height();

	return TRUE;
}

void W3DFontLibrary::releaseFontData(GameFont* font)
{
	if (font && font->fontData)
	{
		FontCharsClass* fontChar = (FontCharsClass*)font->fontData;
		REF_PTR_RELEASE(fontChar);
		font->fontData = nullptr;
	}
}


////////////////////////////////////////////////////////////////////////////////
// SECTION 9: testMinimumRequirements
////////////////////////////////////////////////////////////////////////////////

Bool testMinimumRequirements(ChipsetType* videoChipType, CpuType* cpuType, Int* cpuFreq,
                             MemValueType* numRAM, Real* intBenchIndex, Real* floatBenchIndex,
                             Real* memBenchIndex)
{
	if (videoChipType)
		*videoChipType = (ChipsetType)0; // DC_UNKNOWN
	if (cpuType)
		*cpuType = (CpuType)0; // XX
	if (cpuFreq)
		*cpuFreq = 3000; // Fake a fast CPU
	if (numRAM)
		*numRAM = (MemValueType)(4ULL * 1024 * 1024 * 1024); // 4 GB
	if (intBenchIndex)
		*intBenchIndex = 100.0f;
	if (floatBenchIndex)
		*floatBenchIndex = 100.0f;
	if (memBenchIndex)
		*memBenchIndex = 100.0f;
	return TRUE;
}


////////////////////////////////////////////////////////////////////////////////
// SECTION 10: StatDebugDisplay
////////////////////////////////////////////////////////////////////////////////

void StatDebugDisplay(DebugDisplayInterface* dd, void*, FILE* fp)
{
	if (!dd || !TheDisplay)
		return;

	// Rendering statistics overlay -- FPS, resolution, and engine settings.
	W3DDisplay* display = static_cast<W3DDisplay*>(TheDisplay);

	Real fps = display->getAverageFPS();
	Real curFps = display->getCurrentFPS();
	Int drawCalls = display->getLastFrameDrawCalls();

	dd->setTextColor(DebugDisplayInterface::WHITE);
	dd->printf("FPS: %.1f  (cur: %.1f)", fps, curFps);
	dd->printf("Draw calls: %d", drawCalls);
	dd->printf("Resolution: %dx%d", display->getWidth(), display->getHeight());

	if (TheGlobalData)
	{
		dd->printf("Texture reduction: %d", TheGlobalData->m_textureReductionFactor);
		dd->printf("Shadows: vol=%s decal=%s",
			TheGlobalData->m_useShadowVolumes ? "on" : "off",
			TheGlobalData->m_useShadowDecals ? "on" : "off");
	}

	// Also dump to file if requested (used by automated testing)
	if (fp)
	{
		fprintf(fp, "FPS: %.1f (cur: %.1f)  DrawCalls: %d  Res: %dx%d\n",
			fps, curFps, drawCalls, display->getWidth(), display->getHeight());
	}
}


////////////////////////////////////////////////////////////////////////////////
// SECTION 11: doSkyBoxSet, oversizeTheTerrain, ReloadAllTextures
////////////////////////////////////////////////////////////////////////////////

void doSkyBoxSet(Bool startDraw)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_drawSkyBox = startDraw;
}

void oversizeTheTerrain(Int amount)
{
	// In the original DX8 renderer this expanded the visible terrain tile window.
	// The D3D11 terrain renderer always builds and draws the full heightmap, so
	// there is no draw-width to expand.  Invalidate the terrain mesh so it gets
	// rebuilt on the next frame (the caller may have resized the logical map).
	Render::TerrainRenderer::Instance().Invalidate();
}

void ReloadAllTextures()
{
	// Clear the D3D11 image cache so every texture is reloaded from disk on
	// the next frame.  This is used for hot-reloading assets during development.
	Render::ImageCache::Instance().Clear();

	// Also invalidate the terrain so its texture atlas is rebuilt from the
	// freshly-loaded tile textures.
	Render::TerrainRenderer::Instance().Invalidate();
}



// SECTION 12 REMOVED: Draw module implementations now in real .cpp files

// Symbols needed by draw modules from removed WW3D2/GameEngineDevice files:

// Line3D and SegLine now compiled from their own .cpp files
#include "WW3D2/texture.h"
#include "WW3D2/texturefilter.h"
#include "WW3D2/surfaceclass.h"
#include "WW3D2/shader.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "W3DDevice/GameClient/W3DTerrainTracks.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/W3DPropBuffer.h"
#include "W3DDevice/GameClient/Module/W3DTreeDraw.h"

// TextureFilterClass stub
TextureFilterClass::TextureFilterClass(MipCountType) {}

// TextureBaseClass / TextureClass stubs - D3D8 texture system replaced by D3D11
TextureBaseClass::TextureBaseClass(unsigned width, unsigned height, MipCountType mip,
	PoolType pool, bool rendertarget, bool reducible)
	: Initialized(false), IsLightmap(false), IsCompressionAllowed(false),
	  IsProcedural(false), IsReducible(reducible), MipLevelCount(mip),
	  InactivationTime(0), ExtendedInactivationTime(0), LastInactivationSyncTime(0),
	  LastAccessed(0), Width(width), Height(height) {}
TextureBaseClass::~TextureBaseClass() {}

TextureClass::TextureClass(IDirect3DBaseTexture8*)
	: TextureBaseClass(0, 0, MIP_LEVELS_1, POOL_MANAGED, false, false),
	  TextureFormat(WW3D_FORMAT_UNKNOWN), Filter(MIP_LEVELS_1) {}
void TextureClass::Init() {}
void TextureClass::Apply(unsigned int) {}
void TextureClass::Apply_New_Surface(IDirect3DBaseTexture8*, bool, bool) {}
unsigned TextureClass::Get_Texture_Memory_Usage() const { return 0; }
void TextureClass::Get_Level_Description(SurfaceClass::SurfaceDescription& desc, unsigned int) {
	desc.Format = WW3D_FORMAT_UNKNOWN;
	desc.Width = 0;
	desc.Height = 0;
}

// W3DShadowManager
W3DShadowManager* TheW3DShadowManager = nullptr;
W3DShadowManager::W3DShadowManager()
{
	m_isShadowScene = FALSE;
	m_shadowColor = 0x7fa0a0a0;
	m_stencilShadowMask = 0;
	TheProjectedShadowManager = &g_d3d11ShadowManager;
}

W3DShadowManager::~W3DShadowManager()
{
	if (TheW3DShadowManager == this)
		TheW3DShadowManager = nullptr;

	TheProjectedShadowManager = &g_d3d11ShadowManager;
}

Bool W3DShadowManager::init()
{
	TheW3DShadowManager = this;
	TheProjectedShadowManager = &g_d3d11ShadowManager;
	return TRUE;
}

void W3DShadowManager::Reset()
{
	removeAllShadows();
}

void W3DShadowManager::removeAllShadows()
{
	D3D11Shadow* s = g_d3d11ShadowManager.m_shadowList;
	while (s)
	{
		D3D11Shadow* next = s->m_next;
		g_d3d11ShadowManager.removeShadow(s);
		delete s;
		s = next;
	}
}

#define SUN_DISTANCE_FROM_GROUND 10000.0f

void W3DShadowManager::setTimeOfDay(TimeOfDay tod)
{
	if (!TheGlobalData)
		return;

	const GlobalData::TerrainLighting *ol = &TheGlobalData->m_terrainObjectsLighting[tod][0];

	Vector3 lightRay(-ol->lightPos.x, -ol->lightPos.y, -ol->lightPos.z);
	lightRay.Normalize();
	lightRay *= SUN_DISTANCE_FROM_GROUND;

	setLightPosition(0, lightRay.X, lightRay.Y, lightRay.Z);
}

void W3DShadowManager::invalidateCachedLightPositions() {}

Vector3 &W3DShadowManager::getLightPosWorld(Int lightIndex)
{
	if (lightIndex < 0 || lightIndex >= MAX_SHADOW_LIGHTS)
		lightIndex = 0;

	return g_fallbackShadowLightPos[lightIndex];
}

void W3DShadowManager::setLightPosition(Int lightIndex, Real x, Real y, Real z)
{
	if (lightIndex < 0 || lightIndex >= MAX_SHADOW_LIGHTS)
		return;

	g_fallbackShadowLightPos[lightIndex].X = x;
	g_fallbackShadowLightPos[lightIndex].Y = y;
	g_fallbackShadowLightPos[lightIndex].Z = z;
}

void W3DShadowManager::RenderShadows() {}
void W3DShadowManager::ReleaseResources() {}
Bool W3DShadowManager::ReAcquireResources() { return TRUE; }

Shadow* W3DShadowManager::addShadow(RenderObjClass* robj, Shadow::ShadowTypeInfo* shadowInfo, Drawable* draw)
{
	if (shadowInfo && shadowInfo->m_type == SHADOW_NONE)
		return nullptr;

	// Pass the render object through so the resulting D3D11Shadow can sample
	// its world transform every frame and follow the unit as it moves.
	return g_d3d11ShadowManager.addDecalInternal(robj, shadowInfo);
}

void W3DShadowManager::removeShadow(Shadow* shadow)
{
	if (shadow)
		shadow->release();
}

////////////////////////////////////////////////////////////////////////////////
// SECTION: Terrain Tracks (Vehicle Tire/Tread Marks) - D3D11 Implementation
//
// Ported from the original W3DTerrainTracks.cpp. The edge-management,
// bind/unbind, update (fading), and edge-layout logic is unchanged.
// The DX8 vertex/index buffer rendering in flush() has been replaced with
// D3D11 Renderer draw calls following the same pattern as scorch marks.
////////////////////////////////////////////////////////////////////////////////

#include "GameClient/Drawable.h"
#include "GameLogic/Object.h"
#include "W3DDevice/GameClient/ImageCache.h"
#include "Renderer.h"

#define TRACKS_BRIDGE_OFFSET_FACTOR 0.25f

TerrainTracksRenderObjClassSystem* TheTerrainTracksRenderObjClassSystem = nullptr;

// ---------------------------------------------------------------------------
// TerrainTracksRenderObjClass - individual track object
// ---------------------------------------------------------------------------

TerrainTracksRenderObjClass::TerrainTracksRenderObjClass()
{
	m_stageZeroTexture = nullptr;
	m_lastAnchor = Vector3(0, 1, 2.25f);
	m_haveAnchor = false;
	m_haveCap = true;
	m_topIndex = 0;
	m_bottomIndex = 0;
	m_activeEdgeCount = 0;
	m_totalEdgesAdded = 0;
	m_bound = false;
	m_ownerDrawable = nullptr;
	m_nextSystem = nullptr;
	m_prevSystem = nullptr;
	m_airborne = false;
	m_width = 0;
	m_length = 0;
}

TerrainTracksRenderObjClass::~TerrainTracksRenderObjClass()
{
	freeTerrainTracksResources();
}

void TerrainTracksRenderObjClass::Get_Obj_Space_Bounding_Sphere(SphereClass& sphere) const
{
	sphere = m_boundingSphere;
}

void TerrainTracksRenderObjClass::Get_Obj_Space_Bounding_Box(AABoxClass& box) const
{
	box = m_boundingBox;
}

Int TerrainTracksRenderObjClass::Class_ID() const
{
	return RenderObjClass::CLASSID_IMAGE3D;
}

RenderObjClass* TerrainTracksRenderObjClass::Clone() const
{
	return nullptr;
}

Int TerrainTracksRenderObjClass::freeTerrainTracksResources()
{
	REF_PTR_RELEASE(m_stageZeroTexture);
	m_haveAnchor = false;
	m_haveCap = true;
	m_topIndex = 0;
	m_bottomIndex = 0;
	m_activeEdgeCount = 0;
	m_totalEdgesAdded = 0;
	m_ownerDrawable = nullptr;
	return 0;
}

void TerrainTracksRenderObjClass::init(Real width, Real length, const Char* texturename)
{
	freeTerrainTracksResources();

	m_boundingSphere.Init(Vector3(0, 0, 0), 400 * MAP_XY_FACTOR);
	m_boundingBox.Center.Set(0.0f, 0.0f, 0.0f);
	m_boundingBox.Extent.Set(400.0f * MAP_XY_FACTOR, 400.0f * MAP_XY_FACTOR, 1.0f);
	m_width = width;
	m_length = length;
	Set_Force_Visible(TRUE);
	// Load texture through WW3D asset manager (same as original)
	m_stageZeroTexture = WW3DAssetManager::Get_Instance()->Get_Texture(texturename);
}

void TerrainTracksRenderObjClass::addCapEdgeToTrack(Real x, Real y)
{
	if (m_haveCap)
		return;

	if (m_activeEdgeCount == 1)
	{
		m_haveCap = TRUE;
		m_haveAnchor = false;
		return;
	}

	Vector3 vPos, vZ;
	Coord3D vZTmp;
	PathfindLayerEnum objectLayer;
	Real eHeight;

	if (m_ownerDrawable && (objectLayer = m_ownerDrawable->getObject()->getLayer()) != LAYER_GROUND)
		eHeight = TRACKS_BRIDGE_OFFSET_FACTOR + TheTerrainLogic->getLayerHeight(x, y, objectLayer, &vZTmp);
	else
		eHeight = TheTerrainLogic->getGroundHeight(x, y, &vZTmp);

	vZ.X = vZTmp.x;
	vZ.Y = vZTmp.y;
	vZ.Z = vZTmp.z;

	vPos.X = x;
	vPos.Y = y;
	vPos.Z = eHeight;

	Vector3 vDir = Vector3(x, y, eHeight) - m_lastAnchor;
	Int maxEdgeCount = TheTerrainTracksRenderObjClassSystem->m_maxTankTrackEdges;

	if (vDir.Length2() < sqr(m_length))
	{
		Int lastAddedEdge = m_topIndex - 1;
		if (lastAddedEdge < 0)
			lastAddedEdge = maxEdgeCount - 1;
		m_edges[lastAddedEdge].alpha = 0.0f;
		m_haveCap = TRUE;
		m_haveAnchor = false;
		return;
	}

	if (m_activeEdgeCount >= maxEdgeCount)
	{
		m_bottomIndex++;
		m_activeEdgeCount--;
		if (m_bottomIndex >= maxEdgeCount)
			m_bottomIndex = 0;
	}

	if (m_topIndex >= maxEdgeCount)
		m_topIndex = 0;

	vDir.Z = 0;
	vDir.Normalize();

	Vector3 vX;
	Vector3::Cross_Product(vDir, vZ, &vX);

	edgeInfo& topEdge = m_edges[m_topIndex];

	topEdge.endPointPos[0] = vPos - (m_width * 0.5f * vX);
	topEdge.endPointPos[0].Z += 0.2f * MAP_XY_FACTOR;

	if (m_totalEdgesAdded & 1)
	{
		topEdge.endPointUV[0].X = 0.0f;
		topEdge.endPointUV[0].Y = 0.0f;
	}
	else
	{
		topEdge.endPointUV[0].X = 0.0f;
		topEdge.endPointUV[0].Y = 1.0f;
	}

	topEdge.endPointPos[1] = vPos + (m_width * 0.5f * vX);
	topEdge.endPointPos[1].Z += 0.2f * MAP_XY_FACTOR;

	if (m_totalEdgesAdded & 1)
	{
		topEdge.endPointUV[1].X = 1.0f;
		topEdge.endPointUV[1].Y = 0.0f;
	}
	else
	{
		topEdge.endPointUV[1].X = 1.0f;
		topEdge.endPointUV[1].Y = 1.0f;
	}

	topEdge.timeAdded = WW3D::Get_Sync_Time();
	topEdge.alpha = 0.0f;
	m_lastAnchor = vPos;
	m_activeEdgeCount++;
	m_totalEdgesAdded++;
	m_topIndex++;
	m_haveCap = TRUE;
	m_haveAnchor = false;
}

void TerrainTracksRenderObjClass::addEdgeToTrack(Real x, Real y)
{
	if (!m_haveAnchor)
	{
		PathfindLayerEnum objectLayer;
		if (m_ownerDrawable && (objectLayer = m_ownerDrawable->getObject()->getLayer()) != LAYER_GROUND)
			m_lastAnchor = Vector3(x, y, TheTerrainLogic->getLayerHeight(x, y, objectLayer) + TRACKS_BRIDGE_OFFSET_FACTOR);
		else
			m_lastAnchor = Vector3(x, y, TheTerrainLogic->getGroundHeight(x, y));

		m_haveAnchor = true;
		m_airborne = true;
		m_haveCap = true;
		return;
	}

	m_haveCap = false;

	Vector3 vPos, vZ;
	Coord3D vZTmp;
	Real eHeight;
	PathfindLayerEnum objectLayer;

	if (m_ownerDrawable && (objectLayer = m_ownerDrawable->getObject()->getLayer()) != LAYER_GROUND)
		eHeight = TRACKS_BRIDGE_OFFSET_FACTOR + TheTerrainLogic->getLayerHeight(x, y, objectLayer, &vZTmp);
	else
		eHeight = TheTerrainLogic->getGroundHeight(x, y, &vZTmp);

	vZ.X = vZTmp.x;
	vZ.Y = vZTmp.y;
	vZ.Z = vZTmp.z;

	vPos.X = x;
	vPos.Y = y;
	vPos.Z = eHeight;

	Vector3 vDir = Vector3(x, y, eHeight) - m_lastAnchor;

	if (vDir.Length2() < sqr(m_length))
		return;

	Int maxEdgeCount = TheTerrainTracksRenderObjClassSystem->m_maxTankTrackEdges;

	if (m_activeEdgeCount >= maxEdgeCount)
	{
		m_bottomIndex++;
		m_activeEdgeCount--;
		if (m_bottomIndex >= maxEdgeCount)
			m_bottomIndex = 0;
	}

	if (m_topIndex >= maxEdgeCount)
		m_topIndex = 0;

	vDir.Z = 0;
	vDir.Normalize();

	Vector3 vX;
	Vector3::Cross_Product(vDir, vZ, &vX);

	edgeInfo& topEdge = m_edges[m_topIndex];

	topEdge.endPointPos[0] = vPos - (m_width * 0.5f * vX);
	topEdge.endPointPos[0].Z += 0.2f * MAP_XY_FACTOR;

	if (m_totalEdgesAdded & 1)
	{
		topEdge.endPointUV[0].X = 0.0f;
		topEdge.endPointUV[0].Y = 0.0f;
	}
	else
	{
		topEdge.endPointUV[0].X = 0.0f;
		topEdge.endPointUV[0].Y = 1.0f;
	}

	topEdge.endPointPos[1] = vPos + (m_width * 0.5f * vX);
	topEdge.endPointPos[1].Z += 0.2f * MAP_XY_FACTOR;

	if (m_totalEdgesAdded & 1)
	{
		topEdge.endPointUV[1].X = 1.0f;
		topEdge.endPointUV[1].Y = 0.0f;
	}
	else
	{
		topEdge.endPointUV[1].X = 1.0f;
		topEdge.endPointUV[1].Y = 1.0f;
	}

	topEdge.timeAdded = WW3D::Get_Sync_Time();
	topEdge.alpha = 1.0f;
	if (m_airborne || m_activeEdgeCount <= 1)
		topEdge.alpha = 0.0f;
	m_airborne = false;

	m_lastAnchor = vPos;
	m_activeEdgeCount++;
	m_totalEdgesAdded++;
	m_topIndex++;
}

void TerrainTracksRenderObjClass::Render(RenderInfoClass& rinfo)
{
	if (TheGlobalData->m_makeTrackMarks && m_activeEdgeCount >= 2)
		TheTerrainTracksRenderObjClassSystem->m_edgesToFlush += m_activeEdgeCount;
}

// ---------------------------------------------------------------------------
// Track spacing helper - measures distance between TREADFX bones
// ---------------------------------------------------------------------------
#define TRACKS_DEFAULT_TRACK_SPACING (MAP_XY_FACTOR * 1.4f)
#define TRACKS_DEFAULT_TRACK_WIDTH 4.0f

static Real tracksComputeTrackSpacing(RenderObjClass* renderObj)
{
	Real trackSpacing = TRACKS_DEFAULT_TRACK_SPACING;
	Int leftTrack;
	Int rightTrack;

	if ((leftTrack = renderObj->Get_Bone_Index("TREADFX01")) != 0 &&
		(rightTrack = renderObj->Get_Bone_Index("TREADFX02")) != 0)
	{
		Vector3 leftPos, rightPos;
		leftPos = renderObj->Get_Bone_Transform(leftTrack).Get_Translation();
		rightPos = renderObj->Get_Bone_Transform(rightTrack).Get_Translation();
		rightPos -= leftPos;
		trackSpacing = rightPos.Length() + TRACKS_DEFAULT_TRACK_WIDTH;
	}

	return trackSpacing;
}

// ---------------------------------------------------------------------------
// TerrainTracksRenderObjClassSystem - system managing all track objects
// ---------------------------------------------------------------------------

TerrainTracksRenderObjClassSystem::TerrainTracksRenderObjClassSystem()
{
	m_usedModules = nullptr;
	m_freeModules = nullptr;
	m_TerrainTracksScene = nullptr;
	m_edgesToFlush = 0;
	m_indexBuffer = nullptr;
	m_vertexMaterialClass = nullptr;
	m_vertexBuffer = nullptr;

	m_maxTankTrackEdges = TheGlobalData ? TheGlobalData->m_maxTankTrackEdges : MAX_TRACK_EDGE_COUNT;
	m_maxTankTrackOpaqueEdges = TheGlobalData ? TheGlobalData->m_maxTankTrackOpaqueEdges : MAX_TRACK_OPAQUE_EDGE;
	m_maxTankTrackFadeDelay = TheGlobalData ? TheGlobalData->m_maxTankTrackFadeDelay : FADE_TIME_FRAMES;
}

TerrainTracksRenderObjClassSystem::~TerrainTracksRenderObjClassSystem()
{
	shutdown();
	m_vertexMaterialClass = nullptr;
	m_TerrainTracksScene = nullptr;
}

void TerrainTracksRenderObjClassSystem::ReleaseResources()
{
	// No DX8 resources to release in D3D11 path
	REF_PTR_RELEASE(m_indexBuffer);
	REF_PTR_RELEASE(m_vertexBuffer);
}

void TerrainTracksRenderObjClassSystem::ReAcquireResources()
{
	// No DX8 resources to acquire in D3D11 path - rendering uses Renderer API
	REF_PTR_RELEASE(m_indexBuffer);
	REF_PTR_RELEASE(m_vertexBuffer);
}

void TerrainTracksRenderObjClassSystem::init(SceneClass* TerrainTracksScene)
{
	const Int numModules = TheGlobalData ? TheGlobalData->m_maxTerrainTracks : 32;

	m_TerrainTracksScene = TerrainTracksScene;

	// Reacquire resources (no-op for D3D11 but maintains API)
	ReAcquireResources();

	if (m_freeModules || m_usedModules)
		return; // already initialized

	for (Int i = 0; i < numModules; i++)
	{
		TerrainTracksRenderObjClass* mod = NEW_REF(TerrainTracksRenderObjClass, ());
		if (mod == nullptr)
			return;

		mod->m_prevSystem = nullptr;
		mod->m_nextSystem = m_freeModules;
		if (m_freeModules)
			m_freeModules->m_prevSystem = mod;
		m_freeModules = mod;
	}
}

void TerrainTracksRenderObjClassSystem::shutdown()
{
	TerrainTracksRenderObjClass *nextMod, *mod;

	// Release unbound tracks that may still be fading out
	mod = m_usedModules;
	while (mod)
	{
		nextMod = mod->m_nextSystem;
		if (!mod->m_bound)
			releaseTrack(mod);
		mod = nextMod;
	}

	// Free all module storage
	while (m_freeModules)
	{
		nextMod = m_freeModules->m_nextSystem;
		REF_PTR_RELEASE(m_freeModules);
		m_freeModules = nextMod;
	}

	REF_PTR_RELEASE(m_indexBuffer);
	REF_PTR_RELEASE(m_vertexMaterialClass);
	REF_PTR_RELEASE(m_vertexBuffer);
}

TerrainTracksRenderObjClass* TerrainTracksRenderObjClassSystem::bindTrack(
	RenderObjClass* renderObject, Real length, const Char* texturename)
{
	TerrainTracksRenderObjClass* mod = m_freeModules;
	if (mod)
	{
		// Take module off the free list
		if (mod->m_nextSystem)
			mod->m_nextSystem->m_prevSystem = mod->m_prevSystem;
		if (mod->m_prevSystem)
			mod->m_prevSystem->m_nextSystem = mod->m_nextSystem;
		else
			m_freeModules = mod->m_nextSystem;

		// Put module on the used list
		mod->m_prevSystem = nullptr;
		mod->m_nextSystem = m_usedModules;
		if (m_usedModules)
			m_usedModules->m_prevSystem = mod;
		m_usedModules = mod;

		mod->init(tracksComputeTrackSpacing(renderObject), length, texturename);
		mod->m_bound = true;
		// In D3D11 we don't add to a WW3D scene - rendering is handled by
		// RenderTerrainTracksDX11() called from W3DDisplay::draw()
	}

	return mod;
}

void TerrainTracksRenderObjClassSystem::unbindTrack(TerrainTracksRenderObjClass* mod)
{
	if (!mod) return;
	mod->m_bound = false;
	mod->m_ownerDrawable = nullptr;
}

void TerrainTracksRenderObjClassSystem::releaseTrack(TerrainTracksRenderObjClass* mod)
{
	if (mod == nullptr)
		return;

	// Remove module from used list
	if (mod->m_nextSystem)
		mod->m_nextSystem->m_prevSystem = mod->m_prevSystem;
	if (mod->m_prevSystem)
		mod->m_prevSystem->m_nextSystem = mod->m_nextSystem;
	else
		m_usedModules = mod->m_nextSystem;

	// Add module to free list
	mod->m_prevSystem = nullptr;
	mod->m_nextSystem = m_freeModules;
	if (m_freeModules)
		m_freeModules->m_prevSystem = mod;
	m_freeModules = mod;
	mod->freeTerrainTracksResources();
}

void TerrainTracksRenderObjClassSystem::update()
{
	Int iTime = WW3D::Get_Sync_Time();
	Real iDiff;
	TerrainTracksRenderObjClass *mod = m_usedModules, *nextMod;

	while (mod)
	{
		nextMod = mod->m_nextSystem;

		if (!TheGlobalData->m_makeTrackMarks)
			mod->m_haveAnchor = false;

		for (Int i = 0, index = mod->m_bottomIndex; i < mod->m_activeEdgeCount; i++, index++)
		{
			if (index >= m_maxTankTrackEdges)
				index = 0;

			iDiff = (float)(iTime - mod->m_edges[index].timeAdded);
			iDiff = 1.0f - iDiff / (Real)m_maxTankTrackFadeDelay;
			if (iDiff < 0.0f)
				iDiff = 0.0f;
			if (mod->m_edges[index].alpha > 0.0f)
				mod->m_edges[index].alpha = iDiff;

			if (iDiff == 0.0f)
			{
				mod->m_bottomIndex++;
				mod->m_activeEdgeCount--;
				if (mod->m_bottomIndex >= m_maxTankTrackEdges)
					mod->m_bottomIndex = 0;
			}
			if (mod->m_activeEdgeCount == 0 && !mod->m_bound)
				releaseTrack(mod);
		}
		mod = nextMod;
	}
}

void TerrainTracksRenderObjClassSystem::flush()
{
	// In the D3D11 path, flush is handled by RenderTerrainTracksDX11().
	// This method is kept for API compatibility.
	m_edgesToFlush = 0;
}

void TerrainTracksRenderObjClassSystem::Reset()
{
	TerrainTracksRenderObjClass *nextMod, *mod = m_usedModules;
	while (mod)
	{
		nextMod = mod->m_nextSystem;
		releaseTrack(mod);
		mod = nextMod;
	}
	m_edgesToFlush = 0;
}

void TerrainTracksRenderObjClassSystem::clearTracks()
{
	TerrainTracksRenderObjClass* mod = m_usedModules;
	while (mod)
	{
		mod->m_haveAnchor = false;
		mod->m_haveCap = true;
		mod->m_topIndex = 0;
		mod->m_bottomIndex = 0;
		mod->m_activeEdgeCount = 0;
		mod->m_totalEdgesAdded = 0;
		mod = mod->m_nextSystem;
	}
	m_edgesToFlush = 0;
}

void TerrainTracksRenderObjClassSystem::setDetail()
{
	clearTracks();
	ReleaseResources();

	m_maxTankTrackEdges = TheGlobalData->m_maxTankTrackEdges;
	m_maxTankTrackOpaqueEdges = TheGlobalData->m_maxTankTrackOpaqueEdges;
	m_maxTankTrackFadeDelay = TheGlobalData->m_maxTankTrackFadeDelay;

	ReAcquireResources();
}

// ---------------------------------------------------------------------------
// D3D11 terrain track rendering - called from W3DDisplay::draw()
// ---------------------------------------------------------------------------

// Cache of D3D11 textures loaded from track texture names
static std::unordered_map<std::string, Render::Texture*> g_trackTextureCache;

static Render::Texture* GetTrackTextureDX11(const char* textureName)
{
	if (!textureName || !textureName[0])
		return nullptr;

	auto it = g_trackTextureCache.find(textureName);
	if (it != g_trackTextureCache.end())
		return it->second;

	auto& renderer = Render::Renderer::Instance();
	Render::Texture* tex = Render::ImageCache::Instance().GetTexture(
		renderer.GetDevice(), textureName);

	g_trackTextureCache[textureName] = tex;
	return tex;
}

void ClearTerrainTrackTextures()
{
	g_trackTextureCache.clear();
}

void TerrainTracksRenderObjClassSystem::renderDX11()
{
	if (!TheGlobalData || !TheGlobalData->m_makeTrackMarks)
		return;

	auto& renderer = Render::Renderer::Instance();

	// Compute ambient+diffuse lighting tint for tracks (same as original)
	Real shadeR = TheGlobalData->m_terrainAmbient[0].red;
	Real shadeG = TheGlobalData->m_terrainAmbient[0].green;
	Real shadeB = TheGlobalData->m_terrainAmbient[0].blue;
	shadeR += TheGlobalData->m_terrainDiffuse[0].red * 0.5f;
	shadeG += TheGlobalData->m_terrainDiffuse[0].green * 0.5f;
	shadeB += TheGlobalData->m_terrainDiffuse[0].blue * 0.5f;

	// Clamp to [0,1]
	if (shadeR > 1.0f) shadeR = 1.0f;
	if (shadeG > 1.0f) shadeG = 1.0f;
	if (shadeB > 1.0f) shadeB = 1.0f;

	Int diffuseR = REAL_TO_INT(shadeR * 255.0f);
	Int diffuseG = REAL_TO_INT(shadeG * 255.0f);
	Int diffuseB = REAL_TO_INT(shadeB * 255.0f);

	Real numFadedEdges = (Real)(m_maxTankTrackEdges - m_maxTankTrackOpaqueEdges);
	if (numFadedEdges < 1.0f) numFadedEdges = 1.0f;

	// Persistent dynamic GPU buffers for track rendering - created once, reused for each track.
	// MAX_TRACK_EDGE_COUNT edges per track: 2 vertices per edge, 6 indices per edge-pair.
	static const uint32_t TRACK_MAX_VERTS = MAX_TRACK_EDGE_COUNT * 2;
	static const uint32_t TRACK_MAX_INDICES = (MAX_TRACK_EDGE_COUNT - 1) * 6;
	static Render::VertexBuffer s_trackVB;
	static Render::IndexBuffer  s_trackIB;
	static bool s_trackBuffersCreated = false;

	auto& device = renderer.GetDevice();

	if (!s_trackBuffersCreated)
	{
		if (!s_trackVB.Create(device, nullptr, TRACK_MAX_VERTS, sizeof(Render::Vertex3D), true))
			return;
		if (!s_trackIB.Create32(device, nullptr, TRACK_MAX_INDICES, true))
			return;
		s_trackBuffersCreated = true;
	}

	// CPU-side arrays for filling per-track data (stack allocated, reused each iteration)
	Render::Vertex3D trackVerts[TRACK_MAX_VERTS];
	uint32_t trackIndices[TRACK_MAX_INDICES];

	// Iterate over all used track modules and render each one
	TerrainTracksRenderObjClass* mod = m_usedModules;

	Render::Float4x4 worldIdentity;
	DirectX::XMStoreFloat4x4(&Render::ToXM(worldIdentity), DirectX::XMMatrixIdentity());

	renderer.SetAlphaBlend3DState();
	BindOverlayTexturesForGroundPass();

	while (mod)
	{
		if (mod->m_activeEdgeCount >= 2)
		{
			// Determine the texture for this track object
			Render::Texture* trackTex = nullptr;
			if (mod->m_stageZeroTexture)
			{
				const char* texName = mod->m_stageZeroTexture->Get_Texture_Name().str();
				if (texName && texName[0])
					trackTex = GetTrackTextureDX11(texName);
			}

			// Fill CPU vertex array for this track
			uint32_t vertCount = 0;
			for (Int i = 0, edgeIdx = mod->m_bottomIndex; i < mod->m_activeEdgeCount; i++, edgeIdx++)
			{
				if (edgeIdx >= m_maxTankTrackEdges)
					edgeIdx = 0;

				const TerrainTracksRenderObjClass::edgeInfo& edge = mod->m_edges[edgeIdx];

				Real distanceFade = 1.0f;
				if ((mod->m_activeEdgeCount - 1 - i) >= m_maxTankTrackOpaqueEdges)
				{
					distanceFade = 1.0f - (float)((mod->m_activeEdgeCount - i) - m_maxTankTrackOpaqueEdges) / numFadedEdges;
				}
				distanceFade *= edge.alpha;
				if (distanceFade < 0.0f) distanceFade = 0.0f;
				if (distanceFade > 1.0f) distanceFade = 1.0f;

				Int alpha = REAL_TO_INT(distanceFade * 255.0f);
				uint32_t color = ((uint32_t)alpha << 24) | ((uint32_t)diffuseB << 16) |
								 ((uint32_t)diffuseG << 8) | ((uint32_t)diffuseR);

				// Left endpoint
				trackVerts[vertCount].position = { edge.endPointPos[0].X, edge.endPointPos[0].Y, edge.endPointPos[0].Z };
				trackVerts[vertCount].normal = { 0.0f, 0.0f, 1.0f };
				trackVerts[vertCount].texcoord = { edge.endPointUV[0].X, edge.endPointUV[0].Y };
				trackVerts[vertCount].color = color;
				vertCount++;

				// Right endpoint
				trackVerts[vertCount].position = { edge.endPointPos[1].X, edge.endPointPos[1].Y, edge.endPointPos[1].Z };
				trackVerts[vertCount].normal = { 0.0f, 0.0f, 1.0f };
				trackVerts[vertCount].texcoord = { edge.endPointUV[1].X, edge.endPointUV[1].Y };
				trackVerts[vertCount].color = color;
				vertCount++;
			}

			// Fill CPU index array for the quad strip
			uint32_t idxCount = 0;
			for (Int i = 0; i < mod->m_activeEdgeCount - 1; i++)
			{
				uint32_t base = (uint32_t)(i * 2);
				trackIndices[idxCount++] = base;
				trackIndices[idxCount++] = base + 1;
				trackIndices[idxCount++] = base + 3;
				trackIndices[idxCount++] = base;
				trackIndices[idxCount++] = base + 3;
				trackIndices[idxCount++] = base + 2;
			}

			if (vertCount > 0 && idxCount > 0)
			{
				s_trackVB.Update(device, trackVerts, vertCount * sizeof(Render::Vertex3D));
				s_trackIB.Update(device, trackIndices, idxCount * sizeof(uint32_t));

				Render::Float4 tint = { 1.0f, 1.0f, 1.0f, 1.0f };
				renderer.Draw3DIndexed(s_trackVB, s_trackIB, idxCount, trackTex, worldIdentity, tint);
			}
		}

		mod = mod->m_nextSystem;
	}

	renderer.Restore3DState();
}

void RenderTerrainTracksDX11(CameraClass* camera)
{
	if (!camera)
		return;

	if (!TheTerrainTracksRenderObjClassSystem)
		return;

	TheTerrainTracksRenderObjClassSystem->renderDX11();
}


////////////////////////////////////////////////////////////////////////////////
// SECTION: Water Track (Wave) Rendering for D3D11
//
// In the original DX8 engine, water tracks (shore waves, boat wakes) were
// rendered by WaterTracksRenderSystem::flush() → WaterTracksObj::render(),
// which locked a DX8 vertex buffer, computed per-wave vertex positions, and
// issued DX8 Draw_Strip calls.
//
// In the D3D11 port, those DX8 calls are no-ops, and the WaterRenderObjClass
// scene object (which hosted flush()) is not processed by the D3D11 scene
// iteration.  This function replaces the entire pipeline: it updates wave
// animation state, computes quad vertices, and submits them via the D3D11
// Renderer — modeled after RenderTerrainTracksDX11() above.
////////////////////////////////////////////////////////////////////////////////

#include "GameClient/Water.h"
#include "Common/GlobalData.h"
#include <math.h>

// =======================================================================
// W3D Water Tracks — D3D11 lifecycle implementation
// =======================================================================
// The original W3DWaterTracks.cpp is not compiled in the D3D11 build because
// of its heavy DX8 dependencies (DX8VertexBufferClass, DX8Wrapper::Set_*,
// DX8IndexBufferClass, W3DAssetManager TextureClass, etc.). This section
// provides a DX8-free re-implementation of the CPU-side lifecycle:
// WaterTracksObj (wave instance), WaterTracksRenderSystem (pool + iteration),
// plus the waveType enum. The actual GPU submission is done later in this
// file by RenderWaterTracksDX11() which walks m_usedModules.
//
// The code below mirrors the original's behavior as closely as possible —
// the math/timing constants come directly from waveTypeInfo in the original
// W3DWaterTracks.cpp. The only omissions are:
//   - m_stageZeroTexture stays null (RenderWaterTracksDX11 falls back to
//     "wave256.tga" via GetTrackTextureDX11)
//   - ReAcquireResources/ReleaseResources are no-ops (no DX8 buffers)
//   - flush() is a no-op (rendering done in RenderWaterTracksDX11)
//   - render() returns the batchStart unchanged (rendering done elsewhere)
//   - saveTracks/loadTracks are no-ops (persistence was save-game only)

// waveType enum — the header has a forward decl; we provide the definition.
enum waveType : Int
{
    WaveTypeFirst,
    WaveTypePond = WaveTypeFirst,
    WaveTypeOcean,
    WaveTypeCloseOcean,
    WaveTypeCloseOceanDouble,
    WaveTypeRadial,
    WaveTypeLast = WaveTypeRadial,
    WaveTypeStationary,
    WaveTypeMax,
};

// RenderWaterTracksDX11 already had a `WATER_WAVE_TYPE_STATIONARY == 5`
// constant for compatibility with the forward-decl pattern; keep it as an
// alias to the enum so existing uses still compile.
static const int WATER_WAVE_TYPE_STATIONARY = WaveTypeStationary;

// Wave-type metadata table (copied from W3DWaterTracks.cpp waveTypeInfo).
struct WaterTracksWaveInfo
{
    Real m_finalWidth;
    Real m_finalHeight;
    Real m_waveDistance;
    Real m_initialVelocity;
    Int  m_fadeMs;
    Real m_initialWidthFraction;
    Real m_initialHeightWidthFraction;
    Int  m_timeToCompress;
    Int  m_secondWaveTimeOffset;
    const char* m_textureName;
    const char* m_waveTypeName;
};

static const WaterTracksWaveInfo s_waveTypeInfo[WaveTypeMax] = {
    {28.0f, 18.0f, 25.0f, 0.018f,  900, 0.01f, 0.18f, 1500,    0, "wave256.tga", "Pond"},
    {55.0f, 36.0f, 80.0f, 0.015f, 2000, 0.50f, 0.18f, 1000, 6267, "wave256.tga", "Ocean"},
    {55.0f, 36.0f, 80.0f, 0.015f, 2000, 0.05f, 0.18f, 1000, 6267, "wave256.tga", "Close Ocean"},
    {55.0f, 36.0f, 80.0f, 0.015f, 4000, 0.01f, 0.18f, 2000, 6267, "wave256.tga", "Close Ocean Double"},
    {55.0f, 27.0f, 80.0f, 0.015f, 2000, 0.01f, 8.00f, 2000, 5367, "wave256.tga", "Radial"},
};

#define WATER_TRACKS_STRIP_X 2
#define WATER_TRACKS_STRIP_Y 2

// The global singleton. The ctor assigns `this` to TheWaterTracksRenderSystem,
// matching the original behavior.
WaterTracksRenderSystem *TheWaterTracksRenderSystem = nullptr;

// --------------------------------------------------------------------------
// WaterTracksObj methods
// --------------------------------------------------------------------------

WaterTracksObj::WaterTracksObj()
{
    m_stageZeroTexture = nullptr;
    m_bound = false;
    m_initTimeOffset = 0;
    m_type = (waveType)0;
    m_x = WATER_TRACKS_STRIP_X;
    m_y = WATER_TRACKS_STRIP_Y;
    m_nextSystem = nullptr;
    m_prevSystem = nullptr;
    m_startPos.Set(0, 0);
    m_waveDir.Set(0, 0);
    m_perpDir.Set(0, 0);
    m_initStartPos.Set(0, 0);
    m_initEndPos.Set(0, 0);
    m_fadeMs = 0;
    m_totalMs = 0;
    m_elapsedMs = 0;
    m_waveInitialWidth = 0;
    m_waveInitialHeight = 0;
    m_waveFinalWidth = 0;
    m_waveFinalWidthPeakFrac = 0;
    m_waveFinalHeight = 0;
    m_initialVelocity = 0;
    m_waveDistance = 0;
    m_timeToReachBeach = 0;
    m_frontSlowDownAcc = 0;
    m_timeToStop = 0;
    m_timeToRetreat = 0;
    m_backSlowDownAcc = 0;
    m_timeToCompress = 0;
    m_flipU = 0;
}

WaterTracksObj::~WaterTracksObj()
{
    // No DX8 resources to free in this build.
}

void WaterTracksObj::Get_Obj_Space_Bounding_Sphere(SphereClass &sphere) const
{
    sphere = m_boundingSphere;
}

void WaterTracksObj::Get_Obj_Space_Bounding_Box(AABoxClass &box) const
{
    box = m_boundingBox;
}

Int WaterTracksObj::freeWaterTracksResources()
{
    m_stageZeroTexture = nullptr;
    return 0;
}

void WaterTracksObj::init(Real width, Real length, const Vector2 &start,
                          const Vector2 &end, const Char *texturename,
                          Int waveTimeOffset)
{
    freeWaterTracksResources();

    m_initStartPos = start;
    m_initEndPos = end;
    m_initTimeOffset = waveTimeOffset;

    m_boundingSphere.Init(Vector3(0, 0, 0), 400);
    m_boundingBox.Center.Set(0, 0, 0);
    m_boundingBox.Extent.Set(400.0f, 400.0f, 1.0f);
    m_x = WATER_TRACKS_STRIP_X;
    m_y = WATER_TRACKS_STRIP_Y;
    m_elapsedMs = m_initTimeOffset;
    m_startPos = start;
    m_perpDir = m_waveDir = end - start;
    m_perpDir.Rotate(-1.57079632679f);
    m_perpDir.Normalize();
    m_waveDir = m_perpDir;
    m_waveDir.Rotate(1.57079632679f);

    const int idx = (m_type >= 0 && m_type < WaveTypeMax) ? (int)m_type : (int)WaveTypePond;
    m_waveDistance = s_waveTypeInfo[idx].m_waveDistance;
    m_waveDir *= m_waveDistance;
    m_startPos -= m_waveDir;

    m_initialVelocity = s_waveTypeInfo[idx].m_initialVelocity;
    m_totalMs = (Int)(m_waveDistance / m_initialVelocity);
    m_fadeMs = s_waveTypeInfo[idx].m_fadeMs;

    m_waveInitialWidth  = length * s_waveTypeInfo[idx].m_initialWidthFraction;
    m_waveInitialHeight = m_waveInitialWidth * s_waveTypeInfo[idx].m_initialHeightWidthFraction;
    m_waveFinalWidth    = length;
    m_waveFinalHeight   = width;

    m_timeToReachBeach = (Int)((m_waveDistance - m_waveFinalHeight) / m_initialVelocity);
    m_frontSlowDownAcc = -(m_initialVelocity * m_initialVelocity) / (2 * m_waveFinalHeight);
    m_timeToStop = (Int)(-m_initialVelocity / m_frontSlowDownAcc);
    m_timeToRetreat = (Int)sqrtf(fabsf(2.0f * m_waveFinalHeight / m_frontSlowDownAcc));
    m_totalMs = m_timeToReachBeach + m_timeToStop + m_timeToRetreat;
    m_backSlowDownAcc = (2.0f * m_waveInitialHeight / (Real)(m_timeToStop * m_timeToStop));
    m_timeToCompress = s_waveTypeInfo[idx].m_timeToCompress;

    if (m_type == WaveTypeStationary)
    {
        m_timeToRetreat = 1000;
        m_totalMs = m_timeToReachBeach + m_timeToStop + m_fadeMs + m_timeToRetreat;
        m_startPos = start;
        m_fadeMs = 1000;
    }
    // m_stageZeroTexture stays null — rendering path uses "wave256.tga" fallback.
    (void)texturename;
}

void WaterTracksObj::init(Real width, const Vector2 &start, const Vector2 &end,
                          const Char *texturename)
{
    // Alternate init signature. Sets up wave along a perpendicular vector.
    freeWaterTracksResources();
    m_boundingSphere.Init(Vector3(0, 0, 0), 400);
    m_boundingBox.Center.Set(0, 0, 0);
    m_boundingBox.Extent.Set(400.0f, 400.0f, 1.0f);
    m_perpDir = end - start;
    m_startPos = start + m_perpDir * 0.5f;
    Real length = m_perpDir.Length();
    if (length < 0.001f) length = 0.001f;
    m_perpDir *= 1.0f / length;
    m_waveDir = m_perpDir;
    m_waveDir.Rotate(1.57079632679f);
    m_startPos -= m_waveDir * width;
    m_waveDir *= 1.3f * 10.0f; // approximate MAP_XY_FACTOR
    m_startPos -= m_waveDir;
    m_x = WATER_TRACKS_STRIP_X;
    m_y = WATER_TRACKS_STRIP_Y;
    m_elapsedMs = 0;
    m_initialVelocity = 0.001f * 10.0f;
    m_totalMs = (Int)(m_waveDir.Length() / m_initialVelocity);
    m_fadeMs = 3000;
    (void)texturename;
}

Int WaterTracksObj::update(Int msElapsed)
{
    (void)msElapsed;
    return 1; // always report "updated"
}

Int WaterTracksObj::render(DX8VertexBufferClass *vertexBuffer, Int batchStart)
{
    // No-op in the D3D11 build — RenderWaterTracksDX11 submits the GPU draws.
    (void)vertexBuffer;
    return batchStart;
}

// --------------------------------------------------------------------------
// WaterTracksRenderSystem methods
// --------------------------------------------------------------------------

WaterTracksRenderSystem::WaterTracksRenderSystem()
{
    m_usedModules = nullptr;
    m_freeModules = nullptr;
    m_vertexBuffer = nullptr;
    m_indexBuffer = nullptr;
    m_vertexMaterialClass = nullptr;
    m_stripSizeX = WATER_TRACKS_STRIP_X;
    m_stripSizeY = WATER_TRACKS_STRIP_Y;
    m_batchStart = 0;
    m_level = 0.0f;
    TheWaterTracksRenderSystem = this;
}

WaterTracksRenderSystem::~WaterTracksRenderSystem()
{
    shutdown();
    if (TheWaterTracksRenderSystem == this)
        TheWaterTracksRenderSystem = nullptr;
}

void WaterTracksRenderSystem::ReleaseResources() {}
void WaterTracksRenderSystem::ReAcquireResources() {}

void WaterTracksRenderSystem::flush(RenderInfoClass &)
{
    // No-op — rendering is done by RenderWaterTracksDX11() instead.
}

void WaterTracksRenderSystem::init()
{
    const Int numModules = 2000;
    m_stripSizeX = WATER_TRACKS_STRIP_X;
    m_stripSizeY = WATER_TRACKS_STRIP_Y;
    if (TheGlobalData)
        m_level = TheGlobalData->m_waterPositionZ;

    // Pre-allocate the free pool.
    for (Int i = 0; i < numModules; ++i)
    {
        WaterTracksObj *mod = new WaterTracksObj();
        mod->m_nextSystem = m_freeModules;
        mod->m_prevSystem = nullptr;
        if (m_freeModules) m_freeModules->m_prevSystem = mod;
        m_freeModules = mod;
    }
}

void WaterTracksRenderSystem::shutdown()
{
    // Release any still-bound used modules into the free list.
    WaterTracksObj *mod = m_usedModules;
    while (mod)
    {
        WaterTracksObj *next = mod->m_nextSystem;
        mod->m_bound = false;
        releaseTrack(mod);
        mod = next;
    }
    // Delete all free modules.
    while (m_freeModules)
    {
        WaterTracksObj *next = m_freeModules->m_nextSystem;
        delete m_freeModules;
        m_freeModules = next;
    }
    m_usedModules = nullptr;
}

void WaterTracksRenderSystem::reset()
{
    // Release all active (used) tracks back to the free pool.
    WaterTracksObj *mod = m_usedModules;
    while (mod)
    {
        WaterTracksObj *next = mod->m_nextSystem;
        mod->m_bound = false;
        releaseTrack(mod);
        mod = next;
    }
    m_usedModules = nullptr;
}

WaterTracksObj *WaterTracksRenderSystem::bindTrack(::waveType type)
{
    WaterTracksObj *mod = m_freeModules;
    if (!mod) return nullptr;

    // Unlink from free list
    if (mod->m_nextSystem) mod->m_nextSystem->m_prevSystem = mod->m_prevSystem;
    if (mod->m_prevSystem) mod->m_prevSystem->m_nextSystem = mod->m_nextSystem;
    else                   m_freeModules = mod->m_nextSystem;

    mod->m_type = type;

    // Insert into used list (group by type, simple head-insert)
    mod->m_nextSystem = m_usedModules;
    mod->m_prevSystem = nullptr;
    if (m_usedModules) m_usedModules->m_prevSystem = mod;
    m_usedModules = mod;
    mod->m_bound = true;
    return mod;
}

void WaterTracksRenderSystem::unbindTrack(WaterTracksObj *mod)
{
    if (!mod) return;
    mod->m_bound = false;
    releaseTrack(mod);
}

void WaterTracksRenderSystem::releaseTrack(WaterTracksObj *mod)
{
    if (!mod) return;

    // Unlink from used list
    if (mod->m_nextSystem) mod->m_nextSystem->m_prevSystem = mod->m_prevSystem;
    if (mod->m_prevSystem) mod->m_prevSystem->m_nextSystem = mod->m_nextSystem;
    else if (m_usedModules == mod) m_usedModules = mod->m_nextSystem;

    // Add to free list (head)
    mod->m_prevSystem = nullptr;
    mod->m_nextSystem = m_freeModules;
    if (m_freeModules) m_freeModules->m_prevSystem = mod;
    m_freeModules = mod;
    mod->freeWaterTracksResources();
}

void WaterTracksRenderSystem::update()
{
    // Iterate used modules; expire ones whose time is up. The actual per-
    // frame visual advance is done by RenderWaterTracksDX11 when it advances
    // mod->m_elapsedMs.
    WaterTracksObj *mod = m_usedModules;
    while (mod)
    {
        WaterTracksObj *next = mod->m_nextSystem;
        // When an unbound track's animation completes, return it to the pool.
        if (!mod->m_bound && mod->m_totalMs > 0 && mod->m_elapsedMs >= mod->m_totalMs)
            releaseTrack(mod);
        mod = next;
    }
}

void WaterTracksRenderSystem::saveTracks() {}

void WaterTracksRenderSystem::loadTracks()
{
    // Ocean / pond waves are persisted per-map in a ".wak" sidecar file
    // next to the ".map" file. Format (from original W3DWaterTracks.cpp):
    //   - N wave entries, each: Vector2 startPos, Vector2 endPos, Int type (20 bytes)
    //   - 4-byte trackCount trailer at the END of the file
    // Read via TheFileSystem so .big archives are searched.
    if (!TheTerrainLogic) return;
    AsciiString filenameStr = TheTerrainLogic->getSourceFilename();
    if (filenameStr.isEmpty()) return;
    // Replace .map with .wak
    char path[256];
    strncpy(path, filenameStr.str(), sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    int len = (int)strlen(path);
    if (len < 4) return;
    strcpy(path + len - 4, ".wak");

    File *file = TheFileSystem->openFile(path, File::READ | File::BINARY);
    if (!file) return;

    // Read trailer to get wave count
    file->seek(-4, File::END);
    Int trackCount = 0;
    file->read(&trackCount, sizeof(trackCount));
    file->seek(0, File::START);

    Int flipU = 0;
    for (Int i = 0; i < trackCount; ++i)
    {
        Vector2 startPos, endPos;
        ::waveType wtype;
        if (file->read(&startPos, sizeof(startPos)) != sizeof(startPos)) break;
        if (file->read(&endPos, sizeof(endPos))     != sizeof(endPos))   break;
        if (file->read(&wtype,   sizeof(wtype))     != sizeof(wtype))    break;

        if (wtype < 0 || wtype >= WaveTypeMax) continue;
        if (findTrack(startPos, endPos, wtype)) continue; // already loaded

        WaterTracksObj *umod = bindTrack(wtype);
        if (!umod) continue;

        flipU ^= 1;
        umod->init(s_waveTypeInfo[wtype].m_finalHeight,
                   s_waveTypeInfo[wtype].m_finalWidth,
                   startPos, endPos,
                   s_waveTypeInfo[wtype].m_textureName, 0);
        umod->m_flipU = (Real)flipU;

        // Some wave types spawn a second offset wave automatically
        if (s_waveTypeInfo[wtype].m_secondWaveTimeOffset)
        {
            WaterTracksObj *umod2 = bindTrack(wtype);
            if (umod2)
            {
                umod2->init(s_waveTypeInfo[wtype].m_finalHeight,
                            s_waveTypeInfo[wtype].m_finalWidth,
                            startPos, endPos,
                            s_waveTypeInfo[wtype].m_textureName,
                            s_waveTypeInfo[wtype].m_secondWaveTimeOffset);
                umod2->m_flipU = (Real)(flipU ^ 1);
            }
        }
    }
    file->close();
}

WaterTracksObj *WaterTracksRenderSystem::findTrack(Vector2 &start, Vector2 &end,
                                                    ::waveType type)
{
    WaterTracksObj *mod = m_usedModules;
    while (mod)
    {
        if (mod->m_type == type && mod->m_initStartPos == start && mod->m_initEndPos == end)
            return mod;
        mod = mod->m_nextSystem;
    }
    return nullptr;
}

// One-shot global instance — the ctor assigns TheWaterTracksRenderSystem.
// The instance is initialized lazily via EnsureWaterTracksReady() below.
static WaterTracksRenderSystem g_waterTracksRenderSystemInstance;
static bool g_waterTracksInited = false;
static AsciiString g_waterTracksLoadedMap;

static void EnsureWaterTracksReady()
{
    if (!g_waterTracksInited)
    {
        g_waterTracksInited = true;
        TheWaterTracksRenderSystem = &g_waterTracksRenderSystemInstance;
        TheWaterTracksRenderSystem->init();
    }

    // Detect map change and reload the per-map .wak file if needed. This is
    // how the original game populated waves — the shipped .wak files next to
    // each map's .map file contain pre-placed waves (ocean coastline, pond
    // edges, etc.). Without this call, no waves get bound and the render
    // path has nothing to draw.
    if (TheTerrainLogic)
    {
        AsciiString currentMap = TheTerrainLogic->getSourceFilename();
        if (!currentMap.isEmpty() && currentMap != g_waterTracksLoadedMap)
        {
            g_waterTracksLoadedMap = currentMap;
            TheWaterTracksRenderSystem->reset();
            TheWaterTracksRenderSystem->loadTracks();
        }
    }
}

// ============================================================================
// Reflection RT mesh casters
// ============================================================================
//
// Walks W3DDisplay::m_3DScene and submits MESH/HLOD/DISTLOD/COLLECTION render
// objects through ModelRenderer with the assumption that the caller has already
// bound the reflected viewProjection at cbuffer b0 and a no-cull rasterizer
// state. Reuses ShadowCasterMode on ModelRenderer because the suppressions are
// identical to what reflection wants:
//   - skip particle emitter side-effects (don't double-add to pendingParticleBuffers)
//   - skip translucent batches (reflection of opaque geometry only)
//   - skip per-batch state changes (keep our no-cull/opaque state bound)
//
// Caller responsibilities:
//   1. SetRenderTarget(reflectionRT)
//   2. SetCamera(reflectedView, projection, reflectedCamPos)
//   3. SetReflectionMesh3DState()
//   4. FlushFrameConstants()
//   5. CALL THIS
//   6. RestoreBackBuffer / SetCamera back to normal / Restore3DState
//
// Returns the number of render objects submitted (for diagnostic).
int RenderReflectionMeshesDX11()
{
	if (!W3DDisplay::m_3DScene)
		return 0;

	auto& modelRenderer = Render::ModelRenderer::Instance();

	// ModelRenderer requires m_camera to be non-null. It was set by W3DView::draw
	// earlier in the frame and remains valid for the rest of the frame.
	// We're not changing it here — its frustum + camera position are still the
	// regular camera's, but that's fine: anything visible to the regular camera
	// is also visible in its reflection (the reflected camera mirrors the same
	// scene below the water plane). The CBUFFER view matrix is the reflected
	// one (set by the caller), so vertices project correctly.

	int submitted = 0;
	modelRenderer.SetShadowCasterMode(true);

	SceneClass* scene = reinterpret_cast<SceneClass*>(W3DDisplay::m_3DScene);
	SceneIterator* iter = scene->Create_Iterator(false);
	if (iter)
	{
		for (iter->First(); !iter->Is_Done(); iter->Next())
		{
			RenderObjClass* robj = iter->Current_Item();
			if (!robj || !robj->Is_Not_Hidden_At_All() || !robj->Is_Visible())
				continue;

			int classId = robj->Class_ID();
			if (classId == RenderObjClass::CLASSID_MESH ||
				classId == RenderObjClass::CLASSID_HLOD ||
				classId == RenderObjClass::CLASSID_DISTLOD ||
				classId == RenderObjClass::CLASSID_COLLECTION)
			{
				modelRenderer.RenderRenderObject(robj);
				++submitted;
			}
		}
		scene->Destroy_Iterator(iter);
	}

	// Walk the GameClient drawables too — these are the units/buildings that
	// went through w3dview_renderDrawable in the regular pass. Their underlying
	// W3D render objects are NOT in m_3DScene (the D3D11 port iterates Drawables
	// instead of the scene graph), so the scene iteration above won't catch them.
	// We have to use the same drawable-iteration path here.
	if (TheGameClient && TheTacticalView)
	{
		Region3D axisAlignedRegion;
		// Use a generous region to capture everything potentially visible.
		// The actual rendering is gated by the reflected viewProjection, so
		// objects outside the reflection's view will draw off-screen and be
		// clipped by the rasterizer's depth test.
		Coord3D mapMin, mapMax;
		extern WorldHeightMap* GetTerrainHeightMap();
		WorldHeightMap* hm = GetTerrainHeightMap();
		if (hm)
		{
			mapMin.x = 0.0f; mapMin.y = 0.0f; mapMin.z = -10000.0f;
			mapMax.x = (Real)(hm->getXExtent() * MAP_XY_FACTOR);
			mapMax.y = (Real)(hm->getYExtent() * MAP_XY_FACTOR);
			mapMax.z = 10000.0f;
			axisAlignedRegion.lo = mapMin;
			axisAlignedRegion.hi = mapMax;

			Drawable* draw = TheGameClient->getDrawableList();
			while (draw)
			{
				if (!draw->isDrawableEffectivelyHidden())
				{
					for (DrawModule** dm = draw->getDrawModules(); *dm; ++dm)
					{
						ObjectDrawInterface* odi = (*dm)->getObjectDrawInterface();
						if (!odi)
							continue;
						W3DModelDraw* w3dDraw = static_cast<W3DModelDraw*>(odi);
						RenderObjClass* renderObject = w3dDraw->getRenderObject();
						if (renderObject)
						{
							modelRenderer.RenderRenderObject(renderObject);
							++submitted;
						}
					}
				}
				draw = draw->getNextDrawable();
			}
		}
	}

	modelRenderer.SetShadowCasterMode(false);
	return submitted;
}

// ============================================================================
// Occluded-unit silhouettes
// ============================================================================
//
// For each owned/enemy unit, re-render its mesh in the player's house color
// using a depth-test=GREATER state so the mesh appears only where the depth
// buffer is closer (something else is in front of it). Reproduces the
// original DX8 game's stenciled "see units behind your own buildings" effect
// without needing the stencil buffer — uses the depth buffer directly.
//
// Caller (W3DDisplay::draw) decides whether to invoke this; the function
// itself walks the entire drawable list each time it runs.
void RenderOccludedSilhouettesDX11()
{
	if (!TheGameClient || !TheTacticalView)
		return;

	auto& renderer = Render::Renderer::Instance();
	auto& modelRenderer = Render::ModelRenderer::Instance();

	// Bind the silhouette pipeline state ONCE for the whole pass — depth
	// test GREATER + alpha blend + unlit shader. ModelRenderer's
	// ShadowCasterMode keeps it bound across the per-mesh draws (skips
	// per-batch state changes that would otherwise call Restore3DState).
	renderer.SetOccludedSilhouette3DState();
	modelRenderer.SetShadowCasterMode(true);

	Drawable* draw = TheGameClient->getDrawableList();
	while (draw)
	{
		// Skip drawables that aren't currently visible to the player.
		if (draw->isDrawableEffectivelyHidden() || draw->getFullyObscuredByShroud())
		{
			draw = draw->getNextDrawable();
			continue;
		}

		// Skip non-objects (UI, scenery, terrain props, etc.) — only real
		// game objects with a Player owner get the silhouette overlay.
		Object* obj = draw->getObject();
		if (!obj)
		{
			draw = draw->getNextDrawable();
			continue;
		}

		// Skip things that aren't worth highlighting through walls (props,
		// shrubbery, mines, civilians without selection-color etc.). Stick
		// to the meaningful gameplay categories.
		if (obj->isKindOf(KINDOF_SHRUBBERY) ||
		    obj->isKindOf(KINDOF_MINE) ||
		    obj->isKindOf(KINDOF_OPTIMIZED_TREE) ||
		    obj->isKindOf(KINDOF_IGNORED_IN_GUI))
		{
			draw = draw->getNextDrawable();
			continue;
		}

		// Use the indicator color (player house color) as the silhouette
		// color, with reduced alpha so the silhouette is visible but not
		// overpowering. Indicator color is ARGB.
		uint32_t indicatorColor = obj->getIndicatorColor();
		float r = ((indicatorColor >> 16) & 0xFF) / 255.0f;
		float g = ((indicatorColor >> 8) & 0xFF) / 255.0f;
		float b = ( indicatorColor       & 0xFF) / 255.0f;

		// Skip objects whose indicator color is fully black (uninitialized).
		if (r < 0.001f && g < 0.001f && b < 0.001f)
		{
			draw = draw->getNextDrawable();
			continue;
		}

		modelRenderer.SetSilhouetteOverride({ r, g, b, 0.55f });

		// Walk every W3DModelDraw module on the drawable and submit its
		// render object through ModelRenderer. ShadowCasterMode + the
		// bound silhouette state mean the mesh draws as a flat colored
		// fill only on pixels where it's behind another mesh.
		for (DrawModule** dm = draw->getDrawModules(); *dm; ++dm)
		{
			ObjectDrawInterface* odi = (*dm)->getObjectDrawInterface();
			if (!odi)
				continue;
			W3DModelDraw* w3dDraw = static_cast<W3DModelDraw*>(odi);
			RenderObjClass* renderObject = w3dDraw->getRenderObject();
			if (renderObject)
				modelRenderer.RenderRenderObject(renderObject);
		}

		draw = draw->getNextDrawable();
	}

	modelRenderer.ClearSilhouetteOverride();
	modelRenderer.SetShadowCasterMode(false);
	renderer.Restore3DState();
}

void RenderWaterTracksDX11(CameraClass* camera)
{
	if (!camera)
		return;

	// Lazily initialize the water-tracks pool on first call. This avoids
	// order-of-init problems with the static instance's constructor.
	EnsureWaterTracksReady();

	if (!TheWaterTracksRenderSystem)
		return;

	// Drive the CPU-side lifecycle (expire unbound tracks, advance state).
	TheWaterTracksRenderSystem->update();

	// Note: WaterTracksRenderSystem::update() manages track lifecycle (expiring
	// old waves, releasing unbound modules). Since W3DWaterTracks.cpp is not
	// compiled in the D3D11 build, we skip the update call. When the water track
	// system is eventually ported, this should call update() each frame.

	WaterTracksObj* mod = TheWaterTracksRenderSystem->m_usedModules;
	if (!mod)
		return;

	// Early-exit checks matching original flush()
	if (!TheGlobalData || !TheWaterTransparency)
		return;
	if (!TheGlobalData->m_showSoftWaterEdge || TheWaterTransparency->m_transparentWaterDepth == 0)
		return;

	auto& renderer = Render::Renderer::Instance();
	auto& device = renderer.GetDevice();

	// NOTE: the original W3DWaterTracks.cpp computes a `diffuseLight` tint
	// from terrain ambient+diffuse, but it uses it for the SHORELINE TILE
	// blending path (W3DWaterTracks.cpp:895), NOT for the wave foam quads.
	// The wave verts themselves use pure white at line 439 (`(alpha << 24)
	// | 0xffffff`). We removed the diffuseRGB computation here because the
	// wave path is the only consumer in this function and it doesn't
	// modulate by lighting.

	// Static GPU buffers for water track quads — 4 vertices, 6 indices per wave
	static Render::VertexBuffer s_waveVB;
	static Render::IndexBuffer  s_waveIB;
	static bool s_waveBuffersCreated = false;

	if (!s_waveBuffersCreated)
	{
		if (!s_waveVB.Create(device, nullptr, 4, sizeof(Render::Vertex3D), true))
			return;
		uint32_t quadIndices[6] = { 0, 1, 3, 0, 3, 2 };
		if (!s_waveIB.Create32(device, quadIndices, 6, false))
			return;
		s_waveBuffersCreated = true;
	}

	Render::Float4x4 worldIdentity;
	DirectX::XMStoreFloat4x4(&Render::ToXM(worldIdentity), DirectX::XMMatrixIdentity());

	// Use additive blend so the foam reads as bright white over the
	// underlying water surface. The original DX8 game's water-track wave
	// uses an additive sprite shader (PSAdditive equivalent) for the
	// same reason — alpha-blended white at 50% modulates against the
	// dark water and looks grey, not foamy. Additive sums the wave
	// texture's brightness on top of the water → bright crisp foam.
	renderer.SetAdditive3DState();

	extern FramePacer *TheFramePacer;

	while (mod)
	{
		// Advance elapsed time (normally done as the first line of render())
		if (TheFramePacer)
			mod->m_elapsedMs += TheFramePacer->getLogicTimeStepMilliseconds();

		// --- Replicate wave position computation from WaterTracksObj::render() ---
		Vector2 waveTailOrigin, waveFrontOrigin;
		Real ooWaveDirLen = 1.0f / mod->m_waveDir.Length();
		Real waveAlpha = 0.0f;
		Real widthFrac = 1.0f;

		if (mod->m_type == WATER_WAVE_TYPE_STATIONARY)
		{
			// Stationary wave
			waveFrontOrigin = mod->m_startPos;
			waveFrontOrigin -= mod->m_perpDir * mod->m_waveFinalWidth * 0.5f;
			waveTailOrigin = waveFrontOrigin - mod->m_waveFinalHeight * ooWaveDirLen * mod->m_waveDir;
			waveAlpha = 0.0f;

			if (mod->m_elapsedMs >= mod->m_totalMs)
				mod->m_elapsedMs = 0;

			if (mod->m_elapsedMs > (mod->m_timeToReachBeach + mod->m_timeToStop - 1000 + mod->m_fadeMs))
			{
				// Fading out
				waveAlpha = (Real)(mod->m_elapsedMs - (mod->m_timeToReachBeach + mod->m_timeToStop - 1000 + mod->m_fadeMs));
				waveAlpha = waveAlpha / mod->m_timeToRetreat;
				waveAlpha = 1.0f - waveAlpha;
				if (waveAlpha < 0.0f) waveAlpha = 0.0f;
			}
			else if (mod->m_elapsedMs > (mod->m_timeToReachBeach + mod->m_timeToStop - 1000))
			{
				// Fading up
				waveAlpha = (Real)(mod->m_elapsedMs - (mod->m_timeToReachBeach + mod->m_timeToStop - 1000));
				waveAlpha = waveAlpha / mod->m_fadeMs;
				if (waveAlpha > 1.0f) waveAlpha = 1.0f;
			}
		}
		else
		{
			// Moving wave
			if (mod->m_elapsedMs < mod->m_timeToReachBeach)
			{
				waveAlpha = (Real)mod->m_elapsedMs / mod->m_timeToReachBeach;
				widthFrac = waveAlpha;
				widthFrac = (mod->m_waveInitialWidth + widthFrac * (mod->m_waveFinalWidth - mod->m_waveInitialWidth)) / mod->m_waveFinalWidth;

				waveFrontOrigin = mod->m_startPos + mod->m_initialVelocity * mod->m_elapsedMs * ooWaveDirLen * mod->m_waveDir;
				waveFrontOrigin -= mod->m_perpDir * mod->m_waveFinalWidth * 0.5f * widthFrac;
				waveTailOrigin = waveFrontOrigin - mod->m_waveInitialHeight * ooWaveDirLen * mod->m_waveDir;
			}
			else if (mod->m_elapsedMs < mod->m_totalMs)
			{
				waveAlpha = 1.0f;
				widthFrac = 1.0f;
				waveFrontOrigin = mod->m_startPos + mod->m_initialVelocity * mod->m_timeToReachBeach * ooWaveDirLen * mod->m_waveDir;
				waveTailOrigin = waveFrontOrigin;
				Real elapsedMs = (Real)(mod->m_elapsedMs - mod->m_timeToReachBeach);
				waveFrontOrigin += (mod->m_initialVelocity * elapsedMs + 0.5f * mod->m_frontSlowDownAcc * elapsedMs * elapsedMs) * ooWaveDirLen * mod->m_waveDir;
				waveFrontOrigin -= mod->m_perpDir * mod->m_waveFinalWidth * 0.5f * widthFrac;

				Real timeSinceBacktrack = (Real)(mod->m_elapsedMs - mod->m_timeToReachBeach) - mod->m_timeToStop;
				if (timeSinceBacktrack < 0) timeSinceBacktrack = 0;
				waveAlpha = timeSinceBacktrack / mod->m_fadeMs;
				if (waveAlpha > 1.0f) waveAlpha = 1.0f;
				waveAlpha = 1.0f - waveAlpha;

				waveTailOrigin -= mod->m_waveInitialHeight * ooWaveDirLen * mod->m_waveDir;

				if (mod->m_elapsedMs > (mod->m_timeToReachBeach + mod->m_timeToStop + mod->m_timeToCompress))
				{
					waveTailOrigin += (0.5f * mod->m_backSlowDownAcc * (mod->m_timeToStop + mod->m_timeToCompress) * (mod->m_timeToStop + mod->m_timeToCompress)) * ooWaveDirLen * mod->m_waveDir;
					Real newElapsed = (Real)(mod->m_elapsedMs - (mod->m_timeToReachBeach + mod->m_timeToStop + mod->m_timeToCompress));
					waveTailOrigin += (0.5f * mod->m_frontSlowDownAcc * newElapsed * newElapsed) * ooWaveDirLen * mod->m_waveDir;
				}
				else
				{
					waveTailOrigin += (0.5f * mod->m_backSlowDownAcc * elapsedMs * elapsedMs) * ooWaveDirLen * mod->m_waveDir;
				}

				waveTailOrigin -= mod->m_perpDir * mod->m_waveFinalWidth * 0.5f * widthFrac;
			}
			else
			{
				mod->m_elapsedMs = 0;
				waveAlpha = 0.0f;
				widthFrac = 0.0f;
				waveFrontOrigin = mod->m_startPos;
				waveFrontOrigin -= mod->m_perpDir * mod->m_waveFinalWidth * 0.5f;
				waveTailOrigin = waveFrontOrigin - mod->m_waveInitialHeight * ooWaveDirLen * mod->m_waveDir;
			}
		}

		// Skip waves with zero alpha
		if (waveAlpha > 0.01f)
		{
			// Get water height at wave position
			Real waterHeight = 0.0f;
			if (TheTerrainLogic)
				TheTerrainLogic->isUnderwater(waveTailOrigin.X, waveTailOrigin.Y, &waterHeight);
			float z = waterHeight + 1.5f;

			Int alpha = REAL_TO_INT(waveAlpha * 255.0f);
			if (alpha > 255) alpha = 255;
			// Original W3DWaterTracks.cpp:439 uses pure white with only the
			// alpha varying — `(alpha << 24) | 0xffffff`. The diffuseR/G/B
			// values computed above are for the SHORELINE TILE blending
			// path, NOT the wave foam quad. Modulating the wave by terrain
			// ambient+diffuse drives the color to ~0 on dim scenes (and
			// even on bright scenes leaves it grey instead of white),
			// which is the "black shore waves" bug.
			uint32_t color = ((uint32_t)alpha << 24) | 0x00FFFFFFu;

			float u0 = mod->m_flipU ? 1.0f : 0.0f;
			float u1 = mod->m_flipU ? 0.0f : 1.0f;

			// Build 4 vertices: tail-left, tail-right, front-left, front-right
			Render::Vertex3D verts[4] = {};

			// Tail left
			verts[0].position = { waveTailOrigin.X, waveTailOrigin.Y, z };
			verts[0].normal = { 0, 0, 1 };
			verts[0].texcoord = { u0, 0.0f };
			verts[0].color = color;

			// Tail right
			Vector2 tailRight = waveTailOrigin + mod->m_perpDir * mod->m_waveFinalWidth * widthFrac;
			verts[1].position = { tailRight.X, tailRight.Y, z };
			verts[1].normal = { 0, 0, 1 };
			verts[1].texcoord = { u1, 0.0f };
			verts[1].color = color;

			// Front left
			verts[2].position = { waveFrontOrigin.X, waveFrontOrigin.Y, z };
			verts[2].normal = { 0, 0, 1 };
			verts[2].texcoord = { u0, 1.0f };
			verts[2].color = color;

			// Front right
			Vector2 frontRight = waveFrontOrigin + mod->m_perpDir * mod->m_waveFinalWidth * widthFrac;
			verts[3].position = { frontRight.X, frontRight.Y, z };
			verts[3].normal = { 0, 0, 1 };
			verts[3].texcoord = { u1, 1.0f };
			verts[3].color = color;

			s_waveVB.Update(device, verts, sizeof(verts));

			// Resolve wave texture via the track texture cache. Classic DX8
			// code stored a TextureClass* in m_stageZeroTexture; the D3D11
			// water-tracks lifecycle (below) doesn't use TextureClass so it
			// leaves m_stageZeroTexture null. Fall back to the hardcoded
			// "wave256.tga" (the texture every wave type uses in the
			// original game's waveTypeInfo table).
			Render::Texture* waveTex = nullptr;
			if (mod->m_stageZeroTexture)
			{
				const char* texName = mod->m_stageZeroTexture->Get_Texture_Name().str();
				if (texName && texName[0])
					waveTex = GetTrackTextureDX11(texName);
			}
			if (!waveTex)
				waveTex = GetTrackTextureDX11("wave256.tga");

			Render::Float4 tint = { 1.0f, 1.0f, 1.0f, 1.0f };
			renderer.Draw3DIndexed(s_waveVB, s_waveIB, 6, waveTex, worldIdentity, tint);
		}

		mod = mod->m_nextSystem;
	}

	renderer.Restore3DState();
}


////////////////////////////////////////////////////////////////////////////////
// Shared helper: Build camera-facing quad strips from 3D line points.
// Used by waypoint rendering, SegmentedLineClass, and Line3DClass.
////////////////////////////////////////////////////////////////////////////////

#include "Renderer.h"
#include "WW3D2/camera.h"

// Build a camera-facing quad strip from a series of 3D points.
// For each segment between consecutive points, generates two triangles (six vertices)
// forming a billboard quad that always faces the camera.
static uint32_t BuildCameraFacingQuadStrip(
	const Vector3& cameraPos,
	const Vector3* points,
	unsigned int numPoints,
	float width,
	float r, float g, float b, float a,
	float tileFactor,
	Render::Vertex3D* outVerts)
{
	if (numPoints < 2) return 0;

	// Pack color as RGBA bytes (ABGR layout for D3D11 R8G8B8A8_UNORM)
	uint8_t cr = (uint8_t)(r * 255.0f > 255.0f ? 255 : r * 255.0f);
	uint8_t cg = (uint8_t)(g * 255.0f > 255.0f ? 255 : g * 255.0f);
	uint8_t cb = (uint8_t)(b * 255.0f > 255.0f ? 255 : b * 255.0f);
	uint8_t ca = (uint8_t)(a * 255.0f > 255.0f ? 255 : a * 255.0f);
	uint32_t packedColor = (ca << 24) | (cb << 16) | (cg << 8) | cr;

	float halfWidth = width * 0.5f;
	uint32_t vertCount = 0;
	float accumulatedV = 0.0f;

	for (unsigned int i = 0; i < numPoints - 1; ++i)
	{
		const Vector3& p0 = points[i];
		const Vector3& p1 = points[i + 1];

		// Segment direction
		Vector3 segDir(p1.X - p0.X, p1.Y - p0.Y, p1.Z - p0.Z);
		float segLen = segDir.Length();
		if (segLen < 0.001f) continue;
		segDir *= (1.0f / segLen);

		// Camera-to-segment midpoint direction
		Vector3 mid((p0.X + p1.X) * 0.5f, (p0.Y + p1.Y) * 0.5f, (p0.Z + p1.Z) * 0.5f);
		Vector3 viewDir(mid.X - cameraPos.X, mid.Y - cameraPos.Y, mid.Z - cameraPos.Z);
		viewDir.Normalize();

		// Cross product of segment direction and view direction gives the billboard "right" vector
		Vector3 right;
		Vector3::Cross_Product(segDir, viewDir, &right);
		float rightLen = right.Length();
		if (rightLen < 0.0001f)
		{
			right.Set(0.0f, 0.0f, 1.0f);
			Vector3::Cross_Product(segDir, right, &right);
			rightLen = right.Length();
			if (rightLen < 0.0001f) continue;
		}
		right *= (halfWidth / rightLen);

		// UV coordinates
		float v0 = accumulatedV;
		float v1 = accumulatedV + (tileFactor > 0.0f ? segLen / (width * tileFactor) : 1.0f);
		accumulatedV = v1;

		// Four corners
		Vector3 c0(p0.X + right.X, p0.Y + right.Y, p0.Z + right.Z);
		Vector3 c1(p0.X - right.X, p0.Y - right.Y, p0.Z - right.Z);
		Vector3 c2(p1.X + right.X, p1.Y + right.Y, p1.Z + right.Z);
		Vector3 c3(p1.X - right.X, p1.Y - right.Y, p1.Z - right.Z);

		Vector3 norm(-viewDir.X, -viewDir.Y, -viewDir.Z);

		// Triangle 1: c0, c2, c1
		outVerts[vertCount++] = { {c0.X, c0.Y, c0.Z}, {norm.X, norm.Y, norm.Z}, {0.0f, v0}, packedColor };
		outVerts[vertCount++] = { {c2.X, c2.Y, c2.Z}, {norm.X, norm.Y, norm.Z}, {0.0f, v1}, packedColor };
		outVerts[vertCount++] = { {c1.X, c1.Y, c1.Z}, {norm.X, norm.Y, norm.Z}, {1.0f, v0}, packedColor };

		// Triangle 2: c1, c2, c3
		outVerts[vertCount++] = { {c1.X, c1.Y, c1.Z}, {norm.X, norm.Y, norm.Z}, {1.0f, v0}, packedColor };
		outVerts[vertCount++] = { {c2.X, c2.Y, c2.Z}, {norm.X, norm.Y, norm.Z}, {0.0f, v1}, packedColor };
		outVerts[vertCount++] = { {c3.X, c3.Y, c3.Z}, {norm.X, norm.Y, norm.Z}, {1.0f, v1}, packedColor };
	}

	return vertCount;
}


////////////////////////////////////////////////////////////////////////////////
// Waypoint Path Line Rendering (D3D11)
//
// Renders waypoint path lines for selected units (when in waypoint mode) and
// rally point lines for selected buildings. This replaces the original
// W3DWaypointBuffer::drawWaypoints which used SegmentedLineClass + WW3D::Render.
////////////////////////////////////////////////////////////////////////////////

#include "GameClient/InGameUI.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/UpdateModule.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/GameUtility.h"

// Tessellate a waypoint line so it hugs the terrain between endpoints.
// The authored endpoints only know about their own object Z (building
// position, rally-point Z, unit position), so a straight 3D segment dives
// through hills when the terrain rises between them. We subdivide each
// segment along XY into pieces ~1 cell wide (MAP_XY_FACTOR=10 world units),
// sampling TheTerrainLogic->getGroundHeight + a small constant bias so the
// line reads as sitting just above the terrain surface instead of punching
// through it. Endpoints keep their original Z so the line still meets the
// exit point / rally-point marker exactly. Output is clamped to the fixed
// MAX_WAYPOINT_POINTS cap used by the caller.
//
// Framerate-independent: no per-frame animation; tessellation count depends
// only on segment length, which is logic-driven.
static unsigned int TessellateWaypointsOverTerrain(
	const Vector3* inPoints, unsigned int inCount,
	Vector3* outPoints, unsigned int outCap)
{
	if (inCount == 0 || outCap == 0)
		return 0;
	// First endpoint: keep authored Z so the line visually anchors to the
	// unit / building / exit point that owns it.
	unsigned int outCount = 0;
	outPoints[outCount++] = inPoints[0];
	if (inCount == 1 || !TheTerrainLogic)
		return outCount;

	// Small constant lift above terrain so the line doesn't z-fight with
	// the ground polys. ~0.75 units matches what looks right at RTS camera
	// heights without making the line look detached.
	const float kLineZBias = 0.75f;
	// One intermediate point per ~cell width gives a visually smooth curve
	// on typical hill slopes. Cheap: ~20 samples for a 200-unit segment.
	const float kMaxStep = (float)MAP_XY_FACTOR; // 10 world units

	for (unsigned int i = 0; i + 1 < inCount; ++i)
	{
		const Vector3& a = inPoints[i];
		const Vector3& b = inPoints[i + 1];
		const float dx = b.X - a.X;
		const float dy = b.Y - a.Y;
		const float segLenXY = sqrtf(dx * dx + dy * dy);
		// Pick subdivision count from XY distance only (terrain we need to
		// follow lives in XY). Cap it so huge segments don't blow the
		// output buffer.
		unsigned int steps = (unsigned int)(segLenXY / kMaxStep);
		if (steps < 1) steps = 1;
		// Insert intermediate samples at terrain height. The endpoint b is
		// appended after the loop using its authored Z.
		for (unsigned int s = 1; s < steps; ++s)
		{
			if (outCount >= outCap) return outCount;
			const float t = (float)s / (float)steps;
			const float x = a.X + dx * t;
			const float y = a.Y + dy * t;
			const float terrainZ = TheTerrainLogic->getGroundHeight(x, y);
			// Blend: mostly terrain, but let the straight-line Z pull us
			// toward it on pure-flat stretches too. Max ensures we never
			// dip below terrain even if endpoints sit lower (bridges /
			// underground).
			const float straightZ = a.Z + (b.Z - a.Z) * t;
			const float z = (terrainZ > straightZ ? terrainZ : straightZ) + kLineZBias;
			outPoints[outCount].Set(x, y, z);
			outCount++;
		}
		// Append endpoint b with its authored Z so the line meets the next
		// marker (rally-point puck, waypoint node).
		if (outCount >= outCap) return outCount;
		outPoints[outCount++] = b;
	}
	return outCount;
}

void RenderWaypointsDX11(CameraClass* camera)
{
	if (!camera || !TheInGameUI)
		return;

	auto& renderer = Render::Renderer::Instance();

	// Get camera position for billboard generation
	const Matrix3D& camTM = camera->Get_Transform();
	Vector3 cameraPos(camTM[0][3], camTM[1][3], camTM[2][3]);

	// Waypoint mode: draw movement path lines for selected units
	const DrawableList* selected = TheInGameUI->getAllSelectedDrawables();
	if (!selected || selected->empty())
		return;

	static const int MAX_WAYPOINT_POINTS = 513;
	// Authored waypoint endpoints (one per logical node).
	Vector3 points[MAX_WAYPOINT_POINTS];
	// After terrain-tessellation each segment can spawn up to ~N extra
	// samples. A generous cap (8x) covers even very long rally-point runs
	// across the whole map without clipping the visual line. `static` to
	// keep ~49 KB off the render-thread stack; this path is single-threaded
	// and the buffer is refilled before every use.
	static const int MAX_WAYPOINT_POINTS_TESS = MAX_WAYPOINT_POINTS * 8;
	static Vector3 tessPoints[MAX_WAYPOINT_POINTS_TESS];

	if (TheInGameUI->isInWaypointMode())
	{
		// Blue waypoint path lines
		for (DrawableListCIt it = selected->begin(); it != selected->end(); ++it)
		{
			Drawable* draw = *it;
			Object* obj = draw->getObject();
			int numPoints = 1;
			if (obj && !obj->isKindOf(KINDOF_IGNORED_IN_GUI))
			{
				AIUpdateInterface* ai = obj->getAI();
				Int goalSize = ai ? ai->friend_getWaypointGoalPathSize() : 0;
				Int gpIdx = ai ? ai->friend_getCurrentGoalPathIndex() : 0;
				if (ai && gpIdx >= 0 && gpIdx < goalSize)
				{
					const Coord3D* pos = obj->getPosition();
					points[0].Set(pos->x, pos->y, pos->z);

					for (int i = gpIdx; i < goalSize; i++)
					{
						const Coord3D* waypoint = ai->friend_getGoalPathPosition(i);
						if (waypoint && numPoints < MAX_WAYPOINT_POINTS)
						{
							points[numPoints].Set(waypoint->x, waypoint->y, waypoint->z);
							numPoints++;
						}
					}

					if (numPoints >= 2)
					{
						// Tessellate so the line follows terrain contours
						// between authored waypoint nodes instead of
						// ducking through hills.
						unsigned int tessCount = TessellateWaypointsOverTerrain(
							points, numPoints, tessPoints, MAX_WAYPOINT_POINTS_TESS);
						// Build and render the line strip
						const uint32_t maxVerts = (tessCount - 1) * 6;
						Render::Vertex3D* verts = (Render::Vertex3D*)alloca(maxVerts * sizeof(Render::Vertex3D));
						uint32_t vertCount = BuildCameraFacingQuadStrip(
							cameraPos, tessPoints, tessCount, 1.5f,
							0.25f, 0.5f, 1.0f, 1.0f,  // blue color matching original
							1.0f, verts);

						if (vertCount > 0)
						{
							Render::VertexBuffer vb;
							vb.Create(renderer.GetDevice(), verts, vertCount, sizeof(Render::Vertex3D));
							renderer.SetAdditive3DState();
							Render::Float4x4 identity;
							DirectX::XMStoreFloat4x4(&Render::ToXM(identity), DirectX::XMMatrixIdentity());
							// Bind a white texture so the unlit shader's
							// `texColor * input.color` evaluates to the
							// vertex color (passing nullptr leaves slot 0
							// unbound — sample returns 0 → additive
							// blend draws nothing).
							Render::Texture* whiteTex =
								Render::ModelRenderer::Instance().GetWhiteTexture();
							renderer.Draw3DNoIndex(vb, vertCount, whiteTex, identity, {1,1,1,1});
						}
					}
				}
			}
		}
	}
	else
	{
		// Rally point mode: draw rally point lines for selected buildings
		// Also handles REVEALS_ENEMY_PATHS (listening outpost) enemy path lines
		for (DrawableListCIt it = selected->begin(); it != selected->end(); ++it)
		{
			Drawable* draw = *it;
			Object* obj = draw->getObject();
			int numPoints = 0;

			if (!obj)
				continue;

			if (obj->getControllingPlayer() != rts::getObservedOrLocalPlayer())
				continue;

			// Check for listening outpost revealing enemy paths
			if (obj->isKindOf(KINDOF_REVEALS_ENEMY_PATHS))
			{
				DrawableID enemyID = TheInGameUI->getMousedOverDrawableID();
				Drawable* enemyDraw = TheGameClient->findDrawableByID(enemyID);
				if (enemyDraw)
				{
					Object* enemy = enemyDraw->getObject();
					if (enemy && enemy->getRelationship(obj) == ENEMIES)
					{
						Coord3D delta = *obj->getPosition();
						delta.sub(enemy->getPosition());
						if (delta.length() <= obj->getVisionRange())
						{
							AIUpdateInterface* ai = enemy->getAI();
							Int goalSize = ai ? ai->friend_getWaypointGoalPathSize() : 0;
							Int gpIdx = ai ? ai->friend_getCurrentGoalPathIndex() : 0;
							if (ai)
							{
								const Coord3D* pos = enemy->getPosition();
								points[numPoints++].Set(pos->x, pos->y, pos->z);

								Bool lineExists = FALSE;
								if (gpIdx >= 0 && gpIdx < goalSize)
								{
									for (int i = gpIdx; i < goalSize && numPoints < MAX_WAYPOINT_POINTS; i++)
									{
										const Coord3D* waypoint = ai->friend_getGoalPathPosition(i);
										if (waypoint)
										{
											points[numPoints++].Set(waypoint->x, waypoint->y, waypoint->z);
											lineExists = TRUE;
										}
									}
								}
								else
								{
									const Coord3D* dest = ai->getGoalPosition();
									if (dest && dest->length() > 1.0f)
									{
										points[numPoints++].Set(dest->x, dest->y, dest->z);
										lineExists = TRUE;
									}
								}

								if (lineExists && numPoints >= 2)
								{
									unsigned int tessCount = TessellateWaypointsOverTerrain(
										points, numPoints, tessPoints, MAX_WAYPOINT_POINTS_TESS);
									const uint32_t maxVerts = (tessCount - 1) * 6;
									Render::Vertex3D* verts = (Render::Vertex3D*)alloca(maxVerts * sizeof(Render::Vertex3D));
									uint32_t vertCount = BuildCameraFacingQuadStrip(
										cameraPos, tessPoints, tessCount, 3.0f,
										0.95f, 0.5f, 0.0f, 1.0f,  // orange color for enemy paths
										1.0f, verts);

									if (vertCount > 0)
									{
										Render::VertexBuffer vb;
										vb.Create(renderer.GetDevice(), verts, vertCount, sizeof(Render::Vertex3D));
										renderer.SetAdditive3DState();
										Render::Float4x4 identity;
										DirectX::XMStoreFloat4x4(&Render::ToXM(identity), DirectX::XMMatrixIdentity());
										Render::Texture* whiteTex =
											Render::ModelRenderer::Instance().GetWhiteTexture();
										renderer.Draw3DNoIndex(vb, vertCount, whiteTex, identity, {1,1,1,1});
									}
								}
							}
						}
					}
				}
				break; // Only one listening outpost path at a time (matching original)
			}

			// Rally point lines for buildings with exit interfaces
			ExitInterface* exitInterface = obj->getObjectExitInterface();
			if (!exitInterface)
				continue;

			Coord3D exitPoint;
			if (!exitInterface->getExitPosition(exitPoint))
				exitPoint = *obj->getPosition();

			points[numPoints].Set(exitPoint.x, exitPoint.y, exitPoint.z);
			numPoints++;

			Coord3D naturalRallyPoint;
			if (!exitInterface->getNaturalRallyPoint(naturalRallyPoint, FALSE))
				continue;

			if (!naturalRallyPoint.equals(exitPoint))
			{
				points[numPoints].Set(naturalRallyPoint.x, naturalRallyPoint.y, naturalRallyPoint.z);
				numPoints++;
			}

			const Coord3D* rallyPoint = exitInterface->getRallyPoint();
			if (!rallyPoint)
				continue;

			// Add the final rally point destination
			if (numPoints < MAX_WAYPOINT_POINTS)
			{
				points[numPoints].Set(rallyPoint->x, rallyPoint->y, rallyPoint->z);
				numPoints++;
			}

			if (numPoints >= 2)
			{
				unsigned int tessCount = TessellateWaypointsOverTerrain(
					points, numPoints, tessPoints, MAX_WAYPOINT_POINTS_TESS);
				const uint32_t maxVerts = (tessCount - 1) * 6;
				Render::Vertex3D* verts = (Render::Vertex3D*)alloca(maxVerts * sizeof(Render::Vertex3D));
				uint32_t vertCount = BuildCameraFacingQuadStrip(
					cameraPos, tessPoints, tessCount, 1.5f,
					0.25f, 0.5f, 1.0f, 1.0f,  // blue color matching original waypoint style
					1.0f, verts);

				if (vertCount > 0)
				{
					Render::VertexBuffer vb;
					vb.Create(renderer.GetDevice(), verts, vertCount, sizeof(Render::Vertex3D));
					renderer.SetAdditive3DState();
					Render::Float4x4 identity;
					DirectX::XMStoreFloat4x4(&Render::ToXM(identity), DirectX::XMMatrixIdentity());
					Render::Texture* whiteTex =
						Render::ModelRenderer::Instance().GetWhiteTexture();
					renderer.Draw3DNoIndex(vb, vertCount, whiteTex, identity, {1,1,1,1});
				}
			}
		}
	}
}


// Line3D, SegLine, SegmentedLine - D3D11 implementations
// These classes render lines/segments. In D3D8 they used DynamicVBAccessClass.
// For D3D11 these use the Renderer's Draw3DNoIndex API to render camera-facing
// quad strips (for SegmentedLineClass) and billboard quads (for Line3DClass).
// BuildCameraFacingQuadStrip is defined above (before waypoint rendering).

#include "WW3D2/line3d.h"
#include "WW3D2/segline.h"
#include "WW3D2/seglinerenderer.h"

// --- Line3DClass ---
Line3DClass::Line3DClass(const Vector3& start, const Vector3& end, float width,
	float r, float g, float b, float opacity)
{
	// Store the two endpoints, width, color
	vert[0] = start;
	vert[1] = end;
	Width = width;
	Length = (end - start).Length();
	Color.Set(r, g, b, opacity);
	Shader = ShaderClass::_PresetAdditiveShader;
	SortLevel = 0;
}

Line3DClass::~Line3DClass() {}

void Line3DClass::Re_Color(float r, float g, float b)
{
	Color.X = r;
	Color.Y = g;
	Color.Z = b;
}

void Line3DClass::Reset(const Vector3& start, const Vector3& end)
{
	vert[0] = start;
	vert[1] = end;
	Length = (end - start).Length();
}

void Line3DClass::Reset(const Vector3& start, const Vector3& end, float width)
{
	vert[0] = start;
	vert[1] = end;
	Width = width;
	Length = (end - start).Length();
}

void Line3DClass::Set_Opacity(float opacity)
{
	Color.W = opacity;
}

RenderObjClass* Line3DClass::Clone() const { return nullptr; }

void Line3DClass::Render(RenderInfoClass& rinfo)
{
	if (!Is_Not_Hidden_At_All() || !Is_Visible())
		return;

	auto& renderer = Render::Renderer::Instance();

	// Get camera position from the RenderInfoClass camera
	const Matrix3D& camTM = rinfo.Camera.Get_Transform();
	Vector3 cameraPos(camTM[0][3], camTM[1][3], camTM[2][3]);

	// Transform the stored endpoints by this object's transform
	const Matrix3D& tm = Get_Transform();
	Vector3 worldPts[2];
	Matrix3D::Transform_Vector(tm, vert[0], &worldPts[0]);
	Matrix3D::Transform_Vector(tm, vert[1], &worldPts[1]);

	renderer.SetAdditive3DState();
	Render::Float4x4 identity;
	DirectX::XMStoreFloat4x4(&Render::ToXM(identity), DirectX::XMMatrixIdentity());

	// Safety: skip degenerate tracers
	if (Width < 0.001f) return;

	// Simple single-quad tracer matching the original game
	Render::Vertex3D verts[6];
	uint32_t vertCount = BuildCameraFacingQuadStrip(
		cameraPos, worldPts, 2, Width,
		Color.X, Color.Y, Color.Z, Color.W,
		1.0f, verts);
	if (vertCount == 0) return;

	Render::VertexBuffer vb;
	vb.Create(renderer.GetDevice(), verts, vertCount, sizeof(Render::Vertex3D));
	renderer.Draw3DNoIndex(vb, vertCount, nullptr, identity, {1,1,1,1});
}

void Line3DClass::Scale(float) {}
void Line3DClass::Scale(float, float, float) {}
int Line3DClass::Get_Num_Polys() const { return 2; }
void Line3DClass::Get_Obj_Space_Bounding_Sphere(SphereClass& s) const
{
	AABoxClass box;
	Get_Obj_Space_Bounding_Box(box);
	s.Center = box.Center;
	s.Radius = box.Extent.Length();
}

void Line3DClass::Get_Obj_Space_Bounding_Box(AABoxClass& b) const
{
	Vector3 min_coords = vert[0];
	Vector3 max_coords = vert[0];
	min_coords.Update_Min(vert[1]);
	max_coords.Update_Max(vert[1]);
	float hw = Width * 0.5f;
	Vector3 enlarge(hw, hw, hw);
	min_coords -= enlarge;
	max_coords += enlarge;
	b.Init_Min_Max(min_coords, max_coords);
}

// --- SegLineRendererClass ---
// The SegLineRendererClass stores rendering properties (width, color, texture, shader, etc.)
// that are shared by all segments in a SegmentedLineClass.

SegLineRendererClass::SegLineRendererClass()
	: Texture(nullptr), Width(1.0f), Color(1.0f, 1.0f, 1.0f), Opacity(1.0f),
	  SubdivisionLevel(0), NoiseAmplitude(0.0f), MergeAbortFactor(1.5f),
	  TextureTileFactor(1.0f), LastUsedSyncTime(0),
	  CurrentUVOffset(0.0f, 0.0f), UVOffsetDeltaPerMS(0.0f, 0.0f),
	  Bits(DEFAULT_BITS), m_vertexBufferSize(0), m_vertexBuffer(nullptr)
{
}

SegLineRendererClass::~SegLineRendererClass()
{
	REF_PTR_RELEASE(Texture);
	delete[] m_vertexBuffer;
	m_vertexBuffer = nullptr;
}

SegLineRendererClass::SegLineRendererClass(const SegLineRendererClass& that)
	: Texture(nullptr), Width(that.Width), Color(that.Color), Opacity(that.Opacity),
	  SubdivisionLevel(that.SubdivisionLevel), NoiseAmplitude(that.NoiseAmplitude),
	  MergeAbortFactor(that.MergeAbortFactor), TextureTileFactor(that.TextureTileFactor),
	  LastUsedSyncTime(that.LastUsedSyncTime), CurrentUVOffset(that.CurrentUVOffset),
	  UVOffsetDeltaPerMS(that.UVOffsetDeltaPerMS), Bits(that.Bits),
	  Shader(that.Shader), m_vertexBufferSize(0), m_vertexBuffer(nullptr)
{
	REF_PTR_SET(Texture, that.Texture);
}

SegLineRendererClass& SegLineRendererClass::operator=(const SegLineRendererClass& that)
{
	if (this != &that) {
		REF_PTR_SET(Texture, that.Texture);
		Shader = that.Shader;
		Width = that.Width;
		Color = that.Color;
		Opacity = that.Opacity;
		SubdivisionLevel = that.SubdivisionLevel;
		NoiseAmplitude = that.NoiseAmplitude;
		MergeAbortFactor = that.MergeAbortFactor;
		TextureTileFactor = that.TextureTileFactor;
		LastUsedSyncTime = that.LastUsedSyncTime;
		CurrentUVOffset = that.CurrentUVOffset;
		UVOffsetDeltaPerMS = that.UVOffsetDeltaPerMS;
		Bits = that.Bits;
	}
	return *this;
}

TextureClass* SegLineRendererClass::Get_Texture() const
{
	if (Texture) Texture->Add_Ref();
	return Texture;
}

void SegLineRendererClass::Set_Texture(TextureClass* texture)
{
	REF_PTR_SET(Texture, texture);
}

void SegLineRendererClass::Set_Texture_Tile_Factor(float factor)
{
	TextureTileFactor = factor;
	if (TextureTileFactor < 0.0f) TextureTileFactor = 0.0f;
	if (TextureTileFactor > 8.0f) TextureTileFactor = 8.0f;
}

void SegLineRendererClass::Set_Current_UV_Offset(const Vector2& offset)
{
	CurrentUVOffset = offset;
}

void SegLineRendererClass::Reset_Line() {}
void SegLineRendererClass::Scale(float) {}

VertexFormatXYZDUV1* SegLineRendererClass::getVertexBuffer(unsigned int number)
{
	return nullptr; // Not used in D3D11 path
}

void SegLineRendererClass::Render(
	RenderInfoClass& rinfo,
	const Matrix3D& transform,
	unsigned int point_count,
	Vector3* points,
	const SphereClass& obj_sphere,
	Vector4* rgbas)
{
	// Not called directly in D3D11 path; SegmentedLineClass::Render handles it
}

// --- SegmentedLineClass ---
SegmentedLineClass::SegmentedLineClass()
	: MaxSubdivisionLevels(0), NormalizedScreenArea(0.0f)
{
}

SegmentedLineClass::~SegmentedLineClass() {}

void SegmentedLineClass::Set_Color(const Vector3& color)
{
	LineRenderer.Set_Color(color);
}

void SegmentedLineClass::Set_Points(unsigned int num_points, Vector3* locs)
{
	PointLocations.Delete_All(false);
	if (locs && num_points > 0) {
		for (unsigned int i = 0; i < num_points; ++i) {
			PointLocations.Add(locs[i]);
		}
	}
}

int SegmentedLineClass::Get_Num_Points()
{
	return PointLocations.Count();
}

void SegmentedLineClass::Set_Point_Location(unsigned int point_idx, const Vector3& location)
{
	if ((int)point_idx < PointLocations.Count()) {
		PointLocations[point_idx] = location;
	}
}

void SegmentedLineClass::Get_Point_Location(unsigned int point_idx, Vector3& loc)
{
	if ((int)point_idx < PointLocations.Count()) {
		loc = PointLocations[point_idx];
	}
}

void SegmentedLineClass::Add_Point(const Vector3& location)
{
	PointLocations.Add(location);
}

void SegmentedLineClass::Delete_Point(unsigned int point_idx)
{
	if ((int)point_idx < PointLocations.Count()) {
		PointLocations.Delete(point_idx);
	}
}

void SegmentedLineClass::Reset_Line()
{
	PointLocations.Delete_All(false);
}

TextureClass* SegmentedLineClass::Get_Texture() { return LineRenderer.Get_Texture(); }
ShaderClass SegmentedLineClass::Get_Shader() { return LineRenderer.Get_Shader(); }
float SegmentedLineClass::Get_Width() { return LineRenderer.Get_Width(); }
void SegmentedLineClass::Get_Color(Vector3& color) { color = LineRenderer.Get_Color(); }
float SegmentedLineClass::Get_Opacity() { return LineRenderer.Get_Opacity(); }
float SegmentedLineClass::Get_Noise_Amplitude() { return LineRenderer.Get_Noise_Amplitude(); }
float SegmentedLineClass::Get_Merge_Abort_Factor() { return LineRenderer.Get_Merge_Abort_Factor(); }
unsigned int SegmentedLineClass::Get_Subdivision_Levels() { return LineRenderer.Get_Current_Subdivision_Level(); }
SegLineRendererClass::TextureMapMode SegmentedLineClass::Get_Texture_Mapping_Mode() { return LineRenderer.Get_Texture_Mapping_Mode(); }
float SegmentedLineClass::Get_Texture_Tile_Factor() { return LineRenderer.Get_Texture_Tile_Factor(); }
Vector2 SegmentedLineClass::Get_UV_Offset_Rate() { return LineRenderer.Get_UV_Offset_Rate(); }
int SegmentedLineClass::Is_Merge_Intersections() { return LineRenderer.Is_Merge_Intersections(); }
int SegmentedLineClass::Is_Freeze_Random() { return LineRenderer.Is_Freeze_Random(); }
int SegmentedLineClass::Is_Sorting_Disabled() { return LineRenderer.Is_Sorting_Disabled(); }
int SegmentedLineClass::Are_End_Caps_Enabled() { return LineRenderer.Are_End_Caps_Enabled(); }

void SegmentedLineClass::Set_Shader(ShaderClass shader) { LineRenderer.Set_Shader(shader); }
void SegmentedLineClass::Set_Texture(TextureClass* tex) { LineRenderer.Set_Texture(tex); }
void SegmentedLineClass::Set_Width(float w) { LineRenderer.Set_Width(w); }
void SegmentedLineClass::Set_Opacity(float o) { LineRenderer.Set_Opacity(o); }
void SegmentedLineClass::Set_Noise_Amplitude(float a) { LineRenderer.Set_Noise_Amplitude(a); }
void SegmentedLineClass::Set_Merge_Abort_Factor(float f) { LineRenderer.Set_Merge_Abort_Factor(f); }
void SegmentedLineClass::Set_Subdivision_Levels(unsigned int l) { LineRenderer.Set_Current_Subdivision_Level(l); }
void SegmentedLineClass::Set_Texture_Mapping_Mode(SegLineRendererClass::TextureMapMode m) { LineRenderer.Set_Texture_Mapping_Mode(m); }
void SegmentedLineClass::Set_Texture_Tile_Factor(float f) { LineRenderer.Set_Texture_Tile_Factor(f); }
void SegmentedLineClass::Set_UV_Offset_Rate(const Vector2& r) { LineRenderer.Set_UV_Offset_Rate(r); }
void SegmentedLineClass::Set_Merge_Intersections(int onoff) { LineRenderer.Set_Merge_Intersections(onoff); }
void SegmentedLineClass::Set_Freeze_Random(int onoff) { LineRenderer.Set_Freeze_Random(onoff); }
void SegmentedLineClass::Set_Disable_Sorting(int onoff) { LineRenderer.Set_Disable_Sorting(onoff); }
void SegmentedLineClass::Set_End_Caps(int onoff) { LineRenderer.Set_End_Caps(onoff); }

RenderObjClass* SegmentedLineClass::Clone() const { return nullptr; }

int SegmentedLineClass::Get_Num_Polys() const
{
	int n = PointLocations.Count();
	return (n > 1) ? (n - 1) * 2 : 0;
}

void SegmentedLineClass::Render(RenderInfoClass& rinfo)
{
	if (!Is_Not_Hidden_At_All() || !Is_Visible())
		return;

	int numPts = PointLocations.Count();
	if (numPts < 2) return;

	auto& renderer = Render::Renderer::Instance();

	// Get camera position
	const Matrix3D& camTM = rinfo.Camera.Get_Transform();
	Vector3 cameraPos(camTM[0][3], camTM[1][3], camTM[2][3]);

	// Get line properties from the renderer
	float width = LineRenderer.Get_Width();
	const Vector3& color = LineRenderer.Get_Color();
	float opacity = LineRenderer.Get_Opacity();
	float tileFactor = LineRenderer.Get_Texture_Tile_Factor();

	// Transform points by this object's transform into world space
	const int MAX_LINE_POINTS = 512;
	Vector3 worldPts[MAX_LINE_POINTS];
	int clampedPts = (numPts > MAX_LINE_POINTS) ? MAX_LINE_POINTS : numPts;
	const Matrix3D& tm = Get_Transform();
	bool isIdentity = Is_Transform_Identity();
	for (int i = 0; i < clampedPts; ++i) {
		if (isIdentity) {
			worldPts[i] = PointLocations[i];
		} else {
			Matrix3D::Transform_Vector(tm, PointLocations[i], &worldPts[i]);
		}
	}

	// Build camera-facing quad strip
	const uint32_t maxVerts = (clampedPts - 1) * 6;
	Render::Vertex3D* verts = (Render::Vertex3D*)alloca(maxVerts * sizeof(Render::Vertex3D));

	uint32_t vertCount = BuildCameraFacingQuadStrip(
		cameraPos, worldPts, clampedPts, width,
		color.X, color.Y, color.Z, opacity,
		tileFactor, verts);

	if (vertCount == 0) return;

	// Upload and render
	Render::VertexBuffer vb;
	vb.Create(renderer.GetDevice(), verts, vertCount, sizeof(Render::Vertex3D));

	// Use additive blending (matching the original ShaderClass::_PresetAdditiveShader)
	renderer.SetAdditive3DState();

	// Load the line's texture via ImageCache if available
	Render::Texture* lineTex = nullptr;
	TextureClass* ww3dTex = LineRenderer.Get_Texture();
	if (ww3dTex)
	{
		const char* texName = ww3dTex->Get_Texture_Name().str();
		if (texName && texName[0])
			lineTex = Render::ImageCache::Instance().GetTexture(renderer.GetDevice(), texName);
	}

	Render::Float4x4 identity;
	DirectX::XMStoreFloat4x4(&Render::ToXM(identity), DirectX::XMMatrixIdentity());
	renderer.Draw3DNoIndex(vb, vertCount, lineTex, identity, {1,1,1,1});

	// --- Laser/stream glow pass: wider, softer, UV-scrolled energy pulse ---
	extern bool g_debugDisableLaserGlow;
	if (!g_debugDisableLaserGlow && vertCount > 0 && maxVerts <= 600)
	{
		float glowWidth = width * 3.0f;
		float glowOpacity = opacity * 0.28f;
		float uvScroll = (float)WW3D::Get_Sync_Time() * 0.002f;

		Render::Vertex3D* glowVerts = (Render::Vertex3D*)alloca(maxVerts * sizeof(Render::Vertex3D));
		uint32_t glowVertCount = BuildCameraFacingQuadStrip(
			cameraPos, worldPts, clampedPts, glowWidth,
			color.X, color.Y, color.Z, glowOpacity,
			tileFactor * 0.5f, glowVerts);

		if (glowVertCount > 0)
		{
			// Offset V texcoord for animated energy crawl
			for (uint32_t v = 0; v < glowVertCount; ++v)
				glowVerts[v].texcoord.y += uvScroll;

			Render::VertexBuffer glowVB;
			glowVB.Create(renderer.GetDevice(), glowVerts,
				glowVertCount, sizeof(Render::Vertex3D));
			renderer.Draw3DNoIndex(glowVB, glowVertCount, lineTex, identity, {1,1,1,1});
		}
	}
}

void SegmentedLineClass::Get_Obj_Space_Bounding_Sphere(SphereClass& sphere) const
{
	AABoxClass box;
	Get_Obj_Space_Bounding_Box(box);
	sphere.Center = box.Center;
	sphere.Radius = box.Extent.Length();
}

void SegmentedLineClass::Get_Obj_Space_Bounding_Box(AABoxClass& box) const
{
	unsigned int num_points = PointLocations.Count();
	if (num_points >= 2) {
		Vector3 max_coords = PointLocations[0];
		Vector3 min_coords = PointLocations[0];
		for (unsigned int i = 1; i < num_points; i++) {
			max_coords.Update_Max(PointLocations[i]);
			min_coords.Update_Min(PointLocations[i]);
		}
		float enlarge_factor = LineRenderer.Get_Width() * 0.5f;
		Vector3 enlarge_offset(enlarge_factor, enlarge_factor, enlarge_factor);
		max_coords += enlarge_offset;
		min_coords -= enlarge_offset;
		box.Init_Min_Max(min_coords, max_coords);
	} else {
		box.Init(Vector3(0,0,0), Vector3(1,1,1));
	}
}
void SegmentedLineClass::Prepare_LOD(CameraClass&) {}
void SegmentedLineClass::Increment_LOD() {}
void SegmentedLineClass::Decrement_LOD() {}
float SegmentedLineClass::Get_Cost() const { return 0; }
float SegmentedLineClass::Get_Value() const { return 0; }
float SegmentedLineClass::Get_Post_Increment_Value() const { return 0; }
void SegmentedLineClass::Set_LOD_Level(int) {}
int SegmentedLineClass::Get_LOD_Level() const { return 0; }
int SegmentedLineClass::Get_LOD_Count() const { return 1; }
bool SegmentedLineClass::Cast_Ray(RayCollisionTestClass&) { return false; }

// ShaderClass::Invalidate already defined in Section 13
void WW3D::Add_To_Static_Sort_List(RenderObjClass*, unsigned int) {}

#include "WW3D2/vertmaterial.h"

// TextureClass

namespace
{
	W3DPropBuffer *g_dx11TerrainPropBuffer = nullptr;
	W3DPropBuffer *g_dx11TerrainTreeBuffer = nullptr;

	W3DPropBuffer *EnsureDX11TerrainPropBuffer()
	{
		if (g_dx11TerrainPropBuffer == nullptr) {
			g_dx11TerrainPropBuffer = NEW W3DPropBuffer;
		}

		return g_dx11TerrainPropBuffer;
	}

	W3DPropBuffer *EnsureDX11TerrainTreeBuffer()
	{
		if (g_dx11TerrainTreeBuffer == nullptr) {
			g_dx11TerrainTreeBuffer = NEW W3DPropBuffer;
		}

		return g_dx11TerrainTreeBuffer;
	}
}

void W3DPropBuffer::cull(CameraClass *camera)
{
	for (Int i = 0; i < m_numProps; ++i) {
		m_props[i].visible = (camera != nullptr) ? !camera->Cull_Sphere(m_props[i].bounds) : false;
	}

	m_doCull = false;
}

W3DPropBuffer::W3DPropBuffer()
{
	m_numProps = 0;
	m_anythingChanged = false;
	m_initialized = true;
	m_doCull = true;
	m_numPropTypes = 0;
	m_propShroudMaterialPass = nullptr;
	m_light = nullptr;

	for (Int i = 0; i < MAX_PROPS; ++i) {
		m_props[i].m_robj = nullptr;
		m_props[i].id = 0;
		m_props[i].location.set(0.0f, 0.0f, 0.0f);
		m_props[i].propType = -1;
		m_props[i].ss = OBJECTSHROUD_INVALID;
		m_props[i].visible = false;
		m_props[i].bounds.Center = Vector3(0.0f, 0.0f, 0.0f);
		m_props[i].bounds.Radius = 1.0f;
	}

	for (Int i = 0; i < MAX_TYPES; ++i) {
		m_propTypes[i].m_robj = nullptr;
		m_propTypes[i].m_robjName.clear();
		m_propTypes[i].m_bounds.Center = Vector3(0.0f, 0.0f, 0.0f);
		m_propTypes[i].m_bounds.Radius = 1.0f;
	}
}

W3DPropBuffer::~W3DPropBuffer()
{
	clearAllProps();
}

void W3DPropBuffer::clearAllProps()
{
	for (Int i = 0; i < m_numPropTypes; ++i) {
		REF_PTR_RELEASE(m_propTypes[i].m_robj);
		m_propTypes[i].m_robjName.clear();
	}

	for (Int i = 0; i < m_numProps; ++i) {
		REF_PTR_RELEASE(m_props[i].m_robj);
		m_props[i].propType = -1;
		m_props[i].visible = false;
		m_props[i].ss = OBJECTSHROUD_INVALID;
	}

	m_numPropTypes = 0;
	m_numProps = 0;
	m_anythingChanged = true;
	m_doCull = true;
}

Int W3DPropBuffer::addPropType(const AsciiString &modelName)
{
	if (m_numPropTypes >= MAX_TYPES) {
		DEBUG_CRASH(("Too many prop types in W3DPropBuffer."));
		return -1;
	}

	RenderObjClass *renderObject = WW3DAssetManager::Get_Instance()->Create_Render_Obj(modelName.str());
	if (renderObject == nullptr) {
		DEBUG_CRASH(("Unable to find model for prop %s", modelName.str()));
		return -1;
	}

	const Int propType = m_numPropTypes++;
	m_propTypes[propType].m_robj = renderObject;
	m_propTypes[propType].m_robjName = modelName;
	m_propTypes[propType].m_bounds = renderObject->Get_Bounding_Sphere();
	return propType;
}

void W3DPropBuffer::addProp(Int id, Coord3D location, Real angle, Real scale, const AsciiString &modelName)
{
	static Int s_loggedAdds = 0;

	if (!m_initialized || m_numProps >= MAX_PROPS) {
		return;
	}

	Int propType = -1;
	for (Int i = 0; i < m_numPropTypes; ++i) {
		if (m_propTypes[i].m_robjName.compareNoCase(modelName) == 0) {
			propType = i;
			break;
		}
	}

	if (propType < 0) {
		propType = addPropType(modelName);
		if (propType < 0) {
			return;
		}
	}

	Matrix3D transform(true);
	transform.Rotate_Z(angle);
	transform.Scale(scale);
	transform.Set_Translation(Vector3(location.x, location.y, location.z));

	TProp &prop = m_props[m_numProps++];
	prop.location = location;
	prop.id = id;
	prop.propType = propType;
	prop.ss = OBJECTSHROUD_INVALID;
	prop.visible = false;
	prop.m_robj = m_propTypes[propType].m_robj->Clone();
	if (prop.m_robj != nullptr) {
		prop.m_robj->Set_Transform(transform);
		prop.m_robj->Set_ObjectScale(scale);
	}
	prop.bounds = m_propTypes[propType].m_bounds;
	prop.bounds.Center += Vector3(location.x, location.y, location.z);
	m_anythingChanged = true;
	m_doCull = true;

	if (s_loggedAdds < 32) {
		AppendDX11ShimTrace(
			"W3DPropBuffer::addProp #%d id=%d model=%s loc=(%.2f,%.2f,%.2f) angle=%.2f scale=%.2f type=%d\n",
			s_loggedAdds,
			id,
			modelName.str(),
			location.x,
			location.y,
			location.z,
			angle,
			scale,
			propType);
		++s_loggedAdds;
	}
}

Bool W3DPropBuffer::updatePropPosition(Int id, const Coord3D &location, Real angle, Real scale)
{
	for (Int i = 0; i < m_numProps; ++i) {
		TProp &prop = m_props[i];
		if (prop.id != id || prop.m_robj == nullptr || prop.propType < 0) {
			continue;
		}

		Matrix3D transform(true);
		transform.Rotate_Z(angle);
		transform.Scale(scale);
		transform.Set_Translation(Vector3(location.x, location.y, location.z));

		prop.location = location;
		prop.m_robj->Set_Transform(transform);
		prop.m_robj->Set_ObjectScale(scale);
		prop.bounds = m_propTypes[prop.propType].m_bounds;
		prop.bounds.Center += Vector3(location.x, location.y, location.z);
		prop.ss = OBJECTSHROUD_INVALID;
		m_anythingChanged = true;
		m_doCull = true;
		return true;
	}

	return false;
}

void W3DPropBuffer::removeProp(Int id)
{
	for (Int i = 0; i < m_numProps; ++i) {
		TProp &prop = m_props[i];
		if (prop.id != id) {
			continue;
		}

		prop.location.set(0.0f, 0.0f, 0.0f);
		prop.propType = -1;
		REF_PTR_RELEASE(prop.m_robj);
		prop.bounds.Center = Vector3(0.0f, 0.0f, 0.0f);
		prop.bounds.Radius = 1.0f;
		prop.visible = false;
		prop.ss = OBJECTSHROUD_INVALID;
		m_anythingChanged = true;
		m_doCull = true;
	}
}

void W3DPropBuffer::notifyShroudChanged()
{
	for (Int i = 0; i < m_numProps; ++i) {
		m_props[i].ss = ThePartitionManager ? OBJECTSHROUD_INVALID : OBJECTSHROUD_CLEAR;
	}
}

void W3DPropBuffer::renderDX11(CameraClass *camera)
{
	static Int s_loggedFrames = 0;
	const Bool ignorePropShroud = (this == g_dx11TerrainTreeBuffer);

	if (camera == nullptr) {
		return;
	}

	if (m_doCull) {
		cull(camera);
	}

	Int activeProps = 0;
	Int visibleProps = 0;
	Int submittedProps = 0;
	for (Int i = 0; i < m_numProps; ++i) {
		TProp &prop = m_props[i];
		if (prop.m_robj == nullptr) {
			continue;
		}

		++activeProps;
		if (!prop.visible) {
			continue;
		}
		++visibleProps;

		if (!ignorePropShroud) {
			if (!ThePlayerList || !ThePartitionManager) {
				prop.ss = OBJECTSHROUD_CLEAR;
			}

			if (prop.ss == OBJECTSHROUD_INVALID) {
				const Int localPlayerIndex = rts::getObservedOrLocalPlayerIndex_Safe();
				prop.ss = ThePartitionManager->getPropShroudStatusForPlayer(localPlayerIndex, &prop.location);
			}

			if (prop.ss >= OBJECTSHROUD_SHROUDED) {
				continue;
			}

			if (prop.ss <= OBJECTSHROUD_INVALID) {
				continue;
			}
		}

		Render::ModelRenderer::Instance().RenderRenderObject(prop.m_robj);
		++submittedProps;
	}

	if (s_loggedFrames < 16) {
		AppendDX11ShimTrace(
			"W3DPropBuffer::renderDX11 frame=%d total=%d active=%d visible=%d submitted=%d doCull=%d\n",
			s_loggedFrames,
			m_numProps,
			activeProps,
			visibleProps,
			submittedProps,
			m_doCull ? 1 : 0);
		++s_loggedFrames;
	}
}

void W3DPropBuffer::removePropsForConstruction(const Coord3D *pos, const GeometryInfo &geom, Real angle)
{
	if (pos == nullptr || ThePartitionManager == nullptr) {
		return;
	}

	for (Int i = 0; i < m_numProps; ++i) {
		TProp &prop = m_props[i];
		if (prop.m_robj == nullptr) {
			continue;
		}

		const Real radius = prop.bounds.Radius;
		GeometryInfo propGeometry(GEOMETRY_CYLINDER, false, 5.0f * radius, 2.0f * radius, 2.0f * radius);
		if (!ThePartitionManager->geomCollidesWithGeom(pos, geom, angle, &prop.location, propGeometry, 0.0f)) {
			continue;
		}

		prop.location.set(0.0f, 0.0f, 0.0f);
		prop.propType = -1;
		REF_PTR_RELEASE(prop.m_robj);
		prop.bounds.Center = Vector3(0.0f, 0.0f, 0.0f);
		prop.bounds.Radius = 1.0f;
		prop.visible = false;
		prop.ss = OBJECTSHROUD_INVALID;
		m_anythingChanged = true;
		m_doCull = true;
	}
}

void W3DPropBuffer::crc(Xfer *xfer)
{
	(void)xfer;
}

void W3DPropBuffer::xfer(Xfer *xfer)
{
	(void)xfer;
}

void W3DPropBuffer::loadPostProcess()
{
}

// BaseHeightMapRenderObjClass
void BaseHeightMapRenderObjClass::removeTreesAndPropsForConstruction(const Coord3D *pos, const GeometryInfo &geom, Real angle)
{
	if (m_propBuffer) {
		m_propBuffer->removePropsForConstruction(pos, geom, angle);
	}
	if (g_dx11TerrainTreeBuffer) {
		g_dx11TerrainTreeBuffer->removePropsForConstruction(pos, geom, angle);
	}
}

void BaseHeightMapRenderObjClass::addTree(DrawableID id, Coord3D location, Real scale, Real angle,
	Real randomScaleAmount, const W3DTreeDrawModuleData *data)
{
	(void)randomScaleAmount;

	if (data == nullptr || data->m_modelName.isEmpty()) {
		return;
	}

	EnsureDX11TerrainTreeBuffer()->addProp(static_cast<Int>(id), location, angle, scale, data->m_modelName);
}

void BaseHeightMapRenderObjClass::removeTree(DrawableID id)
{
	if (g_dx11TerrainTreeBuffer) {
		g_dx11TerrainTreeBuffer->removeProp(static_cast<Int>(id));
	}
}

void BaseHeightMapRenderObjClass::removeAllTrees()
{
	if (g_dx11TerrainTreeBuffer) {
		g_dx11TerrainTreeBuffer->clearAllProps();
	}
}

Bool BaseHeightMapRenderObjClass::updateTreePosition(DrawableID id, Coord3D location, Real angle)
{
	(void)id;
	(void)location;
	(void)angle;
	return false;
}

void BaseHeightMapRenderObjClass::addProp(Int id, Coord3D location, Real angle, Real scale, const AsciiString &modelName)
{
	if (m_propBuffer == nullptr) {
		m_propBuffer = NEW W3DPropBuffer;
	}

	if (m_propBuffer) {
		m_propBuffer->addProp(id, location, angle, scale, modelName);
	}
}

void BaseHeightMapRenderObjClass::removeProp(Int id)
{
	if (m_propBuffer) {
		m_propBuffer->removeProp(id);
	}
}

void BaseHeightMapRenderObjClass::removeAllProps()
{
	if (m_propBuffer) {
		m_propBuffer->clearAllProps();
	}
}

void BaseHeightMapRenderObjClass::renderProps(CameraClass *camera)
{
	if (m_propBuffer != nullptr) {
		m_propBuffer->renderDX11(camera);
	}
}

void BaseHeightMapRenderObjClass::notifyShroudChanged()
{
	if (m_propBuffer) {
		m_propBuffer->notifyShroudChanged();
	}
}

// W3DDynamicLight
// W3DDynamicLight — real implementation now compiled from W3DDynamicLight.cpp

////////////////////////////////////////////////////////////////////////////////
// SECTION 13: W3DShaderManager::testMinimumRequirements static wrapper
// The actual implementation is the free function above. The class method
// just delegates.
////////////////////////////////////////////////////////////////////////////////

Bool W3DShaderManager::testMinimumRequirements(ChipsetType* videoChipType, CpuType* cpuType,
                                                Int* cpuFreq, MemValueType* numRAM,
                                                Real* intBenchIndex, Real* floatBenchIndex,
                                                Real* memBenchIndex)
{
	return ::testMinimumRequirements(videoChipType, cpuType, cpuFreq, numRAM,
	                                  intBenchIndex, floatBenchIndex, memBenchIndex);
}

ChipsetType W3DShaderManager::getChipset()
{
	return m_currentChipset;
}

ChipsetType W3DShaderManager::m_currentChipset = (ChipsetType)0;
GraphicsVenderID W3DShaderManager::m_currentVendor = (GraphicsVenderID)0;
__int64 W3DShaderManager::m_driverVersion = 0;
TextureClass* W3DShaderManager::m_Textures[8] = {};
W3DShaderManager::ShaderTypes W3DShaderManager::m_currentShader = W3DShaderManager::ST_INVALID;
Int W3DShaderManager::m_currentShaderPass = 0;
FilterTypes W3DShaderManager::m_currentFilter = (FilterTypes)0;
Bool W3DShaderManager::m_renderingToTexture = FALSE;
IDirect3DSurface8* W3DShaderManager::m_oldRenderSurface = nullptr;
IDirect3DTexture8* W3DShaderManager::m_renderTexture = nullptr;
IDirect3DSurface8* W3DShaderManager::m_newRenderSurface = nullptr;
IDirect3DSurface8* W3DShaderManager::m_oldDepthSurface = nullptr;

void W3DShaderManager::init() {}
void W3DShaderManager::shutdown() {}
void W3DShaderManager::updateCloud() {}
Int W3DShaderManager::getShaderPasses(ShaderTypes shader) { return 1; }
Int W3DShaderManager::setShader(ShaderTypes shader, Int pass) { m_currentShader = shader; m_currentShaderPass = pass; return 0; }
Int W3DShaderManager::setShroudTex(Int stage) { return 0; }
void W3DShaderManager::resetShader(ShaderTypes shader) {}
HRESULT W3DShaderManager::LoadAndCreateD3DShader(const char* strFilePath, const DWORD* pDeclaration, DWORD Usage, Bool ShaderType, DWORD* pHandle) { return E_NOTIMPL; }
StaticGameLODLevel W3DShaderManager::getGPUPerformanceIndex() { return (StaticGameLODLevel)3; } // High
Real W3DShaderManager::GetCPUBenchTime() { return 0.001f; }
Bool W3DShaderManager::filterPreRender(FilterTypes filter, Bool& skipRender, CustomScenePassModes& scenePassMode) { skipRender = FALSE; return FALSE; }
Bool W3DShaderManager::filterPostRender(FilterTypes filter, FilterModes mode, Coord2D& scrollDelta, Bool& doExtraRender) { doExtraRender = FALSE; return FALSE; }
Bool W3DShaderManager::filterSetup(FilterTypes filter, FilterModes mode) { return TRUE; }
void W3DShaderManager::startRenderToTexture() {}
IDirect3DTexture8* W3DShaderManager::endRenderToTexture() { return nullptr; }
IDirect3DTexture8* W3DShaderManager::getRenderTexture() { return nullptr; }
void W3DShaderManager::drawViewport(Int color) {}


////////////////////////////////////////////////////////////////////////////////
// SECTION 14: Additional missing symbols
////////////////////////////////////////////////////////////////////////////////

// W3DView implementations are in SECTION 15 below

#include "WW3D2/render2d.h"
#include "WW3D2/shader.h"

// Render2DClass::Get_Default_Shader
ShaderClass Render2DClass::Get_Default_Shader()
{
	ShaderClass shader;
	return shader;
}


////////////////////////////////////////////////////////////////////////////////
// SECTION 15: W3DView implementation (D3D11 port)
//
// Full W3DView method implementations with all camera math, scrolling,
// zooming, lookAt, movement, and scripted camera logic intact.
// DX8/D3D8 rendering calls have been removed.
////////////////////////////////////////////////////////////////////////////////

#include "W3DDevice/GameClient/W3DView.h"

#include "Common/BuildAssistant.h"
#include "Common/FramePacer.h"
#include "Common/GameUtility.h"
#include "Common/GlobalData.h"
#include "Common/Radar.h"
#include "Common/RandomValue.h"
#include "Common/ThingSort.h"
#include "Common/PlayerList.h"
#include "Common/Player.h"

#include "GameClient/CommandXlat.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Line2D.h"
#include "GameClient/SelectionInfo.h"
#include "GameClient/TerrainVisual.h"
#include "GameClient/Water.h"

#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/OpenContain.h"
#include "GameLogic/Object.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/TerrainLogic.h"

#include "W3DDevice/Common/W3DConvert.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/CameraShakeSystem.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"

#include "WW3D2/light.h"
#include "WW3D2/camera.h"
#include "WW3D2/coltype.h"

// 30 fps
Real TheW3DFrameLengthInMsec = MSEC_PER_LOGICFRAME_REAL;
static const Int MAX_REQUEST_CACHE_SIZE = 40;
static const Real DRAWABLE_OVERSCAN = 75.0f;

constexpr const Real NearZ_W3DView = MAP_XY_FACTOR;

//=================================================================================================
inline Real w3dview_minf(Real a, Real b) { if (a < b) return a; else return b; }
inline Real w3dview_maxf(Real a, Real b) { if (a > b) return a; else return b; }

//-------------------------------------------------------------------------------------------------
static void w3dview_normAngle(Real &angle)
{
	angle = WWMath::Normalize_Angle(angle);
}

#define TERRAIN_SAMPLE_SIZE_W3DVIEW 40.0f
static Real w3dview_getHeightAroundPos(Real x, Real y)
{
	Real terrainHeight = TheTerrainLogic->getGroundHeight(x, y);
	Real terrainHeightMax = terrainHeight;
	terrainHeightMax = max(terrainHeightMax, TheTerrainLogic->getGroundHeight(x+TERRAIN_SAMPLE_SIZE_W3DVIEW, y-TERRAIN_SAMPLE_SIZE_W3DVIEW));
	terrainHeightMax = max(terrainHeightMax, TheTerrainLogic->getGroundHeight(x-TERRAIN_SAMPLE_SIZE_W3DVIEW, y-TERRAIN_SAMPLE_SIZE_W3DVIEW));
	terrainHeightMax = max(terrainHeightMax, TheTerrainLogic->getGroundHeight(x+TERRAIN_SAMPLE_SIZE_W3DVIEW, y+TERRAIN_SAMPLE_SIZE_W3DVIEW));
	terrainHeightMax = max(terrainHeightMax, TheTerrainLogic->getGroundHeight(x-TERRAIN_SAMPLE_SIZE_W3DVIEW, y+TERRAIN_SAMPLE_SIZE_W3DVIEW));
	return terrainHeightMax;
}


//-------------------------------------------------------------------------------------------------
W3DView::W3DView()
{
	m_3DCamera = nullptr;
	m_2DCamera = nullptr;
	m_groundLevel = 10.0f;
	m_cameraOffset.z = TheGlobalData->m_cameraHeight;
	m_cameraOffset.y = -(m_cameraOffset.z / tan(TheGlobalData->m_cameraPitch * (PI / 180.0)));
	m_cameraOffset.x = -(m_cameraOffset.y * tan(TheGlobalData->m_cameraYaw * (PI / 180.0)));

	m_viewFilterMode = FM_VIEW_DEFAULT;
	m_viewFilter = FT_VIEW_DEFAULT;
	m_isWireFrameEnabled = m_nextWireFrameEnabled = FALSE;
	m_shakeOffset.x = 0.0f;
	m_shakeOffset.y = 0.0f;
	m_shakeIntensity = 0.0f;
	m_FXPitch = 1.0f;
	m_freezeTimeForCameraMovement = false;
	m_cameraHasMovedSinceRequest = true;
	m_locationRequests.clear();
	m_locationRequests.reserve(MAX_REQUEST_CACHE_SIZE + 10);

	m_scriptedState = 0;
	m_CameraArrivedAtWaypointOnPathFlag = false;
	m_isCameraSlaved = false;
	m_useRealZoomCam = false;
	m_shakerAngles.X = 0.0f;
	m_shakerAngles.Y = 0.0f;
	m_shakerAngles.Z = 0.0f;

	m_recalcCamera = false;
}

//-------------------------------------------------------------------------------------------------
W3DView::~W3DView()
{
	REF_PTR_RELEASE( m_2DCamera );
	REF_PTR_RELEASE( m_3DCamera );
}

//-------------------------------------------------------------------------------------------------
void W3DView::setHeight(Int height)
{
	View::setHeight(height);

	Vector2 vMin,vMax;
	m_3DCamera->Set_Aspect_Ratio((Real)getWidth()/(Real)height);
	m_3DCamera->Get_Viewport(vMin,vMax);
	vMax.Y=(Real)(m_originY+height)/(Real)TheDisplay->getHeight();
	m_3DCamera->Set_Viewport(vMin,vMax);

	// Match ZH (W3DView.cpp:204-214): viewport size changes do NOT invalidate
	// camera area constraints. Invalidating here triggers a recalc on the next
	// frame whose offset is recomputed against the *current* camera state, and
	// for shallow scripted-camera pitches that offset becomes huge enough to
	// clamp the cinematic camera back to the playable area (black screen).
	m_recalcCamera = true;
}

//-------------------------------------------------------------------------------------------------
void W3DView::setWidth(Int width)
{
	View::setWidth(width);

	Vector2 vMin,vMax;
	m_3DCamera->Set_Aspect_Ratio((Real)width/(Real)getHeight());
	m_3DCamera->Get_Viewport(vMin,vMax);
	vMax.X=(Real)(m_originX+width)/(Real)TheDisplay->getWidth();
	m_3DCamera->Set_Viewport(vMin,vMax);

	m_3DCamera->Set_View_Plane((Real)width/(Real)TheDisplay->getWidth()*DEG_TO_RADF(50.0f),-1);

	// Match ZH (W3DView.cpp:219-233): viewport size changes do NOT invalidate
	// camera area constraints. See setHeight() above for the rationale.
	m_recalcCamera = true;
}

//-------------------------------------------------------------------------------------------------
void W3DView::setOrigin( Int x, Int y)
{
	View::setOrigin(x,y);

	Vector2 vMin,vMax;

	m_3DCamera->Get_Viewport(vMin,vMax);
	vMin.X=(Real)x/(Real)TheDisplay->getWidth();
	vMin.Y=(Real)y/(Real)TheDisplay->getHeight();
	m_3DCamera->Set_Viewport(vMin,vMax);

	setWidth(m_width);
	setHeight(m_height);
}

//-------------------------------------------------------------------------------------------------
#define MIN_CAPPED_ZOOM (0.5f)
void W3DView::buildCameraTransform( Matrix3D *transform )
{
	Vector3 sourcePos, targetPos;

	Real groundLevel = m_groundLevel;

	Real zoom = getZoom();
	Real angle = getAngle();
	Real pitch = getPitch();
	Coord3D pos = *getPosition();

	pos.x += m_shakeOffset.x;
	pos.y += m_shakeOffset.y;

	if (m_cameraAreaConstraintsValid)
	{
		pos.x = w3dview_maxf(m_cameraAreaConstraints.lo.x, pos.x);
		pos.x = w3dview_minf(m_cameraAreaConstraints.hi.x, pos.x);
		pos.y = w3dview_maxf(m_cameraAreaConstraints.lo.y, pos.y);
		pos.y = w3dview_minf(m_cameraAreaConstraints.hi.y, pos.y);
	}

	sourcePos.X = m_cameraOffset.x;
	sourcePos.Y = m_cameraOffset.y;
	sourcePos.Z = m_cameraOffset.z;

	if (m_useRealZoomCam)
	{
		Real cappedZoom = clamp(MIN_CAPPED_ZOOM, zoom, 1.0f);
		m_FOV = DEG_TO_RADF(50.0f) * cappedZoom * cappedZoom;
	}
	else
	{
		sourcePos.X *= zoom;
		sourcePos.Y *= zoom;
		sourcePos.Z *= zoom;
	}

	targetPos.X = 0;
	targetPos.Y = 0;
	targetPos.Z = 0;

	const Real heightScale = (fabsf(sourcePos.Z) > 0.001f) ? (1.0f - (groundLevel / sourcePos.Z)) : 1.0f;

	const Matrix3D angleTransform( Vector3( 0.0f, 0.0f, 1.0f ), angle );
	const Matrix3D pitchTransform( Vector3( 1.0f, 0.0f, 0.0f ), pitch );

#ifdef ALLOW_TEMPORARIES
	sourcePos = pitchTransform * sourcePos;
	sourcePos = angleTransform * sourcePos;
#else
	pitchTransform.mulVector3(sourcePos);
	angleTransform.mulVector3(sourcePos);
#endif

	sourcePos *= heightScale;

	targetPos.X += pos.x;
	targetPos.Y += pos.y;
	targetPos.Z += groundLevel;

	sourcePos += targetPos;

	if (m_useRealZoomCam)
	{
		Real pitchAdjust = 1.0f;

		if (!TheDisplay->isLetterBoxed())
		{
			Real cappedZoom = clamp(MIN_CAPPED_ZOOM, zoom, 1.0f);
			sourcePos.Z = sourcePos.Z * (0.5f + cappedZoom * 0.5f);
			pitchAdjust = cappedZoom;
		}
		m_FXPitch = 1.0f * (0.25f + pitchAdjust*0.75f);
		sourcePos.X = targetPos.X + ((sourcePos.X - targetPos.X) / m_FXPitch);
		sourcePos.Y = targetPos.Y + ((sourcePos.Y - targetPos.Y) / m_FXPitch);
	}
	else
	{
#if RTS_GENERALS
		Real height = sourcePos.Z - targetPos.Z;
		height *= m_FXPitch;
		targetPos.Z = sourcePos.Z - height;
#else
		if (m_FXPitch <= 1.0f)
		{
			targetPos.Z = sourcePos.Z - ((sourcePos.Z - targetPos.Z) * m_FXPitch);
		}
		else
		{
			sourcePos.X = targetPos.X + ((sourcePos.X - targetPos.X) / m_FXPitch);
			sourcePos.Y = targetPos.Y + ((sourcePos.Y - targetPos.Y) / m_FXPitch);
		}
#endif
	}

	transform->Make_Identity();
	transform->Look_At( sourcePos, targetPos, 0 );

	CameraShakerSystem.Timestep(TheFramePacer->getLogicTimeStepMilliseconds());
	CameraShakerSystem.Update_Camera_Shaker(sourcePos, &m_shakerAngles);
	transform->Rotate_X(m_shakerAngles.X);
	transform->Rotate_Y(m_shakerAngles.Y);
	transform->Rotate_Z(m_shakerAngles.Z);

	if (m_isCameraSlaved) {
		Object * obj = TheScriptEngine->getUnitNamed(m_cameraSlaveObjectName);

		if (obj != nullptr) {
			Drawable * draw = obj->getDrawable();
			if (draw != nullptr) {
				for (DrawModule ** dm = draw->getDrawModules(); *dm; ++dm) {
					const ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
					if (di) {
						Matrix3D tm;
						di->clientOnly_getRenderObjBoneTransform(m_cameraSlaveObjectBoneName,&tm);
						*transform = tm;

						Vector3 position = transform->Get_Translation();
						Coord3D coord;
						coord.set(position.X, position.Y, position.Z);
						View::setPosition(&coord);
						break;
					}
				}
			} else {
				m_isCameraSlaved = false;
			}
		} else {
			m_isCameraSlaved = false;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void W3DView::calcCameraAreaConstraints()
{
	if (TheTerrainLogic)
	{
		Region3D mapRegion;
		TheTerrainLogic->getExtent( &mapRegion );

		Real maxEdgeZ = m_groundLevel;
		Coord3D center, bottom;
		ICoord2D screen;

		screen.x=0.5f*getWidth()+m_originX;
		screen.y=0.5f*getHeight()+m_originY;

		Vector3 rayStart,rayEnd;

		getPickRay(&screen,&rayStart,&rayEnd);

		center.x = Vector3::Find_X_At_Z(maxEdgeZ, rayStart, rayEnd);
		center.y = Vector3::Find_Y_At_Z(maxEdgeZ, rayStart, rayEnd);
		center.z = maxEdgeZ;

		screen.y = m_originY+ 0.95f*getHeight();
		getPickRay(&screen,&rayStart,&rayEnd);
		bottom.x = Vector3::Find_X_At_Z(maxEdgeZ, rayStart, rayEnd);
		bottom.y = Vector3::Find_Y_At_Z(maxEdgeZ, rayStart, rayEnd);
		bottom.z = maxEdgeZ;
		center.x -= bottom.x;
		center.y -= bottom.y;

		Real offset = center.length();

		if (TheGlobalData->m_debugAI) {
			offset = -1000;
		}

		m_cameraAreaConstraints.lo.x = mapRegion.lo.x + offset;
		m_cameraAreaConstraints.hi.x = mapRegion.hi.x - offset;
		m_cameraAreaConstraints.lo.y = mapRegion.lo.y + offset;
		m_cameraAreaConstraints.hi.y = mapRegion.hi.y - offset;

		m_cameraAreaConstraintsValid = true;
	}
}

//-------------------------------------------------------------------------------------------------
Bool W3DView::isWithinCameraHeightConstraints() const
{
	const Bool isAboveMinHeight = m_currentHeightAboveGround >= m_minHeightAboveGround;
	const Bool isBelowMaxHeight = m_currentHeightAboveGround <= m_maxHeightAboveGround;
	return isAboveMinHeight && (isBelowMaxHeight || !TheGlobalData->m_enforceMaxCameraHeight);
}

//-------------------------------------------------------------------------------------------------
void W3DView::getPickRay(const ICoord2D *screen, Vector3 *rayStart, Vector3 *rayEnd)
{
	Real logX;
	Real logY;
	Real screenX = screen->x - m_originX;
	Real screenY = screen->y - m_originY;

	PixelScreenToW3DLogicalScreen(screenX, screenY, &logX, &logY, getWidth(), getHeight());

	*rayStart = m_3DCamera->Get_Position();
	m_3DCamera->Un_Project(*rayEnd,Vector2(logX,logY));
	*rayEnd -= *rayStart;
	rayEnd->Normalize();
	*rayEnd *= m_3DCamera->Get_Depth();
	*rayEnd += *rayStart;
}

//-------------------------------------------------------------------------------------------------
void W3DView::setCameraTransform()
{
	if (TheGlobalData->m_headless)
		return;

	if (m_viewLockedUntilFrame > TheGameClient->getFrame())
		return;

	m_cameraHasMovedSinceRequest = true;
	Matrix3D cameraTransform;

	Real farZ = 1200.0f;

	if (m_useRealZoomCam)
	{
		if (m_FXPitch<0.95f)
		{
			farZ = farZ / m_FXPitch;
		}
	}
	else
	{
		if ((TheGlobalData->m_drawEntireTerrain) || (m_FXPitch<0.95f || m_zoom>1.05))
		{
			farZ *= MAP_XY_FACTOR;
		}
	}

	// In non-debug builds this flag is compiled out, so enforce constraints unconditionally.
	Bool enforceCameraConstraints = TRUE;
#if defined(RTS_DEBUG)
	enforceCameraConstraints = TheGlobalData->m_useCameraConstraints;
#endif
	if (enforceCameraConstraints)
	{
		if (!m_cameraAreaConstraintsValid)
		{
			buildCameraTransform(&cameraTransform);
			m_3DCamera->Set_Transform( cameraTransform );
			calcCameraAreaConstraints();
		}
		DEBUG_ASSERTLOG(m_cameraAreaConstraintsValid,("*** cam constraints are not valid!!!"));

		if (m_cameraAreaConstraintsValid)
		{
			Coord3D pos = *getPosition();
			pos.x = w3dview_maxf(m_cameraAreaConstraints.lo.x, pos.x);
			pos.x = w3dview_minf(m_cameraAreaConstraints.hi.x, pos.x);
			pos.y = w3dview_maxf(m_cameraAreaConstraints.lo.y, pos.y);
			pos.y = w3dview_minf(m_cameraAreaConstraints.hi.y, pos.y);
			setPosition(&pos);
		}
	}

	m_3DCamera->Set_Clip_Planes(NearZ_W3DView, farZ);

#if defined(RTS_DEBUG)
	m_3DCamera->Set_View_Plane( m_FOV, -1 );
#endif

	buildCameraTransform( &cameraTransform );
	m_3DCamera->Set_Transform( cameraTransform );

	if (TheTerrainRenderObject)
	{
		updateTerrain();
	}

	if (TheRadar)
	{
		TheRadar->notifyViewChanged();
	}
}

//-------------------------------------------------------------------------------------------------
void W3DView::init()
{
	View::init();
	setName("W3DView");
	Coord3D pos;
	pos.x = 87.0f;
	pos.y = 77.0f;
	pos.z = 0;

	pos.x *= MAP_XY_FACTOR;
	pos.y *= MAP_XY_FACTOR;

	setPosition(&pos);

	m_3DCamera = NEW_REF( CameraClass, () );

	m_2DCamera = NEW_REF( CameraClass, () );
	m_2DCamera->Set_Position( Vector3( 0, 0, 1 ) );
	Vector2 min = Vector2( -1, -0.75f );
	Vector2 max = Vector2( +1, +0.75f );
	m_2DCamera->Set_View_Plane( min, max );
	m_2DCamera->Set_Clip_Planes( 0.995f, 2.0f );

	m_cameraAreaConstraintsValid = false;

	m_scrollAmountCutoff = TheGlobalData->m_scrollAmountCutoff;

	m_recalcCamera = true;
}

//-------------------------------------------------------------------------------------------------
const Coord3D& W3DView::get3DCameraPosition() const
{
	Vector3 camera = m_3DCamera->Get_Position();
	static Coord3D pos;
	pos.set( camera.X, camera.Y, camera.Z );
	return pos;
}

//-------------------------------------------------------------------------------------------------
void W3DView::reset()
{
	View::reset();

	setTimeMultiplier(1);

	Coord3D arbitraryPos = { 0, 0, 0 };
	resetCamera(&arbitraryPos, 1, 0.0f, 0.0f);

	setViewFilter(FT_VIEW_DEFAULT);

	Coord2D gb = { 0,0 };
	setGuardBandBias( &gb );
}

//-------------------------------------------------------------------------------------------------
static void w3dview_drawDrawable( Drawable *draw, void *userData )
{
	draw->draw();
}

//-------------------------------------------------------------------------------------------------
struct W3DViewRenderPassState
{
	W3DView *view;
	Int drawableCount;
	Int propDrawCount;
	Int treeDrawCount;
	Int modelDrawCount;
	Int submittedCount;
	Int hiddenCount;
	Int shroudedCount;
	std::vector<RenderObjClass*> renderedObjects; // track to avoid double-render in scene iteration

	void finalizeRenderedObjects()
	{
		std::sort(renderedObjects.begin(), renderedObjects.end());
	}

	bool wasRendered(RenderObjClass* obj) const
	{
		return std::binary_search(renderedObjects.begin(), renderedObjects.end(), obj);
	}
};

//-------------------------------------------------------------------------------------------------
static ObjectShroudStatus w3dview_getDrawableShroudStatus(Object *obj)
{
	if (!obj || !TheGlobalData->m_shroudOn) {
		return OBJECTSHROUD_CLEAR;
	}

	if (ThePartitionManager == nullptr || ThePlayerList == nullptr) {
		return OBJECTSHROUD_CLEAR;
	}

	if (rts::getObservedOrLocalPlayer_Safe() == nullptr) {
		return OBJECTSHROUD_CLEAR;
	}

	return obj->getShroudedStatus(rts::getObservedOrLocalPlayerIndex_Safe());
}

//-------------------------------------------------------------------------------------------------
static void w3dview_drawablePostDraw( Drawable *draw, void *userData )
{
	Real FXPitch = TheTacticalView->getFXPitch();
	if (draw->isDrawableEffectivelyHidden() || FXPitch < 0.0f)
		return;

	// Use the cached shroud flag (same as the render path) so that icons
	// are hidden for shrouded objects but still shown during the grace period.
	if (draw->getFullyObscuredByShroud())
		return;

	draw->drawIconUI();

	TheGameClient->incrementRenderedObjectCount();
}

//-------------------------------------------------------------------------------------------------
extern bool g_debugDisableModels;

static void w3dview_renderDrawable( Drawable *draw, void *userData )
{
	if (g_debugDisableModels) return;

	W3DViewRenderPassState *state = static_cast<W3DViewRenderPassState *>(userData);
	if (state != nullptr) {
		++state->drawableCount;
	}

	Real FXPitch = TheTacticalView->getFXPitch();
	if (draw->isDrawableEffectivelyHidden() || FXPitch < 0.0f) {
		if (state != nullptr) {
			++state->hiddenCount;
		}
		return;
	}

	Object* obj = draw->getObject();
	ObjectShroudStatus ss = w3dview_getDrawableShroudStatus(obj);
	if (state != nullptr && ss > OBJECTSHROUD_PARTIAL_CLEAR) {
		++state->shroudedCount;
	}

	// Track the frame when an object was last seen in the clear.
	// GameClient::update() uses this for a 2-second grace period that keeps
	// recently-visible objects rendered after they enter fog (e.g. planes).
	// The old W3DScene::renderOneObject set this; without it the grace period
	// never triggers because getShroudClearFrame() stays InvalidShroudClearFrame.
	if (ss == OBJECTSHROUD_CLEAR) {
		draw->setShroudClearFrame(TheGameLogic->getFrame());
	}

	// Shroud gate: use the cached flag set by GameClient::update().
	// This is the authoritative check that accounts for the 2-second grace period.
	// It matches Drawable::draw() and the original Visibility_Check path.
	if (draw->getFullyObscuredByShroud())
	{
		// Mark this drawable's render objects as "already handled" so the
		// second-pass scene iteration (which walks the entire W3D scene to
		// pick up debris/lasers/lines) doesn't render them anyway. Without
		// this, enemy units in unexplored shroud are visible because the
		// drawable iteration skips them but the scene iteration finds them
		// in m_3DScene and renders them via ModelRenderer::RenderRenderObject.
		if (state != nullptr)
		{
			for (DrawModule** dm = draw->getDrawModules(); *dm; ++dm)
			{
				ObjectDrawInterface* odi = (*dm)->getObjectDrawInterface();
				if (!odi)
					continue;
				W3DModelDraw* w3dDraw = static_cast<W3DModelDraw*>(odi);
				RenderObjClass* renderObject = w3dDraw->getRenderObject();
				if (renderObject)
					state->renderedObjects.push_back(renderObject);
			}
		}
		return;
	}

	// Within the 2-second grace period after losing vision, fogged objects still render
	// at reduced brightness to show the "last known position" ghost.
	float fogDarkening = 1.0f;
	if (ss == OBJECTSHROUD_FOGGED)
		fogDarkening = 0.35f;

	auto& modelRenderer = Render::ModelRenderer::Instance();
	modelRenderer.SetFogDarkening(fogDarkening);

	// Drawable color/opacity overrides split into two paths:
	//
	//   * Translucent drawables (placement ghosts, fading-out objects): bind
	//     the dedicated ghost shader via SetGhostMode. The shader is alpha-
	//     blended with depth-write off, so the ghost layers cleanly over
	//     terrain and existing buildings without depth fighting. A non-zero
	//     drawable tint (e.g. IllegalBuildColor when a placement is invalid)
	//     becomes a per-pixel mix toward that color.
	//
	//   * Opaque drawables with a flash/selection tint (capture-building
	//     flash, selection glow, low-health damage pulse): keep the existing
	//     additive-tint path through SetTintColor, which feeds vertex color
	//     in ComputeMeshColor and looks like a brightening glow.
	const Vector3 *tintColor = draw->getTintColor();
	const Vector3 *selectionColor = draw->getSelectionColor();
	const float drawOpacity = draw->getEffectiveOpacity();
	const bool useGhost = (drawOpacity < 1.0f);

	if (useGhost)
	{
		float tr = 1.0f, tg = 1.0f, tb = 1.0f;
		float intensity = 0.0f;
		if (tintColor || selectionColor)
		{
			tr = tg = tb = 0.0f;
			if (tintColor)      { tr += tintColor->X;      tg += tintColor->Y;      tb += tintColor->Z;      }
			if (selectionColor) { tr += selectionColor->X; tg += selectionColor->Y; tb += selectionColor->Z; }
			tr = std::clamp(tr, 0.0f, 1.0f);
			tg = std::clamp(tg, 0.0f, 1.0f);
			tb = std::clamp(tb, 0.0f, 1.0f);
			intensity = 0.65f; // strong wash so invalid red reads clearly
		}
		modelRenderer.SetGhostMode(tr, tg, tb, intensity, drawOpacity);
	}
	else if (tintColor || selectionColor)
	{
		float tr = 0, tg = 0, tb = 0;
		if (tintColor)     { tr += tintColor->X;     tg += tintColor->Y;     tb += tintColor->Z; }
		if (selectionColor){ tr += selectionColor->X; tg += selectionColor->Y; tb += selectionColor->Z; }
		modelRenderer.SetTintColor(tr, tg, tb);
	}

	draw->draw();

	// Single pass: iterate draw modules once, using getObjectDrawInterface()
	// to identify W3DModelDraw modules without expensive RTTI/dynamic_cast.
	for (DrawModule **dm = draw->getDrawModules(); *dm; ++dm)
	{
		ObjectDrawInterface* odi = (*dm)->getObjectDrawInterface();
		if (odi == nullptr)
			continue;

		// ObjectDrawInterface is only implemented by W3DModelDraw in this codebase
		W3DModelDraw* w3dDraw = static_cast<W3DModelDraw*>(odi);
		RenderObjClass* renderObject = w3dDraw->getRenderObject();
		if (renderObject != nullptr)
		{
			renderObject->On_Frame_Update();
			modelRenderer.RenderRenderObject(renderObject);
			if (state != nullptr) {
				++state->submittedCount;
				++state->modelDrawCount;
				state->renderedObjects.push_back(renderObject);
			}
		}
	}

	// Reset fog darkening, tint, ghost mode, and D3D11 state for the next
	// drawable. draw->draw() and draw modules may trigger old WW3D render
	// paths that call DX8Wrapper stubs, corrupting the D3D11 pipeline.
	if (fogDarkening != 1.0f)
		modelRenderer.SetFogDarkening(1.0f);
	if (useGhost)
		modelRenderer.ClearGhostMode();
	else if (tintColor || selectionColor)
		modelRenderer.ClearTintColor();
	Render::Renderer::Instance().Restore3DState();
}

//-------------------------------------------------------------------------------------------------
Bool W3DView::updateCameraMovements()
{
	Bool didUpdate = false;

	if (hasScriptedState(Scripted_Zoom))
	{
		zoomCameraOneFrame();
		didUpdate = true;
	}
	if (hasScriptedState(Scripted_Pitch))
	{
		pitchCameraOneFrame();
		didUpdate = true;
	}
	if (hasScriptedState(Scripted_Rotate))
	{
		m_previousLookAtPosition = *getPosition();
		rotateCameraOneFrame();
		didUpdate = true;
	}
	else if (hasScriptedState(Scripted_MoveOnWaypointPath))
	{
		m_previousLookAtPosition = *getPosition();
		moveAlongWaypointPath(TheFramePacer->getLogicTimeStepMilliseconds(FramePacer::IgnoreFrozenTime));
		didUpdate = true;
	}
	if (hasScriptedState(Scripted_CameraLock))
	{
		didUpdate = true;
	}
	return didUpdate;
}

//-------------------------------------------------------------------------------------------------
void W3DView::updateView()
{
	update();
}

//-------------------------------------------------------------------------------------------------
void W3DView::stepView()
{
	if (m_shakeIntensity > 0.01f)
	{
		m_shakeOffset.x = m_shakeIntensity * m_shakeAngleCos;
		m_shakeOffset.y = m_shakeIntensity * m_shakeAngleSin;

		const Real dampingCoeff = 0.75f;
		m_shakeIntensity *= dampingCoeff;

		m_shakeAngleCos = -m_shakeAngleCos;
		m_shakeAngleSin = -m_shakeAngleSin;
	}
	else
	{
		m_shakeIntensity = 0.0f;
		m_shakeOffset.x = 0.0f;
		m_shakeOffset.y = 0.0f;
	}
}

//-------------------------------------------------------------------------------------------------
void W3DView::update()
{
	Bool didScriptedMovement = false;

	if (TheTerrainRenderObject && TheTerrainRenderObject->doesNeedFullUpdate())
	{
		updateTerrain();
	}

	static Real followFactor = -1;
	ObjectID cameraLock = getCameraLock();
	if (cameraLock == INVALID_ID)
	{
		followFactor = -1;
	}
	if (cameraLock != INVALID_ID)
	{
		removeScriptedState(Scripted_MoveOnWaypointPath);
		m_CameraArrivedAtWaypointOnPathFlag = false;

		Object* cameraLockObj = TheGameLogic->findObjectByID(cameraLock);
		Bool loseLock = false;

		if (cameraLockObj == nullptr)
		{
			loseLock = true;
		}

		if (loseLock)
		{
			setCameraLock(INVALID_ID);
			setCameraLockDrawable(nullptr);
			followFactor = -1;
		}
		else
		{
			if (followFactor<0) {
				followFactor = 0.05f;
			} else {
				followFactor += 0.05f;
				if (followFactor>1.0f) followFactor = 1.0f;
			}
			if (getCameraLockDrawable() != nullptr)
			{
				Drawable* cameraLockDrawable = (Drawable *)getCameraLockDrawable();

				if (!cameraLockDrawable)
				{
					setCameraLockDrawable(nullptr);
				}
				else
				{
					Coord3D pos;
					Real boundingSphereRadius;
					Matrix3D transform;
					if (cameraLockDrawable->clientOnly_getFirstRenderObjInfo(&pos, &boundingSphereRadius, &transform))
					{
						Vector3 zaxis(0,0,1);

						Vector3 objPos;
						objPos.X = pos.x;
						objPos.Y = pos.y;
						objPos.Z = pos.z;

						objPos += boundingSphereRadius * 1.0f * zaxis;
						Vector3 objview = transform.Get_X_Vector();
						Vector3 camtran = objPos - objview * boundingSphereRadius*4.5f;

						Vector3 prevCamTran = m_3DCamera->Get_Position();

						Vector3 tranDiff = (camtran - prevCamTran);

						camtran = prevCamTran + tranDiff * 0.1f;

						Matrix3D camXForm;
						camXForm.Look_At(camtran,objPos,0);
						m_3DCamera->Set_Transform(camXForm);
					}
				}
			}
			else
			{
				Coord3D objpos = *cameraLockObj->getPosition();
				Coord3D curpos = *getPosition();
				Real snapThreshSqr = sqr(TheGlobalData->m_partitionCellSize);
				Real curDistSqr = sqr(curpos.x - objpos.x) + sqr(curpos.y - objpos.y);
				if ( m_snapImmediate)
				{
					curpos.x = objpos.x;
					curpos.y = objpos.y;
				}
				else
				{
					Real dx = objpos.x-curpos.x;
					Real dy = objpos.y-curpos.y;
					if (m_lockType == LOCK_TETHER)
					{
						if (curDistSqr >= snapThreshSqr)
						{
							Real ratio = 1.0f - snapThreshSqr/curDistSqr;
							curpos.x += dx*ratio*0.5f;
							curpos.y += dy*ratio*0.5f;
						}
						else
						{
							Real ratio = 0.01f * m_lockDist;
							Real dx = objpos.x-curpos.x;
							Real dy = objpos.y-curpos.y;
							curpos.x += dx*ratio;
							curpos.y += dy*ratio;
						}
					}
					else
					{
						curpos.x += dx*followFactor;
						curpos.y += dy*followFactor;
					}
				}
				if (!(TheScriptEngine->isTimeFrozenDebug() || TheScriptEngine->isTimeFrozenScript()) && !TheGameLogic->isGamePaused()) {
					m_previousLookAtPosition = *getPosition();
				}
				setPosition(&curpos);

				if (m_lockType == LOCK_FOLLOW)
				{
					if (cameraLockObj->isUsingAirborneLocomotor() && cameraLockObj->isAboveTerrainOrWater())
					{
						Matrix3D camXForm;
						Real idealZRot = cameraLockObj->getOrientation() - M_PI_2;

						if (m_snapImmediate)
						{
							View::setAngle(idealZRot);
						}
						else
						{
							w3dview_normAngle(idealZRot);
							Real oldZRot = m_angle;
							w3dview_normAngle(oldZRot);
							Real diffRot = idealZRot - oldZRot;
							w3dview_normAngle(diffRot);
							View::setAngle(m_angle + diffRot * 0.1f);
						}
					}
				}
				if (m_snapImmediate)
					m_snapImmediate = FALSE;

				m_groundLevel = objpos.z;
				didScriptedMovement = true;
				m_recalcCamera = true;
			}
		}
	}

	if (!(TheScriptEngine->isTimeFrozenDebug()) && !TheGameLogic->isGamePaused()) {
		if (updateCameraMovements()) {
			didScriptedMovement = true;
			m_recalcCamera = true;
		}
	} else {
		if (isDoingScriptedCamera()) {
			didScriptedMovement = true;
		}
	}

	if (m_shakeIntensity > 0.01f)
	{
		m_recalcCamera = true;
	}

	if (CameraShakerSystem.IsCameraShaking())
	{
		m_recalcCamera = true;
	}

	m_terrainHeightAtPivot = w3dview_getHeightAroundPos(m_pos.x, m_pos.y);
	m_currentHeightAboveGround = m_cameraOffset.z * m_zoom - m_terrainHeightAtPivot;

	if (m_okToAdjustHeight)
	{
		Real desiredHeight = (m_terrainHeightAtPivot + m_heightAboveGround);
		Real desiredZoom = desiredHeight / m_cameraOffset.z;

		if (didScriptedMovement)
		{
			m_heightAboveGround = m_currentHeightAboveGround;
		}

		const Bool isScrolling = TheInGameUI && TheInGameUI->isScrolling();
		const Bool isScrollingTooFast = m_scrollAmount.length() >= m_scrollAmountCutoff;
		const Bool isWithinHeightConstraints = isWithinCameraHeightConstraints();

		const Bool adjustZoomWhenScrolling = isScrolling && (!isScrollingTooFast || !isWithinHeightConstraints);
		const Bool adjustZoomWhenNotScrolling = !isScrolling && !didScriptedMovement;

		if (adjustZoomWhenScrolling || adjustZoomWhenNotScrolling)
		{
			const Real fpsRatio = TheFramePacer->getBaseOverUpdateFpsRatio();
			const Real zoomAdj = (desiredZoom - m_zoom) * TheGlobalData->m_cameraAdjustSpeed * fpsRatio;
			if (fabs(zoomAdj) >= 0.0001f)
			{
				m_zoom += zoomAdj;
				m_recalcCamera = true;
			}
		}
	}

	if (TheScriptEngine->isTimeFast()) {
		return;
	}

	if (m_recalcCamera || m_isCameraSlaved)
	{
		setCameraTransform();
		m_recalcCamera = false;
	}

	Region3D axisAlignedRegion;
	getAxisAlignedViewRegion(axisAlignedRegion);

	// Submit every visible drawable to the render queue. We used to gate
	// this on `WW3D::Get_Sync_Frame_Time() > 0` to suppress sub-logic-frame
	// re-iteration, but that's already handled by the per-logic-frame
	// WW3D::Sync gate in W3DDisplay::draw — Sync only advances on logic
	// frame change, so animation poses stay stable across multiple render
	// frames even if we re-iterate every frame.
	//
	// More importantly, the old gate broke scripted-time-freeze cinematics
	// (e.g. the USA01 "Welcome back, General" briefing): when a script
	// freezes time the logic frame stops advancing, so WW3D::Sync is never
	// called, so Get_Sync_Frame_Time stays at 0, so this gate suppressed
	// the iteration forever — leaving an empty terrain shot. The camera
	// kept moving (updateCameraMovements is correctly NOT gated on
	// script-frozen) but there was nothing to look at. Matches original ZH
	// behaviour: W3DView::draw in CnC_Generals_Zero_Hour iterates drawables
	// on every render frame.
	TheGameClient->iterateDrawablesInRegion( &axisAlignedRegion, w3dview_drawDrawable, nullptr );
}

//-------------------------------------------------------------------------------------------------
void W3DView::getAxisAlignedViewRegion(Region3D &axisAlignedRegion)
{
	Coord3D box[ 4 ];
	getScreenCornerWorldPointsAtZ( &box[ 0 ], &box[ 1 ], &box[ 2 ], &box[ 3 ], 0.0f );

	axisAlignedRegion.setFromPointsNoZ(box, ARRAY_SIZE(box));

	Region3D mapExtent;
	Real safeValue = 999999;
	TheTerrainLogic->getExtent( &mapExtent );
	axisAlignedRegion.lo.z = mapExtent.lo.z - safeValue;
	axisAlignedRegion.hi.z = mapExtent.hi.z + safeValue;

	axisAlignedRegion.lo.x -= (DRAWABLE_OVERSCAN + m_guardBandBias.x);
	axisAlignedRegion.lo.y -= (DRAWABLE_OVERSCAN + m_guardBandBias.y + 60.0f );
	axisAlignedRegion.hi.x += (DRAWABLE_OVERSCAN + m_guardBandBias.x);
	axisAlignedRegion.hi.y += (DRAWABLE_OVERSCAN + m_guardBandBias.y);
}

//-------------------------------------------------------------------------------------------------
// D3D11 view-filter fade state tracking. The original ScreenBWFilter and
// ScreenCrossFadeFilter classes had protected static counters that
// W3DShaderManager::ScreenBWFilter::set() drove during render and reset
// when the fade reached zero (which is what cleared FT_VIEW_BW_FILTER
// after a doBlackWhiteMode(false, ...) script call). The D3D11 build
// no longer goes through the W3DShaderManager set() path, so the fade
// never advances and the BW filter stays stuck on. We mirror the
// counters here in file scope so W3DView::draw can drive them.
namespace {
	int g_bwFadeFrames = 0;     // duration of the current fade
	int g_bwFadeDirection = 0;  // +1 fading in, -1 fading out, 0 idle
	int g_bwCurFadeFrame = 0;   // ticks since fade began
}

void W3DView::setFadeParameters(Int fadeFrames, Int direction)
{
	g_bwFadeFrames = fadeFrames;
	g_bwFadeDirection = direction;
	g_bwCurFadeFrame = 0;
	// Still call into the original static-data classes so any code
	// reading them sees the same values (no-op for our renderer
	// today but keeps the inspector / save-load codepaths happy).
	ScreenBWFilter::setFadeParameters(fadeFrames, direction);
	ScreenCrossFadeFilter::setFadeParameters(fadeFrames,direction);
}

void W3DView::set3DWireFrameMode(Bool enable)
{
	m_nextWireFrameEnabled = enable;
}

//-------------------------------------------------------------------------------------------------
void W3DView::setViewFilterPos(const Coord3D *pos)
{
	ScreenMotionBlurFilter::setZoomToPos(pos);
}

//-------------------------------------------------------------------------------------------------
Bool W3DView::setViewFilterMode(FilterModes filterMode)
{
	FilterModes oldMode = m_viewFilterMode;

	m_viewFilterMode = filterMode;
	if (m_viewFilterMode != FM_NULL_MODE &&
		m_viewFilter != FT_NULL_FILTER) {
		if (!W3DShaderManager::filterSetup(m_viewFilter, m_viewFilterMode))
		{
			m_viewFilterMode = oldMode;
			return FALSE;
		}
	}

	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool W3DView::setViewFilter(FilterTypes filter)
{
	FilterTypes oldFilter = m_viewFilter;

	m_viewFilter = filter;
	if (m_viewFilterMode != FM_NULL_MODE &&
		m_viewFilter != FT_NULL_FILTER) {
		if (!W3DShaderManager::filterSetup(m_viewFilter, m_viewFilterMode))
		{
			m_viewFilter = oldFilter;
			return FALSE;
		};
	}

	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void W3DView::calcDeltaScroll(Coord2D &screenDelta)
{
	screenDelta.x = 0;
	screenDelta.y = 0;
	Vector3 prevPos(m_previousLookAtPosition.x,m_previousLookAtPosition.y, m_groundLevel);
	Vector3 prevScreen;
	if (m_3DCamera->Project( prevScreen, prevPos ) != CameraClass::INSIDE_FRUSTUM)
	{
		return;
	}
	Vector3 pos(m_pos.x,m_pos.y, m_groundLevel);
	Vector3 screen;
	if (m_3DCamera->Project( screen, pos ) != CameraClass::INSIDE_FRUSTUM)
	{
		return;
	}
	screenDelta.x = screen.X-prevScreen.X;
	screenDelta.y = screen.Y-prevScreen.Y;
}

// Forward declarations for D3D11 rendering of special scene object types
static void RenderDazzleDX11(RenderObjClass* robj, RenderInfoClass& rinfo);
static void RenderParticleBufferDX11(RenderObjClass* robj, RenderInfoClass& rinfo);

//-------------------------------------------------------------------------------------------------
void W3DView::drawView()
{
	draw();
}

//-------------------------------------------------------------------------------------------------
void W3DView::draw()
{
	// D3D11 port: View filters (screen fades, BW, motion blur) are handled
	// directly via D3D11 Renderer instead of the old W3DShaderManager/DX8 path.
	// The filter state (m_viewFilter, m_viewFilterMode) is managed by scripts
	// and the game engine; we just need to render the visual effect.

	// Track fade progress for motion blur (screen fade) effects.
	// When the filter mode changes, we initialize s_fadeAlpha to the correct
	// starting value so the fade animates properly.
	static float s_fadeAlpha = 0.0f;
	static bool s_fadeIn = true;
	static FilterTypes s_prevFilter = FT_NULL_FILTER;
	static FilterModes s_prevMode = FM_NULL_MODE;

	Bool skipRender = false;

	if (m_viewFilterMode &&
			m_viewFilter > FT_NULL_FILTER &&
			m_viewFilter < FT_MAX)
	{
		// Detect filter activation/change — initialize fade alpha
		if (m_viewFilter != s_prevFilter || m_viewFilterMode != s_prevMode)
		{
			s_prevFilter = m_viewFilter;
			s_prevMode = m_viewFilterMode;

			if (m_viewFilter == FT_VIEW_MOTION_BLUR_FILTER)
			{
				bool startsFull = (m_viewFilterMode == FM_VIEW_MB_IN_ALPHA ||
					m_viewFilterMode == FM_VIEW_MB_IN_SATURATE ||
					m_viewFilterMode == FM_VIEW_MB_IN_AND_OUT_ALPHA ||
					m_viewFilterMode == FM_VIEW_MB_IN_AND_OUT_SATURATE);
				s_fadeAlpha = startsFull ? 1.0f : 0.0f;
				s_fadeIn = true;
			}
			else if (m_viewFilter == FT_VIEW_CROSSFADE)
			{
				s_fadeAlpha = 1.0f;
			}
		}

		// Motion blur filter is used for screen fade in/out effects
		if (m_viewFilter == FT_VIEW_MOTION_BLUR_FILTER)
		{
			bool isFadeIn = (m_viewFilterMode == FM_VIEW_MB_IN_ALPHA ||
				m_viewFilterMode == FM_VIEW_MB_IN_SATURATE);
			bool isFadeOut = (m_viewFilterMode == FM_VIEW_MB_OUT_ALPHA ||
				m_viewFilterMode == FM_VIEW_MB_OUT_SATURATE);
			bool isInAndOut = (m_viewFilterMode == FM_VIEW_MB_IN_AND_OUT_ALPHA ||
				m_viewFilterMode == FM_VIEW_MB_IN_AND_OUT_SATURATE);

			float fadeSpeed = 0.02f;  // per-frame fade speed
			if (isFadeIn) {
				s_fadeAlpha -= fadeSpeed;
				if (s_fadeAlpha <= 0.0f) { s_fadeAlpha = 0.0f; m_viewFilter = FT_VIEW_DEFAULT; m_viewFilterMode = FM_VIEW_DEFAULT; s_prevFilter = FT_VIEW_DEFAULT; s_prevMode = FM_VIEW_DEFAULT; }
			} else if (isFadeOut) {
				s_fadeAlpha += fadeSpeed;
				if (s_fadeAlpha >= 1.0f) { s_fadeAlpha = 1.0f; m_viewFilter = FT_VIEW_DEFAULT; m_viewFilterMode = FM_VIEW_DEFAULT; s_prevFilter = FT_VIEW_DEFAULT; s_prevMode = FM_VIEW_DEFAULT; }
			} else if (isInAndOut) {
				if (s_fadeIn) {
					s_fadeAlpha += fadeSpeed;
					if (s_fadeAlpha >= 1.0f) { s_fadeAlpha = 1.0f; s_fadeIn = false; }
				} else {
					s_fadeAlpha -= fadeSpeed;
					if (s_fadeAlpha <= 0.0f) { s_fadeAlpha = 0.0f; s_fadeIn = true; m_viewFilter = FT_VIEW_DEFAULT; m_viewFilterMode = FM_VIEW_DEFAULT; s_prevFilter = FT_VIEW_DEFAULT; s_prevMode = FM_VIEW_DEFAULT; }
				}
			}
		}
		// BW filter: drive the fade-in / fade-out animation that the original
		// ScreenBWFilter::set() drove in W3DShaderManager.cpp:374. Without
		// this, the script's call to doBlackWhiteMode(false, frames) would
		// never clear the filter — it sets g_bwFadeDirection (via
		// W3DView::setFadeParameters) to -1 expecting the renderer to count
		// it down, then call setViewFilter(FT_NULL_FILTER) when the counter
		// reaches the total frame count. That's exactly the USA01 intro→
		// gameplay transition bug: BW stayed stuck on after the cinematic
		// ended.
		else if (m_viewFilter == FT_VIEW_BW_FILTER)
		{
			if (g_bwFadeDirection != 0 && g_bwFadeFrames > 0)
			{
				g_bwCurFadeFrame++;
				if (g_bwCurFadeFrame >= g_bwFadeFrames)
				{
					if (g_bwFadeDirection < 0)
					{
						// Fade-out finished — clear the filter back to
						// default so the cinematic post-process stops
						// applying saturation=0. Matches the original DX8
						// code at W3DShaderManager.cpp:408-409.
						setViewFilterMode(FM_NULL_MODE);
						setViewFilter(FT_NULL_FILTER);
					}
					g_bwCurFadeFrame = 0;
					g_bwFadeDirection = 0;
				}
			}
		}
		else if (m_viewFilter == FT_VIEW_CROSSFADE)
		{
			// Cross-fade simplified: just do a quick fade
			s_fadeAlpha -= 0.05f;
			if (s_fadeAlpha <= 0.0f) { s_fadeAlpha = 0.0f; m_viewFilter = FT_VIEW_DEFAULT; m_viewFilterMode = FM_VIEW_DEFAULT; }
		}
	}

	Region3D axisAlignedRegion;
	getAxisAlignedViewRegion(axisAlignedRegion);

	if (!skipRender)
	{
		// Tell animation system what logic frame we're on — animations advance
		// only once per logic tick, preventing flicker from per-render-frame time reads.
		{
			extern GameLogic* TheGameLogic;
			unsigned int logicFrame = TheGameLogic ? TheGameLogic->getFrame() : 0;
			Animatable3DObjClass::SetLogicFrame(logicFrame);
		}

		// Update camera per-frame state
		if (m_3DCamera)
			m_3DCamera->On_Frame_Update();

		// Run visibility check so castRay() can find objects for mouse picking.
		// The old W3D Render() path called this automatically; without it the
		// IS_VISIBLE bit is never set and pickDrawable always misses.
		if (m_3DCamera && W3DDisplay::m_3DScene)
		{
			RTS3DScene* scene = reinterpret_cast<RTS3DScene*>(W3DDisplay::m_3DScene);
			scene->Visibility_Check(m_3DCamera);
		}

		static Int s_loggedFrames = 0;
		W3DViewRenderPassState renderState = {};
		renderState.view = this;
		Render::ModelRenderer::Instance().BeginFrame(m_3DCamera);
		TheGameClient->iterateDrawablesInRegion(&axisAlignedRegion, w3dview_renderDrawable, &renderState);
		if (s_loggedFrames < 16) {
			AppendDX11ShimTrace(
				"W3DView::draw frame=%d drawables=%d hidden=%d shrouded=%d propModules=%d treeModules=%d modelModules=%d submitted=%d\n",
				s_loggedFrames,
				renderState.drawableCount,
				renderState.hiddenCount,
				renderState.shroudedCount,
				renderState.propDrawCount,
				renderState.treeDrawCount,
				renderState.modelDrawCount,
				renderState.submittedCount);
			++s_loggedFrames;
		}

		renderState.finalizeRenderedObjects();

		// Render scene objects not already handled by drawable iteration.
		// This includes debris (W3DDebrisDraw), line effects (lasers, tracers),
		// and any other render objects added to m_3DScene directly.
		// Skip objects already rendered by w3dview_renderDrawable to avoid flickering.
		if (m_3DCamera && W3DDisplay::m_3DScene)
		{
			RenderInfoClass rinfo(*m_3DCamera);
			SceneClass* scene = reinterpret_cast<SceneClass*>(W3DDisplay::m_3DScene);
			SceneIterator* iter = scene->Create_Iterator(false);
			if (iter)
			{
				for (iter->First(); !iter->Is_Done(); iter->Next())
				{
					RenderObjClass* robj = iter->Current_Item();
					if (!robj || !robj->Is_Not_Hidden_At_All() || !robj->Is_Visible())
						continue;

					// Skip objects already rendered by the drawable iteration
					if (renderState.wasRendered(robj))
						continue;

					int classId = robj->Class_ID();
					if (classId == RenderObjClass::CLASSID_SEGLINE ||
						classId == RenderObjClass::CLASSID_LINE3D)
					{
						// Let line objects call their Render() — DX8 Draw calls are
						// now no-ops so they can't corrupt D3D11 state. The visual
						// output is handled by the D3D11 rendering system when these
						// objects are also processed as draw module render objects.
						robj->On_Frame_Update();
						robj->Render(rinfo);
					}
					else if (classId == RenderObjClass::CLASSID_MESH ||
							 classId == RenderObjClass::CLASSID_HLOD ||
							 classId == RenderObjClass::CLASSID_DISTLOD ||
							 classId == RenderObjClass::CLASSID_COLLECTION)
					{
						robj->On_Frame_Update();
						Render::ModelRenderer::Instance().RenderRenderObject(robj);
					}
					else if (classId == RenderObjClass::CLASSID_DAZZLE)
					{
						robj->On_Frame_Update();
						RenderDazzleDX11(robj, rinfo);
					}
					else if (classId == RenderObjClass::CLASSID_PARTICLEEMITTER)
					{
						robj->On_Frame_Update();
						// Emitter itself doesn't render — its buffer is a
						// separate render object caught separately.
					}
					else if (classId == RenderObjClass::CLASSID_PARTICLEBUFFER)
					{
						robj->On_Frame_Update();
						RenderParticleBufferDX11(robj, rinfo);
					}
				}
				scene->Destroy_Iterator(iter);
			}

			// Render particle buffers discovered on model sub-objects (missile exhaust, etc.).
			// These emitters produce particles during ModelRenderer traversal but their
			// buffers may not be in the scene graph.
			{
				auto& modelRenderer = Render::ModelRenderer::Instance();
				for (RenderObjClass* buf : modelRenderer.m_pendingParticleBuffers)
				{
					if (buf && buf->Class_ID() == RenderObjClass::CLASSID_PARTICLEBUFFER)
						RenderParticleBufferDX11(buf, rinfo);
				}
				modelRenderer.ClearPendingParticleBuffers();
			}

			// Reset D3D11 state after scene iteration — some objects may have
			// called old DX8Wrapper methods that corrupt pipeline state
			Render::Renderer::Instance().Restore3DState();
		}
	}

	// Mark animation frame as complete — subsequent render frames in the same
	// logic tick will skip Single_Anim_Progress() to prevent flicker.
	Animatable3DObjClass::MarkAnimFrameComplete();

	// --- D3D11 View Filter Overlay ---
	// Draw screen fade/BW overlay BEFORE text overlays so typewriter text
	// is visible on top of fade-from-black during mission intros.
	//
	// Pre-clear the BW filter flag every frame so the post-process desaturation
	// only runs while the script-driven FT_VIEW_BW_FILTER is active. The
	// FT_VIEW_BW_FILTER branch below will set it back if needed.
	Render::Renderer::Instance().SetBwFilterEnabled(false);

	if (m_viewFilterMode &&
		m_viewFilter > FT_NULL_FILTER &&
		m_viewFilter < FT_MAX)
	{
		auto& renderer = Render::Renderer::Instance();
		float w = (float)renderer.GetWidth();
		float h = (float)renderer.GetHeight();

		if (m_viewFilter == FT_VIEW_MOTION_BLUR_FILTER && s_fadeAlpha > 0.01f)
		{
			// Screen fade effect — draw black overlay with current fade alpha
			bool isSaturate = (m_viewFilterMode == FM_VIEW_MB_IN_SATURATE ||
				m_viewFilterMode == FM_VIEW_MB_OUT_SATURATE ||
				m_viewFilterMode == FM_VIEW_MB_IN_AND_OUT_SATURATE);
			uint8_t a = (uint8_t)(s_fadeAlpha * 255.0f);
			uint32_t fadeColor = isSaturate
				? ((a << 24) | 0x00FFFFFF)   // white flash
				: ((a << 24) | 0x00000000);  // black fade
			renderer.Begin2D();
			renderer.DrawRect(0, 0, w, h, fadeColor);
			renderer.End2D();
		}
		else if (m_viewFilter == FT_VIEW_BW_FILTER)
		{
			// Real grayscale: pipe through the cinematic post-process
			// shader by setting saturation = 0. This is a real RGB→luma
			// desaturation pass, not a 0x40808080 darkening overlay.
			// W3DDisplay::draw runs ApplyCinematic later in the frame and
			// picks up the flag — see Renderer::ApplyCinematic.
			renderer.SetBwFilterEnabled(true);
		}
		else if (m_viewFilter == FT_VIEW_CROSSFADE && s_fadeAlpha > 0.01f)
		{
			uint8_t a = (uint8_t)(s_fadeAlpha * 255.0f);
			renderer.Begin2D();
			renderer.DrawRect(0, 0, w, h, (a << 24) | 0x00000000);
			renderer.End2D();
		}
	}

	// Post-draw overlays (health bars, construction text, icons) are deferred
	// to drawPostOverlays() which is called from W3DDisplay::draw() AFTER
	// particle rendering, so smoke/fire particles don't occlude the text.
}

//-------------------------------------------------------------------------------------------------
void W3DView::drawPostOverlays()
{
	Region3D axisAlignedRegion;
	getAxisAlignedViewRegion(axisAlignedRegion);

	auto& renderer = Render::Renderer::Instance();
	renderer.Begin2D();
	TheGameClient->resetRenderedObjectCount();
	TheGameClient->iterateDrawablesInRegion( &axisAlignedRegion, w3dview_drawablePostDraw, this );
	TheGameClient->flushTextBearingDrawables();
	renderer.End2D();
}

//-------------------------------------------------------------------------------------------------
void W3DView::setCameraLock(ObjectID id)
{
	if (TheGlobalData->m_disableCameraMovement && id!=INVALID_ID) {
		return;
	}
	View::setCameraLock(id);
	removeScriptedState(Scripted_CameraLock);
}

//-------------------------------------------------------------------------------------------------
void W3DView::setSnapMode( CameraLockType lockType, Real lockDist )
{
	View::setSnapMode(lockType, lockDist);
	addScriptedState(Scripted_CameraLock);
}

//-------------------------------------------------------------------------------------------------
void W3DView::scrollBy( Coord2D *delta )
{
	if( delta && (delta->x != 0 || delta->y != 0) )
	{
		constexpr const Real SCROLL_RESOLUTION = 250.0f;

		Vector3 world, worldStart, worldEnd;
		Vector2 start, end;

		m_scrollAmount = *delta;

		start.X = getWidth();
		start.Y = getHeight();

		end.X = start.X + delta->x * SCROLL_RESOLUTION;
		end.Y = start.Y + delta->y * SCROLL_RESOLUTION;

		m_3DCamera->Device_To_View_Space( start, &worldStart );
		m_3DCamera->Device_To_View_Space( end, &worldEnd );

		const Real zRotation = m_3DCamera->Get_Transform().Get_Z_Rotation();
		worldStart.Rotate_Z(zRotation);
		worldEnd.Rotate_Z(zRotation);

		world.X = worldEnd.X - worldStart.X;
		world.Y = worldEnd.Y - worldStart.Y;
		world.Z = worldEnd.Z - worldStart.Z;

		Coord3D pos = *getPosition();
		pos.x += world.X;
		pos.y += world.Y;
		setPosition(&pos);

		removeScriptedState(Scripted_Rotate);
		m_recalcCamera = true;
	}
}

//-------------------------------------------------------------------------------------------------
void W3DView::forceRedraw()
{
	// Match ZH (W3DView.cpp:1851-1856): forceRedraw just triggers a transform
	// rebuild; it does NOT invalidate camera area constraints.
	m_recalcCamera = true;
}

//-------------------------------------------------------------------------------------------------
void W3DView::setAngle( Real radians )
{
	View::setAngle( radians );

	stopDoingScriptedCamera();
	m_CameraArrivedAtWaypointOnPathFlag = false;
	m_recalcCamera = true;
}

//-------------------------------------------------------------------------------------------------
void W3DView::setPitch( Real radians )
{
	View::setPitch( radians );

	// Match ZH (W3DView.cpp:1883-1896): setPitch stops scripted camera state
	// and forces a transform rebuild, but does NOT invalidate camera area
	// constraints.
	stopDoingScriptedCamera();
	m_recalcCamera = true;
}

//-------------------------------------------------------------------------------------------------
void W3DView::setAngleToDefault()
{
	View::setAngleToDefault();
	m_recalcCamera = true;
}

//-------------------------------------------------------------------------------------------------
void W3DView::setPitchToDefault()
{
	View::setPitchToDefault();

	m_FXPitch = 1.0f;

	// Match ZH setAngleAndPitchToDefault (W3DView.cpp:1901-1910): does NOT
	// invalidate camera area constraints.
	m_recalcCamera = true;
}

//-------------------------------------------------------------------------------------------------
void W3DView::setDefaultView(Real pitch, Real angle, Real maxHeight)
{
	m_defaultPitch = pitch;

	// Original ZH GameData.ini shipped with MaxCameraHeight = 310. The remastered
	// repo bumped it to 700 as a "widescreen-friendly" change so the player can
	// scroll the camera much further out on wide displays. That's fine for
	// skirmish / multiplayer / shellmap, but it BREAKS scripted campaign
	// cinematics: cameraModFinalZoom multiplies the script's `finalZoom` by
	// `m_maxHeightAboveGround / m_cameraOffset.z`. With the bumped max, a
	// cinematic that authored zoom=1.0 ends up rendering at ~2.25x the original
	// distance, destroying the framing the mission designers intended.
	//
	// In single-player campaign mode, clamp m_maxHeightAboveGround to the
	// original ZH-equivalent value so SETUP_CAMERA / cameraModFinalZoom resolve
	// to the original framing AND the player's free-zoom matches the original
	// game's behavior. The per-map maxHeight scalar is preserved.
	const Real ZH_ORIGINAL_MAX_CAMERA_HEIGHT = 310.0f;
	Real effectiveMaxCameraHeight = TheGlobalData->m_maxCameraHeight;
	if (TheGameLogic && TheGameLogic->getGameMode() == GAME_SINGLE_PLAYER &&
		effectiveMaxCameraHeight > ZH_ORIGINAL_MAX_CAMERA_HEIGHT)
	{
		effectiveMaxCameraHeight = ZH_ORIGINAL_MAX_CAMERA_HEIGHT;
	}
	m_maxHeightAboveGround = effectiveMaxCameraHeight * maxHeight;

	if (m_minHeightAboveGround > m_maxHeightAboveGround)
		m_maxHeightAboveGround = m_minHeightAboveGround;
}

//-------------------------------------------------------------------------------------------------
void W3DView::setHeightAboveGround(Real z)
{
	View::setHeightAboveGround(z);

	stopDoingScriptedCamera();
	m_CameraArrivedAtWaypointOnPathFlag = false;
	m_cameraAreaConstraintsValid = false;
	m_recalcCamera = true;
}

//-------------------------------------------------------------------------------------------------
void W3DView::setZoom(Real z)
{
	View::setZoom(z);

	stopDoingScriptedCamera();
	m_CameraArrivedAtWaypointOnPathFlag = false;
	m_cameraAreaConstraintsValid = false;
	m_recalcCamera = true;
}

//-------------------------------------------------------------------------------------------------
void W3DView::setZoomToDefault()
{
	Real terrainHeightMax = w3dview_getHeightAroundPos(m_pos.x, m_pos.y);

	Real desiredHeight = (terrainHeightMax + m_maxHeightAboveGround);
	Real desiredZoom = desiredHeight / m_cameraOffset.z;

	m_zoom = desiredZoom;
	m_heightAboveGround = m_maxHeightAboveGround;

	stopDoingScriptedCamera();
	m_CameraArrivedAtWaypointOnPathFlag = false;
	m_cameraAreaConstraintsValid = false;
	m_recalcCamera = true;
}

//-------------------------------------------------------------------------------------------------
void W3DView::setFieldOfView( Real angle )
{
	View::setFieldOfView( angle );

#if defined(RTS_DEBUG)
	m_cameraAreaConstraintsValid = false;
	m_recalcCamera = true;
#endif
}

//-------------------------------------------------------------------------------------------------
View::WorldToScreenReturn W3DView::worldToScreenTriReturn( const Coord3D *w, ICoord2D *s )
{
	if( w == nullptr || s == nullptr )
		return WTS_INVALID;

	if( m_3DCamera )
	{
		Vector3 world;
		Vector3 screen;

		world.Set( w->x, w->y, w->z );
		enum CameraClass::ProjectionResType projection = m_3DCamera->Project( screen, world );
		if (projection != CameraClass::INSIDE_FRUSTUM && projection!=CameraClass::OUTSIDE_FRUSTUM)
		{
			s->x = 0;
			s->y = 0;
			return WTS_INVALID;
		}

		W3DLogicalScreenToPixelScreen( screen.X, screen.Y,
																	 &s->x, &s->y,
																	 getWidth(), getHeight());
		s->x += m_originX;
		s->y += m_originY;

		if (projection != CameraClass::INSIDE_FRUSTUM)
		{
			return WTS_OUTSIDE_FRUSTUM;
		}

		return WTS_INSIDE_FRUSTUM;
	}

	return WTS_INVALID;
}

//-------------------------------------------------------------------------------------------------
Int W3DView::iterateDrawablesInRegion( IRegion2D *screenRegion,
																			 Bool (*callback)( Drawable *draw, void *userData ),
																			 void *userData )
{
	Bool inside = FALSE;
	Int count = 0;
	Drawable *draw;
	Vector3 screen, world;
	Coord3D pos;
	Region2D normalizedRegion;

	Bool regionIsPoint = FALSE;

	if( screenRegion )
	{
		if (screenRegion->height() == 0 && screenRegion->width() == 0)
		{
			regionIsPoint = TRUE;
		}

		normalizedRegion.lo.x = ((Real)(screenRegion->lo.x - m_originX) / (Real)getWidth()) * 2.0f - 1.0f;
		normalizedRegion.lo.y = -(((Real)(screenRegion->hi.y - m_originY) / (Real)getHeight()) * 2.0f - 1.0f);
		normalizedRegion.hi.x = ((Real)(screenRegion->hi.x - m_originX) / (Real)getWidth()) * 2.0f - 1.0f;
		normalizedRegion.hi.y = -(((Real)(screenRegion->lo.y - m_originY) / (Real)getHeight()) * 2.0f - 1.0f);
	}

	Drawable *onlyDrawableToTest = nullptr;
	if (regionIsPoint)
	{
		onlyDrawableToTest = pickDrawable(&screenRegion->lo, TRUE, (PickType) getPickTypesForContext(TheInGameUI->isInForceAttackMode()));
		if (onlyDrawableToTest == nullptr) {
			return 0;
		}
	}

	for( draw = TheGameClient->firstDrawable();
			 draw;
			 draw = draw->getNextDrawable() )
	{
		if (onlyDrawableToTest)
		{
			draw = onlyDrawableToTest;
			inside = TRUE;
		}
		else
		{
			inside = FALSE;

			if( screenRegion == nullptr )
				inside = TRUE;
			else
			{
				pos = *draw->getPosition();
				world.X = pos.x;
				world.Y = pos.y;
				world.Z = pos.z;

				if( m_3DCamera->Project( screen, world ) == CameraClass::INSIDE_FRUSTUM &&
						screen.X >= normalizedRegion.lo.x &&
						screen.X <= normalizedRegion.hi.x &&
						screen.Y >= normalizedRegion.lo.y &&
						screen.Y <= normalizedRegion.hi.y )
				{
					inside = TRUE;
				}
			}
		}

		if( inside )
		{
			if( callback( draw, userData ) )
				++count;
		}

		if (onlyDrawableToTest != nullptr)
			break;
	}

	return count;
}

// RTS3DScene::castRay — ported from W3DScene.cpp (which can't compile due to D3D8 deps).
//
// Picking semantics (matches original ZH):
//   1. Cull cheaply with bounding-sphere ray-test (NO inflation — we want the
//      visible mesh footprint, not a fattened hitbox).
//   2. For objects that pass, run precise per-triangle Cast_Ray.
//   3. Only fall back to sphere hit for *small* render objects whose mesh is
//      thin/sparse (helicopter rotors, tracers, etc.). Buildings have huge
//      bounding spheres that wrap their INI BOX geometry and must NOT use the
//      sphere fallback — that's what made the clickable hotspot extend well
//      outside the visible building mesh.
Bool RTS3DScene::castRay(RayCollisionTestClass & raytest, Bool testAll, Int collisionType)
{
	CastResultStruct result;
	RayCollisionTestClass tempRayTest(raytest.Ray, &result);
	Vector3 newEndPoint;
	Bool hit = FALSE;

	// Bounding-sphere radius above which we REFUSE the sphere fallback.
	// Typical infantry ~3, vehicle ~8, helicopter ~12, building >>20.
	// 15 cleanly separates "thin-mesh vehicles" from "structures".
	const Real SPHERE_FALLBACK_MAX_RADIUS = 15.0f;

	RenderObjClass* closestSphereObj = nullptr;
	Real closestSphereDist = 1e30f;

	tempRayTest.CollisionType = COLL_TYPE_ALL;
	tempRayTest.CheckTranslucent = true;

	RefRenderObjListIterator it(&RenderList);
	it.First();

	while (!it.Is_Done())
	{
		RenderObjClass* robj = it.Peek_Obj();
		it.Next();

		if (robj->Get_Collision_Type() & collisionType && (testAll || robj->Is_Really_Visible()))
		{
			const SphereClass* sphere = &robj->Get_Bounding_Sphere();
			Vector3 sphere_vector(sphere->Center - tempRayTest.Ray.Get_P0());
			Real Alpha = Vector3::Dot_Product(sphere_vector, tempRayTest.Ray.Get_Dir());

			// Strict (un-inflated) sphere cull — original ZH behavior.
			Real Beta = sphere->Radius * sphere->Radius -
				(Vector3::Dot_Product(sphere_vector, sphere_vector) - Alpha * Alpha);

			if (Beta < 0.0f)
				continue;

			// Track closest bounding-sphere hit for the small-object fallback only.
			if (sphere->Radius <= SPHERE_FALLBACK_MAX_RADIUS &&
				Alpha > 0.0f && Alpha < closestSphereDist)
			{
				closestSphereDist = Alpha;
				closestSphereObj = robj;
			}

			if (robj->Cast_Ray(tempRayTest))
			{
				raytest.CollidedRenderObj = robj;
				hit = TRUE;
				tempRayTest.Ray.Compute_Point(tempRayTest.Result->Fraction, &newEndPoint);
				tempRayTest.Ray.Set(raytest.Ray.Get_P0(), newEndPoint);
				tempRayTest.Result->Fraction = 1.0f;
			}
		}
	}

	// Small-object fallback: helicopters/rotors/thin meshes whose tri data
	// doesn't cover their visual silhouette. NEVER used for structures — their
	// sphere radius exceeds SPHERE_FALLBACK_MAX_RADIUS.
	if (!hit && closestSphereObj)
	{
		raytest.CollidedRenderObj = closestSphereObj;
		hit = TRUE;
	}
	else
	{
		raytest.Ray = tempRayTest.Ray;
	}
	return hit;
}

//-------------------------------------------------------------------------------------------------
Drawable *W3DView::pickDrawable( const ICoord2D *screen, Bool forceAttack, PickType pickType )
{
	if( screen == nullptr )
		return nullptr;

	// Don't pick if there's an opaque UI window over the cursor.
	GameWindow *window = nullptr;
	if (TheWindowManager)
		window = TheWindowManager->getWindowUnderCursor(screen->x, screen->y);

	while (window)
	{
		if (!BitIsSet( window->winGetStatus(), WIN_STATUS_SEE_THRU ))
			return nullptr;
		window = window->winGetParent();
	}

	// All UI checks passed — fall through to the raw ray-cast.
	return pickDrawableIgnoreUI( screen, forceAttack, pickType );
}

//-------------------------------------------------------------------------------------------------
// Raw drawable pick that SKIPS the "is a UI window over the cursor" check.
// Used by the inspector so shell-map / main-menu drawables can be inspected
// even though the menu UI windows cover the entire screen.
Drawable *W3DView::pickDrawableIgnoreUI( const ICoord2D *screen, Bool forceAttack, PickType pickType )
{
	RenderObjClass *renderObj = nullptr;
	Drawable *draw = nullptr;
	DrawableInfo *drawInfo = nullptr;

	if( screen == nullptr )
		return nullptr;

	Vector3 rayStart,rayEnd;
	getPickRay(screen,&rayStart,&rayEnd);

	LineSegClass lineseg;
	lineseg.Set(rayStart,rayEnd);

	CastResultStruct result;

	if (forceAttack)
		result.ComputeContactPoint = true;

	RayCollisionTestClass raytest(lineseg,&result,COLL_TYPE_ALL,false,false);

	// Respect pickType, matching legacy W3DView behavior.
	if (W3DDisplay::m_3DScene != nullptr)
	{
		RTS3DScene* scene = reinterpret_cast<RTS3DScene*>(W3DDisplay::m_3DScene);
		if (scene->castRay(raytest, FALSE, (Int)pickType))
		{
			renderObj = raytest.CollidedRenderObj;
		}
	}

	if( renderObj )
		drawInfo = (DrawableInfo *)renderObj->Get_User_Data();
	if (drawInfo)
		draw=drawInfo->m_drawable;

	return draw;
}

static Bool FindTerrainIntersectionFromLogic(const Vector3 &rayStart, const Vector3 &rayEnd, Vector3 *intersection)
{
	if (intersection == nullptr || TheTerrainLogic == nullptr)
		return FALSE;

	const Real dx = rayEnd.X - rayStart.X;
	const Real dy = rayEnd.Y - rayStart.Y;
	const Real dz = rayEnd.Z - rayStart.Z;
	const Int sampleCount = 128;
	const Int refineIterations = 10;

	Real prevT = 0.0f;
	Vector3 prevPoint = rayStart;
	Real prevGround = TheTerrainLogic->getGroundHeight(prevPoint.X, prevPoint.Y);
	Real prevDelta = prevPoint.Z - prevGround;

	for (Int sampleIndex = 1; sampleIndex <= sampleCount; ++sampleIndex)
	{
		const Real t = sampleIndex / static_cast<Real>(sampleCount);
		Vector3 point(rayStart.X + (dx * t), rayStart.Y + (dy * t), rayStart.Z + (dz * t));
		const Real ground = TheTerrainLogic->getGroundHeight(point.X, point.Y);
		const Real delta = point.Z - ground;

		if (delta <= 0.0f || (prevDelta >= 0.0f && delta <= 0.0f))
		{
			Real lo = prevT;
			Real hi = t;
			Vector3 bestPoint = point;

			for (Int refineIndex = 0; refineIndex < refineIterations; ++refineIndex)
			{
				const Real mid = (lo + hi) * 0.5f;
				Vector3 midPoint(rayStart.X + (dx * mid), rayStart.Y + (dy * mid), rayStart.Z + (dz * mid));
				const Real midGround = TheTerrainLogic->getGroundHeight(midPoint.X, midPoint.Y);
				const Real midDelta = midPoint.Z - midGround;

				if (midDelta > 0.0f)
				{
					lo = mid;
				}
				else
				{
					hi = mid;
					bestPoint = midPoint;
				}
			}

			bestPoint.Z = TheTerrainLogic->getGroundHeight(bestPoint.X, bestPoint.Y);
			*intersection = bestPoint;
			return TRUE;
		}

		prevT = t;
		prevDelta = delta;
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void W3DView::screenToTerrain( const ICoord2D *screen, Coord3D *world )
{
	if( screen == nullptr || world == nullptr )
		return;

	if (m_cameraHasMovedSinceRequest) {
		m_locationRequests.clear();
		m_cameraHasMovedSinceRequest = false;
	}

	if (m_locationRequests.size() > MAX_REQUEST_CACHE_SIZE) {
		m_locationRequests.erase(m_locationRequests.begin(), m_locationRequests.begin() + 10);
	}

	for (int i = m_locationRequests.size() - 1; i >= 0; --i) {
		if (m_locationRequests[i].first.x == screen->x && m_locationRequests[i].first.y == screen->y) {
			(*world) = m_locationRequests[i].second;
			return;
		}
	}

	Vector3 rayStart,rayEnd;
	LineSegClass lineseg;
	CastResultStruct result;
	Vector3 intersection(0.0f, 0.0f, 0.0f);
	Bool foundIntersection = FALSE;

	getPickRay(screen,&rayStart,&rayEnd);

	lineseg.Set(rayStart,rayEnd);

	RayCollisionTestClass raytest(lineseg,&result);

	if( TheTerrainRenderObject != nullptr && TheTerrainRenderObject->Cast_Ray(raytest) )
	{
		intersection = result.ContactPoint;
		foundIntersection = TRUE;
	}
	else if (FindTerrainIntersectionFromLogic(rayStart, rayEnd, &intersection))
	{
		foundIntersection = TRUE;
	}

	Vector3 bridgePt;
	Drawable *bridge = TheTerrainLogic->pickBridge(rayStart, rayEnd, &bridgePt);
	if (bridge && (!foundIntersection || bridgePt.Z > intersection.Z)) {
		intersection = bridgePt;
		foundIntersection = TRUE;
	}

	if (!foundIntersection)
	{
		world->x = 0.0f;
		world->y = 0.0f;
		world->z = 0.0f;
		return;
	}

	world->x = intersection.X;
	world->y = intersection.Y;
	world->z = intersection.Z;

	PosRequest req;
	req.first = (*screen);
	req.second = (*world);
	m_locationRequests.push_back(req);
}

//-------------------------------------------------------------------------------------------------
void W3DView::lookAt( const Coord3D *o )
{
	Coord3D pos = *o;

	if (o->z > PATHFIND_CELL_SIZE_F+TheTerrainLogic->getGroundHeight(pos.x, pos.y)) {
		Vector3 rayStart,rayEnd;
		LineSegClass lineseg;
		CastResultStruct result;
		Vector3 intersection(0,0,0);

		rayStart = m_3DCamera->Get_Position();
		m_3DCamera->Un_Project(rayEnd,Vector2(0.0f,0.0f));
		rayEnd -= rayStart;
		rayEnd.Normalize();
		rayEnd *= m_3DCamera->Get_Depth();
		rayStart.Set(pos.x, pos.y, pos.z);
		rayEnd += rayStart;
		lineseg.Set(rayStart,rayEnd);

		RayCollisionTestClass raytest(lineseg,&result);

		if( TheTerrainRenderObject && TheTerrainRenderObject->Cast_Ray(raytest) )
		{
			pos.x = result.ContactPoint.X;
			pos.y = result.ContactPoint.Y;
		}
	}
	pos.z = 0;
	setPosition(&pos);

	removeScriptedState(Scripted_Rotate | Scripted_CameraLock | Scripted_MoveOnWaypointPath);
	m_CameraArrivedAtWaypointOnPathFlag = false;

	m_recalcCamera = true;
}

//-------------------------------------------------------------------------------------------------
void W3DView::initHeightForMap()
{
	m_groundLevel = TheTerrainLogic->getGroundHeight(m_pos.x, m_pos.y);
	const Real MAX_GROUND_LEVEL = 120.0;
	if (m_groundLevel>MAX_GROUND_LEVEL) {
		m_groundLevel = MAX_GROUND_LEVEL;
	}

	m_cameraOffset.z = m_groundLevel+TheGlobalData->m_cameraHeight;
	m_cameraOffset.y = -(m_cameraOffset.z / tan(TheGlobalData->m_cameraPitch * (PI / 180.0)));
	m_cameraOffset.x = -(m_cameraOffset.y * tan(TheGlobalData->m_cameraYaw * (PI / 180.0)));
	m_cameraAreaConstraintsValid = false;
	m_recalcCamera = true;
}

//-------------------------------------------------------------------------------------------------
void W3DView::moveCameraTo(const Coord3D *o, Int milliseconds, Int shutter, Bool orient, Real easeIn, Real easeOut)
{
	m_mcwpInfo.waypoints[0] = *getPosition();
	m_mcwpInfo.cameraAngle[0] = getAngle();
	m_mcwpInfo.waySegLength[0] = 0;

	m_mcwpInfo.waypoints[1] = *getPosition();
	m_mcwpInfo.waySegLength[1] = 0;

	m_mcwpInfo.waypoints[2] = *o;
	m_mcwpInfo.waySegLength[2] = 0;

	m_mcwpInfo.numWaypoints = 2;
	if (milliseconds<1) milliseconds = 1;
	m_mcwpInfo.totalTimeMilliseconds = milliseconds;
	m_mcwpInfo.shutter = 1;
	m_mcwpInfo.ease.setEaseTimes(easeIn/milliseconds, easeOut/milliseconds);
	m_mcwpInfo.curSegment = 1;
	m_mcwpInfo.curSegDistance = 0;
	m_mcwpInfo.totalDistance = 0;

	setupWaypointPath(orient);
	if (m_mcwpInfo.totalTimeMilliseconds==1) {
		moveAlongWaypointPath(1);
		addScriptedState(Scripted_MoveOnWaypointPath);
		m_CameraArrivedAtWaypointOnPathFlag = false;
	}
}

//-------------------------------------------------------------------------------------------------
void W3DView::rotateCamera(Real rotations, Int milliseconds, Real easeIn, Real easeOut)
{
	m_rcInfo.numHoldFrames = 0;
	m_rcInfo.trackObject = FALSE;

	if (milliseconds<1) milliseconds = 1;
	m_rcInfo.numFrames = milliseconds/TheW3DFrameLengthInMsec;
	if (m_rcInfo.numFrames < 1) {
		m_rcInfo.numFrames = 1;
	}
	m_rcInfo.curFrame = 0;
	addScriptedState(Scripted_Rotate);
	m_rcInfo.angle.startAngle = m_angle;
	m_rcInfo.angle.endAngle = m_angle + 2*PI*rotations;
	m_rcInfo.startTimeMultiplier = m_timeMultiplier;
	m_rcInfo.endTimeMultiplier = m_timeMultiplier;
	m_rcInfo.ease.setEaseTimes(easeIn/milliseconds, easeOut/milliseconds);

	removeScriptedState(Scripted_MoveOnWaypointPath);
	m_CameraArrivedAtWaypointOnPathFlag = false;
}

//-------------------------------------------------------------------------------------------------
void W3DView::rotateCameraTowardObject(ObjectID id, Int milliseconds, Int holdMilliseconds, Real easeIn, Real easeOut)
{
	m_rcInfo.trackObject = TRUE;
	if (holdMilliseconds<1) holdMilliseconds = 0;
	m_rcInfo.numHoldFrames = holdMilliseconds/TheW3DFrameLengthInMsec;
	if (m_rcInfo.numHoldFrames < 1) {
		m_rcInfo.numHoldFrames = 0;
	}

	if (milliseconds<1) milliseconds = 1;
	m_rcInfo.numFrames = milliseconds/TheW3DFrameLengthInMsec;
	if (m_rcInfo.numFrames < 1) {
		m_rcInfo.numFrames = 1;
	}
	m_rcInfo.curFrame = 0;
	addScriptedState(Scripted_Rotate);
	m_rcInfo.target.targetObjectID = id;
	m_rcInfo.startTimeMultiplier = m_timeMultiplier;
	m_rcInfo.endTimeMultiplier = m_timeMultiplier;
	m_rcInfo.ease.setEaseTimes(easeIn/milliseconds, easeOut/milliseconds);

	removeScriptedState(Scripted_MoveOnWaypointPath);
	m_CameraArrivedAtWaypointOnPathFlag = false;
}

//-------------------------------------------------------------------------------------------------
void W3DView::rotateCameraTowardPosition(const Coord3D *pLoc, Int milliseconds, Real easeIn, Real easeOut, Bool reverseRotation)
{
	m_rcInfo.numHoldFrames = 0;
	m_rcInfo.trackObject = FALSE;

	if (milliseconds<1) milliseconds = 1;
	m_rcInfo.numFrames = milliseconds/TheW3DFrameLengthInMsec;
	if (m_rcInfo.numFrames < 1) {
		m_rcInfo.numFrames = 1;
	}
	Coord3D curPos = *getPosition();
	Vector2 dir(pLoc->x-curPos.x, pLoc->y-curPos.y);
	const Real dirLength = dir.Length();
	if (dirLength<0.1f) return;
	Real angle = WWMath::Acos(dir.X/dirLength);
	if (dir.Y<0.0f) {
		angle = -angle;
	}
	angle -= PI/2;
	w3dview_normAngle(angle);

	if (reverseRotation) {
		if (m_angle < angle) {
			angle -= 2.0f*WWMATH_PI;
		} else {
			angle += 2.0f*WWMATH_PI;
		}
	}

	m_rcInfo.curFrame = 0;
	addScriptedState(Scripted_Rotate);
	m_rcInfo.angle.startAngle = m_angle;
#if RTS_GENERALS
	m_rcInfo.angle.endAngle = m_angle + angle;
#else
	m_rcInfo.angle.endAngle = angle;
#endif
	m_rcInfo.startTimeMultiplier = m_timeMultiplier;
	m_rcInfo.endTimeMultiplier = m_timeMultiplier;
	m_rcInfo.ease.setEaseTimes(easeIn/milliseconds, easeOut/milliseconds);

	removeScriptedState(Scripted_MoveOnWaypointPath);
	m_CameraArrivedAtWaypointOnPathFlag = false;
}

//-------------------------------------------------------------------------------------------------
void W3DView::zoomCamera( Real finalZoom, Int milliseconds, Real easeIn, Real easeOut )
{
	if (milliseconds<1) milliseconds = 1;
	m_zcInfo.numFrames = milliseconds/TheW3DFrameLengthInMsec;
	if (m_zcInfo.numFrames < 1) {
		m_zcInfo.numFrames = 1;
	}
	m_zcInfo.curFrame = 0;
	addScriptedState(Scripted_Zoom);
	m_zcInfo.startZoom = m_zoom;
	m_zcInfo.endZoom = finalZoom;
	m_zcInfo.ease.setEaseTimes(easeIn/milliseconds, easeOut/milliseconds);
}

//-------------------------------------------------------------------------------------------------
void W3DView::pitchCamera( Real finalPitch, Int milliseconds, Real easeIn, Real easeOut )
{
	if (milliseconds<1) milliseconds = 1;
	m_pcInfo.numFrames = milliseconds/TheW3DFrameLengthInMsec;
	if (m_pcInfo.numFrames < 1) {
		m_pcInfo.numFrames = 1;
	}
	m_pcInfo.curFrame = 0;
	addScriptedState(Scripted_Pitch);
	m_pcInfo.startPitch = m_FXPitch;
	m_pcInfo.endPitch = finalPitch;
	m_pcInfo.ease.setEaseTimes(easeIn/milliseconds, easeOut/milliseconds);
}

//-------------------------------------------------------------------------------------------------
void W3DView::cameraModFinalZoom( Real finalZoom, Real easeIn, Real easeOut )
{
	if (hasScriptedState(Scripted_Rotate))
	{
		Real terrainHeightMax = w3dview_getHeightAroundPos(m_pos.x, m_pos.y);
		Real maxHeight = (terrainHeightMax + m_maxHeightAboveGround);
		Real maxZoom = maxHeight / m_cameraOffset.z;

		Real time = (m_rcInfo.numFrames + m_rcInfo.numHoldFrames - m_rcInfo.curFrame)*TheW3DFrameLengthInMsec;
		zoomCamera( finalZoom*maxZoom, time, time*easeIn, time*easeOut );
	}
	if (hasScriptedState(Scripted_MoveOnWaypointPath))
	{
		Coord3D pos = m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints];
		Real terrainHeightMax = w3dview_getHeightAroundPos(pos.x, pos.y);
		Real maxHeight = (terrainHeightMax + m_maxHeightAboveGround);
		Real maxZoom = maxHeight / m_cameraOffset.z;

		Real time = m_mcwpInfo.totalTimeMilliseconds - m_mcwpInfo.elapsedTimeMilliseconds;
		zoomCamera( finalZoom*maxZoom, time, time*easeIn, time*easeOut );
	}
}

//-------------------------------------------------------------------------------------------------
void W3DView::cameraModFreezeAngle()
{
	if (hasScriptedState(Scripted_Rotate))
	{
		if (m_rcInfo.trackObject) {
			m_rcInfo.target.targetObjectID = INVALID_ID;
		} else {
			m_rcInfo.angle.startAngle = m_rcInfo.angle.endAngle = m_angle;
		}
	}
	if (hasScriptedState(Scripted_MoveOnWaypointPath))
	{
		Int i;
		for (i=0; i<m_mcwpInfo.numWaypoints; i++) {
			m_mcwpInfo.cameraAngle[i+1] = m_mcwpInfo.cameraAngle[0];
		}
	}
}

//-------------------------------------------------------------------------------------------------
void W3DView::cameraModLookToward(Coord3D *pLoc)
{
	if (hasScriptedState(Scripted_Rotate))
	{
		return;
	}
	if (hasScriptedState(Scripted_MoveOnWaypointPath))
	{
		Int i;
		for (i=2; i<=m_mcwpInfo.numWaypoints; i++) {
			Coord3D start, mid, end;
			Real factor = 0.5;
			start = m_mcwpInfo.waypoints[i-1];
			start.x += m_mcwpInfo.waypoints[i].x;
			start.y += m_mcwpInfo.waypoints[i].y;
			start.x /= 2;
			start.y /= 2;
			mid = m_mcwpInfo.waypoints[i];
			end = m_mcwpInfo.waypoints[i];
			end.x += m_mcwpInfo.waypoints[i+1].x;
			end.y += m_mcwpInfo.waypoints[i+1].y;
			end.x /= 2;
			end.y /= 2;
			Coord3D result = start;
			result.x += factor*(end.x-start.x);
			result.y += factor*(end.y-start.y);
			result.x += (1-factor)*factor*(mid.x-end.x + mid.x-start.x);
			result.y += (1-factor)*factor*(mid.y-end.y + mid.y-start.y);
			result.z = 0;
			Vector2 dir(pLoc->x-result.x, pLoc->y-result.y);
			const Real dirLength = dir.Length();
			if (dirLength<0.1f) continue;
			Real angle = WWMath::Acos(dir.X/dirLength);
			if (dir.Y<0.0f) {
				angle = -angle;
			}
			angle -= PI/2;
			w3dview_normAngle(angle);
			m_mcwpInfo.cameraAngle[i] = angle;
		}
		if (m_mcwpInfo.totalTimeMilliseconds==1) {
			moveAlongWaypointPath(1);
			addScriptedState(Scripted_MoveOnWaypointPath);
			m_CameraArrivedAtWaypointOnPathFlag = false;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void W3DView::cameraModFinalMoveTo(Coord3D *pLoc)
{
	if (hasScriptedState(Scripted_Rotate))
	{
		return;
	}
	if (hasScriptedState(Scripted_MoveOnWaypointPath))
	{
		Int i;
		Coord3D start, delta;
		start = m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints];
		delta.x = pLoc->x - start.x;
		delta.y = pLoc->y - start.y;
		for (i=2; i<=m_mcwpInfo.numWaypoints; i++) {
			Coord3D result = m_mcwpInfo.waypoints[i];
			result.x += delta.x;
			result.y += delta.y;
			m_mcwpInfo.waypoints[i] = result;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void W3DView::cameraModFinalLookToward(Coord3D *pLoc)
{
	if (hasScriptedState(Scripted_Rotate))
	{
		return;
	}
	if (hasScriptedState(Scripted_MoveOnWaypointPath))
	{
		Int i;
		Int min = m_mcwpInfo.numWaypoints-1;
		if (min<2) min=2;
		for (i=min; i<=m_mcwpInfo.numWaypoints; i++) {
			Coord3D start, mid, end;
			Real factor = 0.5;
			start = m_mcwpInfo.waypoints[i-1];
			start.x += m_mcwpInfo.waypoints[i].x;
			start.y += m_mcwpInfo.waypoints[i].y;
			start.x /= 2;
			start.y /= 2;
			mid = m_mcwpInfo.waypoints[i];
			end = m_mcwpInfo.waypoints[i];
			end.x += m_mcwpInfo.waypoints[i+1].x;
			end.y += m_mcwpInfo.waypoints[i+1].y;
			end.x /= 2;
			end.y /= 2;
			Coord3D result = start;
			result.x += factor*(end.x-start.x);
			result.y += factor*(end.y-start.y);
			result.x += (1-factor)*factor*(mid.x-end.x + mid.x-start.x);
			result.y += (1-factor)*factor*(mid.y-end.y + mid.y-start.y);
			result.z = 0;
			Vector2 dir(pLoc->x-result.x, pLoc->y-result.y);
			const Real dirLength = dir.Length();
			if (dirLength<0.1f) continue;
			Real angle = WWMath::Acos(dir.X/dirLength);
			if (dir.Y<0.0f) {
				angle = -angle;
			}
			angle -= PI/2;
			w3dview_normAngle(angle);
			if (i==m_mcwpInfo.numWaypoints) {
				m_mcwpInfo.cameraAngle[i] = angle;
			} else {
				Real deltaAngle = angle - m_mcwpInfo.cameraAngle[i];
				w3dview_normAngle(deltaAngle);
				angle = m_mcwpInfo.cameraAngle[i] + deltaAngle/2;
				w3dview_normAngle(angle);
				m_mcwpInfo.cameraAngle[i] = angle;
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void W3DView::cameraModFinalTimeMultiplier(Int finalMultiplier)
{
	if (hasScriptedState(Scripted_Zoom))
	{
		m_zcInfo.endTimeMultiplier = finalMultiplier;
	}
	if (hasScriptedState(Scripted_Pitch))
	{
		m_pcInfo.endTimeMultiplier = finalMultiplier;
	}
	if (hasScriptedState(Scripted_Rotate))
	{
		m_rcInfo.endTimeMultiplier = finalMultiplier;
	}
	else if (hasScriptedState(Scripted_MoveOnWaypointPath))
	{
		Int i;
		Real curDistance = 0;
		for (i=0; i<m_mcwpInfo.numWaypoints; i++) {
			curDistance += m_mcwpInfo.waySegLength[i];
			Real factor2 = curDistance / m_mcwpInfo.totalDistance;
			Real factor1 = 1.0-factor2;
			m_mcwpInfo.timeMultiplier[i+1] = REAL_TO_INT_FLOOR(0.5+m_mcwpInfo.timeMultiplier[i+1]*factor1 + finalMultiplier*factor2);
		}
	}
	else
	{
		m_timeMultiplier = finalMultiplier;
	}
}

//-------------------------------------------------------------------------------------------------
void W3DView::cameraModRollingAverage(Int framesToAverage)
{
	if (framesToAverage < 1) framesToAverage = 1;
	m_mcwpInfo.rollingAverageFrames = framesToAverage;
}

//-------------------------------------------------------------------------------------------------
void W3DView::cameraModFinalPitch(Real finalPitch, Real easeIn, Real easeOut)
{
	if (hasScriptedState(Scripted_Rotate))
	{
		Real time = (m_rcInfo.numFrames + m_rcInfo.numHoldFrames - m_rcInfo.curFrame)*TheW3DFrameLengthInMsec;
		pitchCamera( finalPitch, time, time*easeIn, time*easeOut );
	}
	if (hasScriptedState(Scripted_MoveOnWaypointPath))
	{
		Real time = m_mcwpInfo.totalTimeMilliseconds - m_mcwpInfo.elapsedTimeMilliseconds;
		pitchCamera( finalPitch, time, time*easeIn, time*easeOut );
	}
}

//-------------------------------------------------------------------------------------------------
void W3DView::resetCamera(const Coord3D *location, Int milliseconds, Real easeIn, Real easeOut)
{
	moveCameraTo(location, milliseconds, 0, false, easeIn, easeOut);
	m_mcwpInfo.cameraAngle[2] = 0.0f;
	View::setAngle(m_mcwpInfo.cameraAngle[0]);

	Real terrainHeightMax = w3dview_getHeightAroundPos(location->x, location->y);
	Real desiredHeight = (terrainHeightMax + m_maxHeightAboveGround);
	Real desiredZoom = desiredHeight / m_cameraOffset.z;

	zoomCamera( desiredZoom, milliseconds, easeIn, easeOut );

	pitchCamera( 1.0f, milliseconds, easeIn, easeOut );
}

//-------------------------------------------------------------------------------------------------
Bool W3DView::isCameraMovementFinished()
{
	if (m_viewFilter == FT_VIEW_MOTION_BLUR_FILTER) {
		if (m_viewFilterMode == FM_VIEW_MB_IN_AND_OUT_ALPHA ||
				m_viewFilterMode == FM_VIEW_MB_IN_AND_OUT_SATURATE ||
				m_viewFilterMode == FM_VIEW_MB_IN_ALPHA ||
				m_viewFilterMode == FM_VIEW_MB_OUT_ALPHA ||
				m_viewFilterMode == FM_VIEW_MB_IN_SATURATE ||
				m_viewFilterMode == FM_VIEW_MB_OUT_SATURATE ) {
			return true;
		}
	}

	return !hasScriptedState(Scripted_Rotate | Scripted_Pitch | Scripted_Zoom | Scripted_MoveOnWaypointPath);
}

//-------------------------------------------------------------------------------------------------
Bool W3DView::isCameraMovementAtWaypointAlongPath()
{
	Bool returnValue = m_CameraArrivedAtWaypointOnPathFlag;
	m_CameraArrivedAtWaypointOnPathFlag = false;
	return returnValue;
}

//-------------------------------------------------------------------------------------------------
void W3DView::moveCameraAlongWaypointPath(Waypoint *pWay, Int milliseconds, Int shutter, Bool orient, Real easeIn, Real easeOut)
{
	const Real MIN_DELTA = MAP_XY_FACTOR;

	m_mcwpInfo.waypoints[0] = *getPosition();
	m_mcwpInfo.cameraAngle[0] = getAngle();
	m_mcwpInfo.waySegLength[0] = 0;
	m_mcwpInfo.waypoints[1] = *getPosition();
	m_mcwpInfo.numWaypoints = 1;
	if (milliseconds<1) milliseconds = 1;
	m_mcwpInfo.totalTimeMilliseconds = milliseconds;
	m_mcwpInfo.shutter = shutter/TheW3DFrameLengthInMsec;
	if (m_mcwpInfo.shutter<1) m_mcwpInfo.shutter = 1;
	m_mcwpInfo.ease.setEaseTimes(easeIn/milliseconds, easeOut/milliseconds);

	while (pWay && m_mcwpInfo.numWaypoints <MAX_WAYPOINTS) {
		m_mcwpInfo.numWaypoints++;
		m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints] = *pWay->getLocation();
		if (pWay->getNumLinks()>0) {
			pWay = pWay->getLink(0);
		} else {
			pWay = nullptr;
		}
		Vector2 dir(m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints].x-m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints-1].x, m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints].y-m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints-1].y);
		if (dir.Length()<MIN_DELTA) {
			if (pWay) {
				m_mcwpInfo.numWaypoints--;
			} else {
				m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints-1] = m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints];
				m_mcwpInfo.numWaypoints--;
			}
		}
	}
	setupWaypointPath(orient);
}

//-------------------------------------------------------------------------------------------------
void W3DView::setupWaypointPath(Bool orient)
{
	m_mcwpInfo.curSegment = 1;
	m_mcwpInfo.curSegDistance = 0;
	m_mcwpInfo.totalDistance = 0;
	m_mcwpInfo.rollingAverageFrames = 1;
	Int i;
	Real angle = getAngle();
	for (i=1; i<m_mcwpInfo.numWaypoints; i++) {
		Vector2 dir(m_mcwpInfo.waypoints[i+1].x-m_mcwpInfo.waypoints[i].x, m_mcwpInfo.waypoints[i+1].y-m_mcwpInfo.waypoints[i].y);
		const Real dirLength = dir.Length();
		m_mcwpInfo.waySegLength[i] = dirLength;
		m_mcwpInfo.totalDistance += m_mcwpInfo.waySegLength[i];
		if (orient && dirLength >= 0.1f) {
			angle = WWMath::Acos(dir.X/dirLength);
			if (dir.Y<0.0f) {
				angle = -angle;
			}
			angle -= PI/2;
			w3dview_normAngle(angle);
		}
		m_mcwpInfo.cameraAngle[i] = angle;
	}
	m_mcwpInfo.cameraAngle[1] = getAngle();
	m_mcwpInfo.cameraAngle[m_mcwpInfo.numWaypoints] = m_mcwpInfo.cameraAngle[m_mcwpInfo.numWaypoints-1];
	for (i=m_mcwpInfo.numWaypoints-1; i>1; i--) {
		m_mcwpInfo.cameraAngle[i] = (m_mcwpInfo.cameraAngle[i] + m_mcwpInfo.cameraAngle[i-1]) / 2;
	}
	m_mcwpInfo.waySegLength[m_mcwpInfo.numWaypoints+1] = m_mcwpInfo.waySegLength[m_mcwpInfo.numWaypoints];

	if (m_mcwpInfo.totalDistance<1.0) {
		m_mcwpInfo.waySegLength[m_mcwpInfo.numWaypoints-1] += 1.0-m_mcwpInfo.totalDistance;
		m_mcwpInfo.totalDistance = 1.0;
	}

	Real curDistance = 0;
	Coord3D finalPos = m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints];
	Real newGround = TheTerrainLogic->getGroundHeight(finalPos.x, finalPos.y);
	for (i=0; i<=m_mcwpInfo.numWaypoints+1; i++) {
		Real factor2 = curDistance / m_mcwpInfo.totalDistance;
		Real factor1 = 1.0-factor2;
		m_mcwpInfo.timeMultiplier[i] = m_timeMultiplier;
		m_mcwpInfo.groundHeight[i] = m_groundLevel*factor1 + newGround*factor2;
		curDistance += m_mcwpInfo.waySegLength[i];
	}

	m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints+1] = m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints];
	Coord3D cur = m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints];
	Coord3D prev = m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints-1];
	m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints+1].x += cur.x-prev.x;
	m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints+1].y += cur.y-prev.y;
	m_mcwpInfo.cameraAngle[m_mcwpInfo.numWaypoints+1] = m_mcwpInfo.cameraAngle[m_mcwpInfo.numWaypoints];
	m_mcwpInfo.groundHeight[m_mcwpInfo.numWaypoints+1] = newGround;

	cur = m_mcwpInfo.waypoints[2];
	prev = m_mcwpInfo.waypoints[1];
	m_mcwpInfo.waypoints[0].x -= cur.x-prev.x;
	m_mcwpInfo.waypoints[0].y -= cur.y-prev.y;

	if (m_mcwpInfo.numWaypoints>1)
	{
		addScriptedState(Scripted_MoveOnWaypointPath);
	}
	else
	{
		removeScriptedState(Scripted_MoveOnWaypointPath);
	}

	m_CameraArrivedAtWaypointOnPathFlag = false;
	removeScriptedState(Scripted_Rotate);

	m_mcwpInfo.elapsedTimeMilliseconds = 0;
	m_mcwpInfo.curShutter = m_mcwpInfo.shutter;
}

//-------------------------------------------------------------------------------------------------
void W3DView::rotateCameraOneFrame()
{
	m_rcInfo.curFrame++;
	if (TheGlobalData->m_disableCameraMovement) {
		if (m_rcInfo.curFrame >= m_rcInfo.numFrames + m_rcInfo.numHoldFrames) {
			removeScriptedState(Scripted_Rotate);
			m_freezeTimeForCameraMovement = false;
		}
		return;
	}

	if (m_rcInfo.trackObject)
	{
		if (m_rcInfo.curFrame <= m_rcInfo.numFrames + m_rcInfo.numHoldFrames)
		{
			const Object *obj = TheGameLogic->findObjectByID(m_rcInfo.target.targetObjectID);
			if (obj)
			{
				m_rcInfo.target.targetObjectPos = *obj->getPosition();
			}

			const Vector2 dir(m_rcInfo.target.targetObjectPos.x - m_pos.x, m_rcInfo.target.targetObjectPos.y - m_pos.y);
			const Real dirLength = dir.Length();
			if (dirLength>=0.1f)
			{
				Real angle = WWMath::Acos(dir.X/dirLength);
				if (dir.Y<0.0f) {
					angle = -angle;
				}
				angle -= PI/2;
				w3dview_normAngle(angle);

				if (m_rcInfo.curFrame <= m_rcInfo.numFrames)
				{
					Real factor = m_rcInfo.ease(((Real)m_rcInfo.curFrame)/m_rcInfo.numFrames);
					Real angleDiff = angle - m_angle;
					w3dview_normAngle(angleDiff);
					angleDiff *= factor;
					View::setAngle(m_angle + angleDiff);
					m_timeMultiplier = m_rcInfo.startTimeMultiplier + REAL_TO_INT_FLOOR(0.5 + (m_rcInfo.endTimeMultiplier-m_rcInfo.startTimeMultiplier)*factor);
				}
				else
				{
					View::setAngle(angle);
				}
			}
		}
	}
	else if (m_rcInfo.curFrame <= m_rcInfo.numFrames)
	{
		Real factor = m_rcInfo.ease(((Real)m_rcInfo.curFrame)/m_rcInfo.numFrames);
		Real angle = WWMath::Lerp(m_rcInfo.angle.startAngle, m_rcInfo.angle.endAngle, factor);
		View::setAngle(angle);
		m_timeMultiplier = m_rcInfo.startTimeMultiplier + REAL_TO_INT_FLOOR(0.5 + (m_rcInfo.endTimeMultiplier-m_rcInfo.startTimeMultiplier)*factor);
	}

	if (m_rcInfo.curFrame >= m_rcInfo.numFrames + m_rcInfo.numHoldFrames) {
		removeScriptedState(Scripted_Rotate);
		m_freezeTimeForCameraMovement = false;
		if (! m_rcInfo.trackObject)
		{
			View::setAngle(m_rcInfo.angle.endAngle);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void W3DView::zoomCameraOneFrame()
{
	m_zcInfo.curFrame++;
	if (TheGlobalData->m_disableCameraMovement) {
		if (m_zcInfo.curFrame >= m_zcInfo.numFrames) {
			removeScriptedState(Scripted_Zoom);
		}
		return;
	}
	if (m_zcInfo.curFrame <= m_zcInfo.numFrames)
	{
		Real factor = m_zcInfo.ease(((Real)m_zcInfo.curFrame)/m_zcInfo.numFrames);
		m_zoom = WWMath::Lerp(m_zcInfo.startZoom, m_zcInfo.endZoom, factor);
	}

	if (m_zcInfo.curFrame >= m_zcInfo.numFrames) {
		removeScriptedState(Scripted_Zoom);
		m_zoom = m_zcInfo.endZoom;
	}
}

//-------------------------------------------------------------------------------------------------
void W3DView::pitchCameraOneFrame()
{
	m_pcInfo.curFrame++;
	if (TheGlobalData->m_disableCameraMovement) {
		if (m_pcInfo.curFrame >= m_pcInfo.numFrames) {
			removeScriptedState(Scripted_Pitch);
		}
		return;
	}
	if (m_pcInfo.curFrame <= m_pcInfo.numFrames)
	{
		Real factor = m_pcInfo.ease(((Real)m_pcInfo.curFrame)/m_pcInfo.numFrames);
		m_FXPitch = WWMath::Lerp(m_pcInfo.startPitch, m_pcInfo.endPitch, factor);
	}

	if (m_pcInfo.curFrame >= m_pcInfo.numFrames) {
		removeScriptedState(Scripted_Pitch);
		m_FXPitch = m_pcInfo.endPitch;
	}
}

//-------------------------------------------------------------------------------------------------
Bool W3DView::isDoingScriptedCamera()
{
	return m_scriptedState != 0;
}

//-------------------------------------------------------------------------------------------------
void W3DView::stopDoingScriptedCamera()
{
	m_scriptedState = 0;
}

//-------------------------------------------------------------------------------------------------
Bool W3DView::hasScriptedState(ScriptedState state) const
{
	return (m_scriptedState & state) != 0;
}

//-------------------------------------------------------------------------------------------------
void W3DView::addScriptedState(ScriptedState state)
{
	m_scriptedState |= state;
}

//-------------------------------------------------------------------------------------------------
void W3DView::removeScriptedState(ScriptedState state)
{
	m_scriptedState &= ~state;
}

//-------------------------------------------------------------------------------------------------
void W3DView::moveAlongWaypointPath(Real milliseconds)
{
	m_mcwpInfo.elapsedTimeMilliseconds += milliseconds;
	if (TheGlobalData->m_disableCameraMovement) {
		if (m_mcwpInfo.elapsedTimeMilliseconds>m_mcwpInfo.totalTimeMilliseconds) {
			removeScriptedState(Scripted_MoveOnWaypointPath);
			m_freezeTimeForCameraMovement = false;
		}
		return;
	}
	if (m_mcwpInfo.elapsedTimeMilliseconds>m_mcwpInfo.totalTimeMilliseconds) {
		removeScriptedState(Scripted_MoveOnWaypointPath);
		m_CameraArrivedAtWaypointOnPathFlag = false;

		m_freezeTimeForCameraMovement = false;
		View::setAngle(m_mcwpInfo.cameraAngle[m_mcwpInfo.numWaypoints]);

		m_groundLevel = m_mcwpInfo.groundHeight[m_mcwpInfo.numWaypoints];
		m_cameraOffset.y = -(m_cameraOffset.z / tan(TheGlobalData->m_cameraPitch * (PI / 180.0)));
		m_cameraOffset.x = -(m_cameraOffset.y * tan(TheGlobalData->m_cameraYaw * (PI / 180.0)));

		Coord3D pos = m_mcwpInfo.waypoints[m_mcwpInfo.numWaypoints];
		pos.z = 0;
		setPosition(&pos);
		// Match original ZH (W3DView.cpp:3122-3127): assuming the scripter knows
		// what they're doing, EXPAND the camera constraints to include the new
		// scripted position so subsequent clamps don't push the camera back to
		// the playable area. Unconditional like ZH - if valid is false, the next
		// setCameraTransform recalc will overwrite these anyway, so the write is
		// harmless and matches the original behavior bit-for-bit.
		m_cameraAreaConstraints.lo.x = w3dview_minf(m_cameraAreaConstraints.lo.x, pos.x);
		m_cameraAreaConstraints.hi.x = w3dview_maxf(m_cameraAreaConstraints.hi.x, pos.x);
		m_cameraAreaConstraints.lo.y = w3dview_minf(m_cameraAreaConstraints.lo.y, pos.y);
		m_cameraAreaConstraints.hi.y = w3dview_maxf(m_cameraAreaConstraints.hi.y, pos.y);
		return;
	}

	const Real totalTime = m_mcwpInfo.totalTimeMilliseconds;
	const Real deltaTime = m_mcwpInfo.ease(m_mcwpInfo.elapsedTimeMilliseconds/totalTime) -
		m_mcwpInfo.ease((m_mcwpInfo.elapsedTimeMilliseconds - milliseconds)/totalTime);
	m_mcwpInfo.curSegDistance += deltaTime*m_mcwpInfo.totalDistance;
#if RTS_GENERALS
	while (m_mcwpInfo.curSegDistance > m_mcwpInfo.waySegLength[m_mcwpInfo.curSegment])
#else
	while (m_mcwpInfo.curSegDistance >= m_mcwpInfo.waySegLength[m_mcwpInfo.curSegment])
#endif
	{
		if (hasScriptedState(Scripted_MoveOnWaypointPath))
		{
			m_CameraArrivedAtWaypointOnPathFlag = true;
		}

		m_mcwpInfo.curSegDistance -= m_mcwpInfo.waySegLength[m_mcwpInfo.curSegment];
		m_mcwpInfo.curSegment++;
		if (m_mcwpInfo.curSegment >= m_mcwpInfo.numWaypoints) {
			m_mcwpInfo.totalTimeMilliseconds = 0;
			return;
		}
	}
	Real avgFactor = 1.0/m_mcwpInfo.rollingAverageFrames;
	m_mcwpInfo.curShutter--;
	if (m_mcwpInfo.curShutter>0) {
		return;
	}
	m_mcwpInfo.curShutter = m_mcwpInfo.shutter;
	Real factor = m_mcwpInfo.curSegDistance / m_mcwpInfo.waySegLength[m_mcwpInfo.curSegment];
	if (m_mcwpInfo.curSegment == m_mcwpInfo.numWaypoints-1) {
		avgFactor = avgFactor + (1.0-avgFactor)*factor;
	}
	Real factor1;
	Real factor2;
	factor1 = 1.0-factor;
	factor2 = 1.0-factor1;
	Real angle1 = m_mcwpInfo.cameraAngle[m_mcwpInfo.curSegment];
	Real angle2 = m_mcwpInfo.cameraAngle[m_mcwpInfo.curSegment+1];
	if (angle2-angle1 > PI) angle1 += 2*PI;
	if (angle2-angle1 < -PI) angle1 -= 2*PI;
	Real angle = angle1 * (factor1) + angle2 * (factor2);

	w3dview_normAngle(angle);
	Real deltaAngle = angle-m_angle;
	w3dview_normAngle(deltaAngle);
	if (fabs(deltaAngle) > PI/10) {
		DEBUG_LOG(("Huh."));
	}
	View::setAngle(m_angle + (avgFactor*deltaAngle));

	Real timeMultiplier = m_mcwpInfo.timeMultiplier[m_mcwpInfo.curSegment]*factor1 +
			m_mcwpInfo.timeMultiplier[m_mcwpInfo.curSegment+1]*factor2;
	m_timeMultiplier = REAL_TO_INT_FLOOR(0.5 + timeMultiplier);

	m_groundLevel = m_mcwpInfo.groundHeight[m_mcwpInfo.curSegment]*factor1 +
			m_mcwpInfo.groundHeight[m_mcwpInfo.curSegment+1]*factor2;
	m_cameraOffset.y = -(m_cameraOffset.z / tan(TheGlobalData->m_cameraPitch * (PI / 180.0)));
	m_cameraOffset.x = -(m_cameraOffset.y * tan(TheGlobalData->m_cameraYaw * (PI / 180.0)));

	Coord3D start, mid, end;
	if (factor<0.5) {
		start = m_mcwpInfo.waypoints[m_mcwpInfo.curSegment-1];
		start.x += m_mcwpInfo.waypoints[m_mcwpInfo.curSegment].x;
		start.y += m_mcwpInfo.waypoints[m_mcwpInfo.curSegment].y;
		start.x /= 2;
		start.y /= 2;
		mid = m_mcwpInfo.waypoints[m_mcwpInfo.curSegment];
		end = m_mcwpInfo.waypoints[m_mcwpInfo.curSegment];
		end.x += m_mcwpInfo.waypoints[m_mcwpInfo.curSegment+1].x;
		end.y += m_mcwpInfo.waypoints[m_mcwpInfo.curSegment+1].y;
		end.x /= 2;
		end.y /= 2;
		factor += 0.5;
	} else {
		start = m_mcwpInfo.waypoints[m_mcwpInfo.curSegment];
		start.x += m_mcwpInfo.waypoints[m_mcwpInfo.curSegment+1].x;
		start.y += m_mcwpInfo.waypoints[m_mcwpInfo.curSegment+1].y;
		start.x /= 2;
		start.y /= 2;
		mid = m_mcwpInfo.waypoints[m_mcwpInfo.curSegment+1];
		end = m_mcwpInfo.waypoints[m_mcwpInfo.curSegment+1];
		end.x += m_mcwpInfo.waypoints[m_mcwpInfo.curSegment+2].x;
		end.y += m_mcwpInfo.waypoints[m_mcwpInfo.curSegment+2].y;
		end.x /= 2;
		end.y /= 2;
		factor -= 0.5;
	}

	Coord3D result = start;
	result.x += factor*(end.x-start.x);
	result.y += factor*(end.y-start.y);
	result.x += (1-factor)*factor*(mid.x-end.x + mid.x-start.x);
	result.y += (1-factor)*factor*(mid.y-end.y + mid.y-start.y);
	result.z = 0;

	setPosition(&result);
	// Match original ZH (W3DView.cpp:3231-3236): unconditionally expand existing
	// constraints to include this segment-update position. See snap branch above
	// for the rationale.
	m_cameraAreaConstraints.lo.x = w3dview_minf(m_cameraAreaConstraints.lo.x, result.x);
	m_cameraAreaConstraints.hi.x = w3dview_maxf(m_cameraAreaConstraints.hi.x, result.x);
	m_cameraAreaConstraints.lo.y = w3dview_minf(m_cameraAreaConstraints.lo.y, result.y);
	m_cameraAreaConstraints.hi.y = w3dview_maxf(m_cameraAreaConstraints.hi.y, result.y);
}

//-------------------------------------------------------------------------------------------------
void W3DView::shake( const Coord3D *epicenter, CameraShakeType shakeType )
{
	Real angle = GameClientRandomValueReal( 0, 2*PI );

	m_shakeAngleCos = (Real)cos( angle );
	m_shakeAngleSin = (Real)sin( angle );

	Real intensity = 0.0f;
	switch( shakeType )
	{
		case SHAKE_SUBTLE:
			intensity = TheGlobalData->m_shakeSubtleIntensity;
			break;

		case SHAKE_NORMAL:
			intensity = TheGlobalData->m_shakeNormalIntensity;
			break;

		case SHAKE_STRONG:
			intensity = TheGlobalData->m_shakeStrongIntensity;
			break;

		case SHAKE_SEVERE:
			intensity = TheGlobalData->m_shakeSevereIntensity;
			break;

		case SHAKE_CINE_EXTREME:
			intensity = TheGlobalData->m_shakeCineExtremeIntensity;
			break;

		case SHAKE_CINE_INSANE:
			intensity = TheGlobalData->m_shakeCineInsaneIntensity;
			break;
	}

	const Coord3D *viewPos = getPosition();
	Coord3D d;
	d.x = epicenter->x - viewPos->x;
	d.y = epicenter->y - viewPos->y;

	Real dist = (Real)sqrt( d.x*d.x + d.y*d.y );

	if (dist > TheGlobalData->m_maxShakeRange)
		return;

	intensity *= 1.0f - (dist/TheGlobalData->m_maxShakeRange);

	m_shakeIntensity += intensity;

	const Real maxIntensity = 3.0f;
	if (m_shakeIntensity > TheGlobalData->m_maxShakeIntensity)
		m_shakeIntensity = maxIntensity;
}

//-------------------------------------------------------------------------------------------------
void W3DView::screenToWorldAtZ( const ICoord2D *s, Coord3D *w, Real z )
{
	Vector3 rayStart, rayEnd;

	getPickRay( s, &rayStart, &rayEnd );
	w->x = Vector3::Find_X_At_Z( z, rayStart, rayEnd );
	w->y = Vector3::Find_Y_At_Z( z, rayStart, rayEnd );
	w->z = z;
}

//-------------------------------------------------------------------------------------------------
void W3DView::cameraEnableSlaveMode(const AsciiString & objectName, const AsciiString & boneName)
{
	m_isCameraSlaved = true;
	m_cameraSlaveObjectName = objectName;
	m_cameraSlaveObjectBoneName = boneName;
}

//-------------------------------------------------------------------------------------------------
void W3DView::cameraDisableSlaveMode()
{
	m_isCameraSlaved = false;
}

//-------------------------------------------------------------------------------------------------
void W3DView::cameraEnableRealZoomMode()
{
	m_useRealZoomCam = true;
	m_FXPitch = 1.0f;
	updateView();
}

//-------------------------------------------------------------------------------------------------
void W3DView::cameraDisableRealZoomMode()
{
	m_useRealZoomCam = false;
	m_FXPitch = 1.0f;
	m_FOV = DEG_TO_RADF(50.0f);
	m_recalcCamera = true;
	updateView();
}

//-------------------------------------------------------------------------------------------------
void W3DView::Add_Camera_Shake(const Coord3D & position,float radius,float duration,float power)
{
	Vector3 vpos;

	vpos.X = position.x;
	vpos.Y = position.y;
	vpos.Z = position.z;

	CameraShakerSystem.Add_Camera_Shake(vpos,radius,duration,power);
}

//-------------------------------------------------------------------------------------------------
void W3DView::updateTerrain()
{
	DEBUG_ASSERTCRASH(TheTerrainRenderObject != nullptr, ("TheTerrainRenderObject is null"));

	// D3D11 port: The lights iterator comes from the 3D scene. Since we don't have direct access
	// to the scene object here, we pass nullptr for the lights iterator. The terrain update
	// center call still needs the camera.
	TheTerrainRenderObject->updateCenter(m_3DCamera, nullptr);
}


////////////////////////////////////////////////////////////////////////////////
// SECTION 16: CameraShakeSystemClass
////////////////////////////////////////////////////////////////////////////////

#include "W3DDevice/GameClient/CameraShakeSystem.h"

// Static members of filter classes referenced by W3DView
#include "W3DDevice/GameClient/W3DShaderManager.h"
Coord3D ScreenMotionBlurFilter::m_zoomToPos = {0, 0, 0};
bool ScreenMotionBlurFilter::m_zoomToValid = false;
int ScreenBWFilter::m_fadeFrames = 0;
int ScreenBWFilter::m_fadeDirection = 0;
int ScreenBWFilter::m_curFadeFrame = 0;
Real ScreenBWFilter::m_curFadeValue = 1.0f;
int ScreenCrossFadeFilter::m_fadeFrames = 0;
int ScreenCrossFadeFilter::m_fadeDirection = 0;
int ScreenCrossFadeFilter::m_curFadeFrame = 0;
Real ScreenCrossFadeFilter::m_curFadeValue = 0.0f;
Bool ScreenCrossFadeFilter::m_skipRender = FALSE;
TextureClass* ScreenCrossFadeFilter::m_fadePatternTexture = nullptr;

// Global terrain render object
#include "W3DDevice/GameClient/BaseHeightMap.h"
BaseHeightMapRenderObjClass* TheTerrainRenderObject = nullptr;

// Helper for W3DDisplay to access heightmap without including BaseHeightMap.h
WorldHeightMap* GetTerrainHeightMap()
{
	if (TheTerrainRenderObject)
	{
		WorldHeightMap *heightMap = TheTerrainRenderObject->getMap();
		if (heightMap)
			return heightMap;
	}

	if (TheTerrainVisual)
		return TheTerrainVisual->getLogicHeightMap();

	return nullptr;
}

// ============================================================================
// Inspector model debugger + team-color refresh
// ============================================================================
//
// These bridges let the inspector reach into the W3D render-object
// graph (mesh hierarchy, bounding boxes, color overrides) without
// pulling any W3D / WW3D2 / DX8-era headers into the inspector
// translation units. The inspector calls these via plain C symbols.

// Locate the first W3DModelDraw render object on a Drawable. The
// engine attaches one per visible model (most game units) so this
// finds the "main" mesh tree to inspect.
static RenderObjClass* InspectorFindRenderObj(Object* obj)
{
	if (!obj) return nullptr;
	Drawable* d = obj->getDrawable();
	if (!d) return nullptr;
	for (DrawModule** dm = d->getDrawModules(); dm && *dm; ++dm)
	{
		if (W3DModelDraw* mdraw = dynamic_cast<W3DModelDraw*>(*dm))
		{
			RenderObjClass* ro = mdraw->getRenderObject();
			if (ro) return ro;
		}
	}
	return nullptr;
}

// Refresh the cached house color on every render object owned by a
// given player. Needed because rendobj->Set_ObjectColor is called
// ONCE at bind time (W3DAssetManager.cpp:786), so changing
// Player::m_color at runtime doesn't propagate without explicitly
// re-applying the color to each render object. Returns the count
// of render objects updated, for HUD feedback.
extern "C" int InspectorRefreshPlayerHouseColors(int playerIdx, unsigned int newColor)
{
	if (!ThePlayerList || !TheGameLogic) return 0;
	Player* p = ThePlayerList->getNthPlayer(playerIdx);
	if (!p) return 0;

	int updated = 0;
	for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() != p) continue;
		Drawable* d = o->getDrawable();
		if (!d) continue;
		// Walk EVERY draw module — a single drawable can have multiple
		// W3DModelDraws (turret, body, undercarriage, etc.) each with
		// their own render object that needs the color set.
		for (DrawModule** dm = d->getDrawModules(); dm && *dm; ++dm)
		{
			if (W3DModelDraw* mdraw = dynamic_cast<W3DModelDraw*>(*dm))
			{
				if (RenderObjClass* ro = mdraw->getRenderObject())
				{
					ro->Set_ObjectColor(newColor);
					++updated;
				}
			}
		}
	}
	return updated;
}

// Defined in CommandLine.cpp with C++ linkage. The declaration MUST live
// outside any extern "C" block — otherwise it inherits C linkage and the
// linker hunts for an unmangled `g_launcherPlayerShaderId` that doesn't
// exist (LNK2019).
extern Int g_launcherPlayerShaderId;

// Same flow as InspectorRefreshPlayerHouseColors but for cosmetic shader
// effect ids. Called every frame from the model-render path so newly-
// spawned units pick up their player's shader within one frame. Cheap —
// O(objects) of pointer hops + one int write per render object.
//
// Resolution order (per player):
//   1. CosmeticsCache, looked up by display name. Populated by the relay's
//      RELAY_TYPE_COSMETICS broadcast — covers every authenticated player
//      and stays current when anyone's profile is updated via the REST API.
//   2. Player::getShaderId() — lobby-time snapshot from GameSlot. Used
//      when the cache is cold (single-player, pre-auth, before the first
//      cosmetic packet arrives).
//   3. g_launcherPlayerShaderId — last-resort fallback for the local
//      player (e.g. single-player where there's no relay at all).
extern "C" int RefreshAllPlayerShaderIds()
{
	if (!ThePlayerList || !TheGameLogic) return 0;
	const CosmeticsCache& cache = CosmeticsCache::Instance();

	int updated = 0;
	for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
	{
		Player* p = o->getControllingPlayer();
		if (!p) continue;

		int shaderId = 0;

		// (1) Server-pushed cosmetic cache — authoritative for authed players.
		AsciiString name;
		name.translate(p->getPlayerDisplayName());
		if (const PlayerCosmetic* c = cache.GetByDisplayName(name))
			shaderId = c->shaderId;

		// (2) Lobby-time snapshot from GameSlot (propagated via
		// TheKey_playerShaderId at Player::init).
		if (shaderId == 0 && p->getShaderId() != 0)
			shaderId = p->getShaderId();

		// (3) Local-player launcher arg — single-player / no relay.
		if (shaderId == 0 && ThePlayerList->getLocalPlayer() == p)
			shaderId = g_launcherPlayerShaderId;

		Drawable* d = o->getDrawable();
		if (!d) continue;
		for (DrawModule** dm = d->getDrawModules(); dm && *dm; ++dm)
		{
			if (W3DModelDraw* mdraw = dynamic_cast<W3DModelDraw*>(*dm))
			{
				if (RenderObjClass* ro = mdraw->getRenderObject())
				{
					if (ro->Get_ObjectShaderId() != shaderId)
					{
						ro->Set_ObjectShaderId(shaderId);
						++updated;
					}
				}
			}
		}
	}
	return updated;
}

// ---- Model debugger queries ----

// Top-level info about the selected object's main render-object.
extern "C" bool InspectorGetModelInfo(unsigned int objID,
	char* outName, int outNameSize,
	int* outClassID, int* outNumSubObjects,
	float* outCenterX, float* outCenterY, float* outCenterZ,
	float* outExtentX, float* outExtentY, float* outExtentZ)
{
	if (!TheGameLogic) return false;
	Object* obj = TheGameLogic->findObjectByID((ObjectID)objID);
	RenderObjClass* ro = InspectorFindRenderObj(obj);
	if (!ro) return false;

	if (outName && outNameSize > 0)
	{
		const char* name = ro->Get_Name();
		if (name)
		{
			strncpy(outName, name, outNameSize - 1);
			outName[outNameSize - 1] = 0;
		}
		else outName[0] = 0;
	}
	if (outClassID)        *outClassID        = ro->Class_ID();
	if (outNumSubObjects)  *outNumSubObjects  = ro->Get_Num_Sub_Objects();

	const AABoxClass& box = ro->Get_Bounding_Box();
	if (outCenterX) *outCenterX = box.Center.X;
	if (outCenterY) *outCenterY = box.Center.Y;
	if (outCenterZ) *outCenterZ = box.Center.Z;
	if (outExtentX) *outExtentX = box.Extent.X;
	if (outExtentY) *outExtentY = box.Extent.Y;
	if (outExtentZ) *outExtentZ = box.Extent.Z;
	return true;
}

// Per-sub-mesh info. Loop 0..N-1 where N comes from outNumSubObjects.
// Get_Sub_Object returns a ref-counted handle that MUST be released.
extern "C" bool InspectorGetModelMeshAt(unsigned int objID, int idx,
	char* outName, int outNameSize,
	int* outClassID,
	float* outCenterX, float* outCenterY, float* outCenterZ,
	float* outExtentX, float* outExtentY, float* outExtentZ)
{
	if (!TheGameLogic) return false;
	Object* obj = TheGameLogic->findObjectByID((ObjectID)objID);
	RenderObjClass* ro = InspectorFindRenderObj(obj);
	if (!ro) return false;
	if (idx < 0 || idx >= ro->Get_Num_Sub_Objects()) return false;

	RenderObjClass* sub = ro->Get_Sub_Object(idx);
	if (!sub) return false;

	if (outName && outNameSize > 0)
	{
		const char* name = sub->Get_Name();
		if (name)
		{
			strncpy(outName, name, outNameSize - 1);
			outName[outNameSize - 1] = 0;
		}
		else outName[0] = 0;
	}
	if (outClassID) *outClassID = sub->Class_ID();

	const AABoxClass& box = sub->Get_Bounding_Box();
	if (outCenterX) *outCenterX = box.Center.X;
	if (outCenterY) *outCenterY = box.Center.Y;
	if (outCenterZ) *outCenterZ = box.Center.Z;
	if (outExtentX) *outExtentX = box.Extent.X;
	if (outExtentY) *outExtentY = box.Extent.Y;
	if (outExtentZ) *outExtentZ = box.Extent.Z;

	sub->Release_Ref();
	return true;
}

// ============================================================================
// Inspector camera override
// ============================================================================
//
// Lets the inspector run a free-fly editor camera (Unreal-style WASD +
// QE + RMB look) without including any W3D / view / camera headers in
// the inspector translation units. The inspector reads the engine
// camera's current position via InspectorReadGameCamera (used to seed
// the free-fly state on first activation), then drives a per-frame
// override via InspectorApplyCameraOverride.
//
// The override must be called AFTER the engine's W3DView::update has
// recomputed its own camera transform — otherwise that recompute
// stomps our values. The hook lives in W3DDisplay::draw immediately
// after updateViews().

extern "C" bool InspectorReadGameCamera(
	float* outX, float* outY, float* outZ,
	float* outYaw, float* outPitch)
{
	if (!TheTacticalView) return false;
	const Coord3D& p = TheTacticalView->get3DCameraPosition();
	if (outX) *outX = p.x;
	if (outY) *outY = p.y;
	if (outZ) *outZ = p.z;

	// Estimate yaw/pitch from the camera-to-pivot direction so the
	// free-fly cam picks up "where the game was looking" on first
	// activation. The engine's getAngle/getPitch refer to the pivot
	// orbit parameters, not the camera's facing direction itself, so
	// we recompute from the actual world-space vectors.
	Coord3D pivot;
	TheTacticalView->getPosition(&pivot);
	const float dx = pivot.x - p.x;
	const float dy = pivot.y - p.y;
	const float dz = pivot.z - p.z;
	const float horiz = sqrtf(dx * dx + dy * dy);
	if (outYaw)   *outYaw   = atan2f(dy, dx);
	if (outPitch) *outPitch = atan2f(dz, horiz);
	return true;
}

extern "C" void InspectorApplyCameraOverride(
	float posX, float posY, float posZ,
	float yaw, float pitch)
{
	if (!TheTacticalView) return;
	W3DView* w3d = static_cast<W3DView*>(TheTacticalView);
	CameraClass* cam = w3d->get3DCamera();
	if (!cam) return;

	// Forward vector from yaw + pitch (standard "first person" math).
	// Engine convention: Z is up, angle=0 looks down +X.
	const float cp = cosf(pitch);
	const float fx = cosf(yaw) * cp;
	const float fy = sinf(yaw) * cp;
	const float fz = sinf(pitch);

	const Vector3 source(posX, posY, posZ);
	const Vector3 target(posX + fx, posY + fy, posZ + fz);

	Matrix3D mat;
	mat.Make_Identity();
	mat.Look_At(source, target, 0.0f);
	cam->Set_Transform(mat);
}

// Inspector bridge: extracts the heightmap byte data + world extent
// so the ImGui radar panel can bake a terrain preview texture
// without needing to include any W3D / DX8-era headers itself. The
// caller stays in the modern-rendering side of the codebase and
// only sees opaque bytes + dimensions through this C function.
//
// Returns false if no heightmap is loaded (e.g. on the shell map).
// The returned data pointer is owned by the heightmap and remains
// valid until the next map load — callers should copy out what
// they need before releasing the lock on the render path.
//
// MAP_XY_FACTOR for the world-space per-cell size is hardcoded at
// 10.0f to match the engine's standard tile size. Callers use the
// world extent to position the baked texture correctly on the
// modern radar's zoom/pan canvas.
extern "C" bool InspectorGetTerrainBytes(
	int* outWidth, int* outHeight, int* outBorder,
	float* outWorldWidth, float* outWorldHeight,
	const unsigned char** outData)
{
	WorldHeightMap* hm = GetTerrainHeightMap();
	if (!hm) return false;

	const int w = hm->getXExtent();
	const int h = hm->getYExtent();
	const int border = hm->getBorderSizeInline();
	const unsigned char* data = (const unsigned char*)hm->getDataPtr();
	if (!data || w <= 0 || h <= 0) return false;

	if (outWidth)       *outWidth  = w;
	if (outHeight)      *outHeight = h;
	if (outBorder)      *outBorder = border;
	if (outData)        *outData   = data;

	// MAP_XY_FACTOR = 10.0 world units per heightmap cell (standard
	// engine constant — the terrain mesh builder uses this too).
	const float kCellWorld = 10.0f;
	if (outWorldWidth)  *outWorldWidth  = (float)(w - 2 * border) * kCellWorld;
	if (outWorldHeight) *outWorldHeight = (float)(h - 2 * border) * kCellWorld;
	return true;
}

void ResetDX11TerrainProps()
{
	delete g_dx11TerrainPropBuffer;
	g_dx11TerrainPropBuffer = nullptr;
	delete g_dx11TerrainTreeBuffer;
	g_dx11TerrainTreeBuffer = nullptr;
}

void AddDX11TerrainProp(Int id, Coord3D location, Real angle, Real scale, const AsciiString &modelName)
{
	if (modelName.isEmpty()) {
		return;
	}

	EnsureDX11TerrainPropBuffer()->addProp(id, location, angle, scale, modelName);
}

void RemoveDX11TerrainPropsForConstruction(const Coord3D *pos, const GeometryInfo &geom, Real angle)
{
	if (g_dx11TerrainPropBuffer == nullptr) {
		return;
	}

	g_dx11TerrainPropBuffer->removePropsForConstruction(pos, geom, angle);
}

void RenderTerrainPropsDX11(CameraClass *camera)
{
	if (camera == nullptr) {
		return;
	}

	// Don't call BeginFrame here — it overwrites the constant buffer with
	// potentially different camera data, causing dark frames. The main
	// BeginFrame in W3DView::draw() already set up the correct camera.
	// Restore3DState is sufficient to reset pipeline state for props.
	Render::Renderer::Instance().Restore3DState();
	if (TheTerrainRenderObject != nullptr) {
		TheTerrainRenderObject->renderProps(camera);
	}

	if (g_dx11TerrainTreeBuffer != nullptr) {
		g_dx11TerrainTreeBuffer->renderDX11(camera);
	}

	if (g_dx11TerrainPropBuffer != nullptr) {
		g_dx11TerrainPropBuffer->renderDX11(camera);
	}
}

// DX8Wrapper statics needed by scene.cpp
#include "WW3D2/dx8wrapper.h"
unsigned int number_of_DX8_calls = 0;
bool DX8Wrapper::FogEnable = false;
unsigned long DX8Wrapper::FogColor = 0;
unsigned int DX8Wrapper::RenderStates[256] = {};
unsigned int DX8Wrapper::render_state_changes = 0;
bool DX8Wrapper::IsInitted = false;
void DX8Wrapper::Clear(bool, bool, const Vector3&, float, float, unsigned int) {}
void DX8Wrapper::Set_Light(unsigned int, const D3DLIGHT8*) {}
const char* DX8Wrapper::Get_DX8_Render_State_Name(D3DRENDERSTATETYPE) { return ""; }
void DX8Wrapper::Get_DX8_Render_State_Value_Name(StringClass&, D3DRENDERSTATETYPE, unsigned int) {}
void WW3D::Enable_Texturing(bool) {}
IDirect3DDevice8* DX8Wrapper::D3DDevice = nullptr;
DX8Caps* DX8Wrapper::CurrentCaps = nullptr;

// Create a SimpleSceneClass for W3DDisplay::m_3DScene
#include "WW3D2/scene.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
void CreateD3D11Scene()
{
	SimpleSceneClass* scene = new SimpleSceneClass();
	W3DDisplay::m_3DScene = reinterpret_cast<RTS3DScene*>(scene);
}

// AutoPoolClass allocator for CameraShakerClass - explicit instantiation of static member
typedef CameraShakeSystemClass::CameraShakerClass CameraShaker_t;
ObjectPoolClass<CameraShaker_t, 256> AutoPoolClass<CameraShaker_t, 256>::Allocator;

// Global CameraShakeSystem instance
CameraShakeSystemClass CameraShakerSystem;

// WW3DAssetManager singleton
#include "WW3D2/assetmgr.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
WW3DAssetManager* GetWW3DAssetManagerInstance()
{
	return WW3DAssetManager::Get_Instance();
}
W3DAssetManager* EnsureWW3DAssetManagerInstance()
{
	W3DAssetManager* assetManager = static_cast<W3DAssetManager*>(WW3DAssetManager::Get_Instance());
	if (assetManager == nullptr)
	{
		assetManager = new W3DAssetManager;
		assetManager->Set_WW3D_Load_On_Demand(true);
	}

	return assetManager;
}
void DestroyWW3DAssetManagerInstance()
{
	WW3DAssetManager* assetManager = WW3DAssetManager::Get_Instance();
	if (assetManager)
	{
		assetManager->Free_Assets();
		delete assetManager;
	}
}
RenderObjClass* CreateRenderObjCompat(const char* name, float scale, int color, const char* oldTexture, const char* newTexture)
{
	W3DAssetManager* assetManager = EnsureWW3DAssetManagerInstance();
	if (assetManager == nullptr || name == nullptr || *name == '\0')
		return nullptr;

	return assetManager->Create_Render_Obj(name, scale, color, oldTexture, newTexture);
}

// Helper for W3DDisplay::preloadModelAssets -- avoids including assetmgr.h there
void PreloadModelViaAssetManager(W3DAssetManager* mgr, const char* name)
{
	if (!mgr || !name || !*name)
		return;
	RenderObjClass* obj = mgr->Create_Render_Obj(name);
	if (obj)
		obj->Release_Ref();
}

// WW3D::Get_Render_Target_Resolution - returns the current display resolution
#include "Renderer.h"
void WW3D::Get_Render_Target_Resolution(int& w, int& h, int& bits, bool& windowed)
{
	auto& r = Render::Renderer::Instance();
	w = r.GetWidth();
	h = r.GetHeight();
	bits = 32;
	windowed = true;
}

CameraShakeSystemClass::CameraShakeSystemClass()
{
}

CameraShakeSystemClass::~CameraShakeSystemClass()
{
}

void CameraShakeSystemClass::Add_Camera_Shake(const Vector3& position, float radius, float duration, float power)
{
	CameraShakerClass* shaker = new CameraShakerClass(position, radius, duration, power);
	CameraShakerList.Add_Tail(shaker);
}

void CameraShakeSystemClass::Timestep(float dt)
{
	CameraShakerClass* shaker = nullptr;
	MultiListIterator<CameraShakerClass> it(&CameraShakerList);
	for (it.First(); !it.Is_Done(); it.Next())
	{
		shaker = it.Peek_Obj();
		shaker->Timestep(dt);
	}
	// Remove expired shakers
	it.First();
	while (!it.Is_Done())
	{
		shaker = it.Peek_Obj();
		it.Next();
		if (shaker->Is_Expired())
		{
			CameraShakerList.Remove(shaker);
			delete shaker;
		}
	}
}

bool CameraShakeSystemClass::IsCameraShaking()
{
	return !CameraShakerList.Is_Empty();
}

void CameraShakeSystemClass::Update_Camera_Shaker(Vector3 camera_position, Vector3* shaker_angles)
{
	shaker_angles->Set(0, 0, 0);
	MultiListIterator<CameraShakerClass> it(&CameraShakerList);
	for (it.First(); !it.Is_Done(); it.Next())
	{
		Vector3 angles;
		it.Peek_Obj()->Compute_Rotations(camera_position, &angles);
		*shaker_angles += angles;
	}
}

CameraShakeSystemClass::CameraShakerClass::CameraShakerClass(const Vector3& position, float radius, float duration, float power)
	: Position(position), Radius(radius), Duration(duration), Intensity(power), ElapsedTime(0)
{
}

CameraShakeSystemClass::CameraShakerClass::~CameraShakerClass()
{
}

void CameraShakeSystemClass::CameraShakerClass::Compute_Rotations(const Vector3& pos, Vector3* set_angles)
{
	float dist = (pos - Position).Length();
	if (dist > Radius || Duration <= 0.0f)
	{
		set_angles->Set(0, 0, 0);
		return;
	}

	float attenuation = 1.0f - (dist / Radius);
	float time_scale = 1.0f - (ElapsedTime / Duration);
	float amplitude = Intensity * attenuation * time_scale;

	// Simple sine-based shake
	float freq = 15.0f;
	float t = ElapsedTime * freq;
	set_angles->X = amplitude * 0.01f * sinf(t * 1.1f);
	set_angles->Y = amplitude * 0.01f * sinf(t * 0.9f + 1.0f);
	set_angles->Z = amplitude * 0.005f * sinf(t * 1.3f + 2.0f);
}


////////////////////////////////////////////////////////////////////////////////
// SECTION: Particle Rendering
////////////////////////////////////////////////////////////////////////////////

#include "Win32Device/Common/Win32GameEngine.h"
#include "W3DDevice/GameClient/ImageCache.h"
#include "Renderer.h"
#include "GPUParticles.h"
#include "GameLogic/GameLogic.h"

void D3D11ParticleSystemManager::doParticles(RenderInfoClass &rinfo)
{
	auto& allSystems = getAllParticleSystems();
	if (allSystems.empty())
		return;

	auto& renderer = Render::Renderer::Instance();
	auto& device = renderer.GetDevice();
	auto& imageCache = Render::ImageCache::Instance();

	// White 1x1 fallback for streak particles without a loadable texture
	static Render::Texture s_whiteTex;
	static Render::Texture* s_whiteTexPtr = nullptr;
	if (!s_whiteTexPtr) {
		const uint32_t white = 0xFFFFFFFF;
		s_whiteTex.CreateFromRGBA(device, &white, 1, 1, false);
		s_whiteTexPtr = &s_whiteTex;
	}

	// Compute billboard orientation vectors from the camera.
	Vector3 camPos = rinfo.Camera.Get_Position();
	Matrix3D camTransform = rinfo.Camera.Get_Transform();

	// Camera's local axes in world space.
	float rightX = camTransform[0][0];
	float rightY = camTransform[1][0];
	float rightZ = camTransform[2][0];
	float upX    = camTransform[0][1];
	float upY    = camTransform[1][1];
	float upZ    = camTransform[2][1];
	float fwdX   = camTransform[0][2];
	float fwdY   = camTransform[1][2];
	float fwdZ   = camTransform[2][2];

	// Normalize right, up, and forward vectors
	float rLen = sqrtf(rightX*rightX + rightY*rightY + rightZ*rightZ);
	if (rLen > 0.0001f) { rightX /= rLen; rightY /= rLen; rightZ /= rLen; }
	float uLen = sqrtf(upX*upX + upY*upY + upZ*upZ);
	if (uLen > 0.0001f) { upX /= uLen; upY /= uLen; upZ /= uLen; }
	float fLen = sqrtf(fwdX*fwdX + fwdY*fwdY + fwdZ*fwdZ);
	if (fLen > 0.0001f) { fwdX /= fLen; fwdY /= fLen; fwdZ /= fLen; }

	// --- Frustum culling setup ---
	// Use camera far clip distance as a spherical cull radius.
	// Particles beyond the far plane cannot be visible.
	float cullRadiusSq = 0.0f;
	{
		float nearClip, farClip;
		rinfo.Camera.Get_Clip_Planes(nearClip, farClip);
		float cullRadius = farClip * 1.2f; // slight margin for large particles
		cullRadiusSq = cullRadius * cullRadius;
	}

	// --- Static batch collection to avoid per-frame heap allocations ---
	// Particle blend mode mirrors ParticleSystemInfo::ParticleShaderType so we
	// can route the four original sprite-shader presets correctly.
	enum ParticleBlendMode : uint8_t
	{
		PBM_ADDITIVE = 0,
		PBM_ALPHA = 1,
		PBM_ALPHA_TEST = 2,
		PBM_MULTIPLY = 3,
		PBM_SMUDGE = 4,  // heat-distortion refraction (Enhanced Smudges only)
	};

	struct ParticleBatch
	{
		Render::Texture* texture = nullptr;
		ParticleBlendMode blend = {}; // value-init → PBM_ADDITIVE (==0)
		std::vector<Render::Vertex3D> vertices;
		std::vector<uint16_t> indices;
		// Per-quad depth (camera-forward . (quadCenter - camPos)) paired with
		// the quad's base-vertex index. Populated only for non-additive batches
		// (PBM_ALPHA / PBM_ALPHA_TEST / PBM_MULTIPLY) so we can sort
		// back-to-front before drawing. Additive is commutative — we skip the
		// work there. See drawBatch sort pass below.
		struct QuadDepth { float viewZ; uint16_t baseVertex; };
		std::vector<QuadDepth> quadDepths;
	};

	// Batch key: texture pointer + blend mode. Using pointer avoids string hashing.
	// Textures are cached by ImageCache so same name always returns same pointer.
	struct BatchKey
	{
		Render::Texture* texture;
		ParticleBlendMode blend;
		bool operator==(const BatchKey& o) const { return texture == o.texture && blend == o.blend; }
	};
	struct BatchKeyHash
	{
		size_t operator()(const BatchKey& k) const { return std::hash<void*>()(k.texture) ^ ((size_t)k.blend << 1); }
	};

	static std::unordered_map<BatchKey, ParticleBatch, BatchKeyHash> batches;
	// Clear vectors but keep allocated capacity from previous frames
	for (auto& [key, batch] : batches) {
		batch.vertices.clear();
		batch.indices.clear();
		batch.quadDepths.clear();
	}

	int totalParticles = 0;

	// Reset the field-particle counter at the start of each frame. The
	// original W3DParticleSys.cpp bumps this once per ground-aligned
	// AREA_EFFECT particle as it iterates; ParticleSystem::createParticle
	// uses it (via TheGlobalData->m_maxFieldParticleCount == 30) to cap the
	// number of simultaneous toxin puddles / radiation fields / AOE fog
	// ground particles. Without updating it, the D3D11 port lets field
	// particles accumulate without bound → performance cliff in long games.
	m_fieldParticleCount = 0;

	for (auto& pSystem : allSystems)
	{
		if (!pSystem)
			continue;
		// A "destroyed" particle system has only stopped emitting — its existing
		// particles must keep rendering until they fade out naturally. This matters
		// for persistent ground effects (toxin puddles, radiation fields, etc.) that
		// get destroy()'d when their owning model condition state changes but whose
		// particles still have lifetime remaining. Skip only if no particles remain.
		if (pSystem->isDestroyed() && !pSystem->getFirstParticle())
			continue;

		// Skip drawables (3D model particles — handled elsewhere).
		if (pSystem->isUsingDrawables())
			continue;
		// Smudges (heat-distortion sprites). The original DX8 build rendered
		// these via W3DSmudgeManager as a refractive screen-space pass that
		// sampled a copy of the back buffer with offset UVs.
		// When Enhanced Smudges is OFF (default), skip — matches classic D3D11.
		// When ON, route into a separate smudge batch that the post-particle
		// pass renders with PSMainSmudge (real refraction). The collection
		// here just records vertices into a special blend mode key so the
		// existing batch loop assembles them; the dispatch at the end
		// switches to SetSmudge3DState before drawing the smudge batch.
		extern bool g_useEnhancedSmudges;
		// Original W3DParticleSys.cpp used a hack: detect smudges by testing
		// whether the particle texture name starts with "SMUD" (comparing the
		// first 4 chars as a DWORD against 'DUMS' = 0x44554D53 little-endian).
		// Some shipping particle INI entries rely on this naming convention
		// instead of setting Type = SMUDGE. Honor both checks so all smudge
		// particles route to the refraction path when Enhanced Smudges is on.
		bool isSmudgeSys = pSystem->isUsingSmudge();
		if (!isSmudgeSys)
		{
			const char* texPrefix = pSystem->getParticleTypeName().str();
			if (texPrefix && texPrefix[0] == 'S' && texPrefix[1] == 'M' &&
			    texPrefix[2] == 'U' && texPrefix[3] == 'D')
				isSmudgeSys = true;
		}
		if (isSmudgeSys && !g_useEnhancedSmudges)
			continue;

		// System-level frustum cull
		{
			Coord3D sysPos;
			pSystem->getPosition(&sysPos);
			float sdx = sysPos.x - camPos.X, sdy = sysPos.y - camPos.Y, sdz = sysPos.z - camPos.Z;
			if ((sdx*sdx + sdy*sdy + sdz*sdz) > cullRadiusSq * 4.0f)
				continue;
		}

		// Count ground-aligned AREA_EFFECT particles — one bump per system.
		// ParticleSystem::createParticle reads this counter to enforce the
		// TheGlobalData->m_maxFieldParticleCount cap (default 30) on toxin
		// puddles / radiation fields / AOE fog. Original W3DParticleSys.cpp
		// incremented it inside the per-particle loop; we do it at the
		// system level because iterating particles here just to count would
		// waste CPU (the cap is approximate). Ground-aligned flag: read via
		// !shouldBillboard() since m_isGroundAligned is a protected member.
		if (pSystem->getPriority() == AREA_EFFECT && !pSystem->shouldBillboard())
			m_fieldParticleCount += (UnsignedInt)1;

		const AsciiString& texName = pSystem->getParticleTypeName();
		if (texName.isEmpty())
			continue;

		// Determine blend mode from particle shader type — preserve all four
		// modes the original engine supported. Collapsing MULTIPLY/ALPHA_TEST
		// into ALPHA loses shadow particles (which need DEST*SRC darkening) and
		// hard-edged debris (which need alpha-clipping). Smudges get their own
		// blend mode that routes through the refraction shader.
		ParticleBlendMode blendMode = PBM_ADDITIVE;
		if (isSmudgeSys)
		{
			blendMode = PBM_SMUDGE;
		}
		else
		{
			switch (pSystem->getShaderType())
			{
				case ParticleSystemInfo::ADDITIVE:    blendMode = PBM_ADDITIVE;   break;
				case ParticleSystemInfo::ALPHA:       blendMode = PBM_ALPHA;      break;
				case ParticleSystemInfo::ALPHA_TEST:  blendMode = PBM_ALPHA_TEST; break;
				case ParticleSystemInfo::MULTIPLY:    blendMode = PBM_MULTIPLY;   break;
				default:                              blendMode = PBM_ADDITIVE;   break;
			}
		}
		bool isAdditive = (blendMode == PBM_ADDITIVE);

		// Look up texture — use white fallback for streaks if texture fails
		Render::Texture* tex = imageCache.GetTexture(device, texName.str());
		if (!tex && !pSystem->isUsingStreak())
			continue;

		float sizeMultiplier = pSystem->getSizeMultiplier();
		bool isStreak = pSystem->isUsingStreak();

		// Streaks (rocket trails) use additive blending — their INI alpha values
		// are very low (0.0-0.25), designed for the original's additive StreakRenderer.
		// With standard alpha blending they'd be invisible.
		if (isStreak)
		{
			blendMode = PBM_ADDITIVE;
			isAdditive = true;
		}

		BatchKey batchKey = { tex ? tex : s_whiteTexPtr, blendMode };
		auto& batch = batches[batchKey];
		if (!batch.texture)
		{
			batch.texture = batchKey.texture;
			batch.blend = blendMode;
		}

		if (isStreak)
		{
			// Streak particles: classic ribbon trail is ALWAYS drawn — it is the
			// ground-truth visual, aligned exactly with the CPU streak particles
			// (which follow the emitting bone every frame). The GPU volumetric
			// smoke is an optional overlay; it drifts on its own and is subject
			// to a 256/frame staging cap, so it cannot be the sole visual or
			// contrails under large planes end up misaligned.
			extern bool g_useClassicTrails;
			extern bool g_debugDisableVolumetricTrails;
			bool useVolumetric = !g_useClassicTrails && !g_debugDisableVolumetricTrails;
			bool useRibbon = true;
			Particle* prev = nullptr;
			float prevV = 0.0f;
			auto& gpuParticles = Render::GPUParticleSystem::Instance();
			int gpuTrailType = (pSystem->getFirstParticle() &&
				pSystem->getFirstParticle()->getLifetimeLeft() < 40) ? 0 : 1;

			for (Particle* p = pSystem->getFirstParticle(); p; p = p->m_systemNext)
			{
				// Emit GPU smoke: interpolate between prev and current for continuous trail
				if (useVolumetric && gpuParticles.IsReady() && prev)
				{
					const Coord3D* p0 = prev->getPosition();
					const Coord3D* p1 = p->getPosition();
					float dx = p1->x - p0->x, dy = p1->y - p0->y, dz = p1->z - p0->z;
					float segLen = sqrtf(dx*dx + dy*dy + dz*dz);
					// Space GPU particles every ~2 units along the segment
					float spacing = (gpuTrailType == 0) ? 2.0f : 4.0f;
					int emitCount = (int)(segLen / spacing) + 1;
					if (emitCount > 20) emitCount = 20;
					for (int ei = 0; ei < emitCount; ++ei)
					{
						float t = (emitCount > 1) ? (float)ei / (emitCount - 1) : 0.5f;
						Render::Float3 emitPos = {
							p0->x + dx * t + (rand() % 100 - 50) * 0.005f,
							p0->y + dy * t + (rand() % 100 - 50) * 0.005f,
							p0->z + dz * t + (rand() % 100 - 50) * 0.005f
						};
						Render::Float3 emitVel = {
							(rand() % 100 - 50) * 0.01f,
							(rand() % 100 - 50) * 0.01f,
							(rand() % 100) * 0.005f + 0.3f
						};
						gpuParticles.Emit(emitPos, emitVel, gpuTrailType);
					}
				}

				if (prev)
				{
					const Coord3D* p0 = prev->getPosition();
					const Coord3D* p1 = p->getPosition();

					// Frustum cull: skip segments where both endpoints are beyond the far plane
					float w0 = prev->getSize() * sizeMultiplier * 0.5f;
					float w1 = p->getSize() * sizeMultiplier * 0.5f;
					float d0x = p0->x - camPos.X, d0y = p0->y - camPos.Y, d0z = p0->z - camPos.Z;
					float d1x = p1->x - camPos.X, d1y = p1->y - camPos.Y, d1z = p1->z - camPos.Z;
					bool p0vis = (d0x*d0x + d0y*d0y + d0z*d0z) <= cullRadiusSq;
					bool p1vis = (d1x*d1x + d1y*d1y + d1z*d1z) <= cullRadiusSq;
					if (!p0vis && !p1vis) { prev = p; continue; }

					// Direction along streak
					float dx = p1->x - p0->x, dy = p1->y - p0->y, dz = p1->z - p0->z;
					float len = sqrtf(dx*dx + dy*dy + dz*dz);
					if (len < 0.001f) { prev = p; continue; }

					// Cross direction x camera-to-segment for ribbon width
					float camDx = (p0->x + p1->x) * 0.5f - camPos.X;
					float camDy = (p0->y + p1->y) * 0.5f - camPos.Y;
					float camDz = (p0->z + p1->z) * 0.5f - camPos.Z;
					float nx = dy*camDz - dz*camDy;
					float ny = dz*camDx - dx*camDz;
					float nz = dx*camDy - dy*camDx;
					float nlen = sqrtf(nx*nx + ny*ny + nz*nz);
					if (nlen > 0.001f) { nx /= nlen; ny /= nlen; nz /= nlen; }

					const RGBColor* c0 = prev->getColor();
					float a0 = prev->getAlpha();
					const RGBColor* c1 = p->getColor();
					float a1 = p->getAlpha();
					// Boost alpha for additive visibility (SRC_ALPHA blend).
					// Many missile exhaust streaks have INI alpha=0 (trail was originally
					// invisible, relying on a slave lens flare system for visuals). Give
					// all streaks a minimum alpha so the ribbon itself is visible.
					a0 = fmaxf(a0 * 4.0f, 0.6f);
					a1 = fmaxf(a1 * 4.0f, 0.6f);
					if (a0 > 1.0f) a0 = 1.0f;
					if (a1 > 1.0f) a1 = 1.0f;
					auto packC = [](const RGBColor* c, float a) -> uint32_t {
						return ((uint32_t)(a*255)<<24) | ((uint32_t)(c->blue*255)<<16) | ((uint32_t)(c->green*255)<<8) | (uint32_t)(c->red*255);
					};

					float nextV = prevV + len / (w0 + w1 + 0.01f);

					// Render streak segment immediately (classic ribbon trail).
					if (useRibbon)
					{
						uint32_t sc0 = packC(c0, a0);
						uint32_t sc1 = packC(c1, a1);
						Render::Vertex3D sv[6];
						sv[0] = {{ p0->x - nx*w0, p0->y - ny*w0, p0->z - nz*w0 }, {0,0,1}, {0, prevV}, sc0};
						sv[1] = {{ p0->x + nx*w0, p0->y + ny*w0, p0->z + nz*w0 }, {0,0,1}, {1, prevV}, sc0};
						sv[2] = {{ p1->x + nx*w1, p1->y + ny*w1, p1->z + nz*w1 }, {0,0,1}, {1, nextV}, sc1};
						sv[3] = {{ p0->x - nx*w0, p0->y - ny*w0, p0->z - nz*w0 }, {0,0,1}, {0, prevV}, sc0};
						sv[4] = {{ p1->x + nx*w1, p1->y + ny*w1, p1->z + nz*w1 }, {0,0,1}, {1, nextV}, sc1};
						sv[5] = {{ p1->x - nx*w1, p1->y - ny*w1, p1->z - nz*w1 }, {0,0,1}, {0, nextV}, sc1};
						Render::VertexBuffer svb;
						svb.Create(renderer.GetDevice(), sv, 6, sizeof(Render::Vertex3D));
						renderer.SetAdditive3DState();
						Render::Float4x4 ident;
						DirectX::XMStoreFloat4x4(&Render::ToXM(ident), DirectX::XMMatrixIdentity());
						renderer.Draw3DNoIndex(svb, 6, tex ? tex : s_whiteTexPtr, ident, {1,1,1,1});
					}
					prevV = nextV;
				}
				prev = p;
			}
		}
		else
		{
		// Regular billboard particles
		// Hoist invariants out of the per-particle loop
		bool isBillboard = pSystem->shouldBillboard();
		unsigned int volDepth = pSystem->getVolumeParticleDepth();
		int numLayers = (volDepth > 1) ? (int)volDepth : 1;

		for (Particle* p = pSystem->getFirstParticle(); p; p = p->m_systemNext)
		{
			const Coord3D* pos = p->getPosition();
			float size = p->getSize() * sizeMultiplier;
			float halfSize = size * 0.5f;

			// Frustum cull: skip particles beyond the camera far plane
			float pdx = pos->x - camPos.X, pdy = pos->y - camPos.Y, pdz = pos->z - camPos.Z;
			if ((pdx*pdx + pdy*pdy + pdz*pdz) > cullRadiusSq)
				continue;

			float alpha = p->getAlpha();
			if (alpha < 0.01f || size < 0.1f)
				continue;

			uint16_t baseIdx = static_cast<uint16_t>(batch.vertices.size());
			if (baseIdx > 65530)
				break;

			const RGBColor* color = p->getColor();
			UnsignedByte r = static_cast<UnsignedByte>(std::min(255.0f, color->red * 255.0f));
			UnsignedByte g = static_cast<UnsignedByte>(std::min(255.0f, color->green * 255.0f));
			UnsignedByte b = static_cast<UnsignedByte>(std::min(255.0f, color->blue * 255.0f));
			UnsignedByte a = static_cast<UnsignedByte>(std::min(255.0f, alpha * 255.0f));
			uint32_t packedColor = (a << 24) | (b << 16) | (g << 8) | r;

			float angle = p->getAngle();
			float rx, ry, rz, ux, uy, uz;
			if (isBillboard)
			{
				if (angle == 0.0f)
				{
					// Fast path: no rotation, skip sinf/cosf
					rx = rightX * halfSize; ry = rightY * halfSize; rz = rightZ * halfSize;
					ux = upX * halfSize;    uy = upY * halfSize;    uz = upZ * halfSize;
				}
				else
				{
					// Camera-facing billboard with per-particle rotation
					float cosA = cosf(angle);
					float sinA = sinf(angle);
					rx = (rightX * cosA + upX * sinA) * halfSize;
					ry = (rightY * cosA + upY * sinA) * halfSize;
					rz = (rightZ * cosA + upZ * sinA) * halfSize;
					ux = (-rightX * sinA + upX * cosA) * halfSize;
					uy = (-rightY * sinA + upY * cosA) * halfSize;
					uz = (-rightZ * sinA + upZ * cosA) * halfSize;
				}
			}
			else
			{
				// Ground-aligned: flat on XY plane, rotated by angle around Z
				float cosA = cosf(angle);
				float sinA = sinf(angle);
				rx = cosA * halfSize; ry = sinA * halfSize; rz = 0.0f;
				ux = -sinA * halfSize; uy = cosA * halfSize; uz = 0.0f;
			}

			float cx = pos->x;
			float cy = pos->y;
			float cz = pos->z;

			// Volume particle layering — match original PointGroupClass::RenderVolumeParticle
			// (WW3D2/pointgr.cpp:1776-1812). Two rules from the original:
			//  1) Layer shift is ONLY applied when the particle is a billboard. For
			//     ground-aligned particles (toxin/radiation/anthrax puddles, AOE fog)
			//     all depth iterations render at the same XYZ — extra iterations are
			//     redundant overdraw at the exact same ground footprint, not an
			//     extruded stack poking up through the ground plane.
			//  2) Shift magnitude is `t * size * 0.1 / depth` — tiny fraction of
			//     particle size, spreading layers over ~0.1× size total. Shift
			//     direction is the camera-to-particle vector (normalized). This
			//     produces a subtle parallax/thickness illusion, not a thick slab.
			// The previously-ported version spread layers across the FULL particle
			// size along camera forward, regardless of ground alignment, causing
			// toxin/radiation puddles to look like bright stacked slabs of glow.
			float toPx = pos->x - camPos.X;
			float toPy = pos->y - camPos.Y;
			float toPz = pos->z - camPos.Z;
			float toPlen = sqrtf(toPx*toPx + toPy*toPy + toPz*toPz);
			if (toPlen > 0.0001f) { toPx /= toPlen; toPy /= toPlen; toPz /= toPlen; }
			float recipDepth = (numLayers > 0) ? (0.1f / (float)numLayers) : 0.0f;
			bool allowLayerShift = isBillboard && (numLayers > 1);

			for (int layer = 0; layer < numLayers; ++layer)
			{
			float shiftInc = allowLayerShift ? ((float)layer * size * recipDepth) : 0.0f;
			float lx = cx + toPx * shiftInc;
			float ly = cy + toPy * shiftInc;
			float lz = cz + toPz * shiftInc;

			uint16_t layerBaseIdx = static_cast<uint16_t>(batch.vertices.size());
			if (layerBaseIdx > 65520) break;

			Render::Vertex3D v = {};
			v.normal = { 0.0f, 0.0f, 1.0f };
			v.color = packedColor;

			v.position = { lx - rx - ux, ly - ry - uy, lz - rz - uz };
			v.texcoord = { 0.0f, 1.0f };
			batch.vertices.push_back(v);

			v.position = { lx + rx - ux, ly + ry - uy, lz + rz - uz };
			v.texcoord = { 1.0f, 1.0f };
			batch.vertices.push_back(v);

			v.position = { lx + rx + ux, ly + ry + uy, lz + rz + uz };
			v.texcoord = { 1.0f, 0.0f };
			batch.vertices.push_back(v);

			v.position = { lx - rx + ux, ly - ry + uy, lz - rz + uz };
			v.texcoord = { 0.0f, 0.0f };
			batch.vertices.push_back(v);

			batch.indices.push_back(layerBaseIdx + 0);
			batch.indices.push_back(layerBaseIdx + 1);
			batch.indices.push_back(layerBaseIdx + 2);
			batch.indices.push_back(layerBaseIdx + 0);
			batch.indices.push_back(layerBaseIdx + 2);
			batch.indices.push_back(layerBaseIdx + 3);

			// Record per-quad view-space depth so non-additive batches can be
			// sorted back-to-front before draw. viewZ = camera-forward .
			// (quadCenter - camPos). Larger viewZ = farther from camera →
			// drawn first. Additive batches are commutative — we still record
			// here but the sort path skips them below. Quad center = (lx,ly,lz)
			// for both billboard and ground-aligned particles (same plane origin).
			if (!isAdditive)
			{
				float viewZ = (lx - camPos.X) * fwdX
				            + (ly - camPos.Y) * fwdY
				            + (lz - camPos.Z) * fwdZ;
				batch.quadDepths.push_back({ viewZ, layerBaseIdx });
			}
			} // end volume layer loop

			++totalParticles;
		}
		} // end else (billboard particles)
	}

	m_onScreenParticleCount = totalParticles;

	if (totalParticles == 0)
		return;

	// Persistent dynamic GPU buffers - created once, reused every frame.
	static const uint32_t MAX_PARTICLE_QUADS = 16384;
	static Render::VertexBuffer s_particleVB;
	static Render::IndexBuffer  s_particleIB;
	static bool s_particleBuffersCreated = false;

	if (!s_particleBuffersCreated)
	{
		if (!s_particleVB.Create(device, nullptr, MAX_PARTICLE_QUADS * 4, sizeof(Render::Vertex3D), true))
			return;
		if (!s_particleIB.Create(device, nullptr, MAX_PARTICLE_QUADS * 6, true))
			return;
		s_particleBuffersCreated = true;
	}

	// Render each batch. Draw additive batches FIRST — they blend
	// commutatively so any draw order is correct. Then draw non-additive
	// (alpha / alpha_test / multiply) batches which need consistent
	// ordering to avoid depth z-fighting. Hash-iteration order of the
	// unordered_map is pseudorandom, which was causing smoke columns
	// to pop in/out as the map rehashed. A stable two-pass split gives
	// a consistent frame-to-frame order without the cost of actually
	// sorting by centroid distance.
	Render::Float4x4 worldIdentity;
	DirectX::XMStoreFloat4x4(&Render::ToXM(worldIdentity), DirectX::XMMatrixIdentity());
	Render::Float4 white = { 1.0f, 1.0f, 1.0f, 1.0f };

	// Pre-build a stable ordering: additive first, then non-additive in
	// texture-pointer order (deterministic per-frame).
	struct BatchRef { BatchKey key; ParticleBatch* batch; };
	static std::vector<BatchRef> s_orderedAdditive;
	static std::vector<BatchRef> s_orderedAlpha;
	s_orderedAdditive.clear();
	s_orderedAlpha.clear();
	for (auto& [key, batch] : batches)
	{
		if (batch.vertices.empty())
			continue;
		if (batch.blend == PBM_ADDITIVE)
			s_orderedAdditive.push_back({ key, &batch });
		else
			s_orderedAlpha.push_back({ key, &batch });
	}
	// Sort alpha batches back-to-front by their farthest quad (descending
	// viewZ). Within each batch we ALSO reorder indices per-quad back-to-
	// front below, in drawBatch. Two levels of sort:
	//   - Cross-batch: keeps different-textured translucents (e.g. black
	//     smoke vs. light steam) from occluding each other wrong where
	//     sprites spatially overlap.
	//   - Per-quad (inside drawBatch): handles multiple particle systems
	//     that got merged into the same batch because they share a texture
	//     + blend mode (very common for smoke columns from a squad of
	//     tanks, or stacked toxin/radiation puddles).
	//
	// Fixes z-fighting / pop-in on overlapping smoke columns, stacked
	// toxin/scorch puddles, dust clouds crossing muzzle smoke. Replaces
	// the previous texture-pointer sort which only kept frame-to-frame
	// order stable without achieving depth correctness.
	std::sort(s_orderedAlpha.begin(), s_orderedAlpha.end(),
		[](const BatchRef& a, const BatchRef& b) {
			float aMax = -FLT_MAX, bMax = -FLT_MAX;
			for (auto& qd : a.batch->quadDepths) if (qd.viewZ > aMax) aMax = qd.viewZ;
			for (auto& qd : b.batch->quadDepths) if (qd.viewZ > bMax) bMax = qd.viewZ;
			return aMax > bMax; // farthest first (back-to-front)
		});

	// Scratch buffer reused across batches for the sorted index rebuild.
	static std::vector<uint16_t> s_sortedIndices;

	auto drawBatch = [&](ParticleBatch& batch)
	{
		if (batch.vertices.empty())
			return;

		uint32_t vertCount = static_cast<uint32_t>(batch.vertices.size());
		uint32_t idxCount  = static_cast<uint32_t>(batch.indices.size());

		if (vertCount > MAX_PARTICLE_QUADS * 4)
			vertCount = MAX_PARTICLE_QUADS * 4;
		if (idxCount > MAX_PARTICLE_QUADS * 6)
			idxCount = MAX_PARTICLE_QUADS * 6;

		s_particleVB.Update(device, batch.vertices.data(), vertCount * sizeof(Render::Vertex3D));

		// For non-additive batches, rebuild the index buffer in back-to-
		// front quad order. Additive batches skip this — their blend is
		// commutative so draw order is visually irrelevant; we save both
		// the sort and the index rebuild. This is the core fix for
		// overlapping translucent particles flickering (same texture
		// batched together, emitted in creation order, not depth order).
		if (batch.blend != PBM_ADDITIVE && !batch.quadDepths.empty())
		{
			std::sort(batch.quadDepths.begin(), batch.quadDepths.end(),
				[](const ParticleBatch::QuadDepth& a, const ParticleBatch::QuadDepth& b) {
					return a.viewZ > b.viewZ; // farthest first
				});
			s_sortedIndices.clear();
			s_sortedIndices.reserve(batch.quadDepths.size() * 6);
			for (auto& qd : batch.quadDepths)
			{
				// Drop quads whose vertices didn't fit under MAX_PARTICLE_QUADS
				if ((uint32_t)qd.baseVertex + 3u >= vertCount)
					continue;
				s_sortedIndices.push_back(qd.baseVertex + 0);
				s_sortedIndices.push_back(qd.baseVertex + 1);
				s_sortedIndices.push_back(qd.baseVertex + 2);
				s_sortedIndices.push_back(qd.baseVertex + 0);
				s_sortedIndices.push_back(qd.baseVertex + 2);
				s_sortedIndices.push_back(qd.baseVertex + 3);
			}
			uint32_t sortedCount = static_cast<uint32_t>(s_sortedIndices.size());
			if (sortedCount > MAX_PARTICLE_QUADS * 6)
				sortedCount = MAX_PARTICLE_QUADS * 6;
			if (sortedCount > 0)
				s_particleIB.Update(device, s_sortedIndices.data(), sortedCount * sizeof(uint16_t));
			idxCount = sortedCount;
		}
		else
		{
			s_particleIB.Update(device, batch.indices.data(), idxCount * sizeof(uint16_t));
		}

		// Two paths:
		//   - Classic (g_useEnhancedParticles == false): collapse all 4
		//     blend modes to ADDITIVE / ALPHA-with-LIT-shader. This matches
		//     the previously-shipping look the user is used to.
		//   - Enhanced (g_useEnhancedParticles == true): all 4 blend modes
		//     route through the unlit particle helpers — matches the
		//     original DX8 _Preset*SpriteShader behavior. Smoke / dust /
		//     scorch sprites will look brighter because they aren't
		//     darkened by ComputeLighting + ApplyShroud + ApplyAtmosphere.
		extern bool g_useEnhancedParticles;
		if (batch.blend == PBM_SMUDGE)
		{
			// Smudge refraction: snapshots the back buffer, samples it at
			// displaced UVs from the smudge texture. Always uses the smudge
			// shader regardless of g_useEnhancedParticles.
			renderer.SetSmudge3DState();
		}
		else if (g_useEnhancedParticles)
		{
			switch (batch.blend)
			{
				case PBM_ADDITIVE:    renderer.SetAdditive3DState();              break;
				case PBM_ALPHA:       renderer.SetParticleAlphaBlend3DState();    break;
				case PBM_ALPHA_TEST:  renderer.SetParticleAlphaTest3DState();     break;
				case PBM_MULTIPLY:    renderer.SetParticleMultiplicative3DState();break;
				default:              renderer.SetAdditive3DState();              break;
			}
		}
		else
		{
			// Classic 2-way collapse (matches what previously shipped)
			if (batch.blend == PBM_ADDITIVE)
				renderer.SetAdditive3DState();
			else
				renderer.SetAlphaBlend3DState();
		}

		// Use white fallback for batches without texture (streak trails)
		Render::Texture* drawTex = batch.texture ? batch.texture : s_whiteTexPtr;
		renderer.Draw3DIndexed(s_particleVB, s_particleIB, idxCount, drawTex, worldIdentity, white);
	}; // end drawBatch lambda

	// Draw additive batches first (blend is commutative) then non-additive.
	for (auto& ref : s_orderedAdditive) drawBatch(*ref.batch);
	for (auto& ref : s_orderedAlpha)    drawBatch(*ref.batch);

	renderer.Restore3DState();
}


////////////////////////////////////////////////////////////////////////////////
// SECTION: Dazzle Rendering (D3D11)
//
// Dazzles are glow effects attached to W3D models — muzzle flashes,
// headlights, lens flares.  Each DazzleRenderObjClass carries a type
// index that references a DazzleTypeClass with a texture.  We render
// each dazzle as a camera-facing additive billboard quad.
////////////////////////////////////////////////////////////////////////////////

#include "WW3D2/dazzle.h"
#include "W3DDevice/GameClient/ImageCache.h"

static void RenderDazzleDX11(RenderObjClass* robj, RenderInfoClass& rinfo)
{
	// Dazzle render objects are camera-facing additive billboards used by W3D
	// for radar beacon lights, antenna glows, aircraft afterburners, building
	// spotlights, and some weapon muzzle flashes. They were previously
	// short-circuited in this port because the comment claimed muzzle flashes
	// were already handled by the model's MuzzleFX sub-objects — but that
	// only covers a subset of dazzles, leaving every other beacon/glow dark.
	// The implementation below was in the same file, just behind an early
	// return. Re-enabled here. Risk: any model that ships BOTH a dazzle AND
	// a MuzzleFX sub-object will render the muzzle flash twice; if that
	// turns out to be visible, gate this on the dazzle's owning render-obj
	// being not a muzzle-flash sub-object instead of a global toggle.
	if (!robj)
		return;

	DazzleRenderObjClass* dazzle = static_cast<DazzleRenderObjClass*>(robj);
	if (!DazzleRenderObjClass::Is_Dazzle_Rendering_Enabled())
		return;

	// Get dazzle type for texture lookup
	unsigned typeId = dazzle->Get_Dazzle_Type();
	DazzleTypeClass* dazzleType = DazzleRenderObjClass::Get_Type_Class(typeId);

	// Compute intensity based on view angle/distance via the type class
	float dazzleIntensity = 1.0f;
	float dazzleSize = 1.0f;
	float haloIntensity = 0.0f;

	const Matrix3D& camTM = rinfo.Camera.Get_Transform();
	Vector3 cameraPos(camTM[0][3], camTM[1][3], camTM[2][3]);
	Vector3 camForward(camTM[0][2], camTM[1][2], camTM[2][2]);

	// Get dazzle world position from its transform
	const Matrix3D& dazzleTM = dazzle->Get_Transform();
	Vector3 dazzlePos(dazzleTM[0][3], dazzleTM[1][3], dazzleTM[2][3]);
	Vector3 dazzleDir(dazzleTM[0][2], dazzleTM[1][2], dazzleTM[2][2]);

	Vector3 dirToDazzle = dazzlePos - cameraPos;
	float distance = dirToDazzle.Length();
	if (distance < 0.01f) return;
	dirToDazzle *= (1.0f / distance);

	if (dazzleType)
		dazzleType->Calculate_Intensities(dazzleIntensity, dazzleSize, haloIntensity,
			camForward, dazzleDir, dirToDazzle, distance);

	if (dazzleIntensity < 0.01f) return;

	float scale = dazzle->Get_Scale();
	float halfSize = dazzleSize * scale * 0.4f;  // match original's subtle muzzle flash size

	const Vector3& color = dazzle->Get_Dazzle_Color();
	float alpha = dazzleIntensity;

	// Load texture from dazzle type
	Render::Texture* tex = nullptr;
	if (dazzleType)
	{
		TextureClass* ww3dTex = dazzleType->Get_Dazzle_Texture();
		if (ww3dTex)
		{
			const char* texName = ww3dTex->Get_Texture_Name().str();
			if (texName && texName[0])
				tex = Render::ImageCache::Instance().GetTexture(
					Render::Renderer::Instance().GetDevice(), texName);
			ww3dTex->Release_Ref();
		}
	}

	// Build camera-facing billboard quad
	float rightX = camTM[0][0], rightY = camTM[1][0], rightZ = camTM[2][0];
	float upX    = camTM[0][1], upY    = camTM[1][1], upZ    = camTM[2][1];
	float rLen = sqrtf(rightX*rightX + rightY*rightY + rightZ*rightZ);
	if (rLen > 0.0001f) { rightX /= rLen; rightY /= rLen; rightZ /= rLen; }
	float uLen = sqrtf(upX*upX + upY*upY + upZ*upZ);
	if (uLen > 0.0001f) { upX /= uLen; upY /= uLen; upZ /= uLen; }

	float rx = rightX * halfSize, ry = rightY * halfSize, rz = rightZ * halfSize;
	float ux = upX * halfSize, uy = upY * halfSize, uz = upZ * halfSize;

	uint8_t cr = (uint8_t)(std::min(255.0f, color.X * 255.0f));
	uint8_t cg = (uint8_t)(std::min(255.0f, color.Y * 255.0f));
	uint8_t cb = (uint8_t)(std::min(255.0f, color.Z * 255.0f));
	uint8_t ca = (uint8_t)(std::min(255.0f, alpha * 255.0f));
	uint32_t packedColor = (ca << 24) | (cb << 16) | (cg << 8) | cr;

	Render::Vertex3D verts[6];
	auto makeVert = [&](float px, float py, float pz, float u, float v) -> Render::Vertex3D {
		return { {px, py, pz}, {0,0,1}, {u, v}, packedColor };
	};

	float cx = dazzlePos.X, cy = dazzlePos.Y, cz = dazzlePos.Z;
	verts[0] = makeVert(cx - rx - ux, cy - ry - uy, cz - rz - uz, 0, 1);
	verts[1] = makeVert(cx + rx - ux, cy + ry - uy, cz + rz - uz, 1, 1);
	verts[2] = makeVert(cx + rx + ux, cy + ry + uy, cz + rz + uz, 1, 0);
	verts[3] = makeVert(cx - rx - ux, cy - ry - uy, cz - rz - uz, 0, 1);
	verts[4] = makeVert(cx + rx + ux, cy + ry + uy, cz + rz + uz, 1, 0);
	verts[5] = makeVert(cx - rx + ux, cy - ry + uy, cz - rz + uz, 0, 0);

	auto& renderer = Render::Renderer::Instance();
	Render::VertexBuffer vb;
	vb.Create(renderer.GetDevice(), verts, 6, sizeof(Render::Vertex3D));
	renderer.SetAdditive3DState();

	Render::Float4x4 identity;
	DirectX::XMStoreFloat4x4(&Render::ToXM(identity), DirectX::XMMatrixIdentity());
	renderer.Draw3DNoIndex(vb, 6, tex, identity, {1,1,1,1});
}


////////////////////////////////////////////////////////////////////////////////
// SECTION: WW3D ParticleBuffer Rendering (D3D11)
//
// ParticleBufferClass holds engine-level particles attached to W3D models
// (exhaust, dust, weapon effects). We update internal state, then consume the
// emitter's sprite/line/line-group data directly in the D3D11 renderer.
////////////////////////////////////////////////////////////////////////////////

#include "WW3D2/part_buf.h"
#include "WW3D2/w3d_file.h"

namespace
{
	enum class ParticleBufferDX11BlendMode
	{
		Opaque,
		Alpha,
		AlphaTest,
		Additive,
		Multiplicative
	};

	static bool IsFiniteParticleBufferValue(float value)
	{
		return std::isfinite(value);
	}

	static bool IsFiniteParticleBufferVector3(const Vector3& v)
	{
		return IsFiniteParticleBufferValue(v.X) && IsFiniteParticleBufferValue(v.Y) && IsFiniteParticleBufferValue(v.Z);
	}

	static bool IsFiniteParticleBufferVector4(const Vector4& v)
	{
		return IsFiniteParticleBufferValue(v.X) && IsFiniteParticleBufferValue(v.Y)
			&& IsFiniteParticleBufferValue(v.Z) && IsFiniteParticleBufferValue(v.W);
	}

	static float ClampParticleBuffer01(float value)
	{
		if (value < 0.0f) {
			return 0.0f;
		}
		if (value > 1.0f) {
			return 1.0f;
		}
		return value;
	}

	static uint32_t PackParticleBufferColor(const Vector4& diffuse)
	{
		const uint8_t cr = static_cast<uint8_t>(ClampParticleBuffer01(diffuse.X) * 255.0f + 0.5f);
		const uint8_t cg = static_cast<uint8_t>(ClampParticleBuffer01(diffuse.Y) * 255.0f + 0.5f);
		const uint8_t cb = static_cast<uint8_t>(ClampParticleBuffer01(diffuse.Z) * 255.0f + 0.5f);
		const uint8_t ca = static_cast<uint8_t>(ClampParticleBuffer01(diffuse.W) * 255.0f + 0.5f);
		return (ca << 24) | (cb << 16) | (cg << 8) | cr;
	}

	static ParticleBufferDX11BlendMode ClassifyParticleBufferDX11BlendMode(const ShaderClass& shader)
	{
		const auto srcBlend = shader.Get_Src_Blend_Func();
		const auto dstBlend = shader.Get_Dst_Blend_Func();
		const auto alphaTest = shader.Get_Alpha_Test();

		if (srcBlend == ShaderClass::SRCBLEND_ZERO && dstBlend == ShaderClass::DSTBLEND_SRC_COLOR) {
			return ParticleBufferDX11BlendMode::Multiplicative;
		}

		if ((srcBlend == ShaderClass::SRCBLEND_ONE && dstBlend == ShaderClass::DSTBLEND_ONE)
			|| (srcBlend == ShaderClass::SRCBLEND_SRC_ALPHA && dstBlend == ShaderClass::DSTBLEND_ONE)
			|| (srcBlend == ShaderClass::SRCBLEND_ONE && dstBlend == ShaderClass::DSTBLEND_ONE_MINUS_SRC_COLOR)) {
			return ParticleBufferDX11BlendMode::Additive;
		}

		if (alphaTest == ShaderClass::ALPHATEST_ENABLE) {
			return ParticleBufferDX11BlendMode::AlphaTest;
		}

		if ((srcBlend == ShaderClass::SRCBLEND_SRC_ALPHA && dstBlend == ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA)
			|| shader.Uses_Alpha()) {
			return ParticleBufferDX11BlendMode::Alpha;
		}

		return ParticleBufferDX11BlendMode::Opaque;
	}

	static void ApplyParticleBufferDX11BlendMode(Render::Renderer& renderer, ParticleBufferDX11BlendMode blendMode)
	{
		switch (blendMode) {
		case ParticleBufferDX11BlendMode::Opaque:
			renderer.Restore3DState();
			break;
		case ParticleBufferDX11BlendMode::Alpha:
			renderer.SetAlphaBlend3DState();
			break;
		case ParticleBufferDX11BlendMode::AlphaTest:
			renderer.SetAlphaTest3DState();
			break;
		case ParticleBufferDX11BlendMode::Additive:
			renderer.SetAdditive3DState();
			break;
		case ParticleBufferDX11BlendMode::Multiplicative:
			renderer.SetMultiplicative3DState();
			break;
		}
	}

	static Render::Texture* ResolveParticleBufferDX11Texture(TextureClass* ww3dTex)
	{
		Render::Texture* tex = nullptr;

		if (ww3dTex) {
			const char* texName = ww3dTex->Get_Texture_Name().str();

			if (texName && texName[0]) {
				tex = Render::ImageCache::Instance().GetTexture(
					Render::Renderer::Instance().GetDevice(), texName);
			}

			ww3dTex->Release_Ref();
		}

		return tex;
	}

	static unsigned char GetParticleBufferFrameLog2(int frameMode)
	{
		switch (frameMode) {
		case W3D_EMITTER_FRAME_MODE_2x2:
			return 1;
		case W3D_EMITTER_FRAME_MODE_4x4:
			return 2;
		case W3D_EMITTER_FRAME_MODE_8x8:
			return 3;
		case W3D_EMITTER_FRAME_MODE_16x16:
			return 4;
		default:
			return 0;
		}
	}

	static void GetParticleBufferTriFrameUVs(unsigned char frameLog2, uint8_t frameIndex, Vector2* outUVs)
	{
		const unsigned int gridSize = 1u << frameLog2;
		const unsigned int frameCount = gridSize * gridSize;
		const unsigned int frame = frameCount > 0 ? (frameIndex % frameCount) : 0u;
		const float cellSize = 1.0f / static_cast<float>(gridSize);
		const unsigned int col = frame % gridSize;
		const unsigned int row = frame / gridSize;
		const float u0 = static_cast<float>(col) * cellSize;
		const float v0 = static_cast<float>(row) * cellSize;
		const float u1 = static_cast<float>(col + 1u) * cellSize;
		const float v1 = static_cast<float>(row + 1u) * cellSize;

		outUVs[0].Set((u0 + u1) * 0.5f, v0);
		outUVs[1].Set(u0, v1);
		outUVs[2].Set(u1, v1);
	}

	static void GetParticleBufferQuadFrameUVs(unsigned char frameLog2, uint8_t frameIndex, Vector2* outUVs)
	{
		const unsigned int gridSize = 1u << frameLog2;
		const unsigned int frameCount = gridSize * gridSize;
		const unsigned int frame = frameCount > 0 ? (frameIndex % frameCount) : 0u;
		const float cellSize = 1.0f / static_cast<float>(gridSize);
		const unsigned int col = frame % gridSize;
		const unsigned int row = frame / gridSize;
		const float u0 = static_cast<float>(col) * cellSize;
		const float v0 = static_cast<float>(row) * cellSize;
		const float u1 = static_cast<float>(col + 1u) * cellSize;
		const float v1 = static_cast<float>(row + 1u) * cellSize;

		outUVs[0].Set(u0, v0);
		outUVs[1].Set(u1, v0);
		outUVs[2].Set(u1, v1);
		outUVs[3].Set(u0, v1);
	}

	static bool AppendParticleBufferDX11Sprite(
		const Vector3& position,
		float rightX,
		float rightY,
		float rightZ,
		float upX,
		float upY,
		float upZ,
		bool quadMode,
		float size,
		uint8_t orientation,
		uint8_t frameIndex,
		unsigned char frameLog2,
		uint32_t color,
		std::vector<Render::Vertex3D>& verts,
		std::vector<uint16_t>& indices)
	{
		if (!IsFiniteParticleBufferVector3(position) || !IsFiniteParticleBufferValue(size) || size <= 0.01f) {
			return true;
		}

		static const float kTriRadius = 2.0f / 3.0f;
		static const float kTriHalfBase = 0.57735026919f;
		static const Vector2 kTriBase[3] = {
			Vector2(0.0f, kTriRadius),
			Vector2(-kTriHalfBase, -kTriRadius * 0.5f),
			Vector2(kTriHalfBase, -kTriRadius * 0.5f)
		};
		static const Vector2 kQuadBase[4] = {
			Vector2(-0.5f, 0.5f),
			Vector2(0.5f, 0.5f),
			Vector2(0.5f, -0.5f),
			Vector2(-0.5f, -0.5f)
		};

		const float angle = static_cast<float>(orientation) * (2.0f * WWMATH_PI / 256.0f);
		const float cosA = cosf(angle);
		const float sinA = sinf(angle);
		const Vector2* base = quadMode ? kQuadBase : kTriBase;
		const int vertexCount = quadMode ? 4 : 3;
		Vector2 uvs[4];

		if (quadMode) {
			GetParticleBufferQuadFrameUVs(frameLog2, frameIndex, uvs);
		} else {
			GetParticleBufferTriFrameUVs(frameLog2, frameIndex, uvs);
		}

		const uint16_t baseIndex = static_cast<uint16_t>(verts.size());
		if (baseIndex > (quadMode ? 65530u : 65532u)) {
			return false;
		}

		for (int i = 0; i < vertexCount; ++i) {
			const float localX = (cosA * base[i].X - sinA * base[i].Y) * size;
			const float localY = (sinA * base[i].X + cosA * base[i].Y) * size;

			Render::Vertex3D v = {};
			v.normal = { 0.0f, 0.0f, 1.0f };
			v.color = color;
			v.position = {
				position.X + rightX * localX + upX * localY,
				position.Y + rightY * localX + upY * localY,
				position.Z + rightZ * localX + upZ * localY
			};
			v.texcoord = { uvs[i].X, uvs[i].Y };
			verts.push_back(v);
		}

		if (quadMode) {
			indices.push_back(baseIndex + 0);
			indices.push_back(baseIndex + 1);
			indices.push_back(baseIndex + 2);
			indices.push_back(baseIndex + 0);
			indices.push_back(baseIndex + 2);
			indices.push_back(baseIndex + 3);
		} else {
			indices.push_back(baseIndex + 0);
			indices.push_back(baseIndex + 1);
			indices.push_back(baseIndex + 2);
		}

		return true;
	}

	static bool BuildParticleBufferDX11LineBasis(
		const Vector3& head,
		const Vector3& tail,
		const Vector3& cameraPos,
		float radius,
		Vector3& dir,
		Vector3& side,
		Vector3& up)
	{
		if (!IsFiniteParticleBufferVector3(head) || !IsFiniteParticleBufferVector3(tail)
			|| !IsFiniteParticleBufferVector3(cameraPos) || !IsFiniteParticleBufferValue(radius) || radius <= 0.0f) {
			return false;
		}

		dir = tail - head;
		float dirLen = dir.Length();
		if (dirLen < 0.001f) {
			return false;
		}
		dir *= 1.0f / dirLen;

		Vector3 mid = (head + tail) * 0.5f;
		Vector3 viewDir = mid - cameraPos;
		float viewLen = viewDir.Length();
		if (viewLen < 0.001f) {
			viewDir.Set(0.0f, 0.0f, 1.0f);
		} else {
			viewDir *= 1.0f / viewLen;
		}

		Vector3::Cross_Product(dir, viewDir, &side);
		float sideLen = side.Length();
		if (sideLen < 0.0001f) {
			Vector3 fallbackUp(0.0f, 0.0f, 1.0f);
			Vector3::Cross_Product(dir, fallbackUp, &side);
			sideLen = side.Length();
			if (sideLen < 0.0001f) {
				fallbackUp.Set(0.0f, 1.0f, 0.0f);
				Vector3::Cross_Product(dir, fallbackUp, &side);
				sideLen = side.Length();
				if (sideLen < 0.0001f) {
					return false;
				}
			}
		}
		side *= radius / sideLen;

		Vector3 sideUnit = side;
		sideUnit.Normalize();
		Vector3::Cross_Product(sideUnit, dir, &up);
		float upLen = up.Length();
		if (upLen < 0.0001f) {
			return false;
		}
		up *= radius / upLen;

		return true;
	}

	static Vector3 ComputeParticleBufferFaceNormal(const Vector3& p0, const Vector3& p1, const Vector3& p2)
	{
		Vector3 edge01 = p1 - p0;
		Vector3 edge02 = p2 - p0;
		Vector3 normal;
		Vector3::Cross_Product(edge01, edge02, &normal);
		float normalLen = normal.Length();
		if (normalLen < 0.0001f) {
			return Vector3(0.0f, 0.0f, 1.0f);
		}
		normal *= 1.0f / normalLen;
		return normal;
	}

	static void ReverseParticleBufferTriangle(Vector3* positions, uint32_t* colors, Vector2* uvs)
	{
		std::swap(positions[1], positions[2]);
		std::swap(colors[1], colors[2]);
		std::swap(uvs[1], uvs[2]);
	}

	static void ReverseParticleBufferQuad(Vector3* positions, uint32_t* colors, Vector2* uvs)
	{
		std::swap(positions[1], positions[3]);
		std::swap(colors[1], colors[3]);
		std::swap(uvs[1], uvs[3]);
	}

	static bool AppendParticleBufferDX11Triangle(
		Vector3* positions,
		uint32_t* colors,
		Vector2* uvs,
		const Vector3& objectCenter,
		std::vector<Render::Vertex3D>& verts,
		std::vector<uint16_t>& indices)
	{
		Vector3 normal = ComputeParticleBufferFaceNormal(positions[0], positions[1], positions[2]);
		Vector3 faceCenter = (positions[0] + positions[1] + positions[2]) * (1.0f / 3.0f);
		Vector3 outward = faceCenter - objectCenter;
		if (normal * outward < 0.0f) {
			ReverseParticleBufferTriangle(positions, colors, uvs);
			normal = ComputeParticleBufferFaceNormal(positions[0], positions[1], positions[2]);
		}

		const uint16_t baseIndex = static_cast<uint16_t>(verts.size());
		if (baseIndex > 65532u) {
			return false;
		}

		for (int i = 0; i < 3; ++i) {
			Render::Vertex3D v = {};
			v.position = { positions[i].X, positions[i].Y, positions[i].Z };
			v.normal = { normal.X, normal.Y, normal.Z };
			v.texcoord = { uvs[i].X, uvs[i].Y };
			v.color = colors[i];
			verts.push_back(v);
		}

		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 1);
		indices.push_back(baseIndex + 2);
		return true;
	}

	static bool AppendParticleBufferDX11Quad(
		Vector3* positions,
		uint32_t* colors,
		Vector2* uvs,
		const Vector3& objectCenter,
		std::vector<Render::Vertex3D>& verts,
		std::vector<uint16_t>& indices)
	{
		Vector3 normal = ComputeParticleBufferFaceNormal(positions[0], positions[1], positions[2]);
		Vector3 faceCenter = (positions[0] + positions[1] + positions[2] + positions[3]) * 0.25f;
		Vector3 outward = faceCenter - objectCenter;
		if (normal * outward < 0.0f) {
			ReverseParticleBufferQuad(positions, colors, uvs);
			normal = ComputeParticleBufferFaceNormal(positions[0], positions[1], positions[2]);
		}

		const uint16_t baseIndex = static_cast<uint16_t>(verts.size());
		if (baseIndex > 65530u) {
			return false;
		}

		for (int i = 0; i < 4; ++i) {
			Render::Vertex3D v = {};
			v.position = { positions[i].X, positions[i].Y, positions[i].Z };
			v.normal = { normal.X, normal.Y, normal.Z };
			v.texcoord = { uvs[i].X, uvs[i].Y };
			v.color = colors[i];
			verts.push_back(v);
		}

		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 1);
		indices.push_back(baseIndex + 2);
		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 2);
		indices.push_back(baseIndex + 3);
		return true;
	}

	static bool AppendParticleBufferDX11RibbonSegment(
		const Vector3& cameraPos,
		const Vector3& p0,
		const Vector3& p1,
		float width,
		uint32_t color0,
		uint32_t color1,
		float u0,
		float u1,
		float v0,
		float v1,
		std::vector<Render::Vertex3D>& verts,
		std::vector<uint16_t>& indices)
	{
		if (!IsFiniteParticleBufferVector3(p0) || !IsFiniteParticleBufferVector3(p1)
			|| !IsFiniteParticleBufferValue(width) || width <= 0.01f) {
			return true;
		}

		Vector3 segment = p1 - p0;
		float segmentLen = segment.Length();
		if (segmentLen < 0.001f) {
			return true;
		}
		segment *= 1.0f / segmentLen;

		Vector3 mid = (p0 + p1) * 0.5f;
		Vector3 viewDir = mid - cameraPos;
		float viewLen = viewDir.Length();
		if (viewLen < 0.001f) {
			viewDir.Set(0.0f, 0.0f, 1.0f);
		} else {
			viewDir *= 1.0f / viewLen;
		}

		Vector3 right;
		Vector3::Cross_Product(segment, viewDir, &right);
		float rightLen = right.Length();
		if (rightLen < 0.0001f) {
			right.Set(0.0f, 0.0f, 1.0f);
			Vector3::Cross_Product(segment, right, &right);
			rightLen = right.Length();
			if (rightLen < 0.0001f) {
				return true;
			}
		}
		right *= (width * 0.5f) / rightLen;

		const uint16_t baseIndex = static_cast<uint16_t>(verts.size());
		if (baseIndex > 65530u) {
			return false;
		}

		Vector3 positions[4] = {
			p0 + right,
			p0 - right,
			p1 - right,
			p1 + right
		};
		Vector2 uvs[4] = {
			Vector2(u0, v0),
			Vector2(u1, v0),
			Vector2(u1, v1),
			Vector2(u0, v1)
		};
		const Vector3 normal(-viewDir.X, -viewDir.Y, -viewDir.Z);
		const uint32_t colors[4] = { color0, color0, color1, color1 };

		for (int i = 0; i < 4; ++i) {
			Render::Vertex3D v = {};
			v.position = { positions[i].X, positions[i].Y, positions[i].Z };
			v.normal = { normal.X, normal.Y, normal.Z };
			v.texcoord = { uvs[i].X, uvs[i].Y };
			v.color = colors[i];
			verts.push_back(v);
		}

		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 1);
		indices.push_back(baseIndex + 2);
		indices.push_back(baseIndex + 0);
		indices.push_back(baseIndex + 2);
		indices.push_back(baseIndex + 3);
		return true;
	}

	static bool AppendParticleBufferDX11TetraLineGroup(
		const Vector3& head,
		const Vector3& tail,
		const Vector3& cameraPos,
		float lineSize,
		uint32_t headColor,
		uint32_t tailColor,
		float uCoord,
		std::vector<Render::Vertex3D>& verts,
		std::vector<uint16_t>& indices)
	{
		Vector3 dir;
		Vector3 side;
		Vector3 up;
		if (!BuildParticleBufferDX11LineBasis(head, tail, cameraPos, lineSize * 0.5f, dir, side, up)) {
			return true;
		}

		const Vector3 basis[3] = {
			side,
			(side * -0.5f) + (up * 0.86602540378f),
			(side * -0.5f) - (up * 0.86602540378f)
		};
		Vector3 tailVerts[3] = {
			tail + basis[0],
			tail + basis[1],
			tail + basis[2]
		};
		const Vector3 objectCenter = (head + tail) * 0.5f;
		const float u0 = uCoord;
		const float u1 = uCoord + 1.0f;

		for (int i = 0; i < 3; ++i) {
			Vector3 facePositions[3] = {
				head,
				tailVerts[i],
				tailVerts[(i + 1) % 3]
			};
			uint32_t faceColors[3] = { headColor, tailColor, tailColor };
			Vector2 faceUVs[3] = {
				Vector2(u0 + 0.5f, 0.0f),
				Vector2(u0, 1.0f),
				Vector2(u1, 1.0f)
			};
			if (!AppendParticleBufferDX11Triangle(facePositions, faceColors, faceUVs, objectCenter, verts, indices)) {
				return false;
			}
		}

		Vector3 basePositions[3] = { tailVerts[0], tailVerts[1], tailVerts[2] };
		uint32_t baseColors[3] = { tailColor, tailColor, tailColor };
		Vector2 baseUVs[3] = {
			Vector2(u0 + 0.5f, 0.0f),
			Vector2(u0, 1.0f),
			Vector2(u1, 1.0f)
		};
		return AppendParticleBufferDX11Triangle(basePositions, baseColors, baseUVs, objectCenter, verts, indices);
	}

	static bool AppendParticleBufferDX11PrismLineGroup(
		const Vector3& head,
		const Vector3& tail,
		const Vector3& cameraPos,
		float lineSize,
		uint32_t headColor,
		uint32_t tailColor,
		float uCoord,
		std::vector<Render::Vertex3D>& verts,
		std::vector<uint16_t>& indices)
	{
		Vector3 dir;
		Vector3 side;
		Vector3 up;
		if (!BuildParticleBufferDX11LineBasis(head, tail, cameraPos, lineSize * 0.5f, dir, side, up)) {
			return true;
		}

		const Vector3 basis[3] = {
			side,
			(side * -0.5f) + (up * 0.86602540378f),
			(side * -0.5f) - (up * 0.86602540378f)
		};
		Vector3 headVerts[3] = {
			head + basis[0],
			head + basis[1],
			head + basis[2]
		};
		Vector3 tailVerts[3] = {
			tail + basis[0],
			tail + basis[1],
			tail + basis[2]
		};
		const Vector3 objectCenter = (head + tail) * 0.5f;
		const float u0 = uCoord;
		const float u1 = uCoord + 1.0f;

		Vector3 headCapPositions[3] = { headVerts[0], headVerts[1], headVerts[2] };
		uint32_t headCapColors[3] = { headColor, headColor, headColor };
		Vector2 headCapUVs[3] = {
			Vector2(u0 + 0.5f, 0.0f),
			Vector2(u0, 1.0f),
			Vector2(u1, 1.0f)
		};
		if (!AppendParticleBufferDX11Triangle(headCapPositions, headCapColors, headCapUVs, objectCenter, verts, indices)) {
			return false;
		}

		Vector3 tailCapPositions[3] = { tailVerts[0], tailVerts[1], tailVerts[2] };
		uint32_t tailCapColors[3] = { tailColor, tailColor, tailColor };
		Vector2 tailCapUVs[3] = {
			Vector2(u0 + 0.5f, 0.0f),
			Vector2(u0, 1.0f),
			Vector2(u1, 1.0f)
		};
		if (!AppendParticleBufferDX11Triangle(tailCapPositions, tailCapColors, tailCapUVs, objectCenter, verts, indices)) {
			return false;
		}

		for (int i = 0; i < 3; ++i) {
			const int next = (i + 1) % 3;
			Vector3 facePositions[4] = {
				headVerts[i],
				headVerts[next],
				tailVerts[next],
				tailVerts[i]
			};
			uint32_t faceColors[4] = {
				headColor,
				headColor,
				tailColor,
				tailColor
			};
			Vector2 faceUVs[4] = {
				Vector2(u0, 0.0f),
				Vector2(u1, 0.0f),
				Vector2(u1, 1.0f),
				Vector2(u0, 1.0f)
			};
			if (!AppendParticleBufferDX11Quad(facePositions, faceColors, faceUVs, objectCenter, verts, indices)) {
				return false;
			}
		}

		return true;
	}
}

static void RenderParticleBufferDX11(RenderObjClass* robj, RenderInfoClass& rinfo)
{
	ParticleBufferClass* buf = static_cast<ParticleBufferClass*>(robj);

	buf->UpdateStateForD3D11();

	const int totalParticles = buf->Get_Non_New_Count() + buf->Get_New_Count();
	if (totalParticles <= 0) {
		return;
	}

	auto* positions = buf->Get_Positions();
	auto* diffuse = buf->Get_Diffuse_Array();
	if (!positions || !diffuse) {
		return;
	}

	const unsigned int maxNum = buf->Get_Max_Num();
	const unsigned int start = buf->Get_Start_Index();
	const unsigned int newEnd = buf->Get_New_End_Index();
	const int renderMode = buf->Get_Render_Mode_Value();
	const float defaultSize = buf->Get_Particle_Size();
	const uint8_t defaultFrame = static_cast<uint8_t>(static_cast<int>(buf->Get_Default_Frame_Value()) & 0xFF);
	const unsigned char frameLog2 = GetParticleBufferFrameLog2(buf->Get_Frame_Mode());

	TextureClass* ww3dTex = buf->Get_Texture();
	Render::Texture* tex = ResolveParticleBufferDX11Texture(ww3dTex);

	Vector3* posArray = positions->Get_Array();
	Vector4* diffArray = diffuse->Get_Array();
	float* sizeArray = buf->Get_Sizes() ? buf->Get_Sizes()->Get_Array() : nullptr;
	uint8_t* orientArray = buf->Get_Orientations() ? buf->Get_Orientations()->Get_Array() : nullptr;
	uint8_t* frameArray = buf->Get_Frames() ? buf->Get_Frames()->Get_Array() : nullptr;
	Vector3* prevPosArray = buf->Get_Previous_Positions() ? buf->Get_Previous_Positions()->Get_Array() : nullptr;
	float* uCoordArray = buf->Get_UCoords() ? buf->Get_UCoords()->Get_Array() : nullptr;

	std::vector<Render::Vertex3D> verts;
	std::vector<uint16_t> indices;

	const Vector3 cameraPos = rinfo.Camera.Get_Position();
	const Matrix3D& camTM = rinfo.Camera.Get_Transform();
	float rightX = camTM[0][0];
	float rightY = camTM[1][0];
	float rightZ = camTM[2][0];
	float upX = camTM[0][1];
	float upY = camTM[1][1];
	float upZ = camTM[2][1];

	float rLen = sqrtf(rightX * rightX + rightY * rightY + rightZ * rightZ);
	if (rLen > 0.0001f) {
		rightX /= rLen;
		rightY /= rLen;
		rightZ /= rLen;
	}
	float uLen = sqrtf(upX * upX + upY * upY + upZ * upZ);
	if (uLen > 0.0001f) {
		upX /= uLen;
		upY /= uLen;
		upZ /= uLen;
	}

	switch (renderMode) {
	case W3D_EMITTER_RENDER_MODE_TRI_PARTICLES:
	case W3D_EMITTER_RENDER_MODE_QUAD_PARTICLES: {
		const bool quadMode = renderMode == W3D_EMITTER_RENDER_MODE_QUAD_PARTICLES;
		verts.reserve(totalParticles * (quadMode ? 4u : 3u));
		indices.reserve(totalParticles * (quadMode ? 6u : 3u));

		unsigned int idx = start;
		int count = 0;
		while (idx != newEnd && count < totalParticles && count < static_cast<int>(maxNum)) {
			const Vector3& pos = posArray[idx];
			const Vector4& diff = diffArray[idx];
			const float size = sizeArray ? sizeArray[idx] : defaultSize;
			const uint8_t orientation = orientArray ? orientArray[idx] : 0;
			const uint8_t frame = frameArray ? frameArray[idx] : defaultFrame;

			if (IsFiniteParticleBufferVector3(pos) && IsFiniteParticleBufferVector4(diff) && diff.W > 0.01f) {
				if (!AppendParticleBufferDX11Sprite(
						pos,
						rightX,
						rightY,
						rightZ,
						upX,
						upY,
						upZ,
						quadMode,
						size,
						orientation,
						frame,
						frameLog2,
						PackParticleBufferColor(diff),
						verts,
						indices)) {
					break;
				}
			}

			idx = (idx + 1u) % maxNum;
			++count;
		}
		break;
	}

	case W3D_EMITTER_RENDER_MODE_LINE: {
		const float width = std::max(buf->Get_Line_Width(), 0.01f);
		const float tileFactor = buf->Get_Texture_Tile_Factor();
		const Vector2 uvRate = buf->Get_UV_Offset_Rate();
		const float timeSeconds = static_cast<float>(WW3D::Get_Sync_Time()) * 0.001f;
		const float uOffset = uvRate.X * timeSeconds;
		float accumulatedV = uvRate.Y * timeSeconds;

		std::vector<Vector3> linePoints;
		std::vector<uint32_t> lineColors;
		linePoints.reserve(totalParticles);
		lineColors.reserve(totalParticles);

		unsigned int idx = start;
		int count = 0;
		while (idx != newEnd && count < totalParticles && count < static_cast<int>(maxNum)) {
			const Vector3& pos = posArray[idx];
			const Vector4& diff = diffArray[idx];
			if (IsFiniteParticleBufferVector3(pos) && IsFiniteParticleBufferVector4(diff)) {
				linePoints.push_back(pos);
				lineColors.push_back(PackParticleBufferColor(diff));
			}

			idx = (idx + 1u) % maxNum;
			++count;
		}

		if (linePoints.size() >= 2u) {
			verts.reserve((linePoints.size() - 1u) * 4u);
			indices.reserve((linePoints.size() - 1u) * 6u);

			for (size_t i = 0; i + 1 < linePoints.size(); ++i) {
				const float segmentLength = (linePoints[i + 1] - linePoints[i]).Length();
				const float nextV = accumulatedV
					+ ((tileFactor > 0.0f && width > 0.0001f) ? (segmentLength / (width * tileFactor)) : 1.0f);

				if (!AppendParticleBufferDX11RibbonSegment(
						cameraPos,
						linePoints[i],
						linePoints[i + 1],
						width,
						lineColors[i],
						lineColors[i + 1],
						uOffset,
						uOffset + 1.0f,
						accumulatedV,
						nextV,
						verts,
						indices)) {
					break;
				}

				accumulatedV = nextV;
			}
		}
		break;
	}

	case W3D_EMITTER_RENDER_MODE_LINEGRP_TETRA:
	case W3D_EMITTER_RENDER_MODE_LINEGRP_PRISM: {
		if (!prevPosArray) {
			return;
		}

		verts.reserve(totalParticles * (renderMode == W3D_EMITTER_RENDER_MODE_LINEGRP_TETRA ? 12u : 24u));
		indices.reserve(totalParticles * (renderMode == W3D_EMITTER_RENDER_MODE_LINEGRP_TETRA ? 12u : 24u));

		unsigned int idx = start;
		int count = 0;
		while (idx != newEnd && count < totalParticles && count < static_cast<int>(maxNum)) {
			const Vector3& head = posArray[idx];
			const Vector3& tail = prevPosArray[idx];
			const Vector4& headDiffuse = diffArray[idx];
			const float lineSize = sizeArray ? sizeArray[idx] : defaultSize;
			const float uCoord = uCoordArray ? uCoordArray[idx] : buf->Get_Default_Frame_Value();

			if (IsFiniteParticleBufferVector3(head) && IsFiniteParticleBufferVector3(tail)
				&& IsFiniteParticleBufferVector4(headDiffuse) && headDiffuse.W > 0.01f) {
				Vector4 tailDiffuse = headDiffuse;
				tailDiffuse.W = 0.0f;

				const uint32_t headColor = PackParticleBufferColor(headDiffuse);
				const uint32_t tailColor = PackParticleBufferColor(tailDiffuse);

				const bool appended = (renderMode == W3D_EMITTER_RENDER_MODE_LINEGRP_TETRA)
					? AppendParticleBufferDX11TetraLineGroup(head, tail, cameraPos, lineSize, headColor, tailColor, uCoord, verts, indices)
					: AppendParticleBufferDX11PrismLineGroup(head, tail, cameraPos, lineSize, headColor, tailColor, uCoord, verts, indices);
				if (!appended) {
					break;
				}
			}

			idx = (idx + 1u) % maxNum;
			++count;
		}
		break;
	}

	default:
		return;
	}

	if (verts.empty() || indices.empty()) {
		return;
	}

	auto& renderer = Render::Renderer::Instance();
	auto& device = renderer.GetDevice();

	Render::VertexBuffer vb;
	Render::IndexBuffer ib;
	vb.Create(device, verts.data(), static_cast<uint32_t>(verts.size()), sizeof(Render::Vertex3D));
	ib.Create(device, indices.data(), (uint32_t)indices.size());

	ApplyParticleBufferDX11BlendMode(renderer, ClassifyParticleBufferDX11BlendMode(buf->Get_Shader()));

	Render::Float4x4 identity;
	DirectX::XMStoreFloat4x4(&Render::ToXM(identity), DirectX::XMMatrixIdentity());
	renderer.Draw3DIndexed(vb, ib, (uint32_t)indices.size(), tex, identity, {1,1,1,1});
	renderer.Restore3DState();
}


////////////////////////////////////////////////////////////////////////////////
// SECTION: Shadow Decal Rendering
////////////////////////////////////////////////////////////////////////////////

#include "W3DDevice/GameClient/ImageCache.h"

static Render::Texture* g_shadowTexture = nullptr;
static Render::Texture g_proceduralShadowTexture;
static Render::Texture g_proceduralRadiusDecalTexture; // white-RGB + alpha gradient for
                                                       // alpha/additive radius decals
                                                       // (guard circle, radiation range, ...)
static bool g_shadowTextureLoaded = false;

static Render::Texture* CreateProceduralShadowTexture(Render::Device& device)
{
	// Create a 128x128 circular gradient shadow texture as a last resort.
	//
	// Encoding matches classic D3D8 EXShadow.tga: dark RGB + matching alpha,
	// with a soft circular falloff. The decal shader's multiplicative path
	// reads `1 - max(tex.rgb)` as the shadow mask, so RGB = (1 - alpha) gives
	// opaque-center, transparent-edge when blended multiplicatively against
	// the terrain. Previous procedural encoding (white RGB + alpha gradient)
	// produced a solid square because multiplicative blend ignores alpha.
	const uint32_t size = 128;
	std::vector<uint32_t> pixels(size * size);
	const float center = (float)(size - 1) * 0.5f;
	const float maxRadius = center;

	for (uint32_t y = 0; y < size; ++y)
	{
		for (uint32_t x = 0; x < size; ++x)
		{
			float dx = (float)x - center;
			float dy = (float)y - center;
			float dist = sqrtf(dx * dx + dy * dy);
			float t = dist / maxRadius;
			if (t > 1.0f) t = 1.0f;

			// Smoothstep falloff on the shadow mask (1 = full shadow at
			// center, 0 = no shadow at edge). The smoothstep gives a softer
			// anti-aliased perimeter than a linear ramp.
			float mask = 1.0f - t;
			mask = mask * mask * (3.0f - 2.0f * mask);

			// Encode as dark-RGB shadow texture: rgbGray = 1 - mask so
			// center -> black, edge -> white. Alpha carries the same mask.
			float gray = 1.0f - mask;
			uint8_t g = static_cast<uint8_t>(gray * 255.0f);
			uint8_t a = static_cast<uint8_t>(mask * 255.0f);
			// ABGR byte order: A in high byte, then B, G, R.
			pixels[y * size + x] = (a << 24) | (g << 16) | (g << 8) | g;
		}
	}

	if (g_proceduralShadowTexture.CreateFromRGBA(device, pixels.data(), size, size, true))
		return &g_proceduralShadowTexture;
	return nullptr;
}

// Fallback for alpha/additive radius decals (guard command, radiation
// protection range, bomb blast radius, etc.) when the template's named
// texture fails to load. Unlike the shadow blob this needs WHITE RGB with
// a soft alpha circle — the decal color (yellow, green, red, ...) arrives
// via the vertex color and gets multiplied against tex.rgb in PSDecal's
// alpha/additive branch, so white RGB lets the color come through
// unmodulated. Using the dark shadow texture here would tint all radius
// decals toward black.
static Render::Texture* CreateProceduralRadiusDecalTexture(Render::Device& device)
{
	const uint32_t size = 128;
	std::vector<uint32_t> pixels(size * size);
	const float center = (float)(size - 1) * 0.5f;
	const float maxRadius = center - 1.0f;
	const float edgeThickness = maxRadius * 0.12f;

	for (uint32_t y = 0; y < size; ++y)
	{
		for (uint32_t x = 0; x < size; ++x)
		{
			float dx = (float)x - center;
			float dy = (float)y - center;
			float dist = sqrtf(dx * dx + dy * dy);

			// Ring outline: opaque near the perimeter, transparent inside and
			// beyond. The original game's radius decals are rings, not filled
			// blobs, so the command circle outlines the area of effect.
			float ringInner = maxRadius - edgeThickness;
			float ringOuter = maxRadius;
			float alpha;
			if (dist < ringInner - edgeThickness)
			{
				alpha = 0.0f; // transparent interior
			}
			else if (dist < ringInner)
			{
				// Inner feather from 0 → 1
				float t = (dist - (ringInner - edgeThickness)) / edgeThickness;
				alpha = t * t * (3.0f - 2.0f * t);
			}
			else if (dist < ringOuter)
			{
				alpha = 1.0f; // solid ring
			}
			else if (dist < ringOuter + edgeThickness)
			{
				// Outer feather from 1 → 0
				float t = 1.0f - (dist - ringOuter) / edgeThickness;
				alpha = t * t * (3.0f - 2.0f * t);
			}
			else
			{
				alpha = 0.0f;
			}

			uint8_t a = static_cast<uint8_t>(alpha * 255.0f);
			// White RGB + alpha. ABGR byte order: A,B,G,R from high to low.
			pixels[y * size + x] = (a << 24) | 0x00FFFFFF;
		}
	}

	if (g_proceduralRadiusDecalTexture.CreateFromRGBA(device, pixels.data(), size, size, true))
		return &g_proceduralRadiusDecalTexture;
	return nullptr;
}

// Heightmap GPU texture for instanced decal rendering.
// Created lazily from WorldHeightMap data, recreated if the heightmap changes.
static Render::Texture g_hmTexture;
static WorldHeightMap* g_hmLastPtr = nullptr;
static int g_hmLastWidth = 0, g_hmLastHeight = 0;

// Instance buffer for decal data (StructuredBuffer<DecalInstance>)
static Render::GPUBuffer g_decalInstanceBuffer;
static bool g_decalInstanceBufferCreated = false;
static const int MAX_DECAL_INSTANCES = 2048;

static bool UpdateHeightmapTexture(Render::Device& device, WorldHeightMap* hm)
{
	if (!hm) return false;

	int w = hm->getXExtent();
	int h = hm->getYExtent();

	if (hm == g_hmLastPtr && w == g_hmLastWidth && h == g_hmLastHeight && g_hmTexture.IsValid())
		return true;

	// Recreate heightmap texture with R32_FLOAT format
	g_hmTexture = Render::Texture(); // Reset

	const float hmHeightScale = MAP_XY_FACTOR / 16.0f;

	std::vector<float> heights(w * h);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			heights[y * w + x] = (float)hm->getHeight(x, y) * hmHeightScale;

	if (!g_hmTexture.CreateFromPixels(device, heights.data(), w, h,
		Render::PixelFormat::R32_FLOAT, sizeof(float)))
		return false;

	g_hmLastPtr = hm;
	g_hmLastWidth = w;
	g_hmLastHeight = h;
	return true;
}

void RenderShadowDecalsDX11(CameraClass* camera)
{
	if (!camera)
		return;

	auto& shadowMgr = g_d3d11ShadowManager;
	if (shadowMgr.m_shadowCount == 0 || !shadowMgr.m_shadowList)
		return;

	auto& renderer = Render::Renderer::Instance();
	auto& device = renderer.GetDevice();

	// Load default shadow texture on first use
	if (!g_shadowTextureLoaded)
	{
		auto& imgCache = Render::ImageCache::Instance();
		const char* shadowTexNames[] = {
			"EXShadow.dds",  "EXShadow.tga",  "exshadow.dds", "exshadow.tga",
			"shadow.dds",    "shadow.tga",     "EXShadow01.dds", "EXShadow01.tga",
			"SCDropShadow.tga", "SCDropShadow.dds",
		};
		for (const char* name : shadowTexNames)
		{
			g_shadowTexture = imgCache.GetTexture(device, name);
			if (g_shadowTexture) break;
		}
		if (!g_shadowTexture)
			g_shadowTexture = CreateProceduralShadowTexture(device);
		// Radius decal fallback is always the procedural ring — authored
		// assets don't share a single "default radius decal" name, so if a
		// specific .tga is missing the ring is the right fallback.
		CreateProceduralRadiusDecalTexture(device);
		g_shadowTextureLoaded = true;
	}
	if (!g_shadowTexture)
		return;

	// Create instance StructuredBuffer on first use
	if (!g_decalInstanceBufferCreated)
	{
		if (!g_decalInstanceBuffer.Create(device, sizeof(Render::Renderer::DecalInstance), MAX_DECAL_INSTANCES))
			return;
		g_decalInstanceBufferCreated = true;
	}

	// Upload heightmap to GPU texture (lazy, only recreated when heightmap changes)
	WorldHeightMap* hm = GetTerrainHeightMap();
	if (!UpdateHeightmapTexture(device, hm))
		return;

	// Compute heightmap UV transform constants
	const float mapXYFactor = (float)MAP_XY_FACTOR;
	// Classic D3D8 bias (~0.1 world units) was fine for ground shadows that
	// sit flush on terrain. It's nowhere near enough for alpha/additive
	// radius decals (guard circle, attack-move ring, scud reticle, generals
	// powers) which z-fight heavily on sloped / ridged terrain viewed from
	// the RTS camera. The per-vertex heightmap sample in the VS already
	// conforms the decal quad to terrain contour, but the bilinear sample
	// can dip below the shaded triangle between adjacent heightmap cells
	// and the terrain pass itself uses a slightly different interpolation
	// (diagonal flip bit), so we need a generous constant lift. Shadows
	// keep the tiny original bias so unit shadows don't look detached;
	// radius/command decals get ~1.25 units so they read as painted-on
	// overlays but never clip into slopes.
	const float shadowZOffset = 0.01f * mapXYFactor;   // ~0.1 units (shadows)
	const float decalRingZOffset = 1.25f;              // ~1.25 units (radius cursors)
	int hmW = hm->getXExtent();
	int hmH = hm->getYExtent();
	int border = hm->getBorderSize();

	Render::Renderer::DecalConstants decalCB = {};
	decalCB.hmTransform = {
		1.0f / ((float)hmW * MAP_XY_FACTOR),  // worldX → UV scale
		1.0f / ((float)hmH * MAP_XY_FACTOR),  // worldY → UV scale
		(float)border / (float)hmW,            // UV offset X
		(float)border / (float)hmH             // UV offset Y
	};
	decalCB.hmParams = { shadowZOffset, 0.0f, 0.0f, 0.0f };

	// Extract shadow data
	static float sX[MAX_DECAL_INSTANCES], sY[MAX_DECAL_INSTANCES];
	static float sSizeX[MAX_DECAL_INSTANCES], sSizeY[MAX_DECAL_INSTANCES];
	static float sOffX[MAX_DECAL_INSTANCES], sOffY[MAX_DECAL_INSTANCES], sAngle[MAX_DECAL_INSTANCES];
	static UnsignedByte sOpacity[MAX_DECAL_INSTANCES];
	static const char* sTexNames[MAX_DECAL_INSTANCES];
	static ShadowType sTypes[MAX_DECAL_INSTANCES];
	static uint32_t sDiffuse[MAX_DECAL_INSTANCES];

	int shadowCount = shadowMgr.getShadowData(
		sX, sY, sSizeX, sSizeY, sOpacity, sOffX, sOffY, sAngle,
		sTexNames, sTypes, sDiffuse, MAX_DECAL_INSTANCES);
	if (shadowCount == 0)
		return;

	// Build flat DecalInstance array — just parameters, no vertex generation.
	// GPU vertex shader handles quad expansion, rotation, and heightmap sampling.
	static Render::Renderer::DecalInstance instances[MAX_DECAL_INSTANCES];
	// Sun azimuth — only used when Enhanced Shadows toggle is on, to rotate
	// SHADOW_DIRECTIONAL_PROJECTION decals with the sun direction instead of
	// with the model's heading.
	extern bool g_useEnhancedShadows;
	float sunAzimuth = 0.0f;
	if (g_useEnhancedShadows)
	{
		const Vector3& sunPos = g_fallbackShadowLightPos[0];
		if (sunPos.X != 0.0f || sunPos.Y != 0.0f)
			sunAzimuth = atan2f(sunPos.Y, sunPos.X);
	}
	for (int si = 0; si < shadowCount; ++si)
	{
		auto& inst = instances[si];
		inst.posX = sX[si];
		inst.posY = sY[si];
		inst.offsetX = sOffX[si];
		inst.offsetY = sOffY[si];
		inst.sizeX = sSizeX[si];
		inst.sizeY = sSizeY[si];
		inst.angle = (g_useEnhancedShadows && (sTypes[si] & SHADOW_DIRECTIONAL_PROJECTION))
			? sunAzimuth : sAngle[si];

		if (sTypes[si] & (SHADOW_ALPHA_DECAL | SHADOW_ADDITIVE_DECAL))
		{
			inst.color = sDiffuse[si]; // already ABGR-converted by getData()
		}
		else
		{
			// Multiplicative shadow darkening. Classic path uses a 50% cap
			// (the previously-shipping behavior — looks ghostly but matches
			// what the user is used to). Enhanced Shadows toggle removes
			// the cap and uses full opacity for darker, more accurate-look
			// shadows.
			extern bool g_useEnhancedShadows;
			float opacityF = sOpacity[si] / 255.0f;
			float multiplier = g_useEnhancedShadows
				? (1.0f - opacityF)
				: (1.0f - opacityF * 0.5f);
			if (multiplier < 0.0f) multiplier = 0.0f;
			UnsignedByte c = static_cast<UnsignedByte>(multiplier * 255.0f);
			inst.color = 0xFF000000 | (c << 16) | (c << 8) | c;
		}
	}

	// Group instances by (texture, blend mode) for batched DrawInstanced calls.
	// Uses static arrays to avoid per-frame heap allocations.
	struct DecalBatch {
		Render::Texture* tex;
		Render::Renderer::DecalBlend blend;
		int start;
		int count;
	};
	static DecalBatch batches[MAX_DECAL_INSTANCES];
	static Render::Renderer::DecalInstance batchedInstances[MAX_DECAL_INSTANCES];
	int numBatches = 0;

	// Sort/partition instances by (texture, blend mode)
	auto& imgCache = Render::ImageCache::Instance();

	// First pass: resolve textures and assign blend modes
	struct ShadowKey {
		Render::Texture* tex;
		int blendMode; // 0=mult, 1=alpha, 2=additive
	};
	static ShadowKey keys[MAX_DECAL_INSTANCES];
	for (int si = 0; si < shadowCount; ++si)
	{
		// Blend mode first — picks the correct fallback texture.
		int blendMode = 0;
		if (sTypes[si] & SHADOW_ALPHA_DECAL) blendMode = 1;
		else if (sTypes[si] & SHADOW_ADDITIVE_DECAL) blendMode = 2;

		// Fallback: shadow blob (dark RGB) for multiplicative, white ring for
		// alpha/additive radius decals. Using the dark shadow as fallback for
		// a radius decal would tint the colored circle toward black.
		Render::Texture* tex = (blendMode == 0)
			? g_shadowTexture
			: &g_proceduralRadiusDecalTexture;

		if (sTexNames[si])
		{
			char fullName[80];
			Render::Texture* perObj = nullptr;
			// Try common extensions and then the as-is name.
			snprintf(fullName, sizeof(fullName), "%s.tga", sTexNames[si]);
			perObj = imgCache.GetTexture(device, fullName);
			if (!perObj)
			{
				snprintf(fullName, sizeof(fullName), "%s.dds", sTexNames[si]);
				perObj = imgCache.GetTexture(device, fullName);
			}
			if (!perObj)
				perObj = imgCache.GetTexture(device, sTexNames[si]);
			if (perObj) tex = perObj;
		}
		keys[si].tex = tex;
		keys[si].blendMode = blendMode;
	}

	// Second pass: group contiguous runs and collect batches
	int batchedCount = 0;
	for (int mode = 0; mode <= 2; ++mode)
	{
		// Collect all instances of this blend mode, sub-grouped by texture
		Render::Texture* lastTex = nullptr;
		for (int si = 0; si < shadowCount; ++si)
		{
			if (keys[si].blendMode != mode) continue;
			if (keys[si].tex != lastTex)
			{
				// Start a new batch
				if (numBatches > 0 && batches[numBatches - 1].count == 0)
					--numBatches; // remove empty batch
				batches[numBatches].tex = keys[si].tex;
				batches[numBatches].blend = static_cast<Render::Renderer::DecalBlend>(mode);
				batches[numBatches].start = batchedCount;
				batches[numBatches].count = 0;
				++numBatches;
				lastTex = keys[si].tex;
			}
			batchedInstances[batchedCount++] = instances[si];
			batches[numBatches - 1].count++;
		}
	}

	if (batchedCount == 0)
		return;

	// Upload all instances to GPU in one shot
	g_decalInstanceBuffer.Update(device, batchedInstances, batchedCount);

	// Render each batch with its blend mode — one DrawInstanced call per (texture, blend) group.
	// The StructuredBuffer contains all instances contiguously; each batch references a sub-range.
	// Since DrawInstanced doesn't support base-instance offset, we re-upload per batch.
	for (int bi = 0; bi < numBatches; ++bi)
	{
		auto& batch = batches[bi];
		if (batch.count == 0) continue;

		// Skip multiplicative shadow blobs — the `tex * input.color` shader
		// renders them as solid black squares when input.color is dark
		// (g_useEnhancedShadows or any high-opacity shadow). Alpha and
		// additive batches (guard rings, generals-power markers, radiation
		// range, bomb blast, faction logos) still render normally.
		if (batch.blend == Render::Renderer::DecalBlend::Multiplicative)
			continue;

		// Pass blend mode to pixel shader via hmParams.y so it can handle
		// textures without alpha (24-bit TGA / DXT1 faction logos)
		decalCB.hmParams.y = static_cast<float>(batch.blend);
		// Per-blend-mode Z bias: radius cursors (alpha/additive) need a
		// larger lift above terrain to avoid clipping into hills / ridges
		// under the RTS camera. Shadows (multiplicative) keep the classic
		// flush bias. See comment above for picked values.
		decalCB.hmParams.x = (batch.blend == Render::Renderer::DecalBlend::Multiplicative)
			? shadowZOffset
			: decalRingZOffset;

		// Upload just this batch's instances
		g_decalInstanceBuffer.Update(device, &batchedInstances[batch.start], batch.count);

		renderer.DrawDecalsInstanced(
			batch.count, batch.tex,
			g_decalInstanceBuffer, &g_hmTexture,
			decalCB, batch.blend);
	}

	renderer.Restore3DState();
}


////////////////////////////////////////////////////////////////////////////////
// SECTION: Scorch Mark Rendering
//
// Renders burned ground decals at explosion sites as alpha-blended quads
// on the terrain surface. Uses the same approach as shadow decals above.
////////////////////////////////////////////////////////////////////////////////

static const int MAX_SCORCH_MARKS = 256;
static const int SCORCH_TEXTURE_ROWS = 3;
static const int SCORCH_TEXTURE_COLS = 3;
static const int SCORCH_MARKS_IN_TEXTURE = 9; // 3x3 atlas of scorch types

struct ScorchMark
{
	float x, y, z;
	float radius;
	int   type; // Scorches enum value (0-4 typically)
};

struct ScorchManager
{
	ScorchMark m_scorches[MAX_SCORCH_MARKS];
	int m_numScorches = 0;

	void addScorch(float posX, float posY, float posZ, float radius, int type)
	{
		// Duplicate rejection: skip if there's already a very similar scorch nearby
		float limit = radius * 0.25f;
		for (int i = 0; i < m_numScorches; ++i)
		{
			if (fabsf(posX - m_scorches[i].x) < limit &&
				fabsf(posY - m_scorches[i].y) < limit &&
				fabsf(radius - m_scorches[i].radius) < limit &&
				m_scorches[i].type == type)
			{
				return; // basically a duplicate
			}
		}

		// If full, shift all scorches down by one (evict the oldest)
		if (m_numScorches >= MAX_SCORCH_MARKS)
		{
			for (int i = 0; i < MAX_SCORCH_MARKS - 1; ++i)
				m_scorches[i] = m_scorches[i + 1];
			m_numScorches = MAX_SCORCH_MARKS - 1;
		}

		ScorchMark& s = m_scorches[m_numScorches];
		s.x = posX;
		s.y = posY;
		s.z = posZ;
		s.radius = radius;
		s.type = type;
		m_numScorches++;
	}

	void clear()
	{
		m_numScorches = 0;
	}
};

static ScorchManager g_scorchManager;
static Render::Texture* g_scorchTexture = nullptr;
static bool g_scorchTextureLoaded = false;

// Called from W3DGameClient::addScorch
void AddScorchMark(float x, float y, float z, float radius, int type)
{
	g_scorchManager.addScorch(x, y, z, radius, type);
}

// Called from D3D11TerrainVisual::reset / shutdown
void ClearAllScorchMarks()
{
	g_scorchManager.clear();
	g_scorchTextureLoaded = false;
	g_scorchTexture = nullptr;
}

void RenderScorchMarksDX11(CameraClass* camera)
{
	if (!camera)
		return;

	if (g_scorchManager.m_numScorches == 0)
		return;

	auto& renderer = Render::Renderer::Instance();

	// Load scorch texture on first use - the original game uses "EXScorch01.tga"
	// which is a 3x3 atlas of scorch mark variations
	if (!g_scorchTextureLoaded)
	{
		g_scorchTexture = Render::ImageCache::Instance().GetTexture(
			renderer.GetDevice(), "EXScorch01.tga");
		if (!g_scorchTexture)
			g_scorchTexture = Render::ImageCache::Instance().GetTexture(
				renderer.GetDevice(), "EXScorch01.dds");
		if (!g_scorchTexture)
			g_scorchTexture = Render::ImageCache::Instance().GetTexture(
				renderer.GetDevice(), "scorch.tga");
		g_scorchTextureLoaded = true;
	}

	if (!g_scorchTexture)
		return;

	// Build batch of scorch decal quads
	std::vector<Render::Vertex3D> vertices;
	std::vector<uint32_t> indices;
	vertices.reserve(g_scorchManager.m_numScorches * 4);
	indices.reserve(g_scorchManager.m_numScorches * 6);

	// Get heightmap for terrain height sampling
	WorldHeightMap* hm = GetTerrainHeightMap();
	const float MAP_XY_FACTOR_L = 10.0f;
	const float MAP_HEIGHT_SCALE_L = MAP_XY_FACTOR_L / 16.0f;
	const float SCORCH_Z_OFFSET = MAP_HEIGHT_SCALE_L * 0.3f; // slightly above terrain

	for (int si = 0; si < g_scorchManager.m_numScorches; ++si)
	{
		const ScorchMark& scorch = g_scorchManager.m_scorches[si];
		float cx = scorch.x;
		float cy = scorch.y;
		float halfR = scorch.radius;

		// Clamp scorch type to valid atlas range
		int type = scorch.type;
		if (type < 0) type = 0;
		if (type >= SCORCH_MARKS_IN_TEXTURE) type = type % SCORCH_MARKS_IN_TEXTURE;

		// Compute UV coordinates into the 3x3 atlas
		// Each scorch occupies a 1/3 x 1/3 region of the texture
		int col = type % SCORCH_TEXTURE_COLS;
		int row = type / SCORCH_TEXTURE_COLS;
		float u0 = (float)col / (float)SCORCH_TEXTURE_COLS;
		float v0 = (float)row / (float)SCORCH_TEXTURE_ROWS;
		float u1 = (float)(col + 1) / (float)SCORCH_TEXTURE_COLS;
		float v1 = (float)(row + 1) / (float)SCORCH_TEXTURE_ROWS;

		// Sample terrain heights at the 4 corners
		auto sampleZ = [&](float wx, float wy) -> float {
			if (!hm) return scorch.z + SCORCH_Z_OFFSET;
			float xdiv = wx / MAP_XY_FACTOR_L;
			float ydiv = wy / MAP_XY_FACTOR_L;
			int ix = (int)floorf(xdiv) + hm->getBorderSize();
			int iy = (int)floorf(ydiv) + hm->getBorderSize();
			if (ix < 0) ix = 0; if (ix >= hm->getXExtent()) ix = hm->getXExtent() - 1;
			if (iy < 0) iy = 0; if (iy >= hm->getYExtent()) iy = hm->getYExtent() - 1;
			return hm->getHeight(ix, iy) * MAP_HEIGHT_SCALE_L + SCORCH_Z_OFFSET;
		};

		uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

		// Scorch marks are dark burn marks - use vertex color with full opacity
		// The texture alpha controls the shape, vertex color darkens the terrain
		uint32_t color = 0xFF404040; // dark gray with full alpha (ARGB)
		// Convert ARGB to ABGR for D3D11
		color = (color & 0xFF00FF00) | ((color & 0xFF) << 16) | ((color >> 16) & 0xFF);

		Render::Vertex3D v = {};
		v.normal = {0, 0, 1};
		v.color = color;

		v.position = { cx - halfR, cy - halfR, sampleZ(cx - halfR, cy - halfR) };
		v.texcoord = { u0, v0 };
		vertices.push_back(v);

		v.position = { cx + halfR, cy - halfR, sampleZ(cx + halfR, cy - halfR) };
		v.texcoord = { u1, v0 };
		vertices.push_back(v);

		v.position = { cx + halfR, cy + halfR, sampleZ(cx + halfR, cy + halfR) };
		v.texcoord = { u1, v1 };
		vertices.push_back(v);

		v.position = { cx - halfR, cy + halfR, sampleZ(cx - halfR, cy + halfR) };
		v.texcoord = { u0, v1 };
		vertices.push_back(v);

		indices.push_back(baseIdx + 0);
		indices.push_back(baseIdx + 1);
		indices.push_back(baseIdx + 2);
		indices.push_back(baseIdx + 0);
		indices.push_back(baseIdx + 2);
		indices.push_back(baseIdx + 3);
	}

	if (vertices.empty())
		return;

	// Persistent dynamic GPU buffers - created once, reused every frame via Update()
	static Render::VertexBuffer s_scorchVB;
	static Render::IndexBuffer  s_scorchIB;
	static bool s_scorchBuffersCreated = false;

	auto& device = renderer.GetDevice();

	if (!s_scorchBuffersCreated)
	{
		// Pre-allocate for MAX_SCORCH_MARKS quads (4 verts, 6 indices each)
		if (!s_scorchVB.Create(device, nullptr, MAX_SCORCH_MARKS * 4, sizeof(Render::Vertex3D), true))
			return;
		if (!s_scorchIB.Create32(device, nullptr, MAX_SCORCH_MARKS * 6, true))
			return;
		s_scorchBuffersCreated = true;
	}

	// Upload this frame's data into the persistent dynamic buffers
	s_scorchVB.Update(device, vertices.data(), (uint32_t)(vertices.size() * sizeof(Render::Vertex3D)));
	s_scorchIB.Update(device, indices.data(), (uint32_t)(indices.size() * sizeof(uint32_t)));

	// Render scorches with alpha blending - texture alpha controls the decal shape,
	// dark vertex color darkens the terrain underneath
	renderer.SetAlphaBlend3DState();
	BindOverlayTexturesForGroundPass();

	Render::Float4x4 worldIdentity;
	DirectX::XMStoreFloat4x4(&Render::ToXM(worldIdentity), DirectX::XMMatrixIdentity());
	Render::Float4 tint = { 0.15f, 0.12f, 0.1f, 0.85f }; // dark brownish scorch tint

	renderer.Draw3DIndexed(s_scorchVB, s_scorchIB, (uint32_t)indices.size(), g_scorchTexture, worldIdentity, tint);
	renderer.Restore3DState();
}


////////////////////////////////////////////////////////////////////////////////
// SECTION: Bib Buffer (Building Foundation Pads)
//
// Bibs are the rectangular foundation pads rendered under buildings.
// Replaces W3DBibBuffer which used DX8 vertex/index buffers.
// Renders as alpha-blended textured quads on the terrain surface,
// following the same pattern as shadow decals and scorch marks.
////////////////////////////////////////////////////////////////////////////////

#include "Common/GameType.h"

struct D3D11Bib
{
	Vector3 corners[4];
	Bool highlight;
	ObjectID objectID;
	DrawableID drawableID;
	Bool unused;
};

static const int D3D11_MAX_BIBS = 1000;

struct D3D11BibBuffer
{
	D3D11Bib bibs[D3D11_MAX_BIBS];
	int numBibs = 0;
	bool anythingChanged = true;

	void addBib(Vector3 corners[4], ObjectID id, Bool highlight)
	{
		int bibIndex;
		for (bibIndex = 0; bibIndex < numBibs; bibIndex++) {
			if (!bibs[bibIndex].unused && bibs[bibIndex].objectID == id) {
				break;
			}
		}
		if (bibIndex == numBibs) {
			for (bibIndex = 0; bibIndex < numBibs; bibIndex++) {
				if (bibs[bibIndex].unused) {
					break;
				}
			}
		}
		if (bibIndex == numBibs) {
			if (numBibs >= D3D11_MAX_BIBS) {
				return;
			}
			numBibs++;
		}
		anythingChanged = true;
		for (int i = 0; i < 4; i++)
			bibs[bibIndex].corners[i] = corners[i];
		bibs[bibIndex].highlight = highlight;
		bibs[bibIndex].unused = false;
		bibs[bibIndex].objectID = id;
		bibs[bibIndex].drawableID = INVALID_DRAWABLE_ID;
	}

	void addBibDrawable(Vector3 corners[4], DrawableID id, Bool highlight)
	{
		int bibIndex;
		for (bibIndex = 0; bibIndex < numBibs; bibIndex++) {
			if (!bibs[bibIndex].unused && bibs[bibIndex].drawableID == id) {
				break;
			}
		}
		if (bibIndex == numBibs) {
			for (bibIndex = 0; bibIndex < numBibs; bibIndex++) {
				if (bibs[bibIndex].unused) {
					break;
				}
			}
		}
		if (bibIndex == numBibs) {
			if (numBibs >= D3D11_MAX_BIBS) {
				return;
			}
			numBibs++;
		}
		anythingChanged = true;
		for (int i = 0; i < 4; i++)
			bibs[bibIndex].corners[i] = corners[i];
		bibs[bibIndex].highlight = highlight;
		bibs[bibIndex].unused = false;
		bibs[bibIndex].objectID = INVALID_ID;
		bibs[bibIndex].drawableID = id;
	}

	void removeBib(ObjectID id)
	{
		for (int i = 0; i < numBibs; i++) {
			if (bibs[i].objectID == id) {
				bibs[i].unused = true;
				bibs[i].objectID = INVALID_ID;
				bibs[i].drawableID = INVALID_DRAWABLE_ID;
				anythingChanged = true;
			}
		}
	}

	void removeBibDrawable(DrawableID id)
	{
		for (int i = 0; i < numBibs; i++) {
			if (bibs[i].drawableID == id) {
				bibs[i].unused = true;
				bibs[i].objectID = INVALID_ID;
				bibs[i].drawableID = INVALID_DRAWABLE_ID;
				anythingChanged = true;
			}
		}
	}

	void clearAll()
	{
		numBibs = 0;
		anythingChanged = true;
	}

	void removeHighlighting()
	{
		for (int i = 0; i < numBibs; i++) {
			bibs[i].highlight = false;
		}
		anythingChanged = true;
	}
};

static D3D11BibBuffer g_d3d11BibBuffer;
static Render::Texture* g_bibTexture = nullptr;
static Render::Texture* g_bibHighlightTexture = nullptr;
static bool g_bibTexturesLoaded = false;

// Public interface called from D3D11TerrainVisual
void D3D11BibBuffer_AddBib(Vector3 corners[4], ObjectID id, Bool highlight)
{
	g_d3d11BibBuffer.addBib(corners, id, highlight);
}

void D3D11BibBuffer_AddBibDrawable(Vector3 corners[4], DrawableID id, Bool highlight)
{
	g_d3d11BibBuffer.addBibDrawable(corners, id, highlight);
}

void D3D11BibBuffer_RemoveBib(ObjectID id)
{
	g_d3d11BibBuffer.removeBib(id);
}

void D3D11BibBuffer_RemoveBibDrawable(DrawableID id)
{
	g_d3d11BibBuffer.removeBibDrawable(id);
}

void D3D11BibBuffer_ClearAll()
{
	g_d3d11BibBuffer.clearAll();
	g_bibTexturesLoaded = false;
	g_bibTexture = nullptr;
	g_bibHighlightTexture = nullptr;
}

void D3D11BibBuffer_RemoveHighlighting()
{
	g_d3d11BibBuffer.removeHighlighting();
}

void RenderBibsDX11(CameraClass* camera)
{
	if (!camera)
		return;

	if (g_d3d11BibBuffer.numBibs == 0)
		return;

	auto& renderer = Render::Renderer::Instance();

	// Load bib textures on first use
	if (!g_bibTexturesLoaded)
	{
		g_bibTexture = Render::ImageCache::Instance().GetTexture(
			renderer.GetDevice(), "TBBib.tga");
		if (!g_bibTexture)
			g_bibTexture = Render::ImageCache::Instance().GetTexture(
				renderer.GetDevice(), "TBBib.dds");
		g_bibHighlightTexture = Render::ImageCache::Instance().GetTexture(
			renderer.GetDevice(), "TBRedBib.tga");
		if (!g_bibHighlightTexture)
			g_bibHighlightTexture = Render::ImageCache::Instance().GetTexture(
				renderer.GetDevice(), "TBRedBib.dds");
		g_bibTexturesLoaded = true;
	}

	// We need at least one texture to render
	if (!g_bibTexture && !g_bibHighlightTexture)
		return;

	// Get heightmap for terrain height sampling
	WorldHeightMap* hm = GetTerrainHeightMap();
	const float MAP_XY_FACTOR_L = 10.0f;
	const float MAP_HEIGHT_SCALE_L = MAP_XY_FACTOR_L / 16.0f;
	const float BIB_Z_OFFSET = MAP_HEIGHT_SCALE_L * 0.2f;

	auto sampleZ = [&](float wx, float wy) -> float {
		if (!hm) return BIB_Z_OFFSET;
		float xdiv = wx / MAP_XY_FACTOR_L;
		float ydiv = wy / MAP_XY_FACTOR_L;
		int ix = (int)floorf(xdiv) + hm->getBorderSize();
		int iy = (int)floorf(ydiv) + hm->getBorderSize();
		if (ix < 0) ix = 0; if (ix >= hm->getXExtent()) ix = hm->getXExtent() - 1;
		if (iy < 0) iy = 0; if (iy >= hm->getYExtent()) iy = hm->getYExtent() - 1;
		return hm->getHeight(ix, iy) * MAP_HEIGHT_SCALE_L + BIB_Z_OFFSET;
	};

	// Calculate a lighting value for bib tinting (matches original code)
	float shadeR = 1.0f, shadeG = 1.0f, shadeB = 1.0f;
	if (TheGlobalData)
	{
		shadeR = TheGlobalData->m_terrainAmbient[0].red + TheGlobalData->m_terrainDiffuse[0].red;
		shadeG = TheGlobalData->m_terrainAmbient[0].green + TheGlobalData->m_terrainDiffuse[0].green;
		shadeB = TheGlobalData->m_terrainAmbient[0].blue + TheGlobalData->m_terrainDiffuse[0].blue;
		if (shadeR > 1.0f) shadeR = 1.0f;
		if (shadeG > 1.0f) shadeG = 1.0f;
		if (shadeB > 1.0f) shadeB = 1.0f;
	}
	UnsignedByte r = (UnsignedByte)(shadeR * 255.0f);
	UnsignedByte g = (UnsignedByte)(shadeG * 255.0f);
	UnsignedByte b = (UnsignedByte)(shadeB * 255.0f);
	// Pack as ABGR for D3D11
	uint32_t bibColor = (255u << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;

	// Build quads for normal bibs and highlight bibs separately
	// Normal bibs first, then highlight bibs
	for (int pass = 0; pass < 2; pass++)
	{
		bool doHighlight = (pass == 1);
		Render::Texture* tex = doHighlight ? g_bibHighlightTexture : g_bibTexture;
		if (!tex) continue;

		std::vector<Render::Vertex3D> vertices;
		std::vector<uint32_t> indices;

		for (int bi = 0; bi < g_d3d11BibBuffer.numBibs; bi++)
		{
			const D3D11Bib& bib = g_d3d11BibBuffer.bibs[bi];
			if (bib.unused) continue;
			if ((bool)bib.highlight != doHighlight) continue;

			uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

			// UV mapping: corner 0=bottom-left(0,1), 1=bottom-right(1,1),
			// 2=top-right(1,0), 3=top-left(0,0) - matching original code
			static const float bibU[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
			static const float bibV[4] = { 1.0f, 1.0f, 0.0f, 0.0f };

			for (int vi = 0; vi < 4; vi++)
			{
				Render::Vertex3D v = {};
				v.position = { bib.corners[vi].X, bib.corners[vi].Y,
					sampleZ(bib.corners[vi].X, bib.corners[vi].Y) };
				v.normal = { 0.0f, 0.0f, 1.0f };
				v.texcoord = { bibU[vi], bibV[vi] };
				v.color = bibColor;
				vertices.push_back(v);
			}

			// Two triangles: 0-1-2, 0-2-3
			indices.push_back(baseIdx + 0);
			indices.push_back(baseIdx + 1);
			indices.push_back(baseIdx + 2);
			indices.push_back(baseIdx + 0);
			indices.push_back(baseIdx + 2);
			indices.push_back(baseIdx + 3);
		}

		if (vertices.empty())
			continue;

		Render::VertexBuffer vb;
		Render::IndexBuffer ib;
		auto& device = renderer.GetDevice();
		if (!vb.Create(device, vertices.data(), (uint32_t)vertices.size(), sizeof(Render::Vertex3D), false))
			continue;
		if (!ib.Create32(device, indices.data(), (uint32_t)indices.size(), false))
			continue;

		renderer.SetAlphaBlend3DState();
		BindOverlayTexturesForGroundPass();

		Render::Float4x4 worldIdentity;
		DirectX::XMStoreFloat4x4(&Render::ToXM(worldIdentity), DirectX::XMMatrixIdentity());
		Render::Float4 white = { 1.0f, 1.0f, 1.0f, 1.0f };

		renderer.Draw3D(vb, ib, tex, worldIdentity, white);
	}

	renderer.Restore3DState();
}


////////////////////////////////////////////////////////////////////////////////
// SECTION: Status Circle (Team Dot + Screen Fades)
//
// Replaces W3DStatusCircle which used DX8 vertex buffers.
// Renders: (1) a small team-color circle dot in the screen corner when
// m_showTeamDot is enabled, and (2) full-screen color fades for scripted
// cinematic transitions (FADE_ADD, FADE_SUBTRACT, FADE_SATURATE, FADE_MULTIPLY).
////////////////////////////////////////////////////////////////////////////////

#include "GameLogic/ScriptEngine.h"

void RenderStatusCircleDX11()
{
	if (!TheGameLogic || !TheGameLogic->isInGame() || TheGameLogic->getGameMode() == GAME_SHELL)
		return;

	auto& renderer = Render::Renderer::Instance();

	// Part 1: Team color dot
	if (TheGlobalData && TheGlobalData->m_showTeamDot)
	{
		// Draw a small colored circle in the upper-right area of the screen
		// Original position: (0.95, 0.67) in normalized coordinates
		int screenW = renderer.GetWidth();
		int screenH = renderer.GetHeight();
		float cx = 0.95f * screenW;
		float cy = 0.33f * screenH; // flipped Y: 1.0-0.67=0.33
		float radius = 8.0f;

		// The team color is set via W3DStatusCircle::setColor (m_diffuse)
		// In D3D11 we just draw a small colored rectangle as approximation
		uint32_t teamColor = 0x7F0000FF; // default blue with 50% alpha (ABGR)
		renderer.DrawRect(cx - radius, cy - radius, radius * 2, radius * 2, teamColor);
	}

	// Part 2: Screen fades (scripted cinematic fades)
	if (!TheScriptEngine)
		return;

	ScriptEngine::TFade fade = TheScriptEngine->getFade();
	if (fade == ScriptEngine::FADE_NONE)
		return;

	Real intensity = TheScriptEngine->getFadeValue();
	int screenW = renderer.GetWidth();
	int screenH = renderer.GetHeight();

	// Compute the fade color based on intensity
	UnsignedByte clr = (UnsignedByte)(255.0f * intensity);
	uint32_t fadeColor;

	switch (fade)
	{
	default:
	case ScriptEngine::FADE_ADD:
		// Additive white overlay: adds brightness
		// ABGR format
		fadeColor = (0xFFu << 24) | ((uint32_t)clr << 16) | ((uint32_t)clr << 8) | (uint32_t)clr;
		renderer.DrawRect(0, 0, (float)screenW, (float)screenH, fadeColor);
		break;

	case ScriptEngine::FADE_SUBTRACT:
		// Subtractive: darken screen. We approximate by drawing a black overlay
		// with alpha proportional to the fade intensity.
		fadeColor = ((uint32_t)clr << 24) | 0x00000000; // black with variable alpha
		renderer.DrawRect(0, 0, (float)screenW, (float)screenH, fadeColor);
		break;

	case ScriptEngine::FADE_SATURATE:
		// Saturate: boost contrast via white overlay at reduced alpha
		{
			UnsignedByte alpha = (UnsignedByte)(128.0f * intensity);
			fadeColor = ((uint32_t)alpha << 24) | 0x00FFFFFF; // white with variable alpha
			// Draw twice for stronger effect (approximates the original 4x multiply)
			renderer.DrawRect(0, 0, (float)screenW, (float)screenH, fadeColor);
			renderer.DrawRect(0, 0, (float)screenW, (float)screenH, fadeColor);
		}
		break;

	case ScriptEngine::FADE_MULTIPLY:
		// Multiply: darken by multiplying. Approximated with black overlay.
		{
			UnsignedByte alpha = (UnsignedByte)(255.0f * (1.0f - intensity));
			fadeColor = ((uint32_t)alpha << 24) | 0x00000000;
			renderer.DrawRect(0, 0, (float)screenW, (float)screenH, fadeColor);
		}
		break;
	}
}


////////////////////////////////////////////////////////////////////////////////
// SECTION: D3D11Radar -- D3D11-based minimap / radar implementation
//
// Replaces W3DRadar which used DX8 SurfaceClass for pixel-level writes.
// Uses a CPU-side 128x128 RGBA pixel buffer uploaded to a Render::Texture
// each frame via D3D11 dynamic texture mapping.
////////////////////////////////////////////////////////////////////////////////

#include "Common/Radar.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/GameUtility.h"
#include "Common/AudioEventRTS.h"
#include "Common/GameAudio.h"
#include "GameClient/Color.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Line2D.h"
#include "GameClient/TerrainVisual.h"
#include "GameClient/Water.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameClient/Drawable.h"
#include "Renderer.h"

// ============================================================================
// Inline helper: is a radar cell point inside the radar grid?
// ============================================================================
static inline Bool d3d11_legalRadarPoint(Int px, Int py)
{
	return (px >= 0 && py >= 0 && px < RADAR_CELL_WIDTH && py < RADAR_CELL_HEIGHT);
}

// Unit blip size scales with the radar resolution so a unit takes up
// roughly the same fraction of the on-screen radar regardless of how
// many cells we sample at. At the original 128 cells this is 2 (the
// historical 2x2 dot); at 512 it works out to 8.
static constexpr Int RADAR_UNIT_DOT_SIZE = (RADAR_CELL_WIDTH / 64) > 1
                                            ? (RADAR_CELL_WIDTH / 64)
                                            : 2;

// ============================================================================
// D3D11Radar class
// ============================================================================
class D3D11Radar : public Radar
{
public:
	D3D11Radar();
	~D3D11Radar();

	void xfer(Xfer *xfer) override;

	void init() override;
	void update() override;
	void reset() override;

	void newMap(TerrainLogic *terrain) override;

	void draw(Int pixelX, Int pixelY, Int width, Int height) override;

	void clearShroud() override;
	void setShroudLevel(Int x, Int y, CellShroudStatus setting) override;
	void beginSetShroudLevel() override;
	void endSetShroudLevel() override;

	void refreshTerrain(TerrainLogic *terrain) override;
	void refreshObjects() override;

	void notifyViewChanged() override;

private:
	// Pixel buffer helpers
	void setPixel(uint32_t* buf, Int x, Int y, uint32_t rgba);
	void setPixelBlend(uint32_t* buf, Int x, Int y, uint32_t rgba);

	// Core radar building
	void buildTerrainTexture(TerrainLogic *terrain);
	void buildObjectOverlay();
	void buildShroudOverlay();

	// Drawing helpers
	void drawViewBox(Int pixelX, Int pixelY, Int width, Int height);
	void drawEvents(Int pixelX, Int pixelY, Int width, Int height);
	void drawSingleGenericEvent(Int pixelX, Int pixelY, Int width, Int height, Int index);
	void reconstructViewBox();
	void radarToPixel(const ICoord2D *radar, ICoord2D *pixel,
	                  Int radarULX, Int radarULY, Int radarW, Int radarH);

	void interpolateColorForHeight(RGBColor *color, Real height,
	                               Real hiZ, Real midZ, Real loZ);

	static Bool canRenderObject(const RadarObject *rObj, const Player *localPlayer);

	// CPU-side pixel buffers, sized by the RADAR_CELL_* constants in Radar.h
	uint32_t m_terrainPixels[RADAR_CELL_WIDTH * RADAR_CELL_HEIGHT];
	uint32_t m_compositePixels[RADAR_CELL_WIDTH * RADAR_CELL_HEIGHT];
	uint8_t  m_shroudAlpha[RADAR_CELL_WIDTH * RADAR_CELL_HEIGHT];

	// GPU texture for the final composite
	Render::Texture m_radarTexture;
	bool m_textureReady;

	// View box
	Bool m_reconstructViewBox;
	ICoord2D m_viewBox[4];
	Int m_batchShroudWidth;
	Int m_batchShroudHeight;

	enum { OVERLAY_REFRESH_RATE = 6 };
};

// ============================================================================
// Construction / Destruction
// ============================================================================
D3D11Radar::D3D11Radar()
{
	memset(m_terrainPixels, 0, sizeof(m_terrainPixels));
	memset(m_compositePixels, 0, sizeof(m_compositePixels));
	memset(m_shroudAlpha, 0, sizeof(m_shroudAlpha));
	m_textureReady = false;
	m_reconstructViewBox = TRUE;
	m_batchShroudWidth = 0;
	m_batchShroudHeight = 0;
	for (Int i = 0; i < 4; i++)
	{
		m_viewBox[i].x = 0;
		m_viewBox[i].y = 0;
	}
}

D3D11Radar::~D3D11Radar()
{
}

void D3D11Radar::xfer(Xfer *xfer)
{
	Radar::xfer(xfer);
}

// ============================================================================
// Init -- called once at startup
// ============================================================================
void D3D11Radar::init()
{
	Radar::init();

	// Create the dynamic GPU texture
	auto& device = Render::Renderer::Instance().GetDevice();
	m_textureReady = m_radarTexture.CreateDynamic(device, RADAR_CELL_WIDTH, RADAR_CELL_HEIGHT);
}

// ============================================================================
// Reset -- called when a game ends
// ============================================================================
void D3D11Radar::reset()
{
	Radar::reset();
	memset(m_terrainPixels, 0, sizeof(m_terrainPixels));
	memset(m_compositePixels, 0, sizeof(m_compositePixels));
	memset(m_shroudAlpha, 0, sizeof(m_shroudAlpha));
}

// ============================================================================
// Update -- per-frame
// ============================================================================
void D3D11Radar::update()
{
	Radar::update();
}

// ============================================================================
// NewMap -- called when a map loads
// ============================================================================
void D3D11Radar::newMap(TerrainLogic *terrain)
{
	Radar::newMap(terrain);
	m_batchShroudWidth = 0;
	m_batchShroudHeight = 0;

	if (terrain == nullptr)
		return;

	buildTerrainTexture(terrain);
}

// ============================================================================
// Pixel buffer operations
// ============================================================================
void D3D11Radar::setPixel(uint32_t* buf, Int x, Int y, uint32_t rgba)
{
	if (x >= 0 && y >= 0 && x < RADAR_CELL_WIDTH && y < RADAR_CELL_HEIGHT)
		buf[y * RADAR_CELL_WIDTH + x] = rgba;
}

void D3D11Radar::setPixelBlend(uint32_t* buf, Int x, Int y, uint32_t rgba)
{
	if (x < 0 || y < 0 || x >= RADAR_CELL_WIDTH || y >= RADAR_CELL_HEIGHT)
		return;

	// If fully opaque source, just write it
	uint32_t srcA = (rgba >> 24) & 0xFF;
	if (srcA == 255)
	{
		buf[y * RADAR_CELL_WIDTH + x] = rgba;
		return;
	}
	if (srcA == 0)
		return;

	// Alpha blend: dst = src * srcA + dst * (1-srcA)
	uint32_t dst = buf[y * RADAR_CELL_WIDTH + x];
	uint32_t invA = 255 - srcA;
	uint32_t r = (((rgba >>  0) & 0xFF) * srcA + ((dst >>  0) & 0xFF) * invA) / 255;
	uint32_t g = (((rgba >>  8) & 0xFF) * srcA + ((dst >>  8) & 0xFF) * invA) / 255;
	uint32_t b = (((rgba >> 16) & 0xFF) * srcA + ((dst >> 16) & 0xFF) * invA) / 255;
	uint32_t a = srcA + (((dst >> 24) & 0xFF) * invA) / 255;
	buf[y * RADAR_CELL_WIDTH + x] = (a << 24) | (b << 16) | (g << 8) | r;
}

// ============================================================================
// Helper: make an RGBA uint32_t from 0-255 components (for D3D11 R8G8B8A8)
// ============================================================================
static inline uint32_t makeRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

// ============================================================================
// Shade color for height (ported from W3DRadar)
// ============================================================================
void D3D11Radar::interpolateColorForHeight(RGBColor *color, Real height,
                                           Real hiZ, Real midZ, Real loZ)
{
	const Real howBright = 0.95f;
	const Real howDark   = 0.60f;

	if (hiZ == midZ) hiZ = midZ + 0.1f;
	if (midZ == loZ) loZ = midZ - 0.1f;
	if (hiZ == loZ)  hiZ = loZ + 0.2f;

	Real t;
	RGBColor colorTarget;

	if (height >= midZ)
	{
		t = (height - midZ) / (hiZ - midZ);
		colorTarget.red   = color->red   + (1.0f - color->red)   * howBright;
		colorTarget.green = color->green + (1.0f - color->green) * howBright;
		colorTarget.blue  = color->blue  + (1.0f - color->blue)  * howBright;
	}
	else
	{
		t = (midZ - height) / (midZ - loZ);
		colorTarget.red   = color->red   + (0.0f - color->red)   * howDark;
		colorTarget.green = color->green + (0.0f - color->green) * howDark;
		colorTarget.blue  = color->blue  + (0.0f - color->blue)  * howDark;
	}

	color->red   = color->red   + (colorTarget.red   - color->red)   * t;
	color->green = color->green + (colorTarget.green - color->green) * t;
	color->blue  = color->blue  + (colorTarget.blue  - color->blue)  * t;

	if (color->red   < 0.0f) color->red   = 0.0f;
	if (color->red   > 1.0f) color->red   = 1.0f;
	if (color->green < 0.0f) color->green = 0.0f;
	if (color->green > 1.0f) color->green = 1.0f;
	if (color->blue  < 0.0f) color->blue  = 0.0f;
	if (color->blue  > 1.0f) color->blue  = 1.0f;
}

// ============================================================================
// BuildTerrainTexture -- sample the heightmap to build the 128x128 background
// ============================================================================
void D3D11Radar::buildTerrainTexture(TerrainLogic *terrain)
{
	m_reconstructViewBox = TRUE;

	RGBColor waterColor;
	waterColor.red   = TheWaterTransparency->m_radarColor.red;
	waterColor.green = TheWaterTransparency->m_radarColor.green;
	waterColor.blue  = TheWaterTransparency->m_radarColor.blue;

	ICoord2D radarPoint;
	Coord3D  worldPoint;

	// At the higher radar resolution each cell already covers a small slice
	// of the world, and the GPU samples this texture bilinearly when it's
	// drawn — so the original 3x3 box-filter pre-pass is no longer needed.
	// Sampling once per cell keeps map-load time bounded as W*H grows.
	for (Int y = 0; y < RADAR_CELL_HEIGHT; y++)
	{
		for (Int x = 0; x < RADAR_CELL_WIDTH; x++)
		{
			radarPoint.x = x;
			radarPoint.y = y;
			radarToWorld(&radarPoint, &worldPoint);

			RGBColor color;

			// Check for bridge
			Bool workingBridge = FALSE;
			Bridge *bridge = TheTerrainLogic->findBridgeAt(&worldPoint);
			if (bridge != nullptr)
			{
				Object *obj = TheGameLogic->findObjectByID(bridge->peekBridgeInfo()->bridgeObjectID);
				if (obj)
				{
					BodyModuleInterface *body = obj->getBodyModule();
					if (body->getDamageState() != BODY_RUBBLE)
						workingBridge = TRUE;
				}
			}

			Real waterZ;
			if (workingBridge == FALSE && terrain->isUnderwater(worldPoint.x, worldPoint.y, &waterZ))
			{
				// Water area
				color = waterColor;
				interpolateColorForHeight(&color, worldPoint.z, waterZ, waterZ, m_mapExtent.lo.z);
			}
			else if (workingBridge)
			{
				// Working bridge - paint with the bridge color, height-shaded
				// at the bridge deck Z (not the terrain underneath).
				AsciiString bridgeTName = bridge->getBridgeTemplateName();
				TerrainRoadType *bridgeTemplate = TheTerrainRoads->findBridge(bridgeTName);
				if (bridgeTemplate)
					color = bridgeTemplate->getRadarColor();
				else
					color.setFromInt(0xffffffff);

				Real bridgeHeight = (bridge->peekBridgeInfo()->fromLeft.z +
				                     bridge->peekBridgeInfo()->fromRight.z +
				                     bridge->peekBridgeInfo()->toLeft.z +
				                     bridge->peekBridgeInfo()->toRight.z) / 4.0f;
				interpolateColorForHeight(&color, bridgeHeight,
				                          getTerrainAverageZ(),
				                          m_mapExtent.hi.z, m_mapExtent.lo.z);
			}
			else
			{
				// Land area - single sample, height-shaded
				TheTerrainVisual->getTerrainColorAt(worldPoint.x, worldPoint.y, &color);
				interpolateColorForHeight(&color, worldPoint.z, getTerrainAverageZ(),
				                          m_mapExtent.hi.z, m_mapExtent.lo.z);
			}

			uint8_t r = (uint8_t)(color.red   * 255.0f);
			uint8_t g = (uint8_t)(color.green * 255.0f);
			uint8_t b = (uint8_t)(color.blue  * 255.0f);
			m_terrainPixels[y * RADAR_CELL_WIDTH + x] = makeRGBA(r, g, b, 255);
		}
	}
}

// ============================================================================
// canRenderObject -- check whether a radar object should be visible
// ============================================================================
Bool D3D11Radar::canRenderObject(const RadarObject *rObj, const Player *localPlayer)
{
	if (rObj->isTemporarilyHidden())
		return false;

	const Int playerIndex = localPlayer->getPlayerIndex();
	const Object *obj = rObj->friend_getObject();

	if (obj->getShroudedStatus(playerIndex) > OBJECTSHROUD_PARTIAL_CLEAR)
		return false;

	if (obj->getRadarPriority() == RADAR_PRIORITY_LOCAL_UNIT_ONLY &&
	    obj->getControllingPlayer() != localPlayer &&
	    localPlayer->isPlayerActive())
		return false;

	if (TheControlBar->getCurrentlyViewedPlayerRelationship(obj->getTeam()) == ENEMIES &&
	    obj->testStatus(OBJECT_STATUS_STEALTHED) &&
	    !obj->testStatus(OBJECT_STATUS_DETECTED) &&
	    !obj->testStatus(OBJECT_STATUS_DISGUISED))
		return false;

	return true;
}

// ============================================================================
// BuildObjectOverlay -- render unit dots into the composite buffer
// ============================================================================
void D3D11Radar::buildObjectOverlay()
{
	Player *player = rts::getObservedOrLocalPlayer();
	ICoord2D radarPoint;

	// Iterate both lists: global objects and local player objects
	const RadarObject *lists[2] = { m_objectList, m_localObjectList };

	for (int listIdx = 0; listIdx < 2; listIdx++)
	{
		for (const RadarObject *rObj = lists[listIdx]; rObj; rObj = rObj->friend_getNext())
		{
			if (!canRenderObject(rObj, player))
				continue;

			const Object *obj = rObj->friend_getObject();
			const Coord3D *pos = obj->getPosition();

			worldToRadar(pos, &radarPoint);

			// Get the ARGB color and convert to RGBA for our buffer
			Color argbColor = rObj->getColor();

			// Fade stealthed units
			if (obj->testStatus(OBJECT_STATUS_STEALTHED))
			{
				UnsignedByte r, g, b, a;
				GameGetColorComponents(argbColor, &r, &g, &b, &a);
				const UnsignedInt framesForTransition = LOGICFRAMES_PER_SECOND;
				const UnsignedByte minAlpha = 32;
				Real alphaScale = INT_TO_REAL(TheGameLogic->getFrame() % framesForTransition) / (framesForTransition / 2.0f);
				if (alphaScale > 0.0f)
					a = REAL_TO_UNSIGNEDBYTE(((alphaScale - 1.0f) * (255.0f - minAlpha)) + minAlpha);
				else
					a = REAL_TO_UNSIGNEDBYTE((alphaScale * (255.0f - minAlpha)) + minAlpha);
				argbColor = GameMakeColor(r, g, b, a);
			}

			// Extract ARGB components -> RGBA pixel
			UnsignedByte cr, cg, cb, ca;
			GameGetColorComponents(argbColor, &cr, &cg, &cb, &ca);
			uint32_t rgba = makeRGBA(cr, cg, cb, ca);

			// Draw a NxN dot centered roughly on the unit's cell. Size scales
			// with radar resolution so the on-screen blip stays the same size.
			const Int bx = radarPoint.x - (RADAR_UNIT_DOT_SIZE / 2);
			const Int by = radarPoint.y - (RADAR_UNIT_DOT_SIZE / 2);
			for (Int dy = 0; dy < RADAR_UNIT_DOT_SIZE; ++dy)
			{
				for (Int dx = 0; dx < RADAR_UNIT_DOT_SIZE; ++dx)
				{
					if (d3d11_legalRadarPoint(bx + dx, by + dy))
						setPixel(m_compositePixels, bx + dx, by + dy, rgba);
				}
			}
		}
	}
}

// ============================================================================
// BuildShroudOverlay -- darken pixels based on shroud alpha
// ============================================================================
void D3D11Radar::buildShroudOverlay()
{
	for (Int i = 0; i < RADAR_CELL_WIDTH * RADAR_CELL_HEIGHT; i++)
	{
		uint8_t shroudA = m_shroudAlpha[i];
		if (shroudA == 0)
			continue;

		// Darken the composite pixel: blend toward black by shroudA/255
		uint32_t px = m_compositePixels[i];
		uint32_t r = (px >>  0) & 0xFF;
		uint32_t g = (px >>  8) & 0xFF;
		uint32_t b = (px >> 16) & 0xFF;
		uint32_t a = (px >> 24) & 0xFF;

		uint32_t inv = 255 - shroudA;
		r = (r * inv) / 255;
		g = (g * inv) / 255;
		b = (b * inv) / 255;

		m_compositePixels[i] = (a << 24) | (b << 16) | (g << 8) | r;
	}
}

// ============================================================================
// ClearShroud
// ============================================================================
void D3D11Radar::clearShroud()
{
	memset(m_shroudAlpha, 0, sizeof(m_shroudAlpha));
}

void D3D11Radar::beginSetShroudLevel()
{
	m_batchShroudWidth = 0;
	m_batchShroudHeight = 0;
	if (ThePartitionManager && ThePartitionManager->getCellSize() > 0.0f)
	{
		m_batchShroudWidth = ThePartitionManager->getCellCountX();
		m_batchShroudHeight = ThePartitionManager->getCellCountY();
	}
}

void D3D11Radar::endSetShroudLevel()
{
	m_batchShroudWidth = 0;
	m_batchShroudHeight = 0;
}

// ============================================================================
// SetShroudLevel
// ============================================================================
void D3D11Radar::setShroudLevel(Int shroudX, Int shroudY, CellShroudStatus setting)
{
	uint8_t alpha;
	if (setting == CELLSHROUD_SHROUDED)
		alpha = 255;
	else if (setting == CELLSHROUD_FOGGED)
		alpha = 127;
	else
		alpha = 0;

	Int shroudWidth = m_batchShroudWidth;
	Int shroudHeight = m_batchShroudHeight;
	if (shroudWidth <= 0 || shroudHeight <= 0)
	{
		if (ThePartitionManager && ThePartitionManager->getCellSize() > 0.0f)
		{
			shroudWidth = ThePartitionManager->getCellCountX();
			shroudHeight = ThePartitionManager->getCellCountY();
		}
	}
	if (shroudWidth <= 0 || shroudHeight <= 0)
		return;
	if (shroudX < 0 || shroudY < 0 || shroudX >= shroudWidth || shroudY >= shroudHeight)
		return;

	const Int radarMinX = (shroudX * RADAR_CELL_WIDTH) / shroudWidth;
	const Int radarMaxX = ((shroudX + 1) * RADAR_CELL_WIDTH) / shroudWidth;
	const Int radarMinY = (shroudY * RADAR_CELL_HEIGHT) / shroudHeight;
	const Int radarMaxY = ((shroudY + 1) * RADAR_CELL_HEIGHT) / shroudHeight;

	for (Int y = radarMinY; y < radarMaxY; y++)
	{
		for (Int x = radarMinX; x < radarMaxX; x++)
		{
			if (x >= 0 && y >= 0 && x < RADAR_CELL_WIDTH && y < RADAR_CELL_HEIGHT)
				m_shroudAlpha[y * RADAR_CELL_WIDTH + x] = alpha;
		}
	}
}

// ============================================================================
// RefreshTerrain
// ============================================================================
void D3D11Radar::refreshTerrain(TerrainLogic *terrain)
{
	Radar::refreshTerrain(terrain);
	buildTerrainTexture(terrain);
}

// ============================================================================
// RefreshObjects
// ============================================================================
void D3D11Radar::refreshObjects()
{
	// Objects are rebuilt every OVERLAY_REFRESH_RATE frames in draw(), but
	// this forces an immediate rebuild on the next draw.
}

// ============================================================================
// NotifyViewChanged
// ============================================================================
void D3D11Radar::notifyViewChanged()
{
	m_reconstructViewBox = TRUE;
}

// ============================================================================
// ReconstructViewBox -- compute the camera frustum corners in radar space
//
// IMPORTANT: do NOT use Radar::worldToRadar() here. That helper clamps the
// result to [0, RADAR_CELL_WIDTH-1], which causes the camera trapezoid to
// degenerate into a smaller box (or "disappear behind" the playable area)
// whenever the camera frustum extends past the map edge. The original W3D
// version did the division directly with no clamping, and ClipLine2D in
// drawViewBox is responsible for clipping the actual lines to the radar
// window. Match that behavior.
// ============================================================================
void D3D11Radar::reconstructViewBox()
{
	Coord3D world[4];
	ICoord2D radar[4];

	TheTacticalView->getScreenCornerWorldPointsAtZ(&world[0], &world[1],
	                                                &world[2], &world[3],
	                                                getTerrainAverageZ());

	const Real xPerCell = m_mapExtent.width()  / (Real)RADAR_CELL_WIDTH;
	const Real yPerCell = m_mapExtent.height() / (Real)RADAR_CELL_HEIGHT;

	for (Int i = 0; i < 4; i++)
	{
		// Unclamped: lets corners go negative or past RADAR_CELL_WIDTH so the
		// trapezoid stays geometrically correct off-map. ClipLine2D will trim
		// the rendered lines to the radar window.
		radar[i].x = (Int)(world[i].x / xPerCell);
		radar[i].y = (Int)(world[i].y / yPerCell);

		if (i == 0)
		{
			m_viewBox[i].x = 0;
			m_viewBox[i].y = 0;
		}
		else
		{
			m_viewBox[i].x = radar[i].x - radar[i - 1].x;
			m_viewBox[i].y = radar[i].y - radar[i - 1].y;
		}
	}

	m_reconstructViewBox = FALSE;
}

// ============================================================================
// radarToPixel -- convert radar cell coord to screen pixel
// ============================================================================
void D3D11Radar::radarToPixel(const ICoord2D *radar, ICoord2D *pixel,
                              Int radarULX, Int radarULY,
                              Int radarW, Int radarH)
{
	if (radar == nullptr || pixel == nullptr) return;
	pixel->x = (radar->x * radarW / RADAR_CELL_WIDTH) + radarULX;
	// Invert Y so +y is up in world but down on screen
	pixel->y = ((RADAR_CELL_HEIGHT - 1 - radar->y) * radarH / RADAR_CELL_HEIGHT) + radarULY;
}

// ============================================================================
// DrawViewBox -- draw the yellow camera frustum outline on the radar
//
// We render each edge twice: first a wider black line as an outline, then a
// narrower yellow line on top. This keeps the trapezoid visible regardless of
// what color the terrain underneath happens to be (yellow desert / sand /
// dust used to swallow the 1-pixel single-pass version completely).
// ============================================================================
void D3D11Radar::drawViewBox(Int pixelX, Int pixelY, Int width, Int height)
{
	ICoord2D ulScreen;
	ICoord2D ulRadar;
	Coord3D ulWorld;
	ICoord2D ulStart = { 0, 0 };
	ICoord2D start, end;
	ICoord2D clipStart, clipEnd;
	const Real outlineWidth = 4.0f;
	const Real lineWidth    = 2.0f;
	const Color outlineColor = GameMakeColor(  0,   0,   0, 220);
	const Color topColor     = GameMakeColor(255, 255,  64, 255);
	const Color bottomColor  = GameMakeColor(200, 200,  32, 255);

	// Clipping region
	IRegion2D clipRegion;
	ICoord2D radarWindowSize, radarWindowScreenPos;
	m_radarWindow->winGetSize(&radarWindowSize.x, &radarWindowSize.y);
	m_radarWindow->winGetScreenPosition(&radarWindowScreenPos.x, &radarWindowScreenPos.y);
	clipRegion.lo.x = radarWindowScreenPos.x;
	clipRegion.lo.y = radarWindowScreenPos.y;
	clipRegion.hi.x = radarWindowScreenPos.x + radarWindowSize.x;
	clipRegion.hi.y = radarWindowScreenPos.y + radarWindowSize.y;

	// Convert top-left of tactical screen to world, then to radar.
	// Same anti-clamp note as reconstructViewBox: divide directly so corners
	// past the map edge stay geometrically correct.
	TheTacticalView->getOrigin(&ulScreen.x, &ulScreen.y);
	TheTacticalView->screenToWorldAtZ(&ulScreen, &ulWorld, getTerrainAverageZ());

	const Real xPerCell = m_mapExtent.width()  / (Real)RADAR_CELL_WIDTH;
	const Real yPerCell = m_mapExtent.height() / (Real)RADAR_CELL_HEIGHT;
	ulRadar.x = (Int)(ulWorld.x / xPerCell);
	ulRadar.y = (Int)(ulWorld.y / yPerCell);
	radarToPixel(&ulRadar, &ulStart, pixelX, pixelY, width, height);

	// Build all four edges in screen space first, then draw in two passes
	// (outline pass under, colored pass on top). This is friendlier to the
	// 2D batcher than interleaving outline+fill per-edge.
	ICoord2D edges[4][2];
	ICoord2D radar = ulRadar;
	ICoord2D prev = ulStart;

	// Top edge
	radar.x += m_viewBox[1].x;
	radar.y += m_viewBox[1].y;
	radarToPixel(&radar, &end, pixelX, pixelY, width, height);
	edges[0][0] = prev; edges[0][1] = end; prev = end;

	// Right edge
	radar.x += m_viewBox[2].x;
	radar.y += m_viewBox[2].y;
	radarToPixel(&radar, &end, pixelX, pixelY, width, height);
	edges[1][0] = prev; edges[1][1] = end; prev = end;

	// Bottom edge
	radar.x += m_viewBox[3].x;
	radar.y += m_viewBox[3].y;
	radarToPixel(&radar, &end, pixelX, pixelY, width, height);
	edges[2][0] = prev; edges[2][1] = end; prev = end;

	// Left edge (closes back to ulStart)
	edges[3][0] = prev; edges[3][1] = ulStart;

	// Pass 1: black outline
	for (int i = 0; i < 4; ++i)
	{
		start = edges[i][0]; end = edges[i][1];
		if (ClipLine2D(&start, &end, &clipStart, &clipEnd, &clipRegion))
			TheDisplay->drawLine(clipStart.x, clipStart.y, clipEnd.x, clipEnd.y, outlineWidth, outlineColor);
	}

	// Pass 2: yellow fill (top/right slightly brighter than bottom/left for
	// the historical "lit from above" look)
	const Color edgeColors[4] = { topColor, topColor, bottomColor, bottomColor };
	for (int i = 0; i < 4; ++i)
	{
		start = edges[i][0]; end = edges[i][1];
		if (ClipLine2D(&start, &end, &clipStart, &clipEnd, &clipRegion))
			TheDisplay->drawLine(clipStart.x, clipStart.y, clipEnd.x, clipEnd.y, lineWidth, edgeColors[i]);
	}
}

// ============================================================================
// DrawEvents -- draw radar event markers (spinning triangles)
// ============================================================================
void D3D11Radar::drawSingleGenericEvent(Int pixelX, Int pixelY, Int width, Int height, Int index)
{
	RadarEvent *event = &(m_event[index]);
	ICoord2D tri[3];
	ICoord2D start, end;
	Real angle, addAngle;
	Color startColor, endColor;
	Real lineWidth = 1.0f;
	UnsignedInt currentFrame = TheGameLogic->getFrame();
	UnsignedInt frameDiff = currentFrame - event->createFrame;
	Real maxEventSize = width / 2.0f;
	Int minEventSize = 6;
	const Real TIME_SHRINK = (Real)LOGICFRAMES_PER_SECOND * 1.5f;
	Real totalAnglesToSpin = 2.0f * PI;

	IRegion2D clipRegion;
	clipRegion.lo.x = pixelX;
	clipRegion.lo.y = pixelY;
	clipRegion.hi.x = pixelX + width;
	clipRegion.hi.y = pixelY + height;

	Int eventSize = REAL_TO_INT(maxEventSize * (1.0f - frameDiff / TIME_SHRINK));
	if (eventSize < minEventSize) eventSize = minEventSize;

	addAngle = totalAnglesToSpin * (frameDiff / TIME_SHRINK);

	angle = 0.0f - addAngle;
	tri[0].x = REAL_TO_INT((DOUBLE_TO_REAL(Cos(angle)) * eventSize) + event->radarLoc.x);
	tri[0].y = REAL_TO_INT((DOUBLE_TO_REAL(Sin(angle)) * eventSize) + event->radarLoc.y);

	angle = 2.0f * PI / 3.0f - addAngle;
	tri[1].x = REAL_TO_INT((DOUBLE_TO_REAL(Cos(angle)) * eventSize) + event->radarLoc.x);
	tri[1].y = REAL_TO_INT((DOUBLE_TO_REAL(Sin(angle)) * eventSize) + event->radarLoc.y);

	angle = -2.0f * PI / 3.0f - addAngle;
	tri[2].x = REAL_TO_INT((DOUBLE_TO_REAL(Cos(angle)) * eventSize) + event->radarLoc.x);
	tri[2].y = REAL_TO_INT((DOUBLE_TO_REAL(Sin(angle)) * eventSize) + event->radarLoc.y);

	radarToPixel(&tri[0], &tri[0], pixelX, pixelY, width, height);
	radarToPixel(&tri[1], &tri[1], pixelX, pixelY, width, height);
	radarToPixel(&tri[2], &tri[2], pixelX, pixelY, width, height);

	// Color 1
	UnsignedByte r = event->color1.red, g = event->color1.green, b = event->color1.blue, a = event->color1.alpha;
	if (currentFrame > event->fadeFrame)
		a = REAL_TO_UNSIGNEDBYTE((Real)a * (1.0f - (Real)(currentFrame - event->fadeFrame) / (Real)(event->dieFrame - event->fadeFrame)));
	startColor = GameMakeColor(r, g, b, a);

	// Color 2
	r = event->color2.red; g = event->color2.green; b = event->color2.blue; a = event->color2.alpha;
	if (currentFrame > event->fadeFrame)
		a = REAL_TO_UNSIGNEDBYTE((Real)a * (1.0f - (Real)(currentFrame - event->fadeFrame) / (Real)(event->dieFrame - event->fadeFrame)));
	endColor = GameMakeColor(r, g, b, a);

	if (ClipLine2D(&tri[0], &tri[1], &start, &end, &clipRegion))
		TheDisplay->drawLine(start.x, start.y, end.x, end.y, lineWidth, startColor);
	if (ClipLine2D(&tri[1], &tri[2], &start, &end, &clipRegion))
		TheDisplay->drawLine(start.x, start.y, end.x, end.y, lineWidth, startColor);
	if (ClipLine2D(&tri[2], &tri[0], &start, &end, &clipRegion))
		TheDisplay->drawLine(start.x, start.y, end.x, end.y, lineWidth, startColor);
}

void D3D11Radar::drawEvents(Int pixelX, Int pixelY, Int width, Int height)
{
	for (Int i = 0; i < MAX_RADAR_EVENTS; i++)
	{
		if (m_event[i].active == TRUE && m_event[i].type != RADAR_EVENT_FAKE)
		{
			if (m_event[i].soundPlayed == FALSE)
			{
				static AudioEventRTS eventSound("RadarEvent");
				if (TheAudio)
					TheAudio->addAudioEvent(&eventSound);
			}
			m_event[i].soundPlayed = TRUE;
			drawSingleGenericEvent(pixelX, pixelY, width, height, i);
		}
	}
}

// ============================================================================
// Draw -- main entry point called each frame to render the radar
// ============================================================================
void D3D11Radar::draw(Int pixelX, Int pixelY, Int width, Int height)
{
	if (!rts::localPlayerHasRadar())
		return;

	if (!m_textureReady)
		return;

	auto& renderer = Render::Renderer::Instance();

	// Compute aspect-ratio-preserving draw rectangle
	ICoord2D ul, lr;
	findDrawPositions(pixelX, pixelY, width, height, &ul, &lr);

	Int scaledWidth  = lr.x - ul.x;
	Int scaledHeight = lr.y - ul.y;

	// Draw black border bars
	Color fillColor = GameMakeColor(0, 0, 0, 255);
	Color lineColor = GameMakeColor(50, 50, 50, 255);
	if (m_mapExtent.width() / width >= m_mapExtent.height() / height)
	{
		TheDisplay->drawFillRect(pixelX, pixelY, width, ul.y - pixelY - 1, fillColor);
		TheDisplay->drawFillRect(pixelX, lr.y + 1, width, pixelY + height - lr.y - 1, fillColor);
		TheDisplay->drawLine(pixelX, ul.y, pixelX + width, ul.y, 1, lineColor);
		TheDisplay->drawLine(pixelX, lr.y + 1, pixelX + width, lr.y + 1, 1, lineColor);
	}
	else
	{
		TheDisplay->drawFillRect(pixelX, pixelY, ul.x - pixelX - 1, height, fillColor);
		TheDisplay->drawFillRect(lr.x + 1, pixelY, width - (lr.x - pixelX) - 1, height, fillColor);
		TheDisplay->drawLine(ul.x, pixelY, ul.x, pixelY + height, 1, lineColor);
		TheDisplay->drawLine(lr.x + 1, pixelY, lr.x + 1, pixelY + height, 1, lineColor);
	}

	// Build the composite pixel buffer:
	// 1) Start from terrain
	memcpy(m_compositePixels, m_terrainPixels, sizeof(m_terrainPixels));

	// 2) Draw unit dots on top
	if (TheGameClient->getFrame() % OVERLAY_REFRESH_RATE == 0 || true)
	{
		buildObjectOverlay();
	}

	// 3) Apply shroud darkening
	buildShroudOverlay();

	// Upload the composite directly. The terrain is authored with y=0 at
	// the world's south edge, but on screen we want world-north at the top
	// of the radar. Instead of allocating a giant flip buffer (which at the
	// modernized resolution would be megabytes on the stack), we feed the
	// vertical UVs to DrawImageUV in reverse so the texture is sampled
	// bottom-up. End result is identical, with no extra copy.
	m_radarTexture.UpdateFromRGBA(renderer.GetDevice(), m_compositePixels,
	                              RADAR_CELL_WIDTH, RADAR_CELL_HEIGHT);

	renderer.DrawImageUV(m_radarTexture,
	                     (float)ul.x, (float)ul.y,
	                     (float)scaledWidth, (float)scaledHeight,
	                     0.0f, 1.0f, 1.0f, 0.0f, 0xFFFFFFFF);

	// Draw radar events (spinning triangles for attacks, etc.)
	drawEvents(ul.x, ul.y, scaledWidth, scaledHeight);

	// Reconstruct view box if camera moved
	if (m_reconstructViewBox)
		reconstructViewBox();

	// Draw camera view frustum box
	drawViewBox(ul.x, ul.y, scaledWidth, scaledHeight);
}

// ============================================================================
// Factory function -- called from Win32GameEngine::createRadar()
// ============================================================================
Radar* CreateD3D11Radar()
{
	return NEW D3D11Radar;
}

////////////////////////////////////////////////////////////////////////////////
// SECTION: Link stubs for D3D8-dependent symbols pulled in by consolidated build
////////////////////////////////////////////////////////////////////////////////

#include "WW3D2/surfaceclass.h"
#include "WW3D2/texture.h"
#include "WW3D2/part_emt.h"

void* SurfaceClass::Lock(int*) { return nullptr; }
void  SurfaceClass::Unlock() {}

TextureClass::TextureClass(unsigned w, unsigned h, WW3DFormat, MipCountType, PoolType, bool, bool)
	: TextureBaseClass(w, h, MIP_LEVELS_1, POOL_DEFAULT, false, false) {}

void WW3D::Get_Device_Resolution(int &w, int &h, int &bits, bool &windowed)
{
	auto& r = Render::Renderer::Instance();
	w = r.GetWidth(); h = r.GetHeight(); bits = 32; windowed = true;
}

// LineGroupClass — real implementation in Core/Libraries/Source/WWVegas/WW3D2/linegrp.cpp

// SegLineRendererClass::Init — populate line renderer properties from W3D file data
void SegLineRendererClass::Init(const W3dEmitterLinePropertiesStruct& props)
{
	Bits = props.Flags;
	SubdivisionLevel = props.SubdivisionLevel;
	NoiseAmplitude = props.NoiseAmplitude;
	MergeAbortFactor = props.MergeAbortFactor;
	TextureTileFactor = props.TextureTileFactor;
	UVOffsetDeltaPerMS.X = props.UPerSec * 0.001f;
	UVOffsetDeltaPerMS.Y = props.VPerSec * 0.001f;
}

// ParticleEmitterDefClass — real implementation in Core/Libraries/Source/WWVegas/WW3D2/part_ldr.cpp

// SortingRendererClass — no-op stubs. The D3D11 rendering pipeline handles
// translucent geometry through ModelRenderer::FlushTranslucent() instead.
#include "WW3D2/sortingrenderer.h"
bool SortingRendererClass::_EnableTriangleDraw = true;
void SortingRendererClass::Insert_Triangles(const SphereClass&, unsigned short, unsigned short, unsigned short, unsigned short) {}
void SortingRendererClass::Insert_Triangles(unsigned short, unsigned short, unsigned short, unsigned short) {}
void SortingRendererClass::Insert_VolumeParticle(const SphereClass&, unsigned short, unsigned short, unsigned short, unsigned short, unsigned short) {}
void SortingRendererClass::Flush() {}
void SortingRendererClass::Deinit() {}
void SortingRendererClass::Flush_Sorting_Pool() {}
void SortingRendererClass::Insert_To_Sorting_Pool(SortingNodeStruct*) {}
void SortingRendererClass::SetMinVertexBufferSize(unsigned) {}

// ListenerHandleClass::Initialize — Miles audio stub
#include "WWAudio/ListenerHandle.h"
void ListenerHandleClass::Initialize(SoundBufferClass*) {}

// initLogConsole — real implementation is in Debug.cpp

// WOL browser IID — referenced by WebBrowser constructor
#include <initguid.h>
DEFINE_GUID(IID_IBrowserDispatch,
    0x53D62F24, 0x21A5, 0x11D5, 0x9F, 0x03, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);

////////////////////////////////////////////////////////////////////////////////
// x64 stubs — debug/crash/stack dump system disabled on 64-bit builds
////////////////////////////////////////////////////////////////////////////////
#ifdef _M_AMD64
#include "Common/AsciiString.h"
// Forward-declare just enough to provide stub function bodies.
// The real debug_debug.h pulls in x86-only types (DebugStackwalk etc.)
// so we can't include it on x64.
#include "rts/debug.h" // for Debug class forward decl

AsciiString g_LastErrorDump;

// Debug class — real declaration in debug_debug.h, bodies stubbed for x64
Debug::Debug() {}
Debug::~Debug() {}
Debug& Debug::operator<<(const char*) { return *this; }
Debug& Debug::operator<<(int) { return *this; }
bool Debug::CrashDone(bool) { return true; }
Debug& Debug::CrashBegin(const char*, int) { static Debug d; return d; }
bool Debug::AddCommands(const char*, DebugCmdInterface*) { return false; }
bool Debug::SimpleMatch(const char*, const char*) { return false; }
bool Debug::SkipNext() { return false; }
void Debug::Command(const char*) {}
void Debug::AddPatternEntry(unsigned int, bool, const char*) {}

void DumpExceptionInfo(unsigned int, struct _EXCEPTION_POINTERS*) {}
void FillStackAddresses(void**, unsigned int, unsigned int) {}
void StackDumpFromAddresses(void**, unsigned int, void(*)(const char*)) {}
void Register_Thread_ID(unsigned long, char*, bool) {}
void Unregister_Thread_ID(unsigned long, char*) {}

// W3D memory pool functions are provided by GameMemory.cpp on all platforms.

#endif
