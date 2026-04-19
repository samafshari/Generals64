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

// Stage 6+7 port of the D3D8 volumetric shadow manager to DX11.
//
// The silhouette extraction + extrusion CPU code is a near-literal port of
// the original W3DVolumetricShadow.cpp; only the device-side rendering
// (vertex buffer binding, stencil state, darkening pass) is rewritten
// against the DX11 renderer. Where behaviour differs intentionally from
// the original, a `// DX11:` comment marks the site.

#include "always.h"
#include "Common/Debug.h"
#include "Common/GlobalData.h"
#include "Common/KindOf.h"
#include "GameClient/Drawable.h"
#include "GameLogic/TerrainLogic.h"
#include "WW3D2/camera.h"
#include "WW3D2/hlod.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/rinfo.h"
#include "W3DDevice/GameClient/W3DBufferManager.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/W3DVolumetricShadow.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "hash.h"
#include "refcount.h"

#ifdef BUILD_WITH_D3D11
#include <d3d11.h>
#include <cstdio>
#include "Renderer.h"
#include "RenderUtils.h"
#include "Core/Device.h"
#include "Shaders/ShaderSource.h"
#endif

// ---------------------------------------------------------------------------
// Module-scoped constants (ported from original)
// ---------------------------------------------------------------------------

// Maximum silhouette vertices processable in a single mesh.
#define MAX_SHADOW_VOLUME_VERTS				16384

// When the dot product between current and previous orientation axis falls
// below this (~0.2° delta), we treat the rotation as large enough to rebuild.
static const Real cosAngleToCare = (Real)cos((0.2 * 3.1415926535) / 180.0);

#define MAX_SILHOUETTE_EDGES						1024
#define SHADOW_EXTRUSION_BUFFER					0.1f
#define AIRBORNE_UNIT_GROUND_DELTA				2.0f
#define MAX_SHADOW_LENGTH_SCALE_FACTOR			1.0f
#define MAX_SHADOW_LENGTH_EXTRA_AIRBORNE_SCALE_FACTOR	1.5f

// Constants for polygon neighbor bookkeeping.
const Int MAX_POLYGON_NEIGHBORS	= 3;
const Int NO_NEIGHBOR			= -1;
const Byte POLY_VISIBLE			= 0x01;
const Byte POLY_PROCESSED		= 0x02;

// ---------------------------------------------------------------------------
// Scene-wide cull bounds (set per frame from terrain's max visible box).
// Used to skip shadow updates for objects with bounding boxes entirely
// outside the visible universe.
// ---------------------------------------------------------------------------
static Real bcX = 0, bcY = 0, bcZ = 0;
static Real beX = 0, beY = 0, beZ = 0;

W3DVolumetricShadowManager* TheW3DVolumetricShadowManager = nullptr;

#ifdef BUILD_WITH_D3D11
extern WorldHeightMap* GetTerrainHeightMap();

static Real GetTerrainMinHeightWorld()
{
	static WorldHeightMap* s_cachedMap = nullptr;
	static Real s_cachedMinHeight = 0.0f;

	WorldHeightMap* hmap = GetTerrainHeightMap();
	if (!hmap)
		return 0.0f;

	if (hmap != s_cachedMap)
	{
		Int minHeight = WorldHeightMap::getMaxHeightValue();
		const Int xExtent = hmap->getXExtent();
		const Int yExtent = hmap->getYExtent();
		for (Int y = 0; y < yExtent; ++y)
		{
			for (Int x = 0; x < xExtent; ++x)
				minHeight = std::min(minHeight, (Int)hmap->getHeight(x, y));
		}

		s_cachedMinHeight = (Real)minHeight * MAP_HEIGHT_SCALE;
		s_cachedMap = hmap;
	}

	return s_cachedMinHeight;
}
#endif

// ---------------------------------------------------------------------------
// Geometry — per-volume scratch storage (vertices + indices + bounds +
// visibility state). Lifted nearly verbatim from the original.
// ---------------------------------------------------------------------------
struct Geometry
{
	enum VisibleState
	{
		STATE_UNKNOWN	= 2,	// matches CollisionMath::BOTH
		STATE_VISIBLE	= 1,	// matches CollisionMath::INSIDE
		STATE_INVISIBLE	= 0,	// matches CollisionMath::OUTSIDE
	};

	Geometry(void)
		: m_verts(nullptr), m_indices(nullptr), m_numPolygon(0), m_numVertex(0)
		, m_numActivePolygon(0), m_numActiveVertex(0), m_flags(0)
		, m_visibleState(STATE_UNKNOWN)
	{ }
	~Geometry(void) { Release(); }

	Int Create(Int numVertices, Int numPolygons)
	{
		if (numVertices)
		{
			m_verts = NEW Vector3[numVertices];
			if (!m_verts)
				return FALSE;
		}
		if (numPolygons)
		{
			m_indices = NEW UnsignedShort[numPolygons * 3];
			if (!m_indices)
				return FALSE;
		}
		m_numPolygon = numPolygons;
		m_numVertex = numVertices;
		m_numActivePolygon = 0;
		m_numActiveVertex = 0;
		return TRUE;
	}
	void Release(void)
	{
		if (m_verts) { delete[] m_verts; m_verts = nullptr; }
		if (m_indices) { delete[] m_indices; m_indices = nullptr; }
		m_numActivePolygon = m_numPolygon = 0;
		m_numActiveVertex = m_numVertex = 0;
	}
	Int GetFlags(void) { return m_flags; }
	void SetFlags(Int flags) { m_flags = flags; }
	Int GetNumPolygon(void) { return m_numPolygon; }
	Int GetNumVertex(void) { return m_numVertex; }
	Int GetNumActivePolygon(void) { return m_numActivePolygon; }
	Int GetNumActiveVertex(void) { return m_numActiveVertex; }
	Int SetNumActivePolygon(Int n) { return m_numActivePolygon = n; }
	Int SetNumActiveVertex(Int n) { return m_numActiveVertex = n; }
	UnsignedShort* GetPolygonIndex(long dwPolyId, short* out) const
	{
		*out++ = m_indices[dwPolyId * 3];
		*out++ = m_indices[dwPolyId * 3 + 1];
		*out++ = m_indices[dwPolyId * 3 + 2];
		return &m_indices[dwPolyId];
	}
	Int SetPolygonIndex(long dwPolyId, short* in)
	{
		m_indices[dwPolyId * 3]		= in[0];
		m_indices[dwPolyId * 3 + 1]	= in[1];
		m_indices[dwPolyId * 3 + 2]	= in[2];
		return 3;
	}
	Vector3* GetVertex(int dwVertId) { return &m_verts[dwVertId]; }
	const Vector3* SetVertex(int dwVertId, const Vector3* v) { m_verts[dwVertId] = *v; return v; }

	AABoxClass& getBoundingBox(void) { return m_boundingBox; }
	void setBoundingBox(const AABoxClass& b) { m_boundingBox = b; }
	void setBoundingSphere(const SphereClass& s) { m_boundingSphere = s; }
	SphereClass& getBoundingSphere(void) { return m_boundingSphere; }
	void setVisibleState(VisibleState s) { m_visibleState = s; }
	VisibleState getVisibleState(void) { return m_visibleState; }

private:
	Vector3*		m_verts;
	UnsignedShort*	m_indices;
	Int				m_numPolygon;
	Int				m_numVertex;
	Int				m_numActivePolygon;
	Int				m_numActiveVertex;
	Int				m_flags;
	AABoxClass		m_boundingBox;
	SphereClass		m_boundingSphere;
	VisibleState	m_visibleState;
};

// ---------------------------------------------------------------------------
// Per-polygon neighbor data (shared-edge adjacency for silhouette extraction)
// ---------------------------------------------------------------------------
typedef struct _NeighborEdge
{
	Short neighborIndex;
	Short neighborEdgeIndex[2];
} NeighborEdge;

struct PolyNeighbor
{
	Short			myIndex;
	Byte			status;
	NeighborEdge	neighbor[MAX_POLYGON_NEIGHBORS];
};

// ---------------------------------------------------------------------------
// Per-mesh shadow geometry — cleaned verts, polygon array, neighbor info.
// Created once per unique mesh name via W3DShadowGeometryManager.
// ---------------------------------------------------------------------------
class W3DShadowGeometryMesh
{
	friend class W3DShadowGeometry;
	friend class W3DVolumetricShadow;

public:
	W3DShadowGeometryMesh(void);
	~W3DShadowGeometryMesh(void);

	const Vector3& GetPolygonNormal(long dwPolyNormId) const
	{
		WWASSERT(m_polygonNormals);
		return m_polygonNormals[dwPolyNormId];
	}
	int GetNumPolygon(void) const { return m_numPolygons; }
	void buildPolygonNeighbors(void);
	void buildPolygonNormals(void)
	{
		if (!m_polygonNormals)
		{
			Vector3* tempVec = NEW Vector3[m_numPolygons];
			for (int i = 0; i < m_numPolygons; i++)
				buildPolygonNormal(i, &tempVec[i]);
			m_polygonNormals = tempVec;
		}
	}

protected:
	Vector3* buildPolygonNormal(long dwPolyNormId, Vector3* pvNorm) const
	{
		if (m_polygonNormals)
			return &(*pvNorm = m_polygonNormals[dwPolyNormId]);

		short indexList[3];
		GetPolygonIndex(dwPolyNormId, indexList);
		const Vector3& v0 = GetVertex(indexList[0]);
		const Vector3& v1 = GetVertex(indexList[1]);
		const Vector3& v2 = GetVertex(indexList[2]);

		Vector3 edge1 = v1 - v0;
		Vector3 edge2 = v1 - v2;
		Vector3::Normalized_Cross_Product(edge2, edge1, pvNorm);
		return pvNorm;
	}

	Bool allocateNeighbors(Int numPolys);
	void deleteNeighbors(void);

	PolyNeighbor* GetPolyNeighbor(Int polyIndex);
	int GetNumVertex(void) const { return m_numVerts; }

	void GetPolygonIndex(long dwPolyId, short* out) const
	{
		const TriIndex* polyi = &m_polygons[dwPolyId];
		*out++ = m_parentVerts[polyi->I];
		*out++ = m_parentVerts[polyi->J];
		*out++ = m_parentVerts[polyi->K];
	}
	const Vector3& GetVertex(int dwVertId) const { return m_verts[dwVertId]; }

	MeshClass*			m_mesh;
	Int					m_meshRobjIndex;
	const Vector3*		m_verts;
	Vector3*			m_polygonNormals;
	Int					m_numVerts;
	Int					m_numPolygons;
	const TriIndex*		m_polygons;
	UnsignedShort*		m_parentVerts;
	PolyNeighbor*		m_polyNeighbors;
	Int					m_numPolyNeighbors;
	W3DShadowGeometry*	m_parentGeometry;
};

// ---------------------------------------------------------------------------
// Shadow geometry container — holds all meshes for one hierarchy, hashed by
// robj name so siblings using the same model share the adjacency tables.
// ---------------------------------------------------------------------------
class W3DShadowGeometry : public RefCountClass, public HashableClass
{
public:
	W3DShadowGeometry(void) : m_meshCount(0), m_numTotalsVerts(0)
	{
		memset(m_namebuf, 0, sizeof(m_namebuf));
	}
	~W3DShadowGeometry(void) { }

	virtual const char* Get_Key(void) override { return m_namebuf; }

	Int init(RenderObjClass* robj);
	Int initFromHLOD(RenderObjClass* robj);
	Int initFromMesh(RenderObjClass* robj);

	const char* Get_Name(void) const { return m_namebuf; }
	void Set_Name(const char* name)
	{
		memset(m_namebuf, 0, sizeof(m_namebuf));
		strncpy(m_namebuf, name, sizeof(m_namebuf) - 1);
	}
	Int getMeshCount(void) { return m_meshCount; }
	W3DShadowGeometryMesh* getMesh(Int index) { return &m_meshList[index]; }
	int GetNumTotalVertex(void) { return m_numTotalsVerts; }

private:
	char					m_namebuf[2 * W3D_NAME_LEN];
	W3DShadowGeometryMesh	m_meshList[MAX_SHADOW_CASTER_MESHES];
	Int						m_meshCount;
	Int						m_numTotalsVerts;
};

W3DShadowGeometryMesh::W3DShadowGeometryMesh(void)
	: m_mesh(nullptr), m_meshRobjIndex(-1), m_verts(nullptr), m_polygonNormals(nullptr)
	, m_numVerts(0), m_numPolygons(0), m_polygons(nullptr), m_parentVerts(nullptr)
	, m_polyNeighbors(nullptr), m_numPolyNeighbors(0), m_parentGeometry(nullptr)
{
}

W3DShadowGeometryMesh::~W3DShadowGeometryMesh(void)
{
	deleteNeighbors();
	if (m_parentVerts)		{ delete[] m_parentVerts;	m_parentVerts = nullptr; }
	if (m_polygonNormals)	{ delete[] m_polygonNormals; m_polygonNormals = nullptr; }
}

