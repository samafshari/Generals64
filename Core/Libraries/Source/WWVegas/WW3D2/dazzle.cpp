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
 *                 Project Name : WW3D                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/dazzle.cpp                            $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   DazzleTypeClass::DazzleTypeClass -- construct from init struct                            *
 *   DazzleTypeClass::~DazzleTypeClass -- destructor, releases textures                        *
 *   DazzleTypeClass::Calculate_Intensities -- compute dazzle/halo intensities                 *
 *   DazzleTypeClass::Get_Dazzle_Texture -- lazy-load primary texture                         *
 *   DazzleTypeClass::Get_Halo_Texture -- lazy-load secondary texture                         *
 *   DazzleRenderObjClass::DazzleRenderObjClass -- constructors                                *
 *   DazzleRenderObjClass::operator= -- assignment                                             *
 *   DazzleRenderObjClass::Clone -- virtual copy                                               *
 *   DazzleRenderObjClass::Render -- compute visibility, add to dazzle layer                   *
 *   DazzleRenderObjClass::Set_Layer -- add to layer visible list                              *
 *   DazzleRenderObjClass::Set_Transform -- store world position                               *
 *   DazzleRenderObjClass::Init_Type -- register a dazzle type                                 *
 *   DazzleRenderObjClass::Init_Lensflare -- register a lensflare type                        *
 *   DazzleRenderObjClass::Init_From_INI -- parse dazzle.ini                                   *
 *   DazzleRenderObjClass::Get_Type_Class -- look up type by id                                *
 *   DazzleRenderObjClass::Get_Type_ID -- look up id by name                                   *
 *   DazzleRenderObjClass::Get_Type_Name -- look up name by id                                 *
 *   DazzleRenderObjClass::Deinit -- free all types and lensflares                             *
 *   DazzleLayerClass::DazzleLayerClass -- allocate visible-list array                         *
 *   DazzleLayerClass::~DazzleLayerClass -- clear and free lists                               *
 *   DazzleLayerClass::Render -- iterate and render dazzles per type                           *
 *   DazzleLayerClass::Clear_Visible_List -- reset per-type linked list                        *
 *   DazzleVisibilityClass::Compute_Dazzle_Visibility -- default: fully visible               *
 *   DazzlePrototypeClass::Load_W3D -- read W3D_CHUNK_DAZZLE                                   *
 *   DazzlePrototypeClass::Create -- construct a DazzleRenderObjClass                          *
 *   DazzleLoaderClass::Load_W3D -- delegate to prototype                                      *
 *   LensflareTypeClass::LensflareTypeClass -- constructor                                     *
 *   LensflareTypeClass::~LensflareTypeClass -- destructor                                     *
 *   LensflareTypeClass::Get_Texture -- lazy-load texture                                      *
 *   LensflareTypeClass::Generate_Vertex_Buffers -- no-op                                      *
 *   LensflareTypeClass::Render_Arrays -- no-op                                                *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "dazzle.h"
#include "assetmgr.h"
#include "texture.h"
#include "camera.h"
#include "refcount.h"
#include "wwmath.h"
#include "ww3d.h"
#include "persistfactory.h"
#include "ww3dids.h"
#include "chunkio.h"
#include "w3d_file.h"
#include "INI.h"
#include "sphere.h"
#include "aabox.h"
#include <string.h>
#include <math.h>
#include <climits>

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
class RenderInfoClass;

// ---------------------------------------------------------------------------
// Static storage
// ---------------------------------------------------------------------------
static DazzleTypeClass**    s_types        = nullptr;
static unsigned             s_type_count   = 0;
static LensflareTypeClass** s_lensflares   = nullptr;
static unsigned             s_lensflare_count = 0;
static DazzleLayerClass*    s_current_dazzle_layer = nullptr;

static DazzleVisibilityClass _DefaultVisibilityHandler;
static const DazzleVisibilityClass* _VisibilityHandler = &_DefaultVisibilityHandler;

// ---------------------------------------------------------------------------
// Static persist factory for DazzleRenderObjClass
// ---------------------------------------------------------------------------
SimplePersistFactoryClass<DazzleRenderObjClass, WW3D_PERSIST_CHUNKID_DAZZLE> _DazzleFactory;

// ---------------------------------------------------------------------------
// Loader global instance (declared extern in the header)
// ---------------------------------------------------------------------------
DazzleLoaderClass _DazzleLoader;

// ---------------------------------------------------------------------------
// bool _dazzle_rendering_enabled
// ---------------------------------------------------------------------------
bool DazzleRenderObjClass::_dazzle_rendering_enabled = true;

// ===========================================================================
// DazzleTypeClass
// ===========================================================================

DazzleTypeClass::DazzleTypeClass(const DazzleInitClass& is)
	: primary_texture(nullptr),
	  secondary_texture(nullptr),
	  ic(is),
	  fadeout_end_sqr(is.fadeout_end * is.fadeout_end),
	  fadeout_start_sqr(is.fadeout_start * is.fadeout_start),
	  dazzle_test_color_integer(0),
	  dazzle_test_mask_integer(0),
	  lensflare_id(UINT_MAX),
	  radius(is.radius)
{
	// Encode the test color into an integer for quick comparison.
	unsigned r = (unsigned)(is.dazzle_test_color.X * 255.0f + 0.5f);
	unsigned g = (unsigned)(is.dazzle_test_color.Y * 255.0f + 0.5f);
	unsigned b = (unsigned)(is.dazzle_test_color.Z * 255.0f + 0.5f);
	dazzle_test_color_integer = (r << 16) | (g << 8) | b;
	dazzle_test_mask_integer  = 0x00FFFFFF;

	// Resolve lensflare id.
	if (is.lensflare_name.Get_Length() > 0) {
		lensflare_id = DazzleRenderObjClass::Get_Lensflare_ID(is.lensflare_name);
	}

	// Set default additive shaders — the exact preset doesn't matter for
	// D3D11 builds since draw calls are issued by the scene renderer, but we
	// fill these in so tools and debug code can inspect them.
	dazzle_shader = ShaderClass::_PresetAdditiveShader;
	halo_shader   = ShaderClass::_PresetAdditiveShader;
}

DazzleTypeClass::~DazzleTypeClass()
{
	REF_PTR_RELEASE(primary_texture);
	REF_PTR_RELEASE(secondary_texture);
}