// ---------------------------------------------------------------------------
// initFromHLOD / initFromMesh — strip duplicate verts and cache the polygon
// table. Skinned meshes and transparent meshes (w/o forced CAST_SHADOW) are
// filtered exactly as in the original so the shadow count matches reference.
// ---------------------------------------------------------------------------
Int W3DShadowGeometry::initFromHLOD(RenderObjClass* robj)
{
	HLodClass* hlod = (HLodClass*)robj;
	UnsignedShort vertParent[MAX_SHADOW_VOLUME_VERTS];

	Int i, j, k, newVertexCount;

	Int top = hlod->Get_LOD_Count() - 1;
	W3DShadowGeometryMesh* geomMesh = &m_meshList[m_meshCount];

	m_numTotalsVerts = 0;

	for (i = 0; i < hlod->Get_Lod_Model_Count(top); i++)
	{
		if (hlod->Peek_Lod_Model(top, i) && hlod->Peek_Lod_Model(top, i)->Class_ID() == RenderObjClass::CLASSID_MESH)
		{
			DEBUG_ASSERTCRASH(m_meshCount < MAX_SHADOW_CASTER_MESHES, ("Too many shadow sub-meshes"));

			geomMesh->m_mesh = (MeshClass*)hlod->Peek_Lod_Model(top, i);
			geomMesh->m_meshRobjIndex = i;

			if ((geomMesh->m_mesh->Is_Alpha() || geomMesh->m_mesh->Is_Translucent()) && !geomMesh->m_mesh->Peek_Model()->Get_Flag(MeshGeometryClass::CAST_SHADOW))
				continue;
			if (geomMesh->m_mesh->Peek_Model()->Get_Flag(MeshGeometryClass::SKIN))
				continue;

			MeshModelClass* mm = geomMesh->m_mesh->Peek_Model();
			geomMesh->m_numVerts	= mm->Get_Vertex_Count();
			geomMesh->m_verts		= mm->Get_Vertex_Array();
			geomMesh->m_numPolygons	= mm->Get_Polygon_Count();
			geomMesh->m_polygons	= mm->Get_Polygon_Array();

			if (geomMesh->m_numVerts > MAX_SHADOW_VOLUME_VERTS)
				return FALSE;

			memset(vertParent, 0xff, sizeof(vertParent));
			newVertexCount = geomMesh->m_numVerts;
			for (j = 0; j < geomMesh->m_numVerts; j++)
			{
				if (vertParent[j] != 0xffff)
					continue;
				const Vector3* v_curr = &geomMesh->m_verts[j];
				for (k = j + 1; k < geomMesh->m_numVerts; k++)
				{
					Vector3 len(*v_curr - geomMesh->m_verts[k]);
					if (len.Length2() == 0)
					{
						vertParent[k] = (UnsignedShort)j;
						newVertexCount--;
					}
				}
				vertParent[j] = (UnsignedShort)j;
			}
			geomMesh->m_parentVerts = NEW UnsignedShort[geomMesh->m_numVerts];
			memcpy(geomMesh->m_parentVerts, vertParent, sizeof(UnsignedShort) * geomMesh->m_numVerts);
			geomMesh->m_numVerts = newVertexCount;
			m_numTotalsVerts += newVertexCount;
			geomMesh->m_parentGeometry = this;

			geomMesh++;
			m_meshCount++;
		}
	}

	return m_meshCount != 0;
}

Int W3DShadowGeometry::initFromMesh(RenderObjClass* robj)
{
	UnsignedShort vertParent[MAX_SHADOW_VOLUME_VERTS];

	Int j, k, newVertexCount;
	W3DShadowGeometryMesh* geomMesh = &m_meshList[m_meshCount];

	DEBUG_ASSERTCRASH(m_meshCount < MAX_SHADOW_CASTER_MESHES, ("Too many shadow sub-meshes"));

	geomMesh->m_mesh = (MeshClass*)robj;
	geomMesh->m_meshRobjIndex = -1;

	if ((geomMesh->m_mesh->Is_Alpha() || geomMesh->m_mesh->Is_Translucent()) && !geomMesh->m_mesh->Peek_Model()->Get_Flag(MeshGeometryClass::CAST_SHADOW))
		return FALSE;

	MeshModelClass* mm = geomMesh->m_mesh->Peek_Model();
	geomMesh->m_numVerts	= mm->Get_Vertex_Count();
	geomMesh->m_verts		= mm->Get_Vertex_Array();
	geomMesh->m_numPolygons	= mm->Get_Polygon_Count();
	geomMesh->m_polygons	= mm->Get_Polygon_Array();

	if (geomMesh->m_numVerts > MAX_SHADOW_VOLUME_VERTS)
		return FALSE;

	memset(vertParent, 0xff, sizeof(vertParent));
	newVertexCount = geomMesh->m_numVerts;
	for (j = 0; j < geomMesh->m_numVerts; j++)
	{
		if (vertParent[j] != 0xffff)
			continue;
		const Vector3* v_curr = &geomMesh->m_verts[j];
		for (k = j + 1; k < geomMesh->m_numVerts; k++)
		{
			Vector3 len(*v_curr - geomMesh->m_verts[k]);
			if (len.Length2() == 0)
			{
				vertParent[k] = (UnsignedShort)j;
				newVertexCount--;
			}
		}
		vertParent[j] = (UnsignedShort)j;
	}

	geomMesh->m_parentVerts = NEW UnsignedShort[geomMesh->m_numVerts];
	memcpy(geomMesh->m_parentVerts, vertParent, sizeof(UnsignedShort) * geomMesh->m_numVerts);
	geomMesh->m_numVerts = newVertexCount;
	geomMesh->m_parentGeometry = this;

	m_meshCount = 1;
	m_numTotalsVerts = newVertexCount;

	return TRUE;
}

Int W3DShadowGeometry::init(RenderObjClass* /*robj*/)
{
	return TRUE;
}

// ---------------------------------------------------------------------------
// buildPolygonNeighbors — detect shared edges with correct winding so each
// triangle's up-to-3 neighbors are recorded. Port of the original inner loop.
// ---------------------------------------------------------------------------
PolyNeighbor* W3DShadowGeometryMesh::GetPolyNeighbor(Int polyIndex)
{
	if (!m_polyNeighbors)
		buildPolygonNeighbors();

	if (polyIndex < 0 || polyIndex >= m_numPolyNeighbors)
		return nullptr;
	return &m_polyNeighbors[polyIndex];
}

void W3DShadowGeometryMesh::buildPolygonNeighbors(void)
{
	Int numPolys;
	Int i, j;

	buildPolygonNormals();
	numPolys = GetNumPolygon();

	if (numPolys == 0)
	{
		if (m_numPolyNeighbors != 0)
			deleteNeighbors();
		return;
	}

	if (numPolys != m_numPolyNeighbors)
	{
		deleteNeighbors();
		if (allocateNeighbors(numPolys) == FALSE)
			return;
	}

	for (i = 0; i < m_numPolyNeighbors; i++)
	{
		m_polyNeighbors[i].myIndex = (Short)i;
		for (j = 0; j < MAX_POLYGON_NEIGHBORS; j++)
			m_polyNeighbors[i].neighbor[j].neighborIndex = (Short)NO_NEIGHBOR;
	}

	for (i = 0; i < m_numPolyNeighbors; i++)
	{
		Short poly[3];
		Short otherPoly[3];

		GetPolygonIndex(i, poly);
		const Vector3& vNorm = GetPolygonNormal(i);

		for (j = 0; j < m_numPolyNeighbors; j++)
		{
			Int a, b;
			Int index1, index2;
			Int index1Pos[2];
			Int diff1, diff2;

			if (i == j)
				continue;

			GetPolygonIndex(j, otherPoly);

			index1 = -1;
			index2 = -1;
			for (a = 0; a < 3; a++)
				for (b = 0; b < 3; b++)
					if (poly[a] == otherPoly[b])
					{
						if (index1 == -1)
						{
							index1 = poly[a];
							index1Pos[0] = a;
							index1Pos[1] = b;
						}
						else if (index2 == -1)
						{
							diff1 = a - index1Pos[0];
							diff2 = b - index1Pos[1];
							if (((diff1 & 0x80000000) ^ ((abs(diff1) & 2) << 30)) != ((diff2 & 0x80000000) ^ ((abs(diff2) & 2) << 30)))
							{
								const Vector3& vOtherNorm = GetPolygonNormal(j);
								if (fabs(Vector3::Dot_Product(vOtherNorm, vNorm) + 1.0f) <= 0.01f)
									continue;
								index2 = poly[a];
							}
							else
								continue;
						}
						else
						{
							index1 = index2 = -1;
							continue;
						}
					}

			if (index1 != -1 && index2 != -1)
			{
				for (a = 0; a < MAX_POLYGON_NEIGHBORS; a++)
					if (m_polyNeighbors[i].neighbor[a].neighborIndex == NO_NEIGHBOR)
					{
						m_polyNeighbors[i].neighbor[a].neighborIndex = (Short)j;
						m_polyNeighbors[i].neighbor[a].neighborEdgeIndex[0] = (Short)index1;
						m_polyNeighbors[i].neighbor[a].neighborEdgeIndex[1] = (Short)index2;
						break;
					}
			}
		}
	}
}

Bool W3DShadowGeometryMesh::allocateNeighbors(Int numPolys)
{
	m_polyNeighbors = NEW PolyNeighbor[numPolys];
	if (m_polyNeighbors == nullptr)
		return FALSE;
	m_numPolyNeighbors = numPolys;
	return TRUE;
}

void W3DShadowGeometryMesh::deleteNeighbors(void)
{
	if (m_polyNeighbors)
	{
		delete[] m_polyNeighbors;
		m_polyNeighbors = nullptr;
		m_numPolyNeighbors = 0;
	}
}

// ---------------------------------------------------------------------------
// W3DShadowGeometryManager — hashes geometry by robj name so instances of
// the same model reuse the adjacency info.
// ---------------------------------------------------------------------------
class W3DShadowGeometryManager
{
public:
	W3DShadowGeometryManager(void);
	~W3DShadowGeometryManager(void);

	int					Load_Geom(RenderObjClass* robj, const char* name);
	W3DShadowGeometry*	Get_Geom(const char* name);
	W3DShadowGeometry*	Peek_Geom(const char* name);
	Bool				Add_Geom(W3DShadowGeometry* new_anim);
	void				Free_All_Geoms(void);

	void	Register_Missing(const char* name);
	Bool	Is_Missing(const char* name);
	void	Reset_Missing(void);

private:
	HashTableClass* GeomPtrTable;
	HashTableClass* MissingGeomTable;
	friend class W3DShadowGeometryManagerIterator;
};

class W3DShadowGeometryManagerIterator : public HashTableIteratorClass
{
public:
	W3DShadowGeometryManagerIterator(W3DShadowGeometryManager& manager) : HashTableIteratorClass(*manager.GeomPtrTable) { }
	W3DShadowGeometry* Get_Current_Geom(void) { return (W3DShadowGeometry*)Get_Current(); }
};

class MissingGeomClass : public HashableClass
{
public:
	MissingGeomClass(const char* name)
	{
		strncpy(m_name, name, sizeof(m_name) - 1);
		m_name[sizeof(m_name) - 1] = 0;
	}
	virtual ~MissingGeomClass(void) { }
	virtual const char* Get_Key(void) override { return m_name; }
private:
	char m_name[2 * W3D_NAME_LEN];
};

W3DShadowGeometryManager::W3DShadowGeometryManager(void)
{
	GeomPtrTable = NEW HashTableClass(2048);
	MissingGeomTable = NEW HashTableClass(2048);
}

W3DShadowGeometryManager::~W3DShadowGeometryManager(void)
{
	Free_All_Geoms();
	delete GeomPtrTable;
	GeomPtrTable = nullptr;
	delete MissingGeomTable;
	MissingGeomTable = nullptr;
}

void W3DShadowGeometryManager::Free_All_Geoms(void)
{
	W3DShadowGeometryManagerIterator it(*this);
	for (it.First(); !it.Is_Done(); it.Next())
	{
		W3DShadowGeometry* geom = it.Get_Current_Geom();
		geom->Release_Ref();
	}
	GeomPtrTable->Reset();
}

W3DShadowGeometry* W3DShadowGeometryManager::Peek_Geom(const char* name)
{
	return (W3DShadowGeometry*)GeomPtrTable->Find(name);
}

W3DShadowGeometry* W3DShadowGeometryManager::Get_Geom(const char* name)
{
	W3DShadowGeometry* geom = Peek_Geom(name);
	if (geom != nullptr)
		geom->Add_Ref();
	return geom;
}

Bool W3DShadowGeometryManager::Add_Geom(W3DShadowGeometry* new_geom)
{
	WWASSERT(new_geom != nullptr);
	new_geom->Add_Ref();
	GeomPtrTable->Add(new_geom);
	return true;
}

void W3DShadowGeometryManager::Register_Missing(const char* name)
{
	MissingGeomTable->Add(NEW MissingGeomClass(name));
}

Bool W3DShadowGeometryManager::Is_Missing(const char* name)
{
	return (MissingGeomTable->Find(name) != nullptr);
}

void W3DShadowGeometryManager::Reset_Missing(void)
{
	MissingGeomTable->Reset();
}

int W3DShadowGeometryManager::Load_Geom(RenderObjClass* robj, const char* name)
{
	Bool res = FALSE;
	W3DShadowGeometry* newgeom = NEW W3DShadowGeometry;
	if (newgeom == nullptr)
		return 1;

	SET_REF_OWNER(newgeom);
	newgeom->Set_Name(name);

	switch (robj->Class_ID())
	{
	case RenderObjClass::CLASSID_HLOD:
		res = newgeom->initFromHLOD(robj);
		break;
	case RenderObjClass::CLASSID_MESH:
		res = newgeom->initFromMesh(robj);
		break;
	default:
		break;
	}

	if (res != TRUE)
	{
		newgeom->Release_Ref();
		return 1;
	}
	if (Peek_Geom(newgeom->Get_Name()) != nullptr)
	{
		newgeom->Release_Ref();
		return 1;
	}
	Add_Geom(newgeom);
	newgeom->Release_Ref();
	return 0;
}

// ---------------------------------------------------------------------------
// DX11 renderer plumbing — file-local pipeline state that lives for the
// lifetime of TheW3DVolumetricShadowManager.
// ---------------------------------------------------------------------------

#ifdef BUILD_WITH_D3D11
namespace
{
	// Darkening-pass cbuffer mirrors HLSL's DarkenConstants (b0).
	struct SvDarkenConstants
	{
		float shadowColor[4];
	};

	struct SvPipeline
	{
		Render::Shader			extrudeShader;		// VS+PS (color writes off, stencil-only)
		Render::Shader			darkenShader;		// fullscreen darkening
		bool					extrudeReady	= false;
		bool					darkenReady		= false;