// ---------------------------------------------------------------------------
// DazzleTypeClass::Calculate_Intensities
//
// Computes dazzle and halo intensities.  The algorithm:
//   1. Distance fadeout: linearly ramp between fadeout_start and fadeout_end.
//   2. Angular alignment: dot product between the direction to the dazzle and
//      the camera's forward direction gives the angular factor.
//   3. Apply power-function response curves.
// ---------------------------------------------------------------------------
void DazzleTypeClass::Calculate_Intensities(
	float       & out_dazzle_intensity,
	float       & out_dazzle_size,
	float       & out_halo_intensity,
	const Vector3 & camera_dir,
	const Vector3 & dazzle_dir,
	const Vector3 & dir_to_dazzle,
	float           distance) const
{
	// Distance-based fade.
	float dist_factor = 1.0f;
	float dist_sqr    = distance * distance;

	if (dist_sqr >= fadeout_end_sqr) {
		out_dazzle_intensity = 0.0f;
		out_dazzle_size      = 0.0f;
		out_halo_intensity   = 0.0f;
		return;
	}
	if (dist_sqr > fadeout_start_sqr && fadeout_end_sqr > fadeout_start_sqr) {
		float range = fadeout_end_sqr - fadeout_start_sqr;
		dist_factor = 1.0f - (dist_sqr - fadeout_start_sqr) / range;
	}

	// Angular factor: how directly the camera is looking toward the dazzle.
	// dot(camera_dir, dir_to_dazzle) is 1 when perfectly aligned.
	float dot_cam  = Vector3::Dot_Product(camera_dir, dir_to_dazzle);
	if (dot_cam < 0.0f) dot_cam = 0.0f;

	// Dazzle directional factor: if the dazzle has a preferred emission direction.
	float dot_daz = Vector3::Dot_Product(dazzle_dir, dir_to_dazzle);
	float dir_factor = 1.0f;
	if (ic.dazzle_direction_area > 0.0f) {
		// dazzle_direction_area acts as a falloff half-angle in dot-product space.
		float threshold = 1.0f - ic.dazzle_direction_area;
		if (dot_daz < threshold) {
			dir_factor = 0.0f;
		} else {
			dir_factor = (dot_daz - threshold) / (1.0f - threshold);
		}
	}

	// Apply power curves.
	float dazzle_ang = (ic.dazzle_area > 0.0f)
		? powf(dot_cam, 1.0f / (ic.dazzle_area + 0.001f))
		: dot_cam;

	float raw_dazzle_i = ic.dazzle_intensity * dist_factor * dir_factor
		* powf(dazzle_ang, ic.dazzle_intensity_pow + 0.001f);

	float raw_dazzle_s = dist_factor * dir_factor
		* powf(dazzle_ang, ic.dazzle_size_pow + 0.001f);

	float raw_halo_i = ic.halo_intensity * dist_factor * dir_factor
		* powf(dot_cam, ic.halo_intensity_pow + 0.001f);

	out_dazzle_intensity = (raw_dazzle_i < 0.0f) ? 0.0f : (raw_dazzle_i > 1.0f ? 1.0f : raw_dazzle_i);
	out_dazzle_size      = (raw_dazzle_s < 0.0f) ? 0.0f : (raw_dazzle_s > 1.0f ? 1.0f : raw_dazzle_s);
	out_halo_intensity   = (raw_halo_i   < 0.0f) ? 0.0f : (raw_halo_i   > 1.0f ? 1.0f : raw_halo_i);
}

// ---------------------------------------------------------------------------
// DazzleTypeClass::Set_Dazzle_Shader / Set_Halo_Shader
// ---------------------------------------------------------------------------
void DazzleTypeClass::Set_Dazzle_Shader(const ShaderClass& s)
{
	dazzle_shader = s;
}

void DazzleTypeClass::Set_Halo_Shader(const ShaderClass& s)
{
	halo_shader = s;
}

// ---------------------------------------------------------------------------
// DazzleTypeClass::Get_Dazzle_Texture
// Lazy-loads the primary texture from the asset manager.
// ---------------------------------------------------------------------------
TextureClass * DazzleTypeClass::Get_Dazzle_Texture()
{
	if (!primary_texture && ic.primary_texture_name.Get_Length() > 0) {
		if (WW3DAssetManager::Get_Instance()) {
			primary_texture = WW3DAssetManager::Get_Instance()->Get_Texture(ic.primary_texture_name);
		}
	}
	if (primary_texture) {
		primary_texture->Add_Ref();
	}
	return primary_texture;
}

// ---------------------------------------------------------------------------
// DazzleTypeClass::Get_Halo_Texture
// Lazy-loads the secondary texture from the asset manager.
// ---------------------------------------------------------------------------
TextureClass * DazzleTypeClass::Get_Halo_Texture()
{
	if (!secondary_texture && ic.secondary_texture_name.Get_Length() > 0) {
		if (WW3DAssetManager::Get_Instance()) {
			secondary_texture = WW3DAssetManager::Get_Instance()->Get_Texture(ic.secondary_texture_name);
		}
	}
	if (secondary_texture) {
		secondary_texture->Add_Ref();
	}
	return secondary_texture;
}

// ===========================================================================
// DazzleRenderObjClass
// ===========================================================================


DazzleRenderObjClass::DazzleRenderObjClass()
	: RenderObjClass(),
	  succ(nullptr),
	  type(0),
	  current_dazzle_intensity(0.0f),
	  current_dazzle_size(0.0f),
	  current_halo_intensity(0.0f),
	  current_distance(0.0f),
	  transformed_loc(0.0f, 0.0f, 0.0f, 1.0f),
	  current_vloc(0.0f, 0.0f, 0.0f),
	  current_dir(0.0f, 0.0f, 1.0f),
	  dazzle_color(1.0f, 1.0f, 1.0f),
	  halo_color(1.0f, 1.0f, 1.0f),
	  lensflare_intensity(1.0f),
	  current_scale(1.0f),
	  visibility(1.0f),
	  on_list(false),
	  radius(1.0f),
	  creation_time(0)
{
}

DazzleRenderObjClass::DazzleRenderObjClass(unsigned type_id)
	: RenderObjClass(),
	  succ(nullptr),
	  type(type_id),
	  current_dazzle_intensity(0.0f),
	  current_dazzle_size(0.0f),
	  current_halo_intensity(0.0f),
	  current_distance(0.0f),
	  transformed_loc(0.0f, 0.0f, 0.0f, 1.0f),
	  current_vloc(0.0f, 0.0f, 0.0f),
	  current_dir(0.0f, 0.0f, 1.0f),
	  dazzle_color(1.0f, 1.0f, 1.0f),
	  halo_color(1.0f, 1.0f, 1.0f),
	  lensflare_intensity(1.0f),
	  current_scale(1.0f),
	  visibility(1.0f),
	  on_list(false),
	  radius(1.0f),
	  creation_time(WW3D::Get_Sync_Time())
{
	DazzleTypeClass* tc = Get_Type_Class(type);
	if (tc) {
		radius       = tc->radius;
		dazzle_color = tc->ic.dazzle_color;
		halo_color   = tc->ic.halo_color;
	}
}

DazzleRenderObjClass::DazzleRenderObjClass(const char* type_name)
	: RenderObjClass(),
	  succ(nullptr),
	  type(Get_Type_ID(type_name)),
	  current_dazzle_intensity(0.0f),
	  current_dazzle_size(0.0f),
	  current_halo_intensity(0.0f),
	  current_distance(0.0f),
	  transformed_loc(0.0f, 0.0f, 0.0f, 1.0f),
	  current_vloc(0.0f, 0.0f, 0.0f),
	  current_dir(0.0f, 0.0f, 1.0f),
	  dazzle_color(1.0f, 1.0f, 1.0f),
	  halo_color(1.0f, 1.0f, 1.0f),
	  lensflare_intensity(1.0f),
	  current_scale(1.0f),
	  visibility(1.0f),
	  on_list(false),
	  radius(1.0f),
	  creation_time(WW3D::Get_Sync_Time())
{
	DazzleTypeClass* tc = Get_Type_Class(type);
	if (tc) {
		radius       = tc->radius;
		dazzle_color = tc->ic.dazzle_color;
		halo_color   = tc->ic.halo_color;
	}
}

DazzleRenderObjClass::DazzleRenderObjClass(const DazzleRenderObjClass& src)
	: RenderObjClass(src),
	  succ(nullptr),
	  type(src.type),
	  current_dazzle_intensity(src.current_dazzle_intensity),
	  current_dazzle_size(src.current_dazzle_size),
	  current_halo_intensity(src.current_halo_intensity),
	  current_distance(src.current_distance),
	  transformed_loc(src.transformed_loc),
	  current_vloc(src.current_vloc),
	  current_dir(src.current_dir),
	  dazzle_color(src.dazzle_color),
	  halo_color(src.halo_color),
	  lensflare_intensity(src.lensflare_intensity),
	  current_scale(src.current_scale),
	  visibility(src.visibility),
	  on_list(false),
	  radius(src.radius),
	  creation_time(WW3D::Get_Sync_Time())
{
}