		// Extrude pass (stencil-only, ZPass): depth test LE, no depth write,
		// two-sided stencil — front incr, back decr.
		ID3D11DepthStencilState*	dsExtrudeZPass			= nullptr;
		// ZFail variant — swaps incr/decr to DepthFailOp to handle the
		// camera-inside-volume case. Selected when the near plane of the
		// camera is inside the extruded volume.
		ID3D11DepthStencilState*	dsExtrudeZFail			= nullptr;
		// Darken pass: stencil NOT_EQUAL 0, no depth test.
		ID3D11DepthStencilState*	dsDarken				= nullptr;
		// Blend — color writes off during volume pass.
		ID3D11BlendState*			blendNoColor			= nullptr;
		// Blend — multiplicative darken (DEST * SRC)
		ID3D11BlendState*			blendMultiply			= nullptr;
		// Rasterizer — two-sided so the two-sided stencil ops can fire for
		// both front and back faces in a single draw.
		ID3D11RasterizerState*		rasterNoCull			= nullptr;

		// Cbuffer for the darken pass (shadowColor).
		ID3D11Buffer*				darkenCB				= nullptr;
		// Scratch cbuffer for the per-draw world matrix.
		ID3D11Buffer*				worldCB					= nullptr;
	};

	SvPipeline g_svPipeline;

	// Keeps the last VB bound across successive RenderVolume calls so we can
	// skip redundant IASetVertexBuffers (matches the `lastActiveVertexBuffer`
	// optimization in the original).
	static ID3D11Buffer* g_lastBoundVB = nullptr;

	// Dynamic VB/IB for per-frame animated shadow volumes (immediate path).
	// Matches shadowVertexBufferD3D / shadowIndexBufferD3D in the original.
	static ID3D11Buffer*	g_dynamicVB			= nullptr;
	static ID3D11Buffer*	g_dynamicIB			= nullptr;
	static UINT				g_dynamicVBBytes	= 0;
	static UINT				g_dynamicIBBytes	= 0;
	static UINT				g_dynamicVBCapacity	= 4096 * 16;	// 16 bytes/vtx (XYZD)
	static UINT				g_dynamicIBCapacity	= 8192 * 2;	// 16-bit indices
	// Write cursors — reset at start of renderShadows(); carry between
	// DrawDynamic calls within a single frame.
	static UINT				g_dynamicVBCursor	= 0;
	static UINT				g_dynamicIBCursor	= 0;
	// First map per frame must use WRITE_DISCARD so we don't race the GPU
	// reads from the previous frame sitting at cursor=0. Flipped back after
	// the initial Map call, reset to true at top of renderShadows.
	static Bool				g_dynamicFirstMap	= true;

	bool SetVolShadowCreateDeviceObjects(Render::Device& dev)
	{
		if (g_svPipeline.extrudeReady && g_svPipeline.darkenReady)
			return true;

		// Extrude VS/PS + input layout (POSITION float3).
		if (!g_svPipeline.extrudeShader.CompileVS(dev, Render::g_shaderShadowVolumeExtrude, "VSMain"))
			return false;
		if (!g_svPipeline.extrudeShader.CompilePS(dev, Render::g_shaderShadowVolumeExtrude, "PSMain"))
			return false;

		Render::VertexAttribute extrudeLayout[] = {
			{ "POSITION", 0, Render::VertexFormat::Float3, 0 },
		};
		if (!g_svPipeline.extrudeShader.CreateInputLayout(dev, extrudeLayout, 1, sizeof(float) * 3))
			return false;

		// Darken pass uses a generated fullscreen tri, no VB, no input layout.
		if (!g_svPipeline.darkenShader.CompileVS(dev, Render::g_shaderShadowVolumeDarken, "VSMain"))
			return false;
		if (!g_svPipeline.darkenShader.CompilePS(dev, Render::g_shaderShadowVolumeDarken, "PSMain"))
			return false;

		ID3D11Device* d3d = dev.GetDevice();
		if (!d3d)
			return false;

		// Two-sided stencil — front face INCR_SAT, back face DECR_SAT (ZPass).
		D3D11_DEPTH_STENCIL_DESC ds = {};
		ds.DepthEnable					= TRUE;
		ds.DepthWriteMask				= D3D11_DEPTH_WRITE_MASK_ZERO;
		ds.DepthFunc					= D3D11_COMPARISON_LESS_EQUAL;
		ds.StencilEnable				= TRUE;
		ds.StencilReadMask				= 0xFF;
		ds.StencilWriteMask				= 0xFF;
		ds.FrontFace.StencilFunc		= D3D11_COMPARISON_ALWAYS;
		ds.FrontFace.StencilPassOp		= D3D11_STENCIL_OP_INCR_SAT;
		ds.FrontFace.StencilDepthFailOp	= D3D11_STENCIL_OP_KEEP;
		ds.FrontFace.StencilFailOp		= D3D11_STENCIL_OP_KEEP;
		ds.BackFace.StencilFunc			= D3D11_COMPARISON_ALWAYS;
		ds.BackFace.StencilPassOp		= D3D11_STENCIL_OP_DECR_SAT;
		ds.BackFace.StencilDepthFailOp	= D3D11_STENCIL_OP_KEEP;
		ds.BackFace.StencilFailOp		= D3D11_STENCIL_OP_KEEP;
		if (FAILED(d3d->CreateDepthStencilState(&ds, &g_svPipeline.dsExtrudeZPass)))
			return false;

		// ZFail variant — front DECR_SAT on depth-fail, back INCR_SAT on depth-fail.
		ds.FrontFace.StencilPassOp		= D3D11_STENCIL_OP_KEEP;
		ds.FrontFace.StencilDepthFailOp	= D3D11_STENCIL_OP_DECR_SAT;
		ds.BackFace.StencilPassOp		= D3D11_STENCIL_OP_KEEP;
		ds.BackFace.StencilDepthFailOp	= D3D11_STENCIL_OP_INCR_SAT;
		if (FAILED(d3d->CreateDepthStencilState(&ds, &g_svPipeline.dsExtrudeZFail)))
			return false;

		// Darken pass — stencil NOT_EQUAL 0, no depth test.
		D3D11_DEPTH_STENCIL_DESC dsDark = {};
		dsDark.DepthEnable				= FALSE;
		dsDark.DepthWriteMask			= D3D11_DEPTH_WRITE_MASK_ZERO;
		dsDark.StencilEnable			= TRUE;
		dsDark.StencilReadMask			= 0xFF;
		dsDark.StencilWriteMask			= 0x00;
		dsDark.FrontFace.StencilFunc		= D3D11_COMPARISON_NOT_EQUAL;
		dsDark.FrontFace.StencilPassOp		= D3D11_STENCIL_OP_KEEP;
		dsDark.FrontFace.StencilDepthFailOp	= D3D11_STENCIL_OP_KEEP;
		dsDark.FrontFace.StencilFailOp		= D3D11_STENCIL_OP_KEEP;
		dsDark.BackFace						= dsDark.FrontFace;
		if (FAILED(d3d->CreateDepthStencilState(&dsDark, &g_svPipeline.dsDarken)))
			return false;

		// Blend — color writes off for the stencil-only volume pass.
		D3D11_BLEND_DESC bd = {};
		bd.RenderTarget[0].BlendEnable				= FALSE;
		bd.RenderTarget[0].RenderTargetWriteMask	= 0;
		if (FAILED(d3d->CreateBlendState(&bd, &g_svPipeline.blendNoColor)))
			return false;

		// Blend — multiplicative darken (DEST * SRC). Matches the original
		// D3DBLEND_DESTCOLOR / D3DBLEND_ZERO path used by renderStencilShadows.
		D3D11_BLEND_DESC bmul = {};
		bmul.RenderTarget[0].BlendEnable			= TRUE;
		bmul.RenderTarget[0].SrcBlend				= D3D11_BLEND_DEST_COLOR;
		bmul.RenderTarget[0].DestBlend				= D3D11_BLEND_ZERO;
		bmul.RenderTarget[0].BlendOp				= D3D11_BLEND_OP_ADD;
		bmul.RenderTarget[0].SrcBlendAlpha			= D3D11_BLEND_ONE;
		bmul.RenderTarget[0].DestBlendAlpha			= D3D11_BLEND_ZERO;
		bmul.RenderTarget[0].BlendOpAlpha			= D3D11_BLEND_OP_ADD;
		bmul.RenderTarget[0].RenderTargetWriteMask	= D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(d3d->CreateBlendState(&bmul, &g_svPipeline.blendMultiply)))
			return false;

		// Rasterizer — cull off so the two-sided stencil ops fire on both
		// face orientations. The DX11 two-sided stencil description is
		// what actually drives the INCR/DECR split; the rasterizer just
		// needs to feed both.
		//
		// FrontCounterClockwise=TRUE: the original D3D8 code rendered CCW
		// triangles with INCR (pass 1: CULL_CW) and CW with DECR (pass 2:
		// CULL_CCW). With the default DX11 convention (CW=front) our
		// FrontFace INCR/BackFace DECR fires INCR on CW, which is INVERTED
		// relative to the original — INCR ends up on the back of the volume
		// and DECR on the front, leaving stencil positive on visible side
		// walls (the "standing pillar" artifact). Flipping to CCW=front
		// restores the original semantics.
		D3D11_RASTERIZER_DESC rs = {};
		rs.FillMode				= D3D11_FILL_SOLID;
		rs.CullMode				= D3D11_CULL_NONE;
		rs.FrontCounterClockwise	= TRUE;
		rs.DepthClipEnable		= TRUE;
		if (FAILED(d3d->CreateRasterizerState(&rs, &g_svPipeline.rasterNoCull)))
			return false;

		// Darken cbuffer (b0 — shadowColor).
		D3D11_BUFFER_DESC cbd = {};
		cbd.ByteWidth		= sizeof(SvDarkenConstants);
		cbd.Usage			= D3D11_USAGE_DYNAMIC;
		cbd.BindFlags		= D3D11_BIND_CONSTANT_BUFFER;
		cbd.CPUAccessFlags	= D3D11_CPU_ACCESS_WRITE;
		if (FAILED(d3d->CreateBuffer(&cbd, nullptr, &g_svPipeline.darkenCB)))
			return false;

		// Scratch Object CB (b1) for the per-draw world matrix.
		D3D11_BUFFER_DESC wcbd = {};
		wcbd.ByteWidth		= sizeof(Render::ObjectConstants);
		wcbd.Usage			= D3D11_USAGE_DYNAMIC;
		wcbd.BindFlags		= D3D11_BIND_CONSTANT_BUFFER;
		wcbd.CPUAccessFlags	= D3D11_CPU_ACCESS_WRITE;
		if (FAILED(d3d->CreateBuffer(&wcbd, nullptr, &g_svPipeline.worldCB)))
			return false;

		// Dynamic VB/IB for per-frame dynamic shadow volumes.
		D3D11_BUFFER_DESC vbd = {};
		vbd.ByteWidth		= g_dynamicVBCapacity;
		vbd.Usage			= D3D11_USAGE_DYNAMIC;
		vbd.BindFlags		= D3D11_BIND_VERTEX_BUFFER;
		vbd.CPUAccessFlags	= D3D11_CPU_ACCESS_WRITE;
		if (FAILED(d3d->CreateBuffer(&vbd, nullptr, &g_dynamicVB)))
			return false;
		g_dynamicVBBytes = g_dynamicVBCapacity;

		D3D11_BUFFER_DESC ibd = {};
		ibd.ByteWidth		= g_dynamicIBCapacity;
		ibd.Usage			= D3D11_USAGE_DYNAMIC;
		ibd.BindFlags		= D3D11_BIND_INDEX_BUFFER;
		ibd.CPUAccessFlags	= D3D11_CPU_ACCESS_WRITE;
		if (FAILED(d3d->CreateBuffer(&ibd, nullptr, &g_dynamicIB)))
			return false;
		g_dynamicIBBytes = g_dynamicIBCapacity;