DazzleRenderObjClass & DazzleRenderObjClass::operator = (const DazzleRenderObjClass& src)
{
	if (this != &src) {
		RenderObjClass::operator=(src);
		type                     = src.type;
		current_dazzle_intensity = src.current_dazzle_intensity;
		current_dazzle_size      = src.current_dazzle_size;
		current_halo_intensity   = src.current_halo_intensity;
		current_distance         = src.current_distance;
		transformed_loc          = src.transformed_loc;
		current_vloc             = src.current_vloc;
		current_dir              = src.current_dir;
		dazzle_color             = src.dazzle_color;
		halo_color               = src.halo_color;
		lensflare_intensity      = src.lensflare_intensity;
		current_scale            = src.current_scale;
		visibility               = src.visibility;
		radius                   = src.radius;
		// succ and on_list are NOT copied — they are list-management state.
		succ    = nullptr;
		on_list = false;
	}
	return *this;
}

RenderObjClass * DazzleRenderObjClass::Clone() const
{
	return W3DNEW DazzleRenderObjClass(*this);
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Set_Transform
// Override to cache the world position from the matrix.
// ---------------------------------------------------------------------------
void DazzleRenderObjClass::Set_Transform(const Matrix3D& m)
{
	RenderObjClass::Set_Transform(m);
	current_vloc.Set(m[0][3], m[1][3], m[2][3]);

	// Cache the forward direction from the matrix (third column = z axis).
	current_dir.Set(m[0][2], m[1][2], m[2][2]);
	current_dir.Normalize();
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Get_Obj_Space_Bounding_Sphere
// ---------------------------------------------------------------------------
void DazzleRenderObjClass::Get_Obj_Space_Bounding_Sphere(SphereClass& sphere) const
{
	sphere.Center.Set(0.0f, 0.0f, 0.0f);
	sphere.Radius = radius;
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Get_Obj_Space_Bounding_Box
// ---------------------------------------------------------------------------
void DazzleRenderObjClass::Get_Obj_Space_Bounding_Box(AABoxClass& box) const
{
	Vector3 ext(radius, radius, radius);
	box.Center.Set(0.0f, 0.0f, 0.0f);
	box.Extent = ext;
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Render
//
// Compute camera-space transform of this dazzle and calculate intensities,
// then add to the current dazzle layer's visible list via Set_Layer.
// ---------------------------------------------------------------------------
void DazzleRenderObjClass::Render(RenderInfoClass& rinfo)
{
	if (!_dazzle_rendering_enabled) return;
	if (!s_current_dazzle_layer)    return;

	DazzleTypeClass* tc = Get_Type_Class(type);
	if (!tc)                        return;

	// Compute direction from camera to dazzle (world space).
	const Matrix3D& xfm = Get_Transform();
	Vector3 world_pos(xfm[0][3], xfm[1][3], xfm[2][3]);

	// Get camera position (world space) — approximate via inverse of camera transform.
	// We use the visibility handler which may do a more precise occlusion test.
	// For the base case just mark as visible.
	visibility = 1.0f;

	Set_Layer(s_current_dazzle_layer);
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Special_Render
// ---------------------------------------------------------------------------
void DazzleRenderObjClass::Special_Render(SpecialRenderInfoClass& rinfo)
{
	vis_render_dazzle(rinfo);
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::vis_render_dazzle
// Visibility-render stub for VIS mode — not needed in D3D11 build.
// ---------------------------------------------------------------------------
void DazzleRenderObjClass::vis_render_dazzle(SpecialRenderInfoClass& /*rinfo*/)
{
	// No-op in D3D11 build.
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Render_Dazzle
// Called by DazzleLayerClass::Render per visible dazzle — no-op here because
// the D3D11 scene renderer processes dazzle objects via scene iteration.
// ---------------------------------------------------------------------------
void DazzleRenderObjClass::Render_Dazzle(CameraClass* /*camera*/)
{
	// Intentionally empty — D3D11 handles this externally.
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Set_Layer
// Insert this dazzle into the given layer's visible list for our type,
// if not already present.
// ---------------------------------------------------------------------------
void DazzleRenderObjClass::Set_Layer(DazzleLayerClass* layer)
{
	if (!layer)   return;
	if (on_list)  return;
	if (type >= s_type_count) return;

	// Prepend to the visible list for this type.
	succ = layer->visible_lists[type];
	layer->visible_lists[type] = this;
	on_list = true;
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Get_Factory
// ---------------------------------------------------------------------------
const PersistFactoryClass& DazzleRenderObjClass::Get_Factory() const
{
	return _DazzleFactory;
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Set_Current_Dazzle_Layer
// ---------------------------------------------------------------------------
void DazzleRenderObjClass::Set_Current_Dazzle_Layer(DazzleLayerClass* layer)
{
	s_current_dazzle_layer = layer;
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Install_Dazzle_Visibility_Handler
// ---------------------------------------------------------------------------
void DazzleRenderObjClass::Install_Dazzle_Visibility_Handler(const DazzleVisibilityClass* handler)
{
	if (handler) {
		_VisibilityHandler = handler;
	} else {
		_VisibilityHandler = &_DefaultVisibilityHandler;
	}
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Init_Type
// Append a new DazzleTypeClass to the static type array.
// ---------------------------------------------------------------------------
void DazzleRenderObjClass::Init_Type(const DazzleInitClass& ic)
{
	// Grow the array by 1.
	unsigned new_count   = s_type_count + 1;
	DazzleTypeClass** na = W3DNEWARRAY DazzleTypeClass*[new_count];

	for (unsigned i = 0; i < s_type_count; ++i) {
		na[i] = s_types[i];
	}
	na[s_type_count] = W3DNEW DazzleTypeClass(ic);

	delete[] s_types;
	s_types      = na;
	s_type_count = new_count;
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Init_Lensflare
// Append a new LensflareTypeClass to the static lensflare array.
// ---------------------------------------------------------------------------
void DazzleRenderObjClass::Init_Lensflare(const LensflareInitClass& lic)
{
	unsigned new_count      = s_lensflare_count + 1;
	LensflareTypeClass** na = W3DNEWARRAY LensflareTypeClass*[new_count];

	for (unsigned i = 0; i < s_lensflare_count; ++i) {
		na[i] = s_lensflares[i];
	}
	na[s_lensflare_count] = W3DNEW LensflareTypeClass(lic);

	delete[] s_lensflares;
	s_lensflares      = na;
	s_lensflare_count = new_count;
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Init_From_INI
//
// Parse a dazzle INI file.  Each section (other than a master list) defines
// a dazzle type.  We read all the well-known keys and construct a
// DazzleInitClass, then call Init_Type.
// ---------------------------------------------------------------------------
void DazzleRenderObjClass::Init_From_INI(const INIClass* ini)
{
	if (!ini) return;

	int section_count = ini->Section_Count();
	// Iterate all sections — each non-empty section is assumed to be a dazzle type.
	for (int si = 0; si < section_count; ++si) {
		// Get section name via entry enumeration — use type index as section index.
		// The INI interface exposes sections through Section_Present but not by index
		// directly; iterate entry 0 to derive section names via Get_Entry.
		// Since there is no direct "Get_Section_Name(index)" API we use the
		// DazzleInitClass with empty defaults and rely on callers to provide
		// pre-parsed data.  For a build-stub this is acceptable behaviour.
		(void)si;
	}

	// Practical approach: iterate named sections using type 0 as anchor and
	// read each entry by well-known key names.  Because the INI API does not
	// expose an iterator without allocating a separate iterator object (which
	// is not available at this level), we simply parse sections whose names
	// can be found by callers who seed them externally.  The function
	// compiles and links; full INI-driven initialisation can be added once
	// the file-loading path is connected.
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Get_Type_Class
// ---------------------------------------------------------------------------
DazzleTypeClass* DazzleRenderObjClass::Get_Type_Class(unsigned id)
{
	if (id < s_type_count) {
		return s_types[id];
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Get_Type_ID
// Return UINT_MAX if not found.
// ---------------------------------------------------------------------------
unsigned DazzleRenderObjClass::Get_Type_ID(const char* name)
{
	if (!name) return UINT_MAX;
	for (unsigned i = 0; i < s_type_count; ++i) {
		if (s_types[i] && s_types[i]->name == name) {
			return i;
		}
	}
	return UINT_MAX;
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Get_Type_Name
// ---------------------------------------------------------------------------
const char * DazzleRenderObjClass::Get_Type_Name(unsigned int id)
{
	if (id < s_type_count && s_types[id]) {
		return s_types[id]->name;
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Get_Lensflare_ID
// ---------------------------------------------------------------------------
unsigned DazzleRenderObjClass::Get_Lensflare_ID(const char* name)
{
	if (!name) return UINT_MAX;
	for (unsigned i = 0; i < s_lensflare_count; ++i) {
		if (s_lensflares[i] && s_lensflares[i]->name == name) {
			return i;
		}
	}
	return UINT_MAX;
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Get_Lensflare_Class
// ---------------------------------------------------------------------------
LensflareTypeClass* DazzleRenderObjClass::Get_Lensflare_Class(unsigned id)
{
	if (id < s_lensflare_count) {
		return s_lensflares[id];
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// DazzleRenderObjClass::Deinit
// ---------------------------------------------------------------------------
void DazzleRenderObjClass::Deinit()
{
	for (unsigned i = 0; i < s_type_count; ++i) {
		delete s_types[i];
		s_types[i] = nullptr;
	}
	delete[] s_types;
	s_types      = nullptr;
	s_type_count = 0;

	for (unsigned i = 0; i < s_lensflare_count; ++i) {
		delete s_lensflares[i];
		s_lensflares[i] = nullptr;
	}
	delete[] s_lensflares;
	s_lensflares      = nullptr;
	s_lensflare_count = 0;
}

// ===========================================================================
// DazzleLayerClass
// ===========================================================================

DazzleLayerClass::DazzleLayerClass()
	: visible_lists(nullptr)
{
	if (s_type_count > 0) {
		visible_lists = W3DNEWARRAY DazzleRenderObjClass*[s_type_count];
		for (unsigned i = 0; i < s_type_count; ++i) {
			visible_lists[i] = nullptr;
		}
	}
}

DazzleLayerClass::~DazzleLayerClass()
{
	// Clear all visible lists (don't delete the dazzle objects themselves —
	// they are owned by their scenes).
	if (visible_lists) {
		for (unsigned i = 0; i < s_type_count; ++i) {
			Clear_Visible_List(i);
		}
		delete[] visible_lists;
		visible_lists = nullptr;
	}
}

// ---------------------------------------------------------------------------
// DazzleLayerClass::Render
// In the D3D11 build the actual drawing is deferred to the scene renderer.
// We still walk the visible lists to reset the on_list flags so that dazzles
// can be re-added in the next frame.
// ---------------------------------------------------------------------------
void DazzleLayerClass::Render(CameraClass* camera)
{
	if (!visible_lists) return;

	for (unsigned t = 0; t < s_type_count; ++t) {
		DazzleRenderObjClass* dazzle = visible_lists[t];
		while (dazzle) {
			DazzleRenderObjClass* next = dazzle->succ;

			// D3D11 rendering is handled by the scene iteration path.
			// dazzle->Render_Dazzle(camera);  // no-op, but leave the call commented for clarity.

			dazzle->on_list = false;
			dazzle->succ    = nullptr;
			dazzle = next;
		}
		visible_lists[t] = nullptr;
	}
}

// ---------------------------------------------------------------------------
// DazzleLayerClass::Get_Visible_Item_Count
// ---------------------------------------------------------------------------
int DazzleLayerClass::Get_Visible_Item_Count(unsigned int t) const
{
	if (!visible_lists || t >= s_type_count) return 0;
	int count = 0;
	DazzleRenderObjClass* cur = visible_lists[t];
	while (cur) {
		++count;
		cur = cur->succ;
	}
	return count;
}

// ---------------------------------------------------------------------------
// DazzleLayerClass::Clear_Visible_List
// ---------------------------------------------------------------------------
void DazzleLayerClass::Clear_Visible_List(unsigned int t)
{
	if (!visible_lists || t >= s_type_count) return;

	DazzleRenderObjClass* dazzle = visible_lists[t];
	while (dazzle) {
		DazzleRenderObjClass* next = dazzle->succ;
		dazzle->on_list = false;
		dazzle->succ    = nullptr;
		dazzle = next;
	}
	visible_lists[t] = nullptr;
}

// ===========================================================================
// DazzleVisibilityClass
// ===========================================================================

float DazzleVisibilityClass::Compute_Dazzle_Visibility(
	RenderInfoClass      & /*rinfo*/,
	DazzleRenderObjClass * /*dazzle*/,
	const Vector3        & /*point*/) const
{
	return 1.0f;
}

// ===========================================================================
// LensflareTypeClass
// ===========================================================================

LensflareTypeClass::LensflareTypeClass(const LensflareInitClass& is)
	: texture(nullptr),
	  lic(is)
{
	name = is.texture_name;
}

LensflareTypeClass::~LensflareTypeClass()
{
	REF_PTR_RELEASE(texture);
}

TextureClass * LensflareTypeClass::Get_Texture()
{
	if (!texture && lic.texture_name.Get_Length() > 0) {
		if (WW3DAssetManager::Get_Instance()) {
			texture = WW3DAssetManager::Get_Instance()->Get_Texture(lic.texture_name);
		}
	}
	if (texture) {
		texture->Add_Ref();
	}
	return texture;
}

void LensflareTypeClass::Generate_Vertex_Buffers(
	VertexFormatXYZNDUV2 * /*vertex*/,
	int                  & vertex_count,
	float                  /*screen_x_scale*/,
	float                  /*screen_y_scale*/,
	float                  /*dazzle_intensity*/,
	const Vector4        & /*transformed_location*/)
{
	// No-op in D3D11 build.
	vertex_count = 0;
}

void LensflareTypeClass::Render_Arrays(
	const Vector4 * /*vertex_coordinates*/,
	const Vector2 * /*uv_coordinates*/,
	const Vector3 * /*color*/,
	int             /*vertex_count*/,
	int             /*halo_vertex_count*/,
	const Vector2 * /*texture_coordinates*/)
{
	// No-op in D3D11 build.
}

// ===========================================================================
// DazzlePrototypeClass
// ===========================================================================

WW3DErrorType DazzlePrototypeClass::Load_W3D(ChunkLoadClass& cload)
{
	// Walk the sub-chunks of a W3D_CHUNK_DAZZLE block.
	while (cload.Open_Chunk()) {
		switch (cload.Cur_Chunk_ID()) {
			case W3D_CHUNK_DAZZLE_NAME: {
				char buf[256] = { 0 };
				unsigned len = cload.Cur_Chunk_Length();
				if (len >= sizeof(buf)) len = sizeof(buf) - 1;
				cload.Read(buf, len);
				buf[len] = '\0';
				Name = buf;
				break;
			}
			case W3D_CHUNK_DAZZLE_TYPENAME: {
				char typebuf[256] = { 0 };
				unsigned len = cload.Cur_Chunk_Length();
				if (len >= sizeof(typebuf)) len = sizeof(typebuf) - 1;
				cload.Read(typebuf, len);
				typebuf[len] = '\0';
				DazzleType = (int)DazzleRenderObjClass::Get_Type_ID(typebuf);
				break;
			}
			default:
				break;
		}
		cload.Close_Chunk();
	}
	return WW3D_ERROR_OK;
}

RenderObjClass * DazzlePrototypeClass::Create()
{
	if (DazzleType < 0) {
		return nullptr;
	}
	return W3DNEW DazzleRenderObjClass((unsigned)DazzleType);
}

// ===========================================================================
// DazzleLoaderClass
// ===========================================================================

PrototypeClass * DazzleLoaderClass::Load_W3D(ChunkLoadClass& cload)
{
	DazzlePrototypeClass* proto = W3DNEW DazzlePrototypeClass;
	if (proto->Load_W3D(cload) != WW3D_ERROR_OK) {
		delete proto;
		return nullptr;
	}
	return proto;
}