		g_svPipeline.extrudeReady = true;
		g_svPipeline.darkenReady = true;
		return true;
	}

	void SetVolShadowReleaseDeviceObjects(void)
	{
		if (g_svPipeline.dsExtrudeZPass)	{ g_svPipeline.dsExtrudeZPass->Release();	g_svPipeline.dsExtrudeZPass = nullptr; }
		if (g_svPipeline.dsExtrudeZFail)	{ g_svPipeline.dsExtrudeZFail->Release();	g_svPipeline.dsExtrudeZFail = nullptr; }
		if (g_svPipeline.dsDarken)			{ g_svPipeline.dsDarken->Release();			g_svPipeline.dsDarken = nullptr; }
		if (g_svPipeline.blendNoColor)		{ g_svPipeline.blendNoColor->Release();		g_svPipeline.blendNoColor = nullptr; }
		if (g_svPipeline.blendMultiply)		{ g_svPipeline.blendMultiply->Release();	g_svPipeline.blendMultiply = nullptr; }
		if (g_svPipeline.rasterNoCull)		{ g_svPipeline.rasterNoCull->Release();		g_svPipeline.rasterNoCull = nullptr; }
		if (g_svPipeline.darkenCB)			{ g_svPipeline.darkenCB->Release();			g_svPipeline.darkenCB = nullptr; }
		if (g_svPipeline.worldCB)			{ g_svPipeline.worldCB->Release();			g_svPipeline.worldCB = nullptr; }
		if (g_dynamicVB)					{ g_dynamicVB->Release();					g_dynamicVB = nullptr; }
		if (g_dynamicIB)					{ g_dynamicIB->Release();					g_dynamicIB = nullptr; }
		g_svPipeline.extrudeReady = false;
		g_svPipeline.darkenReady = false;
	}

	// Upload a world matrix to the shared ObjectConstants cbuffer (b1). The
	// renderer's default Shader3D already uses this cbuffer so we piggyback
	// on it — its contents get reset on the next Draw3D call.
	void SetVolShadowUploadWorld(Render::Device& dev, const Matrix3D* meshXform)
	{
		Render::ObjectConstants oc = {};
		// DX11: W3D Matrix3D is column-vector convention; the shader's
		// `mul(v, world)` expects a row-major matrix with the transpose
		// baked in. RenderUtils::Matrix3DToFloat4x4 does exactly that —
		// identical to how Shader3D's ObjectConstants upload works.
		// Direct memcpy here was producing a transposed world matrix,
		// sending every volume vertex to a garbage world position
		// (hence "no shadows on units + triangles mid-air").
		oc.world = RenderUtils::Matrix3DToFloat4x4(*meshXform);
		oc.color = { 1, 1, 1, 1 };
		oc.shaderParams = { 0, 0, 0, 0 };

		// Find / write the object cbuffer. The Renderer exposes it via
		// internal Draw3D helpers, but here we need a direct write — use a
		// tiny Map on a staged cbuffer.
		ID3D11DeviceContext* ctx = dev.GetContext();

		if (!g_svPipeline.worldCB)
			return;

		D3D11_MAPPED_SUBRESOURCE m = {};
		if (SUCCEEDED(ctx->Map(g_svPipeline.worldCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
		{
			memcpy(m.pData, &oc, sizeof(oc));
			ctx->Unmap(g_svPipeline.worldCB, 0);
		}
		ctx->VSSetConstantBuffers(1, 1, &g_svPipeline.worldCB);
	}
}
#endif // BUILD_WITH_D3D11

// ---------------------------------------------------------------------------
// W3DVolumetricShadow implementation
// ---------------------------------------------------------------------------
Geometry W3DVolumetricShadow::m_tempShadowVolume;

W3DVolumetricShadow::W3DVolumetricShadow(void)
{
	Int i, j;

	m_next = nullptr;
	m_geometry = nullptr;
	m_shadowLengthScale = 0.0f;
	m_extraExtrusionPadding = 0.0f;
	m_robj = nullptr;
	m_isEnabled = TRUE;
	m_isInvisibleEnabled = FALSE;

	for (j = 0; j < MAX_SHADOW_CASTER_MESHES; j++)
	{
		m_numSilhouetteIndices[j] = 0;
		m_maxSilhouetteEntries[j] = 0;
		m_silhouetteIndex[j] = nullptr;
		m_shadowVolumeCount[j] = 0;
	}

	for (i = 0; i < MAX_SHADOW_LIGHTS; i++)
	{
		for (j = 0; j < MAX_SHADOW_CASTER_MESHES; j++)
		{
			m_shadowVolume[i][j] = nullptr;
			m_shadowVolumeVB[i][j] = nullptr;
			m_shadowVolumeIB[i][j] = nullptr;
			m_shadowVolumeRenderTask[i][j].m_parentShadow = this;
			m_shadowVolumeRenderTask[i][j].m_meshIndex = (UnsignedByte)j;
			m_shadowVolumeRenderTask[i][j].m_lightIndex = (UnsignedByte)i;
			m_shadowVolumeRenderTask[i][j].m_nextTask = nullptr;
			m_objectXformHistory[i][j].Make_Identity();
			m_lightPosHistory[i][j] = Vector3(0, 0, 0);
		}
	}
}

W3DVolumetricShadow::~W3DVolumetricShadow(void)
{
	Int i, j;

	for (j = 0; j < MAX_SHADOW_CASTER_MESHES; j++)
		deleteSilhouette(j);

	for (i = 0; i < MAX_SHADOW_LIGHTS; i++)
	{
		for (j = 0; j < MAX_SHADOW_CASTER_MESHES; j++)
		{
			if (m_shadowVolume[i][j])
				delete m_shadowVolume[i][j];
			if (m_shadowVolumeVB[i][j])
				TheW3DBufferManager->releaseSlot(m_shadowVolumeVB[i][j]);
			if (m_shadowVolumeIB[i][j])
				TheW3DBufferManager->releaseSlot(m_shadowVolumeIB[i][j]);
		}
	}

	if (m_geometry)
		REF_PTR_RELEASE(m_geometry);
	m_geometry = nullptr;
	m_robj = nullptr;
}

void W3DVolumetricShadow::SetGeometry(W3DShadowGeometry* geometry)
{
	Short numPrevVertices = 0;
	Short numNewVertices = 0;

	for (Int i = 0; i < MAX_SHADOW_CASTER_MESHES; i++)
	{
		if (m_geometry && i < m_geometry->getMeshCount())
			numPrevVertices = (Short)m_geometry->getMesh(i)->GetNumVertex();
		else
			numPrevVertices = 0;

		if (geometry && i < geometry->getMeshCount())
			numNewVertices = (Short)geometry->getMesh(i)->GetNumVertex();
		else
			numNewVertices = 0;

		if (numNewVertices > numPrevVertices)
		{
			deleteSilhouette(i);
			if (allocateSilhouette(i, numNewVertices) == FALSE)
				return;
		}
	}

	m_geometry = geometry;
}

// ---------------------------------------------------------------------------
// updateOptimalExtrusionPadding — expensive one-time rabbit hole to figure
// out how far the shadow volume must extrude to always touch the ground
// (objects on cliff edges need longer extrusions). DX11: we call the same
// terrain-sampling helpers the original used; the math is identical.
// ---------------------------------------------------------------------------
void W3DVolumetricShadow::updateOptimalExtrusionPadding(void)
{
	// Distance from the caster's origin down to terrain under it, plus a
	// small safety margin. The volume extrusion math uses this as the
	// "virtual floor" depth — if it's too small the shadow volume never
	// reaches real ground and stencil INCR/DECR don't balance out on
	// scene pixels, which shows up as air-standing grey quads.
	//
	// Original did a cliff-edge raycast via TheTerrainRenderObject->Cast_Ray
	// plus a safety walk along the object's X/Y footprint. We approximate
	// with getGroundHeight at the unit's XY position; cliff-edge units may
	// under-extrude until the raycast helper is ported.
	if (m_robj && TheTerrainLogic)
	{
		Vector3 pos = m_robj->Get_Position();
		Real ground = TheTerrainLogic->getGroundHeight(pos.X, pos.Y);
		Real padding = pos.Z - ground + SHADOW_EXTRUSION_BUFFER;
		if (padding < SHADOW_EXTRUSION_BUFFER)
			padding = SHADOW_EXTRUSION_BUFFER;
		m_extraExtrusionPadding = padding;
	}
	else
	{
		m_extraExtrusionPadding = SHADOW_EXTRUSION_BUFFER;
	}
}

// ---------------------------------------------------------------------------
// Render paths
// ---------------------------------------------------------------------------
void W3DVolumetricShadow::RenderVolume(Int meshIndex, Int lightIndex)
{
	if (!m_robj || !m_geometry)
		return;
	if (lightIndex < 0 || lightIndex >= MAX_SHADOW_LIGHTS || meshIndex < 0 || meshIndex >= MAX_SHADOW_CASTER_MESHES)
		return;
	if (!m_shadowVolume[lightIndex][meshIndex])
		return;

	HLodClass* hlod = (HLodClass*)m_robj;
	MeshClass* mesh = nullptr;

	Int meshRobjIndex = m_geometry->getMesh(meshIndex)->m_meshRobjIndex;

	if (meshRobjIndex >= 0)
		mesh = (MeshClass*)hlod->Peek_Lod_Model(0, meshRobjIndex);
	else
		mesh = (MeshClass*)m_robj;

	if (mesh)
	{
		// Bug fix: original indexed [0] here — should be [lightIndex].
		if (m_shadowVolume[lightIndex][meshIndex]->GetFlags() & SHADOW_DYNAMIC)
			RenderDynamicMeshVolume(meshIndex, lightIndex, &mesh->Get_Transform());
		else
			RenderMeshVolume(meshIndex, lightIndex, &mesh->Get_Transform());
	}
}

void W3DVolumetricShadow::RenderMeshVolume(Int meshIndex, Int lightIndex, const Matrix3D* meshXform)
{
#ifdef BUILD_WITH_D3D11
	Geometry* geometry = m_shadowVolume[lightIndex][meshIndex];
	if (!geometry)
		return;

	Int numVerts = geometry->GetNumActiveVertex();
	Int numPolys = geometry->GetNumActivePolygon();
	Int numIndex = numPolys * 3;

	if (numVerts == 0 || numPolys == 0)
		return;

	W3DBufferManager::W3DVertexBufferSlot* vbSlot = m_shadowVolumeVB[lightIndex][meshIndex];
	W3DBufferManager::W3DIndexBufferSlot*  ibSlot = m_shadowVolumeIB[lightIndex][meshIndex];
	if (!vbSlot || !ibSlot)
		return;

	Render::Device& dev = Render::Renderer::Instance().GetDevice();
	ID3D11DeviceContext* ctx = dev.GetContext();

	ID3D11Buffer* vb = vbSlot->m_VB->m_buffer;
	ID3D11Buffer* ib = ibSlot->m_IB->m_buffer;
	if (!vb || !ib)
		return;

	// Bind VB only if it changed (mirrors original's lastActiveVertexBuffer opt).
	if (vb != g_lastBoundVB)
	{
		UINT stride = (UINT)W3DBufferManager::getStride(vbSlot->m_VB->m_format);
		UINT offset = 0;
		ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
		g_lastBoundVB = vb;
	}
	ctx->IASetIndexBuffer(ib, DXGI_FORMAT_R16_UINT, 0);

	SetVolShadowUploadWorld(dev, meshXform);

	ctx->DrawIndexed((UINT)numIndex, (UINT)ibSlot->m_start, (INT)vbSlot->m_start);
#endif
}

void W3DVolumetricShadow::RenderDynamicMeshVolume(Int meshIndex, Int lightIndex, const Matrix3D* meshXform)
{
#ifdef BUILD_WITH_D3D11
	Geometry* geometry = m_shadowVolume[lightIndex][meshIndex];
	if (!geometry)
		return;

	Int numVerts = geometry->GetNumActiveVertex();
	Int numPolys = geometry->GetNumActivePolygon();
	Int numIndex = numPolys * 3;

	if (numVerts == 0 || numPolys == 0)
		return;

	Render::Device& dev = Render::Renderer::Instance().GetDevice();
	ID3D11DeviceContext* ctx = dev.GetContext();
	if (!g_dynamicVB || !g_dynamicIB)
		return;

	const UINT vertStride = sizeof(Vector3);	// DX11: dynamic path uses plain XYZ.
	const UINT idxStride = sizeof(UINT16);

	UINT vtxBytes = (UINT)numVerts * vertStride;
	UINT idxBytes = (UINT)numIndex * idxStride;

	// Discard-and-reset when the dynamic buffer would overflow — mirrors the
	// D3DLOCK_DISCARD path. Also force DISCARD on the first map of each
	// frame so we don't race the GPU's in-flight reads from the previous
	// frame at cursor=0.
	D3D11_MAP vbMap = D3D11_MAP_WRITE_NO_OVERWRITE;
	if (g_dynamicFirstMap || g_dynamicVBCursor + vtxBytes > g_dynamicVBBytes)
	{
		vbMap = D3D11_MAP_WRITE_DISCARD;
		g_dynamicVBCursor = 0;
	}
	D3D11_MAP ibMap = D3D11_MAP_WRITE_NO_OVERWRITE;
	if (g_dynamicFirstMap || g_dynamicIBCursor + idxBytes > g_dynamicIBBytes)
	{
		ibMap = D3D11_MAP_WRITE_DISCARD;
		g_dynamicIBCursor = 0;
	}
	g_dynamicFirstMap = false;

	D3D11_MAPPED_SUBRESOURCE vbMapped = {};
	if (FAILED(ctx->Map(g_dynamicVB, 0, vbMap, 0, &vbMapped)))
		return;
	memcpy((Byte*)vbMapped.pData + g_dynamicVBCursor, geometry->GetVertex(0), vtxBytes);
	ctx->Unmap(g_dynamicVB, 0);

	D3D11_MAPPED_SUBRESOURCE ibMapped = {};
	if (FAILED(ctx->Map(g_dynamicIB, 0, ibMap, 0, &ibMapped)))
		return;
	short temp[3];
	UINT16* dstIdx = (UINT16*)((Byte*)ibMapped.pData + g_dynamicIBCursor);
	for (Int p = 0; p < numPolys; p++)
	{
		geometry->GetPolygonIndex(p, temp);
		dstIdx[p * 3 + 0] = (UINT16)temp[0];
		dstIdx[p * 3 + 1] = (UINT16)temp[1];
		dstIdx[p * 3 + 2] = (UINT16)temp[2];
	}
	ctx->Unmap(g_dynamicIB, 0);

	UINT startVertex = g_dynamicVBCursor / vertStride;
	UINT startIndex  = g_dynamicIBCursor / idxStride;

	UINT stride = vertStride;
	UINT offset = 0;
	ctx->IASetVertexBuffers(0, 1, &g_dynamicVB, &stride, &offset);
	ctx->IASetIndexBuffer(g_dynamicIB, DXGI_FORMAT_R16_UINT, 0);
	g_lastBoundVB = g_dynamicVB;

	SetVolShadowUploadWorld(dev, meshXform);

	ctx->DrawIndexed((UINT)numIndex, startIndex, (INT)startVertex);

	g_dynamicVBCursor += vtxBytes;
	g_dynamicIBCursor += idxBytes;
#endif
}

// ---------------------------------------------------------------------------
// Update — called per shadow per frame. Decides whether a re-silhouette
// pass is needed based on mesh rotation + light movement history.
// ---------------------------------------------------------------------------
void W3DVolumetricShadow::Update()
{
	static Vector3 originCompareVector(0, 0, 0);
	Vector3 pos;

	if (m_geometry == nullptr || m_robj == nullptr)
		return;

	pos = m_robj->Get_Position();
	if (pos == originCompareVector)
		return;

	Real groundHeight;
	if (TheTerrainLogic)
		groundHeight = TheTerrainLogic->getGroundHeight(pos.X, pos.Y);
	else
		groundHeight = 0.0f;

	if (fabs(pos.Z - groundHeight) >= AIRBORNE_UNIT_GROUND_DELTA)
	{
		Real extent = MAX_SHADOW_LENGTH_EXTRA_AIRBORNE_SCALE_FACTOR * m_robjExtent;
		if (fabs(pos.X - bcX) > (beX + extent) ||
			fabs(pos.Y - bcY) > (beY + extent) ||
			fabs(pos.Z - bcZ) > (beZ + extent))
			return;

		Real minTerrainHeight = groundHeight;
#ifdef BUILD_WITH_D3D11
		minTerrainHeight = GetTerrainMinHeightWorld();
#endif
		updateVolumes(fabs(pos.Z - minTerrainHeight) + SHADOW_EXTRUSION_BUFFER);
	}
	else
	{
		if (fabs(pos.X - bcX) > (beX + m_robjExtent) ||
			fabs(pos.Y - bcY) > (beY + m_robjExtent) ||
			fabs(pos.Z - bcZ) > (beZ + m_robjExtent))
			return;

		if (!m_extraExtrusionPadding)
			updateOptimalExtrusionPadding();
		updateVolumes(m_extraExtrusionPadding);
	}
}

void W3DVolumetricShadow::updateVolumes(Real zoffset)
{
	Int i, j;

	HLodClass* hlod = (HLodClass*)m_robj;
	MeshClass* mesh;
	Int meshIndex;

	DEBUG_ASSERTCRASH(hlod != nullptr, ("updateVolumes : hlod is NULL!"));

	for (i = 0; i < MAX_SHADOW_LIGHTS; i++)
	{
		for (j = 0; j < m_geometry->getMeshCount(); j++)
		{
			meshIndex = m_geometry->getMesh(j)->m_meshRobjIndex;

			if (meshIndex >= 0)
				mesh = (MeshClass*)hlod->Peek_Lod_Model(0, meshIndex);
			else
				mesh = (MeshClass*)m_robj;

			if (mesh)
			{
				if (!mesh->Is_Not_Hidden_At_All())
					continue;

				updateMeshVolume(j, i, &mesh->Get_Transform(), mesh->Get_Bounding_Box(), m_robj->Get_Position().Z - zoffset);

				if (m_shadowVolume[i][j])
				{
					// DX11: frustum test against shadowCameraFrustum not
					// ported yet — assume visible and let the volume
					// render; the stencil pass culls the trivial cases.
					if (m_shadowVolume[i][j]->getVisibleState() == Geometry::STATE_UNKNOWN)
						m_shadowVolume[i][j]->setVisibleState(Geometry::STATE_VISIBLE);

					if (m_shadowVolume[i][j]->getVisibleState() == Geometry::STATE_VISIBLE)
					{
						W3DBufferManager::W3DVertexBufferSlot* vbSlot = m_shadowVolumeVB[i][j];
						if (vbSlot)
						{
							W3DBufferManager::W3DRenderTask* oldTask = vbSlot->m_VB->m_renderTaskList;
							vbSlot->m_VB->m_renderTaskList = &m_shadowVolumeRenderTask[i][j];
							vbSlot->m_VB->m_renderTaskList->m_nextTask = oldTask;
						}
						else
						{
							TheW3DVolumetricShadowManager->addDynamicShadowTask(&m_shadowVolumeRenderTask[i][j]);
						}
					}
				}
			}
		}
	}
}

void W3DVolumetricShadow::updateMeshVolume(Int meshIndex, Int lightIndex, const Matrix3D* meshXform, const AABoxClass& meshBox, float floorZ)
{
	Vector3 lightPosObject;
	Matrix4x4 worldToObject;
	Vector3 objectCenter;
	Vector3 toLight, toPrevLight;
	Vector3 lightPosWorld;
	Bool isMeshRotating = false;
	Bool isLightMoving = false;

	Matrix4x4 objectToWorld(*meshXform);

	Matrix4x4* prevXForm = &m_objectXformHistory[lightIndex][meshIndex];

	// Normalized-axis orientation comparison (CNC3 variant).
	Vector3 va = (Vector3&)(*prevXForm)[0];
	Vector3 vb = (Vector3&)objectToWorld[0];
	va.Normalize(); vb.Normalize();
	Real cosAngle = WWMath::Fabs(Vector3::Dot_Product(va, vb));

	if (cosAngle >= cosAngleToCare)
	{
		va = (Vector3&)(*prevXForm)[1];
		vb = (Vector3&)objectToWorld[1];
		va.Normalize(); vb.Normalize();
		cosAngle = WWMath::Fabs(Vector3::Dot_Product(va, vb));
		if (cosAngle >= cosAngleToCare)
		{
			va = (Vector3&)(*prevXForm)[2];
			vb = (Vector3&)objectToWorld[2];
			va.Normalize(); vb.Normalize();
			cosAngle = WWMath::Fabs(Vector3::Dot_Product(va, vb));
			if (cosAngle < cosAngleToCare)
				isMeshRotating = true;
		}
		else
			isMeshRotating = true;
	}
	else
		isMeshRotating = true;

	lightPosWorld = TheW3DShadowManager->getLightPosWorld(lightIndex);
	meshXform->Get_Translation(&objectCenter);

	if (m_shadowLengthScale)
	{
		Real lightXYDistance = sqrt(lightPosWorld.X * lightPosWorld.X + lightPosWorld.Y * lightPosWorld.Y);
		Real newZ = lightXYDistance * m_shadowLengthScale;
		if (newZ > lightPosWorld.Z)
			lightPosWorld.Z = newZ;
	}

	if (lightPosWorld != m_lightPosHistory[lightIndex][meshIndex])
	{
		toLight = objectCenter - lightPosWorld;
		toLight.Normalize();
		toPrevLight = objectCenter - m_lightPosHistory[lightIndex][meshIndex];
		toPrevLight.Normalize();
		Real cosAngle2 = fabs(Vector3::Dot_Product(toLight, toPrevLight));
		if (cosAngle2 < cosAngleToCare)
			isLightMoving = true;
	}
	else if (fabs(objectCenter.Z - prevXForm->operator[](2).W) > SHADOW_EXTRUSION_BUFFER)
		isLightMoving = true;

	if (isLightMoving || isMeshRotating)
	{
		// Transform light into object space.
		Matrix4x4 inv;
		Matrix4x4::Inverse(&inv, nullptr, &objectToWorld);
		worldToObject = inv;
		Matrix4x4::Transform_Vector(worldToObject, lightPosWorld, &lightPosObject);

		// Build volume extrusion bounds by extruding the top face of meshBox.
		AABoxClass box(meshBox);
		SphereClass sphere;
		Vector3 Corners[8];
		Vector3 lightRay;
		Real vectorScale, vectorScaleTemp, vectorScaleMax;
		Real length;

		Corners[0] = box.Center + box.Extent;
		Corners[1] = Corners[0]; Corners[1].X -= 2.0f * box.Extent.X;
		Corners[2] = Corners[1]; Corners[2].Y -= 2.0f * box.Extent.Y;
		Corners[3] = Corners[2]; Corners[3].X += 2.0f * box.Extent.X;

		lightRay = Corners[0] - lightPosWorld; length = 1.0f / lightRay.Length(); lightRay *= length;
		vectorScaleMax = vectorScale = (Real)fabs((Corners[0].Z - floorZ) / lightRay.Z);
		Corners[4] = Corners[0] + lightRay * vectorScale;
		vectorScaleMax *= length;

		lightRay = Corners[1] - lightPosWorld; length = 1.0f / lightRay.Length(); lightRay *= length;
		vectorScaleTemp = (Real)fabs((Corners[1].Z - floorZ) / lightRay.Z);
		Corners[5] = Corners[1] + lightRay * vectorScaleTemp;
		vectorScaleTemp *= length;
		if (vectorScaleTemp > vectorScaleMax) vectorScaleMax = vectorScaleTemp;

		lightRay = Corners[2] - lightPosWorld; length = 1.0f / lightRay.Length(); lightRay *= length;
		vectorScale = (Real)fabs((Corners[2].Z - floorZ) / lightRay.Z);
		Corners[6] = Corners[2] + lightRay * vectorScale;
		vectorScale *= length;
		if (vectorScale > vectorScaleMax) vectorScaleMax = vectorScale;

		lightRay = Corners[3] - lightPosWorld; length = 1.0f / lightRay.Length(); lightRay *= length;
		vectorScaleTemp = (Real)fabs((Corners[3].Z - floorZ) / lightRay.Z);
		Corners[7] = Corners[3] + lightRay * vectorScaleTemp;
		vectorScaleTemp *= length;
		if (vectorScaleTemp > vectorScaleMax) vectorScaleMax = vectorScaleTemp;

		box.Init(Corners, 8);
		sphere.Init(box.Center, box.Extent.Length());

		// DX11: visibility test against shadowCameraFrustum not ported — always
		// rebuild and mark visible so downstream renders the volume.
		if (m_numSilhouetteIndices[meshIndex] != 0)
			m_geometry->getMesh(meshIndex)->buildPolygonNormals();

		resetSilhouette(meshIndex);
		buildSilhouette(meshIndex, &lightPosObject);

		if (!m_shadowVolume[lightIndex][meshIndex])
			allocateShadowVolume(lightIndex, meshIndex);

		if (m_shadowVolumeVB[lightIndex][meshIndex])
		{
			if (isMeshRotating || isLightMoving)
			{
				if (isMeshRotating)
				{
					m_shadowVolume[lightIndex][meshIndex]->SetFlags(
						m_shadowVolume[lightIndex][meshIndex]->GetFlags() | SHADOW_DYNAMIC);
				}
				resetShadowVolume(lightIndex, meshIndex);
				allocateShadowVolume(lightIndex, meshIndex);
			}
		}

		if (m_shadowVolume[lightIndex][meshIndex]->GetFlags() & SHADOW_DYNAMIC)
			constructVolume(&lightPosObject, vectorScaleMax, lightIndex, meshIndex);
		else
			constructVolumeVB(&lightPosObject, vectorScaleMax, lightIndex, meshIndex);

		m_objectXformHistory[lightIndex][meshIndex] = objectToWorld;
		m_lightPosHistory[lightIndex][meshIndex] = lightPosWorld;

		box.Translate(-objectCenter);
		m_shadowVolume[lightIndex][meshIndex]->setBoundingBox(box);
		sphere.Center -= objectCenter;
		m_shadowVolume[lightIndex][meshIndex]->setBoundingSphere(sphere);
		m_shadowVolume[lightIndex][meshIndex]->setVisibleState(Geometry::STATE_VISIBLE);
	}
	else
	{
		if (m_shadowVolume[lightIndex][meshIndex])
			m_shadowVolume[lightIndex][meshIndex]->setVisibleState(Geometry::STATE_UNKNOWN);
	}
}

// ---------------------------------------------------------------------------
// Silhouette construction — direct port of the original.
// ---------------------------------------------------------------------------
void W3DVolumetricShadow::addSilhouetteEdge(Int meshIndex, PolyNeighbor* visible, PolyNeighbor* hidden)
{
	Int i;
	Int neighborIndex = 0;
	Short visibleIndexList[3];
	Short edgeStart, edgeEnd;

	W3DShadowGeometryMesh* geomMesh = m_geometry->getMesh(meshIndex);

	for (i = 0; i < MAX_POLYGON_NEIGHBORS; i++)
	{
		if (visible->neighbor[i].neighborIndex == hidden->myIndex)
		{
			neighborIndex = i;
			break;
		}
	}

	geomMesh->GetPolygonIndex(visible->myIndex, visibleIndexList);

	if ((visibleIndexList[0] != visible->neighbor[neighborIndex].neighborEdgeIndex[0]) &&
		(visibleIndexList[0] != visible->neighbor[neighborIndex].neighborEdgeIndex[1]))
	{
		edgeStart = visibleIndexList[1];
		edgeEnd = visibleIndexList[2];
	}
	else if ((visibleIndexList[1] != visible->neighbor[neighborIndex].neighborEdgeIndex[0]) &&
			 (visibleIndexList[1] != visible->neighbor[neighborIndex].neighborEdgeIndex[1]))
	{
		edgeStart = visibleIndexList[2];
		edgeEnd = visibleIndexList[0];
	}
	else
	{
		edgeStart = visibleIndexList[0];
		edgeEnd = visibleIndexList[1];
	}

	addSilhouetteIndices(meshIndex, edgeStart, edgeEnd);
}

void W3DVolumetricShadow::addNeighborlessEdges(Int meshIndex, PolyNeighbor* us)
{
	Short vertexIndexList[3];
	Int i, j;
	Short edgeStart, edgeEnd;
	Bool addEdge;

	W3DShadowGeometryMesh* geomMesh = m_geometry->getMesh(meshIndex);
	geomMesh->GetPolygonIndex(us->myIndex, vertexIndexList);

	for (i = 0; i < 3; i++)
	{
		edgeStart = vertexIndexList[i];
		edgeEnd = (i == 2) ? vertexIndexList[0] : vertexIndexList[i + 1];

		addEdge = TRUE;
		for (j = 0; j < MAX_POLYGON_NEIGHBORS; j++)
		{
			if (us->neighbor[j].neighborIndex != NO_NEIGHBOR)
			{
				if ((us->neighbor[j].neighborEdgeIndex[0] == edgeStart &&
					 us->neighbor[j].neighborEdgeIndex[1] == edgeEnd) ||
					(us->neighbor[j].neighborEdgeIndex[1] == edgeStart &&
					 us->neighbor[j].neighborEdgeIndex[0] == edgeEnd))
				{
					addEdge = FALSE;
					break;
				}
			}
		}

		if (addEdge == TRUE)
			addSilhouetteIndices(meshIndex, edgeStart, edgeEnd);
	}
}

void W3DVolumetricShadow::addSilhouetteIndices(Int meshIndex, Short edgeStart, Short edgeEnd)
{
	if (m_numSilhouetteIndices[meshIndex] + 1 >= m_maxSilhouetteEntries[meshIndex])
		return;
	m_silhouetteIndex[meshIndex][m_numSilhouetteIndices[meshIndex]++] = edgeStart;
	m_silhouetteIndex[meshIndex][m_numSilhouetteIndices[meshIndex]++] = edgeEnd;
}

void W3DVolumetricShadow::buildSilhouette(Int meshIndex, Vector3* lightPosObject)
{
	PolyNeighbor* polyNeighbor;
	Vector3 lightVector;
	Bool visibleNeighborless;
	Int numPolys;
	W3DShadowGeometryMesh* geomMesh;
	Int i, j;
	Int meshEdgeStart = 0;

	geomMesh = m_geometry->getMesh(meshIndex);
	meshEdgeStart = m_numSilhouetteIndices[meshIndex];

	numPolys = geomMesh->GetNumPolygon();
	for (i = 0; i < numPolys; i++)
	{
		Short poly[3];
		polyNeighbor = geomMesh->GetPolyNeighbor(i);
		if (!polyNeighbor) continue;
		polyNeighbor->status = 0;

		const Vector3& normal = geomMesh->GetPolygonNormal(i);
		geomMesh->GetPolygonIndex(i, poly);
		const Vector3& vertex = geomMesh->GetVertex(poly[0]);
		lightVector = vertex - *lightPosObject;

		if (Vector3::Dot_Product(lightVector, normal) < 0.0f)
			polyNeighbor->status |= POLY_VISIBLE;
	}

	for (i = 0; i < numPolys; i++)
	{
		PolyNeighbor* otherNeighbor;

		polyNeighbor = geomMesh->GetPolyNeighbor(i);
		if (!polyNeighbor) continue;
		visibleNeighborless = FALSE;

		for (j = 0; j < MAX_POLYGON_NEIGHBORS; j++)
		{
			otherNeighbor = nullptr;
			if (polyNeighbor->neighbor[j].neighborIndex != NO_NEIGHBOR)
			{
				otherNeighbor = geomMesh->GetPolyNeighbor(polyNeighbor->neighbor[j].neighborIndex);
				if (otherNeighbor && (otherNeighbor->status & POLY_PROCESSED))
					continue;
			}

			if (polyNeighbor->status & POLY_VISIBLE)
			{
				if (otherNeighbor == nullptr)
					visibleNeighborless = TRUE;
				else if ((otherNeighbor->status & POLY_VISIBLE) == 0)
					addSilhouetteEdge(meshIndex, polyNeighbor, otherNeighbor);
			}
			else if (otherNeighbor != nullptr && (otherNeighbor->status & POLY_VISIBLE))
			{
				addSilhouetteEdge(meshIndex, otherNeighbor, polyNeighbor);
			}
		}

		if (visibleNeighborless == TRUE)
			addNeighborlessEdges(meshIndex, polyNeighbor);

		polyNeighbor->status |= POLY_PROCESSED;
	}

	m_numIndicesPerMesh[meshIndex] = m_numSilhouetteIndices[meshIndex] - meshEdgeStart;
}

// ---------------------------------------------------------------------------
// constructVolume (dynamic path) — build into the in-memory Geometry buffer.
// Original strip-shared layout: each silhouette edge becomes two side-wall
// triangles sharing vertices with adjacent edges when possible. Open top /
// open bottom — tall (airborne) volumes can leak through the open bottom
// and show as grey pillars, but for ground-level casters the camera rays
// terminate at the ground before reaching the opening so shadows are
// correct.
// ---------------------------------------------------------------------------
void W3DVolumetricShadow::constructVolume(Vector3* lightPosObject, Real shadowExtrudeDistance, Int volumeIndex, Int meshIndex)
{
	Geometry* shadowVolume;
	Vector3 extrude2;
	Short indexList[3];
	Int i, k;
	Int vertexCount;
	Int polygonCount;
	Int indicesPerMesh;
	W3DShadowGeometryMesh* geomMesh;

	if (volumeIndex < 0 || volumeIndex >= MAX_SHADOW_LIGHTS || lightPosObject == nullptr)
		return;

	shadowVolume = m_shadowVolume[volumeIndex][meshIndex];
	if (shadowVolume == nullptr)
		return;

	vertexCount = 0;
	polygonCount = 0;

	indicesPerMesh = m_numIndicesPerMesh[meshIndex];
	if (!indicesPerMesh)
		return;

	geomMesh = m_geometry->getMesh(meshIndex);

	shadowVolume->SetNumActivePolygon(0);
	shadowVolume->SetNumActiveVertex(0);

	Short* silhouetteIndices = m_silhouetteIndex[meshIndex];

	Short stripStartIndex = silhouetteIndices[0];
	Short stripStartVertex = 0;

	const Vector3& ev2 = geomMesh->GetVertex(silhouetteIndices[0]);
	extrude2 = ev2 - *lightPosObject;
	extrude2 *= shadowExtrudeDistance;
	extrude2 += ev2;

	shadowVolume->SetVertex(vertexCount, &ev2);
	shadowVolume->SetVertex(vertexCount + 1, &extrude2);

	vertexCount = 2;
	Int lastEdgeVertex2Index = 0;
	Int lastExtrude2Index = 1;

	for (i = 0; i < indicesPerMesh; i += 2)
	{
		Short currentEdgeEnd = silhouetteIndices[i + 1];

		for (k = i + 2; k < indicesPerMesh; k += 2)
			if (silhouetteIndices[k] == currentEdgeEnd)
			{
				Int tempIndex = *(Int*)(&silhouetteIndices[i + 2]);
				*(Int*)&silhouetteIndices[i + 2] = *(Int*)&silhouetteIndices[k];
				*(Int*)&silhouetteIndices[k] = tempIndex;
				break;
			}

		if (k >= indicesPerMesh)
		{
			if (currentEdgeEnd == stripStartIndex)
			{
				indexList[0] = (Short)lastEdgeVertex2Index;
				indexList[1] = (Short)lastExtrude2Index;
				indexList[2] = (Short)stripStartVertex;
				shadowVolume->SetPolygonIndex(polygonCount, indexList);

				indexList[0] = (Short)stripStartVertex;
				indexList[1] = (Short)lastExtrude2Index;
				indexList[2] = (Short)(stripStartVertex + 1);
				shadowVolume->SetPolygonIndex(polygonCount + 1, indexList);
			}
			else
			{
				const Vector3& ev = geomMesh->GetVertex(currentEdgeEnd);
				shadowVolume->SetVertex(vertexCount, &ev);

				indexList[0] = (Short)lastEdgeVertex2Index;
				indexList[1] = (Short)lastExtrude2Index;
				indexList[2] = (Short)vertexCount;
				shadowVolume->SetPolygonIndex(polygonCount, indexList);

				extrude2 = ev - *lightPosObject;
				extrude2 *= shadowExtrudeDistance;
				extrude2 += ev;
				shadowVolume->SetVertex(vertexCount + 1, &extrude2);

				indexList[0] = (Short)vertexCount;
				indexList[1] = (Short)lastExtrude2Index;
				indexList[2] = (Short)(vertexCount + 1);
				shadowVolume->SetPolygonIndex(polygonCount + 1, indexList);

				lastEdgeVertex2Index = vertexCount;
				lastExtrude2Index = vertexCount + 1;
				vertexCount += 2;
			}

			if ((i + 2) >= indicesPerMesh)
			{
				polygonCount += 2;
				break;
			}

			const Vector3& ev = geomMesh->GetVertex(silhouetteIndices[i + 2]);
			extrude2 = ev - *lightPosObject;
			extrude2 *= shadowExtrudeDistance;
			extrude2 += ev;

			lastEdgeVertex2Index = vertexCount;
			lastExtrude2Index = vertexCount + 1;
			stripStartIndex = silhouetteIndices[i + 2];
			stripStartVertex = (Short)lastEdgeVertex2Index;

			shadowVolume->SetVertex(lastEdgeVertex2Index, &ev);
			shadowVolume->SetVertex(lastExtrude2Index, &extrude2);
			vertexCount += 2;
			polygonCount += 2;
			continue;
		}
		else
		{
			const Vector3& ev = geomMesh->GetVertex(currentEdgeEnd);
			shadowVolume->SetVertex(vertexCount, &ev);

			indexList[0] = (Short)lastEdgeVertex2Index;
			indexList[1] = (Short)lastExtrude2Index;
			indexList[2] = (Short)vertexCount;
			shadowVolume->SetPolygonIndex(polygonCount, indexList);

			extrude2 = ev - *lightPosObject;
			extrude2 *= shadowExtrudeDistance;
			extrude2 += ev;
			shadowVolume->SetVertex(vertexCount + 1, &extrude2);

			indexList[0] = (Short)vertexCount;
			indexList[1] = (Short)lastExtrude2Index;
			indexList[2] = (Short)(vertexCount + 1);
			shadowVolume->SetPolygonIndex(polygonCount + 1, indexList);

			lastEdgeVertex2Index = vertexCount;
			lastExtrude2Index = vertexCount + 1;
			vertexCount += 2;
			polygonCount += 2;
		}
	}

	// --- Top + bottom caps (same rationale as constructVolumeVB) ---
	// For N silhouette edges: 2 fan-center verts + 4N fresh verts (top
	// silh pair and bottom extruded pair per edge) + 2N cap triangles.
	// Only emits if the Geometry was allocated with enough room; otherwise
	// fall back to the open volume. Indexed writes below depend on
	// m_numVertex / m_numPolygon from the Create() call in
	// allocateShadowVolume.
	{
		const Int N = indicesPerMesh / 2;
		const Int capVerts  = 2 + 4 * N;
		const Int capPolys  = 2 * N;
		if (N > 0
			&& (vertexCount + capVerts) <= shadowVolume->GetNumVertex()
			&& (polygonCount + capPolys) <= shadowVolume->GetNumPolygon())
		{
			Vector3 centroid(0.0f, 0.0f, 0.0f);
			for (Int s = 0; s < indicesPerMesh; ++s)
				centroid += geomMesh->GetVertex(silhouetteIndices[s]);
			centroid /= (Real)indicesPerMesh;

			Vector3 centroidExtruded = centroid - *lightPosObject;
			centroidExtruded *= shadowExtrudeDistance;
			centroidExtruded += centroid;

			const Int topCenterIdx = vertexCount;
			const Int botCenterIdx = vertexCount + 1;
			shadowVolume->SetVertex(topCenterIdx, &centroid);
			shadowVolume->SetVertex(botCenterIdx, &centroidExtruded);

			for (Int e = 0; e < N; ++e)
			{
				const Short si = silhouetteIndices[e * 2];
				const Short ei = silhouetteIndices[e * 2 + 1];
				const Vector3& vs = geomMesh->GetVertex(si);
				const Vector3& ve = geomMesh->GetVertex(ei);

				Vector3 extS = vs - *lightPosObject;
				extS *= shadowExtrudeDistance;
				extS += vs;
				Vector3 extE = ve - *lightPosObject;
				extE *= shadowExtrudeDistance;
				extE += ve;

				const Int vBase = vertexCount + 2 + e * 4;
				shadowVolume->SetVertex(vBase + 0, &vs);
				shadowVolume->SetVertex(vBase + 1, &ve);
				shadowVolume->SetVertex(vBase + 2, &extS);
				shadowVolume->SetVertex(vBase + 3, &extE);

				// Top cap: (topCenter, silh_start, silh_end) — outward-up.
				indexList[0] = (Short)topCenterIdx;
				indexList[1] = (Short)(vBase + 0);
				indexList[2] = (Short)(vBase + 1);
				shadowVolume->SetPolygonIndex(polygonCount + e * 2 + 0, indexList);

				// Bottom cap: (botCenter, extrude_end, extrude_start) —
				// reversed winding for outward-down normal.
				indexList[0] = (Short)botCenterIdx;
				indexList[1] = (Short)(vBase + 3);
				indexList[2] = (Short)(vBase + 2);
				shadowVolume->SetPolygonIndex(polygonCount + e * 2 + 1, indexList);
			}

			vertexCount  += capVerts;
			polygonCount += capPolys;
		}
	}

	shadowVolume->SetNumActivePolygon(polygonCount);
	shadowVolume->SetNumActiveVertex(vertexCount);
}

// ---------------------------------------------------------------------------
// constructVolumeVB — static path. Allocates VB+IB slots via the buffer
// manager and writes extruded vertices straight in.
// ---------------------------------------------------------------------------
void W3DVolumetricShadow::constructVolumeVB(Vector3* lightPosObject, Real shadowExtrudeDistance, Int volumeIndex, Int meshIndex)
{
	Geometry* shadowVolume;
	Vector3 extrude2;
	Int i, k;
	Int vertexCount;
	Int polygonCount;
	Int indicesPerMesh;
	W3DShadowGeometryMesh* geomMesh;

	W3DBufferManager::W3DVertexBufferSlot* vbSlot;
	W3DBufferManager::W3DIndexBufferSlot*  ibSlot;

	if (volumeIndex < 0 || volumeIndex >= MAX_SHADOW_LIGHTS || lightPosObject == nullptr)
		return;

	shadowVolume = m_shadowVolume[volumeIndex][meshIndex];
	if (shadowVolume == nullptr)
		return;

	// First pass: determine exact vertex / polygon counts and sort edges
	// into strip order (consecutive edges share a vertex where possible).
	{
		vertexCount = 0;
		polygonCount = 0;
		indicesPerMesh = m_numIndicesPerMesh[meshIndex];
		if (!indicesPerMesh)
			return;

		Short* silhouetteIndices = m_silhouetteIndex[meshIndex];
		Short stripStartIndex = silhouetteIndices[0];
		Short stripStartVertex = 0;

		vertexCount = 2;
		Int lastEdgeVertex2Index = 0;
		Int lastExtrude2Index = 1;

		for (i = 0; i < indicesPerMesh; i += 2)
		{
			Short currentEdgeEnd = silhouetteIndices[i + 1];

			for (k = i + 2; k < indicesPerMesh; k += 2)
				if (silhouetteIndices[k] == currentEdgeEnd)
				{
					Int tempIndex = *(Int*)(&silhouetteIndices[i + 2]);
					*(Int*)&silhouetteIndices[i + 2] = *(Int*)&silhouetteIndices[k];
					*(Int*)&silhouetteIndices[k] = tempIndex;
					break;
				}

			if (k >= indicesPerMesh)
			{
				if (currentEdgeEnd == stripStartIndex)
				{
					// strip wraps closed — no new verts added
				}
				else
				{
					lastEdgeVertex2Index = vertexCount;
					lastExtrude2Index = vertexCount + 1;
					vertexCount += 2;
				}

				if ((i + 2) >= indicesPerMesh)
				{
					polygonCount += 2;
					break;
				}

				lastEdgeVertex2Index = vertexCount;
				lastExtrude2Index = vertexCount + 1;
				stripStartIndex = silhouetteIndices[i + 2];
				stripStartVertex = (Short)lastEdgeVertex2Index;

				vertexCount += 2;
				polygonCount += 2;
				continue;
			}
			else
			{
				lastEdgeVertex2Index = vertexCount;
				lastExtrude2Index = vertexCount + 1;
				vertexCount += 2;
				polygonCount += 2;
			}
		}
	}

	// DX11: top + bottom cap augmentation. The strip-based layout leaves
	// the volume open at top and bottom; for thin vertical pillars (sun
	// overhead on small-footprint casters), the camera sees strips where
	// near/far walls don't overlap in screen space, so INCR+DECR doesn't
	// cancel — visible grey pillars/boxes. Closing both ends with
	// fan-triangulated caps (silhouette centroid → edge pairs at the top,
	// extruded centroid → edge pairs at the bottom) makes the volume
	// topologically closed so every camera ray crosses exactly two faces.
	//
	// Per silhouette edge: 4 fresh verts (2 for top, 2 for bottom) + 2
	// cap triangles. Plus 2 fan-center verts (silhouette centroid and
	// its extrusion).
	const Int N = indicesPerMesh / 2;	// silhouette edge count
	const Int capVertCount  = 2 + 4 * N;
	const Int capPolyCount  = 2 * N;
	const Int totalVertCount = vertexCount + capVertCount;
	const Int totalPolyCount = polygonCount + capPolyCount;

	DEBUG_ASSERTCRASH(m_shadowVolumeVB[volumeIndex][meshIndex] == nullptr, ("Updating Existing Static Vertex Buffer Shadow"));
	vbSlot = m_shadowVolumeVB[volumeIndex][meshIndex] = TheW3DBufferManager->getSlot(W3DBufferManager::VBM_FVF_XYZ, totalVertCount);

	DEBUG_ASSERTCRASH(m_shadowVolumeIB[volumeIndex][meshIndex] == nullptr, ("Updating Existing Static Index Buffer Shadow"));
	ibSlot = m_shadowVolumeIB[volumeIndex][meshIndex] = TheW3DBufferManager->getSlot(totalPolyCount * 3);

	// If the augmented slot didn't fit, fall back to the un-capped layout
	// (pillar artifacts reappear but the volume still renders).
	bool wantCaps = (vbSlot != nullptr && ibSlot != nullptr);
	if (!wantCaps)
	{
		if (ibSlot) TheW3DBufferManager->releaseSlot(ibSlot);
		if (vbSlot) TheW3DBufferManager->releaseSlot(vbSlot);
		vbSlot = m_shadowVolumeVB[volumeIndex][meshIndex] = TheW3DBufferManager->getSlot(W3DBufferManager::VBM_FVF_XYZ, vertexCount);
		ibSlot = m_shadowVolumeIB[volumeIndex][meshIndex] = TheW3DBufferManager->getSlot(polygonCount * 3);
	}

	if (!ibSlot || !vbSlot)
	{
		if (ibSlot) TheW3DBufferManager->releaseSlot(ibSlot);
		if (vbSlot) TheW3DBufferManager->releaseSlot(vbSlot);
		m_shadowVolumeIB[volumeIndex][meshIndex] = nullptr;
		m_shadowVolumeVB[volumeIndex][meshIndex] = nullptr;
		return;
	}

	geomMesh = m_geometry->getMesh(meshIndex);

#ifdef BUILD_WITH_D3D11
	Vector3* vb = (Vector3*)TheW3DBufferManager->mapSlot(vbSlot, FALSE);
	if (vb == nullptr)
		return;

	UINT16* ib = (UINT16*)TheW3DBufferManager->mapSlot(ibSlot, FALSE);
	if (ib == nullptr)
	{
		TheW3DBufferManager->unmapSlot(vbSlot);
		return;
	}
#else
	Vector3* vb = nullptr;
	UINT16* ib = nullptr;
	return;
#endif

	shadowVolume->SetNumActivePolygon(wantCaps ? totalPolyCount : polygonCount);
	shadowVolume->SetNumActiveVertex(wantCaps ? totalVertCount : vertexCount);

	// Save base pointers so the bottom-cap pass can write into fixed offsets
	// past the strip output without tracking pointer arithmetic through the
	// strip loop. Strip code below keeps advancing `vb` / `ib`.
	Vector3* const vbBase = vb;
	UINT16*  const ibBase = ib;

	Short* silhouetteIndices = m_silhouetteIndex[meshIndex];
	Short stripStartIndex = silhouetteIndices[0];
	Short stripStartVertex = 0;

	// Seed strip with first vertex and its extruded partner.
	const Vector3& ev = geomMesh->GetVertex(silhouetteIndices[0]);
	extrude2 = ev - *lightPosObject;
	extrude2 *= shadowExtrudeDistance;
	extrude2 += ev;
	*vb++ = ev;
	*vb++ = extrude2;

	vertexCount = 2;
	polygonCount = 0;
	Int lastEdgeVertex2Index = 0;
	Int lastExtrude2Index = 1;

	for (i = 0; i < m_numIndicesPerMesh[meshIndex]; i += 2)
	{
		Short currentEdgeEnd = silhouetteIndices[i + 1];

		if (((i + 2) >= m_numIndicesPerMesh[meshIndex]) || silhouetteIndices[i + 2] != currentEdgeEnd)
		{
			if (currentEdgeEnd == stripStartIndex)
			{
				ib[0] = (UINT16)lastEdgeVertex2Index;
				ib[4] = ib[1] = (UINT16)lastExtrude2Index;
				ib[3] = ib[2] = (UINT16)stripStartVertex;
				ib[5] = (UINT16)(stripStartVertex + 1);
				ib += 6;
			}
			else
			{
				const Vector3& ev2 = geomMesh->GetVertex(currentEdgeEnd);
				*vb++ = ev2;

				ib[0] = (UINT16)lastEdgeVertex2Index;
				ib[4] = ib[1] = (UINT16)lastExtrude2Index;
				ib[3] = ib[2] = (UINT16)vertexCount;
				ib[5] = (UINT16)(vertexCount + 1);
				ib += 6;

				extrude2 = ev2 - *lightPosObject;
				extrude2 *= shadowExtrudeDistance;
				extrude2 += ev2;
				*vb++ = extrude2;

				lastEdgeVertex2Index = vertexCount;
				lastExtrude2Index = vertexCount + 1;
				vertexCount += 2;
			}

			if ((i + 2) >= m_numIndicesPerMesh[meshIndex])
			{
				polygonCount += 2;
				break;
			}

			const Vector3& evb = geomMesh->GetVertex(silhouetteIndices[i + 2]);
			extrude2 = evb - *lightPosObject;
			extrude2 *= shadowExtrudeDistance;
			extrude2 += evb;

			lastEdgeVertex2Index = vertexCount;
			lastExtrude2Index = vertexCount + 1;
			stripStartIndex = silhouetteIndices[i + 2];
			stripStartVertex = (Short)lastEdgeVertex2Index;

			*vb++ = evb;
			*vb++ = extrude2;
			vertexCount += 2;
			polygonCount += 2;
			continue;
		}
		else
		{
			const Vector3& ev2 = geomMesh->GetVertex(currentEdgeEnd);
			*vb++ = ev2;

			ib[0] = (UINT16)lastEdgeVertex2Index;
			ib[4] = ib[1] = (UINT16)lastExtrude2Index;
			ib[3] = ib[2] = (UINT16)vertexCount;
			ib[5] = (UINT16)(vertexCount + 1);
			ib += 6;

			extrude2 = ev2 - *lightPosObject;
			extrude2 *= shadowExtrudeDistance;
			extrude2 += ev2;
			*vb++ = extrude2;

			lastEdgeVertex2Index = vertexCount;
			lastExtrude2Index = vertexCount + 1;
			vertexCount += 2;
			polygonCount += 2;
		}
	}

#ifdef BUILD_WITH_D3D11
	// --- Top + bottom cap (fan from centroid and extruded centroid) ---
	// Closes both open ends so the volume is fully closed and every
	// camera ray that enters the volume crosses exactly two faces with
	// matching INCR+DECR. Vertex layout after the strip:
	//   [vertexCount + 0]             top fan center (silhouette centroid)
	//   [vertexCount + 1]             bottom fan center (extruded centroid)
	//   [vertexCount + 2 + 4e + 0]    silh_start (top)
	//   [vertexCount + 2 + 4e + 1]    silh_end   (top)
	//   [vertexCount + 2 + 4e + 2]    extrude_start (bottom)
	//   [vertexCount + 2 + 4e + 3]    extrude_end   (bottom)
	if (wantCaps && N > 0)
	{
		// Centroids in object space.
		Vector3 centroid(0.0f, 0.0f, 0.0f);
		for (Int s = 0; s < indicesPerMesh; ++s)
			centroid += geomMesh->GetVertex(silhouetteIndices[s]);
		centroid /= (Real)indicesPerMesh;

		Vector3 centroidExtruded = centroid - *lightPosObject;
		centroidExtruded *= shadowExtrudeDistance;
		centroidExtruded += centroid;

		const Int topCenterIdx = vertexCount;
		const Int botCenterIdx = vertexCount + 1;
		vbBase[topCenterIdx] = centroid;
		vbBase[botCenterIdx] = centroidExtruded;

		for (Int e = 0; e < N; ++e)
		{
			const Short si = silhouetteIndices[e * 2];
			const Short ei = silhouetteIndices[e * 2 + 1];
			const Vector3& vs = geomMesh->GetVertex(si);
			const Vector3& ve = geomMesh->GetVertex(ei);

			Vector3 extS = vs - *lightPosObject;
			extS *= shadowExtrudeDistance;
			extS += vs;
			Vector3 extE = ve - *lightPosObject;
			extE *= shadowExtrudeDistance;
			extE += ve;

			const Int vBase = vertexCount + 2 + e * 4;
			vbBase[vBase + 0] = vs;
			vbBase[vBase + 1] = ve;
			vbBase[vBase + 2] = extS;
			vbBase[vBase + 3] = extE;

			UINT16* iCap = ibBase + polygonCount * 3 + e * 6;
			// Top cap: (topCenter → silh_start → silh_end). For CCW
			// silhouette this projects CCW in screen from above → FrontFace
			// op = INCR (valid "entry" for top-down rays into the volume).
			iCap[0] = (UINT16)topCenterIdx;
			iCap[1] = (UINT16)(vBase + 0);
			iCap[2] = (UINT16)(vBase + 1);
			// Bottom cap: reversed winding for outward-down normal.
			iCap[3] = (UINT16)botCenterIdx;
			iCap[4] = (UINT16)(vBase + 3);
			iCap[5] = (UINT16)(vBase + 2);
		}
	}

	TheW3DBufferManager->unmapSlot(vbSlot);
	TheW3DBufferManager->unmapSlot(ibSlot);
#endif
}

Bool W3DVolumetricShadow::allocateShadowVolume(Int volumeIndex, Int meshIndex)
{
	Int numVertices, numPolygons;
	Geometry* shadowVolume;

	if (volumeIndex < 0 || volumeIndex >= MAX_SHADOW_LIGHTS)
		return FALSE;

	if ((shadowVolume = m_shadowVolume[volumeIndex][meshIndex]) == nullptr)
	{
		shadowVolume = NEW Geometry;
		m_shadowVolumeCount[meshIndex]++;
	}

	if (shadowVolume == nullptr)
	{
		m_shadowVolumeCount[meshIndex]--;
		return FALSE;
	}

	m_shadowVolume[volumeIndex][meshIndex] = shadowVolume;

	// DX11: Sized for strip side walls + top/bottom cap fans. For N
	// silhouette edges (maxEntries = 2N): strip uses ≤ 2N verts / 2N polys,
	// both caps together add 2 + 4N verts and 2N polys (top + bottom fan
	// from centroid). Total worst case: 6N+2 verts and 4N polys.
	// → numVertices = 3 * maxEntries + 2
	// → numPolygons = 2 * maxEntries
	const Int maxEntries = m_maxSilhouetteEntries[meshIndex];
	if (maxEntries <= 0)
	{
		delete shadowVolume;
		m_shadowVolume[volumeIndex][meshIndex] = nullptr;
		return FALSE;
	}
	numPolygons = maxEntries * 2;
	numVertices = maxEntries * 3 + 2;

	if (shadowVolume->GetFlags() & SHADOW_DYNAMIC)
	{
		if (shadowVolume->Create(numVertices, numPolygons) == FALSE)
		{
			delete shadowVolume;
			return FALSE;
		}
	}

	return TRUE;
}

void W3DVolumetricShadow::deleteShadowVolume(Int volumeIndex)
{
	if (volumeIndex < 0 || volumeIndex >= MAX_SHADOW_LIGHTS)
		return;

	for (Int meshIndex = 0; meshIndex < MAX_SHADOW_CASTER_MESHES; meshIndex++)
	{
		if (m_shadowVolume[volumeIndex][meshIndex])
		{
			delete m_shadowVolume[volumeIndex][meshIndex];
			m_shadowVolume[volumeIndex][meshIndex] = nullptr;
			m_shadowVolumeCount[meshIndex]--;
		}
	}
}

void W3DVolumetricShadow::resetShadowVolume(Int volumeIndex, Int meshIndex)
{
	Geometry* geometry;

	if (volumeIndex < 0 || volumeIndex >= MAX_SHADOW_LIGHTS)
		return;

	geometry = m_shadowVolume[volumeIndex][meshIndex];

	if (geometry)
	{
		if (m_shadowVolumeVB[volumeIndex][meshIndex])
		{
			TheW3DBufferManager->releaseSlot(m_shadowVolumeVB[volumeIndex][meshIndex]);
			m_shadowVolumeVB[volumeIndex][meshIndex] = nullptr;
		}
		if (m_shadowVolumeIB[volumeIndex][meshIndex])
		{
			TheW3DBufferManager->releaseSlot(m_shadowVolumeIB[volumeIndex][meshIndex]);
			m_shadowVolumeIB[volumeIndex][meshIndex] = nullptr;
		}
		geometry->Release();
	}
}

Bool W3DVolumetricShadow::allocateSilhouette(Int meshIndex, Int numVertices)
{
	Int numEntries = numVertices * 5;	// matches original HACK factor

	m_silhouetteIndex[meshIndex] = NEW short[numEntries];
	if (m_silhouetteIndex[meshIndex] == nullptr)
		return FALSE;

	m_numSilhouetteIndices[meshIndex] = 0;
	m_maxSilhouetteEntries[meshIndex] = (Short)numEntries;
	return TRUE;
}

void W3DVolumetricShadow::deleteSilhouette(Int meshIndex)
{
	if (m_silhouetteIndex[meshIndex])
		delete[] m_silhouetteIndex[meshIndex];
	m_silhouetteIndex[meshIndex] = nullptr;
	m_numSilhouetteIndices[meshIndex] = 0;
}

void W3DVolumetricShadow::resetSilhouette(Int meshIndex)
{
	m_numSilhouetteIndices[meshIndex] = 0;
}

// ---------------------------------------------------------------------------
// renderStencilShadows — fullscreen darkening pass.
// ---------------------------------------------------------------------------
void W3DVolumetricShadowManager::renderStencilShadows(void)
{
#ifdef BUILD_WITH_D3D11
	Render::Device& dev = Render::Renderer::Instance().GetDevice();
	ID3D11DeviceContext* ctx = dev.GetContext();
	if (!ctx || !g_svPipeline.darkenReady)
		return;

	UnsignedInt col = TheW3DShadowManager->getShadowColor();
	SvDarkenConstants dc = {};
	// Original shadow color is ARGB with 0xff for alpha — we use it as a
	// multiplicative tint, so a value like 0x7fa0a0a0 becomes (r=0.627,
	// g=0.627, b=0.627) which darkens the backbuffer slightly.
	dc.shadowColor[0] = (float)((col >> 16) & 0xFF) / 255.0f;
	dc.shadowColor[1] = (float)((col >>  8) & 0xFF) / 255.0f;
	dc.shadowColor[2] = (float)((col >>  0) & 0xFF) / 255.0f;
	dc.shadowColor[3] = 1.0f;

	D3D11_MAPPED_SUBRESOURCE m = {};
	if (SUCCEEDED(ctx->Map(g_svPipeline.darkenCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		memcpy(m.pData, &dc, sizeof(dc));
		ctx->Unmap(g_svPipeline.darkenCB, 0);
	}

	// Bind darken pipeline.
	g_svPipeline.darkenShader.Bind(dev);
	ctx->PSSetConstantBuffers(0, 1, &g_svPipeline.darkenCB);
	ctx->OMSetBlendState(g_svPipeline.blendMultiply, nullptr, 0xFFFFFFFF);
	ctx->OMSetDepthStencilState(g_svPipeline.dsDarken, 0x0);	// ref 0, NOT_EQUAL
	ctx->RSSetState(g_svPipeline.rasterNoCull);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	// Fullscreen tri is generated from SV_VertexID — no VB / no layout needed.
	ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	ctx->IASetInputLayout(nullptr);
	ctx->Draw(3, 0);
#endif
}

// ---------------------------------------------------------------------------
// renderShadows — the big orchestration. Walks all shadow casters, updates
// their silhouettes, runs two stencil passes (front incr / back decr), then
// renders the fullscreen darkening pass.
// ---------------------------------------------------------------------------
void W3DVolumetricShadowManager::renderShadows(Int projectionCount)
{
#ifdef BUILD_WITH_D3D11
	// Update scene-wide bounds — every frame, even if nothing to render, so
	// the Update() culling path has current values.
	// DX11: terrain max-visible-box helper not hooked up — use a large box
	// so nothing is culled out in error.
	bcX = bcY = bcZ = 0; beX = beY = beZ = 100000.0f;

	Render::Device& dev = Render::Renderer::Instance().GetDevice();
	if (!SetVolShadowCreateDeviceObjects(dev))
		return;

	ID3D11DeviceContext* ctx = dev.GetContext();

	// Reset dynamic cursor at frame start (mirrors the original's discard).
	g_dynamicVBCursor = 0;
	g_dynamicIBCursor = 0;
	g_dynamicFirstMap = true;

	Bool haveShadows = (m_shadowList != nullptr) && TheGlobalData->m_useShadowVolumes;

	if (haveShadows)
	{
		// Bind shared pipeline for stencil passes.
		g_svPipeline.extrudeShader.Bind(dev);
		ctx->OMSetBlendState(g_svPipeline.blendNoColor, nullptr, 0xFFFFFFFF);
		// Match the original DX8 path: ZPass stencil update (incr/decr on depth pass).
		// A global ZFail path requires robust closed volumes/caps and caused camera-
		// relative/front-layer artifacts in this port.
		ctx->OMSetDepthStencilState(
			g_svPipeline.dsExtrudeZPass ? g_svPipeline.dsExtrudeZPass : g_svPipeline.dsExtrudeZFail,
			0x0);
		ctx->RSSetState(g_svPipeline.rasterNoCull);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		g_lastBoundVB = nullptr;

		m_dynamicShadowVolumesToRender = nullptr;

		W3DVolumetricShadowRenderTask *shadowDynamicTasksStart, *shadowDynamicTask;
		W3DVolumetricShadow* shadow;

		// Phase 1 — update every shadow caster. Each Update() can enqueue
		// zero or more dynamic tasks; render them immediately so they reuse
		// the dynamic VB cursor.
		for (shadow = m_shadowList; shadow; shadow = shadow->m_next)
		{
			if (shadow->m_isEnabled && !shadow->m_isInvisibleEnabled)
			{
				shadowDynamicTasksStart = m_dynamicShadowVolumesToRender;
				shadow->Update();
				shadowDynamicTask = m_dynamicShadowVolumesToRender;
				while (shadowDynamicTask != shadowDynamicTasksStart)
				{
					shadow->RenderVolume(shadowDynamicTask->m_meshIndex, shadowDynamicTask->m_lightIndex);
					shadowDynamicTask = (W3DVolumetricShadowRenderTask*)shadowDynamicTask->m_nextTask;
				}
			}
		}

		// Phase 2 — drain static shadow tasks grouped by VB for locality.
		W3DBufferManager::W3DVertexBuffer* nextVb;
		W3DVolumetricShadowRenderTask* nextTask;
		for (nextVb = TheW3DBufferManager->getNextVertexBuffer(nullptr, W3DBufferManager::VBM_FVF_XYZ);
			 nextVb != nullptr;
			 nextVb = TheW3DBufferManager->getNextVertexBuffer(nextVb, W3DBufferManager::VBM_FVF_XYZ))
		{
			nextTask = (W3DVolumetricShadowRenderTask*)nextVb->m_renderTaskList;
			while (nextTask)
			{
				nextTask->m_parentShadow->RenderVolume(nextTask->m_meshIndex, nextTask->m_lightIndex);
				nextTask = (W3DVolumetricShadowRenderTask*)nextTask->m_nextTask;
			}
		}

		// DX11: with two-sided stencil, both front/back ops fire in one draw.

		// Flush any dynamic tasks queued but not yet drained.
		W3DVolumetricShadowRenderTask* dyn = m_dynamicShadowVolumesToRender;
		while (dyn)
		{
			dyn->m_parentShadow->RenderVolume(dyn->m_meshIndex, dyn->m_lightIndex);
			dyn = (W3DVolumetricShadowRenderTask*)dyn->m_nextTask;
		}

		// Reset render-task lists for next frame.
		for (nextVb = TheW3DBufferManager->getNextVertexBuffer(nullptr, W3DBufferManager::VBM_FVF_XYZ);
			 nextVb != nullptr;
			 nextVb = TheW3DBufferManager->getNextVertexBuffer(nextVb, W3DBufferManager::VBM_FVF_XYZ))
		{
			nextVb->m_renderTaskList = nullptr;
		}

		// Fullscreen darkening pass gated on the stencil count.
		renderStencilShadows();
	}
	else if (projectionCount)
	{
		// Even with no stencil volumes, fill the stencil buffer if projected
		// shadows expected it.
		renderStencilShadows();
	}

	// DX11: we bound VS@b1 (worldCB) and PS@b0 (darkenCB) directly, bypassing
	// the Renderer's m_objectCBBound/FlushFrameConstants path. Leaving them
	// bound to our own buffers is fine — the caller-side Restore3DState now
	// invalidates the Draw3D rebind cache so the next Draw3D will rebind
	// m_cbObject to VS/PS@b1 instead of trusting the stale cache. We DO
	// still drop VB/IB state since Draw3D sets its own VB each call and
	// there's no safety net for a stale pointer.
	{
		ID3D11Buffer* nullBuf = nullptr;
		UINT nullStride = 0, nullOffset = 0;
		ctx->IASetVertexBuffers(0, 1, &nullBuf, &nullStride, &nullOffset);
		ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_R16_UINT, 0);
		g_lastBoundVB = nullptr;
	}
	Render::Renderer::Instance().InvalidateObjectCBCache();
#else
	(void)projectionCount;
#endif
}

// ---------------------------------------------------------------------------
// invalidateCachedLightPositions — force all shadow volumes to rebuild.
// ---------------------------------------------------------------------------
void W3DVolumetricShadowManager::invalidateCachedLightPositions(void)
{
	if (!m_shadowList)
		return;

	Vector3 vec(0, 0, 0);
	for (W3DVolumetricShadow* shadow = m_shadowList; shadow; shadow = shadow->m_next)
	{
		for (Int i = 0; i < MAX_SHADOW_LIGHTS; i++)
		{
			for (Int meshIndex = 0; meshIndex < MAX_SHADOW_CASTER_MESHES; meshIndex++)
				shadow->setLightPosHistory(i, meshIndex, vec);
		}
	}
}

// ---------------------------------------------------------------------------
// Manager lifecycle
// ---------------------------------------------------------------------------
W3DVolumetricShadowManager::W3DVolumetricShadowManager(void)
	: m_shadowList(nullptr)
	, m_dynamicShadowVolumesToRender(nullptr)
{
	DEBUG_ASSERTCRASH(TheW3DBufferManager == nullptr, ("Duplicate W3DBufferManager construction"));
	TheW3DBufferManager = NEW W3DBufferManager;
	m_W3DShadowGeometryManager = NEW W3DShadowGeometryManager;
}

W3DVolumetricShadowManager::~W3DVolumetricShadowManager(void)
{
	removeAllShadows();
	ReleaseResources();
	delete m_W3DShadowGeometryManager;
	m_W3DShadowGeometryManager = nullptr;
	delete TheW3DBufferManager;
	TheW3DBufferManager = nullptr;
}

void W3DVolumetricShadowManager::ReleaseResources(void)
{
#ifdef BUILD_WITH_D3D11
	SetVolShadowReleaseDeviceObjects();
#endif
	if (TheW3DBufferManager)
	{
		TheW3DBufferManager->ReleaseResources();
		invalidateCachedLightPositions();
	}
}

Bool W3DVolumetricShadowManager::ReAcquireResources(void)
{
	if (TheW3DBufferManager && !TheW3DBufferManager->ReAcquireResources())
		return false;
	return true;
}

Bool W3DVolumetricShadowManager::init(void)
{
	return true;
}

void W3DVolumetricShadowManager::reset(void)
{
	removeAllShadows();
	if (m_W3DShadowGeometryManager)
		m_W3DShadowGeometryManager->Free_All_Geoms();
	if (TheW3DBufferManager)
		TheW3DBufferManager->freeAllBuffers();
}

void W3DVolumetricShadowManager::loadTerrainShadows(void)
{
	// DX11: DO_TERRAIN_SHADOW_VOLUMES was never enabled in the shipping
	// build; the patch path exists in the original but is a no-op.
}

W3DVolumetricShadow* W3DVolumetricShadowManager::addShadow(RenderObjClass* robj, Shadow::ShadowTypeInfo* shadowInfo, Drawable* draw)
{
	if (!robj || !TheGlobalData->m_useShadowVolumes)
		return nullptr;

	const char* name = robj->Get_Name();
	if (!name)
		return nullptr;

	W3DShadowGeometry* sg = m_W3DShadowGeometryManager->Get_Geom(name);
	if (sg == nullptr)
	{
		m_W3DShadowGeometryManager->Load_Geom(robj, name);
		sg = m_W3DShadowGeometryManager->Get_Geom(name);
		if (sg == nullptr)
			return nullptr;
	}

	W3DVolumetricShadow* shadow = NEW W3DVolumetricShadow;
	if (shadow == nullptr)
		return nullptr;

	shadow->setRenderObject(robj);
	shadow->SetGeometry(sg);

	SphereClass sphere;
	robj->Get_Obj_Space_Bounding_Sphere(sphere);
	shadow->setRenderObjExtent(sphere.Radius * MAX_SHADOW_LENGTH_SCALE_FACTOR);

	Real sunElevationAngleTan = 0;
	if (shadowInfo && shadowInfo->m_sizeX)
		sunElevationAngleTan = tan(shadowInfo->m_sizeX / 180.0f * 3.1415926535f);
	shadow->setShadowLengthScale(sunElevationAngleTan);

	if (!draw || !draw->isKindOf(KINDOF_IMMOBILE))
		shadow->setOptimalExtrusionPadding(SHADOW_EXTRUSION_BUFFER);

	shadow->m_next = m_shadowList;
	m_shadowList = shadow;
	return shadow;
}

void W3DVolumetricShadowManager::removeShadow(W3DVolumetricShadow* shadow)
{
	W3DVolumetricShadow* prev_shadow = nullptr;

	for (W3DVolumetricShadow* next_shadow = m_shadowList; next_shadow; prev_shadow = next_shadow, next_shadow = next_shadow->m_next)
	{
		if (next_shadow == shadow)
		{
			if (prev_shadow)
				prev_shadow->m_next = shadow->m_next;
			else
				m_shadowList = shadow->m_next;
			delete shadow;
			return;
		}
	}
}

void W3DVolumetricShadowManager::removeAllShadows(void)
{
	W3DVolumetricShadow* next_shadow = m_shadowList;
	m_shadowList = nullptr;

	for (W3DVolumetricShadow* cur_shadow = next_shadow; cur_shadow; cur_shadow = next_shadow)
	{
		next_shadow = cur_shadow->m_next;
		cur_shadow->m_next = nullptr;
		delete cur_shadow;
	}
}
